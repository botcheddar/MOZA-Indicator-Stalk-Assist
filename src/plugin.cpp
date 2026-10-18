#include "input.h"
#include "shared.h"
#include "scssdk_input.h"
#include "scssdk_telemetry.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "amtrucks/scssdk_ats.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include <memory>
#include <algorithm>
#ifdef MOZA_TESTING
#include "../tests/fake_input.h"
using DeviceInput = moza::FakeInput;
uint64_t pluginNow() {
    return moza::testNow;
}
bool pluginForeground() {
    return moza::testFocused;
}
#else
using DeviceInput = moza::Input;
uint64_t pluginNow() {
    return GetTickCount64();
}
bool pluginForeground() {
    return moza::foregroundProcess(GetCurrentProcessId());
}
#endif

namespace {
moza::Mapping mapping;
std::string gameLabel = "ETS2";
const wchar_t *pluginMutexName = L"Local\\ETS2MozaComfort_Plugin";
bool configureGame(const char *id) {
    if (!id)
        return false;
    bool ats = strcmp(id, SCS_GAME_ID_ATS) == 0;
    if (!ats && strcmp(id, SCS_GAME_ID_EUT2) != 0)
        return false;
    mapping.select(ats ? moza::ATSMapName : moza::MapName);
    gameLabel = ats ? "ATS" : "ETS2";
    pluginMutexName = ats ? L"Local\\ATSMozaComfort_Plugin" : L"Local\\ETS2MozaComfort_Plugin";
    return true;
}
moza::Engine engine;
moza::Lamps lamps;
moza::Diagnostics diag;
std::unique_ptr<DeviceInput> input;
HWND inputWindow = nullptr;
HANDLE singleton = nullptr;
bool telemetryInitialized = false, driving = false, inputActive = false, hazard = false,
     hazardKnown = false;
bool seen[2] = {false, false};
uint64_t lastFrame = 0, retryAt = 0, epoch = 0;
moza::Side previousOutput = moza::Side::None, frameOutput = moza::Side::None;
int nextInput = 0;
scs_log_t gameLog = nullptr;
void record(const std::string &text, uint64_t time = 0) {
    auto serial = ++diag.serial;
    auto &r = diag.records[(serial - 1) % 128];
    r.serial = serial;
    r.time = time ? time : GetTickCount64();
    snprintf(r.text, sizeof r.text, "%s", text.c_str());
}
void publish() {
    if (!mapping.open(false))
        return;
    diag.pid = GetCurrentProcessId();
    diag.heartbeat = GetTickCount64();
    diag.connected = input && input->connected();
    diag.telemetry = lamps.valid;
    diag.driving = driving;
    diag.output = int(engine.output());
    diag.position = int(engine.position);
    diag.flashes = engine.flashes;
    diag.elapsed = engine.elapsed;
    snprintf(diag.state, sizeof diag.state, "%s", engine.classification.c_str());
    snprintf(diag.reason, sizeof diag.reason, "%s", engine.reason.c_str());
    moza::publish(mapping.data->diagSeq, mapping.data->diag, diag);
}
void cycle() {
    auto now = pluginNow();
    moza::Command command;
    bool have = mapping.open(false) &&
                moza::snapshot(mapping.data->commandSeq, mapping.data->command, command);
    if (have && command.protocol == moza::Protocol && command.epoch != epoch) {
        epoch = command.epoch;
        engine.reset("Helper session changed");
        engine.cfg = command.config;
        // A pending physical event from before arming may never become a new comfort sequence.
        if (input)
            input->close();
        retryAt = 0;
    }
    bool frameGap = lastFrame && now - lastFrame > 750;
    lastFrame = now;
    if (!input)
        input = std::make_unique<DeviceInput>();
    if (!input->connected() && now >= retryAt) {
        retryAt = now + 2000;
        if (input->open(inputWindow)) {
            snprintf(diag.device, sizeof diag.device, "%s", input->selected.name.c_str());
            snprintf(diag.identity, sizeof diag.identity, "%s",
                     moza::guidText(input->selected.instance).c_str());
            engine.reset("Stalk connected; neutral history cleared");
            record("Connected " + input->selected.name);
        }
    }
    std::string interlock;
    if (!have || !moza::freshCommand(command, now))
        interlock = "Helper disabled or heartbeat expired";
    else if (!telemetryInitialized || !driving || !inputActive)
        interlock = "Game paused or input inactive";
    else if (!hazardKnown || hazard)
        interlock = "Hazards active or state unavailable";
    else if (engine.cfg.UseTelemetryBlinkCounting && !lamps.valid)
        interlock = "Physical lamp telemetry unavailable";
    else if (!pluginForeground())
        interlock = "Game focus lost";
    else if (!input->connected())
        interlock = "Stalk disconnected";
    else if (frameGap)
        interlock = "Game frame stalled; history discarded";
    bool allow = interlock.empty();
    if (allow && !engine.safe)
        engine.reset("Fresh safe session");
    engine.safety(allow, interlock);
    auto events = input->read(now);
    for (auto &e : events) {
        diag.lastButton = e.button;
        diag.eventTime = e.time;
        bool relevant = e.button == engine.cfg.LeftButton || e.button == engine.cfg.RightButton ||
                        e.button == engine.cfg.CentreButton;
        if (allow && relevant) {
            if (e.button == engine.cfg.LeftButton)
                engine.direction(moza::Side::Left, e.time);
            else if (e.button == engine.cfg.RightButton)
                engine.direction(moza::Side::Right, e.time);
            else
                engine.centre(e.time);
        }
        record("Button " + std::to_string(e.button) + "; " + engine.classification + "; elapsed=" +
                   std::to_string(engine.elapsed) + "ms" + (allow ? "" : "; output inhibited"),
               e.time);
    }
    engine.tick(now, lamps);
    auto out = engine.output();
    if (out != previousOutput) {
        record("Semantic hold=" + std::to_string(int(out)) +
               "; flashes=" + std::to_string(engine.flashes) + "; " + engine.reason);
        previousOutput = out;
    }
    frameOutput = out;
    publish();
}
SCSAPI_VOID activeCallback(const scs_u8_t active, const scs_context_t) {
    inputActive = active != 0;
    engine.safety(false, "Input activation changed");
    lastFrame = 0;
    frameOutput = moza::Side::None;
}
SCSAPI_RESULT inputCallback(scs_input_event_t *const event, const scs_u32_t flags,
                            const scs_context_t) {
    if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) {
        nextInput = 0;
        try {
            cycle();
        } catch (const std::exception &e) {
            engine.safety(false, e.what());
            frameOutput = moza::Side::None;
            if (input)
                input->close();
            retryAt = GetTickCount64() + 2000;
            record(std::string("ERROR: ") + e.what());
            if (gameLog)
                gameLog(SCS_LOG_TYPE_error, e.what());
            publish();
        } catch (...) {
            engine.safety(false, "Unknown input error");
            frameOutput = moza::Side::None;
            record("ERROR: unknown input error");
            publish();
        }
    }
    if (nextInput >= 2)
        return SCS_RESULT_not_found;
    event->input_index = nextInput;
    event->value_bool.value =
        (frameOutput == (nextInput == 0 ? moza::Side::Left : moza::Side::Right));
    ++nextInput;
    return SCS_RESULT_ok;
}
SCSAPI_VOID channel(const scs_string_t, const scs_u32_t, const scs_value_t *const value,
                    const scs_context_t context) {
    auto index = reinterpret_cast<uintptr_t>(context);
    if (index == 2) {
        hazardKnown = value != nullptr;
        hazard = value && value->value_bool.value;
        return;
    }
    bool on = value && value->value_bool.value;
    if (on && !lamps.on[index])
        ++lamps.rises[index];
    lamps.on[index] = on;
    seen[index] = value != nullptr;
    lamps.valid = seen[0] && seen[1];
}
SCSAPI_VOID telemetryEvent(const scs_event_t event, const void *const, const scs_context_t) {
    if (event == SCS_TELEMETRY_EVENT_started) {
        driving = true;
        engine.safety(false, "Driving resumed; fresh input required");
    } else if (event == SCS_TELEMETRY_EVENT_paused) {
        driving = false;
        engine.safety(false, "Game paused");
        frameOutput = moza::Side::None;
    }
}
} // namespace
extern "C" SCSAPI_RESULT scs_input_init(const scs_u32_t version,
                                        const scs_input_init_params_t *const params) {
    if (version != SCS_INPUT_VERSION_1_00)
        return SCS_RESULT_unsupported;
    auto p = static_cast<const scs_input_init_params_v100_t *>(params);
    if (!configureGame(p->common.game_id))
        return SCS_RESULT_unsupported;
    gameLog = p->common.log;
    try {
        singleton = CreateMutexW(nullptr, FALSE, pluginMutexName);
        if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (singleton)
                CloseHandle(singleton);
            singleton = nullptr;
            return SCS_RESULT_generic_error;
        }
        inputWindow = CreateWindowExA(0, "STATIC", "MOZA Comfort Input", WS_POPUP, 0, 0, 0, 0,
                                      nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
        if (!inputWindow)
            throw std::runtime_error("Could not create DirectInput window");
        engine = moza::Engine{};
        diag = moza::Diagnostics{};
        lastFrame = epoch = retryAt = 0;
        inputActive = false;
        previousOutput = frameOutput = moza::Side::None;
        const scs_input_device_input_t inputs[] = {
            {"lblinkerh", "Left Comfort Hold", SCS_VALUE_TYPE_bool, 0},
            {"rblinkerh", "Right Comfort Hold", SCS_VALUE_TYPE_bool, 0}};
        scs_input_device_t device = {};
        device.name = "moza_comfort_v1";
        device.display_name = "MOZA Comfort Indicators";
        device.type = SCS_INPUT_DEVICE_TYPE_semantical;
        device.input_count = 2;
        device.inputs = inputs;
        device.input_active_callback = activeCallback;
        device.input_event_callback = inputCallback;
        if (p->register_device(&device) != SCS_RESULT_ok)
            throw std::runtime_error("SCS semantic device registration failed");
        record(gameLabel + " SCS semantic hold device registered");
        gameLog(SCS_LOG_TYPE_message,
                "MOZA Comfort: semantic hold inputs registered; disabled until helper armed");
        return SCS_RESULT_ok;
    } catch (const std::exception &e) {
        if (gameLog)
            gameLog(SCS_LOG_TYPE_error, e.what());
        if (inputWindow)
            DestroyWindow(inputWindow);
        inputWindow = nullptr;
        if (singleton)
            CloseHandle(singleton);
        singleton = nullptr;
        return SCS_RESULT_generic_error;
    }
}
extern "C" SCSAPI_VOID scs_input_shutdown() {
    engine.safety(false, "SCS input shutdown");
    frameOutput = moza::Side::None;
    input.reset();
    publish();
    if (inputWindow)
        DestroyWindow(inputWindow);
    inputWindow = nullptr;
    if (singleton)
        CloseHandle(singleton);
    singleton = nullptr;
}
extern "C" SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,
                                            const scs_telemetry_init_params_t *const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01)
        return SCS_RESULT_unsupported;
    auto p = static_cast<const scs_telemetry_init_params_v100_t *>(params);
    if (!configureGame(p->common.game_id))
        return SCS_RESULT_unsupported;
    telemetryInitialized = false;
    gameLog = p->common.log;
    lamps = {};
    seen[0] = seen[1] = false;
    hazardKnown = hazard = driving = false;
    if (p->register_for_event(SCS_TELEMETRY_EVENT_started, telemetryEvent, nullptr) !=
        SCS_RESULT_ok)
        return SCS_RESULT_generic_error;
    if (p->register_for_event(SCS_TELEMETRY_EVENT_paused, telemetryEvent, nullptr) !=
        SCS_RESULT_ok) {
        p->unregister_from_event(SCS_TELEMETRY_EVENT_started);
        return SCS_RESULT_generic_error;
    }
    const char *names[] = {SCS_TELEMETRY_TRUCK_CHANNEL_light_lblinker,
                           SCS_TELEMETRY_TRUCK_CHANNEL_light_rblinker,
                           SCS_TELEMETRY_TRUCK_CHANNEL_hazard_warning};
    for (uintptr_t i = 0; i < 3; i++) {
        auto result = p->register_for_channel(names[i], SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                                              SCS_TELEMETRY_CHANNEL_FLAG_each_frame |
                                                  SCS_TELEMETRY_CHANNEL_FLAG_no_value,
                                              channel, reinterpret_cast<void *>(i));
        if (result != SCS_RESULT_ok) {
            for (uintptr_t previous = 0; previous < i; previous++)
                p->unregister_from_channel(names[previous], SCS_U32_NIL, SCS_VALUE_TYPE_bool);
            p->unregister_from_event(SCS_TELEMETRY_EVENT_started);
            p->unregister_from_event(SCS_TELEMETRY_EVENT_paused);
            gameLog(SCS_LOG_TYPE_error,
                    "MOZA Comfort: required blinker/hazard telemetry unavailable; plugin disabled");
            return SCS_RESULT_generic_error;
        }
    }
    telemetryInitialized = true;
    gameLog(SCS_LOG_TYPE_message, "MOZA Comfort: physical lamp telemetry registered");
    return SCS_RESULT_ok;
}
extern "C" SCSAPI_VOID scs_telemetry_shutdown() {
    telemetryInitialized = false;
    driving = false;
    lamps.valid = false;
    hazardKnown = false;
    engine.safety(false, "Telemetry shutdown");
    frameOutput = moza::Side::None;
}

#define MOZA_TESTING
#include "../src/plugin.cpp"
#include <cstdio>
#include <stdexcept>
using namespace moza;
namespace {
scs_input_device_t registeredDevice;
std::vector<std::string> inputNames;
scs_telemetry_channel_callback_t channels[3] = {};
scs_telemetry_event_callback_t events[8] = {};
SCSAPI_VOID logMock(scs_log_type_t, const char *) {}
SCSAPI_RESULT registerDevice(const scs_input_device_t *d) {
    registeredDevice = *d;
    inputNames.clear();
    for (unsigned i = 0; i < d->input_count; i++)
        inputNames.push_back(d->inputs[i].name);
    return SCS_RESULT_ok;
}
SCSAPI_RESULT registerEvent(scs_event_t event, scs_telemetry_event_callback_t cb, scs_context_t) {
    events[event] = cb;
    return SCS_RESULT_ok;
}
SCSAPI_RESULT unregisterEvent(scs_event_t event) {
    events[event] = nullptr;
    return SCS_RESULT_ok;
}
SCSAPI_RESULT registerChannel(scs_string_t, scs_u32_t, scs_value_type_t, scs_u32_t,
                              scs_telemetry_channel_callback_t cb, scs_context_t context) {
    channels[reinterpret_cast<uintptr_t>(context)] = cb;
    return SCS_RESULT_ok;
}
SCSAPI_RESULT unregisterChannel(scs_string_t, scs_u32_t, scs_value_type_t) {
    return SCS_RESULT_ok;
}
void expect(bool ok, const char *text) {
    if (!ok)
        throw std::runtime_error(text);
}
void lamp(int side, bool on) {
    scs_value_t v = {};
    v.type = SCS_VALUE_TYPE_bool;
    v.value_bool.value = on;
    channels[side]("test", SCS_U32_NIL, &v, reinterpret_cast<void *>(uintptr_t(side)));
}
Command c;
bool holds[2] = {};
void frame(uint64_t advance = 20) {
    testNow += advance;
    c.heartbeat = testNow;
    Mapping hostMap(gameLabel == "ATS" ? ATSMapName : MapName);
    expect(hostMap.open(true), "simulated helper mapping failed");
    publish(hostMap.data->commandSeq, hostMap.data->command, c);
    scs_input_event_t e = {};
    expect(registeredDevice.input_event_callback(&e, SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame,
                                                 nullptr) == SCS_RESULT_ok,
           "first input missing");
    expect(e.input_index == 0, "first index wrong");
    holds[0] = e.value_bool.value;
    expect(registeredDevice.input_event_callback(&e, 0, nullptr) == SCS_RESULT_ok,
           "second input missing");
    expect(e.input_index == 1, "second index wrong");
    holds[1] = e.value_bool.value;
    expect(registeredDevice.input_event_callback(&e, 0, nullptr) == SCS_RESULT_not_found,
           "unbounded callback events");
}
void button(int number, int advance = 100) {
    testEvents.push_back({number, testNow + uint64_t(advance)});
    frame(advance);
}
void ready() {
    testFocused = testConnected = true;
    testReadError = false;
    c.enabled = 1;
    c.epoch++;
    frame();
    registeredDevice.input_active_callback(1, nullptr);
    events[SCS_TELEMETRY_EVENT_started](SCS_TELEMETRY_EVENT_started, nullptr, nullptr);
    lamp(0, false);
    lamp(1, false);
    lamp(2, false);
    frame();
}
int passed = 0, failed = 0;
template <class F> void run(const char *name, F f) {
    try {
        f();
        printf("PASS %s\n", name);
        passed++;
    } catch (const std::exception &e) {
        printf("FAIL %s: %s\n", name, e.what());
        failed++;
    }
}
} // namespace
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Mapping etsHost;
    Mapping atsHost(ATSMapName);
    if (!etsHost.open(true) || !atsHost.open(true))
        return 2;
    if (!mapping.open(true))
        return 2;
    scs_input_init_params_v100_t ip = {};
    ip.common.game_id = "eut2";
    ip.common.log = logMock;
    ip.register_device = registerDevice;
    scs_telemetry_init_params_v100_t tp = {};
    tp.common.game_id = "eut2";
    tp.common.log = logMock;
    tp.register_for_event = registerEvent;
    tp.unregister_from_event = unregisterEvent;
    tp.register_for_channel = registerChannel;
    tp.unregister_from_channel = unregisterChannel;
    c.config.ComfortBlinkCount = 3;
    c.config.ComfortDurationMs = 2400;
    c.pid = GetCurrentProcessId();
    c.epoch = 1;
    c.enabled = 1;
    run("Unsupported SDK version rejected",
        [&] { expect(scs_input_init(0, &ip) == SCS_RESULT_unsupported, "version accepted"); });
    run("Other game rejected", [&] {
        ip.common.game_id = "unsupported_game";
        expect(scs_input_init(SCS_INPUT_VERSION_1_00, &ip) == SCS_RESULT_unsupported,
               "wrong game accepted");
        ip.common.game_id = "eut2";
    });
    run("Both APIs register against official ABI", [&] {
        expect(scs_input_init(SCS_INPUT_VERSION_1_00, &ip) == 0, "input init failed");
        expect(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01, &tp) == 0, "telemetry init failed");
        expect(inputNames == std::vector<std::string>{"lblinkerh", "rblinkerh"},
               "semantic names wrong");
        expect(registeredDevice.type == SCS_INPUT_DEVICE_TYPE_semantical, "wrong type");
    });
    run("No output before physical event", [] {
        ready();
        expect(!holds[0] && !holds[1], "output on startup");
    });
    run("Buffered left 10 then centre 9 yields left hold", [] {
        ready();
        button(10);
        lamp(0, true);
        button(9, 150);
        expect(holds[0] && !holds[1], "wrong side hold");
    });
    run("Immediate repeat direction releases and suppresses centre", [] {
        button(10);
        expect(!holds[0] && !holds[1], "old hold survived");
        button(9, 150);
        expect(!holds[0] && !holds[1], "repeat rearmed");
    });
    run("Opposite side switch clears old side", [] {
        ready();
        button(10);
        button(9, 150);
        button(8);
        expect(!holds[0] && !holds[1], "old side survived direction");
        button(9, 200);
        expect(!holds[0] && holds[1], "right hold missing");
    });
    run("Exact observed flashes end after third falling edge", [] {
        ready();
        button(10);
        lamp(0, true);
        button(9, 150);
        lamp(0, false);
        frame();
        lamp(0, true);
        frame();
        lamp(0, false);
        frame();
        lamp(0, true);
        frame();
        expect(holds[0], "third lit phase truncated");
        lamp(0, false);
        frame();
        expect(!holds[0] && !holds[1], "third fall did not release");
    });
    run("Normal detent creates no helper hold", [] {
        ready();
        button(8);
        frame(600);
        button(9);
        expect(!holds[0] && !holds[1], "normal return made comfort");
    });
    run("Focus loss clears comfort and pending history", [] {
        ready();
        button(10);
        button(9);
        testFocused = false;
        frame();
        expect(!holds[0] && !holds[1], "focus release missing");
        testFocused = true;
        button(9);
        expect(!holds[0] && !holds[1], "focus restored stale sequence");
    });
    run("Hazards preempt comfort", [] {
        ready();
        button(10);
        button(9);
        lamp(2, true);
        frame();
        expect(!holds[0] && !holds[1], "hazards did not release");
    });
    run("Unavailable hazard state inhibits", [] {
        ready();
        channels[2]("test", SCS_U32_NIL, nullptr, (void *)2);
        button(10);
        button(9);
        expect(!holds[0] && !holds[1], "unknown hazard permitted");
    });
    run("Pause clears comfort", [] {
        ready();
        button(8);
        button(9);
        events[SCS_TELEMETRY_EVENT_paused](SCS_TELEMETRY_EVENT_paused, nullptr, nullptr);
        frame();
        expect(!holds[0] && !holds[1], "pause release missing");
    });
    run("Disconnect error fails closed", [] {
        ready();
        button(8);
        button(9);
        testReadError = true;
        frame();
        expect(!holds[0] && !holds[1], "error release missing");
        testReadError = false;
        testNow += 3000;
    });
    run("Helper stop clears comfort", [] {
        ready();
        button(10);
        button(9);
        c.enabled = 0;
        frame();
        expect(!holds[0] && !holds[1], "stop release missing");
    });
    run("Heartbeat expiry and game stall fail closed", [] {
        ready();
        button(10);
        button(9);
        testNow += 1000;
        scs_input_event_t e = {};
        registeredDevice.input_event_callback(&e, SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame,
                                              nullptr);
        expect(!e.value_bool.value, "stale heartbeat retained hold");
    });
    run("No callback API ordering dependency", [&] {
        scs_input_shutdown();
        scs_telemetry_shutdown();
        expect(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_00, &tp) == 0, "telemetry first failed");
        expect(scs_input_init(SCS_INPUT_VERSION_1_00, &ip) == 0, "input second failed");
        ready();
        button(8);
        button(9, 150);
        expect(!holds[0] && holds[1], "reverse initialization wrong");
    });
    scs_telemetry_shutdown();
    scs_input_shutdown();
    run("ATS accepts both official SDK APIs", [&] {
        ip.common.game_id = SCS_GAME_ID_ATS;
        tp.common.game_id = SCS_GAME_ID_ATS;
        expect(scs_input_init(SCS_INPUT_VERSION_1_00, &ip) == 0, "ATS input rejected");
        expect(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01, &tp) == 0, "ATS telemetry rejected");
        expect(gameLabel == "ATS" &&
                   inputNames == std::vector<std::string>{"lblinkerh", "rblinkerh"},
               "ATS semantic device incorrect");
        c.config.ComfortBlinkCount = 5;
        c.config.ComfortDurationMs = 4000;
    });
    run("ATS and ETS2 commands and diagnostics remain isolated", [&] {
        Command old;
        old.pid = 111;
        old.epoch = 222;
        old.enabled = 0;
        publish(etsHost.data->commandSeq, etsHost.data->command, old);
        ready();
        button(10);
        button(9, 150);
        Command check;
        expect(snapshot(etsHost.data->commandSeq, etsHost.data->command, check) &&
                   check.pid == 111 && check.enabled == 0,
               "ATS changed ETS2 command memory");
        Diagnostics checkDiag;
        expect(snapshot(atsHost.data->diagSeq, atsHost.data->diag, checkDiag) &&
                   checkDiag.pid == GetCurrentProcessId(),
               "ATS diagnostic map not used");
        expect(holds[0] && !holds[1], "ATS isolated command not consumed");
    });
    for (int side = 0; side < 2; side++)
        run(side == 0 ? "ATS left completes exactly five total flashes"
                      : "ATS right completes exactly five total flashes",
            [=] {
                ready();
                button(side == 0 ? 10 : 8);
                lamp(side, true);
                button(9, 150);
                for (int n = 1; n <= 5; n++) {
                    if (n > 1) {
                        lamp(side, true);
                        frame(300);
                    }
                    expect(holds[side] && !holds[1 - side],
                           "wrong output or fifth lit phase cut short");
                    lamp(side, false);
                    frame(300);
                    expect(holds[side] == (n < 5), "ATS ended before/after fifth flash");
                }
            });
    run("ATS repeat cancels and opposite switches with five-flash config", [] {
        ready();
        button(10);
        button(9, 150);
        button(10);
        button(9, 150);
        expect(!holds[0] && !holds[1], "ATS repeat rearmed");
        button(10);
        button(9, 150);
        button(8);
        expect(!holds[0] && !holds[1], "ATS old direction stuck");
        button(9, 150);
        expect(!holds[0] && holds[1], "ATS opposite not started");
    });
    run("ATS telemetry-first reinitialization is supported", [&] {
        scs_input_shutdown();
        scs_telemetry_shutdown();
        expect(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_00, &tp) == 0,
               "ATS telemetry-first failed");
        expect(scs_input_init(SCS_INPUT_VERSION_1_00, &ip) == 0, "ATS input-second failed");
        ready();
        button(8);
        button(9, 150);
        expect(!holds[0] && holds[1], "ATS reverse initialization output wrong");
    });
    scs_input_shutdown();
    scs_telemetry_shutdown();
    run("ETS2 remains supported after ATS session", [&] {
        ip.common.game_id = SCS_GAME_ID_EUT2;
        tp.common.game_id = SCS_GAME_ID_EUT2;
        expect(scs_input_init(SCS_INPUT_VERSION_1_00, &ip) == 0, "ETS2 input regression");
        expect(scs_telemetry_init(SCS_TELEMETRY_VERSION_1_01, &tp) == 0,
               "ETS2 telemetry regression");
        ready();
        button(10);
        button(9, 150);
        expect(holds[0] && !holds[1], "ETS2 output regression");
    });
    scs_input_shutdown();
    scs_telemetry_shutdown();
    printf("RESULT: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

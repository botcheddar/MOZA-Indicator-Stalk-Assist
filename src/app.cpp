#define WIN32_LEAN_AND_MEAN
#include "beam_bridge.h"
#include "input.h"
#include "shared.h"
#include "app_settings.h"
#include <shellapi.h>
#include <fstream>
#include <deque>
#include <memory>
#include <map>
#include <array>
#include <vector>

namespace {
constexpr UINT ShowMessage = WM_APP + 1, TrayMessage = WM_APP + 2, ExitMessage = WM_APP + 3;
constexpr wchar_t WindowClass[] = L"MozaStalkAssistV2";
constexpr wchar_t DetailsClass[] = L"MozaStalkAssistDetailsV2";
enum {
    Toggle = 10,
    Details = 11,
    Hide = 12,
    Startup = 13,
    Quiet = 14,
    Automatic = 15,
    Counts = 20,
    Logs = 31,
    ResetSetup = 32,
    ExitApp = 40
};
HWND window = nullptr, detailsWindow = nullptr, detailsText = nullptr, titleText = nullptr,
     summaryText = nullptr, controllerText = nullptr, toggleButton = nullptr, counts[3] = {},
     gameStatus[3] = {}, startupCheck = nullptr, quietCheck = nullptr, automaticCheck = nullptr;
HFONT regularFont = nullptr, titleFont = nullptr, statusFont = nullptr, smallFont = nullptr;
HBRUSH backgroundBrush = nullptr, cardBrush = nullptr;
HICON appIcon = nullptr;
HANDLE single = nullptr;
bool smokeTest = false, backgroundStart = false, trayAdded = false, exiting = false;
uint64_t smokeStart = 0, nextConnect = 0, lastRender = 0, lastMonitor = 0;
UINT taskbarCreated = 0;
std::wstring executable, folder, configPath, validationPath, logFolder;
moza::Mapping mapping, atsMapping(moza::ATSMapName);
moza::Command command;
moza::AppSettings settings;
moza::BeamBridge beam;
moza::Diagnostics diagnostics;
std::unique_ptr<moza::Input> input;
moza::Engine monitor;
bool enabled = false, validated = false, requested = true;
int quick[2] = {}, normal[2] = {}, selectedGame = -1;
bool gameLive[2] = {false, false};
const char *gameNames[2] = {"ETS2", "ATS"};
uint64_t lastSerial[2] = {}, lastPluginHeartbeat[2] = {};
DWORD lastPluginPid[2] = {};
std::string deviceName, identity, lastError;
std::ofstream logFile;
std::deque<std::string> lines;
struct ControlLayout {
    HWND child, parent;
    int x, y, width, height, role;
};
std::vector<ControlLayout> layout;
std::map<UINT, std::array<HFONT, 4>> dpiFonts;
std::map<HWND, std::wstring> shownText;
int scaled(int value, UINT dpi) {
    return MulDiv(value, int(dpi), 96);
}
HFONT fontAt(int role, UINT dpi) {
    auto found = dpiFonts.find(dpi);
    if (found == dpiFonts.end()) {
        std::array<HFONT, 4> fonts = {};
        const int sizes[] = {16, 28, 21, 13};
        for (int i = 0; i < 4; i++) {
            fonts[i] = CreateFontW(-scaled(sizes[i], dpi), 0, 0, 0,
                                   (i == 1 || i == 2) ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE,
                                   FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                   i == 3 ? L"Consolas" : L"Segoe UI");
            if (!fonts[i])
                throw std::runtime_error("Could not create DPI-scaled font");
        }
        found = dpiFonts.emplace(dpi, fonts).first;
    }
    return found->second[role];
}
void setText(HWND child, const std::wstring &text) {
    if (!child)
        return;
    auto found = shownText.find(child);
    if (found != shownText.end() && found->second == text)
        return;
    shownText[child] = text;
    SetWindowTextW(child, text.c_str());
}
void resizeDetails(HWND hwnd) {
    RECT area;
    GetClientRect(hwnd, &area);
    auto dpi = GetDpiForWindow(hwnd);
    int margin = scaled(16, dpi), footer = scaled(80, dpi), buttonY = area.bottom - scaled(52, dpi);
    if (detailsText)
        MoveWindow(detailsText, margin, margin, (std::max)(1, int(area.right) - 2 * margin),
                   (std::max)(1, int(area.bottom) - footer), TRUE);
    MoveWindow(GetDlgItem(hwnd, Logs), margin, buttonY, scaled(160, dpi), scaled(34, dpi), TRUE);
    MoveWindow(GetDlgItem(hwnd, ResetSetup), scaled(190, dpi), buttonY, scaled(220, dpi),
               scaled(34, dpi), TRUE);
}
void changeDpi(HWND hwnd, LPARAM value) {
    auto suggested = reinterpret_cast<RECT *>(value);
    SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                 suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    auto dpi = GetDpiForWindow(hwnd);
    for (const auto &item : layout)
        if (item.parent == hwnd && IsWindow(item.child)) {
            MoveWindow(item.child, scaled(item.x, dpi), scaled(item.y, dpi),
                       scaled(item.width, dpi), scaled(item.height, dpi), FALSE);
            SendMessageW(item.child, WM_SETFONT, reinterpret_cast<WPARAM>(fontAt(item.role, dpi)),
                         FALSE);
        }
    if (hwnd == detailsWindow)
        resizeDetails(hwnd);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return L"Text unavailable";
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    out.resize(n - 1);
    return out;
}
void record(const std::string &text) {
    std::string row = "t=" + std::to_string(GetTickCount64()) + " " + text;
    lines.push_back(row);
    if (lines.size() > 100)
        lines.pop_front();
    if (command.config.EnableDiagnosticLogging && logFile)
        logFile << row << std::endl;
}
void publishCommands() {
    command.enabled = enabled;
    command.heartbeat = GetTickCount64();
    auto ets = command, ats = command;
    ets.config = moza::configForCount(command.config, settings.etsCount);
    ats.config = moza::configForCount(command.config, settings.atsCount);
    moza::publish(mapping.data->commandSeq, mapping.data->command, ets);
    moza::publish(atsMapping.data->commandSeq, atsMapping.data->command, ats);
}
void refreshEnabled() {
    bool ready = validated && input && input->connected();
    bool next = !smokeTest && requested && ready;
    if (next != enabled) {
        enabled = next;
        command.epoch = GetTickCount64();
        record(enabled ? "Tap-to-flash on automatically; waiting for fresh game input"
                       : "Extra flashes paused or controller unavailable");
    }
}
void checkValidation() {
    wchar_t saved[128] = {};
    GetPrivateProfileStringW(L"Validation", L"DeviceInstance", L"", saved, 128,
                             validationPath.c_str());
    int l = GetPrivateProfileIntW(L"Validation", L"LeftButton", 0, validationPath.c_str()),
        r = GetPrivateProfileIntW(L"Validation", L"RightButton", 0, validationPath.c_str()),
        c = GetPrivateProfileIntW(L"Validation", L"CentreButton", 0, validationPath.c_str());
    validated = wide(identity) == saved && l == command.config.LeftButton &&
                r == command.config.RightButton && c == command.config.CentreButton;
}
void saveValidation() {
    if (!WritePrivateProfileStringW(L"Validation", L"DeviceInstance", wide(identity).c_str(),
                                    validationPath.c_str()))
        throw std::runtime_error("Cannot save controller setup");
    for (auto kv : {std::pair<const wchar_t *, int>{L"LeftButton", command.config.LeftButton},
                    {L"RightButton", command.config.RightButton},
                    {L"CentreButton", command.config.CentreButton}})
        moza::saveValue(validationPath, L"Validation", kv.first, kv.second);
    validated = true;
    record("Controller setup passed: correct direction/centre pulse timing captured on both sides");
}
void monitorInput(uint64_t time) {
    if (!input)
        input = std::make_unique<moza::Input>();
    if (!input->connected() && time >= nextConnect) {
        nextConnect = time + 2000;
        if (input->open(window)) {
            std::string nextIdentity = moza::guidText(input->selected.instance);
            if (!identity.empty() && identity != nextIdentity) {
                quick[0] = quick[1] = normal[0] = normal[1] = 0;
                monitor.reset("Different device");
            }
            identity = nextIdentity;
            deviceName = input->selected.name;
            checkValidation();
            record("Controller connected: " + deviceName + " " + identity);
            lastError.clear();
        }
    }
    for (const auto &event : input->read(time)) {
        if (event.button == command.config.LeftButton)
            monitor.direction(moza::Side::Left, event.time);
        else if (event.button == command.config.RightButton)
            monitor.direction(moza::Side::Right, event.time);
        else if (event.button == command.config.CentreButton) {
            auto before = monitor.pending;
            monitor.centre(event.time);
            if (before != moza::Side::None) {
                int i = before == moza::Side::Left ? 0 : 1;
                if (monitor.elapsed <= uint64_t(command.config.TapThresholdMs))
                    quick[i]++;
                else
                    normal[i]++;
            }
        }
        record("Physical button " + std::to_string(event.button) + "; " + monitor.classification +
               "; elapsed=" + std::to_string(monitor.elapsed) + "ms");
    }
    if (!validated && quick[0] >= 3 && quick[1] >= 3 && normal[0] >= 1 && normal[1] >= 1)
        saveValidation();
}
void readPlugins(uint64_t time) {
    moza::Diagnostics samples[2] = {};
    moza::Mapping *maps[2] = {&mapping, &atsMapping};
    for (int i = 0; i < 2; i++) {
        if (!moza::snapshot(maps[i]->data->diagSeq, maps[i]->data->diag, samples[i]))
            samples[i] = {};
        gameLive[i] = moza::freshDiagnostics(samples[i], time);
    }
    DWORD foreground = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    selectedGame = moza::selectDiagnostics(samples, time, foreground, selectedGame);
    for (int game = 0; game < 2; game++) {
        if (!gameLive[game])
            continue;
        const auto &d = samples[game];
        if (game == selectedGame)
            diagnostics = d;
        if (d.pid != lastPluginPid[game] || d.heartbeat < lastPluginHeartbeat[game] ||
            d.serial < lastSerial[game]) {
            lastSerial[game] = 0;
            lastPluginPid[game] = d.pid;
        }
        lastPluginHeartbeat[game] = d.heartbeat;
        if (d.serial > lastSerial[game] + 128)
            record(std::string(gameNames[game]) + " diagnostic overflow: older entries lost");
        uint64_t first = d.serial > 128 ? d.serial - 127 : 1;
        for (uint64_t serial = std::max(first, lastSerial[game] + 1); serial <= d.serial;
             serial++) {
            const auto &r = d.records[(serial - 1) % 128];
            if (r.serial == serial)
                record(std::string(gameNames[game]) + " t=" + std::to_string(r.time) + " " +
                       r.text);
        }
        lastSerial[game] = d.serial;
    }
}
void updateTray() {
    if (!trayAdded)
        return;
    NOTIFYICONDATAW icon = {};
    icon.cbSize = sizeof icon;
    icon.hWnd = window;
    icon.uID = 1;
    icon.uFlags = NIF_TIP;
    const wchar_t *tip = enabled     ? L"MOZA Indicator Stalk Assist - tap-to-flash on"
                         : requested ? L"MOZA Indicator Stalk Assist - waiting for controller setup"
                                     : L"MOZA Indicator Stalk Assist - tap-to-flash paused";
    wcsncpy(icon.szTip, tip, _countof(icon.szTip) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &icon);
}
void addTray() {
    NOTIFYICONDATAW icon = {};
    icon.cbSize = sizeof icon;
    icon.hWnd = window;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = TrayMessage;
    icon.hIcon = appIcon;
    wcscpy(icon.szTip, L"MOZA Indicator Stalk Assist");
    trayAdded = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}
void showWindow() {
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
}
void exitApplication() {
    if (exiting)
        return;
    exiting = true;
    enabled = false;
    publishCommands();
    record("App exiting; extra flashes released by game watchdogs");
    if (detailsWindow)
        DestroyWindow(detailsWindow);
    DestroyWindow(window);
}
void toggleAssist() {
    requested = !requested;
    settings.wanted = requested;
    moza::saveValue(configPath, L"App", L"AssistEnabled", requested ? 1 : 0);
    record(requested ? "User resumed tap-to-flash"
                     : "User paused extra flashes; normal stalk remains available");
    refreshEnabled();
    publishCommands();
    lastRender = 0;
}
void showDetails();
void trayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ShowMessage, L"Open settings");
    AppendMenuW(menu, MF_STRING, Toggle,
                requested ? L"Pause extra flashes" : L"Resume extra flashes");
    AppendMenuW(menu, MF_STRING, Details, L"Controller details / logs");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ExitApp, L"Exit Stalk Assist");
    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(window);
    int choice =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (choice == ShowMessage)
        showWindow();
    else if (choice == Toggle)
        toggleAssist();
    else if (choice == Details)
        showDetails();
    else if (choice == ExitApp)
        exitApplication();
}
void render(uint64_t time) {
    bool connected = input && input->connected();
    std::wstring title, summary;
    if (!connected) {
        title = L"Waiting for your MOZA stalk";
        summary =
            L"Keep the controller connected. Tap-to-flash resumes automatically when it is ready.";
    } else if (!validated) {
        title = L"Finish controller setup";
        summary = L"Open the button test: make 3 quick flicks each way, then hold each side for 2 "
                  L"seconds.";
    } else if (!requested) {
        title = L"Tap-to-flash is paused";
        summary = L"Extra flashes are off. Your normal stalk bindings still work.";
    } else {
        title = L"Tap-to-flash is on";
        summary =
            L"Quick flick: finish the flashes below. Hold the stalk longer: signal until centre.";
    }
    setText(titleText, title.c_str());
    setText(summaryText, summary.c_str());
    setText(controllerText, connected ? (validated ? L"MOZA stalk connected  |  Setup complete"
                                                   : L"MOZA stalk connected  |  Setup needed")
                                      : L"Controller not detected yet");
    setText(toggleButton, requested ? L"Pause extra flashes" : L"Resume extra flashes");
    EnableWindow(toggleButton, validated);
    for (int i = 0; i < 2; i++)
        setText(gameStatus[i], gameLive[i] ? L"Connected" : L"Waiting for game");
    setText(gameStatus[2],
            beam.connected(time)
                ? (beam.latest.vehicle > 0 ? L"Mod connected" : L"Connected - load a vehicle")
                : L"Waiting for BeamNG mod");
    if (detailsWindow && detailsText) {
        std::string text =
            "STALK BUTTON TEST\r\n" + deviceName + "\r\nInstance: " + identity + "\r\n";
        text += "Expected left/right/centre: " + std::to_string(command.config.LeftButton) + " / " +
                std::to_string(command.config.RightButton) + " / " +
                std::to_string(command.config.CentreButton) + "\r\n";
        text += "Quick flicks left/right: " + std::to_string(quick[0]) + " / " +
                std::to_string(quick[1]) + "; held returns: " + std::to_string(normal[0]) + " / " +
                std::to_string(normal[1]) + "\r\n";
        text +=
            validated
                ? "Setup complete (saved for this controller). Counts below are from this "
                  "session.\r\n"
                : "Setup needs 3 quick flicks each way and a 2-second hold/centre each way.\r\n";
        text += "Tap window: " + std::to_string(command.config.TapThresholdMs) +
                "ms. Button release is not centre.\r\n";
        if (selectedGame >= 0)
            text += std::string(gameNames[selectedGame]) + " state: " + diagnostics.state +
                    "; flashes=" + std::to_string(diagnostics.flashes) + "; " + diagnostics.reason +
                    "\r\n";
        if (beam.connected(time))
            text += "BeamNG: position=" + std::to_string(beam.latest.position) +
                    "; comfort=" + std::to_string(beam.latest.comfort) +
                    "; flashes=" + std::to_string(beam.latest.flashes) + "\r\n";
        if (!lastError.empty())
            text += "Last error: " + lastError + "\r\n";
        if (!beam.error.empty())
            text += beam.error + "\r\n";
        text += "\r\nRECENT EVENTS\r\n";
        for (const auto &line : lines)
            text += line + "\r\n";
        setText(detailsText, wide(text).c_str());
    }
    updateTray();
}
void tick() {
    auto time = GetTickCount64();
    if (smokeTest && time - smokeStart > 1200) {
        exitApplication();
        return;
    }
    try {
        bool fast = !validated || IsWindowVisible(window) ||
                    (detailsWindow && IsWindowVisible(detailsWindow));
        if (fast || time - lastMonitor >= 100) {
            monitorInput(time);
            lastMonitor = time;
        }
    } catch (const std::exception &e) {
        lastError = e.what();
        record("Controller error: " + lastError);
        if (input)
            input->close();
        monitor.reset("Controller error");
        nextConnect = time + 2000;
    }
    refreshEnabled();
    readPlugins(time);
    publishCommands();
    for (const auto &message :
         beam.poll(time, enabled, command.config.TapThresholdMs, settings.beamCount,
                   moza::configForCount(command.config, settings.beamCount).MaxComfortMs,
                   command.epoch, moza::foregroundBeamNG()))
        record(message);
    if (time - lastRender >= 250) {
        lastRender = time;
        render(time);
    }
}
void applyCounts() {
    int values[3];
    for (int i = 0; i < 3; i++) {
        values[i] = int(SendMessageW(counts[i], CB_GETCURSEL, 0, 0)) + 1;
        if (values[i] < 1 || values[i] > 10)
            return;
    }
    settings.etsCount = values[0];
    settings.atsCount = values[1];
    settings.beamCount = values[2];
    moza::saveValue(configPath, L"Games", L"ETS2BlinkCount", values[0]);
    moza::saveValue(configPath, L"Games", L"ATSBlinkCount", values[1]);
    moza::saveValue(configPath, L"Games", L"BeamNGBlinkCount", values[2]);
    command.epoch = GetTickCount64();
    record("Per-game flash counts updated; old sequences cleared");
    publishCommands();
}
LRESULT CALLBACK detailsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DPICHANGED) {
        changeDpi(hwnd, l);
        return 0;
    }
    if (msg == WM_SIZE && detailsText) {
        resizeDetails(hwnd);
        return 0;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(w) == Logs)
            ShellExecuteW(hwnd, L"open", logFolder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        else if (LOWORD(w) == ResetSetup) {
            enabled = false;
            validated = false;
            publishCommands();
            quick[0] = quick[1] = normal[0] = normal[1] = 0;
            WritePrivateProfileStringW(L"Validation", nullptr, nullptr, validationPath.c_str());
            record("Controller setup reset by user");
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        shownText.erase(detailsText);
        layout.erase(std::remove_if(layout.begin(), layout.end(),
                                    [&](const ControlLayout &item) { return item.parent == hwnd; }),
                     layout.end());
        detailsWindow = nullptr;
        detailsText = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
HWND control(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int width,
             int height, int id, HFONT font = nullptr, HWND parent = nullptr) {
    HWND owner = parent ? parent : window;
    UINT dpi = GetDpiForWindow(owner);
    int role = font == titleFont ? 1 : font == statusFont ? 2 : font == smallFont ? 3 : 0;
    HWND h =
        CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, scaled(x, dpi), scaled(y, dpi),
                      scaled(width, dpi), scaled(height, dpi), owner,
                      reinterpret_cast<HMENU>(intptr_t(id)), GetModuleHandle(nullptr), nullptr);
    if (!h)
        throw std::runtime_error("Could not create a settings control");
    layout.push_back({h, owner, x, y, width, height, role});
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontAt(role, dpi)), TRUE);
    return h;
}
void showDetails() {
    if (!detailsWindow) {
        detailsWindow = CreateWindowW(
            DetailsClass, L"MOZA Indicator Stalk Assist - Controller details",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
            scaled(1000, GetDpiForWindow(window)), scaled(680, GetDpiForWindow(window)), window,
            nullptr, GetModuleHandle(nullptr), nullptr);
        detailsText = control(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
                              16, 16, 950, 550, 0, smallFont, detailsWindow);
        control(L"BUTTON", L"Open log folder", WS_TABSTOP, 16, 576, 160, 34, Logs, nullptr,
                detailsWindow);
        control(L"BUTTON", L"Repeat controller setup", WS_TABSTOP, 190, 576, 220, 34, ResetSetup,
                nullptr, detailsWindow);
    }
    resizeDetails(detailsWindow);
    ShowWindow(detailsWindow, SW_SHOW);
    SetForegroundWindow(detailsWindow);
    lastRender = 0;
}
HICON makeIcon() {
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 32, 32),
            mask = CreateBitmap(32, 32, 1, 1, nullptr);
    HGDIOBJ old = SelectObject(dc, color);
    HBRUSH brush = CreateSolidBrush(RGB(25, 112, 77));
    RECT area = {0, 0, 32, 32};
    FillRect(dc, &area, brush);
    DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
    HGDIOBJ previous = SelectObject(dc, pen);
    MoveToEx(dc, 6, 11, nullptr);
    LineTo(dc, 25, 11);
    MoveToEx(dc, 6, 11, nullptr);
    LineTo(dc, 12, 5);
    MoveToEx(dc, 6, 11, nullptr);
    LineTo(dc, 12, 17);
    MoveToEx(dc, 7, 22, nullptr);
    LineTo(dc, 26, 22);
    MoveToEx(dc, 26, 22, nullptr);
    LineTo(dc, 20, 16);
    MoveToEx(dc, 26, 22, nullptr);
    LineTo(dc, 20, 28);
    SelectObject(dc, previous);
    DeleteObject(pen);
    SelectObject(dc, old);
    HDC maskDC = CreateCompatibleDC(screen);
    old = SelectObject(maskDC, mask);
    PatBlt(maskDC, 0, 0, 32, 32, BLACKNESS);
    SelectObject(maskDC, old);
    DeleteDC(maskDC);
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return icon;
}
LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == taskbarCreated && taskbarCreated) {
        addTray();
        return 0;
    }
    switch (msg) {
    case WM_DPICHANGED:
        changeDpi(hwnd, l);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        try {
            tick();
        } catch (const std::exception &e) {
            lastError = e.what();
            record("App error: " + lastError);
            enabled = false;
            publishCommands();
        }
        return 0;
    case ExitMessage:
        exitApplication();
        return 0;
    case ShowMessage:
        showWindow();
        return 0;
    case TrayMessage:
        if (l == WM_LBUTTONDBLCLK)
            showWindow();
        else if (l == WM_RBUTTONUP)
            trayMenu();
        return 0;
    case WM_COMMAND:
        try {
            int id = LOWORD(w);
            if (id == Toggle)
                toggleAssist();
            else if (id == Details)
                showDetails();
            else if (id == Hide)
                ShowWindow(window, SW_HIDE);
            else if (id >= Counts && id < Counts + 3 && HIWORD(w) == CBN_SELCHANGE)
                applyCounts();
            else if (id == Startup) {
                bool on = SendMessageW(startupCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                moza::setStartWithWindows(executable, on);
                record(on ? "Start with Windows enabled" : "Start with Windows disabled");
            } else if (id == Quiet) {
                settings.quietStart = SendMessageW(quietCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                moza::saveValue(configPath, L"App", L"StartMinimized", settings.quietStart);
            } else if (id == Automatic) {
                settings.autoEnable =
                    SendMessageW(automaticCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                moza::saveValue(configPath, L"App", L"AutoEnable", settings.autoEnable);
            }
        } catch (const std::exception &e) {
            lastError = e.what();
            record("Setting error: " + lastError);
            MessageBoxA(hwnd, e.what(), "Stalk Assist", MB_OK | MB_ICONERROR);
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, RGB(35, 45, 54));
        HWND child = reinterpret_cast<HWND>(l);
        if (child == titleText || child == summaryText || child == controllerText) {
            SetBkColor(dc, RGB(236, 246, 240));
            return reinterpret_cast<LRESULT>(cardBrush);
        }
        SetBkColor(dc, RGB(247, 248, 250));
        return reinterpret_cast<LRESULT>(backgroundBrush);
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC target = BeginPaint(hwnd, &paint);
        RECT client;
        GetClientRect(hwnd, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
        FillRect(dc, &client, backgroundBrush);
        HBRUSH old = static_cast<HBRUSH>(SelectObject(dc, cardBrush));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(215, 232, 220));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        auto dpi = GetDpiForWindow(hwnd);
        RoundRect(dc, scaled(24, dpi), scaled(86, dpi), scaled(790, dpi), scaled(202, dpi),
                  scaled(16, dpi), scaled(16, dpi));
        SelectObject(dc, oldPen);
        SelectObject(dc, old);
        DeleteObject(pen);
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_CLOSE:
        if (trayAdded)
            ShowWindow(hwnd, SW_HIDE);
        else
            exitApplication();
        return 0;
    case WM_QUERYENDSESSION:
        enabled = false;
        publishCommands();
        return TRUE;
    case WM_ENDSESSION:
        if (w)
            exitApplication();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (trayAdded) {
            NOTIFYICONDATAW icon = {};
            icon.cbSize = sizeof icon;
            icon.hWnd = hwnd;
            icon.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &icon);
            trayAdded = false;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR args, int show) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        smokeTest = args && wcsstr(args, L"--smoke-test");
        backgroundStart = args && wcsstr(args, L"--background");
        smokeStart = GetTickCount64();
        wchar_t path[32768];
        DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
        if (!length || length == _countof(path))
            throw std::runtime_error("Executable path unavailable");
        executable = path;
        folder = executable.substr(0, executable.find_last_of(L"\\/"));
        if (args && wcsstr(args, L"--install-startup")) {
            moza::setStartWithWindows(executable, true);
            return 0;
        }
        if (args && wcsstr(args, L"--remove-startup")) {
            moza::setStartWithWindows(executable, false);
            return 0;
        }
        configPath = folder + L"\\Comfort.ini";
        validationPath = folder + L"\\InputValidation.ini";
        logFolder = folder + L"\\logs";
        command.config = moza::loadConfig(configPath);
        settings = moza::loadAppSettings(configPath, command.config);
        requested = settings.wanted && settings.autoEnable;
        command.pid = GetCurrentProcessId();
        command.epoch = GetTickCount64();
        monitor.cfg = command.config;
        single = CreateMutexW(nullptr, FALSE, L"Local\\ETS2MozaComfort_Helper");
        if (!single)
            throw std::runtime_error("Helper mutex unavailable");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(single);
            single = nullptr;
            if (!smokeTest) {
                HWND existing = FindWindowW(WindowClass, nullptr);
                if (existing)
                    PostMessageW(existing,
                                 args && wcsstr(args, L"--exit") ? ExitMessage : ShowMessage, 0, 0);
            }
            return 3;
        }
        if (args && wcsstr(args, L"--exit")) {
            CloseHandle(single);
            single = nullptr;
            return 0;
        }
        if (!mapping.open(true) || !atsMapping.open(true))
            throw std::runtime_error("Local game connection unavailable");
        CreateDirectoryW(logFolder.c_str(), nullptr);
        std::wstring logPath = logFolder + L"\\Comfort.log";
        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &data) &&
            ((uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow) > 5 * 1024 * 1024)
            MoveFileExW(logPath.c_str(), (logFolder + L"\\Comfort.previous.log").c_str(),
                        MOVEFILE_REPLACE_EXISTING);
        logFile.open(logPath, std::ios::app);
        if (command.config.EnableDiagnosticLogging && !logFile)
            throw std::runtime_error("Cannot write diagnostics beside the app");
        regularFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                                  0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        titleFont = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                                0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        statusFont = CreateFontW(-21, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                                 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        smallFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"Consolas");
        backgroundBrush = CreateSolidBrush(RGB(247, 248, 250));
        cardBrush = CreateSolidBrush(RGB(236, 246, 240));
        appIcon = makeIcon();
        WNDCLASSW wc = {};
        wc.lpfnWndProc = proc;
        wc.hInstance = instance;
        wc.lpszClassName = WindowClass;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = backgroundBrush;
        wc.hIcon = appIcon;
        RegisterClassW(&wc);
        wc.lpfnWndProc = detailsProc;
        wc.lpszClassName = DetailsClass;
        RegisterClassW(&wc);
        window = CreateWindowW(WindowClass, L"MOZA Indicator Stalk Assist",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                   WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, scaled(836, GetDpiForSystem()),
                               scaled(692, GetDpiForSystem()), nullptr, nullptr, instance, nullptr);
        if (!window)
            throw std::runtime_error("Settings window unavailable");
        control(L"STATIC", L"MOZA Indicator Stalk Assist", 0, 26, 20, 690, 40, 0, titleFont);
        control(L"STATIC", L"Quick-tap turn signals for your MOZA stalk", 0, 27, 60, 650, 24, 0);
        control(L"STATIC", L"v2.0.1", 0, 742, 31, 50, 24, 0, smallFont);
        titleText = control(L"STATIC", L"Getting ready...", 0, 42, 99, 720, 30, 0, statusFont);
        summaryText = control(L"STATIC", L"", 0, 42, 139, 720, 24, 0);
        controllerText = control(L"STATIC", L"", 0, 42, 171, 720, 22, 0);
        control(L"STATIC", L"GAME", 0, 32, 225, 300, 24, 0, smallFont);
        control(L"STATIC", L"CONNECTION", 0, 366, 225, 220, 24, 0, smallFont);
        control(L"STATIC", L"FLASHES PER FLICK", 0, 618, 225, 176, 24, 0, smallFont);
        const wchar_t *names[] = {L"Euro Truck Simulator 2", L"American Truck Simulator",
                                  L"BeamNG.drive"};
        int values[] = {settings.etsCount, settings.atsCount, settings.beamCount};
        for (int i = 0; i < 3; i++) {
            int y = 258 + i * 45;
            control(L"STATIC", names[i], 0, 32, y + 4, 315, 28, 0);
            gameStatus[i] = control(L"STATIC", L"Waiting for game", 0, 366, y + 4, 240, 28, 0);
            counts[i] = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 642,
                                y, 110, 220, Counts + i);
            for (int n = 1; n <= 10; n++) {
                auto label = std::to_wstring(n) + (n == 1 ? L" flash" : L" flashes");
                SendMessageW(counts[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            SendMessageW(counts[i], CB_SETCURSEL, values[i] - 1, 0);
        }
        startupCheck = control(L"BUTTON", L"Start with Windows", BS_AUTOCHECKBOX | WS_TABSTOP, 32,
                               409, 420, 26, Startup);
        quietCheck = control(L"BUTTON", L"Start quietly in the tray", BS_AUTOCHECKBOX | WS_TABSTOP,
                             32, 442, 420, 26, Quiet);
        automaticCheck = control(L"BUTTON", L"Automatically turn on after controller setup",
                                 BS_AUTOCHECKBOX | WS_TABSTOP, 32, 475, 650, 26, Automatic);
        SendMessageW(startupCheck, BM_SETCHECK,
                     moza::startsWithWindows(executable) ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(quietCheck, BM_SETCHECK, settings.quietStart ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(automaticCheck, BM_SETCHECK, settings.autoEnable ? BST_CHECKED : BST_UNCHECKED,
                     0);
        toggleButton =
            control(L"BUTTON", L"Pause extra flashes", WS_TABSTOP, 32, 529, 208, 40, Toggle);
        control(L"BUTTON", L"Details / button test", WS_TABSTOP, 254, 529, 210, 40, Details);
        control(L"BUTTON", L"Hide to tray", WS_TABSTOP, 590, 529, 170, 40, Hide);
        control(L"STATIC",
                L"Closing this window keeps the app running. To stop it, use tray > Exit.", 0, 32,
                594, 746, 25, 0);
        addTray();
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        record("MOZA Indicator Stalk Assist 2.0.1 started. Automatic per-game counts: ETS2 " +
               std::to_string(settings.etsCount) + ", ATS " + std::to_string(settings.atsCount) +
               ", BeamNG " + std::to_string(settings.beamCount));
        publishCommands();
        SetTimer(window, 1, 20, nullptr);
        render(GetTickCount64());
        bool hidden = smokeTest || (backgroundStart && settings.quietStart);
        ShowWindow(window, hidden ? SW_HIDE : show);
        UpdateWindow(window);
        MSG msg;
        int result;
        while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(window, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        input.reset();
        if (appIcon)
            DestroyIcon(appIcon);
        for (auto object : {regularFont, titleFont, statusFont, smallFont})
            if (object)
                DeleteObject(object);
        if (backgroundBrush)
            DeleteObject(backgroundBrush);
        if (cardBrush)
            DeleteObject(cardBrush);
        if (single)
            CloseHandle(single);
        for (const auto &entry : dpiFonts)
            for (auto f : entry.second)
                if (f)
                    DeleteObject(f);
        return result < 0 ? 1 : 0;
    } catch (const std::exception &e) {
        if (!smokeTest)
            MessageBoxA(nullptr, e.what(), "MOZA Indicator Stalk Assist - could not start",
                        MB_OK | MB_ICONERROR);
        if (single)
            CloseHandle(single);
        return 1;
    }
}

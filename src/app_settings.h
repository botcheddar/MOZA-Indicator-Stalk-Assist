#pragma once
#include "config.h"
#include <windows.h>
#include <string>
#include <stdexcept>
#include <algorithm>
namespace moza {
inline Config configForCount(Config c, int count) {
    c.ComfortBlinkCount = count;
    c.MaxComfortMs = (std::max)(c.MaxComfortMs, count * 1200);
    return c;
}
struct AppSettings {
    bool wanted = true, autoEnable = true, quietStart = true;
    int etsCount = 5, atsCount = 5, beamCount = 3;
};
inline AppSettings loadAppSettings(const std::wstring &path, const Config &legacy) {
    AppSettings s;
    auto get = [&](const wchar_t *section, const wchar_t *key, int fallback, int low, int high) {
        int value = GetPrivateProfileIntW(section, key, fallback, path.c_str());
        if (value < low || value > high)
            throw std::runtime_error("Invalid app/game setting in Comfort.ini");
        return value;
    };
    s.wanted = get(L"App", L"AssistEnabled", 1, 0, 1);
    s.autoEnable = get(L"App", L"AutoEnable", 1, 0, 1);
    s.quietStart = get(L"App", L"StartMinimized", 1, 0, 1);
    s.etsCount = get(L"Games", L"ETS2BlinkCount", legacy.ComfortBlinkCount, 1, 10);
    s.atsCount = get(L"Games", L"ATSBlinkCount", legacy.ComfortBlinkCount, 1, 10);
    s.beamCount = get(L"Games", L"BeamNGBlinkCount", 3, 1, 10);
    return s;
}
inline void saveValue(const std::wstring &path, const wchar_t *section, const wchar_t *key,
                      int value) {
    if (!WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(), path.c_str()))
        throw std::runtime_error("Could not save settings beside the app");
}
inline std::wstring startupCommand(const std::wstring &executable) {
    if (executable.empty() || executable.find(L'"') != std::wstring::npos)
        throw std::runtime_error("Invalid startup executable path");
    return L"\"" + executable + L"\" --background";
}
constexpr const wchar_t *StartupKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t *StartupValue = L"StalkAssist";
inline bool startsWithWindows(const std::wstring &executable) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, StartupKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    wchar_t value[32768] = {};
    DWORD type = 0, size = sizeof value;
    LONG result =
        RegQueryValueExW(key, StartupValue, nullptr, &type, reinterpret_cast<BYTE *>(value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ && size >= sizeof(wchar_t) &&
           size <= sizeof value && size % sizeof(wchar_t) == 0 &&
           value[size / sizeof(wchar_t) - 1] == 0 &&
           std::wstring(value) == startupCommand(executable);
}
inline void setStartWithWindows(const std::wstring &executable, bool enable) {
    HKEY key;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, StartupKey, 0, nullptr, 0, KEY_SET_VALUE,
                                  nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("Could not update your Windows sign-in startup entry");
    if (enable) {
        auto command = startupCommand(executable);
        result = RegSetValueExW(key, StartupValue, 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(command.c_str()),
                                DWORD((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, StartupValue);
        if (result == ERROR_FILE_NOT_FOUND)
            result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("Could not save Windows startup preference");
}
} // namespace moza

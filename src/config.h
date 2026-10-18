#pragma once
#include "engine.h"
#include <windows.h>
#include <stdexcept>
namespace moza {
inline Config loadConfig(const std::wstring &path) {
    Config c;
    auto integer = [&](const wchar_t *key, int fallback, int lo, int hi) {
        int v = GetPrivateProfileIntW(L"Comfort", key, fallback, path.c_str());
        if (v < lo || v > hi)
            throw std::runtime_error(
                "Invalid configuration value; see Comfort.ini limits in the guide");
        return v;
    };
    c.TapThresholdMs = integer(L"TapThresholdMs", 500, 50, 1500);
    c.ComfortBlinkCount = integer(L"ComfortBlinkCount", 5, 1, 10);
    c.ComfortDurationMs = integer(L"ComfortDurationMs", 4000, c.TapThresholdMs + 1, 30000);
    c.MaxComfortMs = integer(L"MaxComfortMs", 6000, c.TapThresholdMs + 1, 30000);
    c.CancelOnRepeatedFlick = integer(L"CancelOnRepeatedFlick", 1, 0, 1);
    c.SwitchOnOppositeFlick = integer(L"SwitchOnOppositeFlick", 1, 0, 1);
    c.PreserveFullDetent = integer(L"PreserveFullDetent", 1, 1, 1);
    c.EnableDiagnosticLogging = integer(L"EnableDiagnosticLogging", 1, 0, 1);
    c.UseTelemetryBlinkCounting = integer(L"UseTelemetryBlinkCounting", 1, 0, 1);
    c.LeftButton = integer(L"LeftButton", 10, 1, 128);
    c.RightButton = integer(L"RightButton", 8, 1, 128);
    c.CentreButton = integer(L"CentreButton", 9, 1, 128);
    if (c.LeftButton == c.RightButton || c.LeftButton == c.CentreButton ||
        c.RightButton == c.CentreButton)
        throw std::runtime_error("Button mappings must be distinct");
    return c;
}
} // namespace moza

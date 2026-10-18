#pragma once
#include <cstdint>
#include <string>

namespace moza {
enum class Side { None = 0, Left = 1, Right = 2 };
struct Config {
    int TapThresholdMs = 500, ComfortBlinkCount = 5, ComfortDurationMs = 4000, MaxComfortMs = 6000;
    bool CancelOnRepeatedFlick = true, SwitchOnOppositeFlick = true, PreserveFullDetent = true;
    bool EnableDiagnosticLogging = true, UseTelemetryBlinkCounting = true;
    int LeftButton = 10, RightButton = 8, CentreButton = 9;
};
struct Lamps {
    uint32_t rises[2] = {0, 0};
    bool on[2] = {false, false};
    bool valid = false;
};
class Engine {
  public:
    Config cfg;
    Side position = Side::None, pending = Side::None, comfort = Side::None;
    int flashes = 0;
    uint64_t started = 0, elapsed = 0;
    bool suppressed = false, safe = false;
    std::string classification = "Idle", reason = "Startup";
    Lamps last;
    uint32_t baseline = 0;
    bool initialOn = false;
    Side output() const {
        return safe ? comfort : Side::None;
    }
    void reset(const std::string &why) {
        position = pending = comfort = Side::None;
        suppressed = false;
        flashes = 0;
        classification = "Idle";
        reason = why;
    }
    void safety(bool allow, const std::string &why = "Safety interlock") {
        if (!allow)
            reset(why);
        safe = allow;
    }
    void direction(Side side, uint64_t time) {
        if (side == Side::None)
            return;
        bool repeated = (comfort == side);
        bool opposite = (comfort != Side::None && comfort != side);
        comfort = Side::None;
        reason = repeated   ? "Repeated direction"
                 : opposite ? "Opposite direction"
                            : "Physical direction takes priority";
        suppressed =
            (repeated && cfg.CancelOnRepeatedFlick) || (opposite && !cfg.SwitchOnOppositeFlick);
        position = pending = side;
        started = time;
        elapsed = 0;
        flashes = 0;
        int i = side == Side::Left ? 0 : 1;
        baseline = last.rises[i];
        initialOn = last.valid && last.on[i];
        classification = suppressed ? "Pending - comfort suppressed" : "Pending";
    }
    void centre(uint64_t time) {
        position = Side::None;
        if (pending == Side::None)
            return;
        elapsed = time >= started ? time - started : UINT64_MAX;
        Side side = pending;
        pending = Side::None;
        if (elapsed <= uint64_t(cfg.TapThresholdMs)) {
            classification = suppressed ? "Quick flick cancelled" : "Quick flick";
            if (safe && !suppressed) {
                comfort = side;
                reason = "Quick return accepted";
                classification =
                    side == Side::Left ? "Left comfort active" : "Right comfort active";
            }
        } else {
            classification = "Normal detent";
            reason = "Normal return to centre";
        }
        suppressed = false;
    }
    void tick(uint64_t time, const Lamps &lamps) {
        last = lamps;
        if (pending != Side::None && time >= started &&
            time - started > uint64_t(cfg.TapThresholdMs))
            classification = "Normal detent";
        Side tracked = pending != Side::None ? pending : comfort;
        if (tracked != Side::None) {
            int i = tracked == Side::Left ? 0 : 1;
            uint32_t delta = lamps.rises[i] - baseline;
            // A plugin restart/invalid counter is an interlock failure, never a huge blink count.
            flashes = delta < 10000 ? int(delta) + (initialOn ? 1 : 0) : 0;
        }
        if (comfort == Side::None || time < started)
            return;
        bool exact = cfg.UseTelemetryBlinkCounting && lamps.valid;
        if (exact && flashes >= cfg.ComfortBlinkCount && !lamps.on[comfort == Side::Left ? 0 : 1]) {
            comfort = Side::None;
            reason = "Completed observed flashes";
            classification = "Complete";
        } else if (time - started >= uint64_t(exact ? cfg.MaxComfortMs : cfg.ComfortDurationMs)) {
            comfort = Side::None;
            reason = exact ? "Flash watchdog expired" : "Fallback duration from direction expired";
            classification = "Complete";
        }
    }
};
} // namespace moza

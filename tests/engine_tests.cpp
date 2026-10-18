#include "../src/engine.h"
#include <cstdio>
#include <stdexcept>
#include <random>
using namespace moza;
int passed = 0, failed = 0;
void expect(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void test(const char *name, F f) {
    try {
        f();
        printf("PASS %s\n", name);
        passed++;
    } catch (const std::exception &e) {
        printf("FAIL %s: %s\n", name, e.what());
        failed++;
    }
}
Engine fresh() {
    Engine e;
    e.cfg.ComfortBlinkCount = 3;
    e.cfg.ComfortDurationMs = 2400;
    e.safety(true);
    return e;
}
void flick(Engine &e, Side s, uint64_t start, int ms = 150) {
    e.direction(s, start);
    e.centre(start + ms);
}
int main() {
    for (int ms : {150, 400, 500, 600, 5000})
        test(("Left return at " + std::to_string(ms) + " ms").c_str(), [=] {
            auto e = fresh();
            flick(e, Side::Left, 0, ms);
            expect(e.output() == (ms <= 500 ? Side::Left : Side::None),
                   "inclusive threshold incorrect");
        });
    test("Right quick 200 ms", [] {
        auto e = fresh();
        flick(e, Side::Right, 0, 200);
        expect(e.output() == Side::Right, "wrong side");
    });
    test("Centre without direction", [] {
        auto e = fresh();
        e.centre(100);
        expect(e.output() == Side::None, "unexpected output");
    });
    test("Duplicate centre does not restart", [] {
        auto e = fresh();
        flick(e, Side::Left, 0);
        e.centre(400);
        expect(e.started == 0 && e.output() == Side::Left, "duplicate centre changed sequence");
    });
    for (auto side : {Side::Left, Side::Right})
        test(side == Side::Left ? "Repeated left cancels on entry"
                                : "Repeated right cancels on entry",
             [=] {
                 auto e = fresh();
                 flick(e, side, 0);
                 e.direction(side, 300);
                 expect(e.output() == Side::None, "late cancellation");
                 e.centre(450);
                 expect(e.output() == Side::None, "sequence restarted");
             });
    test("Third flick can start again", [] {
        auto e = fresh();
        flick(e, Side::Left, 0);
        flick(e, Side::Left, 300);
        flick(e, Side::Left, 600);
        expect(e.output() == Side::Left, "artificial cooldown");
    });
    test("Repeat restart option", [] {
        auto e = fresh();
        e.cfg.CancelOnRepeatedFlick = false;
        flick(e, Side::Left, 0);
        flick(e, Side::Left, 300);
        expect(e.output() == Side::Left && e.started == 300, "restart option broken");
    });
    test("Opposite switches without stale left", [] {
        auto e = fresh();
        flick(e, Side::Left, 0);
        e.direction(Side::Right, 300);
        expect(e.output() == Side::None, "old side still active");
        e.centre(500);
        expect(e.output() == Side::Right, "right missing");
        e.tick(2500, Lamps{});
        expect(e.output() == Side::Right, "old timer cancelled new right");
    });
    test("Opposite switching disabled option", [] {
        auto e = fresh();
        e.cfg.SwitchOnOppositeFlick = false;
        flick(e, Side::Left, 0);
        flick(e, Side::Right, 300);
        expect(e.output() == Side::None, "option ignored");
    });
    test("Full detent preempts comfort and no timer owns it", [] {
        auto e = fresh();
        flick(e, Side::Left, 0);
        e.direction(Side::Right, 300);
        e.tick(900, Lamps{});
        expect(e.output() == Side::None && e.position == Side::Right, "comfort owns normal detent");
        e.tick(10000, Lamps{});
        expect(e.position == Side::Right, "old timer changed physical state");
        e.centre(11000);
        expect(e.output() == Side::None, "normal return created comfort");
    });
    test("Fallback duration counts from initial direction", [] {
        auto e = fresh();
        flick(e, Side::Left, 1000, 400);
        e.tick(3399, Lamps{});
        expect(e.output() == Side::Left, "early end");
        e.tick(3400, Lamps{});
        expect(e.output() == Side::None, "late end");
    });
    test("Observed three flashes includes initial physical flash", [] {
        auto e = fresh();
        Lamps l;
        l.valid = true;
        e.tick(0, l);
        e.direction(Side::Left, 10);
        l.rises[0] = 1;
        l.on[0] = true;
        e.tick(20, l);
        e.centre(200);
        e.tick(210, l);
        expect(e.flashes == 1, "initial flash lost");
        for (int i = 1; i <= 3; i++) {
            l.rises[0] = i;
            l.on[0] = true;
            e.tick(300 + i * 600, l);
            expect(e.output() == Side::Left, "third flash truncated");
            l.on[0] = false;
            e.tick(600 + i * 600, l);
            expect(e.output() == (i < 3 ? Side::Left : Side::None), "incorrect completion");
        }
    });
    test("Direction begins in already lit phase", [] {
        auto e = fresh();
        Lamps l;
        l.valid = true;
        l.rises[1] = 7;
        l.on[1] = true;
        e.tick(0, l);
        flick(e, Side::Right, 10);
        e.tick(200, l);
        expect(e.flashes == 1, "partial first flash not counted");
        l.rises[1] = 9;
        l.on[1] = false;
        e.tick(1600, l);
        expect(e.output() == Side::None, "three total not completed");
    });
    test("Other side lamp cannot count", [] {
        auto e = fresh();
        Lamps l;
        l.valid = true;
        e.tick(0, l);
        flick(e, Side::Right, 10);
        l.rises[0] = 10;
        e.tick(2000, l);
        expect(e.output() == Side::Right && e.flashes == 0, "wrong lamp counted");
    });
    test("Exact mode watchdog with no flashing", [] {
        auto e = fresh();
        Lamps l;
        l.valid = true;
        e.tick(0, l);
        flick(e, Side::Left, 0);
        e.tick(6000, l);
        expect(e.output() == Side::None && e.reason == "Flash watchdog expired",
               "unbounded output");
    });
    for (const char *reason : {"Focus lost", "Shutdown", "Disconnect", "Paused", "Buffer overflow",
                               "Plugin heartbeat lost"})
        test(reason, [=] {
            auto e = fresh();
            flick(e, Side::Left, 0);
            e.safety(false, reason);
            e.tick(500, Lamps{});
            e.centre(600);
            expect(e.output() == Side::None && e.pending == Side::None, "stale action");
        });
    test("Restart has no stale state", [] {
        auto e = fresh();
        flick(e, Side::Left, 0);
        Engine restarted;
        expect(restarted.output() == Side::None && restarted.pending == Side::None,
               "stale state restored");
    });
    test("Custom threshold", [] {
        auto e = fresh();
        e.cfg.TapThresholdMs = 250;
        flick(e, Side::Right, 0, 300);
        expect(e.output() == Side::None, "threshold not configurable");
    });
    test("Counter wrap is well defined", [] {
        auto e = fresh();
        Lamps l;
        l.valid = true;
        l.rises[0] = UINT32_MAX - 1;
        e.tick(0, l);
        flick(e, Side::Left, 10);
        l.rises[0] = 1;
        e.tick(2100, l);
        expect(e.output() == Side::None && e.flashes == 3, "counter wrap broken");
    });
    test("250000 rapid alternating flicks no queued actions", [] {
        auto e = fresh();
        uint64_t now = 0;
        for (int i = 0; i < 250000; i++) {
            Side s = (i & 1) ? Side::Right : Side::Left;
            e.direction(s, now);
            expect(e.output() == Side::None, "old output survived");
            e.centre(now + 100);
            expect(e.output() == s, "wrong output");
            e.tick(now + 120, Lamps{});
            now += 150;
        }
        e.safety(false, "end");
        e.tick(now + 100000, Lamps{});
        expect(e.output() == Side::None, "ghost action");
    });
    test("12 hour simulated session has bounded state", [] {
        auto e = fresh();
        for (uint64_t t = 0; t < 43200000; t += 1000) {
            flick(e, (t / 1000 & 1) ? Side::Left : Side::Right, t);
            e.tick(t + 800, Lamps{});
        }
        e.tick(43203000, Lamps{});
        expect(e.output() == Side::None, "session timer leaked");
    });
    test("New defaults select five observed flashes", [] {
        Engine e;
        expect(e.cfg.ComfortBlinkCount == 5 && e.cfg.ComfortDurationMs == 4000,
               "five-flash defaults missing");
    });
    for (auto side : {Side::Left, Side::Right})
        test(side == Side::Left ? "Five left flashes include first physical flash"
                                : "Five right flashes include first physical flash",
             [=] {
                 Engine e;
                 e.safety(true);
                 Lamps l;
                 l.valid = true;
                 e.tick(0, l);
                 int index = side == Side::Left ? 0 : 1;
                 e.direction(side, 10);
                 l.rises[index] = 1;
                 l.on[index] = true;
                 e.tick(20, l);
                 e.centre(160);
                 for (int n = 1; n <= 5; n++) {
                     l.rises[index] = n;
                     l.on[index] = true;
                     e.tick(20 + (n - 1) * 700, l);
                     expect(e.output() == side, "lit phase cut short");
                     l.on[index] = false;
                     e.tick(360 + (n - 1) * 700, l);
                     expect(e.output() == (n < 5 ? side : Side::None),
                            "ended before/after fifth falling edge");
                 }
             });
    test("Five-flash sequence remains active after third flash", [] {
        Engine e;
        e.safety(true);
        Lamps l;
        l.valid = true;
        e.tick(0, l);
        flick(e, Side::Left, 10);
        l.rises[0] = 3;
        l.on[0] = false;
        e.tick(2500, l);
        expect(e.output() == Side::Left, "old three-flash duration truncated output");
        e.direction(Side::Left, 2600);
        e.centre(2750);
        expect(e.output() == Side::None, "repeat did not cancel five-flash sequence");
    });
    printf("RESULT: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

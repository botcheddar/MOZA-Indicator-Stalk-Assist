#define DIRECTINPUT_VERSION 0x0800
#include "../src/button_decoder.h"
#include <cstdio>
#include <string>
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
DIDEVICEOBJECTDATA report(int button, bool down, uint32_t time) {
    DIDEVICEOBJECTDATA e = {};
    e.dwOfs = DIJOFS_BUTTON(button - 1);
    e.dwData = down ? 0x80 : 0;
    e.dwTimeStamp = time;
    return e;
}
int main() {
    test("One-based 10/8/9 mapping and arrival order", [] {
        ButtonDecoder d;
        DIDEVICEOBJECTDATA b[] = {report(10, true, 100), report(10, false, 120),
                                  report(9, true, 318),  report(9, false, 330),
                                  report(8, true, 500),  report(8, false, 510),
                                  report(9, true, 804)};
        auto e = d.decode(b, 7, 810);
        expect(e.size() == 4 && e[0].button == 10 && e[1].button == 9 && e[2].button == 8 &&
                   e[3].button == 9,
               "wrong mapping/order");
        expect(e[1].time - e[0].time == 218 && e[3].time - e[2].time == 304,
               "wrong event interval");
    });
    test("Press and release both between reads still delivered", [] {
        ButtonDecoder d;
        DIDEVICEOBJECTDATA b[] = {report(10, true, 100), report(10, false, 101)};
        auto e = d.decode(b, 2, 150);
        expect(e.size() == 1 && e[0].time == 100, "short pulse missed");
    });
    test("Button release is not centre event", [] {
        ButtonDecoder d;
        auto a = report(10, true, 100);
        d.decode(&a, 1, 100);
        a = report(10, false, 110);
        expect(d.decode(&a, 1, 120).empty(), "release became centre");
    });
    test("Duplicate high suppressed until release", [] {
        ButtonDecoder d;
        DIDEVICEOBJECTDATA b[] = {report(8, true, 100), report(8, true, 101), report(8, false, 102),
                                  report(8, true, 103)};
        expect(d.decode(b, 4, 110).size() == 2, "duplicate edge");
    });
    test("Already high at acquisition ignored until release", [] {
        ButtonDecoder d;
        BYTE seed[128] = {};
        seed[9] = 0x80;
        d.seed(seed);
        DIDEVICEOBJECTDATA b[] = {report(10, true, 100), report(10, false, 110),
                                  report(10, true, 120)};
        auto e = d.decode(b, 3, 130);
        expect(e.size() == 1 && e[0].time == 120, "startup replay");
    });
    test("Axis reports ignored", [] {
        ButtonDecoder d;
        auto e = report(1, true, 100);
        e.dwOfs = DIJOFS_X;
        expect(d.decode(&e, 1, 110).empty(), "axis became button");
    });
    test("32-bit timestamp wrap reconstructed", [] {
        ButtonDecoder d;
        auto e = report(9, true, UINT32_MAX - 20);
        auto decoded = d.decode(&e, 1, uint64_t(UINT32_MAX) + 31);
        expect(decoded.size() == 1 && decoded[0].time == UINT32_MAX - 20ULL, "clock wrap broken");
    });
    test("Old input rejected", [] {
        ButtonDecoder d;
        auto e = report(10, true, 100);
        bool threw = false;
        try {
            d.decode(&e, 1, 1200);
        } catch (...) {
            threw = true;
        }
        expect(threw, "old event replay");
    });
    test("Future timestamp rejected", [] {
        ButtonDecoder d;
        auto e = report(10, true, 101);
        bool threw = false;
        try {
            d.decode(&e, 1, 100);
        } catch (...) {
            threw = true;
        }
        expect(threw, "future event accepted");
    });
    test("Clear discards pressed state after reconnect", [] {
        ButtonDecoder d;
        auto e = report(10, true, 100);
        d.decode(&e, 1, 100);
        d.clear();
        e = report(10, true, 200);
        expect(d.decode(&e, 1, 200).size() == 1, "reconnect retained old state");
    });
    printf("RESULT: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

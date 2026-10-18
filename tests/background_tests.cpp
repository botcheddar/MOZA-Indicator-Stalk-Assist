#include "../src/beam_bridge.h"
#include "../src/app_settings.h"
#include <cstdio>
int passed = 0, failed = 0;
void expect(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void test(const char *n, F f) {
    try {
        f();
        printf("PASS %s\n", n);
        passed++;
    } catch (const std::exception &e) {
        printf("FAIL %s: %s\n", n, e.what());
        failed++;
    }
}
int main() {
    test("BeamNG frame parsed without executable text", [] {
        moza::BeamFrame f;
        expect(moza::parseBeamFrame("MZB2 10 22 -1 0 1 0 1 5 1 150 1 0", f) && f.sequence == 10 &&
                   f.position == -1 && f.elapsed == 150,
               "valid packet rejected");
    });
    for (const char *packet :
         {"MZB2 1 2 3 0 0 0 0 0 0 0 0 0", "MZB2 1 2 0 0 0 0 0 0 0 0 0 0; execute()",
          "MZB2 0 2 0 0 0 0 0 0 0 0 0 0", "BAD 1 2 0 0 0 0 0 0 0 0 0 0",
          "MZB2 1 2 0 0 0 0 0 0 0 0 0", "MZB2 1 2 0 0 0 0 0 0 0 0 8 0"})
        test("Malformed/out-of-range BeamNG packet rejected", [=] {
            moza::BeamFrame f;
            expect(!moza::parseBeamFrame(packet, f), "invalid packet accepted");
        });
    test("BeamNG reply is bounded numeric protocol", [] {
        expect(moza::beamReply(7, 123, true, 500, 3, 6000) == "MZC2 7 123 1 500 3 6000",
               "wrong reply");
    });
    test("Per-game defaults are ETS2 five ATS five BeamNG three", [] {
        moza::Config c;
        auto s = moza::loadAppSettings(L"Z:\\nonexistent\\settings.ini", c);
        expect(s.etsCount == 5 && s.atsCount == 5 && s.beamCount == 3 && s.autoEnable,
               "wrong defaults");
    });
    test("Startup path is quoted and runs quietly", [] {
        expect(moza::startupCommand(L"C:\\Program Files\\Stalk Assist.exe") ==
                   L"\"C:\\Program Files\\Stalk Assist.exe\" --background",
               "unsafe startup command");
    });
    test("Startup embedded quote rejected", [] {
        bool rejected = false;
        try {
            moza::startupCommand(L"C:\\bad\".exe");
        } catch (...) {
            rejected = true;
        }
        expect(rejected, "unsafe quote accepted");
    });
    test("High flash counts extend bounded watchdog", [] {
        moza::Config c;
        auto adjusted = moza::configForCount(c, 10);
        expect(adjusted.ComfortBlinkCount == 10 && adjusted.MaxComfortMs == 12000,
               "watchdog would cut long count short");
    });
    printf("RESULT: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

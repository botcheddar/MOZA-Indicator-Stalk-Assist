#include "../src/shared.h"
#include "../src/config.h"
#include <cstdio>
#include <string>
#include <thread>
#include <atomic>
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
int main() {
    test("Heartbeat is inclusive at 750 ms and rejects future/stale", [] {
        Command c;
        c.pid = 1;
        c.epoch = 1;
        c.enabled = 1;
        c.heartbeat = 1000;
        expect(freshCommand(c, 1750) && !freshCommand(c, 1751) && !freshCommand(c, 999),
               "liveness boundary wrong");
    });
    test("Wrong protocol and disabled command rejected", [] {
        Command c;
        c.pid = 1;
        c.epoch = 1;
        c.enabled = 1;
        c.heartbeat = 1000;
        c.protocol = 0;
        expect(!freshCommand(c, 1000), "protocol accepted");
        c.protocol = Protocol;
        c.enabled = 0;
        expect(!freshCommand(c, 1000), "disabled accepted");
    });
    test("Interrupted writer recovers odd sequence", [] {
        volatile LONG sequence = 7;
        Command value, write, read;
        write.epoch = 1234;
        publish(sequence, value, write);
        expect(!(sequence & 1) && snapshot(sequence, value, read) && read.epoch == 1234,
               "writer crash persisted dead sequence");
    });
    test("Odd sequence is never read", [] {
        volatile LONG sequence = 3;
        Command value, out;
        expect(!snapshot(sequence, value, out), "incomplete snapshot accepted");
    });
    test("Sequence counter wrap remains readable", [] {
        volatile LONG sequence = LONG(0xfffffffeU);
        Command value, write, read;
        write.epoch = 987;
        publish(sequence, value, write);
        expect(sequence == 0 && snapshot(sequence, value, read) && read.epoch == 987,
               "sequence wrap broken");
    });
    test("Helper selects the foreground game's diagnostics", [] {
        Diagnostics d[2];
        for (int i = 0; i < 2; i++) {
            d[i].pid = 100 + i;
            d[i].heartbeat = 1000;
        }
        expect(selectDiagnostics(d, 1100, 101, 0) == 1, "ATS foreground not selected");
        expect(selectDiagnostics(d, 1100, 100, 1) == 0, "ETS2 foreground not selected");
        expect(selectDiagnostics(d, 1100, 999, 1) == 1, "helper focus lost selected live game");
        d[1].heartbeat = 0;
        expect(selectDiagnostics(d, 1600, 101, 1) == 0, "stale ATS selected");
        d[0].heartbeat = 0;
        expect(selectDiagnostics(d, 1600, 100, 0) == -1, "stale games considered connected");
    });
    wchar_t dir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, dir);
    std::wstring path = std::wstring(dir) + L"\\logs\\ConfigTest.ini";
    // A previous interrupted test may have deliberately left invalid values.
    WritePrivateProfileStringW(L"Comfort", nullptr, nullptr, path.c_str());
    test("INI configuration loaded without recompilation", [&] {
        WritePrivateProfileStringW(L"Comfort", L"TapThresholdMs", L"350", path.c_str());
        auto c = loadConfig(path);
        expect(c.TapThresholdMs == 350 && c.LeftButton == 10, "INI not applied");
    });
    test("INI defaults use five flashes", [&] {
        auto c = loadConfig(path);
        expect(c.ComfortBlinkCount == 5 && c.ComfortDurationMs == 4000, "INI defaults not updated");
    });
    test("Invalid threshold rejected", [&] {
        WritePrivateProfileStringW(L"Comfort", L"TapThresholdMs", L"0", path.c_str());
        bool threw = false;
        try {
            loadConfig(path);
        } catch (...) {
            threw = true;
        }
        expect(threw, "invalid threshold accepted");
        WritePrivateProfileStringW(L"Comfort", L"TapThresholdMs", L"500", path.c_str());
    });
    test("Overlapping buttons rejected", [&] {
        WritePrivateProfileStringW(L"Comfort", L"RightButton", L"10", path.c_str());
        bool threw = false;
        try {
            loadConfig(path);
        } catch (...) {
            threw = true;
        }
        expect(threw, "ambiguous mapping accepted");
        WritePrivateProfileStringW(L"Comfort", L"RightButton", L"8", path.c_str());
    });
    test("Disabling full-detent preservation rejected", [&] {
        WritePrivateProfileStringW(L"Comfort", L"PreserveFullDetent", L"0", path.c_str());
        bool threw = false;
        try {
            loadConfig(path);
        } catch (...) {
            threw = true;
        }
        expect(threw, "full-detent protection disabled");
    });
    printf("RESULT: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

#pragma once
#include "engine.h"
#include <windows.h>
#include <cstring>
#include <stdexcept>
namespace moza {
#ifdef MOZA_TESTING
constexpr const wchar_t *MapName = L"Local\\ETS2MozaComfort_Test_v1";
constexpr const wchar_t *ATSMapName = L"Local\\ATSMozaComfort_Test_v1";
#else
constexpr const wchar_t *MapName = L"Local\\ETS2MozaComfort_v1";
constexpr const wchar_t *ATSMapName = L"Local\\ATSMozaComfort_v1";
#endif
constexpr unsigned Protocol = 0x4d5a0101;
struct Command {
    unsigned protocol = Protocol;
    DWORD pid = 0;
    uint64_t epoch = 0, heartbeat = 0;
    int enabled = 0;
    Config config;
};
struct Record {
    uint64_t serial = 0, time = 0;
    char text[256] = {};
};
struct Diagnostics {
    unsigned protocol = Protocol;
    DWORD pid = 0;
    uint64_t heartbeat = 0;
    int connected = 0, telemetry = 0, driving = 0, output = 0, position = 0, lastButton = 0,
        flashes = 0;
    uint64_t eventTime = 0, elapsed = 0, serial = 0;
    char device[256] = {}, identity[96] = {}, state[96] = {}, reason[256] = {};
    Record records[128];
};
struct Shared {
    alignas(8) volatile LONG commandSeq;
    Command command;
    alignas(8) volatile LONG diagSeq;
    Diagnostics diag;
};
template <class T> void publish(volatile LONG &seq, T &target, const T &source) {
    // Recover if the previous writer died with an odd sequence.
    const uint32_t current = uint32_t(InterlockedCompareExchange(&seq, 0, 0));
    InterlockedExchange(&seq, LONG((current & ~1U) + 1U));
    memcpy(&target, &source, sizeof(T));
    MemoryBarrier();
    InterlockedIncrement(&seq);
}
template <class T> bool snapshot(volatile LONG &seq, const T &source, T &out) {
    for (int i = 0; i < 3; i++) {
        LONG a = InterlockedCompareExchange(&seq, 0, 0);
        if (a & 1)
            continue;
        memcpy(&out, &source, sizeof(T));
        MemoryBarrier();
        if (a == InterlockedCompareExchange(&seq, 0, 0))
            return true;
    }
    return false;
}
class Mapping {
    HANDLE handle = nullptr;
    const wchar_t *name;

  public:
    Shared *data = nullptr;
    explicit Mapping(const wchar_t *mappingName = MapName) : name(mappingName) {}
    Mapping(const Mapping &) = delete;
    Mapping &operator=(const Mapping &) = delete;
    ~Mapping() {
        close();
    }
    void close() {
        if (data)
            UnmapViewOfFile(data);
        if (handle)
            CloseHandle(handle);
        data = nullptr;
        handle = nullptr;
    }
    void select(const wchar_t *mappingName) {
        if (wcscmp(name, mappingName) != 0) {
            close();
            name = mappingName;
        }
    }
    bool open(bool create) {
        if (data)
            return true;
        handle = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                             sizeof(Shared), name)
                        : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (!handle)
            return false;
        data = (Shared *)MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
        if (!data) {
            CloseHandle(handle);
            handle = nullptr;
            return false;
        }
        return true;
    }
};
inline bool freshCommand(const Command &c, uint64_t now) {
    return c.protocol == Protocol && c.pid && c.epoch && c.heartbeat <= now &&
           now - c.heartbeat <= 750 && c.enabled;
}
inline bool freshDiagnostics(const Diagnostics &d, uint64_t now) {
    return d.protocol == Protocol && d.pid && d.heartbeat <= now && now - d.heartbeat < 1500;
}
// Retain the selected live game while the helper itself has focus. Each process
// gets an independent mapping, log cursor and watchdog, including when both run.
inline int selectDiagnostics(const Diagnostics (&d)[2], uint64_t now, DWORD foregroundPid,
                             int previous) {
    for (int i = 0; i < 2; i++)
        if (freshDiagnostics(d[i], now) && d[i].pid == foregroundPid)
            return i;
    if (previous >= 0 && previous < 2 && freshDiagnostics(d[previous], now))
        return previous;
    for (int i = 0; i < 2; i++)
        if (freshDiagnostics(d[i], now))
            return i;
    return -1;
}
inline bool foregroundProcess(DWORD pid) {
    DWORD foreground = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    return foreground == pid;
}
} // namespace moza

#include "../src/beam_bridge.h"
#include <stdexcept>
#include <cstdio>
struct Peer {
    SOCKET value = INVALID_SOCKET;
    Peer() {
        value = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (value == INVALID_SOCKET)
            throw std::runtime_error("socket failed");
        DWORD timeout = 100;
        setsockopt(value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout),
                   sizeof timeout);
    }
    ~Peer() {
        if (value != INVALID_SOCKET)
            closesocket(value);
    }
    void send(const std::string &s) {
        sockaddr_in target = {};
        target.sin_family = AF_INET;
        target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        target.sin_port = htons(moza::BeamPort);
        if (sendto(value, s.data(), int(s.size()), 0, reinterpret_cast<sockaddr *>(&target),
                   sizeof target) == SOCKET_ERROR)
            throw std::runtime_error("send failed");
    }
    std::string receive() {
        char b[512];
        int n = recv(value, b, sizeof b, 0);
        return n > 0 ? std::string(b, n) : "";
    }
};
int main() {
    int passed = 0, failed = 0;
    auto test = [&](const char *n, auto f) {
        try {
            f();
            printf("PASS %s\n", n);
            passed++;
        } catch (const std::exception &e) {
            printf("FAIL %s: %s\n", n, e.what());
            failed++;
        }
    };
    auto expect = [](bool b) {
        if (!b)
            throw std::runtime_error("unexpected UDP result");
    };
    moza::BeamBridge bridge;
    if (!bridge.start(GetTickCount64())) {
        printf("FAIL port unavailable: %s\n", bridge.error.c_str());
        return 1;
    }
    Peer peer;
    auto exchange = [&](int seq, bool enable, bool foreground, int hazard = 0) {
        peer.send("MZB2 " + std::to_string(seq) + " 12 0 0 0 " + std::to_string(hazard) +
                  " 0 0 0 0 0 0");
        for (int i = 0; i < 100 && bridge.latest.sequence < uint64_t(seq); i++) {
            bridge.poll(GetTickCount64(), enable, 500, 3, 6000, 88, foreground);
            Sleep(1);
        }
        return peer.receive();
    };
    test("Real loopback UDP roundtrip enables only foreground game",
         [&] { expect(exchange(1, true, true) == "MZC2 1 88 1 500 3 6000"); });
    test("Pause returns disabled command",
         [&] { expect(exchange(2, false, true) == "MZC2 2 88 0 500 3 6000"); });
    test("Focus loss returns disabled command",
         [&] { expect(exchange(3, true, false) == "MZC2 3 88 0 500 3 6000"); });
    test("Hazards return disabled command",
         [&] { expect(exchange(4, true, true, 1) == "MZC2 4 88 0 500 3 6000"); });
    test("Replayed UDP sequence has no reply or state update", [&] {
        peer.send("MZB2 4 12 0 0 0 0 0 0 0 0 0 0");
        bridge.poll(GetTickCount64(), true, 500, 3, 6000, 88, true);
        expect(peer.receive().empty() && bridge.latest.hazard == 1);
    });
    test("Malformed UDP leaves previous state intact", [&] {
        peer.send("MZB2 999 12 0 0 0 0 0 0 0 0 0 0; code()");
        bridge.poll(GetTickCount64(), true, 500, 3, 6000, 88, true);
        expect(peer.receive().empty() && bridge.latest.sequence == 4);
    });
    test("Lost game connection expires", [&] {
        expect(bridge.connected(bridge.seenAt + 1499) && !bridge.connected(bridge.seenAt + 1500));
    });
    test("Game restart on a new port can restart sequence at one", [&] {
        Peer restarted;
        restarted.send("MZB2 1 13 0 0 0 0 0 0 0 0 0 0");
        for (int i = 0; i < 100 && bridge.latest.vehicle != 13; i++) {
            bridge.poll(GetTickCount64(), true, 500, 3, 6000, 88, true);
            Sleep(1);
        }
        expect(restarted.receive() == "MZC2 1 88 1 500 3 6000" && bridge.latest.vehicle == 13);
    });
    printf("RESULT: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

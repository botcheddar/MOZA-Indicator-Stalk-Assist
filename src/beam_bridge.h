#pragma once
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
namespace moza {
constexpr unsigned short BeamPort = 40518;
struct BeamFrame {
    uint64_t sequence = 0, event = 0;
    int vehicle = 0, position = 0, comfort = 0, flashes = 0, hazard = 0, state = 0, kind = 0,
        elapsed = 0, left = 0, right = 0;
};
inline bool parseBeamFrame(const std::string &packet, BeamFrame &out) {
    if (packet.size() > 512)
        return false;
    BeamFrame f;
    std::string magic, extra;
    std::istringstream in(packet);
    if (!(in >> magic >> f.sequence >> f.vehicle >> f.position >> f.comfort >> f.flashes >>
          f.hazard >> f.state >> f.event >> f.kind >> f.elapsed >> f.left >> f.right))
        return false;
    if (magic != "MZB2" || (in >> extra) || !f.sequence || f.sequence > 1000000000000ULL ||
        f.vehicle < 0 || f.vehicle > 1000000000 || f.position < -1 || f.position > 1 ||
        f.comfort < -1 || f.comfort > 1 || f.flashes < 0 || f.flashes > 100 || f.hazard < 0 ||
        f.hazard > 1 || f.state < 0 || f.state > 6 || f.event > 1000000000000ULL || f.kind < 0 ||
        f.kind > 3 || f.elapsed < 0 || f.elapsed > 86400000 || f.left < 0 || f.left > 1 ||
        f.right < 0 || f.right > 1)
        return false;
    out = f;
    return true;
}
inline bool foregroundBeamNG() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    static DWORD cachedPid = 0;
    static ULONGLONG checked = 0;
    static bool cached = false;
    auto time = GetTickCount64();
    if (pid == cachedPid && time - checked < 2000)
        return cached;
    cachedPid = pid;
    checked = time;
    cached = false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    wchar_t path[32768];
    DWORD size = 32768;
    bool match = false;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        const wchar_t *name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        match = _wcsicmp(name, L"BeamNG.drive.x64.exe") == 0 ||
                _wcsicmp(name, L"BeamNG.drive.x64.vulkan.exe") == 0;
    }
    CloseHandle(process);
    cached = match;
    return match;
}
inline std::string beamReply(uint64_t sequence, uint64_t epoch, bool allow, int threshold,
                             int count, int maximum) {
    return "MZC2 " + std::to_string(sequence) + " " + std::to_string(epoch) + " " +
           (allow ? "1" : "0") + " " + std::to_string(threshold) + " " + std::to_string(count) +
           " " + std::to_string(maximum);
}
class BeamBridge {
    SOCKET socket = INVALID_SOCKET;
    bool winsock = false;
    uint64_t nextRetry = 0, lastEvent = 0;
    unsigned short peerPort = 0;

  public:
    BeamFrame latest;
    uint64_t seenAt = 0;
    std::string error;
    ~BeamBridge() {
        if (socket != INVALID_SOCKET)
            closesocket(socket);
        if (winsock)
            WSACleanup();
    }
    bool connected(uint64_t now) const {
        return seenAt && seenAt <= now && now - seenAt < 1500;
    }
    bool start(uint64_t now) {
        if (socket != INVALID_SOCKET)
            return true;
        if (now < nextRetry)
            return false;
        nextRetry = now + 2000;
        if (!winsock) {
            WSADATA data;
            if (WSAStartup(MAKEWORD(2, 2), &data)) {
                error = "BeamNG bridge could not initialize Windows sockets";
                return false;
            }
            winsock = true;
        }
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == INVALID_SOCKET) {
            error = "BeamNG bridge socket unavailable";
            return false;
        }
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(BeamPort);
        if (bind(socket, reinterpret_cast<sockaddr *>(&address), sizeof address) == SOCKET_ERROR) {
            error = "BeamNG local port 40518 is busy; ETS2/ATS remain available";
            closesocket(socket);
            socket = INVALID_SOCKET;
            return false;
        }
        u_long nonblocking = 1;
        if (ioctlsocket(socket, FIONBIO, &nonblocking) == SOCKET_ERROR) {
            error = "BeamNG bridge could not enter nonblocking mode";
            closesocket(socket);
            socket = INVALID_SOCKET;
            return false;
        }
        error.clear();
        return true;
    }
    std::vector<std::string> poll(uint64_t now, bool enabled, int threshold, int count, int maximum,
                                  uint64_t epoch, bool foreground) {
        std::vector<std::string> messages;
        if (!start(now))
            return messages;
        for (int n = 0; n < 16; n++) {
            char buffer[513];
            sockaddr_in peer = {};
            int length = sizeof peer;
            int bytes = recvfrom(socket, buffer, sizeof buffer, 0,
                                 reinterpret_cast<sockaddr *>(&peer), &length);
            if (bytes == SOCKET_ERROR) {
                int code = WSAGetLastError();
                if (code != WSAEWOULDBLOCK)
                    error = "BeamNG bridge receive error " + std::to_string(code);
                break;
            }
            if (peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK))
                continue;
            BeamFrame frame;
            if (!parseBeamFrame(std::string(buffer, bytes), frame))
                continue;
            bool newPeer = peer.sin_port != peerPort;
            if (!newPeer && frame.sequence <= latest.sequence)
                continue;
            if (newPeer) {
                peerPort = peer.sin_port;
                lastEvent = 0;
                messages.push_back("BeamNG mod connected on localhost");
            }
            latest = frame;
            seenAt = now;
            bool allow = enabled && foreground && frame.vehicle > 0 && !frame.hazard;
            const auto response =
                beamReply(frame.sequence, epoch, allow, threshold, count, maximum);
            if (sendto(socket, response.c_str(), int(response.size()), 0,
                       reinterpret_cast<sockaddr *>(&peer), length) == SOCKET_ERROR)
                error = "BeamNG bridge send failed";
            if (frame.event != lastEvent) {
                lastEvent = frame.event;
                messages.push_back("BeamNG " +
                                   std::string(frame.kind == 1   ? "LEFT"
                                               : frame.kind == 2 ? "RIGHT"
                                               : frame.kind == 3 ? "CENTRE"
                                                                 : "state") +
                                   "; elapsed=" + std::to_string(frame.elapsed) +
                                   "ms; flashes=" + std::to_string(frame.flashes));
            }
        }
        return messages;
    }
};
} // namespace moza

#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <functional>
#include <sstream>
#include "button_decoder.h"

namespace moza {
struct DeviceInfo {
    GUID instance, product;
    std::string name;
};
inline std::string guidText(const GUID &g) {
    char b[64];
    snprintf(b, sizeof b, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g.Data1, g.Data2,
             g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5],
             g.Data4[6], g.Data4[7]);
    return b;
}
inline void check(HRESULT hr, const char *what) {
    if (FAILED(hr)) {
        char b[160];
        snprintf(b, sizeof b, "%s failed: 0x%08lX", what, (unsigned long)hr);
        throw std::runtime_error(b);
    }
}
class Input {
    IDirectInput8A *api = nullptr;
    IDirectInputDevice8A *dev = nullptr;
    ButtonDecoder decoder;
    static BOOL CALLBACK collect(const DIDEVICEINSTANCEA *d, void *ctx) {
        ((std::vector<DeviceInfo> *)ctx)
            ->push_back({d->guidInstance, d->guidProduct, d->tszProductName});
        return DIENUM_CONTINUE;
    }

  public:
    DeviceInfo selected = {};
    std::vector<DeviceInfo> devices;
    Input() {
        check(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
                                 (void **)&api, nullptr),
              "DirectInput8Create");
    }
    ~Input() {
        close();
        if (api)
            api->Release();
    }
    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;
    void close() {
        if (dev) {
            dev->Unacquire();
            dev->Release();
            dev = nullptr;
        }
        decoder.clear();
    }
    void scan() {
        devices.clear();
        check(api->EnumDevices(DI8DEVCLASS_GAMECTRL, collect, &devices, DIEDFL_ATTACHEDONLY),
              "EnumDevices");
    }
    bool connected() const {
        return dev != nullptr;
    }
    bool open(HWND window) {
        close();
        scan();
        std::vector<DeviceInfo> matches;
        for (auto &d : devices)
            if (d.product.Data1 == 0x0024346e || d.name.find("Stalk") != std::string::npos ||
                d.name.find("stalk") != std::string::npos)
                matches.push_back(d);
        if (matches.empty())
            return false;
        if (matches.size() != 1)
            throw std::runtime_error("Multiple stalk devices found; disconnect the extra device "
                                     "before using this release");
        selected = matches[0];
        check(api->CreateDevice(selected.instance, &dev, nullptr), "CreateDevice");
        try {
            check(dev->SetDataFormat(&c_dfDIJoystick2), "SetDataFormat");
            check(dev->SetCooperativeLevel(window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND),
                  "SetCooperativeLevel");
            DIPROPDWORD p = {};
            p.diph.dwSize = sizeof p;
            p.diph.dwHeaderSize = sizeof p.diph;
            p.dwData = 1024;
            check(dev->SetProperty(DIPROP_BUFFERSIZE, &p.diph), "SetProperty buffer");
            check(dev->Acquire(), "Acquire");
            // Ignore already-held buttons until released, and discard pre-acquisition history.
            dev->Poll();
            DIJOYSTATE2 state = {};
            check(dev->GetDeviceState(sizeof state, &state), "Initial state");
            decoder.seed(state.rgbButtons);
            DWORD count = INFINITE;
            check(dev->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), nullptr, &count, 0),
                  "Flush input");
        } catch (...) {
            close();
            throw;
        }
        return true;
    }
    using Event = ButtonEvent;
    std::vector<Event> read(uint64_t) {
        std::vector<Event> result;
        if (!dev)
            return result;
        HRESULT poll = dev->Poll();
        if (FAILED(poll)) {
            close();
            throw std::runtime_error("Stalk Poll failed/disconnected; state cleared");
        }
        unsigned processed = 0;
        for (;;) {
            DIDEVICEOBJECTDATA batch[128];
            DWORD count = 128;
            HRESULT hr = dev->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), batch, &count, 0);
            if (hr == DI_BUFFEROVERFLOW) {
                close();
                throw std::runtime_error("Input buffer overflow; history discarded, state cleared");
            }
            if (FAILED(hr)) {
                close();
                check(hr, "GetDeviceData");
            }
            processed += count;
            if (processed > 4096) {
                close();
                throw std::runtime_error("Input flood exceeded per-frame limit; history discarded");
            }
            // Sample AFTER the API read: an event may arrive while Poll/GetDeviceData runs.
            // Sampling before it would misclassify a fresh event as a 49-day-old timestamp.
            try {
                auto decoded = decoder.decode(batch, count, GetTickCount64());
                result.insert(result.end(), decoded.begin(), decoded.end());
            } catch (...) {
                close();
                throw;
            }
            if (count < 128)
                break;
        }
        return result;
    }
};
} // namespace moza

#pragma once
#include <windows.h>
#include <dinput.h>
#include <vector>
#include <stdexcept>
#include <cstdint>
namespace moza {
struct ButtonEvent {
    int button;
    uint64_t time;
};
// Translates a full buffered button report. Releases only rearm a rising edge;
// they do not represent the physical stalk returning to centre.
class ButtonDecoder {
    bool down[128] = {};

  public:
    void clear() {
        for (bool &value : down)
            value = false;
    }
    void seed(const BYTE *buttons) {
        for (int i = 0; i < 128; i++)
            down[i] = (buttons[i] & 0x80) != 0;
    }
    std::vector<ButtonEvent> decode(const DIDEVICEOBJECTDATA *batch, DWORD count,
                                    uint64_t receivedAt) {
        std::vector<ButtonEvent> result;
        for (DWORD n = 0; n < count; n++) {
            const auto &event = batch[n];
            if (event.dwOfs < DIJOFS_BUTTON(0) || event.dwOfs > DIJOFS_BUTTON(127))
                continue;
            unsigned index = event.dwOfs - DIJOFS_BUTTON(0);
            bool pressed = (event.dwData & 0x80) != 0;
            if (pressed && !down[index]) {
                uint32_t age = uint32_t(receivedAt) - event.dwTimeStamp;
                if (age > 1000 || age > receivedAt)
                    throw std::runtime_error("Stale or invalid input timestamp; state cleared");
                result.push_back({int(index) + 1, receivedAt - age});
            }
            down[index] = pressed;
        }
        return result;
    }
};
} // namespace moza

#pragma once
#include <deque>
namespace moza {
inline uint64_t testNow = 100000;
inline bool testFocused = true, testConnected = true, testReadError = false;
inline std::deque<Input::Event> testEvents;
class FakeInput {
    bool attached = false;

  public:
    DeviceInfo selected = {};
    FakeInput() {
        selected.name = "Simulated MOZA Stalk";
    }
    bool connected() const {
        return attached;
    }
    void close() {
        attached = false;
    }
    bool open(HWND) {
        attached = testConnected;
        testEvents.clear();
        return attached;
    }
    std::vector<Input::Event> read(uint64_t) {
        if (testReadError) {
            close();
            throw std::runtime_error("Simulated disconnect or overflow");
        }
        std::vector<Input::Event> r(testEvents.begin(), testEvents.end());
        testEvents.clear();
        return r;
    }
};
} // namespace moza

#include "input.h"
#include "engine.h"
#include <cstdio>
#include <fstream>
#include <conio.h>
int main(int argc, char **argv) {
    try {
        unsigned seconds = argc > 1 ? unsigned(atoi(argv[1])) : 30;
        moza::Input input;
        input.scan();
        std::ofstream log("logs/input-diagnostic.log", std::ios::app);
        auto print = [&](const std::string &line) {
            printf("%s\n", line.c_str());
            fflush(stdout);
            log << line << std::endl;
        };
        print("MOZA buffered DirectInput diagnostic - NO OUTPUT");
        for (auto &d : input.devices)
            print("DEVICE " + d.name + " instance=" + moza::guidText(d.instance) +
                  " product=" + moza::guidText(d.product));
        if (!input.open(GetConsoleWindow())) {
            print("No matching MOZA stalk is connected.");
            return 2;
        }
        print("CONNECTED " + input.selected.name +
              ". Move left, return centre; then right, return centre. Q stops.");
        moza::Engine engine;
        auto start = GetTickCount64();
        while (GetTickCount64() - start < seconds * 1000ULL) {
            auto now = GetTickCount64();
            for (auto &e : input.read(now)) {
                if (e.button == 10)
                    engine.direction(moza::Side::Left, e.time);
                if (e.button == 8)
                    engine.direction(moza::Side::Right, e.time);
                if (e.button == 9)
                    engine.centre(e.time);
                print("t=" + std::to_string(e.time) + " button=" + std::to_string(e.button) +
                      " state=" + engine.classification +
                      " direction_to_centre_ms=" + std::to_string(engine.elapsed));
            }
            if (_kbhit() && (_getch() | 32) == 'q')
                break;
            Sleep(4);
        }
        print("Diagnostic finished. No simulator commands were sent.");
    } catch (const std::exception &e) {
        fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
}

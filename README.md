# MOZA Indicator Stalk Assist

Free quick-tap turn signals for the **MOZA Multi-function Stalk** on Windows x64. Make a quick flick for a short sequence of flashes, or hold the stalk longer to signal until it returns to centre.

**[Download the Windows app](https://github.com/botcheddar/MOZA-Indicator-Stalk-Assist/releases/download/v2.0.1-preview/MOZA-Indicator-Stalk-Assist-2.0.1-preview.zip)** · **[Setup tutorial](docs/SETUP.md)** · [Report a problem](https://github.com/botcheddar/MOZA-Indicator-Stalk-Assist/issues)

Download `MOZA-Indicator-Stalk-Assist-2.0.1-preview.zip` under the release's **Assets**. GitHub's separate **Source code** archives are for building the program yourself.

| Game | Default flashes per quick flick | Validation |
| --- | ---: | --- |
| Euro Truck Simulator 2 | 5 | Tested on a real MOZA stalk |
| American Truck Simulator | 5 | Automated tests pass; real game check pending |
| BeamNG.drive | 3 | Tested on a real MOZA stalk |

Counts are adjustable for each game. They include the first flash started by the physical stalk movement.

## Set up once

1. Extract the downloaded app ZIP to a permanent writable folder.
2. Install the integration for your game. The ZIP contains the **SCS DLL** and **BeamNG mod ZIP**. Follow the [game-specific steps](docs/SETUP.md).
3. Open `MOZAIndicatorStalkAssist.exe` and complete controller setup: three quick flicks each way, then a two-second hold and return to centre each way.
4. Select **Start with Windows** if you want it to run quietly after sign-in.
5. Check normal held signals first, then quick flicks, with the vehicle parked and electrics on.

The [tutorial](docs/SETUP.md) covers the required ETS2/ATS position bindings as well as the three BeamNG actions. The app does not overwrite profiles automatically.

## Everyday use

A return to centre within 500 ms is a quick flick. Holding longer keeps normal indication active until centre. A same-side repeat cancels continuation; an opposite flick switches direction. Full detent takes priority over the tap sequence.

Closing the settings window keeps the app in the tray. **Pause extra flashes** disables continuation; tray **Exit** stops the app. Your normal position bindings continue to work. The UI supports sharp per-monitor DPI scaling, including 4K displays.

No AntiMicroX macro, virtual controller, additional driver, Windows service, Python or .NET installation is needed. Keep your normal MOZA software and unrelated bindings as usual.

## BeamNG and Better Bindings

The included `moza_turn_signal_assist.zip` is this project's separate indicator add-on. It works alongside Better Bindings and also provides the indicator actions without it. Better Bindings itself is not included or modified. Keep it installed if your cruise or lighting bindings use it.

## Source and tests

The original app and add-on are [MIT licensed](LICENSE). Source is in `src/`, tests in `tests/`. [Build instructions](BUILDING.txt) use a pinned official Zig compiler and Lua test runtime. **147 automated tests pass**, alongside binary/GUI smoke checks and the physical tests listed above. See [test results](docs/TEST-RESULTS.txt) for remaining coverage.

This is a public preview. Custom vehicles, other firmware, low frame rates and long driving sessions may need further testing. No personal profiles, controller calibration or live driving logs are shipped.

Independent community project; no affiliation or endorsement by MOZA, SCS Software or BeamNG.

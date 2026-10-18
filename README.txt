MOZA INDICATOR STALK ASSIST 2.0.1 - PREVIEW

New users: start with START-HERE.txt, then INSTALL.txt for game-specific details.

Quick-tap turn signals for the MOZA Multi-function Stalk on Windows x64.
Free, open source, MIT licensed.

Default total flashes per quick flick:
  Euro Truck Simulator 2   5
  American Truck Simulator 5
  BeamNG.drive             3

Hold the stalk longer than 500 ms and the signal continues until physical centre.
A quick return means the distinct centre pulse, not an ordinary button release.
The count includes the first flash started by the physical movement.

Start MOZAIndicatorStalkAssist.exe. First use needs three quick flicks each way and one
2-second hold/centre each way. Saved controller setup is reused next time.
Use Details / button test if setup is needed. Then choose Start with Windows
for automatic background operation. Autostart is opt-in for this public package.

Closing the window hides it in the tray and keeps it running. Pause extra flashes
turns off tap continuation. Right-click the tray icon > Exit stops the app.
Normal physical stalk control remains available when extra flashes are paused.

Install the appropriate game integration first; see INSTALL.txt.
The app does not automatically overwrite game controls or install game files.
One app handles all three games. No AntiMicroX macro, virtual controller,
additional driver, Python, .NET runtime, or Windows service is needed.

Real MOZA hardware tests passed for BeamNG three-flash taps/holds/cancellation
and ETS2 five-flash continuation. ATS and wider recovery/vehicle coverage remain
to be tested physically. Automated results are in TEST-RESULTS.txt.
Do not present the preview as validated on all vehicles or firmware versions.

Source is included in source/. Build with PowerShell: .\Build.ps1 -Bootstrap.
The pinned compiler and Lua test sources are fetched from their official sites
and verified by SHA256. See source/BUILDING.txt.

Better Bindings is optional and is not included or modified by this software.
No personal controller validation, game profile, live log, or saved-game file
is included. MOZA Indicator Stalk Assist has no remote network endpoint or update downloader;
BeamNG communication uses only UDP on this computer's loopback interface.

# Set up MOZA Indicator Stalk Assist

This guide installs the app and the integration for your game. Use a MOZA Multi-function Stalk connected to a Windows x64 PC. Close the games while copying files or editing bindings.

## 1. Download and extract

1. Open the [release page](https://github.com/botcheddar/MOZA-Indicator-Stalk-Assist/releases/tag/v2.0.1-preview).
2. Under **Assets**, download **MOZA-Indicator-Stalk-Assist-2.0.1-preview.zip**. Choose the app ZIP, rather than GitHub's separate Source code archives.
3. Right-click the downloaded ZIP and choose **Extract All**. Put it somewhere permanent and writable. Keep the extracted files together.

You should see:

```text
MOZAIndicatorStalkAssist.exe
Comfort.ini
START-HERE.txt
INSTALL.txt
integrations/
  scs/MozaComfortPlugin.dll
  beamng/moza_turn_signal_assist.zip
source/
```

## 2. Set up the controller

1. Connect the stalk and open **MOZAIndicatorStalkAssist.exe**.
2. If the app says **Finish controller setup**, open **Details / button test**.
3. Make **three quick LEFT flicks**, returning to centre each time. Do the same **three times RIGHT**.
4. Hold LEFT for about **two seconds**, then return to centre. Repeat RIGHT.
5. The main window should say **MOZA stalk connected | Setup complete** and **Tap-to-flash is on**.

A quick flick must return to centre within 500 ms. Releasing the temporary joystick button is not the same as the stalk's distinct centre pulse. Setup is saved for this particular controller, so it is normally needed only once.

## 3. Choose your game

### Euro Truck Simulator 2 and American Truck Simulator

1. Close the game.
2. Find its installation folder. In Steam, right-click the game, then use **Manage > Browse local files**.
3. Open `bin/win_x64/plugins`. Create the `plugins` folder if it does not exist.
4. Copy **integrations/scs/MozaComfortPlugin.dll** from the app download into that folder. Leave all other plugins in place.
5. Check the profile's ordinary stalk controls. Full LEFT/RIGHT must activate immediately, stay on while held, and cancel at physical centre. The hold mixes must also include `semantical.lblinkerh` and `semantical.rblinkerh`. See [position bindings](#ets2-and-ats-position-bindings) below if these are not set up.
6. Start the game. Acknowledge its SDK/plugin notice if shown. The app should show **Connected** for that game when its SDK is running.

Repeat the DLL installation separately for each SCS game you use. A profile copied from someone else's PC can change steering, pedals and other devices; use your own profile.

### BeamNG.drive

1. Use the BeamNG launcher's **Manage User Folder** option to open the active user folder. It may be at a custom location.
2. With the game closed, copy **integrations/beamng/moza_turn_signal_assist.zip** into that user folder's `mods` directory. **Leave this inner ZIP intact.**
3. Start BeamNG. Enable the add-on in Mod Manager if it is disabled.
4. In **Controls**, assign the following actions to the MOZA stalk:

| New action | Physical movement | Tested BeamNG input-map control |
| --- | --- | --- |
| MOZA stalk: left position | Enter LEFT | `button9` |
| MOZA stalk: right position | Enter RIGHT | `button7` |
| MOZA stalk: centre position | Return to CENTRE | `button8` |

BeamNG's saved input-map numbers are zero-based. The same tested inputs are DirectInput buttons **10 / 8 / 9** in the app. Identify the actual movement if your firmware or displayed button labels differ.

5. Remove duplicate old left/right toggle or centre-cancel assignments for **those three physical controls only**. Preserve all other stalk, keyboard and wheel assignments.
6. Keep Better Bindings installed if other controls use it. This project's indicator add-on works alongside it; it does not replace its cruise and lighting actions.
7. Load a car. The app should show **Mod connected**. Normal direction/centre control works through the add-on even if extra flashes are paused; continuation needs the desktop app running.

## 4. Test it while parked

Keep the game focused and unpaused, ignition/electrics on, hazards off.

- **Normal hold:** signal LEFT and RIGHT normally. Each must stay on until centre.
- **Quick flick:** allow **five total flashes** in ETS2/ATS or **three** in BeamNG.
- **Soft hold:** hold the spring-return position for over half a second. It should blink until you release it to centre.
- **Cancel:** during a tap sequence, flick the same side again.
- **Switch:** during a tap sequence, flick the opposite side.
- **Full detent:** engage a normal held turn during a tap sequence. It must stay on until centre.

ETS2 five-flash and BeamNG operation have been tested on real hardware. ATS's automated checks pass; its real game check is still pending in this preview.

## 5. Make it automatic

Select **Start with Windows**, **Start quietly in the tray**, and **Automatically turn on after controller setup**. Windows startup is optional and is not enabled just by extracting the public package.

Closing the window hides it while the app continues working. Double-click the tray icon, or right-click it and choose **Open settings**, to return. **Pause extra flashes** pauses tap continuation. Tray **Exit** stops the app.

If you move the app folder later, reopen it from the new location and select Start with Windows again.

## ETS2 and ATS position bindings

This section is needed if the profile does not already reconstruct the stalk position correctly, or does not include the semantic hold inputs.

1. Close the game and back up the active profile's **controls.sii**. Find the active profile under the game's Documents folder (`profiles` or `steam_profiles`, depending on that profile). Documents may be redirected to another drive or OneDrive.
2. Open the backed-up profile's working **controls.sii** in a text editor. Find its existing `mix lblinkerh` and `mix rblinkerh` lines.
3. Identify the stalk's real joystick alias in that profile, such as `joy2`. **joyN below is a placeholder**: replace it with the correct alias. The alias is not necessarily the same across games or PCs.
4. Edit only the expression between the backticks in each existing line. Preserve the surrounding `config_lines[...]` number and quotes.

For `mix lblinkerh`, use:

```text
memory(joyN.b10?0 | joyN.b9?0, joyN.b10?0 & !(joyN.b9?0)) | semantical.lblinkerh?0
```

For `mix rblinkerh`, use:

```text
memory(joyN.b8?0 | joyN.b9?0, joyN.b8?0 & !(joyN.b9?0)) | semantical.rblinkerh?0
```

These button numbers match the tested stalk firmware: LEFT 10, RIGHT 8, CENTRE 9. They are one-based SCS/DirectInput names. If your firmware maps differently, identify the correct events first and adjust both the expressions and the app's `Comfort.ini`.

5. Preserve keyboard and unrelated controls. If those same physical direction buttons are also assigned to ordinary toggle actions (`mix lblinker` / `mix rblinker`), review that conflict rather than removing keyboard controls.
6. Save, start the game and test normal held turns first. Restore the backup if those do not behave correctly.

These are expression examples, not a complete replacement profile. The app itself never overwrites a profile automatically.

## If something does not work

| What you see | What to check |
| --- | --- |
| Waiting for your MOZA stalk | USB connection and whether the correct stalk is detected. Do not reset unrelated devices. |
| Finish controller setup | Complete the quick flicks and two-second holds in Details / button test. |
| Waiting for game | DLL is in that game's `bin/win_x64/plugins` folder and the game has loaded its SDK. |
| Waiting for BeamNG mod | Correct active user folder, ZIP kept intact, add-on enabled. |
| Held signals work but quick flicks do not | App is on, correct centre action/mix, game focused/unpaused, hazards off. |
| Wrong flash count | Per-game dropdown, correct physical binding, and whether the game supplies lamp state normally. |

After pause, alt-tab, reconnection or app restart, use a fresh stalk movement. Old tap sequences are discarded. For a report, include game/app versions, which side and movement failed, and a short relevant log excerpt. Remove personal paths and device-instance identifiers before posting logs.

## Remove or undo

Uncheck Start with Windows, use tray Exit and close the games. Remove only this project's SCS DLL or BeamNG add-on, and restore any profile/binding backup you changed during setup. For the BeamNG add-on, restore the old three indicator bindings before removing it. Other mods and telemetry plugins can stay in place.

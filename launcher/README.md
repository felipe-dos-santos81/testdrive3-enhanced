# Test Drive III Enhanced (the launcher)

`Test Drive III Enhanced.exe` starts `testdrive3-enhanced`, Test Drive III: The Passion on SDL3 with the
enhanced view, with the options chosen in its window. It is built with [wxWidgets](https://www.wxwidgets.org/) 3.2 from the platform's own controls,
like the Test Drive II Enhanced and Test Drive III launchers it is modelled on.

## The window

* **Game files**
  * **Folder**: the folder with the original game's files (default `Game` beside the launcher).
  * **Program**: `testdrive3-enhanced.exe` (default: beside the launcher).
  * The line below says how many cars and courses the folder has, or what is missing (`TDIII.EXE`,
    `PLAYDISK.DAT`, `DATAA/B/C.DAT`, `INSTR.DAT`). **Play** stays greyed out until the folder and the program
    are there.
* **Start**, as if chosen in the game's menus (which can still change them; the game remembers them in
  `PLAYDISK.DAT` like its own choices):
  * **Car** (`--car`) and **Course** (`--course`): the slots of `PLAYDISK.DAT` whose `.LST` is in the folder,
    named as their `.LST` names them. *Default* is the game's own last choice.
  * **Skill level** (`--skill`): 1-9 as the game shows it; 1-3 have an automatic gearbox.
* **Options**
  * **Game speed** (`--frame-ticks`, default 14, chosen by play-testing): timer ticks (145.6 a second) per frame while driving. The
    game moves the cars once per frame, so fewer ticks make everything faster: your car, the traffic, the police
    and the opponents. At 10 the scenery passes about as fast as the speedometer says; 23 (the faithful port's
    default) passes it at about a third. The race clock always counts real seconds. Stored as `GameSpeed`.
  * **Sound** (`--sound`): AdLib / Sound Blaster (through Nuked-OPL3) or the PC speaker. `TD3.CFG` is not
    changed.
  * **Window size** (`--scale`) and **Start in full screen** (`--fullscreen`; Alt+Enter switches).
  * **Finish direction on the compass** (`--finish-marker`, on by default): a green mark on the compass points
    to the leg's gas station. Stored as `FinishMarker` in `settings.ini`.
* **Picture** (the enhanced view, `../ENHANCED.md`):
  * **Graphics**: *Enhanced* (the 3D view and the mirror drawn again, smooth and at a high resolution) or
    *Original* (`--classic`: the game's own 320 x 200 picture, scaled).
  * **Resolution** (`--res-scale`, default 1280 x 800): the size of the picture; the window scales it to fit.
  * **Anti-aliasing** (`--aa`, default 2 x 2): samples per pixel; *Off* on a slow computer.
  * **Motion** (`--motion-delay`): *Smooth* (default, 100: moves between the game's last two frames, never
    guesses), *Balanced* (50) or *Most direct* (0), which guess half or a whole frame ahead: they answer sooner
    but overshoot and pull back when the steering changes. Stored as `Motion` in `settings.ini`.
  * **Haze** (`--haze`): distance haze, what is far away takes on some of the sky's colour at the horizon:
    *Normal* (default, 30), *Light* (15), *Strong* (50) or *Off* (0). Stored as `Haze` in `settings.ini`.
  * **Draw distance** (`--draw-distance`): how far the scenery reaches: *Far* (default, 7 cells around the
    camera), *Medium* (5) or *Original* (0, the game's own cells). Stored as `DrawDistance` in `settings.ini`.
* **Keys in the game**: a reminder of the game's keys.
* **Play** starts the game; the launcher stays open. **About**: version, author and links.

Everything is remembered in `%APPDATA%\Test Drive III Enhanced\settings.ini` (`~/.config/test-drive-iii-enhanced` on Linux). A
folder or program left at its default is stored empty, so it follows the launcher if the whole folder moves.
Delete the file to go back to the defaults.

## Building

Needs CMake 3.24, a C++17 compiler and wxWidgets 3.2 (MSYS2 `mingw64`: `mingw-w64-x86_64-wxwidgets3.2-msw`;
Debian and Ubuntu: `libwxgtk3.2-dev`). The main build makes it beside `testdrive3-enhanced.exe` (`-DTD3E_LAUNCHER=OFF` leaves it out):

```bash
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

It also builds on its own (`cmake -S launcher -B launcher/build -G Ninja`); then choose the program in the
window or copy the launcher next to it.

On Windows the build copies every DLL the launcher needs beside it (`copy_dlls.cmake`): the two wxWidgets DLLs
and the MSYS2 libraries they load. Keep them with the `.exe` in a release. The C++ runtime of the launcher
itself is linked in.

## Files

* `app.cpp`: the wxWidgets application.
* `launcher.h`, `launcher.cpp`: the window and the About box.
* `game.h`, `game.cpp`: reading `PLAYDISK.DAT` and the `.LST` names, the file checks and starting the game.
* `settings.h`, `settings.cpp`: `settings.ini`.
* `icon.h`, `icon.cpp`: the app icon, a banded road running to the horizon drawn in code. `make_icon.py`
  (Pillow) writes the same drawing to `app.ico` for Explorer.
* `app.rc`, `app.manifest`, `app.ico`, `version.h`: icon, visual styles, DPI awareness, version info.
* `copy_dlls.cmake`: the post-build DLL copy.
* `CMakeLists.txt`: the build, standalone or from the main project.

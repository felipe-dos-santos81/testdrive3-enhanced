# Test Drive III Enhanced — SDL3

An enhanced version of Accolade's *Test Drive III: The Passion* (1990), running natively on SDL3. The game
itself is the faithful C reimplementation of `TDIII.EXE` from the Test Drive III port (VGA, AdLib / Sound
Blaster). On top of it, the 3D view and the rear-view mirror are drawn again at a high resolution with
anti-aliasing, and they move **smoothly at your screen's refresh rate** instead of the game's own 6 frames a
second, from the original data only (see `ENHANCED.md`). It is not an emulator. The original data is not
redistributed, and you need to get it yourself.

## What is enhanced

* **Smooth motion.** The game still moves the cars and runs its clock about 6 times a second, as it was
  tuned for. Between those frames the camera, the other cars and the moving sprites are carried smoothly, so
  the view runs at 60 frames a second (or whatever your display does). How far the smooth view runs behind the
  game is adjustable (`--motion-delay`, "Motion" in the launcher).
* **High resolution.** The polygons, lines, lamps and sprites are drawn at 1280 × 800 by default (up to
  2560 × 1600) instead of 320 × 200, with exact positions instead of whole pixels, and anti-aliased edges.
  Sprites grow smoothly with distance instead of jumping between sizes.
* **Smoother colours.** The original's two-colour checkerboard dithers are shown as the colour they stand for,
  and the sky's banded gradient is a continuous one.
* The mirror, the wipers, the dashboard edge and the headlight beams get the same treatment. The dashboard,
  the instruments, the menus and messages stay as the original drew them.
* **Faster, truer speed.** The game runs at 10.4 frames a second instead of the faithful port's 6.3
  (`--frame-ticks 14`): at 23 the scenery passes at about a third of the speedometer's speed, which the original's
  jerky picture hid; 10 matches the speedometer. The race clock counts real seconds instead of frames.
* The draw distance, the scenery and the rest of the game are unchanged.

## How to play (Windows)

You need the files of the original DOS *Test Drive III* (`TDIII.EXE`, `DATAA.DAT` … and the car and scene
files). They are not included.

1. Put your original game files in a folder named `Game`.
2. Copy the program's files into the folder that holds `Game`, so that `Test Drive III Enhanced.exe` sits
   next to `Game`:

   ```text
   Test Drive III Enhanced\
   ├── Game\                           <- your original game files (TDIII.EXE, DATAA.DAT, PLAYDISK.DAT, ...)
   ├── Test Drive III Enhanced.exe     <- the launcher
   ├── testdrive3-enhanced.exe         <- the game
   ├── SDL3.dll
   └── (the other DLLs)
   ```

3. Double-click `Test Drive III Enhanced.exe`, choose your car, course and options, and press **Play**.
   (`testdrive3-enhanced.exe` also starts on its own with the defaults.)

Keep the folder somewhere you can save files, not Program Files: the game saves its high scores
(`SCENE0x.HI`) and your last choices (`PLAYDISK.DAT`) in `Game`. Press Alt+Enter for full screen.

## The launcher

`Test Drive III Enhanced.exe` starts the game with the options chosen in its window (`launcher/README.md`):

* **Game files**: the game folder and `testdrive3-enhanced.exe` (by default both beside the launcher).
* **Start**: the car, course and skill level the game starts with, as if chosen in its own menus.
* **Options**: game speed (timer ticks per frame while driving, default 14), sound, window size, full screen,
  the finish's direction on the compass.
* **Picture**: enhanced or original graphics, the resolution, anti-aliasing and the motion setting.

Settings are remembered in `%APPDATA%\Test Drive III Enhanced\settings.ini`.

## Requirements

* Your game files in a folder (by default `Game` beside the program): `TDIII.EXE` (packed or unpacked),
  `PLAYDISK.DAT`, `DATAA.DAT`, `DATAB.DAT`, `DATAC.DAT`, `INSTR.DAT` and the car (`C*.DAT/.LST/.POB`) and scene
  (`SCENE*.DAT/.LST/.HI`) files. The folder must be writable.
* A CPU with a few cores for the default picture (it draws 1280 × 800 with 2 × 2 anti-aliasing in about 3 ms
  on 16 threads); lower the resolution or turn anti-aliasing off on a slow computer.
* To build: CMake 3.24+, a C11 compiler and SDL 3; for the launcher a C++17 compiler and wxWidgets 3.2.

## Build

From the repository root, in Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds the game and the launcher (`-DTD3E_LAUNCHER=OFF` for the game only) and copies `SDL3.dll` and the
launcher's wxWidgets DLLs next to them. The MSYS2 `SDL3.dll` also needs `libiconv-2.dll` from
`C:\msys64\mingw64\bin`.

## Run

```bash
./build/testdrive3-enhanced.exe --game-dir Game
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | Folder with the original game files (default `Game`) |
| `--scale N` | Initial window size as a multiple of 320×240 (default 3) |
| `--fullscreen` | Start in full screen |
| `--finish-marker 0\|1` | A green mark on the compass pointing to the leg's finish, the gas station (default 1) |
| `--res-scale N` | Picture resolution: 320×200 times N (default 4 = 1280×800, 1–8) |
| `--aa N` | Anti-aliasing: N×N samples per pixel (default 2, 1 = off; resolution × N is kept ≤ 16) |
| `--motion-delay P` | How far the smooth view runs behind the game, in percent of a game frame (default 100: moves between the game's frames, never guesses ahead; 50 or 0 guess half or a whole frame ahead, answering sooner but overshooting when the steering changes) |
| `--haze P` | Distance haze: what is far away takes on this percent of the horizon's sky colour (default 30, 0 = off) |
| `--classic` | The original picture only, no enhanced view (scaled to `--res-scale`, default 1) |
| `--frame-ticks N` | Game speed: timer ticks per frame while driving (default 14, chosen by play-testing; 10: the scenery passes about as fast as the speedometer says; the faithful port's 23 passes it at a third; 5 = the original's limit). The race clock counts real seconds at any speed |
| `--sound adlib\|speaker` | Sound device (default: `TD3.CFG`'s, AdLib without it) |
| `--car CODE`, `--course CODE` | Start with this car / course, e.g. `CCNSX`, `SCENE02` |
| `--skill N` | Skill level 1–9 (1–3 automatic gearbox) |
| `--check` | Verify that `TDIII.EXE` loads, then exit without opening a window |

## Controls (from the original)

* Arrow keys / numeric keypad: steer, accelerate, brake. A / Z shift up / down (Enter + Up/Down: the gear
  gates); skill levels 1–3 shift automatically.
* R mirror, H headlights, W wipers, C wheel centring, M radio station.
* F1 window size, F2 detail, F3 steering sensitivity, F5 chase car view, F6 return to the road, F7 mouse
  steering, F10 instant replay, F9 pause the replay.
* Esc: leave the race or the game. Ctrl-P pause, Ctrl-S sound, Ctrl-Q music, Ctrl-E engine sound.
* A connected gamepad acts as the joystick: Ctrl-J joystick, Ctrl-K keyboard. The mouse can steer (F7).

## Layout

* `src/` — the faithful port (`ENGINE.md`: architecture and rules; deliberate changes are marked `ENH:`).
* `src/enhanced/` — the enhanced renderer (`ENHANCED.md`).
* `launcher/` — the launcher (`launcher/README.md`).
* `third_party/nuked-opl3/` — the OPL2 emulator.

## License

The code and launcher are MIT licensed (see `LICENSE`). Nuked-OPL3 (`third_party/nuked-opl3`) is LGPL 2.1 or
later. *Test Drive III* is © Accolade. Its files are not part of this repository.

## Support

https://buymeacoffee.com/krzysztofkania

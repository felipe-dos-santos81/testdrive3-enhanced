# macOS (Apple Silicon) build and run — design

Date: 2026-09-30. Status: approved. Version after this change: 0.2.1.

## Goal

Build and run Test Drive III Enhanced natively on Apple Silicon (macOS, arm64) with SDL3, windowed, with the
mouse cursor never captured, hidden or locked to the window. A `Makefile` in the house style of the project's
other pipeline Makefile drives configure, build, run and check.

## Scope

In scope:

* The three toolchain failures that stop the engine compiling with Apple clang.
* Compiler policy for clang in `CMakeLists.txt`; no other build-system change.
* A macOS `Makefile` wrapping the existing CMake + Ninja build.
* README and CHANGELOG documentation, and the version bump to 0.2.1.

Out of scope:

* The wxWidgets launcher. It stays buildable in principle but is not ported; on macOS it is off by default.
  `make run` variables take the launcher's place (see below).
* A `.app` bundle, code signing, notarisation.
* Fullscreen, and any cursor-capture or raw-mouse mode.
* Porting or re-verifying the Windows build (see *Risks*).
* Building SDL3 from source. Homebrew's arm64 `sdl3` is used.

## Environment facts (verified on this machine)

* macOS 27.0 on arm64; Apple clang 21.0.0; Homebrew 7.0.7; cmake 4.4.3; ninja 1.13.
* `sdl3` 3.4.16 is installed (`arm64_golden_gate` bottle) and provides the CMake package config, so
  `find_package(SDL3 REQUIRED CONFIG)` resolves to `/opt/homebrew`.
* The engine (`src/`, ~21k lines of C11) contains no Windows-only code. The only Windows-specific constructs in
  the whole tree are in `launcher/CMakeLists.txt` and one `static const char cwd[] = "C:\\"` in the DOS
  `getcwd` model (`src/platform/dos.c:312`), which is a deliberate part of the emulated DOS environment.
* Configuring succeeds; compiling fails on exactly two source files (below).
* A native `arm64` compile is the default: the build already emits `-arch arm64` with no extra flags.

## 1. Source changes

All three edits are behaviour-preserving. The engine's faithfulness rules (`ENGINE.md`) are unaffected.

### 1.1 `src/enhanced/enhanced.c` — `abs` without `<stdlib.h>`

`abs()` is called at `src/enhanced/enhanced.c:290` but `<stdlib.h>` is not included (the file includes
`math.h`, `stdio.h`, `string.h`). Apple clang 21 rejects the implicit declaration as an error in C99+; MinGW
tolerated it. Fix: add `#include <stdlib.h>` to the include block, in alphabetical order.

### 1.2 `src/game/flow.h`, `src/game/flow.c` — the ported `random()` collides with libc

The port reimplements the original's own generator at `0000:0f58` as `u16 random(void)`
(`src/game/flow.c:431`, declared at `src/game/flow.h:16`). macOS declares `long random(void)` in
`<stdlib.h>`, so every translation unit that pulls in `flow.h` fails with *conflicting types for 'random'*.

Fix: rename the function to `td3_random` at its declaration, its definition, and all seven call sites:

| File | Lines |
|---|---|
| `src/game/flow.h` | 16 (declaration) |
| `src/game/flow.c` | 431 (definition) |
| `src/platform/view.c` | 92 |
| `src/game/render.c` | 129 |
| `src/game/flow_screens.c` | 263, 398, 520 |
| `src/game/flow_race.c` | 131, 177 |

The name follows the existing prefix convention (`crt_` for the DOS/CRT replacements, `enh_` for the enhanced
renderer). Doc comments keep the original's name and address (`0000:0f58 random`) so the mapping back to
`TDIII.EXE` stays visible. The generator's arithmetic (seed `DS:00D2`, `* 41C64E6D + 3039h`, returns
`(seed >> 16) & 7FFFh`) is not touched, and libc's `random()` becomes reachable again for any future caller.

### 1.3 `src/game/sim_physics.c:213` — no change

```c
/* 11. nose pitch: the original's clamp (t > FFh && t < -FFh) can never hold, so it always stores */
if (!(t > 0xFF && t < -0xFF)) DSS(DS_pitch) = t;
```

Apple clang warns `-Wtautological-overlap-compare`. The condition is a faithful reproduction of an original
quirk that its own comment documents; "fixing" it would change the physics. It is silenced by the compiler
flag in section 2, not by the code.

## 2. Build changes (`CMakeLists.txt`)

Toolchain policy belongs beside the existing toolchain policy, in the `else()` (non-MSVC) branch:

* Add `-Wno-tautological-overlap-compare` for `CMAKE_C_COMPILER_ID MATCHES "Clang"`, mirroring the existing
  GNU-only `-Wno-format-truncation`. The MSVC and GNU paths and all `WIN32` blocks are untouched.
* Default `TD3E_LAUNCHER` to `OFF` unless `WIN32`. The option is currently `ON`, which makes the README's
  plain `cmake -S . -B build` fail on macOS at `find_package(wxWidgets 3.2 REQUIRED)`. It stays fully
  opt-in: `-DTD3E_LAUNCHER=ON` builds it wherever wxWidgets 3.2+ is installed. The option's default belongs
  with the option, not with the Makefile, so every documented build command works.

Nothing else changes. The `Makefile` passes `-DCMAKE_OSX_ARCHITECTURES=$(ARCH)` to make the target
architecture explicit rather than implicit.

## 3. The Makefile

A macOS-only `Makefile` in the style of the reference pipeline Makefile: a `SERVICE` header, an idempotent
`install` guarded by a test, a coloured `help` assembled from `## ` comments with the same
`printf`/`grep`/`awk` pipeline, `.PHONY` up top, `clean` last. It wraps CMake and never repeats a compile flag,
so there is one source of truth for the build.

Targets:

| Target | Action |
|---|---|
| `help` | The reference's help block, listing every target below |
| `install` | `brew install` whichever of `cmake`, `ninja`, `sdl3` is missing, then `configure` |
| `configure` | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DTD3E_LAUNCHER=OFF` |
| `build` | `cmake --build build` |
| `run` | `./build/testdrive3-enhanced --game-dir $(GAME_DIR) --scale $(SCALE)` plus any set options |
| `check` | the same binary with `--check`: loads and verifies `TDIII.EXE`, prints a summary, exits |
| `smoke` | headless proof that frames are composed (see below) |
| `clean` | `rm -rf build` |

Variables, all `?=` so the command line always wins:

| Variable | Default | Meaning |
|---|---|---|
| `BUILD_DIR` | `build` | CMake build directory |
| `BUILD_TYPE` | `Release` | `CMAKE_BUILD_TYPE` |
| `GENERATOR` | `Ninja` | CMake generator |
| `ARCH` | `arm64` | `CMAKE_OSX_ARCHITECTURES` |
| `GAME_DIR` | `Game` | folder with the original game files |
| `SCALE` | `3` | window size, × 320×240 |
| `SMOKE_DIR` | `build/smoke` | where `smoke` writes its snapshots |
| `SMOKE_SECONDS` | `6` | how long `smoke` runs before terminating the game |
| `CAR`, `COURSE`, `SKILL`, `SOUND`, `FRAME_TICKS`, `RES`, `AA`, `KEYS` | unset | forwarded **only when set**, as `--car`, `--course`, `--skill`, `--sound`, `--frame-ticks`, `--res-scale`, `--aa`, `--keys` |
| `ARGS` | unset | raw extra flags, e.g. `make run ARGS="--classic --haze 40"` |

`--scale` is always forwarded; every other option is forwarded only when the variable is set, using the
reference's `$(if $(VAR),--flag "$(VAR)")` idiom. Anything not forwarded keeps the game's own default, which
keeps `ARGS="--classic"` meaningful: forcing `--res-scale 4` would otherwise override the `1` that
`--classic` implies in `src/main.c:121`.

Usage: `make run`, `make run CAR=CCNSX COURSE=SCENE02 SKILL=3 SCALE=4`, `make check`, `make clean`.

`smoke` is the only target that is not a pipeline stage. It exists because it is the only deterministic proof
that the engine boots, reads the game files and composes frames without a human watching:

```sh
SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
TD3_SNAPSHOT_DIR=$(SMOKE_DIR) ./build/testdrive3-enhanced --game-dir $(GAME_DIR) & \
pid=$$!; sleep $(SMOKE_SECONDS); kill $$pid
```

one shell line, because a recipe line runs in its own shell and `$$!` is the portable way to get the
background job's pid (job control and `%1` are not available in a non-interactive shell). Then assert that
`$(SMOKE_DIR)` is non-empty. The `TD3_SNAPSHOT_DIR` aid is documented in `src/host.h:82-87`
and writes a BMP of every presented frame at least two seconds after the previous one, so a six-second run
yields snapshots. The game is terminated with `SIGTERM`, not shut down through `crt_exit`, so `smoke` never
writes high scores.

What `smoke` does **not** prove: it captures whatever the game is showing, which at boot is the title or menu
screen. It exercises startup, file loading, the VGA model and the present path — not the enhanced 3D race
renderer. Driving the menus to a race automatically would need a long `TD3_KEYS` script and is not worth it;
`make run` with a human is the proof for the race itself.

## 4. Windowed mode and the unlocked cursor

These are requirements of this port, and both are satisfied by the existing code plus the absence of a flag.

**Windowed.** `host_init` creates a resizable `320×240×scale` window and enters fullscreen only when
`fullscreen` is true (`src/host.c:58-64`), which only `--fullscreen` sets (`src/main.c:63,74`). The `Makefile`
never passes `--fullscreen`, so `make run` is always windowed. `Alt+Enter` still toggles fullscreen
(`src/host.c:477-481`): pre-existing behaviour on both platforms, untouched here, and harmless on macOS, where
SDL3 fullscreen is borderless-desktop rather than a dedicated mode.

**The cursor is never locked, and this port adds nothing that could lock it.** The tree contains no
`SDL_SetRelativeMouseMode`, no `SDL_SetWindowGrab` and no `SDL_HideCursor`; mouse input is read as ordinary
absolute position plus accumulated relative motion (`src/host.c:490-495`, `src/host.c:514-533`). The
invariant is therefore a property of the design, not a runtime check: *no code path in this repository may
capture, hide or confine the pointer, and the `Makefile` passes no flag that would.* It is stated in the
README so a future change has to confront it, and it is the reason a runtime guard is deliberately absent —
there is no condition to detect.

**Consequence, accepted.** `F7` mouse steering accumulates `ev.motion.xrel`; with the pointer uncaptured it can
leave the window, and macOS stops delivering motion to the window once it does, so mouse steering is reliable
only with the pointer near the middle of the window. Keyboard steering (arrows) is unaffected and `F7` is off
by default. A free cursor is also what makes the existing focus handling useful: `SDL_EVENT_WINDOW_FOCUS_LOST`
(`src/host.c:487-489`) releases keys held when the window loses focus, so clicking another application
mid-race does not leave the throttle or the steering stuck on.

## 5. Verification

Each step gates the next.

1. `make clean && make build` completes with no errors and no new warnings. `file build/testdrive3-enhanced`
   reports an arm64 Mach-O; `otool -L` shows the Homebrew SDL3 dylib.
2. `make check` with the original files in `Game/` prints
   `TDIII.EXE ok: image <n> bytes at 1000:0000, DGROUP 2BE4, frame pacing 14 ticks`. This is the real proof
   that the real-mode memory model loads the original executable on this platform, not merely that it compiles.
3. `make smoke` leaves at least one non-empty snapshot in `build/smoke`; each snapshot is 1280×800 (the
   default `--res-scale 4`) and not a uniform image, so real content was composed.
4. `make run` starts the real window, stays up for a few seconds, and exits cleanly when terminated.
5. `git diff` shows the MSVC, MinGW and `WIN32` paths in `CMakeLists.txt` and every Windows file untouched.

## 6. Documentation and versioning

* **README** — a `## How to play (macOS)` section beside the Windows one (put the original files in `Game/`,
  then `make run`; the folder must be writable because high scores and choices are saved there); `make`
  targets in Build and Run; `brew install sdl3` in Requirements; the cursor invariant stated in plain words.
  The Windows instructions are left as they are.
* **CHANGELOG** — a `## 0.2.1` entry: macOS (Apple Silicon) build through the new `Makefile`; windowed; the
  cursor is never captured or hidden.
* **Version** — `0.2.1` in `CMakeLists.txt` (`project(... VERSION ...)`) and `launcher/version.h`.
* **No `ENHANCED.md` entry** — that file documents deliberate behaviour changes, and neither source fix has
  any. **No `ENGINE.md` entry** — the porting rules are unchanged.

One commit for the implementation, in the repo's style (lowercase, comma-separated feature list), for
example:

```
macOS (Apple Silicon): Makefile, stdlib.h for abs, random as td3_random, clang warning, windowed with a free cursor, version 0.2.1
```

## 7. Risks and limits

* **The Windows build cannot be compile-verified from this Mac.** Only the diff can be inspected. Both
  changes are standard-C and compiler-portability issues (`<stdlib.h>` for a libc function, a name that
  collides with libc, a warning flag), so the risk is low but not zero.
* **The macOS binary is not portable to older systems.** Homebrew's `sdl3` bottle is `arm64_golden_gate`
  (macOS 26+), so the game needs a recent macOS on Apple Silicon. Relaxing this means building SDL3 from
  source, which is not proposed.
* **The launcher is absent on macOS**, so its preferences (always on top, key bindings, the graphics
  sliders) have no GUI. `make run` variables and `ARGS` cover every option the launcher could set;
  `--keys` is the only one with no make variable by default and is reachable through `ARGS`.
* **`smoke` terminates the game with `SIGTERM`**, so it exits without saving. It also only proves the boot
  and present path, as described in section 3.
* **`F7` mouse steering is less reliable with a free cursor** (section 4), accepted deliberately.

# macOS (Apple Silicon) Build and Run — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and run Test Drive III Enhanced natively on Apple Silicon with SDL3, windowed, with the mouse cursor never captured, driven by a new macOS `Makefile`.

**Architecture:** The engine (`src/`, ~21k lines of C11 + SDL3) is already free of Windows-only code, so the port is three behaviour-preserving toolchain fixes plus build/run tooling. Compiler policy goes in `CMakeLists.txt` where the existing toolchain policy lives; orchestration goes in a new macOS-only `Makefile` that wraps the existing CMake + Ninja build and never repeats a compile flag.

**Tech Stack:** C11, SDL3 (Homebrew `sdl3` 3.4.16, arm64), CMake 3.24+, Ninja, Apple clang 21, GNU make.

**Spec:** `docs/superpowers/specs/2026-09-30-macos-apple-silicon-design.md`

## Global Constraints

- Target is Apple Silicon (arm64) on macOS. The build must be native arm64, not Rosetta.
- The game runs **windowed**. `Alt+Enter` fullscreen toggle stays (pre-existing, both platforms).
- The mouse cursor is **never captured, hidden or confined**. No `SDL_SetRelativeMouseMode`, no
  `SDL_SetWindowGrab`, no `SDL_HideCursor` anywhere in the tree, and the `Makefile` passes no flag that
  would cause it.
- The three source edits are **behaviour-preserving**. The original's quirks stay: no "fixing" the tautological
  clamp at `src/game/sim_physics.c:213`, no changing the LCG arithmetic in `td3_random`.
- The Windows build must be untouched: the MSVC branch, the MinGW branch, every `WIN32` block, and every file
  under `launcher/`.
- The wxWidgets launcher is **not** ported. `TD3E_LAUNCHER` defaults to `OFF` off Windows and stays opt-in with
  `-DTD3E_LAUNCHER=ON`.
- Original game files live in `Game/` (`TDIII.EXE` or `TD3.EXE`, `PLAYDISK.DAT`, `DATAA`–`DATAC.DAT`,
  `INSTR.DAT`, the `C*` car files, the `SCENE*` files). They are not in git and are never committed.
- Dependency policy: no new dependency. Homebrew's `sdl3` is used; SDL3 is never built from source.
- Version after this work: `0.2.1`, in `CMakeLists.txt` and `launcher/version.h`.
- Commit style (this repo): one lowercase line, no conventional-commit prefix, no trailing period, describing
  the change as a comma-separated feature list. Example:
  `Per-pixel fog, soft headlight beams, launcher menus, graphics settings and key bindings, version 0.2.0`

## File Structure

| File | Action | Responsibility after this change |
|---|---|---|
| `src/enhanced/enhanced.c` | modify (1 line added) | Enhanced renderer; gains the `<stdlib.h>` its `abs()` call needs |
| `src/game/flow.h` | modify (rename) | Declares `td3_random` instead of `random` |
| `src/game/flow.c` | modify (rename) | Defines the original's LCG as `td3_random` |
| `src/platform/view.c` | modify (rename) | `dissolve_poll` calls `td3_random` |
| `src/game/render.c` | modify (rename) | View code calls `td3_random` |
| `src/game/flow_screens.c` | modify (rename, 3 sites) | Screen code calls `td3_random` |
| `src/game/flow_race.c` | modify (rename, 2 sites) | Race code calls `td3_random` |
| `CMakeLists.txt` | modify (2 hunks) | Clang warning policy; `TD3E_LAUNCHER` default per platform; version 0.2.1 |
| `Makefile` | create | The only macOS entry point: install, configure, build, run, check, smoke, clean, help |
| `README.md` | modify (add sections) | macOS how-to-play, build, run, requirements, cursor invariant |
| `CHANGELOG.md` | modify (add 0.2.1) | User-visible record of the macOS support |
| `launcher/version.h` | modify (1 line) | Version string stays in step with `CMakeLists.txt` |

Not touched: `src/game/sim_physics.c`, `src/host.c`, `src/host.h`, `src/main.c`, everything under `launcher/`
except `version.h`, `ENGINE.md`, `ENHANCED.md`, `third_party/`.

---

### Task 1: Build the engine natively on Apple Silicon

The two compile errors, and the one warning that is a deliberate quirk. Each step is a real `clang` invocation
so the plan is verifiable without the `Makefile`, which Task 3 adds.

**Files:**
- Modify: `src/enhanced/enhanced.c:3-7` (include block)
- Modify: `src/game/flow.h:16`
- Modify: `src/game/flow.c:431`
- Modify: `src/platform/view.c:92`
- Modify: `src/game/render.c:129`
- Modify: `src/game/flow_screens.c:263,398,520`
- Modify: `src/game/flow_race.c:131,177`

**Interfaces:**
- Consumes: nothing.
- Produces: a C function `u16 td3_random(void)` declared in `src/game/flow.h` and defined in
  `src/game/flow.c`, replacing the previous `u16 random(void)`. Identical arithmetic, identical return value.
  Every caller in the repository is updated; no other signature changes anywhere.

- [ ] **Step 1: Configure the build to see the failures**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTD3E_LAUNCHER=OFF
```

Expected: succeeds. `find_package(SDL3 CONFIG)` resolves to `/opt/homebrew`.

- [ ] **Step 2: Build and collect every error, not just the first**

```bash
cmake --build build -- -k 0 2>&1 | grep -E '^(FAILED|.*error:)' | sort -u
```

Expected: exactly three lines, one per defect.

```text
FAILED: CMakeFiles/testdrive3-enhanced.dir/src/enhanced/enhanced.c.o
FAILED: CMakeFiles/testdrive3-enhanced.dir/src/main.c.o
src/enhanced/enhanced.c:290:15: error: call to undeclared library function 'abs' ...
src/game/flow.h:16:5: error: conflicting types for 'random'
src/game/sim_physics.c:213:20: warning: non-overlapping comparisons always evaluate to false [-Wtautological-overlap-compare]
```

Note the two FAILED lines name two object files while three defects are listed: `flow.h` is included by
`main.c` and by every file in the list below, so one header error fails several objects.

- [ ] **Step 3: Declare `abs` where it is used**

In `src/enhanced/enhanced.c`, replace the include block:

```c
#include "enh_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
```

with:

```c
#include "enh_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
```

`abs()` is called at `src/enhanced/enhanced.c:290`. The include goes in alphabetical order, matching the
file's existing style.

- [ ] **Step 4: Compile the file to confirm that error is gone**

```bash
clang -c -std=gnu11 -O2 -Isrc -Ithird_party/nuked-opl3 -I/opt/homebrew/include \
  -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -o /tmp/enhanced.o src/enhanced/enhanced.c
```

Expected: no output, exit status 0.

- [ ] **Step 5: Prove the `random` collision is a real conflict, not a stale guess**

```bash
printf '#include <stdlib.h>\nint main(void){return (int)random();}\n' > /tmp/rand_probe.c
clang -std=gnu11 -o /tmp/rand_probe /tmp/rand_probe.c
```

Expected: compiles and links, so `<stdlib.h>` really does provide `long random(void)`. Then confirm the
project's own declaration is what conflicts:

```bash
rg -n 'random' src/game/flow.h src/game/flow.c
```

Expected: `flow.h:16` declares `u16 random(void);` and `flow.c:431` defines `u16 random(void)` — the
original's own LCG at `0000:0f58`, which must not shadow libc.

- [ ] **Step 6: Rename the declaration and definition**

In `src/game/flow.h`, replace:

```c
/* 0000:0f58 random — game_flow.md §4.4 (seed DS:00D2 = seed*41C64E6Dh + 3039h; returns (seed >> 16) & 7FFFh) */
u16 random(void);
```

with:

```c
/* 0000:0f58 random — game_flow.md §4.4 (seed DS:00D2 = seed*41C64E6Dh + 3039h; returns (seed >> 16) & 7FFFh).
 * PORT: named td3_random, because <stdlib.h> declares long random(void). */
u16 td3_random(void);
```

In `src/game/flow.c`, replace:

```c
/* 0000:0f58 random — game_flow.md §4.4 (verified) */
u16 random(void)
```

with:

```c
/* 0000:0f58 random — game_flow.md §4.4 (verified) */
u16 td3_random(void)
```

The body is untouched: `seed * 0x41C64E6Du + 0x3039u`, store low and high halves to `DS:00D2`, return
`(u16)(DSW(DS_rand_hi) & 0x7fff)`. The doc comment keeps the original's name and address so the mapping back
to `TDIII.EXE` stays visible. The prefix follows the repo's existing convention: `crt_` for the DOS/CRT
replacements, `enh_` for the enhanced renderer.

- [ ] **Step 7: Update all seven call sites**

Each is a bare call inside a larger expression; rename only the function name, nothing else on the line.

| File | Line | Before | After |
|---|---|---|---|
| `src/platform/view.c` | 92 | `    (void)random();` | `    (void)td3_random();` |
| `src/game/render.c` | 129 | `        s = random();` | `        s = td3_random();` |
| `src/game/flow_screens.c` | 263 | `        k = random();` | `        k = td3_random();` |
| `src/game/flow_screens.c` | 398 | `        k = random();` | `        k = td3_random();` |
| `src/game/flow_screens.c` | 520 | `        k = random();` | `        k = td3_random();` |
| `src/game/flow_race.c` | 131 | `        random();` | `        td3_random();` |
| `src/game/flow_race.c` | 177 | `        random();` | `        td3_random();` |

- [ ] **Step 8: Prove the rename is complete and exact**

```bash
rg -n '\brandom\b' src/
```

Expected: no C identifier named `random` remains anywhere. The word may still appear inside comments that
quote the original (`0000:0f58 random`, `random(), then key_poll(...)`) and inside string literals — those are
documentation of the original and must stay. What must be gone is every call or declaration.

Then confirm the LCG constants are byte-identical to before the rename:

```bash
rg -n '41C64E6D|0x3039|rand_hi|rand_lo' src/game/flow.c
```

Expected: `0x41C64E6Du`, `0x3039u`, `DSW(DS_rand_hi) << 16 | DSW(DS_rand_lo)`, the two stores and the
`& 0x7fff` mask, all unchanged.

- [ ] **Step 9: Build the whole engine**

```bash
cmake --build build
```

Expected: every object compiles, the link succeeds, and the only remaining diagnostic is the
`sim_physics.c:213` warning, which is silenced in Task 2 and must **not** be edited:

```bash
sed -n 210,215p src/game/sim_physics.c
```

Expected: `if (!(t > 0xFF && t < -0xFF)) DSS(DS_pitch) = t;` still present, still preceded by the comment
`/* 11. nose pitch: the original's clamp (t > FFh && t < -FFh) can never hold, so it always stores */`. This
condition is a faithful reproduction of an original quirk; changing it would change the physics.

- [ ] **Step 10: Confirm the binary is native arm64 and links Homebrew's SDL3**

```bash
file build/testdrive3-enhanced
otool -L build/testdrive3-enhanced | grep -i sdl
```

Expected: `Mach-O 64-bit executable arm64`, and SDL3 from `/opt/homebrew/lib`.

- [ ] **Step 11: Commit**

```bash
git add src/enhanced/enhanced.c src/game/flow.h src/game/flow.c src/platform/view.c \
        src/game/render.c src/game/flow_screens.c src/game/flow_race.c
git commit -m "Apple clang: stdlib.h for abs, the ported random() as td3_random"
```

---

### Task 2: Build system — clang policy and the launcher default

Two hunks in `CMakeLists.txt`: silence the one warning that is a deliberate quirk, and make the launcher
default to off off Windows so the documented `cmake` command works on macOS without wxWidgets.

**Files:**
- Modify: `CMakeLists.txt:2` (version), `CMakeLists.txt:26-35` (warning flags), `CMakeLists.txt:45` (option default)

**Interfaces:**
- Consumes: the compiling tree from Task 1.
- Produces: a project whose `cmake -S . -B build` succeeds on macOS with no `-D` flags, a native arm64
  `testdrive3-enhanced` binary, and `TD3E_LAUNCHER` defaulting to `ON` on Windows and `OFF` elsewhere.

- [ ] **Step 1: See the warning that must be silenced rather than fixed**

```bash
touch src/game/sim_physics.c
cmake --build build 2>&1 | grep -A2 'sim_physics.c:213'
```

Expected: `-Wtautological-overlap-compare` on `if (!(t > 0xFF && t < -0xFF))`.

- [ ] **Step 2: Add the clang-only flag**

In `CMakeLists.txt`, replace:

```cmake
    # Game strings are copied out of mem[] into fixed buffers with snprintf; truncation is intended.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(testdrive3-enhanced PRIVATE -Wno-format-truncation)
    endif()
```

with:

```cmake
    # Game strings are copied out of mem[] into fixed buffers with snprintf; truncation is intended.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(testdrive3-enhanced PRIVATE -Wno-format-truncation)
    endif()
    # sim_physics.c:213 keeps the original's clamp (t > FFh && t < -FFh), which can never hold on purpose.
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(testdrive3-enhanced PRIVATE -Wno-tautological-overlap-compare)
    endif()
```

`CMAKE_C_COMPILER_ID` is `AppleClang`, which `MATCHES "Clang"`, so both Apple clang and upstream clang get
the flag while GCC and MSVC do not. The suppression is compiler policy and therefore lives beside the
existing policy, not as a pragma in the source.

- [ ] **Step 3: Rebuild and confirm a completely clean compile**

```bash
cmake --build build 2>&1 | tail -20
```

Expected: no `warning:` and no `error:` lines anywhere in the output.

- [ ] **Step 4: See the launcher default fail on macOS**

```bash
cmake -S . -B /tmp/td3e-default -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -8
```

Expected: fails inside `launcher/CMakeLists.txt` at `find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)`
with "Could not find a package configuration file provided by wxWidgets", because `TD3E_LAUNCHER` is `ON` and
wxWidgets is not installed. This is the papercut the next step removes.

- [ ] **Step 5: Default the option per platform**

In `CMakeLists.txt`, replace:

```cmake
option(TD3E_LAUNCHER "Also build the launcher, Test Drive III Enhanced.exe (wxWidgets)" ON)
```

with:

```cmake
# The launcher is a Windows convenience: on other platforms it needs wxWidgets, which the documented
# cmake command must not require. Opt in with -DTD3E_LAUNCHER=ON wherever wxWidgets 3.2+ is installed.
if(WIN32)
    set(TD3E_LAUNCHER_DEFAULT ON)
else()
    set(TD3E_LAUNCHER_DEFAULT OFF)
endif()
option(TD3E_LAUNCHER "Also build the launcher, Test Drive III Enhanced (wxWidgets)" ${TD3E_LAUNCHER_DEFAULT})
```

`option()` still works, and `--help` still shows the option, so nothing about the interface is lost.

- [ ] **Step 6: Confirm the plain documented command now works on macOS**

```bash
rm -rf /tmp/td3e-default
cmake -S . -B /tmp/td3e-default -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -4
cmake --build /tmp/td3e-default 2>&1 | tail -3
```

Expected: configures without touching wxWidgets and builds `testdrive3-enhanced`. The launcher target is
simply absent.

- [ ] **Step 7: Confirm the option is still opt-in, and fails loudly when it cannot be satisfied**

```bash
cmake -S . -B /tmp/td3e-on -G Ninja -DCMAKE_BUILD_TYPE=Release -DTD3E_LAUNCHER=ON 2>&1 | tail -4
```

Expected: fails on the missing wxWidgets, which is correct: the user asked for the launcher, the launcher
needs wxWidgets, and the error says so.

- [ ] **Step 8: Confirm the Windows paths in the file are byte-identical apart from the two hunks**

```bash
git diff CMakeLists.txt
```

Expected: two hunks only — the added clang block and the option default. The `MSVC` branch, the `WIN32`
DLL-copy block, the `TD_ENHANCED_SOURCES` `-O2` property and the
`target_link_libraries(... SDL3::SDL3 nuked_opl3)` line are untouched. The version bump is Task 5's, so it
must not appear here.

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt
git commit -m "clang: silence the original's always-false pitch clamp, launcher off by default off Windows"
```

---

### Task 3: The Makefile

The single entry point on macOS. It wraps CMake, repeats no compile flag, and defaults to windowed with the
cursor untouched.

**Files:**
- Create: `Makefile`

**Interfaces:**
- Consumes: the CMake project from Task 2.
- Produces: `make help`, `install`, `configure`, `build`, `run`, `check`, `smoke`, `clean`. The binary is
  `$(BUILD_DIR)/testdrive3-enhanced`; tunables are the `?=` variables listed in step 3.

- [ ] **Step 1: Write the Makefile**

Create `Makefile` with exactly this content:

```make
# Makefile for Test Drive III Enhanced — macOS (Apple Silicon) build and run.
# It wraps the CMake build in CMakeLists.txt; every compile flag lives there. The game is windowed and the
# mouse cursor is never captured, so no target here passes --fullscreen or anything that would grab input.
#   make help                      the targets below
#   make run CAR=CCNSX SCALE=4     play (see the variables at the top)
#   make check                     load Game/TDIII.EXE, print a summary, exit
#   make smoke                     headless: compose frames, save snapshots, exit
SERVICE = Test Drive III Enhanced

# Build
BUILD_DIR  = build
BUILD_TYPE ?= Release
GENERATOR  ?= Ninja
ARCH       ?= arm64
GAME       = $(BUILD_DIR)/testdrive3-enhanced

# Run
GAME_DIR ?= Game
SCALE    ?= 3

# Headless smoke test
SMOKE_DIR     ?= $(BUILD_DIR)/smoke
SMOKE_SECONDS ?= 6

# Passed to the game only when set: CAR COURSE SKILL SOUND FRAME_TICKS RES AA KEYS, and ARGS for the rest.
#   make run CAR=CCNSX COURSE=SCENE02 SKILL=3        make run ARGS="--classic --haze 40"

.PHONY: help install configure build run check smoke clean

# ── Environment ───────────────────────────────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE} — macOS (Apple Silicon)\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [var=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

install: ## Install cmake, ninja and sdl3 with Homebrew (only what is missing), then configure
	@missing=""; \
	for tool in cmake ninja; do \
		command -v $$tool >/dev/null || missing="$$missing $$tool"; \
	done; \
	brew list sdl3 >/dev/null 2>&1 || missing="$$missing sdl3"; \
	if [ -n "$$missing" ]; then \
		echo "Installing with Homebrew:$$missing"; \
		brew install $$missing; \
	else \
		echo "cmake, ninja and sdl3 are already installed."; \
	fi
	@$(MAKE) --no-print-directory configure
	@echo "Environment setup complete."

configure: ## Configure the CMake build (Release, Ninja, arm64, no launcher)
	@cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_OSX_ARCHITECTURES=$(ARCH) \
		-DTD3E_LAUNCHER=OFF
	@echo "Configuration complete."

# ── Build ──────────────────────────────────────────────────────────────────────────────────────────────

build: configure ## Build testdrive3-enhanced
	@cmake --build $(BUILD_DIR)

# ── Run ─────────────────────────────────────────────────────────────────────────────────────────────────

run: build ## Play the game, windowed (usage: make run [CAR=CODE] [COURSE=CODE] [SKILL=N] [SCALE=N] [ARGS="..."])
	@$(GAME) --game-dir "$(GAME_DIR)" --scale "$(SCALE)" \
		$(if $(CAR),--car "$(CAR)") \
		$(if $(COURSE),--course "$(COURSE)") \
		$(if $(SKILL),--skill "$(SKILL)") \
		$(if $(SOUND),--sound "$(SOUND)") \
		$(if $(FRAME_TICKS),--frame-ticks "$(FRAME_TICKS)") \
		$(if $(RES),--res-scale "$(RES)") \
		$(if $(AA),--aa "$(AA)") \
		$(if $(KEYS),--keys "$(KEYS)") \
		$(ARGS)

check: build ## Check that Game/TDIII.EXE loads, then exit
	@$(GAME) --game-dir "$(GAME_DIR)" --check

# ── Smoke test ────────────────────────────────────────────────────────────────────────────────────────

# Proves the engine boots, reads the game files and composes frames without a human watching: the dummy video
# driver needs no window, and TD3_SNAPSHOT_DIR (src/host.h) saves a BMP of every frame presented at least two
# seconds after the previous one. It shows the boot screen, not a race; `make run` is the proof for that.
smoke: build ## Run headless for a few seconds and save snapshots, then check that frames were composed
	@rm -rf "$(SMOKE_DIR)"; mkdir -p "$(SMOKE_DIR)"
	@SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy \
		TD3_SNAPSHOT_DIR="$(SMOKE_DIR)" $(GAME) --game-dir "$(GAME_DIR)" & \
	pid=$$!; sleep $(SMOKE_SECONDS); kill $$pid 2>/dev/null; wait $$pid 2>/dev/null; \
	n=$$(ls -1 "$(SMOKE_DIR)" | wc -l | tr -d ' '); \
	if [ "$$n" -lt 1 ]; then \
		echo "smoke: no snapshots in $(SMOKE_DIR) — nothing was composed"; exit 1; \
	fi; \
	echo "smoke: $$n snapshot(s) in $(SMOKE_DIR)"; ls -l "$(SMOKE_DIR)"

# ── Development ───────────────────────────────────────────────────────────────────────────────────────

clean: ## Remove the build directory
	rm -rf $(BUILD_DIR)
	@echo "Cleanup complete."
```

Two details that are deliberate and should not be "tidied up":

* `$(if $(VAR),--flag "$(VAR)")` follows the reference Makefile's `$(if $(force),--force)` idiom, so an unset
  variable contributes nothing and the game keeps its own default. Always passing `--res-scale` would break
  `ARGS="--classic"`, because `--classic` implies a resolution scale of 1 in `src/main.c`.
* `smoke` is one shell line using `$$!`, not `%1`: a recipe line runs in its own shell and job control is not
  available there.

- [ ] **Step 2: Check the Makefile parses and the help target works**

```bash
make help
```

Expected: the coloured header, the usage line, and these eight targets, each with its `##` text:
`help`, `install`, `configure`, `build`, `run`, `check`, `smoke`, `clean`.

- [ ] **Step 3: Confirm the variables are all overridable**

```bash
make -n configure BUILD_DIR=/tmp/x ARCH=arm64 BUILD_TYPE=Debug
```

Expected: a `cmake -S . -B /tmp/x -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_ARCHITECTURES=arm64
-DTD3E_LAUNCHER=OFF` line. `BUILD_DIR =` has no `?=`, so a plain `BUILD_DIR=…` on the command line still
overrides it — make assigns command-line variables over any file assignment — which is what a caller expects.

- [ ] **Step 4: Confirm `run` never passes `--fullscreen` and forwards only what is set**

Make expands `$(if $(VAR),...)` itself, so an unset variable contributes nothing at all to the command line.

```bash
make -n run 2>/dev/null | grep -- '--game-dir'
make -n run CAR=CCNSX SKILL=3 2>/dev/null | grep -- '--game-dir'
make -n run CAR=CCNSX RES=5 2>/dev/null | grep -c -- '--fullscreen'
```

Expected, first command — only the two always-passed options, because nothing else is set:

```text
--game-dir "Game" --scale "3"
```

Expected, second command — `--car "CCNSX"` and `--skill "3"` appear as well, and still no other option:

```text
--game-dir "Game" --scale "3" --car "CCNSX" --skill "3"
```

Expected, third command: `0`. There is no `--fullscreen` text anywhere in the Makefile, so `make run` cannot
reach the fullscreen path. The grep is anchored on `--game-dir` because `make -n` also prints the
`cmake --build` line, whose `--build` would otherwise pollute an unanchored search.

- [ ] **Step 5: Build for real**

```bash
make clean
make build
```

Expected: configures, compiles every file, links, no warnings, no errors.

- [ ] **Step 6: Commit**

```bash
git add Makefile
git commit -m "Makefile for macOS (Apple Silicon): install, configure, build, run, check, smoke, clean"
```

---

### Task 4: Verify the game actually runs

Everything so far has only compiled code. This task proves the ported engine loads the original game files and
presents frames on Apple Silicon. Requires the original files in `Game/`.

**Files:**
- No source changes expected. If any step fails, the fix belongs in the task that owns the failing code.

**Interfaces:**
- Consumes: `make build`, `make run`, `make check`, `make smoke` from Task 3, and the original game files in
  `Game/`.
- Produces: evidence that the port runs, recorded in the commit body of Task 5.

- [ ] **Step 1: Check the original executable loads through the real-mode memory model**

```bash
make check
```

Expected, a line of this shape:

```text
Game/TDIII.EXE ok: image <N> bytes at 1000:0000, DGROUP 2BE4, frame pacing 14 ticks
```

`<N>` is the size of the load image and depends on the game's executable, so read it off the output. The
other three values are fixed by the code and must match the Windows build exactly: `1000:0000` is `LOAD_SEG`
(`src/mem.h`), `2BE4` is `SEG(0x1BE4)`, and 14 is `HOST_DEFAULT_FRAME_TICKS` (`src/host.h:47`). A match means
the MZ load, the relocation fixups and the segment model all behave identically here.

- [ ] **Step 2: Run headless and confirm frames were composed**

```bash
make smoke
```

Expected: `smoke: 2 snapshot(s) in build/smoke` or more, with `snap0000.bmp` and `snap0001.bmp` listed. The
count is 2 because `snapshot()` in `src/host.c:159-176` writes at most one frame per two seconds and the run
lasts six.

- [ ] **Step 3: Confirm the snapshots are real content, not black frames**

```bash
sips -g pixelWidth -g pixelHeight build/smoke/snap0000.bmp
python3 - <<'EOF'
import struct
for n in ("build/smoke/snap0000.bmp", "build/smoke/snap0001.bmp"):
    d = open(n, "rb").read()
    px = d[54:]
    print(n, len(d), "bytes", "distinct byte values:", len(set(px)))
EOF
```

Expected: 1280×800 (the default `--res-scale 4` times 320×200, per `ENH_DEFAULT_RES_SCALE` in
`src/enhanced/enhanced.h:12`) and many distinct byte values. A single distinct value would mean a uniform
frame — a red flag, not a pass.

- [ ] **Step 4: Play it in a real window**

```bash
make run
```

Expected: a windowed `320×240×3` = 960×720 window, and a game that reaches its menus. Check by hand, with a
human looking:

- [ ] **Step 4a: the cursor is free — move it out of the window while the game runs. It must not be
      captured, hidden or confined, and the game must keep running.**

- [ ] **Step 4b: click another application and back. No key may be stuck down when focus returns (this is
      `SDL_EVENT_WINDOW_FOCUS_LOST` calling the release handler, `src/host.c:487-489`).**

- [ ] **Step 4c: start a race with the arrow keys. The view renders, the car drives, sound plays.**

- [ ] **Step 4d: press `Alt+Enter` and back. Fullscreen toggles and returns to the windowed size.**

- [ ] **Step 4e: press `F7`, then move the mouse. Steering follows the pointer. Note for the README that this
      is only reliable with the pointer near the middle of the window, since the cursor is not captured.**

- [ ] **Step 5: Confirm high scores are written back into the game folder**

```bash
ls -l Game/
```

Expected: at least one file's modification time is from this session. The folder must be writable for scores
and choices; the README must say so.

- [ ] **Step 6: Do not commit Task 4 on its own** — it produces no file changes. Its output is pasted into the
  Task 5 commit body as the verification record.

---

### Task 5: Document the macOS build, and release it as 0.2.1

**Files:**
- Modify: `README.md:17-60` (How to play, Requirements, Build, Run)
- Modify: `CHANGELOG.md:1-3`
- Modify: `launcher/version.h`

**Interfaces:**
- Consumes: the working `make` targets from Task 3 and the evidence from Task 4.
- Produces: the user-facing record of the port, and version `0.2.1` consistent across
  `CMakeLists.txt` and `launcher/version.h`.

- [ ] **Step 1: Read the version header and the README anchors you will edit**

```bash
cat launcher/version.h
rg -n '^## ' README.md
```

Expected: a `0.2.0` in the header; README headings `Features`, `How to play (Windows)`, `Requirements`,
`Build`, `Run`, `Controls`, `Layout`, `License`, `Support`.

- [ ] **Step 2: Add the macOS section to the README**

In `README.md`, insert immediately after the `## How to play (Windows)` section (that is, directly before
`## Requirements`):

````markdown
## How to play (macOS)

1. Put your original game files in a folder named `Game`.
2. Install the build tools and SDL3, then build:

   ```bash
   make install
   ```

3. Run it:

   ```bash
   make run
   ```

* The folder must be writable (high scores and choices are saved in `Game`).
* The game is windowed, and the mouse cursor is never captured, hidden or locked to the window: move it away
  or click another application at any time, and keys you were holding are released when you come back.
  Because the cursor is free, `F7` mouse steering only works reliably with the pointer near the middle of the
  window — the arrow keys are unaffected and are the default.
* Without the Windows launcher, the options are make variables, e.g.
  `make run CAR=CCNSX COURSE=SCENE02 SKILL=3 SCALE=4`, or `make run ARGS="--classic --haze 40"`. `make help`
  lists everything.
````

- [ ] **Step 3: Point Requirements, Build and Run at the Makefile**

In `README.md`, append to the end of the `## Requirements` list:

```markdown
* On macOS: the command line tools (`xcode-select --install`), CMake 3.24+, Ninja and SDL3, which `make
  install` fetches with Homebrew. Apple Silicon only.
```

Then, after the existing Windows `## Build` block, add:

````markdown
### Build (macOS, Apple Silicon)

```bash
make install        # Homebrew: cmake, ninja, sdl3 — only what is missing — then configure
make build          # cmake --build build, Release, arm64
```

`-DTD3E_LAUNCHER=OFF` is implied; the wxWidgets launcher is a Windows program and is not built here. The
binary is `build/testdrive3-enhanced`. SDL3 comes from Homebrew, whose `sdl3` bottle targets a recent macOS
on Apple Silicon; the game therefore needs a recent macOS.
````

And after the existing Windows `## Run` block (i.e. just before `## Controls`), add:

````markdown
### Run (macOS)

```bash
make run                                   # windowed, Game/TDIII.EXE
make run CAR=CCNSX COURSE=SCENE02 SKILL=3  # start car, course and skill
make check                                 # load Game/TDIII.EXE, print a summary, exit
make smoke                                 # headless: compose frames, save snapshots to build/smoke
```

Every option of the table above is available as a make variable — `SCALE`, `RES`, `AA`, `CAR`, `COURSE`,
`SKILL`, `SOUND`, `FRAME_TICKS`, `KEYS` — and anything else goes through `ARGS`, which is passed straight to
the game. Variables that are not set are not passed, so the game's own defaults stand.
````

- [ ] **Step 4: Add the changelog entry**

In `CHANGELOG.md`, insert after the `# Changelog` line and before `## 0.2.0`:

```markdown
## 0.2.1

- macOS (Apple Silicon): builds and runs through the new `Makefile` (`make install`, `make build`,
  `make run`); the game is windowed and the mouse cursor is never captured, hidden or locked to the window.
- The options the Windows launcher offers are make variables on macOS (`make run CAR=CCNSX SCALE=4`).
```

- [ ] **Step 5: Bump the version in both places**

In `CMakeLists.txt` line 2:

```cmake
project(testdrive3_enhanced VERSION 0.2.1 LANGUAGES C)
```

In `launcher/version.h`, change the `0.2.0` literal to `0.2.1`, leaving the surrounding macro untouched.

- [ ] **Step 6: Confirm the version is consistent everywhere it appears**

```bash
rg -n '0\.2\.[01]' CMakeLists.txt launcher/version.h CHANGELOG.md README.md
```

Expected: `0.2.1` in `CMakeLists.txt`, `launcher/version.h` and the `CHANGELOG.md` heading; `0.2.0` appears
only as the older changelog entry.

- [ ] **Step 7: Rebuild and re-run the checks so the docs describe a verified state**

```bash
make clean && make build && make check
```

Expected: clean build, no warnings, and the `TDIII.EXE ok: ...` line from Task 4 step 1.

- [ ] **Step 8: Review the whole diff for the Windows build's sake**

```bash
BASE=$(git merge-base master HEAD)
git diff "$BASE"..HEAD --stat
git diff "$BASE"..HEAD -- src/game/sim_physics.c src/host.c launcher/
```

Use the merge base rather than `HEAD~N`, so the check holds however many commits the fix loop added. Expected:
`src/game/sim_physics.c`, `src/host.c` and everything under `launcher/` except `version.h` show **no** changes.
The changed file list is exactly the twelve in the File Structure table.

- [ ] **Step 9: Commit**

```bash
git add README.md CHANGELOG.md CMakeLists.txt launcher/version.h
git commit -m "macOS (Apple Silicon) in the README and changelog, version 0.2.1" \
  -F - <<'EOF'
Verified on macOS 27.0 arm64 with Apple clang 21, Homebrew sdl3 3.4.16:

  make build   clean build, native arm64 Mach-O, no warnings
  make check   Game/TDIII.EXE ok: image <N> bytes at 1000:0000, DGROUP 2BE4, frame pacing 14 ticks
  make smoke   2 snapshots in build/smoke, 1280x800, non-uniform
  make run     windowed, cursor free, race renders and plays

Fill in <N> from the actual `make check` output.
EOF
```

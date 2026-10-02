# Test Drive III Enhanced — agent notes

## Build / verify (macOS, Apple Silicon)

```bash
make install   # once: Homebrew cmake/ninja/sdl3 (whatever is missing), then configure
make check     # build + verify Game/TDIII.EXE loads (fast gate)
make smoke     # headless: compose frames, assert snapshots land in build/smoke
```

`make run` plays windowed. Original game files live in `Game/` (not in git, never commit).

## Invariants

- Faithful port (`ENGINE.md`): source edits are behaviour-preserving. Never "fix" original quirks (e.g. the nose-pitch clamp in `src/game/sim_physics.c`); silence toolchain warnings in `CMakeLists.txt` instead.
- Windowed, free cursor: no `--fullscreen` flags, no pointer capture/hide/confine anywhere, including the `Makefile`.
- Windows build and `launcher/` stay untouched, except `launcher/version.h` on version bumps.
- Compile flags live in `CMakeLists.txt` only; the `Makefile` wraps the build and never repeats them.
- Commit style: one lowercase line, comma-separated feature list, no prefix, no trailing period.

## Docs

- `README.md`, `CHANGELOG.md`: user-facing; keep macOS/Windows sections in step.
- `docs/superpowers/specs/`, `docs/superpowers/plans/`: design + implementation plans.

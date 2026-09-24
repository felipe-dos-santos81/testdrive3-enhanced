# Changelog

## 0.1.0 (unreleased)

- Project from the faithful Test Drive III port (`../TestDrive3`, `td3port/` at 2c5a58d): the game, its
  launcher (renamed "Test Drive III Enhanced", settings in `%APPDATA%\Test Drive III Enhanced`) and the build.
- Enhanced renderer (`src/enhanced/`, `ENHANCED.md`), stage 1: smooth motion and smooth graphics, the original
  draw distance.
  - The 3D view and the rear-view mirror drawn again for every displayed frame at the display's rate, from what
    each game frame built: the camera, the moving cars and the drifting sprites carried smoothly between the
    game's frames (about 6 a second), with a C1-continuous blend so nothing jerks when a new frame arrives.
    `--motion-delay` (default 50 % of a game frame) sets how far the view runs behind the game.
  - High resolution (`--res-scale`, default 4 = 1280 x 800, up to 8) with anti-aliasing (`--aa`, default
    2 x 2): the original's angular projection, face order, face types, lamps, lines and sprites in floating
    point; sprites from their native images at a size that follows the distance continuously; the two-colour
    dithers shown as their mixed colour; a continuous sky gradient that follows the horizon.
  - The cockpit overlays (rain, snow, windscreen drops, headlight beams, dashboard edge, wipers, the mirror's
    rim) replayed over the enhanced view in the original order; the dashboard, HUD, menus, messages, the replay
    panel and the crash pictures stay the original's.
  - The main menu's turning car is enhanced too. `--classic` shows the original picture only.
- Fixes after the first play-test:
  - Shimmer: between the game drawing a frame and presenting it, the new frame's view buffer was compared with
    the old screen, so parts of the view fell back to the blocky original for a displayed frame, 6 times a second.
    Faces are also drawn in the game's own depth order now, so coplanar faces no longer swap during the motion.
  - Controls: the first of the three control reads per frame runs before the simulation (`ENH:`), so the car
    answers the keys a frame (158 ms) sooner. Verified with real Windows key events (held, auto-repeating):
    throttle, brake and steering register every frame.
  - Wheels of the other cars (lamp blobs) grew relative to the car as it drove away: the blob width is now the
    original's row sum and its radius the original's whole-pixel radius on average.
  - Steering felt jerky on small corrections: the game turns in lumps, and guessing half a frame ahead overshot
    each one. `--motion-delay` now defaults to 100 (interpolation only; launcher "Smooth"), and the horizon's
    bobbing is softened with a 60 ms low-pass.
  - Cars felt slow with the smooth picture. Measured: the game moves a fixed distance per frame, and at the
    faithful port's 23 ticks a frame the scenery passes at about a third of the speedometer's speed (the jerky
    original hid it). Default game speed now 14 ticks (chosen by play-testing; 10 matches the speedometer) (`--frame-ticks`, launcher "Game speed", stored under a new
    key), and the race clock counts real seconds instead of frames (`ENH:` in `race_clock_hud`), so race times
    stay real at any game speed.
- Launcher: a "Picture" box with graphics (enhanced / original), resolution, anti-aliasing and motion.
- Developer aids: `TD3_ENH_COMPARE=1|2` (enhanced and original side by side), `TD3_ENH_LOG=file` (per-frame
  camera and render time).

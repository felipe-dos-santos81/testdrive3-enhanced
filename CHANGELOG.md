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
  - Double images of trees and roadside sprites: when the car enters a new cell the game hands the tiles' sprite
    slots out again in a new order, and the smooth view, matching sprites by slot, carried a tree from where
    another one had stood for a game frame. Only sprites that move (the fixed instances, drifting clouds and
    birds, crash debris) are carried now, and the tiles' slots never across a rebuild of the world.
  - After returning from the chase view, the player's car kept the vertex range of its last chase-view frame
    (the game does not emit it in the cockpit view); another car's vertices there were carried with the
    player's motion. The player's car is no longer treated as a moving vehicle in the cockpit view.
  - Keyboard steering eased in (`ENH:` in `steer_throttle`): the wheel went from the centre to full lock in two
    frames at any speed. Away from the centre it now starts with small steps that grow with the time held, in
    real time and slower at speed (full lock after about 0.4 s standing, 0.6 s at 150 mph); a counter-turn is
    eased from a head start, stops at the centre as before and carries on past it without easing in again;
    both keys and the mouse are not affected. The original's doubled turn at full lock grows over the
    outer band of the wheel instead, to 1.5 times (`ENH:` in `sim_physics.c`).
- Finish marker on the compass (`--finish-marker`, default on; launcher option): a green mark in the compass
  window points to the leg's gas station, an arrow at its edge when the finish lies further to the side.
- Police: a police car driving ahead in the same direction started a chase as soon as the player came near at
  48 or more, while still behind it (the original checks only the distance). It now gives chase only once
  overtaken; oncoming and parked police cars as before (`ENH:` in `police_update`, `sim_traffic.c`).
- Distance haze (`--haze`, default 30 %; launcher "Haze"), after the distance colouring of Play Stunts' upgraded
  renderer (github.com/ACatWithEbola/playstunts): faces, trees and the ground take on some of the horizon's sky
  colour with distance, through the current palette (day, night, weather and fades follow), which also softens
  the scenery appearing at the edge of the view. Not the sun, moon or clouds, the headlight beams or, at night,
  the lamps.
- No far LOD with the enhanced view: vehicles and the buildings that have a simpler far model (barn, hangar,
  houses) are always built with their full model (`ENH:` in `render_object.c`; `--classic` unchanged).
- Seeing under the map for a moment when landing or driving into a steep slope: the smooth camera's blend still
  followed the previous frame's motion (still falling, or still level) and took the eye below the ground. Its
  height now stays at or above the line between the game's last two frames and is not guessed downwards.
- Launcher: a "Picture" box with graphics (enhanced / original), resolution, anti-aliasing, motion and haze.
- Developer aids: `TD3_ENH_COMPARE=1|2` (enhanced and original side by side), `TD3_ENH_LOG=file` (per-frame
  camera and render time).

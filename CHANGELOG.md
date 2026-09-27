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
  - Keyboard steering paced by the frame's length (`ENH:` in `steer_throttle`): the original's steering, whose
    steps are per control read, swung the wheel 23/14 times as fast at the faster default game speed. Its steps
    are now scaled by the frame's ticks / 23, the faithful port's pace in seconds at any game speed (full lock
    in about 0.3 s); otherwise the original's. (An eased steering with a softened full lock tried before this
    is dropped.)
- Flickering road markings and trees: the markings are small sprites lying on the road, and they and trees at
  the foot of slopes were drawn over by the face they are on, on and off as the camera moved (sprites were
  placed among the faces by depth keys that are not in order along the game's face order at the smooth
  camera). The sprites are now drawn after all faces with a per-sample depth test: every face leaves its exact
  distance along each sample's line of sight, and a sprite shows only where it is nearer (with 64 units of
  allowance for what stands or lies on a surface), so trees are hidden exactly where cars, slopes or scenery
  in front of them cover them, also in part.
- The sun is a smooth round disc and the moon a smooth crescent instead of blocky scaled-up images.
- Finish marker on the compass (`--finish-marker`, default on; launcher option): a green mark in the compass
  window points to the leg's gas station, an arrow at its edge when the finish lies further to the side.
- Police: a police car driving ahead in the same direction started a chase as soon as the player came near at
  48 or more, while still behind it (the original checks only the distance). It now gives chase only once
  overtaken; oncoming and parked police cars as before (`ENH:` in `police_update`, `sim_traffic.c`). The same
  for the opponents: a chasing police car gives them a ticket only once they have overtaken it.
- Steering in bursts: the car moved along its course rounded to 1/64 of a turn (curves driven as straight runs
  with 5.6-degree kinks), the turn doubled suddenly on the last wheel step, the wheel self-centring (C) moved
  6 then 0 on alternate frames and pulled the wheel back during the first read of a key press, a frame that ran
  late turned less per tick, and the smooth view turned by the camera heading rounded to 64 units. The course
  is now used whole, the doubling is reached over the last 4 wheel steps, the centring is even and frame-paced
  and stops while a steering key is held, the turn is in proportion to the frame's length (unchanged at the
  configured pacing), and the smooth view takes the unrounded heading (`ENH:` in `sim_physics.c`,
  `sim_controls.c`, `enhanced.c`).
- Seeing under a steep slope when driving onto it at speed: the game's own eye sinks to the surface and a frame
  or two below it there. The enhanced view keeps the eye at least 20 units over the ground under it.
- Draw distance (`--draw-distance`, default 7 map cells around the camera; launcher "Draw distance"): the
  enhanced view builds the cells beyond the game's own 3 to 10 itself, read-only from the same tile and object
  models, with their static objects and trees (thinned exactly as the game does), and the leg's trees and rocks
  as far; the haze fades out at the new edge. The game's own cells are unchanged. Parked objects (houses,
  boats), which the game shows only within a cell and a half, reach as far too, but not the lightning bolts
  (white spikes to the sky on the night legs: the game shows them only in a storm). Fixed after the first try:
  green flashes over the sky and nearby cars and scenery vanishing for a moment (a vertex beyond the 16-bit
  reach of the camera wrapped around, and a far cell the view had not yet left was filled around the camera).
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

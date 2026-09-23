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
- Launcher: a "Picture" box with graphics (enhanced / original), resolution, anti-aliasing and motion.
- Developer aids: `TD3_ENH_COMPARE=1|2` (enhanced and original side by side), `TD3_ENH_LOG=file` (per-frame
  camera and render time).

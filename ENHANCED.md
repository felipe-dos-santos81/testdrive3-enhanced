# Enhanced renderer — design

The faithful engine (see `ENGINE.md`) runs unchanged: simulation, game flow, the original 3D renderer,
cockpit and HUD, all at the game's own pace (about 6.3 frames a second in a race, `--frame-ticks 23`).
`src/enhanced/` records what every game frame built and draws the 3D view and the rear-view mirror again on
the host, for every displayed frame, and lays the result over the VGA screen.

Stage 1 (this version): **smooth motion at the display's rate** and **smooth, high-resolution graphics** from
the original data only. The draw distance is the original's (the same 3, 6 or 10 map cells, the same sprite
and object ranges): what the game builds is what is drawn.

Files: `enhanced.c` (hooks, snapshots, the overlay record, the smooth view state, composition),
`enh_scene.c` (projection, faces, sprites, mirror, sky and ground as draw lists), `enh_raster.c` (draw lists
into sample buffers), `enh_sprite.c` (native sprite images and sizes), `enh_internal.h`.

## Hooks

`run_stage`-style loops call the original frame (`frame_update`, `frame_draw`, HUD, `view_present`) as
before. Hooks, marked `ENH:` in the engine:

| Hook | Where | Purpose |
|---|---|---|
| `enh_init()` | `main.c` | options (not active with `--classic`) |
| `enh_sprites_loaded()` | `stage_load_objects` after `sprites_prescale` | native sprite images from DS:2500 (overwritten by the vertex arrays during the race) and the sizes of the original's pre-scaled copies |
| `enh_ov_begin()` / `enh_ov_pixels()` / `enh_ov_skip()` / `enh_ov_quad()` / `enh_ov_line()` | `frame_draw`, `cockpit_overlays`, `headlight_beam_draw`, `overlay_quads4_draw`, `wipers_update_draw` | the cockpit overlays in drawing order (see Overlays) |
| `enh_frame_drawn()` | end of `frame_draw` | the frame becomes the newest snapshot |
| `enh_view_presented()` | end of `view_present` | where V and the mirror image M are on the screen, and their contents |
| `enh_stop()` | `race_run` leaving and before the water-crash roll (which scrolls the screen), `main_menu` leaving the preview | stop laying the view over the screen (until the next frame is presented) |
| `enh_compose()` | `platform/vga.c` compose | draw and lay the enhanced view over the scaled VGA picture |

The main menu's rotating car uses the same frame, so it is enhanced too.

## Snapshots

`enh_frame_drawn` copies, from `mem[]`: the camera (`cam_x4`, `cam_z4`, `cam_heading`, `cam_y`, `cam_row`,
`pitch`, the roll code), the view window (`BA95`, `BA99`, `BA91`, mirror base `BD3D`, mirror on), the sky and
ground pairs and whether the gradient sky is on, all vertices (`vert_x/y/z`, world coordinates) and face records
(the far face buffer in list order), the objects (position, heading, flags, vertex ranges; moving vehicles are
those whose range lies after `parked_vert_end`), the sprite list (`spr_inst` entries with their instances'
positions and ids), the overlay record and V itself. The time of a snapshot is the due time of the timer tick
the frame started on (`host_tick_ns`), so snapshots are exact multiples of a tick apart.

Two consecutive snapshots form a continuous step unless the kind of view, the window or the layout changed, or
the camera jumped (more than 1.5 cells, 45° or 200h in height): restarts, replay start, the crash reset. A step
that is not continuous is shown without interpolation.

## Smooth motion

The game moves everything once per frame (every 23 ticks = 158 ms by default). The displayed frame at time
`t` uses `x = (t − t_n) / P`, the time since the newest snapshot `n` in game frames (`P` = the time between
snapshots `n−1` and `n`, or the configured pacing after a long gap), limited to 1.5 (a frame that is a tick
or two late keeps moving; a game that stops — message boxes, pause, the crash sequence — holds).

* **Camera.** `traj_n(t) = S_n + (x − L) · (S_n − S_{n−1})` with `L = --motion-delay / 100` (default 1):
  the view runs `L` of a frame behind the game and extrapolates the rest. The default interpolates only: the
  game turns in lumps (its heading changes by 0, 64, 128 or 192 units a frame, a short steering tap is a single
  lump), and any extrapolation overshoots each lump and pulls back, a wobble on every small correction (measured
  over twelve steering taps: 23 heading reversals at `L` = 0.5 against the game's own 12 at `L` = 1). Each component (x, z, heading with
  16-bit wrap, height, row = pitch + horizon, car pitch for the mirror, the roll slopes) is blended from the
  previous trajectory `traj_{n−1}(t)` to `traj_n(t)` with `smoothstep(x)` over the frame, so neither position
  nor speed jumps when a new game frame arrives (C1-continuous). Measured over 8 s of driving and steering: the
  largest frame-to-frame change of speed went from 450 to 38 units/s, of heading rate from 21600 to 1900 (plain
  extrapolation → blended).
  The height is kept at or above the line through the last two snapshots and is never guessed downwards past
  the newest: early in a frame the blend still follows the old trajectory, which keeps falling after a landing
  and keeps level where the road turns up a steep slope, and a late frame extrapolates a fall; either put the
  eye under the ground for a moment (the triangle around the camera then filled upwards, the map seen from
  below). Moving vehicles are likewise not guessed downwards.
* **Moving vehicles** (traffic, police, opponents, the player's car in the external views) are carried with
  `traj_n` too, and turned to their exact interpolated heading: the original builds a model at the high byte
  of its heading (1.4° steps); here the difference to the full 16-bit heading is added around the object's
  position.
* **Sprites** that move (drifting clouds, birds, crash debris, the sun and moon, which follow the camera)
  likewise, matched by instance. Only those: the other sprites never move, and the tiles' children (the
  instances from `sprite_count` on: trees and roadside sprites) are handed out again in a new order whenever the
  world is rebuilt (`last_cell` / `last_octab` changed), so a slot can hold another tree of the same kind a cell
  away, which would be carried from there.
* **Pitch.** The horizon row and the mirror's car pitch also go through a 60 ms low-pass: the game's pitch bobs
  by up to 20 rows between frames over bumps and while steering.
* **Roll** is the original's shift code (`row offset = ±(x − centre) >> |code|`); the slope `2^−|code|` is
  interpolated.
* Everything else (the static world, lamps, colours, blinking pairs, weather) is shown as the newest snapshot
  has it.

`--motion-delay 100` never extrapolates (interpolation only, one game frame behind); `0` shows the newest frame
at once and guesses ahead.

## Projection

The original's projection is angular (cylindrical), not perspective (render3d.md §4.5): x is linear in the
bearing, y in the elevation, 256 px = 45°, and there is no near plane. `enh_scene.c` evaluates it in double
precision for the smooth camera:

* `sx = atan2(dx, dz) · 65536/2π + (BA99 << 8) − heading` (1/32 px, wrapping at 65536), with
  `dx = vx − cam_x4`, `dz = vz − cam_z4` (16-bit wrap);
* `dist = hypot(dx, dz)`, depth key `|vy − cam_y| + dist`;
* front row `cam_row − atan2(vy − cam_y, dist) · 1024/π + roll_slope · (sx − BA97)`;
* mirror row `(−elevation − mroll · (sx − 9400h) − pitch) / 2 + 10`, mirror x `(1600h − (sx − BD3D)) / 2`.

The original floors its bearings and elevations to whole pixels (the atan tables): at the display rate that
would make everything step; here they stay continuous.

## Faces

The face list of the newest snapshot, drawn farthest first in the game's own sorted order (`361c`, captured
from the order array): re-sorting at every displayed frame made coplanar faces (road markings on the road,
decals) swap as their keys crossed during the smooth motion, a visible shimmer. Depth keys by the original's rule
(nearest vertex, average, `s·11/32` for triangles, farthest-vertex flag) are evaluated in floating point for the
view time (for the haze, and for sprites against points and lines, see Sprites). Per face, as `3a7c`:

* the front-view test (all x negative / all ≥ 2800h / none below 5400h, as 16-bit results) and otherwise the
  mirror test against `BD3D`; a face is drawn in at most one view;
* triangles and quads whose bearings leave a gap of more than half a turn are ordinary polygons (their x
  values unwrapped around the view); otherwise the camera is inside: quads are split, and a triangle around the
  camera fills below each edge (above, when its plane is higher than the camera) across the arc of bearings the
  edge covers (`3ebf`), which is how the ground under the car is drawn;
* face type 0 ORs its colours into what is below (headlight beams), hidden by day when its pair is 0707h; pair
  010Fh blinks with the frame counter;
* lines: one pixel wide, or (width flags) the angular half width `atan(size / dist)` of the near end, plus a
  pixel as the original widens; at least one row high;
* points (lamps, and the wheels of the cars): the original's lens-shaped blob — row `k` of `2r` widened by
  `inc · k · (2r − 1 − k) / 2`, width from how squarely the lamp faces the camera, owner object by vertex range —
  as a polygon. The original truncates the radius `atan(size / dist)` to whole pixels (nothing below 1); the
  continuous radius is taken half a pixel smaller, the original's average, so small blobs (far wheels) keep
  their size relative to their car.

The ground-slope, surface, crash and bump tests of the face drawer stay in the original renderer: the enhanced
one only draws.

`ENH:` no far LOD. Vehicles and some buildings (barn, hangar, houses) have a far model with fewer faces that
`539d` / `52a3` use more than 200h (Manhattan, from the car) away; with the enhanced view on, the game always
builds the near model (`far_lod` in `render_object.c`), since at the enhanced resolution the missing detail
shows and the cost does not matter. `--classic` keeps the original's switch. The detail level's own choices
(cells, trees, sprite range, F2) are unchanged.

## Sprites

Each sprite is drawn from its native image (decoded from the sprite set at stage load, rows centred as
`sprite_scale_row` pads them). Its size follows the apparent size `a = atan(size / key)` (8-pixel units)
continuously: at every whole `a` it is exactly the size of the copy the original would pick (`BA0E` step →
cached copy W × H), linear in between; fixed-size sprites (kind 1) keep their 1:1 copy. Placement as `2b74` /
`2999`: centred on the bearing, bottom on the elevation of its distance (clamped by `95CD`), the roll term;
the mirror's half size (key × 2), its x and its rows. The visibility window (`hi(angle) + 8`), the key limits
(`B6E2`, 980h, 10h) and the classes are the original's.

`ENH:` **where a sprite goes among the faces.** `323e` merges the sprites into the sorted faces by depth key (a
sprite before a face when its key is larger). Here the faces keep the game's order while the keys are the
smooth camera's, so along that order they are not monotonic, and a small move of the camera put a sprite before
the face it lies or stands on: the road markings (sprites 1 and 3, 6 × 2 white-and-yellow dashes lying on the
road) and trees at the foot of slopes were drawn over, on and off. Measured while driving: 14..17 sprites a
displayed frame drawn under a face that could not hide them, one of them switching every 2..3 displayed frames.
Placing each sprite somewhere in the faces' order (after the faces that cannot hide it, before those in
front of it) was tried and is not enough: no single place is right for a sprite that a car or a slope hides
only in part, and the game's order itself is not exact (a large ground face a tree stands on can come after a
hill in front of the tree), so trees still showed over cars and slopes.

Instead the sprites are tested per sample. Every face, drawn in the game's order as before, leaves in the
target's depth buffer the distance from the camera along each sample's line of sight: a polygon its plane
(`Z_PLANE`: the line of sight of the sample, the inverse of the projection, `EnhMap` / `target_rays` in
`enh_raster.c`, met with the plane `n · X = d`; checked: at every face's first corner it gives that corner's
distance, to float precision), a lamp or a line the distance of its nearest end. The sky clears it; the OR
faces (headlight beams) and the cockpit overlays leave it. Then the sprites are drawn farthest first, each
sample only where the sprite's distance (to its base) less `SPR_ON_FACE` (64 units, so that what stands or
lies on a surface, a tree on a slope, a road marking, is not hidden by it) is below the depth there. So a
sprite is hidden exactly where something nearer covers it: partly behind a car, behind the crest of a hill,
and the markings stay on the road. The sun and the moon are drawn first, behind everything. Measured at the
default 1280 × 800, 2 × 2: 3.4 ms a frame (median), about as before.

`ENH:` **the sun and the moon** (instance 8, sprites 5 and 4) are a solid disc (14 × 11, round with the VGA's
tall pixels) and a crescent (8 × 11); scaled up they were blocky blobs. They are drawn as smooth shapes in the
same rectangle and colour (`P_ELLIPSE`): the sun the inscribed ellipse, the moon a circle 11 image pixels across
at the image's left edge less a 10 × 11 ellipse centred 10 pixels right of it (fitted to the image: 4 of its 88
pixels differ).

## Sky and ground

The horizon is the camera row with the roll slope. The sky: the flat colour, and when the original draws its
gradient (detail ≥ 1, VGA, no flash), the five four-row bands above the horizon blended into a continuous
gradient that follows the horizon, also when rolled (the original drops the gradient when the car rolls). The
ground: the ground pair below the horizon. The mirror's sky and ground come from `7b9b`'s horizon (the car's
pitch and roll, its halved rows included).

## Distance haze

`ENH:` after Play Stunts' distance colouring (a browser reconstruction of Stunts, whose engine TD3 shares; its
upgraded renderer tints what is beyond five tiles by up to 25 % towards a pale sky colour). Here what is far
away takes on some of the colour of the sky at the horizon (the gradient's lowest band, or the flat sky pair),
resolved through the current DAC, so the haze follows the time of day, the weather, fades and flashes by
itself. The amount is `--haze` (default 30 %) × `smoothstep` of the depth from `ENH_HAZE_NEAR` (C00h) to
`ENH_HAZE_FAR` (3000h, depth-key units: the farthest faces are about 2800h at medium detail):

* faces by the average depth of their corners (the key's own rule would haze a long face by its far end);
  not the OR faces (headlight beams), nor the lamps at night;
* sprites by their key; not those above the eye (the sun, the moon, clouds, birds);
* the ground by its angle below the horizon, as seen from `ENH_HAZE_EYE` (50 units, the cockpit's eye over
  the road) over flat ground: the haze gathers in the last few rows below the horizon and meets the sky's
  colour there.

The haze is the top byte of the sample (see Rasterising). Measured at medium detail: the faces reach depth
keys of 9000..11000, the trees stop at 1100h (`B6E2`). Not in the main menu's preview, nor on the mirror's
ground.

## Overlays

The cockpit overlays are drawn into V after the 3D (`cockpit_overlays`, `mirror_frame_draw`). They are recorded
in drawing order:

* pixel sections — rain streaks, snow flakes, windscreen drops, the mirror's lower rim: the pixels V changed
  since the last mark, drawn as blocks of the original pixel size;
* polygon sections — headlight beams (OR mode), the dashboard edge and the wipers (four quads and two edge
  lines): recorded with their vertices (`scr_x/scr_y` slots 630h..63Fh) and fake face records, drawn again at
  the output resolution.

They are replayed over the enhanced 3D view in the same order.

## Rasterising

A draw list per target (the front view, the mirror) of polygons (sample coordinates, up to 16 points), sprites
(destination rectangle, native image), overlay blocks and the sky. A target has `K = res_scale × aa` samples
per original pixel along each axis; it is drawn in bands of 16 sample rows on the host's worker threads (every
band walks the whole list: painter's algorithm), with exact coverage at sample centres (no widening: shared
edges tile exactly).

A sample holds a colour pair and the weight of its high colour. The original's faces are two-colour checkerboard
dithers; from afar a dither is its average, so a sample of a pair resolves to the average of the two DAC
colours (weight 128), the sky gradient uses the weight for its blend, sprites and overlay pixels are solid.
The top byte is the distance haze (0..255, the share of the horizon's sky colour). Beside it every sample has a
depth (float, the distance along its line of sight to what was drawn there), used only to test the sprites
(see Sprites). Resolving averages
`aa × aa` samples per output pixel through the current DAC (fades and flashes included).

## Game speed

The game moves every car a fixed distance per frame: nothing in the simulation depends on time (only the
steering yaw uses the measured frame length `B70E`), and the race clock advances one second every 5 frames, its
design rate. So how fast the world passes is purely the frame rate, `--frame-ticks`. Measured at the faithful
port's 23 ticks (6.3 frames a second): at a speedometer reading of 96 the car moves 193 world units a frame; the
cars are about 460 units long and 144 wide (1–1.4 cm a unit), so the scenery passes at 13–17 m/s, 30–40 mph —
about a third of the speedometer (the game's own odometer claims 16 m a frame, six times the geometry). The
original's 6-frame jumps of 2–3 m made that feel fast; drawn smoothly at the display rate it looks slow.

`ENH:` the default is **14 ticks** (10.4 frames a second, 1.6 times faster than the faithful port; chosen by
play-testing). At 10 ticks (14.6 frames a second, 2.3 times faster) the scenery passes at about the
speedometer's speed and 0–100 takes about 6 s instead of 10, and the race clock counts real time instead of
frames (`race_clock_hud`, `hud_topbar.c`): timer ticks of each frame, at most two frames' worth (a message box or
pause does not advance it, as the original's frame count stops then), a second per 145.652 ticks; `clock_frames`
and `clock_sub` keep their meaning (fifths of the second, and the digit made from them). Race times, the
opponents' times and penalties are therefore in real seconds at any game speed. Everything else the game counts
in frames (traffic, police, opponents, the lights, the wipers) runs faster together with the cars, as it did on
fast PCs (the original program's own limit is 5 ticks).

## Controls

`race_run` reads the controls three times a frame (`race_input` 0, 1, 2), all after the simulation step, so
the car reacted to keys held up to a whole frame (158 ms) earlier. `ENH:` the first read now runs just before
`frame_update`: still three reads a frame (steering and throttle change at the same rate per frame), the newest
one right before the physics. Measured: the steering wheel moves in the first frame after the key instead of the
second. (The game's own steering still builds up over about three frames.)

Keyboard steering (`steer_throttle`) is the original's: the wheel (0..20h, centre 10h) moves by 1, 2, then 3
a read (the hold count), 1 more far out when turning back, stops at the centre on a counter-turn; the turn
doubles at full lock (`sim_physics.c`, step 18). Its steps are per control read, three a frame, so at a faster
game speed the wheel swings faster in real time (23/14 times at the default 14 ticks). `ENH:` each read's step
and the hold count's growth are scaled by the last frame's ticks / 23 (`STEER_TICKS`, the faithful port's
pacing), the wheel keeping its fraction between reads: the same pace in seconds as the faithful port at any
`--frame-ticks`, and exactly the original at 23. Measured: full lock 0.28 s after the key at 14 ticks, 0.32 s
at 23 (frame granularity). Nothing else is tuned; the mouse and joystick are the original's.

## Finish marker

`ENH:` `compass_draw` (`hud_topbar.c`) also marks the direction of the leg's finish (`--finish-marker`, default
on): the finish is the one cell whose tile has a face of type 1Fh (the gas station; found by scanning the leg
map's tile models, remembered while that cell still holds it), its centre's bearing from the car relative to the
heading at the compass's 128 px a turn. Inside the window (x 8..31) a light green triangle at the bottom, under
the letter of that direction; outside it an arrow at the window's left or right edge. The window is redrawn
when the mark moves. The compass letters are the game's per leg (`leg_compass_offset`: they read N while
leg A of SCENE01 heads east); the mark follows the map.

## Police

`police_update` (`sim_traffic.c`) starts a chase for any police car whose nearest vertex is within about F80h
(depth key) of the camera while the car does 48 or more, in any direction: also one driving ahead the same
way, which then speeds off in front of the player at chase speed. `ENH:` a moving police car going the same
way (heading within 90° of the player's) that is ahead of the player (in front of the line across the car,
`police_ahead`) does not start a chase; once the player has overtaken it, it does. Oncoming police cars
(which turn round) and parked ones (a speed trap) as before, and so do the radar detector and the pull-over.

## Composition

`platform/vga.c` scales the 320 × 200 VGA picture by `--res-scale` and calls `enh_compose`, which lays the
enhanced view over the view window and the mirror. An original pixel of the view window shows the enhanced view
when the screen still holds what `view_present` copied there (so message boxes, the replay panel, the crash
cracks and the water roll, drawn over the screen or into V later, stay), V was not changed after the frame
(the cracks) — both compared as they were at that `view_present`, since the game draws its next frame before
presenting it and the host presents in between — it is not in the mirror's hole (V rows 0..13 at x 168..255) or under a message box
(`msg_protect`). A mirror pixel shows the enhanced mirror when it is in the part `mirror_present` copies, not
the frame `mirror_frame_draw` paints, the mirror is on and valid, and the screen still holds it.

While the enhanced view is shown the frame source reports a change on every call, so the host presents at
the display rate (VSync).

## Options

| Option | Default | |
|---|---|---|
| `--res-scale N` | 4 | picture 320 × 200 times N (1..8) |
| `--aa N` | 2 | N × N samples per output pixel (1 = off; res scale × aa is kept ≤ 16) |
| `--motion-delay P` | 100 | percent of a game frame the smooth view runs behind the game (below 100 it guesses ahead) |
| `--haze P` | 30 | distance haze: percent of the horizon's sky colour on what is farthest (0 = off) |
| `--classic` | | the original's picture only (at `--res-scale`, default 1) |

## Developer aids

* `TD3_ENH_COMPARE=1` (`2`): the newest game frame without smoothing, enhanced on the left (right) half of the
  screen and the original on the other half.
* `TD3_ENH_LOG=file`: one line per displayed frame: time, game frame, camera, primitives, render time in µs,
  speed, held direction bits, brake, throttle, steering wheel.
* `TD3_KBD_LOG=file` (`host.c`): every XT byte fed to the game's keyboard handler, with its time.
* `TD3_ENH_BLEND=frames`: how long a new frame's motion takes to take over (default 1).
* `TD3_DEBUG_KEYS=1` (`race_run`): turns on the original's dormant debug keys, Shift+T rain, Shift+S snow,
  Shift+N night (they also make the car invulnerable), to check the renderer in weather.
* The port's `TD3_SNAPSHOT_DIR` / `TD3_KEYS` work as before; snapshots are saved at the output resolution.

## Later

Longer draw distance (more cells, larger sprite and object ranges; the haze would then move out with it),
widescreen, the original's dither as an
option, weather effects animated at the display rate, the menu preview's own pacing.

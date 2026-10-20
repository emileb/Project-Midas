# Attract-demo desync fixes

GoldenEye's 14 attract demos are N64 controller recordings that store a byte of
the RNG seed in every block. Played back on a port that simulates the game
exactly as the N64 does, every block matches. A mismatch ("desync") means the
port did something the N64 did not, so the demos double as a gameplay
regression test.

Each fix found this way is listed here, and is marked in the code with
`DEMO DESYNC FIX #n` so it can be found with `grep -rn "DEMO DESYNC FIX" src port`.
They are written up so they can be reported to Project Midas upstream: unless
noted, they apply to every build of the port (32-bit Windows/Linux and 64-bit),
not only to this branch.

How they are found: `tools/n64trace` runs the same demo in an emulator with a
debugger, logs the N64's full seed, draw count and RNG callers per tick, and
compares them with the port's `GE_DEMODIAG` / `GE_RNGTRACE` output. See
[README.md](README.md).

## Current state

Per demo (`GE_DEMO=n`), macOS arm64, after fix #4, default `midas.ini`
(widescreen, fullscreen). "Port vs recording" is the first tick where the
port's seed byte differs from the recording's; "port vs N64" is `compare.py`
against the emulator (the authority: the emulated N64 itself disagrees with
the dam2 recording from tick 68, and ends a demo at its first mismatch).

| # | Demo | Port vs recording: before #1 | Port vs recording: now | N64 plays to | Port vs N64 |
|---|---|---|---|---|---|
| 0 | dam1 | 185 | none | 453 | identical |
| 1 | dam2 | 44 | 68 | 87 | identical (the N64 also fails at 68) |
| 2 | facility1 | 235 | none | 419 | identical |
| 3 | facility2 | none | none | 557 | identical |
| 4 | facility3 | 114 | none | 398 | identical |
| 5 | runway1 | 301 | none | 409 | identical |
| 6 | runway2 | 350 | none | 417 | identical |
| 7 | bunker1 | 297 | none | 498 | identical |
| 8 | bunker2 | 50 | none | 796 | identical |
| 9 | silo1 | 141 | none | 516 | identical |
| 10 | silo2 | none | none | 518 | identical |
| 11 | frigate1 | 116 | none | 343 | identical |
| 12 | frigate2 | 61 | none | 401 | identical |
| 13 | train | 279 | none | 484 | identical |

"Identical" means the same full 64-bit seed and RNG draw count every tick for
as long as both play (the port's log stops a few ticks before the N64's, at
the fade out). The results are the same paced and unpaced, and in any window
size.

Before fix #2, a widescreen run's numbers depended on the window (and, in
fullscreen, on when the window finished resizing), so the per-fix effects
below other than #1's on dam2 were measured in a fixed 4:3 setup.

Open issues: none in the demos. Fixes #2 and #3 are demo-only, so normal
widescreen play still culls differently from the N64 (see each fix).

## #1 Bullet dust puff skipped when only the floor-tile test hits

- **Found in:** dam2, tick 44 (Bond's first shot). The N64 made 269 RNG draws
  that tick, the port 27. Full 64-bit seeds matched up to tick 43.
- **Code:** `src/game/chrprop.c`, `chraiDefaultWeaponFireHandler`, the
  `explosionCreate(0, finalpos, hittile, 1, ...)` call.
- **What the N64 does:** when a player shot hits the stan (floor-tile)
  collision but no background triangle, `besttexture` is -1 and the original
  code reads `g_Textures[-1]`, the 8 bytes before the table. On the N64 those
  are zero (`0x800492F8`, read in the emulator), material 0 is neither 5 nor 6,
  so a type-1 explosion (dust puff, 10 shrapnel pieces, about 240 RNG draws)
  is created.
- **What the port did:** Project Midas added `besttexture >= 0 &&` to avoid
  the out-of-bounds read, which removed the puff. It also read the material
  from byte 0's low nibble, which on little-endian is `hitSound`, not
  `hitTexture` (the same bitfield-order bug the port had already fixed for the
  impact sound a few lines above).
- **Fix:** on PC, treat `besttexture < 0` as material 0 (create the puff) and
  read `g_Textures[besttexture].hitTexture` otherwise. The N64 build keeps the
  decomp's original line.
- **Effect:** dam2 44 -> 68, after which dam2 matches the N64 for every tick
  it plays. The changes first seen in other demos at the same time were
  mostly the fullscreen resize timing of #2, not this fix.
- **Applies to:** every build of the port (the guard is in the base commit).
- **Upstream:** not reported yet.

## #2 Demos simulated with the window's aspect ratio

- **Found in:** every demo. With the default `widescreen=1`, 10 of the 14
  demos desynced somewhere (dam1, facility1, facility3, runway1, runway2,
  bunker1, silo1, frigate1, frigate2, train), and in fullscreen the results
  changed from run to run.
- **Code:** `src/game/bondview2.c` (`portSetScenePillar` call before
  `portWideFactor`), `port/src/video.c` `portPillarboxFrame`.
- **What the port did:** widescreen widens the player's projection aspect by
  the window's shape, and the game's culling follows it ("culling follows for
  free", bondview2.c). So rooms and props at the screen edges count as
  visible when on the N64 they are not: more rooms load (bullets hit different
  walls), more monitors run their RNG-drawing scripts, guards see differently.
  In fullscreen on macOS the window opens at 640x480 and reaches its final
  shape some time later, so the tick at which the frustum widened depended on
  wall-clock time.
- **Fix:** during demo playback, the frame asks for the existing MP pillarbox
  (a centred 4:3 with black bars, bit 2 of `portSetScenePillar`), so the wide
  factor reads 1.0 whatever the window is.
- **Effect:** the per-tick logs are now identical paced and unpaced, and in a
  1280x720 window, a 640x480 window and fullscreen. Matching the N64:
  3 -> 10 demos (dam1, facility1, facility3, runway2, bunker1, silo1 and
  train became identical).
- **Applies to:** every build with widescreen on (the default). Normal play
  still widens culling with the window, which is the feature; it just means a
  widescreen game is not frame-for-frame the N64's.
- **Upstream:** not reported yet.

## #3 Padded room boxes make props visible early

- **Found in:** frigate2 tick 64 (and runway1 301, frigate1 116). A
  single-screen monitor at x=434 ran its random-effect script one tick after
  the port started drawing it; the N64 never drew it at all.
- **Code:** `src/game/bg.c`, the `GE_PORTALPAD` block after the portal walk.
- **What the port did:** pads every drawn room's screen box by 12 px so
  coarse portal quads don't clip visible geometry. The comment says the room
  set stays vanilla, which is true, but the same boxes decide which props are
  on screen (`getPropCombinedRoomsBBox2D` -> `posIsOnScreen` ->
  `camIsPosInScreenBox`). The emulator showed room 50's box as x 1..37.5 on
  the N64 and 1..49.5 on the port; the monitor's margin (108.885) was the same.
- **Fix:** no padding during demo playback, like the existing demo exception
  for the one-pixel edge inset in the same file.
- **Effect:** runway1, frigate1 and frigate2 match the N64 to the end.
- **Applies to:** every build. Outside demos the pad still marks props
  on screen up to 12 px early; a full fix would pad only the scissor used for
  drawing and keep the vanilla box for `posIsOnScreen`.
- **Upstream:** not reported yet.

## #4 The watch's Control tab is the port's page during demos

- **Found in:** bunker2 tick 269. The recording opens the watch, goes to
  Control Options and edits the control style (selected from tick 159 to 260,
  read on the N64 with `--peek`), then presses START at tick 268. On the N64
  the watch closes at 269; on the port it stayed open.
- **Code:** `src/game/options.c`: `sub_GAME_7F0A6A80` (Control tab
  navigation), the A/Z handler in the watch drawer, `portWatchRaphnetView`,
  and the device-style write after `sub_GAME_7F0A611C`.
- **What the port did:** the Control tab is the port's own page (input device
  picker, bindings). The recording's Z press, made on Rare's style row, armed
  the port's "press a button" device assign instead, and START then cancelled
  the assign (`portWatchModalBack`) rather than closing the watch. The
  vanilla style edit never happened either, so the rest of the demo could
  have run with a different control style.
- **Fix:** during demo playback the Control tab uses Rare's page and
  navigation (style and inputs rows), A/Z is Rare's select toggle, and a
  style picked by the recording is not written to the device's style in
  `midas.ini`.
- **Effect:** bunker2 matches the N64.
- **Applies to:** every build. The Game Options tab is still the port's page
  in demos; no demo opens it.
- **Upstream:** not reported yet.

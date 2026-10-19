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

Per demo (`GE_DEMO=n`), macOS arm64, after fix #1. "Port vs recording" is the
first tick where the port's seed byte differs from the recording's;
"port vs N64" is `compare.py` against the emulator (the authority: the
emulated N64 itself disagrees with the dam2 recording from tick 68, and ends a
demo at its first mismatch).

| # | Demo | Port vs recording (before #1) | Port vs recording | N64 plays to | Port vs N64 |
|---|---|---|---|---|---|
| 0 | dam1 | 185 | 189 | 453 | differs at 189 |
| 1 | dam2 | 44 | 68 | 87 | **identical 1-87** |
| 2 | facility1 | 235 | 345 | 419 | differs at 345 |
| 3 | facility2 | none | none | 557 | **identical 1-541** |
| 4 | facility3 | 114 | 114 | 398 | differs at 114 |
| 5 | runway1 | 301 | 301 | 409 | differs at 301 |
| 6 | runway2 | 350 | 350 | 417 | differs at 350 |
| 7 | bunker1 | 297 | 445 | 498 | differs at 445 |
| 8 | bunker2 | 50 | 269 | ? | see below |
| 9 | silo1 | 141 | 281 | 516 | differs at 281 |
| 10 | silo2 | none | none | 518 | **identical 1-510** |
| 11 | frigate1 | 116 | 116 | 343 | differs at 116 |
| 12 | frigate2 | 61 | 63 | 401 | differs at 63 |
| 13 | train | 279 | 279 | 484 | differs at 279 |

Open issues:
- **Wall-clock dependence.** With `GE_DEMOFAST=1 GE_NOSLEEP=1` (unpaced
  playback) 12 demos give identical per-tick logs, but silo1 (281 -> 141) and
  frigate2 (63 -> 61) change, and bunker2 varies from run to run even paced.
  The N64's timing is fixed, so some simulation in the port depends on real
  time, probably game logic that runs in the render path (player shots and
  monitor scripts draw RNG from `lvlRender`) being run a different number of
  times per tick. Until it's found, compare paced runs (`PACED=1`).
- **bunker2**: the emulator run did not report an end; check `getrace`
  output for demo 8.
- **frigate2, tick 61-63**: a multi-screen monitor's random-effect script
  draws RNG one tick away from the N64.

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
- **Effect:** dam2 44 -> 68, bunker2 50 -> 269, silo1 141 -> 281,
  facility1 235 -> 345, bunker1 297 -> 445, dam1 185 -> 189, frigate2 61 -> 63.
- **Applies to:** every build of the port (the guard is in the base commit).
- **Upstream:** not reported yet.

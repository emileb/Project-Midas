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

First desync tick per demo (`GE_DEMO=n`, tolerant mode), macOS arm64:

| # | Demo | Before #1 | After #1 | End |
|---|---|---|---|---|
| 0 | dam1 | 185 | 189 | 438 |
| 1 | dam2 | 44 | 68 | 445 |
| 2 | facility1 | 235 | 345 | 410 |
| 3 | facility2 | none | none | 538 |
| 4 | facility3 | 114 | 114 | 380 |
| 5 | runway1 | 301 | 301 | 388 |
| 6 | runway2 | 350 | 350 | 388 |
| 7 | bunker1 | 297 | 445 | 482 |
| 8 | bunker2 | 50 | 269 | varies, see below |
| 9 | silo1 | 141 | 281 | 501 |
| 10 | silo2 | none | none | 503 |
| 11 | frigate1 | 116 | 116 | 328 |
| 12 | frigate2 | 61 | 63 | 384 |
| 13 | train | 279 | 279 | 464 |

Open issues:
- **bunker2 is not deterministic run to run**: its end tick has been 388, 438
  and 328 across runs, so something there depends on more than the inputs.
- **frigate2, tick 61-63**: a multi-screen monitor's random-effect script
  draws RNG one tick away from the N64. The port gives each screen the same
  animation as the N64 code and ROM data imply; the remaining difference is
  timing.

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

# n64trace: N64 ground truth for attract-demo desyncs

The 14 attract demos record a byte of the RNG seed in every block, so a port
that simulates GoldenEye exactly like the N64 plays them in sync to the end.
When one desyncs, this tool runs the same demo in mupen64plus with its
debugger and logs what the N64 did: the full seed and RNG draw count per tick,
every RNG caller, and any function's arguments. Comparing that with the
port's own diagnostics shows the first point where the two differ.

Fixes found this way are listed in [DESYNC_FIXES.md](DESYNC_FIXES.md) and
marked `DEMO DESYNC FIX #n` in the code.

## Build

```sh
tools/n64trace/build.sh
```

This clones mupen64plus-core and mupen64plus-rsp-hle at pinned commits into
`tools/n64trace/build/` (gitignored), builds the core with `DEBUGGER=1`, and
builds the stub plugins and `getrace`. Needs git, make, a C compiler, SDL2,
libpng, zlib and freetype (Homebrew: `brew install sdl2 libpng freetype`).
mupen64plus is GPL-2.0 and is downloaded, not vendored.

## Use

1. Find the first desync on the port:

   ```sh
   tools/n64trace/run_port_demos.sh build_mac demo_logs
   ```

   Don't touch the game windows while it runs: a key press ends a demo.
   It plays the demos unpaced (`GE_DEMOFAST=1 GE_NOSLEEP=1`, several times
   faster; `vsync=0` in the build's `midas.ini` helps further). For now use
   `PACED=1` when the exact tick matters: two demos still change when
   unpaced (see DESYNC_FIXES.md, open issues).

2. Log the same demo on the N64 and compare:

   ```sh
   B=tools/n64trace/build
   $B/getrace --demo 1 --stop 80 build_mac/Data/baserom.u.z64 > n64.log
   tools/n64trace/compare.py demo_logs/demo1.log n64.log
   ```

   `compare.py` prints the first tick where the draw count or the full seed
   differ. Ticks are numbered the same way on both sides.

   **Compare against the emulator, not the recording.** The emulated N64
   itself disagrees with some recordings (dam2 from tick 68: port and
   emulator produce the same seed, the recording another), and, unlike the
   port's tolerant mode, the N64 ends a demo at its first mismatch with the
   recording (`getrace` reports "demo ended after tick N" and exits). So a
   port desync from the recording is only a port bug if `compare.py` also
   shows a difference; "identical for ticks 1-N" up to the N64's end means
   the port matches the N64 for as long as the N64 plays the demo.

3. List the RNG callers around that tick on both sides:

   A whole demo takes a few seconds headless. Then:

   ```sh
   $B/getrace --demo 1 --stop 46 --trace 44 build_mac/Data/baserom.u.z64 \
       | tools/n64trace/n64syms.py annotate > n64_callers.log
   ```

   On the port, run the demo with `GE_RNGTRACE=44` under `lldb` (which
   disables ASLR) and symbolize with `atos -l 0x100000000`:

   ```sh
   cd build_mac && GE_DEMO=1 GE_DEMODIAG=1 GE_RNGTRACE=44 lldb -b -o run -- ./ProjectMidas
   ```

   The first caller that differs is where the simulations split.

4. Narrow it down with breakpoints. `--break ADDR,...` logs `a0`-`a3`, `ra`,
   `sp` and the three floats behind any pointer argument for each hit in the
   traced ticks. `--stack ADDR` lists the code addresses on the stack at the
   first traced draw from `[ADDR, ADDR+0x4000)`, i.e. who called it.
   Function addresses come from the decomp (`Address 0x7F...` comments,
   `sub_GAME_7F...` names, asm files); `n64syms.py table` lists the ~1200 it
   can find.

## Traps

- **Never read TLB-mapped memory with the debugger.** GoldenEye demand-pages
  its `0x7F` code; a debugger read of a page that is not resident raises a TLB
  refill exception inside the game and it crashes or hangs. `getrace` reads
  only KSEG0 RAM (`0x80000000`-`0x80800000`). Function arguments on the stack
  and game data are there; code is not.
- Breakpoints use virtual addresses, so `0x7F...` addresses are fine as
  breakpoints even though they can't be read.
- A function's address in a comment can be wrong for the US build; check
  that a breakpoint there fires with sensible arguments before trusting it.
- `nullgfx` exists because the core's built-in dummy video never raises the
  RDP interrupt and the game waits for it forever; `nullinput` because the
  legal screen otherwise stops at "No Controllers".
- The demo is chosen by setting `g_randomSeed` just before
  `select_ramrom_to_play` draws; the demo then restores its recorded seed, so
  this doesn't affect the run.

## Addresses used (US)

| What | Address |
|---|---|
| `randomGetNext` | `0x7000A450` |
| `g_randomSeed` | `0x80024460` |
| `ramrom_replay_handler` (seed check) | `0x7F0C0080` |
| `select_ramrom_to_play` | `0x7F0C0970` |
| `g_Textures` | `0x80049300` |
| `chraiDefaultWeaponFireHandler` | `0x7F03B15C` |
| `bgTestBulletHitBackground` | `0x7F0B7A78` |
| `walkTilesBetweenPoints_NoCallback` | `0x7F0B0BE4` |
| `chrlvStanLineDirIntersection` | `0x7F02624C` |

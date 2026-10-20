#!/bin/bash
# Play the 14 attract demos on a port build and print, per demo, the first
# RNG desync tick and how playback ended.
#   run_port_demos.sh <dir with ProjectMidas> [out dir] [extra args...]
# Each run stops at its first "demo exit" line, when a second demo starts
# (attract mode plays on), or after 150 s. Logs keep the
# GE_DEMODIAG lines for tools/n64trace/compare.py. Don't touch the game
# windows while it runs: a real key or button press ends a demo.
# Demos run unpaced (GE_DEMOFAST=1 GE_NOSLEEP=1; same results, ~5x faster);
# vsync=0 in the build's midas.ini makes it faster still. Set
# PACED=1 to play at N64 speed.
dir=$1; out=${2:-demo_logs}; shift; [ $# -gt 0 ] && shift
mkdir -p "$out"
out=$(cd "$out" && pwd)
for i in $(seq 0 13); do
  log="$out/demo$i.log"
  if [ -z "$PACED" ]; then export GE_DEMOFAST=1 GE_NOSLEEP=1; fi
  (cd "$dir" && GE_DEMO=$i GE_DEMODIAG=1 exec ./ProjectMidas "$@" > "$log" 2>&1) &
  pid=$!
  for t in $(seq 1 600); do
    sleep 0.25
    kill -0 $pid 2>/dev/null || break
    grep -q "demo exit:" "$log" && break
    # a multiplayer demo can end without an exit line; stop when the next starts
    [ "$(grep -c "demo start:" "$log")" -gt 1 ] && break
  done
  kill $pid 2>/dev/null; wait $pid 2>/dev/null
  desync=$(grep -m1 "RANDSEED desync at tick" "$log" | sed -E 's/.*at tick ([0-9]+).*/\1/')
  ex=$(awk '/demo start:/{n++} n<=1' "$log" | grep -m1 "demo exit:" | sed 's/.*demo exit: //; s/, gametime.*//')
  fatal=$(grep -m1 -i "fatal signal\|AddressSanitizer\|runtime error" "$log")
  printf "demo%-2d desync@%-5s %s%s\n" "$i" "${desync:-none}" "${ex:-NO EXIT}" "${fatal:+ | $fatal}"
done

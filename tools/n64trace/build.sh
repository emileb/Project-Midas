#!/bin/sh
# Build the N64 side of tools/n64trace into tools/n64trace/build/:
# mupen64plus-core with its debugger, the HLE RSP plugin, the two stub
# plugins and getrace. mupen64plus (GPL-2.0) is cloned here at pinned
# commits, not vendored. Needs: git, make, a C compiler, SDL2, libpng,
# zlib, freetype (Homebrew: brew install sdl2 libpng freetype).
set -e
cd "$(dirname "$0")"
OUT=$PWD/build
mkdir -p "$OUT/src"

CORE_REV=902bc60f561570bb3480d76910258b9afa10bafd
RSP_REV=8a7a472a7172eb2c8725b305eae26818ed7b51a2

fetch() { # dir url rev
    if [ ! -d "$OUT/src/$1" ]; then
        git clone -q "$2" "$OUT/src/$1"
    fi
    git -C "$OUT/src/$1" fetch -q origin "$3" 2>/dev/null || true
    git -C "$OUT/src/$1" checkout -q "$3"
}
fetch core https://github.com/mupen64plus/mupen64plus-core.git "$CORE_REV"
fetch rsp-hle https://github.com/mupen64plus/mupen64plus-rsp-hle.git "$RSP_REV"

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
# DEBUGGER=1 for breakpoints; NO_ASM / no dynarec so the interpreter runs
# (accurate, and the debugger only steps the interpreters)
make -C "$OUT/src/core/projects/unix" -j"$JOBS" all DEBUGGER=1 NO_ASM=1 NEW_DYNAREC=0 OSD=0 >"$OUT/core.log" 2>&1 \
    || { echo "core build failed, see $OUT/core.log"; exit 1; }
make -C "$OUT/src/rsp-hle/projects/unix" -j"$JOBS" all APIDIR="$OUT/src/core/src/api" >"$OUT/rsp.log" 2>&1 \
    || { echo "rsp-hle build failed, see $OUT/rsp.log"; exit 1; }

API="$OUT/src/core/src/api"
cp "$OUT/src/core/projects/unix/"libmupen64plus.*dylib "$OUT/" 2>/dev/null || cp "$OUT/src/core/projects/unix/"libmupen64plus.so* "$OUT/"
cp "$OUT/src/rsp-hle/projects/unix/"mupen64plus-rsp-hle.* "$OUT/"
rm -rf "$OUT/data" && cp -R "$OUT/src/core/data" "$OUT/data"

case "$(uname)" in Darwin) SO=dylib; SHARED=-dynamiclib ;; *) SO=so; SHARED="-shared -fPIC" ;; esac
cc -O2 -Wall -I"$API" $SHARED -o "$OUT/nullgfx.$SO" nullgfx.c
cc -O2 -Wall -I"$API" $SHARED -o "$OUT/nullinput.$SO" nullinput.c
cc -O2 -Wall -I"$API" -o "$OUT/getrace" getrace.c -ldl
echo "built $OUT/getrace"

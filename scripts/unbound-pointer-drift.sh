#!/bin/sh
# SOH [Unbound] Pointer-type drift check for the decompiled game sources.
#
# The game's C files are compiled with -w, so when Unbound widens a type (Vec3s -> Vec3f path points,
# s16 -> s32 collision vertices, Sphere16 centres to f32, ...) a caller that still passes the old pointer type
# compiles silently and reads the new bytes as the old type. Three such bugs shipped in 9.2.3-unbound0.3.
# This re-checks every game C file with only -Wincompatible-pointer-types on, and drops the vanilla
# "asset name string as pointer" hits so what remains is real drift. Clang only.
#
#   scripts/unbound-pointer-drift.sh [build-dir]        (default build-cmake)
set -e
BUILD=${1:-build-cmake}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FLAGS=$BUILD/soh/CMakeFiles/soh.dir/flags.make
[ -f "$FLAGS" ] || { echo "no $FLAGS; configure the build first" >&2; exit 2; }
CC=$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' "$BUILD/CMakeCache.txt")
RSP=$(mktemp)
{ sed -n 's/^C_DEFINES = //p' "$FLAGS"; sed -n 's/^C_INCLUDES = //p' "$FLAGS"; sed -n 's/^C_FLAGS = //p' "$FLAGS" | sed 's/ -w / /g'; } | tr '\n' ' ' > "$RSP"
cd "$ROOT"
find soh/src -name '*.c' | xargs -P "$(sysctl -n hw.ncpu 2>/dev/null || nproc)" -n 1 sh -c \
  "\"$CC\" -fsyntax-only @$RSP -Wno-everything -Wincompatible-pointer-types \"\$0\" 2>&1 | grep 'incompatible pointer types' || true" \
  | grep -v "const char" | sort -u
rm -f "$RSP"

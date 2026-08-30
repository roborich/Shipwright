#!/bin/sh
# SOH [Unbound] Type-drift checks for the decompiled game sources (clang only).
#
# The game's C files are compiled with -w, so when Unbound widens a type (Vec3s -> Vec3f path points,
# s16 -> s32 collision vertices, Sphere16 centres to f32, ...) a caller that still uses the old type compiles
# silently and decodes the new bytes as the old type. Three such bugs shipped in 9.2.3-unbound0.3.
# Both modes keep only diagnostics that mention a type this fork widened (WIDENED below); extend that list
# when you widen something new.
#
#   scripts/unbound-pointer-drift.sh [build-dir]              pointer drift: the old *pointer* type is passed
#                                                             or assigned, so the callee reads the wrong bytes.
#                                                             Expected output: only the vanilla lines named in
#                                                             unbound-docs/README.md.
#   scripts/unbound-pointer-drift.sh --narrowing [build-dir]  value drift: a widened value stored into a
#                                                             narrower one (an s16 local holding a position).
#                                                             These are the world-extent limits, most of them
#                                                             vanilla; grep for the field you just widened.
set -e
MODE=pointer
[ "$1" = "--narrowing" ] && { MODE=narrowing; shift; }
BUILD=${1:-build-cmake}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FLAGS=$BUILD/soh/CMakeFiles/soh.dir/flags.make
[ -f "$FLAGS" ] || { echo "no $FLAGS; configure the build first" >&2; exit 2; }
CC=$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' "$BUILD/CMakeCache.txt")
RSP=$(mktemp)
{ sed -n 's/^C_DEFINES = //p' "$FLAGS"; sed -n 's/^C_INCLUDES = //p' "$FLAGS"; sed -n 's/^C_FLAGS = //p' "$FLAGS" | sed 's/ -w / /g'; } | tr '\n' ' ' > "$RSP"
# Types and fields this fork widened (unbound-docs/extent.md). Pointer mode matches them in the diagnostic;
# narrowing mode matches them in the offending source line.
WIDENED='Vec3s|Vec3f|Vec3i|Sphere16|Cylinder16|Spheref|Cylinderf|CollisionPoly|CollisionHeader|WaterBox|SurfaceType|CamData|LightPoint|PathData|\bPath\b|Vtx'
FIELDS='vtxList|minBounds|maxBounds|\bdist\b|xMin|zMin|ySurface|xLength|zLength|points|boundingSphere|worldSphere|center|\bdim\.|minY|maxY|floorHeight|BGCHECK_Y_MIN|horsePos|HorseData|\.pos\b'
cd "$ROOT"
if [ "$MODE" = pointer ]; then
  find soh/src -name '*.c' | xargs -P "$(sysctl -n hw.ncpu 2>/dev/null || nproc)" -n 1 sh -c \
    "\"$CC\" -fsyntax-only @$RSP -Wno-everything -Wincompatible-pointer-types \"\$0\" 2>&1 | grep 'incompatible pointer types' || true" \
    | grep -E "$WIDENED" | grep -v "const char" | sort -u
else
  find soh/src -name '*.c' | xargs -P "$(sysctl -n hw.ncpu 2>/dev/null || nproc)" -n 1 sh -c \
    "\"$CC\" -fsyntax-only @$RSP -Wno-everything -Wimplicit-int-conversion -Wimplicit-float-conversion -Wfloat-conversion \"\$0\" 2>&1 | grep -A1 'implicit conversion' || true" \
    | awk -v ids="$FIELDS|$WIDENED" '/warning:/ { w=$0; next } { if (w != "" && $0 ~ ids) print w "\n    " $0; w="" }' | sort -u
fi
rm -f "$RSP"

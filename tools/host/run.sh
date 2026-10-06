#!/bin/sh
# Builds the Mac harness and renders one act to frames, then a contact sheet
# and a GIF. Run from the repo root:
#   tools/host/run.sh <out_dir> <act> <seconds> [stride] [theme] [weather] [temp]
set -e
OUT="$1"
shift
mkdir -p "$OUT"
BIN="${TMPDIR:-/tmp}/orbi-harness"
cc -O2 -Wall -Wno-unused-function -Itools/host \
  tools/host/harness.c src/c/raster.c src/c/bg.c src/c/face.c src/c/anim.c -lm -o "$BIN"
find "$OUT" -name 'f*.ppm' -delete
"$BIN" "$OUT" "$@"
python3 tools/host/frames.py "$OUT"

#!/bin/bash
# Same-engine sweep: rccore+Skia (rc2image) vs rccore+ThorVG (rc2png). With the engine held
# constant, every remaining difference belongs to the ThorVG backend.
# usage: [FRAMES=n] tools/sweep_skia.sh DIR OUTDIR [SIZE]
# Note: rc2image clears once and paints FRAMES times on top, so translucent overlays
# accumulate when FRAMES>1; rc2png clears per frame (as a device must). Default 1.
DIR=$(cd "$1" && pwd); OUT=$(mkdir -p "$2" && cd "$2" && pwd); W=${3:-400}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLOCK=1760700000000
for f in "$DIR"/*.rc; do
  n=$(basename "$f" .rc)
  RC_FRAMES=${FRAMES:-1} "$ROOT/host/skia-ref/tools/rc2image/rc2image" "$f" "$OUT/$n.ref.png" --fit $W $W --time $CLOCK --seed 1 >/dev/null 2>&1 || { echo "$n: skia failed"; continue; }
  "$ROOT/host/build/rc2png" "$f" "$OUT/$n.png" --native --fit $W $W --theme dark --bg FFFFFFFF --frames ${FRAMES:-1} --time $CLOCK >/dev/null 2>&1 || { echo "$n: rc2png failed"; continue; }
  python3 -I "$ROOT/tools/compare.py" "$OUT/$n.ref.png" "$OUT/$n.png" "$OUT/$n.cmp.png" | sed "s|$OUT/||"
done | sort -t' ' -k5 -rn

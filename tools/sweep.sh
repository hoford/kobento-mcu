#!/bin/bash
# Render every .rc in DIR with rc2png (ThorVG) and the TS reference player, then score.
# usage: tools/sweep.sh DIR OUTDIR [WIDTH]
DIR=$(cd "$1" && pwd); OUT=$(mkdir -p "$2" && cd "$2" && pwd); W=${3:-400}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TS=${RC_EXPERIMENTS:-$ROOT/../remotecompose-experiments}/players/typescript
CLOCK=1760700000000   # pinned so time-driven docs are comparable
for f in "$DIR"/*.rc; do
  n=$(basename "$f" .rc)
  "$ROOT/host/build/rc2png" "$f" "$OUT/$n.png" --frames 3 --time $CLOCK --fit $W $W >/dev/null 2>"$OUT/$n.err" || { echo "$n: RC2PNG FAILED"; continue; }
  (cd "$TS" && node render.mjs "$f" "$OUT/$n.ref.png" --width $W --height $W --frames 3 --time $CLOCK >/dev/null 2>&1) || { echo "$n: ref failed"; continue; }
  python3 -I "$ROOT/tools/compare.py" "$OUT/$n.ref.png" "$OUT/$n.png" "$OUT/$n.cmp.png" | sed "s|$OUT/||"
done | sort -t' ' -k5 -rn

#!/bin/sh
# Regenerate the Kobento logo RC documents from the brand SVGs.
#   tools/logo/build_logos.sh            # square 480x480 (default)
#   tools/logo/build_logos.sh W H        # custom canvas
# svg2rc.py bakes the SVG transforms into absolute paths (RC has no skew) and emits
# paint+drawPath; compile.mjs (json2rc) turns the JSON into the .rc.
set -e
cd "$(dirname "$0")/../.."
W=${1:-480}; H=${2:-480}
for name in color black; do
  python3 tools/logo/svg2rc.py "brand/kobento-logo-$name.svg" \
      "tools/logo/kobento_logo_$name.json" "$W" "$H"
  node tools/splash/compile.mjs "tools/logo/kobento_logo_$name.json" \
      "documents/kobento_logo_$name.rc"
done
echo "built documents/kobento_logo_{color,black}.rc at ${W}x${H}"

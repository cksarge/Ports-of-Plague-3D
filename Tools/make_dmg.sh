#!/bin/bash
# Makes the disk image people download: it opens to a window with the game and the Applications folder side by
# side, and the game is installed by dragging one onto the other.
#   Tools/make_dmg.sh            after Tools/package_mac.sh; writes Saved/Package/Ports-of-Plague.dmg
#   Tools/make_dmg.sh 1.2        the same, named Ports-of-Plague-1.2.dmg
# The first run fetches the two small Python tools it uses (dmgbuild, to lay the window out without opening
# Finder, and Pillow, to draw its background) into Saved/DmgTools.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/Saved/Package/Mac/PortsOfPlague.app"
NAME="Ports-of-Plague${1:+-$1}"
OUT="$ROOT/Saved/Package/$NAME.dmg"
WORK="$ROOT/Saved/DmgWork"
ENV="$ROOT/Saved/DmgTools"
[ -d "$APP" ] || { echo "No packaged game at $APP. Run Tools/package_mac.sh first."; exit 1; }

if [ ! -x "$ENV/bin/dmgbuild" ]; then
  echo "Fetching the disk-image tools (once)..."
  python3 -m venv "$ENV"
  "$ENV/bin/pip" install --quiet --upgrade pip
  "$ENV/bin/pip" install --quiet dmgbuild pillow
fi

rm -rf "$WORK"; mkdir -p "$WORK"
# In the image the game carries its proper name, with spaces.
ditto "$APP" "$WORK/Ports of Plague.app"
"$ENV/bin/python" "$ROOT/Tools/dmg/make_background.py" "$WORK"
# One picture holding both sizes, so the background is sharp on a Retina screen.
tiffutil -cathidpicheck "$WORK/background.png" "$WORK/background@2x.png" -out "$WORK/background.tiff" > /dev/null
rm -f "$OUT"
"$ENV/bin/dmgbuild" -s "$ROOT/Tools/dmg/settings.py" \
  -D app="$WORK/Ports of Plague.app" -D icon="$APP/Contents/Resources/AppIcon.icns" -D background="$WORK/background.tiff" \
  "Ports of Plague" "$OUT" > /dev/null
rm -rf "$WORK"
echo "Done: $OUT ($(du -h "$OUT" | cut -f1))"

#!/bin/bash
# Fetches the game's three typefaces (the same families the website uses, all
# under the SIL Open Font License) as complete fonts, and writes the weights
# the game needs to Content/UI/Fonts. The website ships cut-down copies that
# lack the florin sign and a few other characters; these are the full ones.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/Content/UI/Fonts"
WORK="$ROOT/Intermediate/PortsFonts"
mkdir -p "$OUT" "$WORK"
[ -d "$WORK/venv" ] || python3 -m venv "$WORK/venv"
"$WORK/venv/bin/pip" install --quiet fonttools brotli
BASE="https://github.com/google/fonts/raw/main/ofl"
fetch() { [ -s "$WORK/$2" ] || curl -sSL --fail -o "$WORK/$2" "$BASE/$1"; }
fetch "ebgaramond/EBGaramond%5Bwght%5D.ttf" EBGaramond-var.ttf
fetch "ebgaramond/EBGaramond-Italic%5Bwght%5D.ttf" EBGaramond-Italic-var.ttf
fetch "cinzel/Cinzel%5Bwght%5D.ttf" Cinzel-var.ttf
fetch "unifrakturmaguntia/UnifrakturMaguntia-Book.ttf" UnifrakturMaguntia.ttf
inst() { "$WORK/venv/bin/fonttools" varLib.instancer "$WORK/$1" wght=$2 -o "$OUT/$3" > /dev/null 2>&1; }
inst EBGaramond-var.ttf 400 EBGaramond-Regular.ttf
inst EBGaramond-var.ttf 600 EBGaramond-SemiBold.ttf
inst EBGaramond-Italic-var.ttf 400 EBGaramond-Italic.ttf
inst Cinzel-var.ttf 600 Cinzel-SemiBold.ttf
inst Cinzel-var.ttf 800 Cinzel-ExtraBold.ttf
cp "$WORK/UnifrakturMaguntia.ttf" "$OUT/UnifrakturMaguntia.ttf"
# The licences travel with the fonts.
cp "$ROOT/ports_web_reference_READ_ONLY/assets/licenses/OFL-"*.txt "$OUT/"
ls -la "$OUT"

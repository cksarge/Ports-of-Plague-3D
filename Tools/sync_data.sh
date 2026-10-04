#!/bin/bash
# Copies the game's data files, unchanged, from the web reference into
# Content/Data, where the Unreal game loads them. The reference is never
# written to. With --check, only reports whether the copies still match.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/ports_web_reference_READ_ONLY/data"
DST="$ROOT/Content/Data"
status=0
mkdir -p "$DST"
for f in "$SRC"/*.json; do
  name="$(basename "$f")"
  if [ "${1:-}" = "--check" ]; then
    cmp -s "$f" "$DST/$name" || { echo "DIFFERS: $name"; status=1; }
  else
    cp "$f" "$DST/$name"
  fi
done
[ "${1:-}" = "--check" ] && [ $status -eq 0 ] && echo "Content/Data matches the reference."
exit $status

#!/bin/bash
# Converts the web version's six songs (ports_web_reference_READ_ONLY/assets/music) to the
# WAV files Unreal imports, in SourceArt/Music. Run Tools/build_content.sh afterwards.
# Only needed if the songs change: the imported songs are already in Content/Ports/Audio.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/SourceArt/Music"
for t in menu trade-1 trade-2 trade-3 plague ending; do
  afconvert -f WAVE -d LEI16@44100 "$ROOT/ports_web_reference_READ_ONLY/assets/music/$t.mp3" "$ROOT/SourceArt/Music/music_${t//-/_}.wav" && echo "made music_${t//-/_}.wav"
done

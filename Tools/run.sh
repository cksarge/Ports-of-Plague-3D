#!/bin/bash
# Runs the game in its own window, without opening the editor.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
exec "$ENGINE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$ROOT/PortsOfPlague.uproject" -game -windowed -ResX=1600 -ResY=900 -log "$@"

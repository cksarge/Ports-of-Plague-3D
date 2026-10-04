#!/bin/bash
# Rebuilds the generated Unreal assets (close the Unreal editor first).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
exec "$ENGINE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/PortsOfPlague.uproject" -run=pythonscript -script="$ROOT/Tools/build_content.py" -unattended -nosplash -nullrhi "$@"

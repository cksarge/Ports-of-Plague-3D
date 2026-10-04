#!/bin/bash
# Compiles the game's C++ for the editor (close the Unreal editor first).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
exec "$ENGINE/Engine/Build/BatchFiles/Mac/Build.sh" PortsOfPlagueEditor Mac Development -project="$ROOT/PortsOfPlague.uproject" "$@"

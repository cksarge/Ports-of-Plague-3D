#!/bin/bash
# Runs the rules tests without opening the editor window and prints the results.
#   Tools/test.sh            every Ports test
#   Tools/test.sh Golden     only Ports.Engine.Golden
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
FILTER="Ports.Engine${1:+.$1}"
LOG="$ROOT/Saved/Logs/PortsTests.log"
mkdir -p "$ROOT/Saved/Logs"
"$ENGINE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/PortsOfPlague.uproject" -ExecCmds="Automation RunTests ${FILTER};Quit" \
  -testexit="Automation Test Queue Empty" -unattended -nullrhi -nosplash -nosound -abslog="$LOG" > /dev/null 2>&1
grep -E "Test Completed|LogAutomationController: Error" "$LOG" | sed -E 's/^\[[^]]*\]\[[ 0-9]*\]//; s/LogAutomationController: (Display: )?//; s/ Path=\{.*//'
echo "passed: $(grep -c "Result={Success}" "$LOG")   failed: $(grep -c "Result={Fail" "$LOG")"

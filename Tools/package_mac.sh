#!/bin/bash
# Makes the packaged Mac game (Saved/Package/Mac/PortsOfPlague.app). Close the Unreal editor first.
#
# Unreal 5.8's build tool starts Xcode with no input channel when it finishes the app, and Xcode
# then fails at once. So the two last steps of the build (finishing the app with Xcode, and writing
# the build's receipt) are run here directly, and the packaging is told the build is already done.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
STEPS="$ROOT/Intermediate/Build/Mac/arm64/PortsOfPlague/Development"
LOG="$ROOT/Saved/Logs/package_mac.log"
mkdir -p "$ROOT/Saved/Logs"

echo "Building the game (its last step is expected to fail here; it is redone below)..."
"$ENGINE/Engine/Build/BatchFiles/Mac/Build.sh" PortsOfPlague Mac Development -project="$ROOT/PortsOfPlague.uproject" < /dev/null > "$LOG" 2>&1
if ! grep -q "Link \[Apple\] PortsOfPlague\|Target is up to date" "$LOG"; then echo "The game did not compile. See $LOG"; exit 1; fi

cd "$ENGINE" || exit 1
source Engine/Build/BatchFiles/Mac/SetupEnvironment.sh -dotnet Engine/Build/BatchFiles/Mac > /dev/null 2>&1
echo "Finishing the app with Xcode..."
dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll -Mode=ApplePostBuildSync -Input="$STEPS/PostBuildSync_PortsOfPlague.json" -XmlConfigCache="$ROOT/Intermediate/Build/XmlConfigCache.bin" < /dev/null >> "$LOG" 2>&1 || { echo "Xcode could not finish the app. See $LOG"; exit 1; }
dotnet Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll -Mode=WriteMetadata -Input="$STEPS/TargetMetadata.json" -Version=2 < /dev/null >> "$LOG" 2>&1 || { echo "The build receipt could not be written. See $LOG"; exit 1; }

echo "Cooking and packaging (a few minutes)..."
rm -rf "$ROOT/Saved/Package"
"$ENGINE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$ROOT/PortsOfPlague.uproject" -platform=Mac -clientconfig=Development -skipbuild -cook -stage -pak -package -archive -archivedirectory="$ROOT/Saved/Package" -nop4 -utf8output -unattended < /dev/null >> "$LOG" 2>&1 || { echo "Packaging failed. See $LOG"; exit 1; }
echo "Done: $ROOT/Saved/Package/Mac/PortsOfPlague.app"

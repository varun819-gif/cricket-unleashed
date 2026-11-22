#!/bin/sh
# Same as sync_core.ps1 (for WSL / Linux / macOS). Run from anywhere.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
mod="$root/unreal/CricketUnleashed/Plugins/CricketCore/Source/CricketCore"
rm -rf "$mod/Public/cricket" "$mod/Private/Core"
mkdir -p "$mod/Public/cricket" "$mod/Private/Core"
cp "$root"/core/include/cricket/*.h "$mod/Public/cricket/"
cp "$root"/core/src/*.cpp "$mod/Private/Core/"
echo "Copied $(ls "$mod/Public/cricket" | wc -l) headers and $(ls "$mod/Private/Core" | wc -l) sources into the CricketCore plugin."

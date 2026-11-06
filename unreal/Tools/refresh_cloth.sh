#!/bin/zsh
# Compile the module, rebind the Chaos cloth on the imported player, then render the proof shots.
# Run detached so an interrupted shell cannot kill it:  nohup unreal/Tools/refresh_cloth.sh NAME > unreal/Saved/Devin/NAME.out 2>&1 &
cd -- "$(dirname -- "$0")/../.."
UE="/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
OUT="$PWD/unreal/Saved/Devin"; mkdir -p "$OUT"
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" RougarouEditor Mac Development -Project="$PWD/unreal/Rougarou.uproject" > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
echo BUILD_OK
# -ExecCmds splits on commas; in the editor the exit command is QUIT_EDITOR (plain Quit is ignored and the editor idles forever).
"$UE" "$PWD/unreal/Rougarou.uproject" -ExecCmds="rougarou.BuildClothing,QUIT_EDITOR" -unattended -nullrhi -nosound -nop4 -noanalytics -abslog="$OUT/clothing.log" > /dev/null 2>&1
echo CLOTH_EXIT=$?
grep -E 'Garment collider|Cloth setup|Cloth bind|Cloth save|LogClothingAsset' "$OUT/clothing.log" | cut -c1-200 | head -12
NOMAT=1 unreal/Tools/look.sh "${1:-look}"
echo ALL_DONE

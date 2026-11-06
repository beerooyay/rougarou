#!/bin/zsh
# Rebuild the module, re-author the gym materials and render the route 0 proof shots into unreal/Saved/Devin/$1
set -e
cd -- "$(dirname -- "$0")/../.."
UE="/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
OUT="$PWD/unreal/Saved/Devin/${1:-look}"; mkdir -p "$OUT"
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" RougarouEditor Mac Development -Project="$PWD/unreal/Rougarou.uproject" > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
if [ -z "$NOMAT" ]; then
"$UE" "$PWD/unreal/Rougarou.uproject" -run=pythonscript -script="$PWD/unreal/Tools/build_gym_materials.py" -unattended -nullrhi -nosound -nop4 -noanalytics -abslog="$OUT/materials.log" > /dev/null 2>&1 || true
grep -E 'Built M_|LogPython: Error|Traceback' "$OUT/materials.log" | head -20
fi
"$UE" "$PWD/unreal/Rougarou.uproject" -game -windowed -ResX=1440 -ResY=900 -route=${ROUTE:-0} -shot -shotdir="$OUT" ${EXTRA} -nop4 -noanalytics -abslog="$OUT/shot.log" > /dev/null 2>&1 || true
grep -E 'LogMaterial: Warning|LogTemp: Error' "$OUT/shot.log" | grep -v UnifiedErrorTest | head -10
ls "$OUT"

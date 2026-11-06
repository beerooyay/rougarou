#!/bin/zsh
# Render the locomotion proof shots (idle/walk/run/turn/stop) into unreal/Saved/Devin/$1. Run detached.
cd -- "$(dirname -- "$0")/../.."
UE="/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
OUT="$PWD/unreal/Saved/Devin/${1:-motion}"; mkdir -p "$OUT"
"$UE" "$PWD/unreal/Rougarou.uproject" -game -windowed -ResX=1440 -ResY=900 -motiontest -shotdir="$OUT" -nop4 -noanalytics -abslog="$OUT/motion.log" >/dev/null 2>&1
ls "$OUT"; echo ALL_DONE

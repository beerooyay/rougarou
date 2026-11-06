#!/bin/zsh
set -e
cd -- "$(dirname -- "$0")"
player_args=(-latestplayer)
for arg in "$@"; do
  case "$arg" in
    -coreybaseline|-blaizeplayer|-blaize2player|-modularplayer|-garmentcandidate) player_args=();;
  esac
done
exec "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PWD/unreal/Rougarou.uproject" -game -windowed -ResX=1440 -ResY=900 -nop4 -noanalytics "${player_args[@]}" "$@"

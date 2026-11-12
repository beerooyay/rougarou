#!/bin/zsh
set -e
cd -- "$(dirname -- "$0")"
# the latest character with the second raised-arm shoulder weight pass; -shoulderv1 goes back to the first
player_args=(-latestplayer -shoulderv2)
for arg in "$@"; do
  case "$arg" in
    -shoulderv1) player_args=(-latestplayer);;
    -coreybaseline|-blaizeplayer|-blaize2player|-modularplayer|-garmentcandidate|-clothplayer) player_args=();;
  esac
done
exec "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PWD/unreal/Rougarou.uproject" -game -windowed -ResX=1440 -ResY=900 -nop4 -noanalytics "${player_args[@]}" "$@"

# Latest character, trackpad controls, and recorded training preview

User-authorized native integration, November 5. `play-unreal.command` now selects
`-latestplayer` unless an explicit older-player flag is passed. The new package
is `/Game/Local/Blaize2ShoulderPlayer`; previous packages remain available.

Source is `movement-lab/training/characters/shoulder-bridge-v1`. The builder
`fit_shoulder_bridge.py` exports its FBX, GLB, textures and material manifest.
Locomotion MUST use `retarget_corey_player.py --align-rest-directions --gym-jog`.
The initial unaligned bake visibly twisted the arms and was replaced, not used.
Import with `import_corey_player.py --destination /Game/Local/Blaize2ShoulderPlayer
--finish-script unreal/Tools/finish_blaize_materials.py --external-materials
--preserve-normals`, supplying the source directory. Verify in the native game.

Trackpad input is handled by a lifetime-scoped Slate input processor. Scroll
GESTURES carry both axes; two-finger swipes look/orbit in walking, climbing and
the reference preview. Option + pointer movement is a fallback. Menu-open events
are left unhandled for normal scrolling. The persisted TrackpadLook setting
can disable this behavior. Left-click hold selection is unchanged. Right-drag
still works in third-person walking. The settings body scrolls to avoid footer
overlap. Missing garment bones now return an invalid compact index before
calling the engine lookup, preventing the prior startup ensure on this rig.

`bake_native_reference.py` bakes frozen reference-003 to `reference_regrip.fbx`.
Import it as animation onto the new package's skeleton using the same animation
import settings as the locomotion clips. `ReferencePreview.cpp` displays this
recorded animation with corresponding box ledges and explicit 90% harness /
not-live-training labeling. It is available in Settings and via
`-referencepreview`; J or the return button restores normal gameplay. It does
not drive ClimbSim, train in-game, or claim a general learned climbing policy.
Source manifest and evaluation are included under Content/Data/TrainingReference.
The underlying run accepted 2/4 practiced placements, 0/4 wider held-out cases.

Validation: native Locomotion, LocomotionAssets and Presentation checks passed.
`-flowtest` verifies trackpad controller deltas, menu pass-through, disable toggle,
walk/climb transitions, first-person targeting and menu pause. This exercises
controller logic, not a physical trackpad. Captures are Saved/TrackpadProof.
`-referencepreview -referenceproof` captures Saved/TrainingReference.png then
exits. Final reference proof has no handled ensure or error; visual review shows
the actual imported character and matching reference pose. Source-generation
and import failures during development were fixed before the final captures.

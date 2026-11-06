# Modular player validation — November 4, 2026

The editable `assets/character/modular/player.blend` contains separate shirt,
shorts, head, arms, legs and shoes on one rig, plus collar and covered torso.
Original head is a disabled alternative. Runtime export combines only active
parts into one skeletal component and one shared outfit simulation.

Latest native validation completed at 08:29 UTC:

- Native module built successfully; Blender master checks passed.
- All eight Rougarou automation suites passed on the final imported asset.
- Covered torso: 352 vertices, zero outside the 16 bind-pose collision capsules.
- Outfit simulation: 419 pinned / 8,140 movable particles, self-collision and CCD.
- Native walking/running/turn/stopping captures completed.
- Menu, first-person transition, pause, route mounting and dismount flow passed.

Proofs are in `Saved/ModularPlayer/NativeContact`, `NativeSkinning`,
`NativeMotion`, `NativeFlow` and `Automation`. The Blender `bend-probe.png`
is a separate static posing diagnostic, not an Unreal cloth result.

Visual review: shoulder inflation is reduced, and extending the covered thighs
removed the tall exposed overlap strip above the shorts in the climbing capture.
Remaining defects are visible: garment edge roughness, skin showing through
parts of the walking shorts and tee/shorts overlap, and folds that still need
better garment construction. Passing automation does not certify garment fit,
posed surface collision, anatomical soft-tissue volume or visual completion.
No performance improvement is claimed from these captures.

The newly downloaded standalone shirt/shorts are candidate replacements, as
documented in `downloaded-garments-review.md`; they have not been rigged or
substituted. Current sources and desktop originals remain preserved. Four
byte-identical loose generated texture duplicates and the generated Blender
backup were removed after checking the packed master and atlas hashes.

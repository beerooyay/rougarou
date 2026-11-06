# Stable player reset — November 4, 2026

Normal native launches now load the preserved `/Game/Local/CoreyPlayer/climber`
CC4 player and its matching locomotion clips. No body, skeleton or clothing
mesh was regenerated during this reset. This replaces the automatic selection
of the modular/auto-rigged character with the existing coherent CC4 baseline.

The default profile has no active Chaos clothing assets. Its eight garment
controls use bounded spring motion, capped at 1 cm for hems, 0.5 cm for sleeves
and 0.7 cm for cuffs. Blended vertex influences reduce actual surface motion
further. The Fabric setting remains available; `-nofabric` is a temporary
skinning-only diagnostic. Web spring limits mirror the same meter values.

The modular and downloaded-outfit experiments are preserved and require
`-modularplayer` or `-garmentcandidate`. The launcher now forwards explicit
arguments. Experimental generators do not replace the default implicitly.

## Verification

- Native module built successfully.
- Complete native automation: 10 successful entries, zero failures. The new
  StablePlayer test checks the default package, absence of Chaos, all eight
  garment controls, and six clips bound to its skeleton. Modular-specific
  checks are gated to their experimental profile.
- Presentation checks enforce the reduced displacement caps under wind.
- Captured walking, running, turning, stopping, deep climbing, first-person,
  menus, pause and dismount with the default profile.
- Reviewed walking/running/deep-climb/first-person images. Legs and clothing
  remain coherent in these captures without the experimental outfit's large
  spikes or broad skin-through-fabric patches.
- Web syntax check and all nine web route checks passed.
- Launcher syntax and `git diff --check` passed.

Proofs: `unreal/Saved/StablePlayer/{Motion,Contact,Flow,Automation}` and logs.
The initial body check used `-nofabric`; final captures use the default Fabric
setting and the reduced spring limits. No interactive game was left running.

This establishes a stable deformation baseline, not finished character art.
Hair styling, the collar, sleeve-edge finish, footwear and further clothing
fit remain visible cosmetic refinement work. These captures do not prove
zero intersection in every possible pose or a benchmarked performance gain.

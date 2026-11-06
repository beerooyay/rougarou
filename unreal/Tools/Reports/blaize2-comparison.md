# Blaize2 comparison — November 5

`/Users/beerooyay/Desktop/blaize2.glb` is a complete skinned character: one
mesh, one skin, 65 Mixamo-named bones including fingers, 38,453 vertices,
49,600 triangles, four influences per vertex, and no embedded animations.
The local source copy is `assets/character/blaize2/source/character.glb`.
Its SHA-256 and import invariants are in `Saved/Blaize2Player/build.json`.

The source is 0.9787495m tall. The comparison applies one uniform conversion
to 1.8m, matching Blaize's existing game scale, to both mesh and skeleton.
No individual joints, vertices, UVs or weights are sculpted/painted. Bone
names are normalized, and three unweighted eye/outsole landmarks are added.
Blender reconstructs bone matrices on edit-mode exit; the measured roundoff
is recorded and bounded to 1e-4 matrix components.

## Reproduce

1. Run `build_blaize_player.py -- --source-dir /absolute/assets/character/blaize2
   --proof-dir /absolute/unreal/Saved/Blaize2Player --height-m 1.8` in Blender.
2. Run `retarget_corey_player.py -- --source-dir /absolute/assets/character/blaize2
   --align-rest-directions --gym-jog` in Blender.
3. Run `import_corey_player.py --source-dir /absolute/assets/character/blaize2
   --destination /Game/Local/Blaize2Player --finish-script
   /absolute/unreal/Tools/finish_blaize_materials.py --external-materials
   --preserve-normals`.
4. Launch with `-blaize2player`. `-blaizeplayer` still selects the earlier
   character. The default selection is unchanged.

The candidate uses the same authored jogging cycle for the jog/run slots but
omits the earlier eight-degree torso correction and all shirt weight edits.
Both players still use the same climbing IK and anatomical limits. This is
an isolated integration comparison, not a claim of zero runtime posing.

## Diagnosis

There is no neural deformation network in this active character path. The
runtime uses sampled animation clips, FK/IK, a loaded joint-limit profile,
clavicle elevation, chest extension, head look, and bounded body/contact fitting.
The training audit evaluates that profile; it does not retrain the mesh live.

The source rigs differ materially. At equal overall height, Blaize2's upper-arm
root is about 3cm lower, while its head joint is about 4cm higher. Its pelvis and
spine joints also differ; it is not merely the old rig with different labels.

A texture-filtered sample of rear shirt fabric (spatial bounds in the audit)
has about 31% average thigh influence in the original rig and 49% in Blaize2.
Raised thighs can therefore pull the two sides of the hem independently into
that U shape. These are regional averages, not every shirt vertex. The new
rig is not automatically a clothing fix. See `Saved/Blaize2Player/weight-audit.json`.

Existing corrections are deterministic, not repeatedly accumulated: the
shirt script always reloads `baseline-before-polish/player.blend`, and the
retarget script resets joint location/scale before baking each frame. But the
combination of rest-direction alignment, an eight-degree spine correction,
climbing chest extension and scapular elevation can still compound visually.
The candidate deliberately omits the optional posture/garment corrections so
its native captures can expose that difference before adding another tweak.

## Native review result

`Untouched/Contact/shot.png` and `Untouched/Motion/*.png` were captured in Unreal.
The jog is usable, but the wall pose has awkward leg folding and retains the
split/U-shaped lower shirt. Automated result: 11/12 pass; `HumanRig` fails
opening contacts, including outsole gaps of 3.30cm and 11.93cm. Limits and
thresholds were not weakened. The candidate is not promoted to the default.

The current player remains intact. Select either with `-blaizeplayer` or
`-blaize2player`; no extra shirt correction or eight-degree spine offset is
applied to Blaize2. The game scale conversion is explicit, not a proportion fix.
Run `audit_blaize_variants.py` in Blender to reproduce the weight comparison.

Next corrective work should calibrate the chosen rig's neutral pose, hand/sole
landmarks and joint frames against native contacts, then repair garment-only
weights in the actual source topology. Adding a neural model or more universal
head/shoulder offsets would not resolve thigh-driven fabric deformation.

Original-player regression after adding the comparison selector: 12/12 native
tests passed (`OriginalRegression/index.json`). Both test applications exited.

## Rejected shoulder-only review (historical)

A five-degree clavicle depression is defined in
`Content/Data/PlayerFits/Blaize2Player.json`. Retarget with the previous flags
plus `--fit-profile /absolute/unreal/Content/Data/PlayerFits/Blaize2Player.json`.
The climbing rig loads that same setting based on the selected mesh package;
other characters without a fit file retain zero correction. The adjustment is
applied before arm IK, not after contact fitting. It rotates clavicles without
changing head scale, bind positions, mesh geometry, clothing weights or joint
limits. `Untouched` retains the native before captures; `Shoulders` is the new
review. Omitting the fit profile from the bake and setting its value to zero
restores the earlier pose.

Shoulder review validation: native build and animation import succeeded.
Jog and wall captures were visually reviewed. Automation remains 11/12, with
the existing HumanRig contact failure; the two measured opening sole gaps
remain 3.30cm and 11.93cm. No new failing test was introduced, and fewer
opening-contact assertions fail, but this is not a contact fix or promotion.
The test processes exited.

## Current neck/trap and head revision

The user rejected the shoulder-only trial. Its optional runtime and bake code
has been removed, and all six clips are rebaked without clavicle depression. The current shape
profile uses head scale 0.96, neck lowering 2cm and a smoothly masked trapezius
lowering of up to 2.5cm. It follows the source rig's neck, head, chest and shoulder
landmarks rather than fixed world-height bands. Outer shoulder/limb pivots,
UVs and skin weights are retained; neck/head pivots follow the fitted surface.
Normals are transformed using the deformation Jacobian to preserve smooth shading.

Current reproduction adds `--shape-profile
/absolute/unreal/Content/Data/PlayerFits/Blaize2Player.json` to the source build; the animation bake uses `--align-rest-directions --gym-jog`. The build
always reloads the immutable GLB. The explicit localized change leaves the
lower body untouched; it does not rescale the character back up afterward.
Reviewed fitted height is 177.58cm, checked on import against that profile.
`NeckHead/build.json` records changed vertices, displacement, and invariants;
`NeckHead/Contact` and `NeckHead/Motion` contain the new native proofs.

Final neck/head validation after removing the shoulder code: 11/12 native
tests pass; the pre-existing HumanRig opening-contact failure remains. Geometry
checks pass at the intentional fitted height. Native jog/wall captures were
reviewed, Python files compile, and git diff whitespace checks pass.

## Torso ease pass

Trap mask maximum increased from 2.5cm to 3.2cm; head and neck settings retained.
Front chest moves inward up to 0.6cm and the front waist outward up to 0.8cm,
with smooth fades at the collar, sides and hem. Skin weights are unchanged.
Native jog and wall captures are in TorsoEase. Toe articulation is NOT yet
implemented: the solver currently fits a rigid Foot-parented sole landmark.
Contact-aware MTP articulation and exhaustive clothing-intersection validation
remain outstanding. Do not claim this pass completes those requests.

## Back/hem correction — current, November 5 afternoon

Rebuilt from immutable source using neck_lowering_cm=4, trap_lowering_cm=5,
head_scale=.96 and the previous chest/waist ease. This replaces the previous
profile; it does not add a runtime offset. Spine/pelvis/outer shoulder and limb
bind positions are unchanged. The neck/head pivots follow the fitted surface.
The central upper-back peak is flatter in the matched raised-arm review.
Expected height is now 175.58cm, explicitly checked by native geometry tests.

`release_shirt_hem` runs `fit_blaize_garment.py` during fresh source builds.
Texture-derived fabric weights are smoothed spatially to avoid sharp boundaries.
Thigh influence transfers to pelvis/lower spine; 6,130 vertices change with
mean 0.374 thigh weight removed. The soft boundary includes adjacent shorts
on this fused mesh. Geometry, UVs, topology and maximum four influences remain
unchanged by this weight step. This is skinning correction, NOT cloth simulation.
The first hard-mask trial produced spikes and was rejected before native import.

Proofs: `Saved/Blaize2Player/BackFit/{before,candidate}-wall.png` use identical
posed bone directions and camera. Native `Contact/shot.png`, `Motion/*.png`,
and `Automation/index.json` were generated after import. 11 tests pass; HumanRig
still fails opening contacts, including the same 3.30/11.93cm outsole gaps.
No claim of exhaustive clothing collision validation. Native wall and mid-jog
captures reviewed; current sample shows no hard-mask hem spikes.

Previous profile, blend, clips and native package are preserved under
`BackFit/before`. Training GLB and human_motion.json hashes remain unchanged
(`training-inputs-before.json`). The new `BackFit/player.glb` is a separate
rigged export for deliberate future lab adoption, not an automatic replacement
of the frozen training snapshot. Active native selection is still `-blaize2player`.

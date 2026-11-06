# Blaize2 comparison

`-blaize2player` selects the isolated Mixamo-rigged export with the current
neck/trap and 4% head-size fit. The rejected shoulder rotation is removed.
It is imported and motion-tested, but is not promoted: its opening contact check fails. See
`Reports/blaize2-comparison.md` for source validation, native captures and the
pose/garment diagnosis. `-blaizeplayer` retains the earlier reviewed character.

# Reviewed Blaize player

Use `-blaizeplayer` for the user's complete Tripo / ActionForge character.
Its original appearance is preserved; the November 5 pass refines jog/run
posture and the shirt's upper-back/underarm weights; the hem uses its original skinning. See
`Reports/blaize-player.md` for the source archive, preserved baseline,
reproduction commands, native captures and remaining drape limitations.

# Default personal player

Normal launches select `/Game/Local/PersonalPlayer/climber` after verifying its
six matching clips, eye/sole landmarks and absence of Chaos bindings. The
preserved `/Game/Local/CoreyPlayer/climber` remains the fallback; pass
`-coreybaseline` for an explicit comparison. Earlier experiment flags retain
their separate packages.

The personal master is `assets/character/personal-player/player.blend`. Its
body is the exact native CC4 snapshot, uniformly scaled to 170.18 cm. The user's
`Head_Face` replaces every old face/eye/hair surface. The downloaded shorts use
transferred CC4 weights. The tee preserves the reviewed CC4 topology and body
weights: the downloaded tee failed raised-arm visual acceptance. Eight edge controls allow bounded secondary motion:
3 cm hem, 1.5 cm sleeve, 2.1 cm cuff. There is no full fabric/self-collision
solver. Web/default-Corey limits remain 1 / 0.5 / 0.7 cm.

Reproduction:

1. `export_stable_source.py` runs through a regular renderer-enabled editor,
   exporting the original package without modifying it. The FBX exporter needs
   a render mesh; do not use `-nullrhi` for this export. Keep `source.blend` and
   `source-clips/` as immutable inputs.
2. Blender runs `build_personal_player.py`, then `retarget_corey_player.py --
   --source-dir /absolute/path/to/assets/character/personal-player`.
3. Run `import_corey_player.py --source-dir /absolute/path/to/assets/character/personal-player
   --destination /Game/Local/PersonalPlayer --finish-script
   /absolute/path/to/unreal/Tools/finish_personal_materials.py --external-materials`.
4. Rebuild, run native automation and review motion/contact/first-person captures.

`Saved/PersonalPlayer/` contains native review captures and automation results.
Read `Reports/personal-player.md` for the evidence and art limitations.

# Experimental modular player

The `-modularplayer` flag selects `/Game/Local/ModularPlayer/climber` once its
shared outfit cloth asset and six matching clips are present. The editable master is
`assets/character/modular/player.blend`: shirt, shorts, head, left/right arms,
legs and shoes remain separate objects on one rig. `Collar` and `Body_Underlay`
finish the neckline and provide covered skin beneath moving fabric.
`Head_Original` is a disabled variant; only the fitted `Head_Face` is exported.

Canonical local source files live under `assets/character/modular/`:
`source.blend`, the untouched recovered `source.glb`, and `head-source.fbx`.
Desktop originals are never edited. Generated files and likeness assets stay
ignored by Git. The web renderer still uses its earlier local character asset.

Build in order:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 --python unreal/Tools/build_modular_player.py
/Applications/Blender.app/Contents/MacOS/Blender -b assets/character/modular/player.blend --python-exit-code 1 --python unreal/Tools/check_modular_player.py
/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 --python unreal/Tools/retarget_corey_player.py -- --source-dir "$PWD/assets/character/modular"
```

Compile the native module, then import the geometry, matching animation clips,
and shared texture atlases:

```sh
python3 unreal/Tools/import_corey_player.py --source-dir "$PWD/assets/character/modular" --destination /Game/Local/ModularPlayer --finish-script "$PWD/unreal/Tools/finish_modular_materials.py" --external-materials
```

In the editor console, run
`rougarou.BuildClothing /Game/Local/ModularPlayer/climber.climber` and save.
The builder regenerates `Content/Data/modular-player-collision.json`; that
single definition fits the covered skin foundation and the native collision
shapes. Rebind cloth after rebuilding geometry or changing this definition.

The runtime export combines the pieces into one skeletal mesh. Shirt and
shorts share one cloth section with self-collision so their separate surfaces
can interact. Vertex alpha carries mobility for pinning; the `FabricRegion`
UV channel identifies garment colors independently of cloth deformation.
Sole-contact helper bones are actual shoe-surface vertices, used by climbing
IK. Footwear is fitted to a 28.2 cm exterior length, an approximate US men's 9.
`PlayerEyes` supplies the eye position independently of the skull's rotation
pivot. Covered thigh geometry continues beneath the shorts; skin at garment
openings follows the same upper-limb frame as the cuff before blending toward
the next joint. The tee uses a continuous chest-to-sleeve weight field.

Run the complete `Rougarou` native automation suite, including
`Rougarou.ModularPlayer`, and the `-motiontest`, `-flowtest`, and climbing
`-shot -contactshot` captures. The Blender master check verifies dimensions,
weights, separate openings, and outsole landmarks; the native modular check
verifies shared cloth, pinning, collision bindings and the skin foundation.
Render inspection is still required to assess appearance and moving contact.
Use `-nofabric` with the native capture flags to inspect actual skinning without
cloth; this diagnostic does not change saved player preferences.

Additional desktop garment candidates are inventoried in
`Reports/downloaded-garments-review.md`. They have not replaced the active
garments and need their own fitting and deformation review first.

## Downloaded garment candidate

`fit_downloaded_garments.py` rigs local copies of the downloaded tee and shorts
to the unchanged modular player skeleton. It preserves the current master,
fits a roomier tee and forest-green shorts, preserves the tee's separate shell
surfaces during reduction, and removes extra interior/decorative geometry.
The rib collar and waistband are anchored explicitly. The detailed render mesh
is not used as the physical simulation surface.
The visible garments are driven by separate manifold shirt/shorts simulation
proxies (about 3,800 triangles combined). These export as `Outfit_Proxy`; the
cloth builder uses that section for physical particles and binds its motion
to `Outfit_Fabric`. The proxy section is disabled for rendering and shadows.

The candidate lives in `assets/character/garment-candidate/player.blend` and
`/Game/Local/ModularPlayerCandidate`. Only `-garmentcandidate` selects this
package; the normal player remains available. Rebuild and check with:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 --python unreal/Tools/fit_downloaded_garments.py
/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 --python unreal/Tools/check_garment_candidate.py
python3 unreal/Tools/import_corey_player.py --source-dir "$PWD/assets/character/garment-candidate" --destination /Game/Local/ModularPlayerCandidate --finish-script "$PWD/unreal/Tools/finish_modular_materials.py" --external-materials
```

Bind with `rougarou.BuildClothing /Game/Local/ModularPlayerCandidate/climber.climber`.
Use `-garmentcandidate` for the native automation and screenshot flows too.
The original clothes remain hidden in the candidate master as comparisons;
they are excluded from the export. Static pose renders do not certify fit
through movement. Review native captures before promoting the candidate.
The runtime export omits deeply covered thigh faces while retaining overlap
inside the shorts cuffs. The editable body and collision capsules remain
complete. The garment import passes its source/destination to the material
finisher explicitly so a candidate import cannot finish the original package.

# Corey fallback player

This older sewn garment experiment lives in `/Game/Local/CoreyCloth`. It is not the normal launch profile; legacy experimental selection may fall back to it.
This uses Corey's CC4 body from commit `3d30a187e736c28c76b58840d0d3248573175737`,
retaining its original body weights, hierarchy and twist/share joints. A T-pose
bind is baked through those weights to match the local animation library; bone
aliases connect the existing humanoid solvers. The previous scan player remains
under `/Game/Local/Player`.

The local build adds slightly paler skin, a relaxed ivory crew-neck tee,
forest green shorts, a close light-brown buzz cut and climbing shoes.
The native staged mesh uses Chaos cloth on the shirt and shorts, built by
`ClothingSetup.cpp`. `GarmentPhysics.h` supplies eight coarse hem, sleeve and
cuff masses only when native cloth is absent; the web fallback mirrors those
masses in `web/garment-physics.js`. Cloth bindings alone do not establish that
the garment fit, bend deformation, or body volume is correct.

The sewn garment and buzz-fiber build stages in `/Game/Local/CoreyCloth`. It uses the preserved source at `assets/character/corey/source.glb`:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 --python unreal/Tools/build_corey_player.py
/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 --python unreal/Tools/retarget_corey_player.py
python3 unreal/Tools/import_corey_player.py
```

Build the native module before importing. After import, run `rougarou.BuildClothing` in the Unreal editor console to bind the two cloth sections. The runtime uses the staged mesh only when native cloth and all six matching clips are present. Use `-route=0 -shot -contactshot` to capture the opening grip and shoe placement at an oblique angle.
The importer also applies explicit cotton, hair and skin roughness/specular
finishes. FBX textures use distinct diffuse and alpha nodes and PNG payloads;
sharing one RGBA texture object between both sockets triggers the engine's
headless FBX warning-panel crash. The generated files stay local. The web build uses its own validated copy at `web/models/corey-player.glb`.

# Previous scan pipeline

The local likeness uses a clean T-pose deform skeleton, normalized skin weights
with at most four influences, and parent-relative rotation retargeting. No limb
translations or scales are baked into locomotion. Climbing IK preserves bind
joint offsets and clamps unreachable targets instead of stretching the skin.

`Locomotion.h` supplies acceleration, braking, and velocity-driven body facing.
The camera orbits independently; WASD moves relative to it, Shift runs, and
right-drag looks. Releasing movement keeps the body's last facing. Collision
samples the feet, hips and head against the gym wall profiles, arch and blockout.
`LocomotionAnim` blends phase-aligned idle/walk/jog/run samples plus brief authored
start/stop weight shifts. The gait speed comes from actual collision-resolved
movement, so pushing into a wall stops the feet.

Rebuild local assets (Blender and the Rawah Mixamo library must be installed):

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --python unreal/Tools/rebuild_character.py
python3 unreal/Tools/import_character.py
```

Build the editor module before importing. Do not run asset import while the
module is linking. Personal likeness, imported clips and generated FBX/Blender
files stay under ignored `assets/` and `unreal/Content/Local/`.

Run `Rougarou.Locomotion`, `Rougarou.LocomotionAssets`, `Rougarou.Rig` and
`Rougarou.ClimbSim` automation tests. `-game -motiontest -shotdir=...` exercises
idle, walk, run, a turn and stopping, writes screenshots, and exits. `-game
-route=0 -shot` checks the climbing pose; `-lift` also exercises reaching.

This is a rotation-only deform rig and native animation graph. Ground foot IK,
full-body climbing IK, and authored turn-in-place clips are future refinements.

## Player polish and first person

The previous likeness lives in `/Game/Local/Player` (the previous local mesh
is retained). The source bake fits lower, wider shoulders, smooths the scan,
reduces the control mesh to 45k faces, and subdivides once for the silhouette.
Skin, cotton and shorts have separate materials. Eight garment bones drive
bounded, acceleration-driven secondary motion on the hem, sleeves and cuffs.
The active animation instance applies `GarmentPhysics.h` to both walking and climbing when the mesh has no native cloth asset.

First person ports Corey's web work from
[cefc14d](https://github.com/ceeboozwah/rougarou/commit/cefc14d6fcfd438305062b45ede2829359e2313e).
The web integration preserves the local likeness renderer and uses Corey's
procedural body in first person. Unreal uses the same aimed-hold selection,
R/L then A/L limb picks, latched arrow-key hips, and Space to center. V changes
view; Escape frees the first-person cursor for HUD buttons. The native view
uses an 80-degree vertical field of view, hides the head, keeps the eye clear
of the wall, and opens toward the next hold. The player remains visible below
the camera.

M opens the native problems/settings/help menu. Settings and sends persist in
`Saved/Player.ini`; the menu freezes the climbing model and walking animation.
`-game -flowtest -shotdir=...` checks menu entry, route selection, first-person
transition, pause, and dismount, capturing all three menu tabs and the view.
Its preferences use `Saved/PolishProof/TestPlayer.ini` to isolate test state.

`-game -uishot -uitab=1` captures settings (`0` problems, `2` help).
The automation suite also includes `Rougarou.Presentation` for fabric settling,
teleport safety, eye clearance and aiming.

The final CC4 shoe contact frame uses the shoe's actual 25 cm ankle-to-toe
reach and a 6 cm contact offset; native goals and the fitted web skin share it.
The retained CC4 validation report is `Reports/corey-motion-validation.json`.
The native verification covers all nine opening stances, 91 sampled climbing
poses, locomotion assets and motion, garment response/collision, and menu/view
flow. Extreme poses remain bounded by the anatomical solver rather than widening
its limits to force a reach.

## Deformation diagnostics

The [character review](Reports/character-deformation-review.md) records the
November 3 audit, current limitations, and Unreal 5.8 cloth references. Earlier
motion/contact verification does not certify later garment builds.

Audit a generated Blender file without saving it or changing imported assets:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b assets/character/corey/player.blend --python-exit-code 1 --python unreal/Tools/audit_character.py -- --output /tmp/character-audit.json
```

For an isolated build, set `ROUGAROU_CHARACTER_OUTPUT` to a candidate directory
when running `build_corey_player.py`. The source still comes from
`assets/character/corey/source.blend`; the active generated files are untouched.
Candidate output is diagnostic until fit, skinning, native cloth, and the
in-game pose sweep have been reviewed.

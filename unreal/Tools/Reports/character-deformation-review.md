# Character deformation review — November 3, 2026

The character is not ready for anatomical or garment-quality sign-off. The
current problems include garment construction, skinning, and cloth constraints;
they cannot all be corrected by increasing solver quality or training more
joint limits. Preserve the improved gym and Corey's base character while
addressing those layers independently.

## What was inspected

- The three supplied screenshots, the current source scripts, and the newer
  `assets/character/corey/player.blend` generated at 22:38 local time.
- Local Unreal **5.8.4**, verified from the installed engine's `Build.version`.
- `build_corey_player.py`, `drape_tee.py`, `author_garments.py`,
  `ClothingSetup.cpp`, and the procedural pose construction in `HumanRig.cpp`.
- A read-only Blender pose sweep, recorded in
  [the baseline report](character-deformation-audit.json). Its source SHA-256
  identifies the exact generated file. These are skinning diagnostics before
  Chaos cloth, game IK, corrective shapes, or collision.

## Confirmed defects and the candidate correction

The sleeve classification used distance along a lowered arm to decide which
vertices should follow that arm. Lower torso vertices also project far along
that axis. The initial pattern swing likewise acted on lower side torso
vertices. Together these could pull the lower shirt sideways when returning
the drape to the T-pose bind. The baseline preview visibly flared into a skirt.

The candidate restricts sleeve swing to the upper pattern and carries an
explicit sleeve-panel attribute through cloth settling and subdivision into
weight transfer. The lower shirt is no longer classified as a sleeve by its
posed position alone.

Separately, both the garment weight transfer and export pruning removed Blender
vertex group entries through live references. A reproduction with eight groups
left seven behind after the attempted removal. Snapshotting integer group
indices removes all eight correctly. This was corrected in the active Corey
builder, drape stage, and the older scan builder. The Corey exporter now rejects
invalid influence counts or non-normalized weights.

The exact corrected pruning loops passed Blender regression probes. A complete
isolated build succeeded at `unreal/Saved/CharacterReview/candidate/`; neither
the active generated character nor Unreal content was replaced. The build still
targets 1.7018 m barefoot height and a 0.282 m exterior shoe. An exterior shoe
length is an approximation, not a verified commercial size fit.

| Diagnostic | Existing generated asset | Corrected candidate |
| --- | ---: | ---: |
| Tee vertices exceeding the intended four influences | 123 | 0 |
| Shorts vertices exceeding four influences | 73 | 0 |
| Tee maximum weight sum | 1.085946 | approximately 1 |
| Overhead tee edges stretched beyond 1.5× bind length | 3,233 | 352 |
| Arms-down tee edges stretched beyond 1.5× bind length | 269 | 66 |
| High-step shorts edges stretched beyond 1.5× bind length | 579 | 582 |

The candidate report is [recorded separately](character-deformation-candidate.json).
An overhead test rotates the arms 65 degrees above horizontal. The high step
flexes the left hip 110 degrees forward and bends its knee 120 degrees.
These are diagnostic poses, not recordings of the game's solved climbing pose.
Counts depend on topology and are not a universal material acceptance threshold.
The shorts result demonstrates that repairing pruning did not repair their cut.

Workbench review images are saved alongside the candidate: `arms-down.png`,
`back-reach.png`, and `high-step.png`. The tee's flare is improved, but folds
remain baked into the hem; the overhead neckline and underarm region still
distort. The shorts still trace the buttocks and pinch at the raised hip.
These images do not include running Chaos simulation.

## Garment construction and cloth

The tee uses procedural T-shaped front/back panels rather than separately
fitted set-in sleeves. The shorts are extracted from body topology and expanded,
so their seat and crotch inherit the body's surface instead of a trouser pattern.
For the next authored garments, fit an actual armscye/sleeve seam, a continuous
ribbed crew-neck band, regular cuff/hem boundaries, and a shorts rise/crotch with
enough ease for the deep squat and high-step poses. Fabric should bridge the
body where appropriate rather than reproduce the gluteal cleft. This is an
authoring recommendation based on the inspected mesh and pose failures.

Epic's current [Chaos Cloth 5.8 update](https://dev.epicgames.com/community/learning/tutorials/Wb2V/unreal-engine-chaos-cloth-updates-5-8)
identifies the unified Dataflow Cloth Editor and Cloth Asset as production
ready; the old Panel Editor is deprecated and Outfit Asset remains beta.
This is the appropriate current authoring workflow to evaluate for the next
garment revision. The existing skeletal-mesh cloth path still runs Chaos;
changing editors alone will not correct an invalid rest shape.

Use a clean, coarse simulation surface to drive the detailed render surface,
rather than simulating every render detail. Epic's
[workflow overview](https://dev.epicgames.com/documentation/unreal-engine/panel-cloth-editor-overview)
describes separate simulation/render meshes. Its older editor terminology
should be read alongside the 5.8 update above. The
[weight transfer reference](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosClothAssetDataflowNodes/FChaosClothAssetTransferSkinWeig-/Evaluate)
recommends transferring render weights from the simulation mesh and explains
how creases around armpits can produce inconsistent transfers.

`ClothingSetup.cpp` currently pins a broad upper region using a height-based
Max Distance mask, releases lower fabric only a few centimeters, and applies
uniform animation drive. That setup is a plausible contributor to the hard
horizontal back band and stiff shoulders; proving causation requires comparing
the mask and simulation in Unreal. Epic specifies that
[zero Max Distance makes particles kinematic](https://dev.epicgames.com/documentation/unreal-engine/node-reference/Dataflow/SimulationMaxDistanceConfig?lang=en-US).
Fit and visualize the collar/shoulder anchors and gradual release instead of
using a broad horizontal boundary. Inspect body-fit collision in reaching and
squatting poses; overlapping shoulder/torso primitives can inflate fabric.

No explicit self-collision or backstop setup was found in this native builder.
Evaluate those selectively after the fit is corrected. Epic documents
[backstop distance/radius](https://dev.epicgames.com/documentation/unreal-engine/node-reference/Dataflow/SimulationBackstopConfig?lang=en-US)
and [collision thickness/CCD](https://dev.epicgames.com/documentation/unreal-engine/node-reference/Dataflow/SimulationCollisionConfig?lang=en-US).
CCD has a cost and should be justified by observed missed collisions.
Avoid adding thickness everywhere to hide incorrect body or garment geometry.

The Blender settling stage uses `mass=.25`. Blender exposes this as
[Vertex Mass](https://docs.blender.org/manual/en/5.3/physics/cloth/settings/physical_properties.html),
not a documented total-shirt mass. Calibrate it with the mesh resolution and
material mass before treating the baked folds as physically representative.

## Body proportions, joints, and volume

The candidate retains 109 CC4 bones and their bind offsets. Measured joint-center
distances in the normalized source are: upper arm 30.1 cm, forearm 23.5 cm,
thigh 42.7 cm, shin 43.2 cm, shoulder-joint separation 35.7 cm, and hip-joint
separation 17.3 cm. Joint-center separation is not external body width. These
measurements do not justify tearing out the skeleton, and do not establish that
every joint center or soft-tissue shape is correct.

The rendered body and garments have no corrective shape keys. The climbing
solver limits rotations and includes an elevation-dependent clavicle lift; it
does not implement a full scapula sliding/rotating on the rib cage or a
pose-dependent flesh-volume correction. Retaining twist/share bones does not
prove that every retained helper is correctly driven by procedural climbing.

A [primary scapulothoracic modeling study](https://pmc.ncbi.nlm.nih.gov/articles/PMC4712143/)
describes scapular movement using elevation, abduction, upward rotation, and
internal rotation on a thoracic surface. A single arm-elevation multiplier is
therefore an approximation. For this game, an authored shoulder/scapular
response plus targeted corrective shapes is a practical candidate; full
musculoskeletal simulation is not required merely to improve the silhouette.

Inspect the full uncropped body before garment masking. The current render
mesh removes covered torso/upper-leg faces, so it cannot establish closed
body-volume preservation or distinguish all apparent clothing bulges from
actual underlying tissue. Check the acromion/deltoid, armpit, pelvis/seat,
inner thigh, elbows, and knees with the clothing hidden, then review them
together. The garment must accommodate the corrected body, not compensate for
body collapse with oversized collision spheres.

Author corrective shapes for shoulder elevation/protraction, hip flexion with
abduction, and deep elbow/knee bends. Validate them before adding secondary
soft-tissue motion. Unreal's
[Pose Driver](https://dev.epicgames.com/documentation/unreal-engine/pose-driver-in-unreal-engine)
can drive poses or curves from bone motion, though Epic currently labels it
experimental. A small deterministic corrective implementation is another
option; the authoring result matters more than the choice of driver.

Corey's learned movement limits describe allowed motion, not the desired shape
of flesh or fabric in each pose. Epic's
[ML Deformer framework](https://dev.epicgames.com/documentation/unreal-engine/ml-deformer-framework-in-unreal-engine)
needs a skeletal mesh, a pose animation, and matching desired deformed geometry
cache. More joint-limit training cannot substitute for that geometry. Consider
training only after a good target deformation set exists.

## Hair and performance

The generated fibers are already approximately 1.2–2.3 mm long, but sit over an
opaque dark-brown scalp shell with a hard hairline. The native material finish
also explicitly reapplies that dark scalp color. Shortening fibers alone will
not remove the cap appearance. Revise both the scalp treatment and the import
finish together, showing skin through the stubble with a tapered hairline and
the requested lighter brown.

The hair has 188,664 fiber vertices in a 221,885-vertex candidate: approximately
85% of the character's vertices. This is a strong reduction/LOD opportunity,
not a measured frame-time result. Keep density and silhouette appropriate to a
buzz cut and profile the actual native character before reporting speed gains.

## Required visual validation before promotion

Review front/back/side at rest, walking/running, both arms overhead, one-arm
reach, shoulder crossing, deep squat, high step, hip abduction, and bent
elbow/knee. Check skin-only, garment skinning, collision overlays, and settled
Chaos cloth in that order. Watch transitions, not only still frames. Inspect
collar continuity, armpit seams, joint volume, crotch stretch, hem overlap,
body penetration, and hand/shoe contact separately.

This turn passed Python compilation, Blender pruning probes, the full isolated
character build, and the two read-only audit runs. No native asset import,
Unreal cloth playback, frame-time measurement, or anatomical volume acceptance
was performed. Existing game assets were intentionally not promoted from this
partially corrected candidate.

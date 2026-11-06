# Personal player review — 2026-11-04

The active package is `/Game/Local/PersonalPlayer`. Original `/Game/Local/CoreyPlayer`
assets remain unchanged and can be selected with `-coreybaseline`. Normal loading
requires six matching clips, measured eye/outsole landmarks and no Chaos binding.

## Delivered

- The user's `Head_Face` replaces the old head, separate eyeballs, lashes, teeth
  and hair cap. Its own diffuse/normal/roughness maps and buzz cut are retained.
  The scan's shoulder skirt is trimmed and a neck bridge fits the CC4 body.
- The native CC4 body and weights are preserved under uniform scaling to
  170.18 cm (5 ft 7 in). The editable parts remain separate in `player.blend`;
  the runtime uses one skinned component and six matching locomotion clips.
- Footwear has a heel cup, instep opening, arch and rounded toe box. Exterior
  length is 28.2 cm, an approximate US men's 9 shoe target, not a sizing standard.
  Each measured outsole marker coincides with actual rubber geometry. Climbing
  uses these markers and the sampled resin target, not an ankle as contact proof.
- Downloaded LP shorts are widened, shortened by cutting the lower panels,
  coloured forest green and bound to the preserved rig.
- The stable CC4 tee remains. Hem clearance and free-edge influence weights
  increase; controlled motion is bounded to 3 cm at hem controls, 1.5 cm at
  sleeves and 2.1 cm at cuffs. Settings can turn it off. These are control limits;
  blended vertices move less. Original Corey/web limits are unchanged.

## Clothing decision and limits

The downloaded tee did not pass visual review with raised arms. Relaxed-pose
fitting and automatic weight transfer still produced stretched armpit panels,
so it is excluded from the active pipeline. Increasing simulation complexity
would not repair those bind/topology errors. The active tee preserves the
reviewed topology and skinning. A full replacement still needs a clean garment
fitted and weighted for this body, plus a separately validated simulation mesh.

This is bounded bone-based secondary movement, not full fabric or garment
self-collision. Collar/cuff finish, the neck texture transition and realistic
folds remain character-art limitations. Passing automation does not certify
visual realism or every possible climbing contact.

## Verification

The final native build succeeded. All **11 automation tests passed, 0 failed**
on the normal default profile after the final asset import. Motion, contact and
first-person/menu captures were regenerated and reviewed. The app exited.

The evidence is under `unreal/Saved/PersonalPlayer`:

- `Motion`: idle, walking, running, turning and stopping.
- `Contact/shot.png`: the deep opening pose on slab dance.
- `Flow`: menu/settings/help/first-person and player views.
- `Automation/index.json`: the full native suite, including original route
  solvability, anatomical limits, contact fitting, stable clip bindings, the
  170.18 cm geometry check, outsole-to-rubber coincidence and bounded motion.

The reference exporter needs a renderer-enabled editor; `-nullrhi` cannot supply
its CPU-skinned render mesh. Native review captures use `-windowed`, avoiding a
macOS fullscreen transition hang. Temporary synchronous shader compilation was
rejected after an engine assertion; normal shader compilation remains enabled.

## Relevant engine guidance

[Epic's AnimDynamics documentation](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-animdynamics-in-unreal-engine)
describes bone-based secondary motion. The existing bounded spring controls are
used here; no AnimDynamics or third-party plugin is claimed to be installed.
[Epic's clothing tool](https://dev.epicgames.com/documentation/en-us/unreal-engine/clothing-tool-in-unreal-engine)
and [panel cloth overview](https://dev.epicgames.com/documentation/unreal-engine/panel-cloth-editor-overview?lang=en-US)
describe the authored mesh, weights and collision workflow needed for a future
full-fabric replacement.

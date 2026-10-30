# rougarou rock lounge: unreal plan

## the feel we are chasing

The core idea: what you're doing is stupidly simple (put a hand here, a foot there), but it's hard to do well because your whole body is balancing. Think balancing a broom on your hand, QWOP, Trials, Flappy Bird. The fun lives in weight, angles and tension, not in button combos. Every rule should be one real climbers would recognise.

- **weight is the game.** Every contact carries load. A hold fails when it's pulled the wrong way or too hard, not on a timer.
- **the body is honest.** Real limb lengths. Hands pull and never shove. Legs push until straight, and can't stand you up past your own hands. A foot can swing free and you put it back. (All already in the sim.)
- **reading is a puzzle.** Hold type plus pull direction plus where your hips are: one glance tells you the move. Wrong-way loads slip.
- **a focused look at the hold, like aim-down-sights.** Third person while you read the route. While a limb is selected, the camera eases into a tight shot of the target hold and hand, the way ADS punches in. Fingers shape to the hold (crimp, open hand, pinch, sloper) as you commit.

## what's already built (unity, the spec)

- three routes (vertical, overhang, traverse) that a greedy solver proves are climbable at real human proportions
- four-contact physics with hold types, pull directions, smears, slips and falls
- a 1.85m mixamo rig with two-bone IK, reset to its true T-pose each frame

## unreal architecture

- `ClimbSim` is a plain C++ model in the wall plane, deterministic at 60 Hz, with no engine physics. It's the single source of truth for gameplay and tests.
- `AClimberActor` steps the sim and draws debug shapes. Next it drives a skeletal mesh.
- **body:** a MetaHuman made from the blaize scan (Mesh to MetaHuman), with its skeleton lengths fed into `FClimbBody` so the sim and the visuals can never disagree.
- **pose:** Control Rig with the Full Body IK solver. Effectors on both hands and feet, the pelvis taken from the sim, and the spine, shoulders and neck solved together. That's what the unity two-bone setup could never do.
- **hands:** a per-hold-type finger pose, blended in Control Rig on contact.
- **input:** Enhanced Input with one action set: select limb, aim, commit, shift hips, release, camera. Mapped to mouse/trackpad, gamepad (sticks aim, triggers commit) and touch (tap a limb, drag to a hold).

## milestones

1. **parity.** 5.8 installs, the project compiles, and `Rougarou.ClimbSim` passes all three routes. The debug actor shows the climb in a level.
2. **body.** Make a MetaHuman from the scan, retarget it to the sim's limb lengths, and drive it with Full Body IK.
3. **play.** Mouse and gamepad input, the focused hold camera, finger poses, slip and fall feedback.
4. **gym.** The first rougarou rock lounge corner: wall panels, a hold set, mats, lighting; then a small outdoor crag from the Fab packs.
5. **devices.** A trackpad and tablet pass, and frame-pacing checks on a medium-poly gym.

## open questions

- Mesh to MetaHuman on Mac with 5.8: confirm it runs locally, or whether part of the pipeline needs the cloud or Windows.
- Whether the downloaded Fab packs import cleanly into 5.8.

# Human motion port

Source: [ceeboozwah/rougarou at 3d30a187e736c28c76b58840d0d3248573175737](https://github.com/ceeboozwah/rougarou/tree/3d30a187e736c28c76b58840d0d3248573175737).

`corey-limits.json` preserves the original profile. The port adapts the swing/twist cone math from `web/limits.js`, constrained swivel search and posture sharing from `web/figure.js`, and posture fitting from `web/formlab.js`. These are numerical posture constraints and optimization, not a trained neural locomotion network.

Native `HumanRig` maps the CC4 names to the local Mixamo skeleton and re-expresses A-pose limits using a software neutral pose 32 degrees below its T-pose arms. Bind translations and scales stay unchanged. Limits never widen to force a contact. The pelvis can shift at most 20 cm, pitch 18 degrees and yaw 26 degrees; the constrained spine shares the counter-bend. Knees/elbows search for wall clearance. Swivel, pelvis correction and joint rotations have temporal limits. Clothing's separate garment bones retain the existing secondary motion.

`Content/Data/human_motion.json` is the promoted native profile; `web/human-motion.json` mirrors it for the local fitted web skin. The default tube renderer is unchanged. `web/human-motion.js` uses the same swing/twist math, native joint mapping, spine/scapula mix and rotation/swivel rate bounds. Native pelvis correction remains more complete than the web fitted skin.

## Reproduce

Build RougarouEditor, then run its automation tests with the absolute project path:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "$PWD/unreal/Rougarou.uproject" \
  -ExecCmds="Automation RunTests Rougarou" -TestExit="Automation Test Queue Empty" \
  -unattended -nullrhi -nosound
```

`Rougarou.HumanTraining` normally validates. Add `-humantrain` to fit a candidate with seed 20261102. It gathers 91 snapshots from the actual successful nine-problem simulation paths; routes 0,1,3,4,6,7 train the mix, and 2,5,8 are held out. It writes `Saved/HumanMotion/training.json`, never promotes automatically. A candidate is eligible only when train and held-out cost improve, held-out worst contact error and minimum clearance do not regress, and limits remain intact. The retained fitting report is in `../Reports/human-motion-training.json`.

`python3 unreal/Tools/check_web_motion.py` checks the actual local GLB hierarchy through 240 moving frames, including fixed bone offsets, joint bounds and per-frame rotation bounds. It installs the web app's pinned three.js version in an isolated temporary directory. `node web/check.mjs` separately proves all nine web problems remain solvable.

The fit is a sampled solver check, not proof of perfect motion in every stance. The report records worst contact errors as well as means. It does not train new walking clips, establish biomechanical validity, or provide contact locking for every extreme reach.

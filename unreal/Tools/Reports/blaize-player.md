# Blaize ActionForge player

The reviewed character is `/Game/Local/BlaizePlayer/climber`, selected with
`-blaizeplayer`. Normal launches still use the earlier personal-player fallback.
Desktop `blaize-rig.afcharacter` is the complete character archive; the older
Desktop `blaize.glb` contains Manny animation only and must not replace it.

Immutable extracted input: `assets/character/blaize/source/character.glb`.
It has 38,453 vertices, 49,600 triangles, 71 source bones, four influences per
vertex and three texture maps. Preserve its authored 180cm height and appearance.
The adapter renames anatomical bones and adds unweighted eye/sole landmarks.

The original reviewed import and six clips are retained under
`assets/character/blaize/baseline-before-polish/`. The shirt correction starts
from that baseline every time; it never accumulates changes across runs.

## November 5 posture and shirt pass

- `polish_blaize_weights.py` reduces arm influence on the upper side
  and back torso panels. A spatially smoothed fabric mask prevents
  texture shadows or lettering from creating sharp weight transitions.
- The lower panel is reset to the supplied source weights. The previous wholesale spine reweighting was rejected
  after user review because it exaggerated lower-back folds.
- Retarget with `--align-rest-directions --upright-torso`. This applies an
  eight-degree distributed spine correction to the jog and uses that authored
  jogging cycle for the faster run slot instead of the deeply folded Sprint.
  Mesh positions, bind transforms, UVs, body proportions and bone lengths stay fixed.
- The opening simulation starts 8cm lower and settles toward a target 10cm lower
  on slab/vertical panels while all four original contacts remain attached.
  Overhanging starts retain their original placement. The native and web sim match.
- Automatic backward chest extension for raised hands is reduced from 16 to 8
  degrees on slab/vertical walls in native and web rigs; overhangs retain 16. Bone translations and joint limits are preserved.
- Outsole-aware body fitting now includes a 6cm search step before the existing
  3cm/1cm refinement, within the same 12cm total correction bound.
- Native motion proofs now sample four moments across the running stride.

Rebuild in order with Blender's `--python-exit-code 1`:

1. Run `polish_blaize_weights.py` against the preserved baseline.
2. Run `retarget_corey_player.py -- --source-dir /absolute/assets/character/blaize
   --align-rest-directions --upright-torso`.
3. Run `import_corey_player.py --source-dir /absolute/assets/character/blaize
   --destination /Game/Local/BlaizePlayer --finish-script
   /absolute/unreal/Tools/finish_blaize_materials.py --external-materials
   --preserve-normals`.
4. Capture native `-motiontest` and `-route=0 -shot -contactshot` with
   `-blaizeplayer -windowed -ForceRes`; then run native Rougarou automation.

The material finisher must disable recomputed LOD normals explicitly: FBX
reimports can retain old build settings despite the requested import method.
Use the original color, normal and packed metallic/roughness maps; roughness is
green and metallic blue. Do not substitute flat fabric colors.

Proofs live in `Saved/BlaizePlayer`. `BeforePolish` retains the user's approved
baseline views. The lower shirt still contains authored wrinkles, and these
corrections do not establish collision-free cloth or perfect extreme contacts.

The earlier 12-test pass predates the latest user-requested posture changes.
See Review2/Automation for the rejected deeper-start trial.

The revised run slot keeps the hip-to-chest vertical separation between 37.98
and 38.77cm through the complete baked cycle, versus 34.11–35.25cm with the
original Sprint. The walk cycle remains unchanged (38.54–38.92cm). See
`Saved/BlaizePlayer/cycle-review.log`. These are skeletal measurements, not
a claim of cloth simulation or complete collision validation.

Final revised validation: 12/12 native Rougarou tests passed, zero failed or
skipped (`Review4/AutomationFinal/index.json`). All nine web problems remain
solvable. `Review4/Contact/shot.png` and the eight `Review4/Motion` captures
were generated in the native renderer; wall and multiple stride phases were
visually reviewed. The hem now retains its supplied drape and still has folds;
there is no added cloth solver. No source mesh positions or body dimensions
were changed. Test apps exited automatically.

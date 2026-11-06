# Native measured contact correction — November 5, 2026

The earlier character fit had pre-existing HumanRig failures (slab dance outsole
misses 3.30 and 11.93 cm). Hold meshes and sampled contact points already existed;
the pose solver did not reliably fit the imported ankle/knuckle frames.

HumanRig now fits the measured outsole landmark and actual middle-knuckle bone
with constrained coordinate descent over the existing joints. Bone translations,
scales, skin weights and joint ROM remain unchanged. Held contacts receive higher
priority than the comfort preference. Knuckles include 2.5 cm skin clearance.
The foot fit stops early once its squared contact/clearance cost is small.

Pole directions are filtered as well as swivel angles. Joint speed is capped at
5 radians/sec and swivel at 2 radians/sec even with the older Corey profile.
Thumb chains and toe-base joints now participate in grasp/support articulation;
these are procedural poses, not a trained finger controller. Upper-back extension
was reduced from 8/16 degrees to 4/12 degrees (vertical/overhang).

The HumanRig check now verifies actual posed knuckle location against sampled
resin, persistent outsole placement after 180 stationary frames, a high-step pole
change, rotation speed, anatomical limits and moving-joint wall clearance.

Limits: contact landmarks and joint/segment clearance are not full skinned-mesh
collision. Fingers still use grip-specific curls; arbitrary per-finger surface
conformity and dynamic loading need additional validation. These changes do not
promote the assisted training replay to a live controller or alter ClimbSim rules.

Final validation: HumanRig passes with latestplayer, including the 180-frame
settled-contact and high-step transition assertions. Final pose is refined within
the rotation budget and unsafe interpolated steps are shortened against the wall.
This checks joint positions, not every skin vertex.

Logs: /tmp/rougarou-contact-reviewed-final.log
Captures: Saved/MeasuredContactProof/shot.png and Saved/MeasuredHighStepProof/shot.png

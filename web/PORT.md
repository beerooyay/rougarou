# Native character and contact port

The browser ships `models/blaize-player.glb`, exported from the same shoulder-v2
Blaize player used by the Unreal `-latestplayer -shoulderv2` launcher. Its skin,
clothes, skeleton, and sole landmarks are preserved. `tools/export-player.py`
reproduces the GLB from the authoring blend without modifying that source file.
The browser still uses procedural locomotion; it does not run Unreal animation
blueprints or Unreal's renderer.

`sim.js` supplies support, reach, pump, slip, and finish rules. `skin.js` adapts the
native rig's anatomical swing path, bend-axis alignment, wrist/forearm orientation,
finger curls, thumb fold, and constrained contact fitting to Three.js. The body
may adjust at most 12 cm around the simulation pose to accommodate the actual
skeleton. Released feet do not contribute to this correction. Bone lengths stay
fixed. Climbing goals ease before fitting; post-fit smoothing must not displace
planted fingers or soles.

`hold-contact.js` builds a front-surface height map from each rendered hold's
triangles. Changing hold size, seed, angle, or route placement rebuilds that map.
Hands reserve palm clearance; fingers sample it while closing, and shoes use the
upper shoulder of the hold. This is a contact approximation, not full skinned-mesh
collision: deep undercuts and individual unsupported fingers still need visual
review. The browser's hold silhouettes use 1,280 triangles for route holds and
320 for background holds, with rough resin shading.

Validation:

- `node web/check.mjs`: all nine problems must survive the initial hang and be
  sendable. It builds the actual contact surfaces before testing grip placement.
- Serve `web/`, then open `rig-check.html`: render-skeleton checks for all starts.
- Open `rig-check.html?crimp`: make a reachable crimp move on each problem, then
  check digit segments against resin and planted sole positions. Results include
  actual gaps (1 mm sampled-surface penetration tolerance; 4 cm sole-target
  tolerance), so a passing contact check is not a claim that every digit
  bears weight or that every possible climbing sequence was tested.
- Verify the visible game, walking, drop/retry, and console separately.

Unreal remains the rules source of truth. Changes here do not promote any
movement-lab experimental controller into the game.

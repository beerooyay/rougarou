# Downloaded garment rig — November 4, 2026

The downloaded tee and standalone shorts are fitted and weighted to the
existing modular player skeleton. The tee is roomier, with shorter sleeves;
the shorts use forest green. The downloaded OBJ originals were not edited.

## Assets and selection

- Editable rig: `assets/character/garment-candidate/player.blend`.
- Runtime package: `/Game/Local/ModularPlayerCandidate/climber`.
- Native preview selection: `-game -garmentcandidate`.
- Original outfit remains the normal default in `/Game/Local/ModularPlayer`.
- Rebuild instructions: `unreal/Tools/README.md`, downloaded garment section.

Previous garments and original head remain hidden comparisons in the master;
they are not duplicated in the runtime export. The full editable body remains
intact. Runtime export omits deeply covered thigh faces, leaving overlap inside
the cuffs; body collision capsules remain complete.

## Construction

The tee's visible shell and rib collar retain their authored layers instead of
welding nearby inner and outer surfaces together. Additional interior/decorative
geometry is omitted. The source tee's approximately 1.8 million polygons become
18,000 render triangles; shorts use 8,854 render triangles.

Separate, manifold simulation proxies total 3,799 triangles and 2,199 particles.
This is approximately 82% fewer particles than an earlier direct-simulation
candidate's 11,923. This is a workload count, not a benchmarked frame-rate claim.
The proxies are fitted toward the inside of the visible garments, disabled for
rendering/shadows, and bound to the detailed fabric with multiple influences.
Both garments share one Chaos asset for cloth-to-cloth collision, with body
collision and continuous collision detection enabled. Neck and waist anchors
support the fabric while sleeves, sides, hem and cuffs can move.

## Verified final state

- Native editor module builds successfully; Python syntax checks and
  `git diff --check` pass.
- Blender checks pass: normalized weights, unchanged source body coordinates
  and skeleton, usable UVs, anchors/mobility, no branching render or proxy edges.
- All nine native Rougarou automation suites pass, including the new
  `GarmentBodyPreserved` test and proxy visibility/size checks.
- Native walking, running, turning and stopping captures completed.
- Native menu, first-person, pause, route mounting and dismount flow passed.

Proofs and logs are under `unreal/Saved/GarmentCandidate/`, including
`master-validation.json`, `Automation/`, `NativeMotion/`, `NativeContact/`,
`NativeSkinning/`, and `NativeFlow/`. The latter skinning capture disables
fabric through `-nofabric` without modifying saved player preferences.

## Visual limits

The rigging and native integration are implemented, but this remains an outfit
preview. Native captures still show rough deformation/edges around the tee's
underarms and hem, and cuff fit needs further visual refinement. Numerical
checks do not establish finished garment construction, complete absence of
posed intersections, or anatomically correct soft-tissue deformation. The
candidate has therefore not replaced the default player.

The web renderer and its character asset are unchanged by this native preview.

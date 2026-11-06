# Desktop character assets — November 4, 2026

The desktop assets are useful. The strongest starting point is the weighted
body recovered from `rig.afcharacter`, with its embedded texture atlas. It
contains the desired outfit and a working finger-weighted skeleton. It is a
candidate source, not a validated in-game replacement.

## Verified inventory

| Asset | Inspection result | Reuse |
| --- | --- | --- |
| Desktop `climber.fbx` | Blender imports one 88-bone armature and an empty, no mesh, materials, or textures | Skeleton reference only; this file is not the complete visible player |
| Desktop `body/…texture.fbx` | Static textured body; 110,907 vertices, 221,810 triangles, UVs, embedded color/normal/roughness/metallic images | Body/outfit shape and matching materials |
| `character.glb` recovered from `rig.afcharacter` | 71 bones; 221,810 body triangles, four normalized influences, no unweighted body vertices | Weighted body and fingers; retargeting and cleanup still required |
| Desktop `face/…texture.fbx` | Static head/bust; 855,188 vertices, 1,711,004 triangles, UVs and embedded maps; no bones or corrective shapes | Detailed head source for fitting, reduction and texture/normal baking |

The archive's `source.fbx` and the desktop body's FBX were compared with
SHA-256 and matched exactly. The archive metadata calls it an autorigged body.
Its rig differs from both the 88-bone standalone FBX and the current CC4 player;
do not assume those skeletons or animation bindings are interchangeable.

The original `rig.afcharacter` disappeared from the desktop during this review.
Its weighted `character.glb` had already been extracted and remains available
at `unreal/Saved/DesktopAssetReview/archive-character.glb`. No original desktop
file was moved, deleted or modified by this audit.

## Fingers and bend probes

All 30 articulated finger joints have nonzero vertex weights: three joints per
digit, five digits on each hand. The left four-finger curl probe moved 23,286
of 23,345 vertices with more than 5% finger influence by over 1 mm. This proves
that finger joints deform the mesh, not that the resulting grip is correct.
The curl still needs shape/weight refinement and calibration to hold surfaces.

Rendered arms-down, overhead and high-step poses exposed sleeve bunching,
underarm stretching and a lower-shirt region pulled by hip/thigh movement.
Those are bone-pose probes, not game IK, cloth playback or animation acceptance
tests. The asset has no corrective shape keys.

The static source body has exactly one connected component: outfit and skin
form a continuous mesh. The shirt and shorts are not already independent cloth
surfaces. Separate the fabric and finish its boundaries before expecting the
cuffs, hem and shorts to hang away from the body. Preserve the current garment
silhouette while correcting those regions and skin weights.

The recovered body measures approximately 1.84808 m high under its current
transforms, despite the `5_7` filename. The archive also records 185 cm. It
needs uniform rig/mesh normalization to the requested 1.7018 m and ground
alignment; foot sizing should then be checked independently.

## Texture recovery

The body source FBX contains an 8192×8192 color image and 4096×4096 detail maps.
The recovered rig contains embedded color and normal images. Its material also
has white emission enabled, which washed out the initial rendered inspection.
Disabling emission restored the textured appearance without repainting it.

The separately downloaded body PNG color atlas did not line up with the rig's
UVs in the trial render. Reassigning that PNG produced scrambled skin/clothes
patches. Use the matching embedded atlas; do not pair loose maps solely by
filename. The external PNG trial was discarded from the corrected candidate.
Roughness/metallic maps from the source FBX should be checked against that same
UV layout before the final material import.

## Preserved review artifacts

`unreal/Saved/DesktopAssetReview/` contains:

- `textured-rig-candidate.blend`: recovered rig with matching embedded textures
  and white emission disabled, saved in the neutral pose. Its hidden helper
  sphere is not part of the character and must be excluded from game export.
- `textured-rig-front.png`, `textured-rig-arms-down.png`,
  `textured-rig-overhead-back.png`, `textured-rig-high-step.png`, and
  `textured-rig-finger-probe.png`: reviewed pose renders.
- `face-front.png` and `face-back.png`: separate head previews.
- `inventory.json`, `pose-probe.json`, `topology-probe.json`,
  `rig-bone-positions.json`, and `provenance.json`: measured evidence and source
  hashes. Inventory paths describe the files at the time of import; temporary
  inspection blends and superseded previews were removed to avoid duplicates.

The GLB has 665,430 imported body vertices for 221,810 triangles because its
primitive is non-indexed. Optimize duplicate vertex storage while preserving
UV seams, normals and skin weights before making a runtime performance claim.
The separate head also needs substantial reduction before game integration.

No live character assets, game code, climbing rules, or desktop originals were
changed. Native animation retargeting, cloth simulation, contact fitting and
frame time remain untested for this source.

## Source promotion

During modular-player construction, the recovered GLB and corrected rig Blender
source were moved from the scratch review directory into
`assets/character/modular/source.glb` and `source.blend`. The desktop head FBX
was preserved as `head-source.fbx` there as well. Earlier inventory paths above
are historical inspection locations; rebuilds now use the canonical local
assets and do not depend on the desktop folders or Unreal's Saved directory.

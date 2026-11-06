# Downloaded garments — November 4, 2026

Inspected desktop originals read-only. Archives were extracted only into
`unreal/Saved/ClothesReview/`; no downloaded asset replaced the active player.
The comparison render there is `garments-front.png`, ordered clothes, shorts,
shirt from left to right. Each is independently scaled for viewing, so this
image is not a size comparison. Materials are neutral inspection overrides.

| Folder | Actual contents | Reuse assessment |
| --- | --- | --- |
| clothes | OBJ of a complete dressed man, 181,797 vertices / 186,404 polygons, three material slots, UVs | Potential alternate garment source; outfit must first be isolated from the body |
| man-shorts | Standalone OBJ shorts, 4,514 vertices / 4,427 polygons, UVs, separate color/normal/roughness textures | Promising lightweight candidate; fit, length, body clearance and skin weights still need work |
| shirt | Standalone tee OBJ, 1,030,822 vertices / 1,806,256 polygons, four materials, UVs | Proper collar and finished openings visible; requires substantial mesh reduction before realtime cloth |

All three sources are static OBJ geometry, with no skeleton or skin weights.
Making the tee roomier and changing shorts to forest green are practical edits.
Successful integration also requires fitting their rest poses, transferring
weights, preserving collar/waist anchors, and checking body/garment collision
through walking and deep climbing bends. A preview render does not establish
that any of these assets has working physics.

For the current player, the shoulder field and garment-opening skin weights
were revised before evaluating these alternatives. Native screenshots still
require review separately from Blender pose previews.

Relevant Epic documentation:

- [Clothing tool properties](https://dev.epicgames.com/documentation/en-us/unreal-engine/clothing-tool-in-unreal-engine---properties-reference): movement masks, collision and animation drive.
- [Panel cloth overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/panel-cloth-editor-overview): separate simulation/render geometry and transferred weights in the newer workflow.

## Rigging follow-up

Derived copies of the standalone tee and shorts are now fitted and weighted
to the existing player in `assets/character/garment-candidate/player.blend`.
The downloaded originals remain static and untouched. The rigged candidate
uses separate manifold physics proxies and stages in
`/Game/Local/ModularPlayerCandidate`; `-garmentcandidate` selects it for native
testing. See `downloaded-garments-rig.md` for validation and remaining fit work.

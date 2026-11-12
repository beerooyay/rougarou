# Grip and route audit

| Stage | What was found | Change |
| --- | --- | --- |
| Pointer to contact | The hover and move gate tested the center before the grab tested the clicked side. | Both now test the selected placement. The planar reach check allows 2.5 cm at the boundary and still passes the move to the skeleton gate. |
| Resin geometry | Thin crimps used rounded blobs. The front-facing knuckle sample sat high, leaving little room to hook over the top. | Edges and rails have a flatter pulling side, with denser surface sampling. The crimp knuckle sample sits lower on the front face. |
| Reach and latch | The native rig begins shaping toward the hold before arrival and takes the fitted grip when load begins. | The crimp solver now compares safe final curls, scoring fingertip clearance and a preferred bend. A safe hooked pose can exist beyond a collision encountered by the one-parameter curl sweep. |
| Fingers and thumb | The solver used almost the same direction for the middle and distal segments on crimps. Thumb clearance used tip and segment probes, and the skinned browser path skipped the last segment on rigs without a tip bone. | Crimp directions now distinguish the middle and distal segments. The browser skin estimates the terminal axis from the preceding bone when the tip bone is absent. |
| Load and feedback | Grip capacity varies with placement, pull direction, wall angle and fatigue. The cursor can get a different answer if it aims at another part of the same hold. | Reach and hover now use the same placement calculation as the contact model. |

The native pose uses bone probes against a wall-normal height map. It approximates finger pads, and it cannot see back-facing undercuts or collisions between skinned fingers. The landing animation blends a separate upper-body pose while falling. The standing animation uses the mirrored idle and saved pose offsets. The browser tube and skinned fingers use procedural grip directions. They do not use the native resin probes.

Slab Dance now offsets the low footholds to give the climber a lateral weight shift. The Roof gives the low foot a deeper hold and spreads the next two feet. New profiles include a scallop, stepped edge, wave sloper and asymmetric rib. Match the pull direction and visible usable face when placing variations. Judge a crimp on the first loaded frame and again after shifting the hips. Check both the thumb and the last two finger segments from the side.

The native finger regression includes a move to the first Slab Dance crimp. The route checks exercise all nine problems. These checks measure contact probes and gameplay reach, and a close visual pass is still needed for skin thickness and undercut cases.

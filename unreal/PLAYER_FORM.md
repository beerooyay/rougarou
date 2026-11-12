# Rig tuning studio

Open **menu (M) → player form**. The climb freezes in place while the character moves into a separate lit studio in an anatomical neutral pose.

## Tune a joint

Click a joint dot or select it from the list. Right-drag the background to orbit and scroll outside the controls to zoom. Drag a joint dot to move its anchor in the camera plane. Orbit to adjust it from another angle. The connected mesh follows smoothly. Each editable anchor can move up to 8 cm from its original position.

- **bend / side / twist** preview a joint rotation in the climbing solver's anatomical frame.
- Drag a colored ring or enter a preview angle. Edit **min / max**, or rotate to an angle and choose **set min / set max**.
- **mirror left / right** mirrors anchor moves and copies equivalent anatomical limits.
- **hold preview at the limits** applies the same clamp used by the live climbing solver. Combined bend and side form an elliptical swing boundary; their independent maxima are not a rectangular range.
- **show permitted arcs** shows each single-axis range. These arcs illustrate the individual limits, not the combined swing surface.
- **reset joint** restores that joint's factory anchor and limits. **reset pose** clears preview rotations without changing limits. **human defaults** reloads the entire factory profile into the draft.
- Select **idle** and drag a rotation ring or enter an angle to edit the standing animation. Edits pause playback and stay on the idle clip. **reset pose** clears the idle edits while that clip is selected. Animation previews can be played or scrubbed. With the clamp enabled, the draft limits constrain the preview animation as well.

## Save and apply

**save version** creates a readable JSON version in `unreal/Saved/RigProfiles/`. **open profile folder** opens that directory. Load a version by clicking its filename. Profiles use the same `limits`, `native` and `native_mix` format as `unreal/Content/Data/human_motion.json`.

**apply rig** validates the draft, atomically writes `unreal/Saved/RigProfiles/active.json`, saves a named version, updates both the character's climbing IK and the move/reach gate, restores the original camera and character transform, and resumes the existing climb. Later launches load the active profile automatically. An invalid active profile falls back to the factory profile.

**cancel** discards unapplied draft changes and restores the original scene. Applying human defaults restores the factory limits through the same workflow.

The climbing rig uses these anchor positions and joint limits at runtime. Animation previews in the studio also show the anchor offsets.

## Reading a profile

Joint keys use anatomical aliases, for example `R_Upperarm`, `L_Calf`, and `Waist`. Values are degrees:

| Studio control | JSON fields |
| --- | --- |
| bend min / max | `-back` / `fwd` |
| side min / max | `-inn` / `out` |
| twist min / max | the two values in `twist` |

The optional `idle_angles_deg` object stores bend, side, and twist offsets in degrees for the standing animation, keyed by skeleton bone name. The optional `anchor_offsets_cm` object stores parent-local XYZ offsets in centimeters, keyed by skeleton bone name. The `profile_name` identifies the tuning pass. The `character` field records its source mesh. A loaded file must contain all supported joint constraints; invalid or incomplete files leave the draft unchanged. After placing a JSON file in the profile folder, use **refresh profile list**, load it, then **apply rig**.

## Verification

`Rougarou.PlayerForm` checks mirrored bounds, the live clamp, readable JSON, save/load, applying to both solvers, restart reload, malformed-file rejection, and preservation of the factory profile.

`-route=0 -formpreview -studioflow -shotdir=/absolute/path` exercises entering the studio, preserving the paused climb, applying to an isolated test profile, returning to the wall, and capturing both views. It does not replace the user's active profile.

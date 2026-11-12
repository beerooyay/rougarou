"""Blender --background --python web/tools/export-player.py -- /path/to/player.blend
Export the native shoulder-v2 player without changing the authoring file.
"""
import sys
from pathlib import Path
import bpy

args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
if len(args) != 1:
    raise SystemExit('Supply the native shoulder-v2 player.blend after --')
root = Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(Path(args[0]).resolve()))
bpy.ops.object.select_all(action='DESELECT')
for obj in bpy.context.scene.objects:
    if obj.type in ('ARMATURE', 'MESH'):
        obj.select_set(True)
        if obj.type == 'ARMATURE':
            obj.animation_data_clear()
            for bone in obj.pose.bones:
                bone.matrix_basis.identity()
bpy.ops.export_scene.gltf(
    filepath=str(root / 'models/blaize-player.glb'), export_format='GLB',
    use_selection=True, export_animations=False, export_yup=True)

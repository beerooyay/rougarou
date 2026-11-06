"""Adapt a complete Blaize export with optional explicit source-shape fitting.

The immutable source and UVs are retained. Optional shape/garment fitting is
reported explicitly. Without a profile, skin weights and proportions are retained.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

import bpy
from mathutils import Vector, Matrix

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source-dir', type=Path, default=ROOT/'assets/character/blaize')
parser.add_argument('--shape-profile', type=Path, help='Optional localized neck/trap and head shape fit')
parser.add_argument('--height-m', type=float, help='Uniform unit conversion for a source exported without human-scale units')
parser.add_argument('--proof-dir', type=Path, default=ROOT/'unreal/Saved/BlaizePlayer')
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
OUT = args.source_dir.resolve()
PROOF = args.proof_dir.resolve()
PROOF.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(Path(__file__).parent))
import build_modular_player as helpers

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(OUT / 'source/character.glb'))
arm = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
mesh = next(o for o in bpy.context.scene.objects if o.type == 'MESH' and o.vertex_groups)
for obj in list(bpy.context.scene.objects):
    if obj not in (arm, mesh):
        # glTF importer creates a bone display icosphere; never export it.
        bpy.data.objects.remove(obj, do_unlink=True)
arm.parent = None
mesh.parent = arm
arm.name = 'BlaizeRig'
mesh.name = 'climber'

unit_scale = 1.0
if args.height_m:
    unit_scale = args.height_m / (max(v.co.z for v in mesh.data.vertices)-min(v.co.z for v in mesh.data.vertices))
    transform = Matrix.Scale(unit_scale, 4)
    mesh.data.transform(transform)
    arm.data.transform(transform)
positions = [tuple(v.co) for v in mesh.data.vertices]
weights = [[(g.group, g.weight) for g in v.groups] for v in mesh.data.vertices]
uv = [tuple(v.uv) for v in mesh.data.uv_layers.active.data]
bind = {b.name: list(sum((list(row) for row in b.matrix_local), [])) for b in arm.data.bones}
original_names = [b.name for b in arm.data.bones]
# Mixamo exports already have the target anatomical names behind a namespace.
for bone in arm.data.bones:
    if bone.name.startswith('mixamorig:'):
        old = bone.name
        bone.name = old.split(':', 1)[1]
        group = mesh.vertex_groups.get(old)
        if group: group.name = bone.name
helpers.aliases(arm, [mesh])
mapping = dict(zip(original_names, [b.name for b in arm.data.bones]))

# Shape fitting starts from the immutable export every run; never compound edits.
shape_report = None
garment_report = None
if args.shape_profile:
    from fit_blaize_shape import fit_shape
    profile = json.loads(args.shape_profile.read_text())
    shape_report = fit_shape(mesh, arm, profile)
    if profile.get('release_shirt_hem', False):
        from fit_blaize_garment import fit_hem
        garment_report = fit_hem(mesh)
        weights = [[(g.group, g.weight) for g in v.groups] for v in mesh.data.vertices]
    # Subsequent adapter invariants are relative to the explicitly fitted shape.
    positions = [tuple(v.co) for v in mesh.data.vertices]
    bind = {old: list(sum((list(row) for row in arm.data.bones[new].matrix_local), [])) for old,new in mapping.items()}

# Choose a real underside vertex near the toe, preserving all shoe geometry.
landmarks = {}
for side in ('Left', 'Right'):
    foot = arm.data.bones[side + 'Foot'].head_local
    toe = arm.data.bones[side + 'ToeBase'].head_local
    forward = toe - foot
    forward.z = 0
    forward.normalize()
    groups = {g.index for g in mesh.vertex_groups if g.name.startswith(side) and ('Foot' in g.name or 'Toe' in g.name)}
    verts = [v.co.copy() for v in mesh.data.vertices
             if sum(g.weight for g in v.groups if g.group in groups) > .5]
    low = min(v.z for v in verts)
    underside = [v for v in verts if v.z < low + .008]
    landmarks[side + 'SoleContact'] = (max(underside, key=lambda v: (v-foot).dot(forward)), side+'Foot')
top = max(v.co.z for v in mesh.data.vertices)
head = arm.data.bones['Head'].head_local
landmarks['PlayerEyes'] = (Vector((head.x, head.y-.085, top-.105)), 'Head')
bpy.context.view_layer.objects.active = arm
arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
for name, (point, parent) in landmarks.items():
    b = arm.data.edit_bones.new(name)
    b.head = point
    b.tail = point + Vector((0, 0, .01))
    b.parent = arm.data.edit_bones[parent]
bpy.ops.object.mode_set(mode='OBJECT')

assert positions == [tuple(v.co) for v in mesh.data.vertices]
assert weights == [[(g.group, g.weight) for g in v.groups] for v in mesh.data.vertices]
assert uv == [tuple(v.uv) for v in mesh.data.uv_layers.active.data]
bind_error = max(abs(a-b) for old,new in mapping.items() for a,b in zip(bind[old], sum((list(row) for row in arm.data.bones[new].matrix_local), [])))
# Entering Edit Mode reconstructs bone bases after uniform unit conversion.
# Bound that floating-point roundoff to 0.1mm / 1e-4 matrix components.
assert bind_error < 1e-4, bind_error
report = {'source_sha256': hashlib.sha256((OUT/'source/character.glb').read_bytes()).hexdigest(),
          'uniform_unit_scale': unit_scale, 'bind_roundoff_max': bind_error, 'vertices': len(positions), 'triangles': len(mesh.data.polygons),
          'height_m': top-min(v[2] for v in positions), 'bone_mapping': mapping,
          'original_bones': len(bind), 'export_bones': len(arm.data.bones),
          'positions_uv_weights_bind_unchanged': unit_scale == 1.0 and bind_error == 0.0 and shape_report is None,
          'shape_fit': shape_report,
          'garment_fit': garment_report,
          'positions_uv_weights_preserved_after_unit_conversion': shape_report is None,
          'maximum_influences': max(len(w) for w in weights),
          'landmarks': {n: list(p) for n, (p, _) in landmarks.items()}}
(PROOF/'build.json').write_text(json.dumps(report, indent=2)+'\n')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'player.blend'))
helpers.OUT = OUT
helpers.export_player([mesh], arm)
# glTF metallic-roughness is G=roughness, B=metallic. Preserve this mapping.
manifest = json.loads((OUT/'materials.json').read_text())
for spec in manifest['materials'].values():
    spec['roughness_channel'] = 'G'
    spec['metallic_texture'] = spec['roughness_texture']
    spec['metallic_channel'] = 'B'
    spec['two_sided'] = True
(OUT/'materials.json').write_text(json.dumps(manifest, indent=2)+'\n')

# Source-faithful reference render, without game lighting or any deformation.
scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 900
scene.render.resolution_y = 1000
scene.render.resolution_percentage = 100
scene.world = bpy.data.worlds.new('Preview World')
scene.world.use_nodes = True
scene.world.node_tree.nodes['Background'].inputs[0].default_value = (.22,.22,.22,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value = .6
scene.view_settings.view_transform = 'Standard'
for p, power, size in [((2,-3,4),350,4),((-3,-1,2),180,3),((0,3,3),250,3)]:
    bpy.ops.object.light_add(type='AREA', location=p)
    light = bpy.context.object
    light.data.energy = power
    light.data.shape = 'DISK'
    light.data.size = size
    light.rotation_euler = (Vector((0,0,1))-light.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(2,-4,2))
camera = bpy.context.object
camera.data.type = 'ORTHO'
camera.data.ortho_scale = 2.1
camera.rotation_euler = (Vector((0,0,.92))-camera.location).to_track_quat('-Z','Y').to_euler()
scene.camera = camera
scene.render.filepath = str(PROOF/'source-reference.png')
bpy.ops.render.render(write_still=True)

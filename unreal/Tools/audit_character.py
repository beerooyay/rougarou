"""Read-only Blender diagnostic for the current character's skinning.

Blender -b assets/character/corey/player.blend --python unreal/Tools/audit_character.py -- --output /tmp/character-audit.json

This measures skinning before Chaos cloth, corrective deformation, or game IK.
Edge strain is a diagnostic, not a cloth or anatomical acceptance test.
The loaded .blend and exported assets are never saved or replaced.
"""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector


def pose(arm, name):
    for bone in arm.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    if name in ('arms_down_65', 'drape_pose_42', 'overhead_65'):
        angle = {'arms_down_65': 65, 'drape_pose_42': 42, 'overhead_65': -65}[name]
        for side, sign in (('Left', 1), ('Right', -1)):
            bone = arm.pose.bones[side + 'Arm']
            head = bone.head.copy()
            bone.matrix = (Matrix.Translation(head)
                           @ Matrix.Rotation(math.radians(angle * sign), 4, 'Y')
                           @ Matrix.Translation(-head) @ bone.matrix)
            bpy.context.view_layer.update()
    if name == 'high_step_110':
        # Character faces -Y: negative X flexes the hip forward. Bend the
        # knee back relative to the thigh, rather than testing a locked leg.
        for bone_name, angle in (('LeftUpLeg', -110), ('LeftLeg', 120)):
            bone = arm.pose.bones[bone_name]
            bone.rotation_mode = 'QUATERNION'
            rest = arm.data.bones[bone_name].matrix_local.to_quaternion()
            bone.rotation_quaternion = Quaternion(rest.inverted() @ Vector((1, 0, 0)), math.radians(angle))
    bpy.context.view_layer.update()


def audit():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    source = Path(bpy.data.filepath)
    if not bpy.data.filepath:
        raise RuntimeError('Open a generated character .blend before running the audit')
    arm = next(obj for obj in bpy.context.scene.objects if obj.type == 'ARMATURE')
    report = {
        'source': str(source),
        'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'scope': 'skin-only diagnostics; no Chaos cloth, runtime IK, anatomical volume, or performance certification',
        'bones': len(arm.data.bones),
        'meshes': {},
    }
    names = {'Cotton_Tee', 'Forest_Shorts', 'CC_Base_Body', 'Buzz_Cut', 'Buzz_Cut_Fibers', 'Crew_Neck_Ribbing'}
    pose(arm, 'neutral')
    for obj in bpy.context.scene.objects:
        if obj.type != 'MESH' or obj.name not in names:
            continue
        vertices = np.array([tuple(vertex.co) for vertex in obj.data.vertices])
        influences = [[group.weight for group in vertex.groups if group.weight > 1e-6] for vertex in obj.data.vertices]
        sums = np.array([sum(weights) for weights in influences])
        entry = {
            'vertices': len(vertices),
            'bounds_object_units': np.ptp(vertices, axis=0).tolist(),
            'object_scale': list(obj.scale),
            'max_influences': max(map(len, influences)),
            'vertices_above_four_influences': sum(len(weights) > 4 for weights in influences),
            'weight_sum_range': [float(sums.min()), float(sums.max())],
            'shape_keys': list(obj.data.shape_keys.key_blocks.keys()) if obj.data.shape_keys else [],
            'modifiers': [modifier.type for modifier in obj.modifiers],
        }
        report['meshes'][obj.name] = entry
        if obj.name not in ('Cotton_Tee', 'Forest_Shorts'):
            continue
        edges = np.array([tuple(edge.vertices) for edge in obj.data.edges])
        initial = np.linalg.norm(vertices[edges[:, 0]] - vertices[edges[:, 1]], axis=1)
        valid = initial > 1e-7
        entry['skin_only_pose_checks'] = {}
        for name in ('neutral', 'drape_pose_42', 'arms_down_65', 'overhead_65', 'high_step_110'):
            pose(arm, name)
            evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
            mesh = evaluated.to_mesh()
            try:
                if len(mesh.vertices) != len(vertices) or len(mesh.edges) != len(edges):
                    raise RuntimeError('Topology-changing modifier prevents edge comparison on ' + obj.name)
                posed = np.array([tuple(vertex.co) for vertex in mesh.vertices])
                lengths = np.linalg.norm(posed[edges[:, 0]] - posed[edges[:, 1]], axis=1)
                ratios = lengths[valid] / initial[valid]
                entry['skin_only_pose_checks'][name] = {
                    'edge_ratio_p05': float(np.quantile(ratios, .05)),
                    'edge_ratio_p95': float(np.quantile(ratios, .95)),
                    'edges_above_1p5': int(np.count_nonzero(ratios > 1.5)),
                    'edges_below_0p5': int(np.count_nonzero(ratios < .5)),
                }
            finally:
                evaluated.to_mesh_clear()
        pose(arm, 'neutral')
    bones = arm.data.bones
    head = lambda name: bones[name].head_local
    report['bone_distances_object_units'] = {
        key: (head(start) - head(end)).length for key, start, end in (
            ('hip_joint_width', 'LeftUpLeg', 'RightUpLeg'),
            ('shoulder_joint_width', 'LeftArm', 'RightArm'),
            ('upper_arm', 'LeftArm', 'LeftForeArm'),
            ('forearm', 'LeftForeArm', 'LeftHand'),
            ('thigh', 'LeftUpLeg', 'LeftLeg'),
            ('shin', 'LeftLeg', 'LeftFoot'),
        )
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('CHARACTER AUDIT:', args.output, flush=True)


if __name__ == '__main__':
    audit()

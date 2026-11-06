"""Localized source-shape adjustment, with matching head/neck joints and normals."""
import bpy
from mathutils import Vector, Matrix


def fit_shape(mesh, arm, profile):
    scale = float(profile.get('head_scale', 1))
    neck = float(profile.get('neck_lowering_cm', 0)) / 100
    trap = float(profile.get('trap_lowering_cm', 0)) / 100
    assert .9 <= scale <= 1 and 0 <= neck <= .05 and 0 <= trap <= .06
    center = arm.data.bones['Head'].head_local.copy()
    head_group = mesh.vertex_groups['Head'].index
    skull = [v.co for v in mesh.data.vertices if any(g.group == head_group and g.weight > .8 for g in v.groups)]
    center.z = (min(p.z for p in skull)+max(p.z for p in skull))/2
    neck_z = arm.data.bones['Neck'].head_local.z
    head_z = arm.data.bones['Head'].head_local.z
    chest_z = arm.data.bones['Spine2'].head_local.z
    lower_z = chest_z + .25*(neck_z-chest_z)
    shoulder_span = max(abs(arm.data.bones[s+'Arm'].head_local.x-center.x) for s in ('Left','Right'))

    def smooth(a, b, x):
        t = max(0, min(1, (x-a)/(b-a)))
        return t*t*(3-2*t)

    def warp(p):
        # The head moves uniformly above the neck blend; the trapezius mask
        # fades out before the outer shoulder and below the upper chest.
        head = smooth(neck_z, head_z, p.z)
        inner = (1-smooth(.3*shoulder_span, shoulder_span, abs(p.x-center.x))) * smooth(lower_z, head_z, p.z)
        q = p + (p-center)*((scale-1)*head)
        q.z -= neck*head + trap*inner*(1-head)
        # Ease only the front torso, leaving collar, hem and limbs outside the mask.
        band = smooth(.99, 1.10, p.z)*(1-smooth(1.36, 1.44, p.z))
        front = 1-smooth(-.015, .04, p.y)
        middle = 1-smooth(.13, shoulder_span, abs(p.x-center.x))
        chest = smooth(1.16, 1.30, p.z)
        q.y += band*front*middle*(float(profile.get('chest_in_cm',0))*chest-float(profile.get('waist_ease_cm',0))*(1-chest))/100
        return q

    before = [v.co.copy() for v in mesh.data.vertices]
    normals = [n.vector.copy() for n in mesh.data.corner_normals]
    normal_maps = {}
    largest = 0
    for v, p in zip(mesh.data.vertices, before):
        q = warp(p)
        delta = (q-p).length
        if delta < 1e-9:
            continue
        largest = max(largest, delta)
        epsilon = .0001
        columns = []
        for axis in range(3):
            step = Vector((0, 0, 0)); step[axis] = epsilon
            columns.append((warp(p+step)-warp(p-step))/(2*epsilon))
        jacobian = Matrix(columns).transposed()
        assert jacobian.determinant() > .5, 'shape fit folds geometry'
        normal_maps[v.index] = jacobian.inverted().transposed()
        v.co = q
    mesh.data.update()
    mesh.data.normals_split_custom_set([
        (normal_maps[loop.vertex_index] @ normals[loop.index]).normalized()
        if loop.vertex_index in normal_maps else normals[loop.index]
        for loop in mesh.data.loops])

    # Only the neck/head pivots follow the sculpt. Outer shoulders and the
    # complete arm/leg chains retain their original bind positions.
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    moved_bones = []
    for b in arm.data.edit_bones:
        if b.name in ('Neck', 'Head', 'HeadTop_End'):
            b.head = warp(b.head.copy())
            b.tail = warp(b.tail.copy())
            moved_bones.append(b.name)
    bpy.ops.object.mode_set(mode='OBJECT')
    assert all((v.co-p).length == 0 for v,p in zip(mesh.data.vertices,before) if p.z <= .99)
    return {'head_scale': scale, 'neck_lowering_cm': neck*100,
            'trap_lowering_cm': trap*100, 'changed_vertices': len(normal_maps),
            'maximum_displacement_cm': largest*100, 'moved_bones': moved_bones,
            'lower_body_unchanged': True, 'normals_transformed_with_shape': True}

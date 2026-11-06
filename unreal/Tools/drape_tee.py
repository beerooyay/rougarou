"""Drape the sewn tee on the body in a relaxed A-pose, bind it with the body's own skin weights, then unpose it to the rig's rest.

Called from build_corey_player.py in place of author_garments.replace_tee. The old sewn tee was authored flat in the
T-pose with hand-blended arm weights, so its sleeves stayed horizontal tubes and peaked at the shoulder as soon as
the arms came down. Here the panels are cut around the arms-down pose, settled with Blender's cloth solver against
the full skin, weighted straight off that skin, and inverse-skinned back to the bind pose.
"""
import math
import bpy
import bmesh
from mathutils import Vector, Matrix, geometry

ARM_DROP = math.radians(42)   # below horizontal; lower than this the sleeve tube starts inside the lats and the drape tangles
FRAMES = 110


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def pose_arms(arm, angle):
    for side, sign in (('Left', 1), ('Right', -1)):
        pb = arm.pose.bones[side + 'Arm']
        head = pb.head.copy()
        pb.matrix = Matrix.Translation(head) @ Matrix.Rotation(sign * angle, 4, 'Y') @ Matrix.Translation(-head) @ pb.matrix
        bpy.context.view_layer.update()


def reset_pose(arm):
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()


def sew_panels(arm):
    """Front and back pattern pieces joined at the seams, with the sleeves swung down around the posed arms."""
    outline = [(-.080, 1.555), (-.205, 1.568), (-.425, 1.568), (-.425, 1.420),
               (-.235, 1.415), (-.220, 1.020), (.220, 1.020), (.235, 1.415),
               (.425, 1.420), (.425, 1.568), (.205, 1.568), (.080, 1.555)]
    for i in range(1, 12):
        a = math.pi * i / 12
        outline.append((.080 * math.cos(a), 1.555 - .030 * math.sin(a)))
    boundary = []
    edge_kind = []
    for i, a in enumerate(outline):
        b = outline[(i + 1) % len(outline)]
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        steps = max(1, round(length / .019))
        kind = 'open' if i in (2, 5, 8) or i >= 11 else 'seam'
        for j in range(steps):
            t = j / steps
            boundary.append((a[0] * (1 - t) + b[0] * t, a[1] * (1 - t) + b[1] * t))
            edge_kind.append(kind)

    def inside(x, z):
        result = False
        for a, b in zip(outline, outline[1:] + outline[:1]):
            if (a[1] > z) != (b[1] > z) and x < (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]) + a[0]:
                result = not result
        return result

    points = [Vector(p) for p in boundary]
    for iz in range(31):
        z = 1.025 + iz * .019
        for ix in range(46):
            x = -.4275 + ix * .019
            if inside(x, z):
                points.append(Vector((x, z)))
    edges = [(i, (i + 1) % len(boundary)) for i in range(len(boundary))]
    verts2, _, triangles, _, _, _ = geometry.delaunay_2d_cdt(points, edges, [list(range(len(boundary)))], 1, 1e-6)
    shoulders = {sign: arm.data.bones[side + 'Arm'].head_local for side, sign in (('Left', 1), ('Right', -1))}
    verts = []
    sleeve_masks = []
    for back in (False, True):
        for p in verts2:
            x, z = p
            # Pattern coordinates identify the sleeve before posing/draping.
            # Side torso and hem vertices can lie far along a lowered arm's
            # axis too; an axis projection alone must never classify them.
            sleeve_height = smoothstep((z - 1.350) / .070)
            sleeve = max(0, min(1, (abs(x) - .20) / .07)) * sleeve_height
            torso_depth = .173 * math.sqrt(max(.025, 1 - (x / .24) ** 2))
            sleeve_depth = .074 * math.sqrt(max(.015, 1 - ((z - 1.505) / .086) ** 2))
            depth = torso_depth * (1 - sleeve) + sleeve_depth * sleeve
            if z > 1.515 and abs(x) < .090:
                neck_blend = max(0, min(1, (z - 1.515) / .025))
                neck_depth = .081 * math.sqrt(max(.02, 1 - (x / .083) ** 2))
                depth = depth * (1 - neck_blend) + neck_depth * neck_blend
            neck = max(0, 1 - abs(x) / .10) * max(0, min(1, (z - 1.52) / .035))
            zz = z + (.038 * neck if back else 0)
            point = Vector((x, .028 + .026 * sleeve + (depth if back else -depth), zz))
            # Swing the sleeve about the shoulder joint by the same drop as the arm, easing in across the yoke.
            swing = smoothstep((abs(x) - .185) / .09) * sleeve_height
            sleeve_masks.append(swing)
            if swing > 0:
                sign = 1 if x > 0 else -1
                pivot = Vector((shoulders[sign].x, point.y, shoulders[sign].z))
                point = Matrix.Translation(pivot) @ Matrix.Rotation(sign * ARM_DROP * swing, 4, 'Y') @ Matrix.Translation(-pivot) @ point
            verts.append(tuple(point))
    n = len(verts2)
    faces = [tuple(t) for t in triangles] + [tuple(reversed([i + n for i in t])) for t in triangles]

    def closest(p):
        return min(range(n), key=lambda i: (verts2[i] - Vector(p)).length_squared)

    for i, kind in enumerate(edge_kind):
        if kind == 'seam':
            a = closest(boundary[i])
            b = closest(boundary[(i + 1) % len(boundary)])
            faces.append((a, a + n, b + n, b))
    data = bpy.data.meshes.new('Draped tee')
    data.from_pydata(verts, [], faces)
    data.update()
    mask = data.attributes.new('sleeve_panel', 'FLOAT', 'POINT')
    for item, value in zip(mask.data, sleeve_masks):
        item.value = value
    return data


def settle(tee, skin):
    scene = bpy.context.scene
    skin.modifiers.new('Skin collision', 'COLLISION')
    skin.collision.thickness_outer = .004
    skin.collision.cloth_friction = 10
    cloth = tee.modifiers.new('Drape', 'CLOTH')
    st = cloth.settings
    st.quality = 10
    st.mass = .25
    st.tension_stiffness = 14
    st.compression_stiffness = 14
    st.shear_stiffness = 4
    st.bending_stiffness = .3
    st.tension_damping = 8
    st.compression_damping = 8
    st.shear_damping = 8
    st.bending_damping = .6
    st.air_damping = 1.6
    cs = cloth.collision_settings
    cs.use_collision = True
    cs.distance_min = .005
    cs.collision_quality = 5
    cs.use_self_collision = True
    cs.self_distance_min = .004
    cs.self_friction = 6
    cloth.point_cache.frame_start = 1
    cloth.point_cache.frame_end = FRAMES
    scene.frame_start = 1
    scene.frame_end = FRAMES
    for frame in range(1, FRAMES + 1):
        scene.frame_set(frame)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    draped = bpy.data.meshes.new_from_object(tee.evaluated_get(depsgraph), preserve_all_data_layers=True, depsgraph=depsgraph)
    tee.modifiers.remove(cloth)
    old = tee.data
    tee.data = draped
    draped.name = 'Sewn tee panels'
    if not draped.materials:
        for m in old.materials:
            draped.materials.append(m)
    bpy.data.meshes.remove(old)
    skin.modifiers.remove(skin.modifiers['Skin collision'])
    scene.frame_set(1)


def transfer_weights(tee, skin, arm):
    for g in skin.vertex_groups:
        if g.name not in tee.vertex_groups:
            tee.vertex_groups.new(name=g.name)
    dt = tee.modifiers.new('Skin weights', 'DATA_TRANSFER')
    dt.object = skin
    dt.use_vert_data = True
    dt.data_types_verts = {'VGROUP_WEIGHTS'}
    dt.vert_mapping = 'POLYINTERP_NEAREST'
    dt.layers_vgroup_select_src = 'ALL'
    dt.layers_vgroup_select_dst = 'NAME'
    bpy.context.view_layer.objects.active = tee
    bpy.ops.object.modifier_apply(modifier=dt.name)
    # Relax the transferred weights along the fabric so sleeve and yoke share influence instead of snapping.
    mesh = tee.data
    neighbours = [[] for _ in mesh.vertices]
    for e in mesh.edges:
        a, b = e.vertices
        neighbours[a].append(b)
        neighbours[b].append(a)
    weights = [{g.group: g.weight for g in v.groups if g.weight > 1e-5} for v in mesh.vertices]
    for _ in range(3):
        relaxed = []
        for i, own in enumerate(weights):
            mixed = {k: .5 * w for k, w in own.items()}
            share = .5 / max(1, len(neighbours[i]))
            for j in neighbours[i]:
                for k, w in weights[j].items():
                    mixed[k] = mixed.get(k, 0) + w * share
            relaxed.append(mixed)
        weights = relaxed
    # Past the shoulder joint the sleeve belongs to the arm chain only: torso influence there leaves fabric
    # hanging in space when the arm drops, which read as fins and shoulder pads in game.
    arms = {}
    sleeve_mask = mesh.attributes.get('sleeve_panel')
    if not sleeve_mask:
        raise RuntimeError('Draped tee lost its sleeve pattern mask')
    for side, letter in (('Left', 'L'), ('Right', 'R')):
        pb = arm.pose.bones[side + 'Arm']
        chain_names = {side + 'Arm', side + 'ForeArm', side + 'Hand'} | {f'CC_Base_{letter}_{part}Twist0{n}' for part in ('Upperarm', 'Forearm') for n in (1, 2)}
        chain = {g.index for g in tee.vertex_groups if g.name in chain_names}
        arms[side] = (pb.head.copy(), (pb.tail - pb.head).normalized(), chain, tee.vertex_groups[side + 'Arm'].index)
    for i, own in enumerate(weights):
        co = mesh.vertices[i].co
        head, axis, chain, arm_index = arms['Left' if co.x > 0 else 'Right']
        along = (co - head).dot(axis)
        f = smoothstep((along - .025) / .07) * sleeve_mask.data[i].value
        if f <= 0:
            continue
        arm_only = {k: w for k, w in own.items() if k in chain}
        total = sum(arm_only.values())
        arm_only = {k: w / total for k, w in arm_only.items()} if total > 1e-6 else {arm_index: 1.0}
        mixed = {k: w * (1 - f) for k, w in own.items()}
        for k, w in arm_only.items():
            mixed[k] = mixed.get(k, 0) + w * f
        weights[i] = mixed
    for i, own in enumerate(weights):
        top = sorted(own.items(), key=lambda p: p[1], reverse=True)[:4]
        total = sum(w for _, w in top)
        if total <= 1e-6:
            raise RuntimeError('Unweighted tee vertex ' + str(i))
        # Removing a weight invalidates Blender's live VertexGroupElement
        # references. Snapshot integer indices before changing the collection.
        for group_index in [g.group for g in mesh.vertices[i].groups]:
            tee.vertex_groups[group_index].remove([i])
        for k, w in top:
            tee.vertex_groups[k].add([i], w / total, 'REPLACE')


def unpose(tee, arm):
    """Inverse linear-blend skinning: move the posed drape back to the rest pose through its new weights."""
    names = [g.name for g in tee.vertex_groups]
    skin_matrix = {}
    for group_index, name in enumerate(names):
        bone = arm.data.bones.get(name)
        if bone:
            skin_matrix[group_index] = arm.pose.bones[name].matrix @ bone.matrix_local.inverted()
    for v in tee.data.vertices:
        blended = Matrix.Identity(4) * 0
        total = 0
        for g in v.groups:
            if g.group in skin_matrix:
                blended = blended + skin_matrix[g.group] * g.weight
                total += g.weight
        if total > 1e-6:
            v.co = (blended * (1 / total)).inverted() @ v.co


def add_collar(tee, arm, material):
    bm = bmesh.new()
    bm.from_mesh(tee.data)
    adjacency = {}
    for edge in [e for e in bm.edges if e.is_boundary]:
        a, b = edge.verts
        adjacency.setdefault(a, []).append(b)
        adjacency.setdefault(b, []).append(a)
    remaining = set(adjacency)
    loops = []
    while remaining:
        start = remaining.pop()
        loop = [start]
        previous = None
        current = start
        while True:
            choices = [v for v in adjacency[current] if v is not previous]
            following = next((v for v in choices if v in remaining), None)
            if following is None:
                break
            loop.append(following)
            remaining.remove(following)
            previous, current = current, following
        loops.append(loop)
    neck = max((loop for loop in loops if abs(sum(v.co.x for v in loop) / len(loop)) < .06), key=lambda loop: sum(v.co.z for v in loop) / len(loop))
    collar_verts = [tuple(v.co) for v in neck]
    neck_indices = [v.index for v in neck]
    count = len(neck)
    for v in neck:
        point = v.co.copy()
        point.x *= .90
        point.y = .028 + (point.y - .028) * .90
        point.z += .006
        collar_verts.append(tuple(point))
    collar_faces = [(i, (i + 1) % count, (i + 1) % count + count, i + count) for i in range(count)]
    bm.free()
    collar_data = bpy.data.meshes.new('Sewn rib collar')
    collar_data.from_pydata(collar_verts, [], collar_faces)
    collar_data.update()
    collar = bpy.data.objects.new('Crew_Neck_Ribbing', collar_data)
    bpy.context.collection.objects.link(collar)
    collar.parent = arm
    rib = material.copy()
    rib.name = 'Ivory_Collar_Rib'
    collar.data.materials.append(rib)
    for group in tee.vertex_groups:
        collar.vertex_groups.new(name=group.name)
    for i, source in enumerate(neck_indices):
        for group in tee.data.vertices[source].groups:
            collar.vertex_groups[group.group].add([i, i + count], group.weight, 'REPLACE')
    rib.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value = (.62, .605, .57, 1)
    for f in collar_data.polygons:
        f.use_smooth = True
    bpy.context.view_layer.objects.active = collar
    thickness = collar.modifiers.new('Rib thickness', 'SOLIDIFY')
    thickness.thickness = .002
    bpy.ops.object.modifier_apply(modifier=thickness.name)
    anchor = collar.modifiers.new('Neck anchor', 'ARMATURE')
    anchor.object = arm
    return collar


def preview(path):
    scene = bpy.context.scene
    bpy.ops.object.camera_add(location=(0, -6, 1.0))
    cam = bpy.context.object
    cam.rotation_euler = (math.pi / 2, 0, 0)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = 2.0
    previous = (scene.camera, scene.render.engine, scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath)
    scene.camera = cam
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'MATERIAL'
    scene.display.shading.show_cavity = True
    scene.render.resolution_x = 900
    scene.render.resolution_y = 1100
    scene.render.resolution_percentage = 100
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)
    scene.camera, scene.render.engine, scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath = previous


def drape_tee(arm, old, skin, out):
    material = old.data.materials[0]
    pose_arms(arm, ARM_DROP)
    tee = bpy.data.objects.new('Cotton_Tee', sew_panels(arm))
    bpy.context.collection.objects.link(tee)
    tee.data.materials.append(material)
    tee.parent = arm
    for f in tee.data.polygons:
        f.use_smooth = True
    for o in bpy.context.scene.objects:
        o.hide_render = o not in (tee, skin)
    settle(tee, skin)
    bpy.context.view_layer.objects.active = tee
    relax = tee.modifiers.new('Relax seam ridges', 'SMOOTH')
    relax.factor = .5
    relax.iterations = 3
    bpy.ops.object.modifier_apply(modifier=relax.name)
    sub = tee.modifiers.new('Smooth sewn seams', 'SUBSURF')
    sub.levels = 1
    bpy.ops.object.modifier_apply(modifier=sub.name)
    for f in tee.data.polygons:
        f.use_smooth = True
    transfer_weights(tee, skin, arm)
    preview(out / 'apose.png')
    for o in bpy.context.scene.objects:
        o.hide_render = False
    unpose(tee, arm)
    reset_pose(arm)
    deform = tee.modifiers.new('CC4 weights', 'ARMATURE')
    deform.object = arm
    add_collar(tee, arm, material)
    bpy.data.objects.remove(old, do_unlink=True)
    bpy.data.objects.remove(skin, do_unlink=True)
    print('DRAPE: tee', len(tee.data.vertices), 'vertices', flush=True)
    return tee

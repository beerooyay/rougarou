"""Separate the recovered desktop player without discarding its rig or UVs.

Blender -b --python-exit-code 1 --python unreal/Tools/build_modular_player.py
Source files are read only. The editable modular master and game export live
under ignored assets/character/modular; proof renders live in unreal/Saved.
"""
import json
import math
from collections import Counter
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/character/modular'
PROOF = ROOT / 'unreal/Saved/ModularPlayer'
SOURCE = OUT / 'source.blend'
HEAD_SOURCE = OUT / 'head-source.fbx'
HEIGHT = 1.7018
REGIONS = ('Shirt', 'Head', 'Arms', 'Shorts', 'Legs', 'Shoes')


def smoothstep(value):
    value = np.clip(value, 0, 1)
    return value * value * (3 - 2 * value)


def set_weights(obj, vertex, weights):
    weights = sorted(((name, value) for name, value in weights.items() if value > 1e-6), key=lambda item: -item[1])[:4]
    total = sum(value for _, value in weights)
    if total < 1e-6:
        raise RuntimeError(f'Unweighted vertex {obj.name}:{vertex.index}')
    for index in [group.group for group in vertex.groups]:
        obj.vertex_groups[index].remove([vertex.index])
    for name, value in weights:
        group = obj.vertex_groups.get(name) or obj.vertex_groups.new(name=name)
        group.add([vertex.index], value / total, 'REPLACE')


def classify(source):
    mesh = source.data
    coords = np.array([tuple(source.matrix_world @ vertex.co) for vertex in mesh.vertices], dtype=np.float32)
    faces = np.array([face.vertices[:] for face in mesh.polygons])
    if faces.shape[1] != 3:
        raise RuntimeError('The recovered source is expected to be triangulated')
    uv = np.array([tuple(loop.uv) for loop in mesh.uv_layers.active.data]).reshape(-1, 3, 2).mean(axis=1)
    shader = next(node for node in source.data.materials[0].node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
    image = shader.inputs['Base Color'].links[0].from_node.image.copy()
    image.scale(1024, 1024)
    pixels = np.array(image.pixels[:], dtype=np.float32).reshape(1024, 1024, 4)
    xy = np.clip((uv * 1024).astype(int), 0, 1023)
    rgb = pixels[xy[:, 1], xy[:, 0], :3]
    bpy.data.images.remove(image)
    centers = coords[faces].mean(axis=1)
    x, y, z = centers.T
    white = (rgb[:, 2] > .80) & ((rgb[:, 0] - rgb[:, 1]) < .085) & ((rgb[:, 1] - rgb[:, 2]) < .085)
    labels = np.full(len(faces), -1, dtype=np.int32)
    labels[z < .155] = 5
    labels[(z >= .155) & (z < .77)] = 4
    labels[(z >= .69) & (z < 1.055) & (rgb[:, 0] < .72) & ~white] = 3
    labels[(z >= .94) & (z < 1.64) & white & (np.abs(x) < .48)] = 0
    labels[(z > 1.50) & (np.abs(x) < .145) & (labels != 0)] = 1
    labels[(z > 1.25) & (np.abs(x) > .22) & (labels != 0)] = 2
    known = np.flatnonzero(labels >= 0)
    tree = KDTree(len(known))
    for index in known:
        tree.insert(centers[index], int(index))
    tree.balance()
    for index in np.flatnonzero(labels < 0):
        _, nearest, _ = tree.find(centers[index])
        labels[index] = labels[nearest]
    return coords, labels


def aliases(arm, meshes):
    names = {'pelvis': 'Hips', 'spine_01': 'Spine', 'spine_02': 'Spine1', 'spine_03': 'Spine2', 'neck_01': 'Neck', 'Head': 'Head'}
    for side, prefix in (('l', 'Left'), ('r', 'Right')):
        for old, new in (('clavicle', 'Shoulder'), ('upperarm', 'Arm'), ('lowerarm', 'ForeArm'), ('hand', 'Hand'), ('thigh', 'UpLeg'), ('calf', 'Leg'), ('foot', 'Foot'), ('ball', 'ToeBase')):
            names[f'{old}_{side}'] = prefix + new
        for finger, digit in (('thumb', 'Thumb'), ('index', 'Index'), ('middle', 'Middle'), ('ring', 'Ring'), ('pinky', 'Pinky')):
            for joint in range(1, 4):
                names[f'{finger}_{joint:02d}_{side}'] = f'{prefix}Hand{digit}{joint}'
    for old, new in names.items():
        bone = arm.data.bones.get(old)
        if bone:
            bone.name = new
        for obj in meshes:
            group = obj.vertex_groups.get(old)
            if group:
                group.name = new


def weld_and_fair(source):
    bm = bmesh.new()
    bm.from_mesh(source.data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
    # Texture pixels near a seam can make isolated mislabelled triangles.
    # Remove those islands instead of leaving pinholes in the separated parts.
    for _ in range(2):
        remaining = set(bm.faces)
        components = {region: [] for region in range(len(REGIONS))}
        while remaining:
            seed = remaining.pop()
            region = seed.material_index
            group = [seed]
            stack = [seed]
            while stack:
                face = stack.pop()
                for edge in face.edges:
                    for adjacent in edge.link_faces:
                        if adjacent in remaining and adjacent.material_index == region:
                            remaining.remove(adjacent)
                            group.append(adjacent)
                            stack.append(adjacent)
            components[region].append(group)
        for region, groups in components.items():
            keep = 2 if region in (2, 4, 5) else 1
            for group in sorted(groups, key=len, reverse=True)[keep:]:
                neighbours = Counter(adjacent.material_index for face in group for edge in face.edges
                                     for adjacent in edge.link_faces if adjacent.material_index != region)
                if neighbours:
                    replacement = neighbours.most_common(1)[0][0]
                    for face in group:
                        face.material_index = replacement
    # Fair a shared boundary before duplicating it into parts. Both parts then
    # begin at identical coordinates, with less texture-driven edge noise.
    for _ in range(4):
        neighbours = {}
        for edge in bm.edges:
            if len(edge.link_faces) == 2 and edge.link_faces[0].material_index != edge.link_faces[1].material_index:
                a, b = edge.verts
                neighbours.setdefault(a, []).append(b)
                neighbours.setdefault(b, []).append(a)
        changes = {vertex: vertex.co.lerp(sum((other.co for other in adjacent), Vector()) / len(adjacent), .45)
                   for vertex, adjacent in neighbours.items() if len(adjacent) == 2}
        for vertex, point in changes.items():
            vertex.co = point
    # The original texture-painted shorts edge is ragged. Give each cuff a
    # shared clean plane before splitting the fabric and the skin underneath.
    for sign in (-1, 1):
        cuff = {vertex for edge in bm.edges if len(edge.link_faces) == 2
                and {face.material_index for face in edge.link_faces} == {3, 4}
                for vertex in edge.verts if vertex.co.x * sign > 0}
        if cuff:
            height = float(np.median([vertex.co.z for vertex in cuff]))
            for vertex in cuff:
                vertex.co.z = height
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(source.data)
    bm.free()


def extract(source, region, name, side=None):
    obj = source.copy()
    obj.data = source.data.copy()
    obj.name = name
    bpy.context.collection.objects.link(obj)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    discard = [face for face in bm.faces if face.material_index != region
               or (side is not None and face.calc_center_median().x * side < 0)]
    bmesh.ops.delete(bm, geom=discard, context='FACES')
    bmesh.ops.delete(bm, geom=[vertex for vertex in bm.verts if not vertex.link_faces], context='VERTS')
    bm.to_mesh(obj.data)
    bm.free()
    if not obj.data.polygons:
        raise RuntimeError('Empty part ' + name)
    for face in obj.data.polygons:
        face.material_index = 0
        face.use_smooth = True
    return obj


def boundary_loops(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    adjacency = {}
    for edge in bm.edges:
        if edge.is_boundary:
            a, b = edge.verts
            adjacency.setdefault(a.index, []).append(b.index)
            adjacency.setdefault(b.index, []).append(a.index)
    remaining = set(adjacency)
    loops = []
    while remaining:
        start = min(remaining)
        current = start
        loop = []
        while current in remaining:
            loop.append(current)
            remaining.remove(current)
            next_vertices = [index for index in adjacency[current] if index in remaining]
            if not next_vertices:
                break
            current = next_vertices[0]
        if len(loop) >= 8:
            loops.append(loop)
    bm.free()
    return loops


def extend_skin(obj, direction, distance, arm):
    # Underlap is inside clothing; it gives a moving sleeve/short hem real skin
    # behind its opening, without sealing or welding the garment to the limb.
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    candidates = [edge for edge in bm.edges if edge.is_boundary]
    if not candidates:
        bm.free()
        return
    if obj.name.startswith('Leg'):
        # The scan stops at the fabric edge. A short, open overlap strip is
        # exposed by a high step; build the whole covered thigh instead.
        upper = {vertex for edge in candidates for vertex in edge.verts if vertex.co.z > .40}
        if upper:
            center = sum((vertex.co for vertex in upper), Vector()) / len(upper)
            obj['skin_cuff_z'] = center.z
            for vertex in bm.verts:
                blend = float(smoothstep((vertex.co.z - center.z + .055) / .055))
                radial = Vector((vertex.co.x - center.x, vertex.co.y - center.y, 0))
                if radial.length > .02:
                    radius = min(radial.length, .074)
                    vertex.co -= radial.normalized() * (radial.length - radius) * blend
    skin = bpy.data.materials.get('Skin_Underlap')
    if skin is None:
        skin = bpy.data.materials.new('Skin_Underlap')
        skin.use_nodes = True
        shader = skin.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Base Color'].default_value = (.72, .36, .26, 1)
        shader.inputs['Roughness'].default_value = .64
    obj.data.materials.append(skin)
    if obj.name.startswith('Leg') and upper:
        material_index = len(obj.data.materials) - 1
        prefix = 'Left' if obj.name.endswith('_L') else 'Right'
        hip = arm.data.bones[prefix + 'UpLeg'].head_local
        end_center = hip + Vector((0, 0, -.035))
        current = [edge for edge in candidates if all(vertex in upper for vertex in edge.verts)]
        roots = {vertex: vertex.co.copy() for vertex in upper}
        destinations = {vertex: end_center + Vector((vertex.co.x - center.x, vertex.co.y - center.y, 0)).normalized() * .074
                        for vertex in upper}
        for row in range(1, 6):
            previous = {vertex for edge in current for vertex in edge.verts}
            before_faces = set(bm.faces)
            result = bmesh.ops.extrude_edge_only(bm, edges=current)
            new_vertices = {item for item in result['geom'] if isinstance(item, bmesh.types.BMVert)}
            next_roots, next_destinations = {}, {}
            for vertex in new_vertices:
                parent = min(previous, key=lambda other: (other.co - vertex.co).length_squared)
                next_roots[vertex], next_destinations[vertex] = roots[parent], destinations[parent]
                vertex.co = roots[parent].lerp(destinations[parent], row / 5)
            for face in set(bm.faces) - before_faces:
                face.material_index = material_index
            current = [edge for edge in bm.edges if edge.is_boundary and all(vertex in new_vertices for vertex in edge.verts)]
            roots, destinations = next_roots, next_destinations
        # The end stays under the pelvis; cap it so looking down a loose cuff
        # cannot reveal an empty, cut-off tube.
        before_faces = set(bm.faces)
        bmesh.ops.holes_fill(bm, edges=current, sides=0)
        for face in set(bm.faces) - before_faces:
            face.material_index = material_index
        for face in bm.faces:
            if face.calc_center_median().z > center.z - .045:
                face.material_index = material_index
        # Only the ankle opening needs the short overlap below the shoe.
        candidates = [edge for edge in candidates if not all(vertex in upper for vertex in edge.verts)]
    original_faces = set(bm.faces)
    result = bmesh.ops.extrude_edge_only(bm, edges=candidates)
    new_vertices = [item for item in result['geom'] if isinstance(item, bmesh.types.BMVert)]
    for vertex in new_vertices:
        delta = direction(vertex.co) if callable(direction) else direction
        vertex.co += delta * distance
    for face in set(bm.faces) - original_faces:
        face.material_index = len(obj.data.materials) - 1
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(obj.data)
    bm.free()


def limit_weights(part):
    cuff = (part.get('skin_cuff_z', max(v.co.z for v in part.data.vertices) - .035) if part.name.startswith('Leg_') else
            min(abs(v.co.x) for v in part.data.vertices) + .045 if part.name.startswith('Arm_') else 0)
    for vertex in part.data.vertices:
        own = {part.vertex_groups[group.group].name: group.weight for group in vertex.groups}
        if part.name == 'Shirt':
            # No hip, thigh, hand or finger can pull a tee panel. Keep the arm
            # influence on sleeves and a smooth shoulder transition only.
            x, y, z = vertex.co
            prefix = 'Left' if x > 0 else 'Right'
            body = {name: value for name, value in own.items() if name in ('Spine', 'Spine1', 'Spine2')}
            upper = {name: value for name, value in own.items() if name in (prefix + 'Shoulder', prefix + 'Arm')}
            if not body:
                body = {'Spine2': 1}
            if not upper:
                upper = {prefix + 'Arm': 1}
            # A continuous chest-to-sleeve field avoids the old hard cutoff
            # through the shoulder blade. The cuff follows the upper arm,
            # just like the skin underneath; the collar follows the chest.
            blend = float(smoothstep((abs(x) - .12) / .18) * smoothstep((z - 1.25) / .10))
            body_total, upper_total = sum(body.values()), sum(upper.values())
            own = {name: value / body_total * (1 - blend) for name, value in body.items()}
            own.update({name: value / upper_total * blend for name, value in upper.items()})
        elif part.name == 'Shorts':
            x, y, z = vertex.co
            hip = max(float(smoothstep((z - .77) / .14)), .80 * (1 - float(smoothstep(abs(x) / .06))))
            left = float(smoothstep((x + .025) / .05))
            own = {'Hips': hip, 'LeftUpLeg': (1 - hip) * left, 'RightUpLeg': (1 - hip) * (1 - left)}
        elif part.name.startswith(('Arm_', 'Leg_', 'Shoe_')):
            prefix = 'Left' if part.name.endswith('_L') else 'Right'
            allowed = (prefix + 'Shoulder', prefix + 'Arm', prefix + 'ForeArm', prefix + 'Hand') if part.name.startswith('Arm') else (prefix + 'UpLeg', prefix + 'Leg', prefix + 'Foot', prefix + 'ToeBase')
            own = {name: value for name, value in own.items() if name in allowed or (part.name.startswith('Arm') and name.startswith(prefix + 'Hand'))}
            if part.name.startswith('Leg_'):
                # The scan blends knee motion far up the thigh. At a high
                # step that pulls exposed skin through a thigh-bound cuff.
                # Match the cuff's upper-leg frame, then feather into the
                # original knee weights below the opening.
                rigid = float(smoothstep((vertex.co.z - cuff + .095) / .065))
                total = sum(own.values())
                own = {name: value / total * (1 - rigid) for name, value in own.items()}
                own[prefix + 'UpLeg'] = own.get(prefix + 'UpLeg', 0) + rigid
            elif part.name.startswith('Arm_'):
                rigid = 1 - float(smoothstep((abs(vertex.co.x) - cuff) / .085))
                total = sum(own.values())
                own = {name: value / total * (1 - rigid) for name, value in own.items()}
                own[prefix + 'Arm'] = own.get(prefix + 'Arm', 0) + rigid
        set_weights(part, vertex, own)


def reduce(part, triangles):
    if len(part.data.polygons) <= triangles:
        return
    bpy.context.view_layer.objects.active = part
    modifier = part.modifiers.new('Simulation-friendly topology', 'DECIMATE')
    modifier.ratio = triangles / len(part.data.polygons)
    modifier.use_collapse_triangulate = True
    while list(part.modifiers).index(modifier) > 0:
        bpy.ops.object.modifier_move_up(modifier=modifier.name)
    bpy.ops.object.modifier_apply(modifier=modifier.name)


def fit_head(original, arm):
    """Fit the detailed desktop head and bridge its neck into the collar.

    Keep the original as a disabled variant in the editable master, rather than
    leaving two coincident heads in the game export.
    """
    path = HEAD_SOURCE
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path), use_anim=False)
    imported = set(bpy.data.objects) - before
    head = next(obj for obj in imported if obj.type == 'MESH')
    head.name = 'Head_Face'
    for obj in imported - {head}:
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.context.view_layer.objects.active = head
    bpy.ops.object.select_all(action='DESELECT')
    head.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    reduce(head, 50000)
    # Strip the bust's clothing by its actual texture boundary, retaining the
    # longer front neck instead of cutting through the chin with a flat plane.
    shader = next(node for node in head.data.materials[0].node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
    image = shader.inputs['Base Color'].links[0].from_node.image.copy()
    image.scale(1024, 1024)
    pixels = np.array(image.pixels[:], dtype=np.float32).reshape(1024, 1024, 4)
    bm = bmesh.new()
    bm.from_mesh(head.data)
    uv_layer = bm.loops.layers.uv.active
    remove = []
    for face in bm.faces:
        center = face.calc_center_median()
        uv = sum((loop[uv_layer].uv for loop in face.loops), Vector((0, 0))) / len(face.loops)
        r, g, b = pixels[min(1023, max(0, int(uv.y * 1024))), min(1023, max(0, int(uv.x * 1024))), :3]
        if center.z < -.77 or (center.z < -.40 and b > .80 and r - g < .085 and g - b < .085):
            remove.append(face)
    bmesh.ops.delete(bm, geom=remove, context='FACES')
    bmesh.ops.delete(bm, geom=[vertex for vertex in bm.verts if not vertex.link_faces], context='VERTS')
    bm.to_mesh(head.data)
    bm.free()
    bpy.data.images.remove(image)
    # Head width and crown define a uniform fit; facial features are not warped
    # independently to satisfy a bounding box.
    old_points = np.array([tuple(vertex.co) for vertex in original.data.vertices])
    new_points = np.array([tuple(vertex.co) for vertex in head.data.vertices])
    old_band = old_points[(old_points[:, 2] > 1.66) & (old_points[:, 2] < 1.79)]
    new_band = new_points[(new_points[:, 2] > 0) & (new_points[:, 2] < .40)]
    scale = np.ptp(old_band[:, 0]) / np.ptp(new_band[:, 0])
    shift = Vector((float((old_band[:, 0].max() + old_band[:, 0].min()) / 2),
                    float((old_band[:, 1].max() + old_band[:, 1].min()) / 2),
                    float(old_points[:, 2].max() - new_points[:, 2].max() * scale)))
    shift.y += .012
    shift.z -= .008
    for vertex in head.data.vertices:
        vertex.co = vertex.co * scale + shift
    # The bust includes small collar wings behind the jaw. Keep the front
    # beard/jaw, and remove only the low outer back/side pieces.
    bm = bmesh.new()
    bm.from_mesh(head.data)
    bmesh.ops.delete(bm, geom=[face for face in bm.faces
                              if face.calc_center_median().z < 1.625
                              and face.calc_center_median().y > -.035
                              and abs(face.calc_center_median().x) > .060], context='FACES')
    bmesh.ops.delete(bm, geom=[vertex for vertex in bm.verts if not vertex.link_faces], context='VERTS')
    bm.to_mesh(head.data)
    bm.free()
    # Close any small segmentation pinholes; only the lowest major loop is a
    # neck opening. The original collar supplies its destination profile.
    bm = bmesh.new()
    bm.from_mesh(head.data)
    bmesh.ops.holes_fill(bm, edges=[edge for edge in bm.edges if edge.is_boundary], sides=12)
    bm.to_mesh(head.data)
    bm.free()
    loops = boundary_loops(head)
    neck = min((loop for loop in loops if len(loop) > 20), key=lambda loop: sum(head.data.vertices[i].co.z for i in loop) / len(loop))
    old_neck = max(boundary_loops(original), key=len)
    old_center = sum((original.data.vertices[i].co for i in old_neck), Vector()) / len(old_neck)
    samples = sorted((math.atan2(original.data.vertices[i].co.y - old_center.y,
                                original.data.vertices[i].co.x - old_center.x), original.data.vertices[i].co.copy()) for i in old_neck)
    angles = np.array([angle for angle, point in samples])
    positions = np.array([tuple(point) for angle, point in samples])
    angles = np.concatenate((angles[-1:] - 2 * math.pi, angles, angles[:1] + 2 * math.pi))
    positions = np.vstack((positions[-1:], positions, positions[:1]))
    targets = {}
    for index in neck:
        point = head.data.vertices[index].co
        angle = math.atan2(point.y - old_center.y, point.x - old_center.x)
        target = Vector(tuple(np.interp(angle, angles, positions[:, axis]) for axis in range(3)))
        target.z -= .008
        targets[index] = target
    # A short tapered extension overlaps inside the collar, independent of the
    # shirt. It uses the existing neck UV at its root instead of a bare cap.
    bm = bmesh.new()
    bm.from_mesh(head.data)
    bm.verts.ensure_lookup_table()
    neck_set = {bm.verts[index] for index in neck}
    edges = [edge for edge in bm.edges if edge.is_boundary and all(vertex in neck_set for vertex in edge.verts)]
    target_by_vertex = {bm.verts[index]: target for index, target in targets.items()}
    # Extrude once, then aim each new vertex from its coincident old root.
    result = bmesh.ops.extrude_edge_only(bm, edges=edges)
    for vertex in result['geom']:
        if isinstance(vertex, bmesh.types.BMVert):
            root = min(neck_set, key=lambda other: (other.co - vertex.co).length_squared)
            vertex.co = target_by_vertex[root]
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(head.data)
    bm.free()
    head.vertex_groups.clear()
    for vertex in head.data.vertices:
        height = float(smoothstep((vertex.co.z - 1.55) / .10))
        set_weights(head, vertex, {'Head': height, 'Neck': 1 - height})
    modifier = head.modifiers.new('Player rig', 'ARMATURE')
    modifier.object = arm
    for face in head.data.polygons:
        face.use_smooth = True
    head.data.materials[0].name = 'Skin_Head_Face'
    head.parent = arm
    head.matrix_world = Matrix.Identity(4)
    head['region'] = 'Head'
    head['source'] = str(path)
    head['uniform_fit_scale'] = float(scale)
    variants = bpy.data.collections.new('Head variants - excluded from game export')
    bpy.context.scene.collection.children.link(variants)
    for collection in list(original.users_collection):
        collection.objects.unlink(original)
    variants.objects.link(original)
    original.hide_render = True
    original.hide_set(True)
    original['export'] = False
    return head


def add_collar(shirt, arm):
    loops = boundary_loops(shirt)
    neck = max((loop for loop in loops if abs(sum(shirt.data.vertices[i].co.x for i in loop) / len(loop)) < .08),
               key=lambda loop: sum(shirt.data.vertices[i].co.z for i in loop) / len(loop))
    center = sum((shirt.data.vertices[i].co for i in neck), Vector()) / len(neck)
    vertices = []
    for row in (0, 1):
        for index in neck:
            point = shirt.data.vertices[index].co.copy()
            radial = Vector((point.x - center.x, point.y - center.y, 0)).normalized()
            point += radial * (.001 if row == 0 else -.007)
            point.z += .002 if row == 0 else .003
            vertices.append(tuple(point))
    count = len(neck)
    faces = [(i, (i + 1) % count, (i + 1) % count + count, i + count) for i in range(count)]
    mesh = bpy.data.meshes.new('Crew neck ribbing')
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    collar = bpy.data.objects.new('Collar', mesh)
    bpy.context.collection.objects.link(collar)
    collar.parent = arm
    material = bpy.data.materials.new('Collar_Rib')
    material.use_nodes = True
    shader = material.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (.77, .75, .70, 1)
    shader.inputs['Roughness'].default_value = .92
    mesh.materials.append(material)
    for i, original in enumerate(neck):
        own = {shirt.vertex_groups[group.group].name: group.weight for group in shirt.data.vertices[original].groups
               if shirt.vertex_groups[group.group].name != 'ClothPin'}
        set_weights(collar, mesh.vertices[i], own)
        set_weights(collar, mesh.vertices[i + count], own)
    for face in mesh.polygons:
        face.use_smooth = True
    modifier = collar.modifiers.new('Player rig', 'ARMATURE')
    modifier.object = arm
    collar['region'] = 'Shirt'
    return collar


def add_underbody(arm, shirt):
    """A covered skin foundation gives cloth a body to move away from."""
    vertices, faces, weights = [], [], []
    samples = [(1.04, .17, .105),
               (1.16, .175, .105), (1.29, .18, .112), (1.40, .185, .105),
               (1.48, .15, .080), (1.53, .055, .048)]
    sides = 32
    for height, width, depth in samples:
        mix = float(smoothstep((height - .94) / .40))
        for i in range(sides):
            angle = 2 * math.pi * i / sides
            vertices.append((width * math.cos(angle), .025 + depth * math.sin(angle), height))
            weights.append({'Hips': 1 - mix, 'Spine2': mix})
    for row in range(len(samples) - 1):
        for i in range(sides):
            j = row * sides + i
            k = row * sides + (i + 1) % sides
            faces.append((j, k, k + sides, j + sides))
    for side in ('Left', 'Right'):
        shoulder = arm.data.bones[side + 'Arm'].head_local
        elbow = arm.data.bones[side + 'ForeArm'].head_local
        axis = (elbow - shoulder).normalized()
        front = Vector((0, -1, 0))
        vertical = axis.cross(front).normalized()
        start = len(vertices)
        for row, t in enumerate((0, .25, .50, .72)):
            center = shoulder.lerp(elbow, t)
            for i in range(20):
                angle = 2 * math.pi * i / 20
                point = center + front * math.cos(angle) * .049 + vertical * math.sin(angle) * .052
                vertices.append(tuple(point))
                weights.append({side + 'Arm': .8 + .2 * t, side + 'Shoulder': .2 * (1 - t)})
            if row:
                for i in range(20):
                    a = start + (row - 1) * 20 + i
                    b = start + (row - 1) * 20 + (i + 1) % 20
                    faces.append((a, b, b + 20, a + 20))
    mesh = bpy.data.meshes.new('Covered body foundation')
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    envelope = BVHTree.FromPolygons([vertex.co.copy() for vertex in shirt.data.vertices],
                                    [tuple(face.vertices) for face in shirt.data.polygons])
    for vertex in mesh.vertices:
        closest, normal, index, distance = envelope.find_nearest(vertex.co)
        if closest is not None and (vertex.co - closest).dot(normal) > -.012:
            vertex.co = closest - normal * .012
    body = bpy.data.objects.new('Body_Underlay', mesh)
    bpy.context.collection.objects.link(body)
    body.parent = arm
    mesh.materials.append(bpy.data.materials['Skin_Underlap'])
    for vertex, own in zip(mesh.vertices, weights):
        set_weights(body, vertex, own)
    for face in mesh.polygons:
        face.use_smooth = True
    modifier = body.modifiers.new('Player rig', 'ARMATURE')
    modifier.object = arm
    body['region'] = 'Body'
    return body


def fabric_material(original):
    material = original.copy()
    material.name = 'Outfit_Fabric'
    shader = next(node for node in material.node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
    color = shader.inputs['Base Color'].links[0].from_socket
    tint = material.node_tree.nodes.new('ShaderNodeVertexColor')
    tint.layer_name = 'Fabric'
    luminance = material.node_tree.nodes.new('ShaderNodeRGBToBW')
    material.node_tree.links.new(color, luminance.inputs[0])
    contrast = material.node_tree.nodes.new('ShaderNodeMapRange')
    contrast.inputs['To Min'].default_value = .55
    contrast.inputs['To Max'].default_value = 1
    material.node_tree.links.new(luminance.outputs[0], contrast.inputs['Value'])
    multiply = material.node_tree.nodes.new('ShaderNodeMixRGB')
    multiply.blend_type = 'MULTIPLY'
    multiply.inputs[0].default_value = 1
    material.node_tree.links.new(contrast.outputs[0], multiply.inputs[1])
    material.node_tree.links.new(tint.outputs['Color'], multiply.inputs[2])
    material.node_tree.links.new(multiply.outputs[0], shader.inputs['Base Color'])
    shader.inputs['Roughness'].default_value = .88
    return material


def paint_fabric(part):
    mesh = part.data
    layer = mesh.color_attributes.new(name='Fabric', type='BYTE_COLOR', domain='CORNER')
    region = mesh.uv_layers.new(name='FabricRegion')
    pin = part.vertex_groups.get('ClothPin') or part.vertex_groups.new(name='ClothPin')
    loops = boundary_loops(part)
    if part.name == 'Shirt':
        neck = max((loop for loop in loops if abs(sum(mesh.vertices[i].co.x for i in loop) / len(loop)) < .08),
                   key=lambda loop: sum(mesh.vertices[i].co.z for i in loop) / len(loop))
        tree = KDTree(len(neck))
        for j, i in enumerate(neck):
            tree.insert(mesh.vertices[i].co, j)
        tree.balance()
        mobility = [float(smoothstep((tree.find(vertex.co)[2] - .012) / .055)) for vertex in mesh.vertices]
        for vertex in mesh.vertices:
            # Keep the shoulder supported rather than letting the whole tee
            # collapse around a neck-only attachment. The hem stays free.
            lower = float(smoothstep((1.30 - vertex.co.z) / .30))
            sleeve = float(smoothstep((abs(vertex.co.x) - .21) / .13))
            mobility[vertex.index] *= max(.20 + .80 * lower, .32 * sleeve)
        tint = (.80, .78, .73)
    else:
        waist = max(vertex.co.z for vertex in mesh.vertices)
        mobility = [.60 * float(smoothstep((waist - vertex.co.z - .012) / .065)) for vertex in mesh.vertices]
        tint = (.018, .095, .045)
    for vertex, value in zip(mesh.vertices, mobility):
        pin.add([vertex.index], 1 - value, 'REPLACE')
    for loop in mesh.loops:
        layer.data[loop.index].color = (*tint, mobility[loop.vertex_index])
        region.data[loop.index].uv = (0 if part.name == 'Shirt' else 1, 0)
    mesh.color_attributes.active_color = layer


def free_waist(part):
    # Garments shared the scan's waist vertices. Move them onto separate
    # overlapping surfaces so a cloth weld cannot stitch tee to shorts.
    for vertex in part.data.vertices:
        x, y, z = vertex.co
        radial = Vector((x, y - .025, 0))
        if radial.length < .01:
            continue
        if part.name == 'Shirt':
            blend = float(smoothstep((1.06 - z) / .10))
            vertex.co += radial.normalized() * (.005 * blend)
            vertex.co.z -= .012 * blend
        else:
            blend = float(smoothstep((z - .88) / .075))
            vertex.co -= radial.normalized() * (.004 * blend)
            vertex.co.z += .018 * blend


def fit_shoes(parts, arm):
    contacts = []
    for side, suffix in (('Left', 'L'), ('Right', 'R')):
        shoe = next(part for part in parts if part.name == 'Shoe_' + suffix)
        ankle = arm.data.bones[side + 'Foot'].head_local.copy()
        toe = arm.data.bones[side + 'ToeBase'].head_local.copy()
        forward = Vector((toe.x - ankle.x, toe.y - ankle.y, 0)).normalized()
        projections = [(vertex.co - ankle).dot(forward) for vertex in shoe.data.vertices]
        factor = .282 / (max(projections) - min(projections))
        for vertex in shoe.data.vertices:
            projection = (vertex.co - ankle).dot(forward)
            vertex.co += forward * projection * (factor - 1)
        low = float(np.quantile([vertex.co.z for vertex in shoe.data.vertices], .25))
        sole = [vertex.co for vertex in shoe.data.vertices if vertex.co.z <= low]
        # This landmark is an actual rubber-surface vertex. Combining a toe
        # projection with a separate heel height would put it below the shoe.
        point = max(sole, key=lambda point: (point - ankle).dot(forward)).copy()
        contacts.append((side, point))
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.select_all(action='DESELECT')
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for side, point in contacts:
        bone = arm.data.edit_bones.new(side + 'SoleContact')
        bone.head = point
        bone.tail = point + Vector((0, 0, .02))
        bone.parent = arm.data.edit_bones[side + 'Foot']
        bone.use_deform = False
    face = next(part for part in parts if part.name == 'Head_Face')
    eyes = arm.data.edit_bones.new('PlayerEyes')
    eyes.head = Vector((0, min(vertex.co.y for vertex in face.data.vertices) + .032,
                        max(vertex.co.z for vertex in face.data.vertices) - .11))
    eyes.tail = eyes.head + Vector((0, -.02, 0))
    eyes.parent = arm.data.edit_bones['Head']
    eyes.use_deform = False
    bpy.ops.object.mode_set(mode='OBJECT')


def collision_definition(arm, body):
    """One set of shapes drives the covered skin and the Unreal cloth asset."""
    head = lambda name: arm.data.bones[name].head_local.copy()
    origin = head('Hips')
    side = (head('LeftArm') - head('RightArm')).normalized()
    up = (head('Head') - origin)
    up = (up - side * up.dot(side)).normalized()
    front = side.cross(up).normalized()
    capsules = []

    def add(bone, a, b, radius, bounded=True):
        a, b = a.copy(), b.copy()
        if bounded and (b - a).length > 1e-6:
            axis = (b - a).normalized()
            margin = min(radius, (b - a).length * .5)
            a += axis * margin
            b -= axis * margin
        capsules.append({'bone': bone, 'a': list(a), 'b': list(b), 'radius': radius})

    for offset in (-.06, 0, .06):
        add('Hips', origin + side * offset, head('Spine2') + side * offset, .10)
    pelvis = origin - Vector((0, 0, .06)) + Vector((0, -.035, 0))
    add('Hips', pelvis - side * .07, pelvis + side * .07, .10, False)
    chest = head('Spine2') + Vector((0, 0, .05))
    add('Spine2', chest - side * .08, chest + side * .08, .09, False)
    add('Neck', head('Neck'), head('Head'), .041)
    for prefix in ('Left', 'Right'):
        add(prefix + 'Arm', head(prefix + 'Arm'), head(prefix + 'ForeArm'), .045)
        add(prefix + 'Arm', head(prefix + 'Arm'), head(prefix + 'Arm'), .049)
        add(prefix + 'ForeArm', head(prefix + 'ForeArm'), head(prefix + 'Hand'), .038)
        add(prefix + 'UpLeg', head(prefix + 'UpLeg'), head(prefix + 'Leg'), .076)
        add(prefix + 'Leg', head(prefix + 'Leg'), head(prefix + 'Foot'), .049)
    # Every covered skin vertex belongs to a shape that encloses it with a
    # safety margin, and follows that shape's bone. A cloth collider should
    # never describe a smaller body than the visible foundation underneath it.
    for vertex in body.data.vertices:
        best = None
        for shape in capsules:
            a, b = Vector(shape['a']), Vector(shape['b'])
            axis = b - a
            t = max(0, min(1, (vertex.co - a).dot(axis) / axis.length_squared)) if axis.length_squared > 1e-10 else 0
            center = a + axis * t
            delta = vertex.co - center
            distance = delta.length - shape['radius']
            if best is None or distance < best[0]:
                best = (distance, center, shape)
        distance, center, shape = best
        if distance > -.010:
            axis = (vertex.co - center).normalized()
            vertex.co = center + axis * (shape['radius'] - .010)
        set_weights(body, vertex, {shape['bone']: 1})
    data = {'origin': list(origin), 'side': list(side), 'front': list(front), 'up': list(up), 'capsules': capsules}
    (ROOT / 'unreal/Content/Data/modular-player-collision.json').write_text(json.dumps(data, indent=2) + '\n')


def export_player(parts, arm, prepare_copy=None):
    # Keep the modular master separate. The runtime has one skinned component
    # and one shared outfit simulation, avoiding duplicate body/cloth updates.
    for part in parts:
        group = part.vertex_groups.get('ClothPin')
        if group:
            part.vertex_groups.remove(group)
    bpy.ops.object.select_all(action='DESELECT')
    copies = []
    for part in parts:
        obj = part.copy()
        obj.data = part.data.copy()
        if prepare_copy:
            prepare_copy(obj)
        bpy.context.collection.objects.link(obj)
        obj.select_set(True)
        copies.append(obj)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    combined = bpy.context.object
    combined.name = 'climber'
    arm.select_set(True)
    # These are opaque scanned surfaces, including the mesh buzz cut. Do not
    # emit an extra alpha texture object sharing the diffuse FBX identity.
    for material in combined.data.materials:
        if material and material.use_nodes:
            shader = next((node for node in material.node_tree.nodes if node.type == 'BSDF_PRINCIPLED'), None)
            if shader:
                for link in list(shader.inputs['Alpha'].links):
                    material.node_tree.links.remove(link)
                shader.inputs['Alpha'].default_value = 1
    # Authoring materials keep their texture graphs. The FBX carries plain
    # slot identities only; textures are imported once from this manifest.
    # FBX otherwise creates clashing texture objects for shared atlases.
    def texture(socket, visited=None):
        visited = set() if visited is None else visited
        if not socket.is_linked:
            return None
        node = socket.links[0].from_node
        if node in visited:
            return None
        visited.add(node)
        if node.type == 'TEX_IMAGE':
            return node.image
        for input in node.inputs:
            result = texture(input, visited)
            if result:
                return result
        return None
    manifest = {'materials': {}, 'images': {}}
    originals, placeholders = [], []
    for index, material in enumerate(list(combined.data.materials)):
        name = material.name
        shader = next(node for node in material.node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
        spec = {'color': list(shader.inputs['Base Color'].default_value)}
        for role, socket in [('color_texture', 'Base Color'), ('normal_texture', 'Normal'), ('roughness_texture', 'Roughness')]:
            image = texture(shader.inputs[socket])
            if image:
                key = 'Atlas_' + image.name.replace('.', '_').replace('/', '_')
                spec[role] = key
                if key not in manifest['images']:
                    image.filepath_raw = str(OUT / (key + '.png'))
                    image.file_format = 'PNG'
                    image.save()
                    manifest['images'][key] = {'file': key + '.png', 'normal': role == 'normal_texture', 'srgb': role == 'color_texture'}
        manifest['materials'][name] = spec
        material.name = name + '_Authoring'
        placeholder = bpy.data.materials.new(name)
        placeholder.use_nodes = True
        placeholder.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value = tuple(spec['color'])
        combined.data.materials[index] = placeholder
        originals.append((material, name))
        placeholders.append(placeholder)
    (OUT / 'materials.json').write_text(json.dumps(manifest, indent=2) + '\n')
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(filepath=str(OUT / 'climber.fbx'), use_selection=True,
                            object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                            bake_anim=False, path_mode='STRIP', embed_textures=False,
                            axis_forward='-Y', axis_up='Z', armature_nodetype='NULL',
                            mesh_smooth_type='FACE', colors_type='LINEAR')
    bpy.data.objects.remove(combined, do_unlink=True)
    for placeholder in placeholders:
        bpy.data.materials.remove(placeholder)
    for material, name in originals:
        material.name = name


def render(parts, arm, name, back=False, explode=False):
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = 1100
    scene.render.resolution_y = 1100
    scene.render.resolution_percentage = 100
    offsets = {}
    if explode:
        for part in parts:
            offsets[part.name] = part.location.copy()
            if part.name.startswith('Head'):
                part.location.z += .20
            elif part.name.startswith('Arm'):
                part.location.x += .16 if '_L' in part.name else -.16
            elif part.name == 'Shirt':
                part.location.y -= .30
            elif part.name == 'Shorts':
                part.location.y -= .20
            elif part.name.startswith('Shoe'):
                part.location.z -= .13
    bpy.ops.object.camera_add(location=(2.4 if explode else .6, 5 if back else -5, 1.9))
    cam = bpy.context.object
    cam.rotation_euler = (Vector((0, 0, .88)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = 2.25 if explode else 1.98
    scene.camera = cam
    scene.render.filepath = str(PROOF / (name + '.png'))
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)
    for part in parts:
        if part.name in offsets:
            part.location = offsets[part.name]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    PROOF.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene = bpy.context.scene
    source = bpy.data.objects['Mesh_0']
    arm = next(obj for obj in scene.objects if obj.type == 'ARMATURE')
    original_material = source.data.materials[0]
    coords, labels = classify(source)
    world = arm.matrix_world.copy()
    arm.parent = None
    arm.matrix_world = world
    bpy.ops.object.select_all(action='DESELECT')
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    source.parent = None
    source.matrix_world = Matrix.Identity(4)
    for vertex, point in zip(source.data.vertices, coords):
        vertex.co = point
    for obj in list(scene.objects):
        if obj not in (source, arm):
            bpy.data.objects.remove(obj, do_unlink=True)
    source.data.materials.clear()
    for name in REGIONS:
        material = bpy.data.materials.new('Region_' + name)
        source.data.materials.append(material)
    for face, label in zip(source.data.polygons, labels):
        face.material_index = int(label)
    weld_and_fair(source)
    aliases(arm, [source])
    # The auto-rig placed the head pivot at the base of the neck. Put the
    # rotation center near the skull base while preserving the rest mesh.
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.select_all(action='DESELECT')
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    head_bone = arm.data.edit_bones['Head']
    delta = Vector((0, .045, .060))
    head_bone.head += delta
    head_bone.tail += delta
    arm.data.edit_bones['Neck'].tail = head_bone.head
    bpy.ops.object.mode_set(mode='OBJECT')
    parts = []
    for region, name in enumerate(REGIONS):
        if name in ('Arms', 'Legs', 'Shoes'):
            for suffix, side in (('L', 1), ('R', -1)):
                parts.append(extract(source, region, name[:-1] + '_' + suffix, side))
        else:
            parts.append(extract(source, region, 'Head_Original' if name == 'Head' else name))
    fabric = fabric_material(original_material)
    skin_materials = {}
    for part in parts:
        part.data.materials.clear()
        if part.name in ('Shirt', 'Shorts'):
            part.data.materials.append(fabric)
            limit_weights(part)
            free_waist(part)
            reduce(part, 10000 if part.name == 'Shirt' else 6000)
            limit_weights(part)
            paint_fabric(part)
        else:
            name = 'Skin_Head' if part.name.startswith('Head') else 'Skin_Arms' if part.name.startswith('Arm') else 'Skin_Legs' if part.name.startswith('Leg') else 'Climbing_Shoes'
            material = skin_materials.get(name)
            if material is None:
                material = original_material.copy()
                material.name = name
                skin_materials[name] = material
            part.data.materials.append(material)
            if part.name.startswith('Arm'):
                extend_skin(part, Vector((-1 if '_L' in part.name else 1, 0, .10)).normalized(), .045, arm)
            elif part.name.startswith('Leg'):
                extend_skin(part, lambda p: Vector((0, 0, 1 if p.z > .40 else -1)), .035, arm)
            limit_weights(part)
        part.parent = arm
        part.matrix_world = Matrix.Identity(4)
        part['region'] = 'Head' if part.name.startswith('Head') else part.name.split('_')[0]
    old_head = next(part for part in parts if part.name == 'Head_Original')
    new_head = fit_head(old_head, arm)
    parts.remove(old_head)
    parts.append(new_head)
    parts.append(add_collar(next(part for part in parts if part.name == 'Shirt'), arm))
    parts.append(add_underbody(arm, next(part for part in parts if part.name == 'Shirt')))
    bpy.data.objects.remove(source, do_unlink=True)
    visible_points = [vertex.co.z for part in parts for vertex in part.data.vertices]
    scale = HEIGHT / (max(visible_points) - min(visible_points))
    for part in parts:
        for vertex in part.data.vertices:
            vertex.co *= scale
        if 'skin_cuff_z' in part:
            part['skin_cuff_z'] *= scale
    for vertex in old_head.data.vertices:
        vertex.co *= scale
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.select_all(action='DESELECT')
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for bone in arm.data.edit_bones:
        bone.head *= scale
        bone.tail *= scale
    bpy.ops.object.mode_set(mode='OBJECT')
    fit_shoes(parts, arm)
    collision_definition(arm, next(part for part in parts if part.name == 'Body_Underlay'))
    arm.name = 'PlayerRig'
    for part in parts:
        deform = next((modifier for modifier in part.modifiers if modifier.type == 'ARMATURE'), None)
        if deform:
            deform.object = arm
    scene.world = bpy.data.worlds.new('PlayerReviewWorld')
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value = (.15, .15, .15, 1)
    for location, power in (((2, -4, 4), 650), ((-3, -1, 2), 400), ((1, 3, 3), 700)):
        bpy.ops.object.light_add(type='AREA', location=location)
        light = bpy.context.object
        light.data.energy = power
        light.data.shape = 'DISK'
        light.data.size = 3
        light.rotation_euler = (Vector((0, 0, 1)) - light.location).to_track_quat('-Z', 'Y').to_euler()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'player.blend'))
    report = {'height_m': HEIGHT, 'bones': len(arm.data.bones), 'parts': {part.name: {'vertices': len(part.data.vertices), 'triangles': len(part.data.polygons), 'boundary_loops': len(boundary_loops(part))} for part in parts}}
    (PROOF / 'separation.json').write_text(json.dumps(report, indent=2) + '\n')
    render(parts, arm, 'assembled-front')
    render(parts, arm, 'assembled-back', back=True)
    render(parts, arm, 'separated-parts', explode=True)
    export_player(parts, arm)
    print('MODULAR PLAYER SEPARATED:', json.dumps(report), flush=True)


if __name__ == '__main__':
    main()

"""Remove thigh drag from texture-identified shirt hem; no geometry changes.
Run only during a fresh source build, never repeatedly on a fitted asset.
"""
import numpy as np
from mathutils.kdtree import KDTree


def fit_hem(mesh):
    shader = next(n for n in mesh.data.materials[0].node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    image = shader.inputs['Base Color'].links[0].from_node.image
    pixels = np.asarray(image.pixels[:]).reshape(image.size[1], image.size[0], 4)
    uv = np.empty(len(mesh.data.loops)*2, dtype=np.float32)
    mesh.data.uv_layers.active.data.foreach_get('uv', uv)
    width, height = image.size
    white = np.zeros(len(mesh.data.vertices))
    for loop in mesh.data.loops:
        u, v = uv[loop.index*2:loop.index*2+2]
        r, g, b = pixels[min(height-1, max(0, int(v*height))), min(width-1, max(0, int(u*width)))][:3]
        if g > .26 and b > .19 and r < g*1.27 and g < b*1.55:
            white[loop.vertex_index] = 1
    def smooth(a, b, x):
        t = min(1, max(0, (x-a)/(b-a)))
        return t*t*(3-2*t)
    tree = KDTree(len(mesh.data.vertices))
    for v in mesh.data.vertices: tree.insert(v.co, v.index)
    tree.balance()
    active = [v for v in mesh.data.vertices if .79 < v.co.z < 1.20 and abs(v.co.x) < .35]
    near = {v.index:[(i,max(0,1-d/.03)**2) for _,i,d in tree.find_range(v.co,.03)] for v in active}
    for _ in range(4):
        previous = white.copy()
        for i, neighbours in near.items():
            white[i] = sum(previous[j]*w for j,w in neighbours)/sum(w for _,w in neighbours)
    changed = []; removed = 0
    for v in active:
        x, y, z = v.co
        # A soft material mask includes shadowed fabric and avoids hard weight
        # seams. Its narrow border can include adjoining shorts on this fused mesh.
        strength = white[v.index]*smooth(.79,.87,z)*(1-smooth(1.06,1.16,z))*(1-smooth(.25,.35,abs(x)))
        weights = {mesh.vertex_groups[g.group].name:g.weight for g in v.groups}
        amount = 0
        for n in ('LeftUpLeg','RightUpLeg'):
            delta = weights.get(n,0)*strength
            weights[n] = weights.get(n,0)-delta; amount += delta
        if amount < 1e-6: continue
        weights['Hips'] = weights.get('Hips',0)+amount*.8
        weights['Spine'] = weights.get('Spine',0)+amount*.2
        for group_index in [g.group for g in v.groups]:
            mesh.vertex_groups[group_index].remove([v.index])
        keep = sorted(((n,w) for n,w in weights.items() if w>1e-6),key=lambda x:-x[1])[:4]
        total = sum(w for _,w in keep)
        for n,w in keep: mesh.vertex_groups[n].add([v.index],w/total,'REPLACE')
        changed.append(v.index); removed += amount
    assert changed, 'no shirt vertices selected'
    worst = max(abs(sum(g.weight for g in v.groups)-1) for v in mesh.data.vertices)
    assert worst < 1e-4, f'invalid weight sum: {worst}'
    return {'changed_vertices':len(changed),'mean_thigh_weight_removed':removed/len(changed),
            'vertex_indices':changed,'geometry_unchanged':True,'max_influences':max(len(v.groups) for v in mesh.data.vertices)}

"""Author sewn cotton panels and close-cropped fibers in the CC4 T-pose."""
import bpy
import math
import random
import bmesh
from mathutils import Vector, geometry, kdtree


def replace_tee(arm, old):
    # Pattern outline, clockwise from the left neck over the shoulder.
    outline = [(-.080,1.555),(-.205,1.568),(-.425,1.568),(-.425,1.420),
               (-.235,1.415),(-.220,1.020),(.220,1.020),(.235,1.415),
               (.425,1.420),(.425,1.568),(.205,1.568),(.080,1.555)]
    for i in range(1,12):
        angle=math.pi*i/12
        outline.append((.080*math.cos(angle),1.555-.030*math.sin(angle)))
    # Uniformly sampled seams give the simulation predictable edge lengths.
    boundary=[];edge_kind=[]
    for i,a in enumerate(outline):
        b=outline[(i+1)%len(outline)]
        length=math.hypot(b[0]-a[0],b[1]-a[1])
        steps=max(1,round(length/.019))
        kind='open' if i in (2,5,8) or i>=11 else 'seam'
        for j in range(steps):
            t=j/steps
            boundary.append((a[0]*(1-t)+b[0]*t,a[1]*(1-t)+b[1]*t))
            edge_kind.append(kind)
    def inside(x,z):
        result=False
        for a,b in zip(outline,outline[1:]+outline[:1]):
            if (a[1]>z)!=(b[1]>z) and x<(b[0]-a[0])*(z-a[1])/(b[1]-a[1])+a[0]:result=not result
        return result
    points=[Vector(p) for p in boundary]
    for iz in range(31):
        z=1.025+iz*.019
        for ix in range(46):
            x=-.4275+ix*.019
            if inside(x,z):points.append(Vector((x,z)))
    edges=[(i,(i+1)%len(boundary)) for i in range(len(boundary))]
    verts2,_,triangles,_,_,_=geometry.delaunay_2d_cdt(points,edges,[list(range(len(boundary)))],1,1e-6)
    verts=[]
    for back in (False,True):
        for p in verts2:
            x,z=p
            sleeve=max(0,min(1,(abs(x)-.20)/.07))
            torso_depth=.173*math.sqrt(max(.025,1-(x/.24)**2))
            sleeve_depth=.088*math.sqrt(max(.015,1-((z-1.505)/.086)**2))
            depth=torso_depth*(1-sleeve)+sleeve_depth*sleeve
            if z>1.515 and abs(x)<.090:
                neck_blend=max(0,min(1,(z-1.515)/.025))
                neck_depth=.081*math.sqrt(max(.02,1-(x/.083)**2))
                depth=depth*(1-neck_blend)+neck_depth*neck_blend
            neck=max(0,1-abs(x)/.10)*max(0,min(1,(z-1.52)/.035))
            zz=z+(.038*neck if back else 0)
            verts.append((x,.028+.026*sleeve+(depth if back else -depth),zz))
    n=len(verts2)
    faces=[tuple(t) for t in triangles]+[tuple(reversed([i+n for i in t])) for t in triangles]
    def closest(p):return min(range(n),key=lambda i:(verts2[i]-Vector(p)).length_squared)
    for i,kind in enumerate(edge_kind):
        if kind=='seam':
            a=closest(boundary[i]);b=closest(boundary[(i+1)%len(boundary)])
            faces.append((a,a+n,b+n,b))
    data=bpy.data.meshes.new('Sewn tee panels');data.from_pydata(verts,[],faces);data.update()
    tee=bpy.data.objects.new('Cotton_Tee',data);bpy.context.collection.objects.link(tee)
    tee.data.materials.append(old.data.materials[0]);tee.parent=arm
    tree=kdtree.KDTree(len(old.data.vertices))
    for v in old.data.vertices:tree.insert(v.co,v.index)
    tree.balance()
    for g in old.vertex_groups:tee.vertex_groups.new(name=g.name)
    for v in data.vertices:
        weights={}
        for _,index,distance in tree.find_n(v.co,6):
            for group in old.data.vertices[index].groups:
                name=old.vertex_groups[group.group].name
                if not name.startswith(('Hem','Sleeve')):
                    weights[name]=weights.get(name,0)+group.weight/(distance+.008)**2
        # Sleeve follows the upper arm, including the surface away from skin.
        x,y,z=v.co
        arm_weight=max(0,min(1,(abs(x)-.207)/.115))
        arm_weight=arm_weight*arm_weight*(3-2*arm_weight)
        total=sum(weights.values())
        weights={name:value/total*(1-arm_weight) for name,value in weights.items()}
        arm_name='LeftArm' if x>0 else 'RightArm'
        weights[arm_name]=weights.get(arm_name,0)+arm_weight
        for name,weight in weights.items():
            if weight>1e-5:tee.vertex_groups[name].add([v.index],weight,'REPLACE')
    for f in data.polygons:f.use_smooth=True
    bpy.context.view_layer.objects.active=tee
    sub=tee.modifiers.new('Smooth sewn seams','SUBSURF');sub.levels=1;bpy.ops.object.modifier_apply(modifier=sub.name)
    modifier=tee.modifiers.new('CC4 weights','ARMATURE');modifier.object=arm
    # Sew ribbing directly onto the garment's actual neckline boundary.
    bm=bmesh.new();bm.from_mesh(tee.data)
    boundary_edges=[e for e in bm.edges if e.is_boundary]
    adjacency={}
    for edge in boundary_edges:
        a,b=edge.verts
        adjacency.setdefault(a,[]).append(b);adjacency.setdefault(b,[]).append(a)
    remaining=set(adjacency);loops=[]
    while remaining:
        start=remaining.pop();loop=[start];previous=None;current=start
        while True:
            choices=[v for v in adjacency[current] if v is not previous]
            following=next((v for v in choices if v in remaining),None)
            if following is None:break
            loop.append(following);remaining.remove(following);previous,current=current,following
        loops.append(loop)
    neck=max((loop for loop in loops if abs(sum(v.co.x for v in loop)/len(loop))<.06),key=lambda loop:sum(v.co.z for v in loop)/len(loop))
    collar_verts=[tuple(v.co) for v in neck]
    neck_indices=[v.index for v in neck]
    count=len(neck)
    for v in neck:
        point=v.co.copy();point.x*=.90;point.y=.028+(point.y-.028)*.90;point.z+=.006
        collar_verts.append(tuple(point))
    collar_faces=[(i,(i+1)%count,(i+1)%count+count,i+count) for i in range(count)]
    bm.free()
    collar_data=bpy.data.meshes.new('Sewn rib collar');collar_data.from_pydata(collar_verts,[],collar_faces);collar_data.update()
    collar=bpy.data.objects.new('Crew_Neck_Ribbing',collar_data);bpy.context.collection.objects.link(collar);collar.parent=arm
    material=old.data.materials[0].copy();material.name='Ivory_Collar_Rib';collar.data.materials.append(material)
    for group in tee.vertex_groups:collar.vertex_groups.new(name=group.name)
    for i,source in enumerate(neck_indices):
        for group in tee.data.vertices[source].groups:
            collar.vertex_groups[group.group].add([i,i+count],group.weight,'REPLACE')
    material.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.62,.605,.57,1)
    for f in collar_data.polygons:f.use_smooth=True
    bpy.context.view_layer.objects.active=collar
    thickness=collar.modifiers.new('Rib thickness','SOLIDIFY');thickness.thickness=.002;bpy.ops.object.modifier_apply(modifier=thickness.name)
    modifier=collar.modifiers.new('Neck anchor','ARMATURE');modifier.object=arm
    bpy.data.objects.remove(old,do_unlink=True)
    return tee


def replace_hair(arm, scalp):
    rng=random.Random(87)
    scalp.data.calc_loop_triangles()
    verts=[];faces=[]
    for triangle in scalp.data.loop_triangles:
        a,b,c=[scalp.data.vertices[i].co for i in triangle.vertices]
        normal=(b-a).cross(c-a).normalized()
        area=(b-a).cross(c-a).length*.5
        count=int(area*400000+rng.random())
        for _ in range(count):
            u=math.sqrt(rng.random());v=rng.random();root=a*(1-u)+b*(u*(1-v))+c*(u*v)+normal*.0002
            tangent=normal.cross(Vector((0,1,0))).normalized()
            bitangent=normal.cross(tangent)
            length=rng.uniform(.0012,.0023)
            tip=root+normal*length
            first=len(verts)
            for j in range(3):
                angle=j*2*math.pi/3
                verts.append(root+(tangent*math.cos(angle)+bitangent*math.sin(angle))*.00016)
            verts.append(tip)
            faces.extend([(first,first+1,first+3),(first+1,first+2,first+3),(first+2,first,first+3)])
    data=bpy.data.meshes.new('Buzz fibers');data.from_pydata(verts,[],faces);data.update()
    hair=bpy.data.objects.new('Buzz_Cut_Fibers',data);bpy.context.collection.objects.link(hair);hair.parent=arm
    material=bpy.data.materials.new('Brown_Buzz_Fibers');material.use_nodes=True
    shader=material.node_tree.nodes.get('Principled BSDF');shader.inputs['Base Color'].default_value=(.12,.065,.03,1);shader.inputs['Roughness'].default_value=.9
    data.materials.append(material);hair.vertex_groups.new(name='Head').add(list(range(len(verts))),1,'REPLACE')
    mod=hair.modifiers.new('Scalp binding','ARMATURE');mod.object=arm
    # Keep the scalp shell under the fibers as the dark shadow of a buzz cut; the fibers alone read as bald.
    scalp.name='Buzz_Cut'
    base=bpy.data.materials.new('Brown_Buzz_Scalp');base.use_nodes=True
    shader=base.node_tree.nodes.get('Principled BSDF');shader.inputs['Base Color'].default_value=(.085,.05,.03,1);shader.inputs['Roughness'].default_value=.82
    scalp.data.materials.clear();scalp.data.materials.append(base)
    return hair

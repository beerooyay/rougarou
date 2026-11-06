"""Blender --background --python unreal/Tools/rebuild_character.py
Rebuild a clean deform skeleton and weights, then retarget local Mixamo clips.
Sources and generated likeness assets deliberately stay in ignored assets/.
"""
import bpy
import sys
from pathlib import Path
from mathutils import Matrix, Vector
import numpy as np
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/character/locomotion'
OUT.mkdir(parents=True, exist_ok=True)
LIB = ROOT.parent / 'rawah/rawah-lowpoly/assets/characters/anims'
EXTRA = ROOT.parent / 'rawah/rawah-unused/clips'
def export(path, objects, anim=False):
    if not anim:
        # FBX emits a texture object per material. Give each one a unique
        # name/path so UE's commandlet never opens a texture-clash log tab.
        for obj in objects:
            if obj.type!='MESH':continue
            for mat in obj.data.materials:
                for node in mat.node_tree.nodes:
                    if node.type!='TEX_IMAGE' or not node.image:continue
                    image=node.image.copy();image.name=mat.name+'_'+node.name
                    image.filepath_raw=str(OUT/(image.name+'.png'));image.file_format='PNG';image.save()
                    node.image=image
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.select_set(True)
    bpy.context.view_layer.objects.active=arm
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'ARMATURE','MESH'},add_leaf_bones=False,
        bake_anim=anim,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,
        path_mode='COPY',embed_textures=not anim,axis_forward='-Y',axis_up='Z',armature_nodetype='NULL',mesh_smooth_type='FACE')
if '--rebake' in sys.argv:
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'climber.blend'))
    arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
    mesh=next(o for o in bpy.context.scene.objects if o.type=='MESH')
    bpy.context.view_layer.objects.active=arm
    bpy.ops.object.mode_set(mode='EDIT')
    for n in ['HemFront','HemBack','HemLeft','HemRight']:arm.data.edit_bones[n].parent=arm.data.edit_bones['Hips']
    bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'climber.blend'))
    export(OUT/'climber.fbx',[arm,mesh])
else:
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'assets/character/skin/skin.blend'))
    old = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
    mesh = next(o for o in bpy.context.scene.objects if o.type == 'MESH')
    # Keep a game-sized control mesh before subdivision; the scan contains far
    # more triangles than its silhouette needs. Interpolated smooth normals and
    # one subdivision give a cleaner result without a million-vertex character.
    bpy.context.view_layer.objects.active=mesh
    bpy.ops.object.select_all(action='DESELECT');mesh.select_set(True)
    if len(mesh.data.polygons)>45000:
        reduce=mesh.modifiers.new('Game topology','DECIMATE');reduce.ratio=45000/len(mesh.data.polygons)
        bpy.ops.object.modifier_apply(modifier=reduce.name)
    print('CONTROL MESH',len(mesh.data.vertices),'vertices',len(mesh.data.polygons),'faces',flush=True)
    # Polish the scan without changing its UVs. The shoulder cap and sleeve need
    # their own contour; flat face normals made every source triangle visible.
    for face in mesh.data.polygons: face.use_smooth=True
    bpy.context.view_layer.objects.active=mesh
    bpy.ops.object.select_all(action='DESELECT');mesh.select_set(True)
    polish=mesh.modifiers.new('Surface polish','SMOOTH');polish.factor=.42;polish.iterations=4
    bpy.ops.object.modifier_apply(modifier=polish.name)
    # Sample the original atlas to separate fabric from skin for secondary motion.
    shader=next(n for n in mesh.data.materials[0].node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    img=shader.inputs['Base Color'].links[0].from_node.image
    pixels=np.array(img.pixels[:],dtype=np.float32).reshape((-1,4))
    colors=np.zeros((len(mesh.data.vertices),3),dtype=np.float32)
    for loop,uv in zip(mesh.data.loops,mesh.data.uv_layers.active.data):
        px=int(max(0,min(img.size[0]-1,uv.uv.x*img.size[0])))
        py=int(max(0,min(img.size[1]-1,uv.uv.y*img.size[1])))
        colors[loop.vertex_index]=pixels[py*img.size[0]+px,:3]
    shirt_ids=set();short_ids=set()
    for v in mesh.data.vertices:
        x,y,z=v.co;c=colors[v.index]
        if .78<z<1.53 and abs(x)<.59 and min(c)>.075 and max(c)-min(c)<.23:shirt_ids.add(v.index)
        elif .62<z<.96 and abs(x)<.36 and c[1]>=c[0]*.92 and max(c)<.38:short_ids.add(v.index)
    # Lower and open the shoulders while retaining the face and the arm lengths.
    def region(a,b,x):
        t=max(0,min(1,(x-a)/(b-a)));return t*t*(3-2*t)
    for v in mesh.data.vertices:
        x,y,z=v.co
        arm=region(.12,.27,abs(x))*region(1.15,1.30,z)
        v.co.x+=(1 if x>0 else -1)*.027*arm
        v.co.z-=.033*arm
        cap=region(.07,.16,abs(x))*(1-region(.31,.43,abs(x)))*region(1.27,1.43,z)*(1-region(1.53,1.58,z))
        v.co.z-=.028*cap
    # Preserve indices until garment masks and skin weights have been assigned.
    # A smooth subdivision is baked after binding.
    heads = {b.name: old.matrix_world @ b.head_local for b in old.data.bones}
    tails = {b.name: old.matrix_world @ b.tail_local for b in old.data.bones}
    parents = {b.name: b.parent.name if b.parent else None for b in old.data.bones}
    children = {b.name: [c.name for c in b.children] for b in old.data.bones}
    for name in heads:
        if name.startswith(('LeftArm','LeftForeArm','LeftHand','RightArm','RightForeArm','RightHand')):
            shift=Vector((.027 if name.startswith('Left') else -.027,0,-.033))
            heads[name]+=shift;tails[name]+=shift
        elif name in ('LeftShoulder','RightShoulder'):
            tails[name]+=Vector((.027 if name.startswith('Left') else -.027,0,-.033))
    # Garment bones are deliberately independent of the limb solver. Only fabric
    # vertices receive these weights; skin and joint lengths stay fixed.
    cloth={
     'HemFront':('Hips',Vector((0,-.13,.94))), 'HemBack':('Hips',Vector((0,.13,.94))),
     'HemLeft':('Hips',Vector((.23,0,.94))), 'HemRight':('Hips',Vector((-.23,0,.94))),
     'SleeveLeft':('LeftArm',Vector((.40,.05,1.35))), 'SleeveRight':('RightArm',Vector((-.40,.05,1.35))),
     'CuffLeft':('LeftUpLeg',Vector((.15,0,.70))), 'CuffRight':('RightUpLeg',Vector((-.15,0,.70)))}
    for name,(parent,point) in cloth.items():
        heads[name]=point;tails[name]=point+Vector((0,0,.05));parents[name]=parent;children[name]=[]
    original_weights = {v.index: {mesh.vertex_groups[g.group].name:g.weight for g in v.groups} for v in mesh.data.vertices}
    mesh.parent = None
    for m in list(mesh.modifiers):
        if m.type == 'ARMATURE': mesh.modifiers.remove(m)
    mesh.vertex_groups.clear()
    for o in list(bpy.context.scene.objects):
        if o != mesh: bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.object.armature_add()
    arm = bpy.context.object
    arm.name = 'ClimberRig'
    bpy.ops.object.mode_set(mode='EDIT')
    arm.data.edit_bones.remove(arm.data.edit_bones[0])
    for name in heads:
        b = arm.data.edit_bones.new(name)
        b.head = heads[name]
        # Explicit joint axes rather than inherited scale/shear from the donor rig.
        kids = children[name]
        b.tail = heads[kids[0]] if len(kids) == 1 else tails[name]
        if (b.tail-b.head).length < .005: b.tail = b.head + Vector((0,0,.015))
        b.align_roll(Vector((0,-1,0)))
    for name,p in parents.items():
        if p: arm.data.edit_bones[name].parent = arm.data.edit_bones[p]
    bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT')
    mesh.select_set(True); arm.select_set(True); bpy.context.view_layer.objects.active=arm
    # The scan is not watertight, so bone heat is ill-conditioned. Use anatomical
    # regions with smooth joint transitions; do not borrow weights across a sleeve,
    # between the legs, or through the torso from a nearby disconnected surface.
    mesh.parent=arm
    mesh.matrix_parent_inverse=Matrix.Identity(4)
    mod=mesh.modifiers.new('Deform','ARMATURE');mod.object=arm
    for name in heads: mesh.vertex_groups.new(name=name)
    def smooth(a,b,x):
        t=max(0,min(1,(x-a)/(b-a)));return t*t*(3-2*t)
    def pair(a,b,t):return {a:1-t,b:t}
    for v in mesh.data.vertices:
        x,y,z=v.co; side='Left' if x>0 else 'Right'; ax=abs(x)
        if ax>.16 and z>1.22:
            elbow=abs(heads[side+'ForeArm'].x);wrist=abs(heads[side+'Hand'].x)
            if ax>wrist+.025:
                weights={n:w for n,w in original_weights[v.index].items() if n.startswith(side+'Hand')}
                if sum(weights.values())<.01:weights={side+'Hand':1}
            elif ax>wrist-.045:weights=pair(side+'ForeArm',side+'Hand',smooth(wrist-.045,wrist+.025,ax))
            else:weights=pair(side+'Arm',side+'ForeArm',smooth(elbow-.065,elbow+.065,ax))
            shoulder=smooth(.16,.33,ax)
            weights={n:w*shoulder for n,w in weights.items()};weights['Spine2']=1-shoulder
        elif z<.87:
            if z>.72:
                t=smooth(.87,.72,z)*smooth(.025,.09,ax)
                weights=pair('Hips',side+'UpLeg',t)
            elif z>.28:
                weights=pair(side+'UpLeg',side+'Leg',1-smooth(.43,.57,z))
            elif z>.08:weights=pair(side+'Leg',side+'Foot',1-smooth(.10,.19,z))
            else:weights=pair(side+'Foot',side+'ToeBase',1-smooth(-.12,-.045,y))
        elif z>1.52:weights={'Head':1}
        elif z>1.40:weights=pair('Spine2','Head',smooth(1.40,1.52,z))
        elif z>1.21:weights=pair('Spine1','Spine2',smooth(1.21,1.38,z))
        elif z>1.04:weights=pair('Spine','Spine1',smooth(1.04,1.23,z))
        else:weights=pair('Hips','Spine',smooth(.87,1.07,z))
        if v.index in shirt_ids:
            if ax<.32 and z<1.20:
                weights=pair('Hips','Spine',smooth(.89,1.12,z))
            amount=(1-smooth(.90,1.10,z))*.45
            if ax>.32:
                cloth_name='SleeveLeft' if x>0 else 'SleeveRight'
                amount=max(amount,smooth(.34,.48,ax)*.26)
            else:
                cloth_name=('HemFront' if y<0 else 'HemBack') if abs(y)>.075 else ('HemLeft' if x>0 else 'HemRight')
            if amount>0:
                weights={n:w*(1-amount) for n,w in weights.items()};weights[cloth_name]=amount
        elif v.index in short_ids:
            amount=(1-smooth(.64,.73,z))*.20
            weights={n:w*(1-amount) for n,w in weights.items()};weights['CuffLeft' if x>0 else 'CuffRight']=amount
        weights=dict(sorted(weights.items(),key=lambda p:p[1],reverse=True)[:4]);total=sum(weights.values())
        if total<.0001:raise RuntimeError('unweighted vertex')
        for n,w in weights.items():
            if w>0:mesh.vertex_groups[n].add([v.index],w/total,'REPLACE')
    # Split the atlas into materials so skin and cloth have appropriate highlights.
    base=mesh.data.materials[0]
    for name,rough in [('skin',.53),('cotton',.93),('shorts',.86)]:
        mat=base.copy();mat.name=name
        bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        for socket,value in [('Roughness',rough),('Metallic',0)]:
            for link in list(bs.inputs[socket].links):mat.node_tree.links.remove(link)
            bs.inputs[socket].default_value=value
        mesh.data.materials.append(mat)
    for face in mesh.data.polygons:
        ids=face.vertices
        face.material_index=2 if sum(i in shirt_ids for i in ids)>=2 else 3 if sum(i in short_ids for i in ids)>=2 else 1
        face.use_smooth=True
    # Subdivision is baked before export, carrying the already normalized weights
    # and interpolated UVs. This improves the silhouette as well as the normals.
    sub=mesh.modifiers.new('Smooth silhouette','SUBSURF');sub.levels=1;sub.render_levels=1
    bpy.context.view_layer.objects.active=mesh
    bpy.ops.object.modifier_move_up(modifier=sub.name)
    bpy.ops.object.modifier_apply(modifier=sub.name)
    # Subdivision interpolates influences; prune and normalize again for UE.
    for v in mesh.data.vertices:
        weights=sorted([(g.group,g.weight) for g in v.groups if g.weight>.00001],key=lambda p:p[1],reverse=True)[:4]
        total=sum(w for _,w in weights)
        if total<.00001:raise RuntimeError('polish produced an unweighted vertex')
        for group_index in [g.group for g in v.groups]:mesh.vertex_groups[group_index].remove([v.index])
        for i,w in weights:mesh.vertex_groups[i].add([v.index],w/total,'REPLACE')
    filled=0
    mesh.name='climber' 
    print('RIG rebuilt:',len(arm.data.bones),'bones;',len(mesh.data.vertices),'vertices; filled',filled,flush=True)
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'climber.blend'))
    export(OUT/'climber.fbx',[arm,mesh])
rest={b.name:b.matrix_local.copy() for b in arm.data.bones}
local={b.name:(b.parent.matrix_local.inverted()@b.matrix_local if b.parent else b.matrix_local.copy()) for b in arm.data.bones}
clips={'idle':'Neutral Idle','walk':'Swagger Walk','jog':'Jogging','run':'Sprint','start':'Start Walking','stop':'Stop Walking'}
for name,file in clips.items():
    path=LIB/(file+'.fbx')
    if not path.exists():path=EXTRA/(file+'.fbx')
    before=set(bpy.data.objects);bpy.ops.import_scene.fbx(filepath=str(path))
    new=set(bpy.data.objects)-before; src=next(o for o in new if o.type=='ARMATURE')
    source={b.name.split(':')[-1]:b for b in src.data.bones}
    pb={b.name.split(':')[-1]:b for b in src.pose.bones}
    sr={n:src.matrix_world@b.matrix_local for n,b in source.items()}
    action=src.animation_data.action;first,last=map(int,action.frame_range)
    period={'walk':41,'jog':26,'run':16}.get(name)
    offset=0
    if period:
        # One full stride per sample; align left foot extension across gaits.
        candidates=[]
        for frame in range(first,first+period):
            bpy.context.scene.frame_set(frame)
            foot=(src.matrix_world@pb['LeftFoot'].matrix).translation
            hip=(src.matrix_world@pb['Hips'].matrix).translation
            candidates.append(((foot-hip).y,frame-first))
        offset=min(candidates)[1]
        last=first+period
    arm.animation_data_create(); baked=bpy.data.actions.new(name);arm.animation_data.action=baked
    scale=rest['Hips'].translation.z/sr['Hips'].translation.z
    # Evaluate source once per frame; solve desired rotations parent-first in
    # world space, then write LOCAL rotations, keeping bind joint offsets.
    for f in range(first,last+1):
        bpy.context.scene.frame_set(first+(f-first+offset)%period if period else f)
        desired={}
        for b in arm.data.bones:
            n=b.name;p=b.parent.name if b.parent else None
            q=(src.matrix_world@pb[n].matrix).to_quaternion()@sr[n].to_quaternion().inverted()@rest[n].to_quaternion() if n in pb else (desired[p]@local[n].to_quaternion() if p else rest[n].to_quaternion())
            desired[n]=q.normalized()
            pose=arm.pose.bones[n];pose.rotation_mode='QUATERNION'
            pose.rotation_quaternion=(local[n].to_quaternion().inverted() @ (desired[p].inverted()@q if p else q)).normalized()
            pose.location=(0,0,0);pose.scale=(1,1,1)
            if not p and n in pb:
                dz=((src.matrix_world@pb[n].matrix).translation.z-sr[n].translation.z)*scale
                pose.location=rest[n].to_quaternion().inverted()@Vector((0,0,dz))
            pose.keyframe_insert('rotation_quaternion',frame=f)
            pose.keyframe_insert('location',frame=f)
            pose.keyframe_insert('scale',frame=f)
    bpy.context.scene.frame_start=first;bpy.context.scene.frame_end=last
    export(OUT/(name+'.fbx'),[arm],True)
    print('BAKED',name,first,last,flush=True)
    # Render the actual weighted mesh, not just bone debug lines.
    if name in ('idle','walk','run'):
        bpy.context.scene.frame_set(first+(last-first)//3)
        bpy.ops.object.camera_add(location=(2,-5,1.5));cam=bpy.context.object
        cam.rotation_euler=(Vector((0,0,.95))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=2.2
        s=bpy.context.scene;s.camera=cam;s.render.engine='BLENDER_WORKBENCH';s.display.shading.light='STUDIO';s.display.shading.color_type='TEXTURE'
        s.render.resolution_x=800;s.render.resolution_y=900;s.render.resolution_percentage=100;s.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
        bpy.data.objects.remove(cam,do_unlink=True)
    for o in new:bpy.data.objects.remove(o,do_unlink=True)
    arm.animation_data.action=None
    bpy.data.actions.remove(baked)
print('CHARACTER COMPLETE',flush=True)

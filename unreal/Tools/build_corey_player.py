"""Blender background build: preserve Corey's CC4 body/bind weights, author an
relaxed tee, shorts, close-cropped hair and shoes; export a compatible rig.
Source and generated character assets remain local under ignored assets/.
"""
import bpy,math,sys,os,numpy as np
from pathlib import Path
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'assets/character/corey'
# Permit an isolated candidate build without replacing the active assets.
OUT=Path(os.environ.get('ROUGAROU_CHARACTER_OUTPUT',str(SOURCE)));OUT.mkdir(parents=True,exist_ok=True)
if (SOURCE/'source.blend').exists():bpy.ops.wm.open_mainfile(filepath=str(SOURCE/'source.blend'))
else:
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(SOURCE/'source.glb'));bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE/'source.blend'))
for o in list(bpy.data.objects):
 if o.type not in ('MESH','ARMATURE') or o.name=='Icosphere':bpy.data.objects.remove(o,do_unlink=True)
arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE');body=bpy.data.objects['CC_Base_Body'];box=bpy.data.objects['Boxers']
# Retain the CC4 bind and all twist/share joints; aliases let the existing native
# and web solvers drive it without rebuilding or heat-weighting the body.
aliases={'Hip':'Hips','Waist':'Spine','Spine01':'Spine1','Spine02':'Spine2','NeckTwist01':'Neck','Head':'Head'}
for side,prefix in [('L','Left'),('R','Right')]:
 for a,b in [('Clavicle','Shoulder'),('Upperarm','Arm'),('Forearm','ForeArm'),('Hand','Hand'),('Thigh','UpLeg'),('Calf','Leg'),('Foot','Foot'),('ToeBase','ToeBase')]:aliases[side+'_'+a]=prefix+b
 for f,d in [('Thumb','Thumb'),('Index','Index'),('Mid','Middle'),('Ring','Ring'),('Pinky','Pinky')]:
  for j in range(1,4):aliases[f'{side}_{f}{j}']=f'{prefix}Hand{d}{j}'
for old,new in aliases.items():
 b=arm.data.bones.get('CC_Base_'+old)
 if b:b.name=new
for o in [o for o in bpy.context.scene.objects if o.type=='MESH']:
 for old,new in aliases.items():
  g=o.vertex_groups.get('CC_Base_'+old)
  if g:g.name=new
# Bring bind data to meters in world axes while preserving deformation.
bpy.ops.object.select_all(action='DESELECT');arm.select_set(True)
for o in bpy.context.scene.objects:
 if o.type=='MESH':o.select_set(True)
bpy.context.view_layer.objects.active=arm;bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
source_height=max(v.co.z for v in body.data.vertices)-min(v.co.z for v in body.data.vertices)
for o in bpy.context.scene.objects:
 if o.type=='MESH':
  for f in o.data.polygons:f.use_smooth=True
# Slightly paler skin, preserving the original pore/detail maps. These are
# material texture bakes, not replacement skin geometry or repainted likeness.
for mat in body.data.materials:
 if not mat.name.startswith('Std_Skin'):continue
 bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED');bs.inputs['Roughness'].default_value=.62
 for n in mat.node_tree.nodes:
  if n.type=='TEX_IMAGE' and 'Diffuse' in n.image.name:
   image=n.image.copy();image.name=mat.name+'_Fair';pix=np.array(image.pixels[:],dtype=np.float32).reshape((-1,4));pix[:,:3]=np.clip(pix[:,:3]*[1.22,1.27,1.31]+[.018,.020,.022],0,1);image.pixels.foreach_set(pix.ravel());image.pack();n.image=image

def mat(name,color,rough=.85):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True;b=m.node_tree.nodes.get('Principled BSDF');b.inputs['Base Color'].default_value=(*color,1);b.inputs['Roughness'].default_value=rough;return m
teeMat=mat('Ivory_Cotton',(.72,.705,.66),.94);shortMat=mat('Forest_Green_Twill',(.018,.075,.042),.88);rubber=mat('Climbing_Shoe_Rubber',(.018,.022,.021),.8);yellow=mat('Ochre_Shoe_Panels',(.38,.26,.035),.75)

def extract(name,source,predicate,material,offset=0):
 faces=[f for f in source.data.polygons if predicate(f)];ids=sorted({i for f in faces for i in f.vertices});lookup={n:i for i,n in enumerate(ids)}
 verts=[source.matrix_world@source.data.vertices[i].co+source.matrix_world.to_3x3()@source.data.vertices[i].normal*offset for i in ids]
 data=bpy.data.meshes.new(name);data.from_pydata(verts,[],[[lookup[i] for i in f.vertices] for f in faces]);data.update()
 if source.data.uv_layers.active:
  uv=data.uv_layers.new(name='UVMap')
  for dst,src in zip(data.polygons,faces):
   for dl,sl in zip(dst.loop_indices,src.loop_indices):uv.data[dl].uv=source.data.uv_layers.active.data[sl].uv
 o=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(o)
 for g in source.vertex_groups:o.vertex_groups.new(name=g.name)
 for old,new in lookup.items():
  for g in source.data.vertices[old].groups:o.vertex_groups[g.group].add([new],g.weight,'REPLACE')
 o.data.materials.append(material);o.parent=arm;m=o.modifiers.new('Original CC4 deform','ARMATURE');m.object=arm
 for f in data.polygons:f.use_smooth=True
 return o

def center(f):return body.matrix_world@f.center
# Extract the connected upper-body topology, then loosen the silhouette. Unlike
# a color mask on a scan, fabric has its own coherent surface and boundaries.
def tee_face(f):
 c=center(f);name=body.data.materials[f.material_index].name
 return (name=='Std_Skin_Body' and .985<c.z<(1.54 if c.y<.03 else 1.56)) or (name=='Std_Skin_Arm' and abs(c.x)<.430 and c.z>1.29)
shirt=extract('Oversized_Tee',body,tee_face,teeMat,.014)
for v in shirt.data.vertices:
 x,y,z=v.co
 if abs(x)<.225:
  # Taper toward the waist and ease the fabric over the chest as one surface.
  blend=max(0,min(1,(1.515-z)/.10))
  angle=math.atan2((y-.025)/.145,x/.195)
  waist=max(0,min(1,(1.32-z)/.32))
  desired=Vector(((.217+.009*waist)*math.cos(angle),.025+(.145+.018*waist)*math.sin(angle),max(1.015,z)))
  v.co=v.co.lerp(desired,blend)
 else:
  sign=1 if x>0 else -1
  a=Vector((sign*.193,.058,1.509));d=Vector((sign*.276,.002,-.173)).normalized()
  t=(v.co-a).dot(d);axis=a+d*t;rad=v.co-axis
  if rad.length>1e-5:
   radius=.088+.005*max(0,min(1,t/.26))
   v.co=v.co.lerp(axis+rad.normalized()*radius,max(0,min(1,(abs(x)-.225)/.10)))
# Weld original material-boundary seams; smooth the cloth's underlying muscles.
bpy.context.view_layer.objects.active=shirt;bpy.ops.object.select_all(action='DESELECT');shirt.select_set(True);bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.remove_doubles(threshold=.001);bpy.ops.object.mode_set(mode='OBJECT')
smooth=shirt.modifiers.new('Loose cotton surface','SMOOTH');smooth.factor=.6;smooth.iterations=7;bpy.ops.object.modifier_move_up(modifier=smooth.name);bpy.ops.object.modifier_apply(modifier=smooth.name)
# Shape the collar boundary into a round crew neck.
import bmesh
bm=bmesh.new();bm.from_mesh(shirt.data)
neckverts={v for e in bm.edges if e.is_boundary for v in e.verts if v.co.z>1.465 and abs(v.co.x)<.146}
for v in neckverts:
 # Wider at the sides and lower at the front, with a smooth, single opening.
 angle=math.atan2((v.co.y-.031)/.089,v.co.x/.105)
 v.co=Vector((.105*math.cos(angle),.031+.089*math.sin(angle),1.548+.033*math.sin(angle)))
for v in {v for e in bm.edges if e.is_boundary for v in e.verts if v.co.z<1.035}:v.co.z=1.015
bm.to_mesh(shirt.data);bm.free()
# Stabilize the neckline and chest against shoulder swing. The outer
# shoulder and sleeves keep the original arm weights.
chest_group=shirt.vertex_groups['Spine2']
for v in shirt.data.vertices:
 x,y,z=v.co
 blend=.86*max(0,min(1,(.205-abs(x))/.105))*max(0,min(1,(z-1.31)/.16))
 if blend>0:
  spine_weight=sum(g.weight for g in v.groups if g.group==chest_group.index)
  for g in list(v.groups):
   shirt.vertex_groups[g.group].add([v.index],g.weight*(1-blend),'REPLACE')
  chest_group.add([v.index],spine_weight*(1-blend)+blend,'REPLACE')
# Use the body topology for both leg panels and the crotch. The original
# underwear had independent openings that widened under deep hip flexion.
def shorts_face(f):
 c=center(f);name=body.data.materials[f.material_index].name
 return name in ('Std_Skin_Body','Std_Skin_Leg') and .745<c.z<1.055
bpy.data.objects.remove(box,do_unlink=True)
box=extract('Forest_Shorts',body,shorts_face,shortMat,.014)
bpy.ops.object.select_all(action='DESELECT');box.select_set(True);bpy.context.view_layer.objects.active=box
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.remove_doubles(threshold=.001);bpy.ops.object.mode_set(mode='OBJECT')
for v in box.data.vertices:
 x,y,z=v.co
 # Keep room for the thigh while giving the cuff a distinct, even edge.
 waist=max(0,min(1,(z-.94)/.075))
 v.co.x*=1-.13*waist
 v.co.y=.025+(y-.025)*(1-.23*waist)
 if y<-.09 and .82<z<.98:
  v.co.y+=min(.027,max(0,-.09-y))*(1-min(1,abs(x)/.085))
 # Ease increases toward the leg openings while the waistband stays fitted.
 ease=max(0,min(1,(.985-z)/.19))
 thigh_x=math.copysign(.107,x)
 if abs(x)>.035:
  v.co.x+=(x-thigh_x)*.21*ease*min(1,(abs(x)-.035)/.03)
 v.co.y+= (v.co.y-.025)*.23*ease
 if z<.79:
  v.co.z=.752+(z-.752)*.24
# A scalp patch supplies the surface used to distribute buzz-cut fibers.
def buzz(f):
 if body.data.materials[f.material_index].name!='Std_Skin_Head':return False
 c=center(f);line=1.785 if c.y<-.035 else 1.742 if c.y<.065 else 1.708
 if abs(c.x)>.065 and c.y<.02:line=1.755
 return c.z>line
hair=extract('Buzz_Cut',body,buzz,body.data.materials[0],.00125)
# Keep skin beneath open sleeve/collar edges, cull only fully covered faces.
import bmesh
# Keep an intact copy of the skin: the tee is draped against it and takes its weights.
skin=body.copy();skin.data=body.data.copy();skin.name='Skin_Collider';bpy.context.collection.objects.link(skin)
bm=bmesh.new();bm.from_mesh(body.data);remove=[]
for f in bm.faces:
 c=f.calc_center_median();name=body.data.materials[f.material_index].name
 if (name=='Std_Skin_Body' and .82<c.z<1.48) or (name=='Std_Skin_Leg' and c.z>.785) or (name in ('Std_Skin_Leg','Std_Nails') and (c.z<.065 or (c.z<.095 and c.y<-.035))):remove.append(f)
bmesh.ops.delete(bm,geom=remove,context='FACES');bm.to_mesh(body.data);bm.free()
# Eight garment tips are added to the intact CC4 rig for secondary physics.
bpy.ops.object.select_all(action='DESELECT');arm.select_set(True);bpy.context.view_layer.objects.active=arm;bpy.ops.object.mode_set(mode='EDIT')
cloth={'HemFront':('Hips',(0,-.138,1.015)),'HemBack':('Hips',(0,.188,1.015)),'HemLeft':('Hips',(.226,.025,1.015)),'HemRight':('Hips',(-.226,.025,1.015)),'SleeveLeft':('LeftArm',(.43,-.052,1.36)),'SleeveRight':('RightArm',(-.43,-.052,1.36)),'CuffLeft':('LeftUpLeg',(.115,-.105,.75)),'CuffRight':('RightUpLeg',(-.115,-.105,.75))}
for n,(p,point) in cloth.items():b=arm.data.edit_bones.new(n);b.head=point;b.tail=Vector(point)+Vector((0,0,.04));b.parent=arm.data.edit_bones[p]
bpy.ops.object.mode_set(mode='OBJECT')
for o in (shirt,box):
 for n in cloth:o.vertex_groups.new(name=n)
 for v in o.data.vertices:
  x,y,z=v.co
  weights={}
  if o==shirt:
   if abs(x)>.30:
    weights['SleeveLeft' if x>0 else 'SleeveRight']=.38*max(0,min(1,(abs(x)-.30)/.13))
   elif z<1.14:
    # Blend between adjacent hem springs rather than pulling a patch from
    # just one point. Keep the upper torso fully driven by the skeleton.
    across=x/.226
    depth=(y-.025)/.163
    fraction=abs(across)/max(1e-6,abs(across)+abs(depth))
    amount=.58*max(0,min(1,(1.14-z)/.125))
    weights['HemLeft' if x>0 else 'HemRight']=amount*fraction
    weights['HemFront' if depth<0 else 'HemBack']=amount*(1-fraction)
  else:
   weights['CuffLeft' if x>0 else 'CuffRight']=.18*max(0,min(1,(.84-z)/.10))
  total=sum(weights.values())
  if total>0:
   for g in list(v.groups):o.vertex_groups[g.group].add([v.index],g.weight*(1-total),'REPLACE')
   for name,weight in weights.items():
    if weight>0:o.vertex_groups[name].add([v.index],weight,'REPLACE')
# Closed climbing-shoe lofts avoid individual toe silhouettes through rubber.
for sign,side in [(1,'Left'),(-1,'Right')]:
 verts=[];faces=[];rings=[(.10,.005,.035,.022),(.085,.040,.047,.045),(.023,.046,.054,.053),(-.045,.052,.042,.034),(-.110,.054,.033,.026),(-.175,.046,.031,.025),(-.213,.025,.026,.019),(-.228,.003,.024,.015)]
 for y,w,z,h in rings:
  for j in range(24):
   a=2*math.pi*j/24;verts.append((sign*(.09-.012*max(0,min(1,-y/.22)))+w*math.cos(a),y,max(.006,z+h*math.sin(a))))
 for k in range(len(rings)-1):
  for j in range(24):faces.append((k*24+j,k*24+(j+1)%24,(k+1)*24+(j+1)%24,(k+1)*24+j))
 faces.extend([tuple(reversed(range(24))),tuple((len(rings)-1)*24+j for j in range(24))]);data=bpy.data.meshes.new(side+'_Shoe');data.from_pydata(verts,[],faces);data.update();shoe=bpy.data.objects.new(side+'_Climbing_Shoe',data);bpy.context.collection.objects.link(shoe);shoe.parent=arm;m=shoe.modifiers.new('CC4 foot','ARMATURE');m.object=arm;shoe.data.materials.append(rubber);shoe.data.materials.append(yellow)
 for n in [side+'Foot',side+'ToeBase']:shoe.vertex_groups.new(name=n)
 for v in data.vertices:
  t=max(0,min(1,(-v.co.y-.07)/.10));shoe.vertex_groups[side+'Foot'].add([v.index],1-t,'REPLACE');shoe.vertex_groups[side+'ToeBase'].add([v.index],t,'REPLACE')
 for f in data.polygons:
  f.use_smooth=True
  center_point=sum((data.vertices[i].co for i in f.vertices),Vector())/len(f.vertices)
  f.material_index=int(center_point.z>.042 and -.19<center_point.y<.04 and not -.089<center_point.y<-.055)
 bpy.context.view_layer.objects.active=shoe
 sub=shoe.modifiers.new('Rounded rubber last','SUBSURF');sub.levels=1
 bpy.ops.object.modifier_move_up(modifier=sub.name);bpy.ops.object.modifier_apply(modifier=sub.name)
# Bake garment smoothing before export, preserve the original body's topology.
for o in (shirt,box,hair):
 bpy.context.view_layer.objects.active=o
 sub=o.modifiers.new('Garment silhouette','SUBSURF');sub.levels=1;sub.render_levels=1;bpy.ops.object.modifier_move_up(modifier=sub.name);bpy.ops.object.modifier_apply(modifier=sub.name)
 if o==box:continue
 solid=o.modifiers.new('Fabric edge thickness' if o in (shirt,box) else 'Short hair density','SOLIDIFY');solid.thickness=.002 if o in (shirt,box) else .00035;solid.offset=0;bpy.ops.object.modifier_move_up(modifier=solid.name);bpy.ops.object.modifier_apply(modifier=solid.name)
# Standardize only the bind stance to a T-pose for the existing locomotion
# library. Bake the pose through the original CC4 skin weights, then apply it
# as rest; no body reweighting, changed proportions or replacement skeleton.
for side,sign in [('Left',1),('Right',-1)]:
 upper=arm.data.bones[side+'Arm'];lower=arm.data.bones[side+'ForeArm'];axis=(lower.head_local-upper.head_local).normalized();delta=axis.rotation_difference(Vector((sign,0,0)))
 matrix=delta.to_matrix().to_4x4()@upper.matrix_local;matrix.translation=upper.head_local;arm.pose.bones[side+'Arm'].matrix=matrix
bpy.context.view_layer.update()
for o in [o for o in bpy.context.scene.objects if o.type=='MESH']:
 bpy.context.view_layer.objects.active=o
 for modifier in list(o.modifiers):
  if modifier.type=='ARMATURE':bpy.ops.object.modifier_apply(modifier=modifier.name)
bpy.context.view_layer.objects.active=arm;bpy.ops.object.select_all(action='DESELECT');arm.select_set(True);bpy.ops.object.mode_set(mode='POSE');bpy.ops.pose.armature_apply(selected=False);bpy.ops.object.mode_set(mode='OBJECT')
for o in [o for o in bpy.context.scene.objects if o.type=='MESH']:
 m=o.modifiers.new('Original CC4 skin weights','ARMATURE');m.object=arm
# Trim the skin fully covered by the sewn sleeves in the same T-pose used
# to author them. Retain an overlap at the opening during deep bends.
bm=bmesh.new();bm.from_mesh(body.data)
covered=[f for f in bm.faces if body.data.materials[f.material_index].name=='Std_Skin_Arm'
         and abs(f.calc_center_median().x)<.357]
bmesh.ops.delete(bm,geom=covered,context='FACES')
bm.to_mesh(body.data);bm.free()
# Author the garment in the normalized T-pose, then size the whole rig.
sys.path.insert(0,str(Path(__file__).resolve().parent))
from author_garments import replace_hair
from drape_tee import drape_tee
shirt=drape_tee(arm,shirt,skin,OUT)
hair=replace_hair(arm,hair)
height_scale=1.7018/source_height
for o in [o for o in bpy.context.scene.objects if o.type=='MESH']:
 for v in o.data.vertices:v.co*=height_scale
bpy.context.view_layer.objects.active=arm
bpy.ops.object.select_all(action='DESELECT');arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
for b in arm.data.edit_bones:b.head*=height_scale;b.tail*=height_scale
bpy.ops.object.mode_set(mode='OBJECT')
# US men's 9 modeled around a 26.7 cm foot, with 28.2 cm exterior rubber.
for o in [o for o in bpy.context.scene.objects if o.name.endswith('_Climbing_Shoe')]:
 length=max(v.co.y for v in o.data.vertices)-min(v.co.y for v in o.data.vertices)
 for v in o.data.vertices:v.co.y*=.282/length
print('SIZE: barefoot height',source_height*height_scale,'shoe exterior .282m',flush=True)
# Prune tiny influences without replacing CC4 weights.
meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
for o in meshes:
 for v in o.data.vertices:
  weights=sorted([(g.group,g.weight) for g in v.groups if g.weight>1e-6],key=lambda p:p[1],reverse=True)[:4];total=sum(w for _,w in weights)
  if not total:raise RuntimeError('unweighted '+o.name)
  # Snapshot indices: removing a weight invalidates live group elements.
  for group_index in [g.group for g in v.groups]:o.vertex_groups[group_index].remove([v.index])
  for i,w in weights:o.vertex_groups[i].add([v.index],w/total,'REPLACE')
  if len(v.groups)>4 or abs(sum(g.weight for g in v.groups)-1)>1e-5:
   raise RuntimeError('invalid export weights '+o.name+' vertex '+str(v.index))
# FBX emits a texture object per shader socket. Separate the opacity node
# from diffuse even when glTF shared one RGBA image for both sockets.
for material in {m for o in bpy.context.scene.objects if o.type=='MESH' for m in o.data.materials}:
 if not material.use_nodes:continue
 bs=next((n for n in material.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
 if bs and bs.inputs['Alpha'].is_linked:
  link=bs.inputs['Alpha'].links[0]
  if link.from_node.type=='TEX_IMAGE':
   texture=material.node_tree.nodes.new('ShaderNodeTexImage');texture.image=link.from_node.image;material.node_tree.links.new(texture.outputs['Alpha'],bs.inputs['Alpha'])
# Unique texture identities prevent FBX import conflicts in unattended UE.
for o in meshes:
 for m in o.data.materials:
  if not m.use_nodes:continue
  for image_index,n in enumerate(m.node_tree.nodes):
   if n.type=='TEX_IMAGE' and n.image:
    source=n.image;image=bpy.data.images.new(m.name+'_Texture_'+str(image_index),source.size[0],source.size[1],alpha=True)
    image.colorspace_settings.name=source.colorspace_settings.name;image.pixels.foreach_set(source.pixels[:])
    image.filepath_raw=str(OUT/(image.name+'.png'));image.file_format='PNG';image.save();image.pack();n.image=image
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'player.blend'))
def select():
 bpy.ops.object.select_all(action='DESELECT');arm.select_set(True)
 for o in meshes:o.select_set(True)
 bpy.context.view_layer.objects.active=arm
select();bpy.ops.export_scene.gltf(filepath=str(OUT/'player.glb'),export_format='GLB',use_selection=True,export_apply=False,export_animations=False,export_morph=False,export_image_format='AUTO',export_yup=True)
# Join surfaces for a single native skin while keeping material slots and weights.
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:o.select_set(True)
bpy.context.view_layer.objects.active=body;bpy.ops.object.join();body.name='climber';meshes=[body]
select();bpy.ops.export_scene.fbx(filepath=str(OUT/'climber.fbx'),use_selection=True,object_types={'ARMATURE','MESH'},add_leaf_bones=False,bake_anim=False,path_mode='COPY',embed_textures=True,axis_forward='-Y',axis_up='Z',armature_nodetype='NULL',mesh_smooth_type='FACE')
# Preview the actual materials and authored garment silhouette.
bpy.ops.object.camera_add(location=(2.8,-6,2.4));cam=bpy.context.object;cam.rotation_euler=(Vector((0,0,1.02))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=2.12
s=bpy.context.scene;s.camera=cam;s.render.engine='BLENDER_EEVEE';s.world.color=(.22,.22,.22)
for pos,power,size in [((2,-4,5),650,4),((-3,-2,3),450,3),((1,3,4),800,3)]:bpy.ops.object.light_add(type='AREA',location=pos);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=size;light.rotation_euler=(Vector((0,0,1))-light.location).to_track_quat('-Z','Y').to_euler()
s.render.resolution_x=900;s.render.resolution_y=1100;s.render.resolution_percentage=100;s.render.filepath=str(OUT/'preview.png');bpy.ops.render.render(write_still=True)
print('COREY PLAYER READY',len(arm.data.bones),'bones',len(body.data.vertices),'vertices',flush=True)

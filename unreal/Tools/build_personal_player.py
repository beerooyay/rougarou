"""Personalize the preserved CC4 player with a fitted head, soft garments and shoes.
No body reweighting, cloth proxy or Chaos simulation is introduced.
"""
import bpy,bmesh,math,json,sys,numpy as np
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'unreal/Tools'))
import build_modular_player as util
OUT=ROOT/'assets/character/personal-player';PROOF=ROOT/'unreal/Saved/PersonalPlayer';OUT.mkdir(exist_ok=True);PROOF.mkdir(exist_ok=True)
util.OUT=OUT;util.PROOF=PROOF
bpy.ops.wm.open_mainfile(filepath=str(OUT/'source.blend'))
arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE');source=next(o for o in bpy.context.scene.objects if o.type=='MESH')
world=source.matrix_world.copy()
for v in source.data.vertices:v.co=world@v.co
source.parent=None;source.matrix_world=Matrix.Identity(4)
parts={}
for i,mat in enumerate(source.data.materials):
 o=source.copy();o.data=source.data.copy();bpy.context.collection.objects.link(o);o.name=mat.name
 bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.material_index!=i],context='FACES');bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS');bm.to_mesh(o.data);bm.free()
 o.data.materials.clear();o.data.materials.append(mat)
 for p in o.data.polygons:p.material_index=0;p.use_smooth=True
 o.parent=arm;o.matrix_world=Matrix.Identity(4);parts[mat.name]=o
 # Preview uses source PNGs; native finishing reuses original UE materials.
 mat.use_nodes=True;bs=mat.node_tree.nodes.get('Principled BSDF')
 image=ROOT/'assets/character/corey'/(mat.name+'_Texture_2.png')
 if image.exists():
  tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(image),check_existing=True);mat.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color'])
bpy.data.objects.remove(source,do_unlink=True)
oldhead=parts['Std_Skin_Head'];oldtee=parts['Ivory_Cotton'];oldshorts=parts['Forest_Green_Twill']
# Append the user's reduced, textured face without importing a second body.
with bpy.data.libraries.load(str(ROOT/'assets/character/modular/player.blend'),link=False) as (a,b):b.objects=['Head_Face']
head=b.objects[0];bpy.context.collection.objects.link(head);head.parent=None;head.matrix_world=Matrix.Identity(4)
for m in list(head.modifiers):head.modifiers.remove(m)
oldpts=np.array([tuple(v.co) for v in oldhead.data.vertices]);newpts=np.array([tuple(v.co) for v in head.data.vertices])
oldband=oldpts[(oldpts[:,2]>arm.data.bones['Head'].head_local.z+.025)&(oldpts[:,2]<oldpts[:,2].max()-.015)]
newband=newpts[(newpts[:,2]>newpts[:,2].max()-.18)&(newpts[:,2]<newpts[:,2].max()-.015)]
scale=np.ptp(oldband[:,0])/np.ptp(newband[:,0]);shift=Vector((float((oldband[:,0].min()+oldband[:,0].max())/2-(newband[:,0].min()+newband[:,0].max())*.5*scale),float((oldband[:,1].min()+oldband[:,1].max())/2-(newband[:,1].min()+newband[:,1].max())*.5*scale),float(oldpts[:,2].max()-newpts[:,2].max()*scale)))
for v in head.data.vertices:
 own={head.vertex_groups[g.group].name:g.weight for g in v.groups if head.vertex_groups[g.group].name in ('Head','Neck')};v.co=v.co*scale+shift;util.set_weights(head,v,own)
# Trim the source scan's shoulder skirt, then bridge a clean neck to CC4.
cut=oldpts[:,2].max()-.255
bm=bmesh.new();bm.from_mesh(head.data);bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),plane_co=(0,0,float(cut)),plane_no=(0,0,1),clear_inner=True,clear_outer=False);bm.to_mesh(head.data);bm.free()
# The imported CC4 head has UV-split boundary chains, not a neck loop.
# Use a small anatomical neck ring instead of stretching to those seams.
neckmat=head.data.materials[0].copy();neckmat.name='Skin_Neck_Bridge';shader=neckmat.node_tree.nodes.get('Principled BSDF')
for link in list(shader.inputs['Normal'].links):neckmat.node_tree.links.remove(link)
head.data.materials.append(neckmat)
bm=bmesh.new();bm.from_mesh(head.data);edges=[e for e in bm.edges if e.is_boundary and all(abs(v.co.z-cut)<.0001 for v in e.verts)];result=bmesh.ops.extrude_edge_only(bm,edges=edges)
for v in result['geom']:
 if isinstance(v,bmesh.types.BMVert):
  a=math.atan2(v.co.y-.025,v.co.x);v.co=Vector((.044*math.cos(a),.025+.050*math.sin(a),1.525))
for face in result['geom']:
 if isinstance(face,bmesh.types.BMFace):face.material_index=1
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(head.data);bm.free()
for v in head.data.vertices:
 w=float(util.smoothstep((v.co.z-cut)/.09));util.set_weights(head,v,{'Head':w,'Neck':1-w})
for p in head.data.polygons:p.use_smooth=True
head.parent=arm;m=head.modifiers.new('CC4 head and neck','ARMATURE');m.object=arm
# Remove all old face, eye, lash, teeth and scalp surfaces from the export.
active=[o for n,o in parts.items() if n not in ('Std_Skin_Head','Std_Eyelash','Ga_Eye','Ga_Teeth','Ivory_Cotton','Forest_Green_Twill','Climbing_Shoe_Rubber','Ochre_Shoe_Panels') and 'Hair' not in n]
for o in parts.values():
 if o not in active:o.hide_render=True;o.hide_set(True)
active=[o for o in active if o.name!='Std_Skin_Body']
parts['Std_Skin_Body'].hide_render=True;parts['Std_Skin_Body'].hide_set(True)
active.append(head)
fabric=bpy.data.materials.new('Outfit_Fabric');fabric.use_nodes=True;bs=fabric.node_tree.nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=.92;vc=fabric.node_tree.nodes.new('ShaderNodeVertexColor');vc.layer_name='Fabric';fabric.node_tree.links.new(vc.outputs['Color'],bs.inputs['Base Color'])
report={'head_uniform_scale':float(scale),'garments':{}}
for name,template,file in [('Shirt',oldtee,'shirt.obj'),('Shorts',oldshorts,'shorts.obj')]:
 tmesh=template.data;tmesh.calc_loop_triangles();triangles=[tuple(t.vertices) for t in tmesh.loop_triangles];points=[v.co.copy() for v in tmesh.vertices];tree=BVHTree.FromPolygons(points,triangles,all_triangles=True)
 if name=='Shirt':
  # Preserve the reviewed shirt topology and skinning. The downloaded tee
  # failed raised-arm review; it must not replace a stable garment.
  o=oldtee.copy();o.data=oldtee.data.copy();bpy.context.collection.objects.link(o);o.name='Shirt';o.hide_render=False;o.hide_set(False)
  for modifier in list(o.modifiers):o.modifiers.remove(modifier)
  for v in o.data.vertices:
   weights={oldtee.vertex_groups[g.group].name:g.weight for g in v.groups if not oldtee.vertex_groups[g.group].name.startswith(('Hem','Sleeve'))};util.set_weights(o,v,weights)
 else:
  bpy.ops.wm.obj_import(filepath=str(ROOT/'assets/character/garment-candidate/sources'/file));o=bpy.context.object;o.name=name;o.matrix_world=Matrix.Identity(4)
  for v in o.data.vertices:v.co=Vector((v.co.x,-v.co.z,v.co.y))
  coords=np.array([tuple(v.co) for v in o.data.vertices]);lo,hi=coords.min(0),coords.max(0);target=np.array([tuple(p) for p in points]);tl,th=target.min(0),target.max(0)
  factor=(th[0]-tl[0])/(hi[0]-lo[0])*1.14
  fitted=(coords-(lo+hi)*.5)*factor+(tl+th)*.5;fitted[:,2]+=th[2]-fitted[:,2].max()
  for v,co in zip(o.data.vertices,fitted):v.co=co
  bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),plane_co=(0,0,float(tl[2])),plane_no=(0,0,1),clear_inner=True,clear_outer=False);bm.to_mesh(o.data);bm.free()
 for v in o.data.vertices:
  x,y,z=v.co
  if name=='Shirt':
   # Sleeves retain their fitted reach to the preserved upper arms.
   radial=Vector((v.co.x,v.co.y-.025,0));hem=float(util.smoothstep((1.15-z)/.14))
   if radial.length>.01:v.co+=radial.normalized()*.025*hem
  if name=='Shorts':
   hit,normal,index,distance=tree.find_nearest(v.co);tri=triangles[index];ws={};dist=[1/max((hit-points[i]).length,.004)**2 for i in tri];total=sum(dist)
   for i,w in zip(tri,dist):
    for g in tmesh.vertices[i].groups:
     bone=template.vertex_groups[g.group].name
     if bone in arm.data.bones and not bone.startswith(('Hem','Sleeve','Cuff')):ws[bone]=ws.get(bone,0)+g.weight*w/total
   util.set_weights(o,v,ws)
 # More of the free panels follows the edge controls; neck/waist stay skinned.
 for v in o.data.vertices:
  x,y,z=v.co;extra={}
  if name=='Shirt':
   hem=.72*float(util.smoothstep((1.18-z)/.16));a=abs(x)/.24;b=abs(y-.025)/.17;side=a/max(a+b,1e-6)
   extra['HemLeft' if x>0 else 'HemRight']=hem*side;extra['HemFront' if y<.025 else 'HemBack']=hem*(1-side)
   if abs(x)>.30 and z>1.30:extra['SleeveLeft' if x>0 else 'SleeveRight']=.35*float(util.smoothstep((abs(x)-.30)/.08))
  else:extra['CuffLeft' if x>0 else 'CuffRight']=.45*float(util.smoothstep((.90-z)/.13))
  total=sum(extra.values());weights={o.vertex_groups[g.group].name:g.weight*(1-total) for g in v.groups};weights.update(extra);util.set_weights(o,v,weights)
 # A short inward return gives the openings a finished, supported edge.
 bm=bmesh.new();bm.from_mesh(o.data)
 edges=[e for e in bm.edges if e.is_boundary and (e.verts[0].co.z+e.verts[1].co.z)*.5<(1.13 if name=='Shirt' else .82)]
 result=bmesh.ops.extrude_edge_only(bm,edges=edges)
 for v in result['geom']:
  if isinstance(v,bmesh.types.BMVert):v.co.z+=.004
 bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
 o.data.materials.clear();o.data.materials.append(fabric)
 for p in o.data.polygons:p.material_index=0;p.use_smooth=True
 layer=o.data.color_attributes.new(name='Fabric',type='BYTE_COLOR',domain='CORNER');region=o.data.uv_layers.new(name='FabricRegion')
 for loop in o.data.loops:layer.data[loop.index].color=(*((.80,.78,.73) if name=='Shirt' else (.018,.075,.042)),1);region.data[loop.index].uv=(0 if name=='Shirt' else 1,0)
 o.data.color_attributes.active_color=layer;o.parent=arm;m=o.modifiers.new('CC4 garment weights','ARMATURE');m.object=arm;active.append(o)
 report['garments'][name]={'vertices':len(o.data.vertices),'faces':len(o.data.polygons)}
# Normalize the body and skeleton uniformly, retaining CC4 proportions/weights.
height=max(v.co.z for v in head.data.vertices)-.006231049
factor=1.7018/height
for o in active:
 for v in o.data.vertices:v.co*=factor
bpy.context.view_layer.objects.active=arm;bpy.ops.object.select_all(action='DESELECT');arm.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
for b in arm.data.edit_bones:b.head*=factor;b.tail*=factor
bpy.ops.object.mode_set(mode='OBJECT')
# Anatomical shoe lasts: heel cup, arch, rounded toe box and ankle opening.
rubber=bpy.data.materials.new('Personal_Rubber');rubber.use_nodes=True;rubber.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.014,.017,.018,1)
yellow=bpy.data.materials.new('Personal_Shoe_Upper');yellow.use_nodes=True;yellow.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.38,.26,.035,1)
contacts=[]
for side,sign in [('Left',1),('Right',-1)]:
 ankle=arm.data.bones[side+'Foot'].head_local;toe=arm.data.bones[side+'ToeBase'].head_local;forward=Vector((toe.x-ankle.x,toe.y-ankle.y,0)).normalized();across=forward.cross(Vector((0,0,1))).normalized()
 rings=[(-.055,.009,.051,.043),(-.045,.030,.054,.050),(-.025,.034,.057,.052),(0,.036,.058,.053),(.035,.040,.051,.045),(.075,.045,.040,.034),(.115,.049,.033,.027),(.155,.046,.031,.025),(.19,.036,.030,.024),(.214,.023,.027,.021),(.227,.007,.024,.015)]
 vertices=[];faces=[];count=32
 for along,width,z,rise in rings:
  for j in range(count):
   a=2*math.pi*j/count;medial=.007*max(0,along/.227)*sign
   p=Vector((ankle.x,ankle.y,0))+forward*along+across*(width*math.cos(a)+medial);p.z=max(.006231049*factor,z+rise*math.sin(a));vertices.append(tuple(p))
 for row in range(len(rings)-1):
  for j in range(count):
   # Remove upper instep faces for a real ankle opening.
   if row in (1,2,3) and math.sin(2*math.pi*(j+.5)/count)>.72:continue
   faces.append((row*count+j,row*count+(j+1)%count,(row+1)*count+(j+1)%count,(row+1)*count+j))
 faces += [tuple(reversed(range(count))),tuple((len(rings)-1)*count+j for j in range(count))]
 mesh=bpy.data.meshes.new(side+' shaped shoe');mesh.from_pydata(vertices,[],faces);mesh.update();shoe=bpy.data.objects.new(side+'_Shoe',mesh);bpy.context.collection.objects.link(shoe);mesh.materials.append(rubber);mesh.materials.append(yellow)
 for p in mesh.polygons:
  center=sum((mesh.vertices[i].co for i in p.vertices),Vector())/len(p.vertices);along=(center-Vector((ankle.x,ankle.y,0))).dot(forward);p.material_index=int(center.z>.032 and (.015<along<.095 or -.04<along<-.015));p.use_smooth=True
 for v in mesh.vertices:util.set_weights(shoe,v,{side+'Foot':1})
 bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
 shoe.parent=arm;m=shoe.modifiers.new('Rigid rubber on foot','ARMATURE');m.object=arm;active.append(shoe)
 low=min(v.co.z for v in mesh.vertices);sole=[v.co for v in mesh.vertices if v.co.z<low+.003]
 contacts.append((side,max(sole,key=lambda p:p.dot(forward)).copy()))
# Eye and actual rubber-contact landmarks calibrate cameras and climbing IK.
bpy.context.view_layer.objects.active=arm;bpy.ops.object.select_all(action='DESELECT');arm.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
for side,p in contacts:
 b=arm.data.edit_bones.new(side+'SoleContact');b.head=p;b.tail=p+Vector((0,0,.02));b.parent=arm.data.edit_bones[side+'Foot'];b.use_deform=False
b=arm.data.edit_bones.new('PlayerEyes');b.head=Vector((0,min(v.co.y for v in head.data.vertices)+.032,max(v.co.z for v in head.data.vertices)-.11));b.tail=b.head+Vector((0,-.02,0));b.parent=arm.data.edit_bones['Head'];b.use_deform=False
bpy.ops.object.mode_set(mode='OBJECT')
arm.name='PersonalRig'
for o in active:
 for v in o.data.vertices:
  weights={o.vertex_groups[g.group].name:g.weight for g in v.groups if g.weight>1e-6};util.set_weights(o,v,weights)
# FBX joining matches UV layers by name. Explicitly keep diffuse UVs at 0
# and fabric region masks at 1 for every part, including generated meshes.
for o in active:
 layers=o.data.uv_layers
 first=layers[0] if layers and layers[0].name!='FabricRegion' else None
 diffuse=[tuple(d.uv) for d in first.data] if first else [(0,0)]*len(o.data.loops)
 region=layers.get('FabricRegion');regions=[tuple(d.uv) for d in region.data] if region else [(0,0)]*len(o.data.loops)
 for layer in list(layers):layers.remove(layer)
 uv=layers.new(name='UVMap');mask=layers.new(name='FabricRegion')
 uv.data.foreach_set('uv',np.asarray(diffuse,dtype=np.float32).ravel());mask.data.foreach_set('uv',np.asarray(regions,dtype=np.float32).ravel())
 uv.active_render=True
# Scene lights and previews describe the actual fitted parts.
s=bpy.context.scene;s.world=bpy.data.worlds.new('Personal review');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.15,.15,.15,1)
for loc,power in [((2,-4,4),650),((-3,-1,2),400),((1,3,3),700)]:
 bpy.ops.object.light_add(type='AREA',location=loc);l=bpy.context.object;l.data.energy=power;l.data.size=3;l.rotation_euler=(Vector((0,0,1))-l.location).to_track_quat('-Z','Y').to_euler()
report['height_m']=max(v.co.z for o in active for v in o.data.vertices)-min(v.co.z for o in active for v in o.data.vertices)
report['bones']=len(arm.data.bones)
bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'player.blend'));util.render(active,arm,'front');util.render(active,arm,'back',back=True);util.export_player(active,arm)
(PROOF/'build.json').write_text(json.dumps(report,indent=2));print('PERSONAL PLAYER BUILT',report,flush=True)

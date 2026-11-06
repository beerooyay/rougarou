"""Fit downloaded OBJ garments onto the existing player rig, keeping a candidate master.
Run in Blender. Inputs and the active modular master are read only.
"""
import sys, json, math, shutil
from pathlib import Path
import bpy, bmesh, numpy as np
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'unreal/Tools'))
import build_modular_player as base
OUT=ROOT/'assets/character/garment-candidate';PROOF=ROOT/'unreal/Saved/GarmentCandidate';PROOF.mkdir(parents=True,exist_ok=True)
base.OUT=OUT;base.PROOF=PROOF
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/character/modular/player.blend'))
arm=bpy.data.objects['PlayerRig'];old={n:bpy.data.objects[n] for n in ('Shirt','Shorts')}
# Garments are fitted in the player's bind pose; the downloaded tee's
# lowered sleeves are opened below before transferring weights.
bpy.context.view_layer.update()
pose_delta={b.name:arm.pose.bones[b.name].matrix@b.matrix_local.inverted() for b in arm.data.bones}
report={}
for name,file in [('Shirt','shirt.obj'),('Shorts','shorts.obj')]:
 template=old[name];deps=bpy.context.evaluated_depsgraph_get();ev=template.evaluated_get(deps);mesh=ev.to_mesh();mesh.calc_loop_triangles();points=[v.co.copy() for v in mesh.vertices];triangles=[tuple(t.vertices) for t in mesh.loop_triangles];tree=BVHTree.FromPolygons(points,triangles,all_triangles=True)
 bpy.ops.wm.obj_import(filepath=str(OUT/'sources'/file));obj=bpy.context.object;obj.name=name+'_Downloaded';obj.matrix_world=Matrix.Identity(4)
 # Both OBJ files store Y-up data; their importer object transform is
 # cleared above so the conversion is applied exactly once.
 for v in obj.data.vertices:v.co=Vector((v.co.x,-v.co.z,v.co.y))
 if name=='Shirt':
  # Dense seam/thread decorations are separate material surfaces, not cloth
  # panels. Simulating them creates microscopic, degenerate triangles.
  bm=bmesh.new();bm.from_mesh(obj.data)
  bmesh.ops.delete(bm,geom=[p for p in bm.faces if p.material_index>1],context='FACES')
  bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
  bm.to_mesh(obj.data);bm.free()
  for v in obj.data.vertices:
   side=1 if v.co.x>0 else -1;pivot=Vector((side*.065,0,.635))
   amount=float(base.smoothstep((abs(v.co.x)-.055)/.045)*base.smoothstep((v.co.z-.535)/.05))
   opened=pivot+Matrix.Rotation(math.radians(-40*side),3,'Y')@(v.co-pivot)
   v.co=v.co.lerp(opened,amount)
 # Do not weld the tee's close inner/outer surfaces together. Its visible
 # shell keeps the authored layers; only the separate proxy is simulated.
 bm=bmesh.new();bm.from_mesh(obj.data)
 if name!='Shirt':bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.0002)
 bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free()
 base.reduce(obj,18000 if name=='Shirt' else 9000)
 bm=bmesh.new();bm.from_mesh(obj.data)
 if name!='Shirt':bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00002)
 bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free()
 source=np.array([tuple(v.co) for v in obj.data.vertices]);target=np.array([tuple(p) for p in points]);lo,hi=source.min(0),source.max(0);tl,th=target.min(0),target.max(0)
 # Fit total height and torso depth; surface transfer below supplies the
 # final body clearance. Preserve the source collar and sewn edge topology.
 scale=(th-tl)/(hi-lo);scale[0]*=1.025;scale[1]*=1.04
 fitted=(source-(lo+hi)*.5)*scale+(tl+th)*.5
 for v,co in zip(obj.data.vertices,fitted):
  hit,normal,index,d=tree.find_nearest(Vector(co));tri=triangles[index];a,b,c=[points[i] for i in tri]
  # Existing garment is a fitting reference, not the exported surface.
  co=Vector(co).lerp(hit,.15)+normal*.005
  if name=='Shirt' and abs(co.x)>.20 and co.z>1.10:
   side='Left' if co.x>0 else 'Right'
   shoulder=arm.data.bones[side+'Arm'].head_local
   elbow=arm.data.bones[side+'ForeArm'].head_local
   t=max(0,min(1,(abs(co.x)-abs(shoulder.x))/(abs(elbow.x)-abs(shoulder.x))))
   center=shoulder.lerp(elbow,t);radial=Vector((0,co.y-center.y,co.z-center.z))
   if radial.length>.085:
    blend=float(base.smoothstep((abs(co.x)-.20)/.08))
    co-=radial.normalized()*(radial.length-.085)*blend
   co.x=math.copysign(.20+(abs(co.x)-.20)*.75,co.x)
  if name=='Shirt':
   hem=float(base.smoothstep((1.04-co.z)/.10))
   radial=Vector((co.x,co.y-.025,0))
   if radial.length>.01:co+=radial.normalized()*.01*hem
   co.z-=.02*hem
  else:co.z-=.015*float(base.smoothstep((.72-co.z)/.06))
  weights={}
  # Inverse-distance interpolation is stable at degenerate source triangles.
  distances=[1/max((hit-points[i]).length,1e-5) for i in tri];total=sum(distances)
  for i,w in zip(tri,distances):
   for g in template.data.vertices[i].groups:
    bone=template.vertex_groups[g.group].name
    if bone in pose_delta:weights[bone]=weights.get(bone,0)+g.weight*w/total
  if name=='Shirt':
   blend=float(base.smoothstep((abs(co.x)-.13)/.15)*base.smoothstep((co.z-1.05)/.10))
   body={bone:value for bone,value in weights.items() if bone in ('Spine','Spine1','Spine2')}
   if not body:body={'Spine2':1}
   total=sum(body.values());weights={bone:value/total*(1-blend) for bone,value in body.items()}
   weights['LeftArm' if co.x>0 else 'RightArm']=blend
  base.set_weights(obj,v,weights)
  if name=='Shirt':
   skin=Matrix(((0,0,0,0),)*4)
   for g in v.groups:
    bone=obj.vertex_groups[g.group].name
    if bone in pose_delta:
     delta=pose_delta[bone]
     for row in range(4):
      for col in range(4):skin[row][col]+=delta[row][col]*g.weight
   v.co=skin.inverted_safe()@co
  else:v.co=co
 ev.to_mesh_clear()
 bm=bmesh.new();bm.from_mesh(obj.data)
 bmesh.ops.triangulate(bm,faces=list(bm.faces))
 bmesh.ops.delete(bm,geom=[p for p in bm.faces if p.calc_area()<1e-10],context='FACES')
 bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
 bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free()
 collar_tree=None
 if name=='Shirt':
  collar_ids={i for p in obj.data.polygons if p.material_index==1 for i in p.vertices}
  assert collar_ids,'Downloaded rib collar was lost during reduction'
  collar_tree=KDTree(len(collar_ids))
  for j,i in enumerate(collar_ids):collar_tree.insert(obj.data.vertices[i].co,j)
  collar_tree.balance()
 obj.data.materials.clear();fabric=bpy.data.materials['Outfit_Fabric'];obj.data.materials.append(fabric)
 for p in obj.data.polygons:p.material_index=0;p.use_smooth=True
 obj.parent=arm;mod=obj.modifiers.new('Existing player rig','ARMATURE');mod.object=arm
 # Explicit cloth mobility, including the closed/thick collar of the tee.
 if obj.data.color_attributes.get('Fabric'):obj.data.color_attributes.remove(obj.data.color_attributes['Fabric'])
 layer=obj.data.color_attributes.new(name='Fabric',type='BYTE_COLOR',domain='CORNER')
 region=obj.data.uv_layers.get('FabricRegion') or obj.data.uv_layers.new(name='FabricRegion')
 waist=max(v.co.z for v in obj.data.vertices)
 for loop in obj.data.loops:
  v=obj.data.vertices[loop.vertex_index];x,y,z=v.co
  free=float(base.smoothstep((1.41-z)/.10)) if name=='Shirt' else float(base.smoothstep((waist-z-.012)/.065))*.6
  if name=='Shirt':free*=max(.2+.8*float(base.smoothstep((1.22-z)/.28)),.32*float(base.smoothstep((abs(x)-.21)/.13)))
  if collar_tree:free*=float(base.smoothstep((collar_tree.find(v.co)[2]-.004)/.035))
  layer.data[loop.index].color=(*((.80,.78,.73) if name=='Shirt' else (.018,.095,.045)),free)
  region.data[loop.index].uv=(0 if name=='Shirt' else 1,0)
 obj.data.color_attributes.active_color=layer
 report[name]={'vertices':len(obj.data.vertices),'faces':len(obj.data.polygons)}
 template.hide_render=True;template.hide_set(True);template.name=name+'_Previous';obj.name=name
for b in arm.pose.bones:b.matrix_basis=Matrix.Identity(4)
bpy.context.view_layer.update()
# Downloaded collar replaces the old scan-derived band.
bpy.data.objects['Collar'].hide_render=True;bpy.data.objects['Collar'].hide_set(True)
parts=[o for o in bpy.context.scene.objects if o.type=='MESH' and not o.hide_render]
proxies=[]
proxy_material=bpy.data.materials.new('Outfit_Proxy');proxy_material.use_nodes=True
for name,budget in [('Shirt',2200),('Shorts',1600)]:
 proxy=old[name].copy();proxy.data=old[name].data.copy();bpy.context.collection.objects.link(proxy)
 proxy.name=name+'_Simulation';proxy.hide_render=False;proxy.hide_set(False)
 base.reduce(proxy,budget)
 target=bpy.data.objects[name].data
 surface=BVHTree.FromPolygons([v.co.copy() for v in target.vertices],[tuple(p.vertices) for p in target.polygons])
 for v in proxy.data.vertices:
  if name=='Shirt':
   if abs(v.co.x)>.20 and v.co.z>1.10:v.co.x=math.copysign(.20+(abs(v.co.x)-.20)*.75,v.co.x)
   hem=float(base.smoothstep((1.04-v.co.z)/.10));radial=Vector((v.co.x,v.co.y-.025,0))
   if radial.length>.01:v.co+=radial.normalized()*.01*hem
   v.co.z-=.02*hem
  else:v.co.z-=.015*float(base.smoothstep((.72-v.co.z)/.06))
  hit,normal,_,_=surface.find_nearest(v.co)
  if hit is not None:
   if name=='Shirt' and abs(hit.x)>.20 and hit.z>1.10:
    side='Left' if hit.x>0 else 'Right';center=arm.data.bones[side+'Arm'].head_local
    outward=Vector((0,hit.y-center.y,hit.z-center.z))
   elif name=='Shorts':
    side='Left' if hit.x>0 else 'Right';hip=arm.data.bones[side+'UpLeg'].head_local;knee=arm.data.bones[side+'Leg'].head_local
    axis=knee-hip;t=max(0,min(1,(hit-hip).dot(axis)/axis.length_squared));outward=hit-(hip+axis*t)
   else:outward=Vector((hit.x,hit.y-.025,0))
   if normal.dot(outward)<0:normal=-normal
   # Keep the physical cage just inside the visible garment. A render
   # surface inside its proxy can otherwise penetrate body collision even
   # when every simulated point is outside the body.
   v.co=v.co.lerp(hit-normal*.004,.85)
 proxy.data.materials.clear();proxy.data.materials.append(proxy_material)
 for p in proxy.data.polygons:p.material_index=0
 # Proxy points have their own mobility paint; render UVs are not used for
 # physical constraints or collision.
 for p in proxy.data.polygons:p.use_smooth=True
 proxy.hide_render=True;proxy.hide_set(True);proxies.append(proxy)
# A neutral detail atlas avoids mapping the old scan's clothes onto new UVs.
fabric=bpy.data.materials['Outfit_Fabric'];shader=next(n for n in fabric.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
for link in list(shader.inputs['Normal'].links):fabric.node_tree.links.remove(link)
for n in fabric.node_tree.nodes:
 if n.type=='TEX_IMAGE':
  img=bpy.data.images.get('GarmentNeutral') or bpy.data.images.new('GarmentNeutral',width=32,height=32)
  img.generated_color=(.95,.95,.95,1);img.pack();n.image=img
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'player.blend'))
base.render(parts,arm,'assembled-front');base.render(parts,arm,'assembled-back',back=True)
def covered_skin(obj):
 if not obj.name.startswith(('Leg_L','Leg_R')):return
 # Keep an overlap inside each loose cuff, but omit the deeply covered
 # thigh surface from the runtime render. The editable body and physics
 # capsules remain complete; hidden skin cannot poke through the shorts.
 shorts=bpy.data.objects['Shorts']
 side=1 if obj.name.startswith('Leg_L') else -1
 garment_hem=min(v.co.z for v in shorts.data.vertices if v.co.x*side>0)
 height=min(obj['skin_cuff_z']-.001,garment_hem+.025)
 bm=bmesh.new();bm.from_mesh(obj.data)
 bmesh.ops.delete(bm,geom=[p for p in bm.faces if all(v.co.z>height for v in p.verts)],context='FACES')
 bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
 bm.to_mesh(obj.data);bm.free()
base.export_player(parts+proxies,arm,prepare_copy=covered_skin)
for n in ('idle','walk','jog','run','start','stop'):shutil.copy2(ROOT/'assets/character/modular'/(n+'.fbx'),OUT/(n+'.fbx'))
(PROOF/'fit.json').write_text(json.dumps(report,indent=2));print('GARMENTS FITTED',report,flush=True)

"""Compare source joint placement and regional weights; run in Blender.
Rear-hem averages use a texture-filtered fabric sample, not all garment vertices.
"""
import bpy,json,collections,numpy as np
from pathlib import Path
root=Path(__file__).resolve().parents[2];result={}
for name,rel in [('original','blaize/baseline-before-polish/player.blend'),('blaize2','blaize2/player.blend')]:
 bpy.ops.wm.open_mainfile(filepath=str(root/'assets/character'/rel))
 arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE');mesh=next(o for o in bpy.context.scene.objects if o.type=='MESH')
 shader=next(n for n in mesh.data.materials[0].node_tree.nodes if n.type=='BSDF_PRINCIPLED');image=shader.inputs['Base Color'].links[0].from_node.image
 pixels=list(image.pixels[:]);w,h=image.size;uv=np.empty(len(mesh.data.loops)*2,dtype=np.float32);mesh.data.uv_layers.active.data.foreach_get('uv',uv);fabric=set()
 for loop in mesh.data.loops:
  u,v=uv[loop.index*2:loop.index*2+2];ix=min(w-1,max(0,int(u*w)));iy=min(h-1,max(0,int(v*h)));r,g,b=pixels[(iy*w+ix)*4:(iy*w+ix)*4+3]
  if g>.26 and b>.19 and r<g*1.27 and g<b*1.55:fabric.add(loop.vertex_index)
 groups={g.index:g.name for g in mesh.vertex_groups};stats={};invalid=0
 for v in mesh.data.vertices:
  if abs(sum(g.weight for g in v.groups)-1)>1e-4:invalid+=1
 for region,check in [('rear_hem',lambda x,y,z:abs(x)<.19 and y>.07 and .89<z<1.06),('upper_back',lambda x,y,z:abs(x)<.19 and y>.025 and 1.25<z<1.43),('upper_sides',lambda x,y,z:.17<abs(x)<.28 and 1.15<z<1.35)]:
  verts=[v for v in mesh.data.vertices if check(*v.co) and (region!='rear_hem' or v.index in fabric)];totals=collections.Counter()
  for v in verts:
   for g in v.groups:totals[groups[g.group]]+=g.weight/len(verts)
  stats[region]={'vertices':len(verts),'mean_weights':dict(totals.most_common(12))}
 result[name]={'regions':stats,'invalid_weight_sums':invalid,'max_influences':max(len(v.groups) for v in mesh.data.vertices),'joints':{n:list(arm.data.bones[n].head_local) for n in ['Hips','Spine','Spine1','Spine2','Neck','Head','LeftShoulder','LeftArm']}}
p=root/'unreal/Saved/Blaize2Player/weight-audit.json';p.write_text(json.dumps(result,indent=2));print(json.dumps(result))

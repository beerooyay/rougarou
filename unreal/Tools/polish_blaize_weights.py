"""Localized shirt weighting correction; geometry, bind, skin and UVs stay intact.
Always starts from the reviewed baseline, so rerunning never compounds edits.
"""
import bpy
import json
import sys
import numpy as np
from mathutils.kdtree import KDTree
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'assets/character/blaize'
sys.path.insert(0,str(Path(__file__).parent))
import build_modular_player as helpers
bpy.ops.wm.open_mainfile(filepath=str(OUT/'baseline-before-polish/player.blend'))
mesh=next(o for o in bpy.context.scene.objects if o.type=='MESH')
arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
original=[tuple(v.co) for v in mesh.data.vertices]
shader=next(n for n in mesh.data.materials[0].node_tree.nodes if n.type=='BSDF_PRINCIPLED')
image=shader.inputs['Base Color'].links[0].from_node.image
pixels=list(image.pixels[:]);w,h=image.size
uv={}
loop_uv=np.empty(len(mesh.data.loops)*2,dtype=np.float32)
mesh.data.uv_layers.active.data.foreach_get('uv',loop_uv)
for loop in mesh.data.loops:uv[loop.vertex_index]=loop_uv[loop.index*2:loop.index*2+2]
def smooth(a,b,v):
 t=max(0,min(1,(v-a)/(b-a)));return t*t*(3-2*t)
mask=np.zeros(len(mesh.data.vertices))
tree=KDTree(len(mesh.data.vertices))
for v in mesh.data.vertices:tree.insert(v.co,v.index)
tree.balance()
for v in mesh.data.vertices:
 x,y,z=v.co
 if not (.85<z<1.48 and abs(x)<.31):continue
 u=uv[v.index];ix=min(w-1,max(0,int(u[0]*w)));iy=min(h-1,max(0,int(u[1]*h)))
 r,g,b=pixels[4*(iy*w+ix):4*(iy*w+ix)+3]
 fabric=(g>.26 and b>.19 and r<g*1.27 and g<b*1.55)
 # Include the printed back graphic, while excluding neck/arms and green shorts.
 graphic=y>.07 and 1.09<z<1.40 and abs(x)<.18
 torso_panel=y>.07 and .925<z<1.32 and abs(x)<.21
 if fabric or graphic or torso_panel:mask[v.index]=1
# Texture shadows and the printed logo must not create sharp skinning borders.
# Blur in physical space so duplicated UV seam vertices receive the same mask.
active=[v for v in mesh.data.vertices if .81<v.co.z<1.52 and abs(v.co.x)<.35]
neighbours={v.index:[(i,max(0,1-d/.035)**2) for _,i,d in tree.find_range(v.co,.035)] for v in active}
for _ in range(3):
 old=mask.copy()
 for i,near in neighbours.items():mask[i]=sum(old[j]*w for j,w in near)/sum(w for _,w in near)
changed=0
for v in active:
 x,y,z=v.co
 cloth=float(mask[v.index])
 if cloth<.001:continue
 weights={mesh.vertex_groups[g.group].name:g.weight for g in v.groups}
 old=dict(weights)
 center=1-smooth(.18,.30,abs(x))
 back=smooth(.025,.075,y)*center*smooth(1.02,1.13,z)*(1-smooth(1.40,1.48,z))*cloth
 for n in list(weights):
  if n.endswith('Arm') or n.endswith('ForeArm') or n.endswith('Shoulder'):
   # Keep shoulder girdle motion, but don't let an arm drag the torso panel.
   strength=.25 if n.endswith('Shoulder') else .45
   # Local upper side panel follows the chest instead of flaring with
   # the raised arm. Preserve sleeves and the original lower-shirt weights.
   side=smooth(.12,.19,abs(x))*(1-smooth(.24,.31,abs(x)))*smooth(1.10,1.20,z)*(1-smooth(1.32,1.40,z))*cloth
   amount=weights[n]*max(back,side)*strength
   weights[n]-=amount;weights['Spine2']=weights.get('Spine2',0)+amount
 if sum(abs(weights.get(n,0)-old.get(n,0)) for n in weights)<1e-6:continue
 changed+=1
 for group in list(v.groups):mesh.vertex_groups[group.group].remove([v.index])
 weights=sorted(((n,a) for n,a in weights.items() if a>1e-6),key=lambda p:-p[1])[:4]
 total=sum(a for _,a in weights)
 for n,a in weights:mesh.vertex_groups[n].add([v.index],a/total,'REPLACE')
assert original==[tuple(v.co) for v in mesh.data.vertices]
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'player.blend'))
helpers.OUT=OUT
helpers.export_player([mesh],arm)
manifest=json.loads((OUT/'materials.json').read_text())
for spec in manifest['materials'].values():
 spec.update(roughness_channel='G',metallic_texture=spec['roughness_texture'],metallic_channel='B',two_sided=True)
(OUT/'materials.json').write_text(json.dumps(manifest,indent=2)+'\n')
(ROOT/'unreal/Saved/BlaizePlayer/polish.json').write_text(json.dumps({'changed_shirt_vertices':changed,'geometry_unchanged':True,'source':'baseline-before-polish/player.blend'},indent=2)+'\n')
print('SHIRT WEIGHTS',changed,flush=True)

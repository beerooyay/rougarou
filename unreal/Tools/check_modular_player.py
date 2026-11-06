"""Read-only checks for the editable modular master, run inside Blender.

Blender -b assets/character/modular/player.blend --python-exit-code 1 --python unreal/Tools/check_modular_player.py
"""
import bpy,sys,json,math,numpy as np
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'unreal/Tools'))
from build_modular_player import boundary_loops
arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE');bones={b.name for b in arm.data.bones}
parts={o.name:o for o in bpy.context.scene.objects if o.type=='MESH' and not o.hide_render}
expected={'Shirt','Shorts','Arm_L','Arm_R','Leg_L','Leg_R','Shoe_L','Shoe_R','Head_Face','Collar','Body_Underlay'}
assert set(parts)==expected,set(parts)
assert bpy.data.objects['Head_Original'].hide_render
report={'parts':{},'failures':[]}
coords=[]
for name,obj in parts.items():
 bad=0;maxweights=0
 for v in obj.data.vertices:
  ws=[g.weight for g in v.groups if obj.vertex_groups[g.group].name in bones and g.weight>1e-6]
  if not ws or len(ws)>4 or abs(sum(ws)-1)>1e-5:bad+=1
  maxweights=max(maxweights,len(ws))
  coords.append(obj.matrix_world@v.co)
 report['parts'][name]={'vertices':len(obj.data.vertices),'bad_weights':bad,'max_bone_influences':maxweights,'boundary_loops':len(boundary_loops(obj))}
 assert bad==0,(name,bad)
 if name in ('Shirt','Shorts'):
  assert len(boundary_loops(obj))==(4 if name=='Shirt' else 3)
  alpha=[c.color[3] for c in obj.data.color_attributes['Fabric'].data]
  assert min(alpha)==0 and max(alpha)>.5 and max(alpha)<=1
  assert obj.data.materials[0].name=='Outfit_Fabric'
  report['parts'][name]['paint_alpha_range']=[min(alpha),max(alpha)]
  assert 'FabricRegion' in obj.data.uv_layers
  region_values=[loop.uv.x for loop in obj.data.uv_layers['FabricRegion'].data]
  assert all(abs(value-(0 if name=='Shirt' else 1))<1e-6 for value in region_values)
  if name=='Shorts':
   for vertex in obj.data.vertices:
    if abs(vertex.co.x)>.045 and vertex.co.z<.72:
     opposite='RightUpLeg' if vertex.co.x>0 else 'LeftUpLeg'
     assert not any(obj.vertex_groups[group.group].name==opposite and group.weight>1e-6 for group in vertex.groups)
 if name.startswith(('Arm_','Leg_')):
  side='Left' if name.endswith('_L') else 'Right'
  target=side+('UpLeg' if name.startswith('Leg_') else 'Arm')
  edge=(max(v.co.z for v in obj.data.vertices)-.045 if name.startswith('Leg_') else
        min(abs(v.co.x) for v in obj.data.vertices)+.035)
  seam=[v for v in obj.data.vertices if (v.co.z>edge if name.startswith('Leg_') else abs(v.co.x)<edge)]
  influence=[sum(g.weight for g in v.groups if obj.vertex_groups[g.group].name==target) for v in seam]
  assert influence and min(influence)>.999,(name,'skin at garment opening follows a different joint')
  report['parts'][name]['opening_min_upper_limb_weight']=min(influence)
height=max(v.z for v in coords)-min(v.z for v in coords)
assert abs(height-1.7018)<1e-4,height
report['height_m']=height
assert 'PlayerEyes' in bones
for side,suffix in [('Left','L'),('Right','R')]:
 ankle=arm.data.bones[side+'Foot'].head_local;toe=arm.data.bones[side+'ToeBase'].head_local;f=Vector((toe.x-ankle.x,toe.y-ankle.y,0)).normalized()
 shoe=parts['Shoe_'+suffix];p=[(v.co-ankle).dot(f) for v in shoe.data.vertices];length=max(p)-min(p)
 assert abs(length-.282)<1e-4,length
 report['parts'][shoe.name]['exterior_length_m']=length
 assert side+'SoleContact' in bones
 contact=arm.data.bones[side+'SoleContact'].head_local
 gap=min((v.co-contact).length for v in shoe.data.vertices)
 assert gap<1e-5,(side,gap)
 report['parts'][shoe.name]['contact_landmark_surface_gap_m']=gap
(root/'unreal/Saved/ModularPlayer/master-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('MODULAR MASTER CHECK PASSED',height,len(parts),flush=True)

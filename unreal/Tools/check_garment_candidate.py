"""Validate the downloaded garments against the preserved player skeleton."""
import bpy,bmesh,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/character/modular/player.blend'))
arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
rest={b.name:[list(row) for row in b.matrix_local] for b in arm.data.bones}
body={n:[tuple(v.co) for v in bpy.data.objects[n].data.vertices] for n in ('Arm_L','Arm_R','Leg_L','Leg_R','Shoe_L','Shoe_R','Head_Face','Body_Underlay')}
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/character/garment-candidate/player.blend'))
arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE');names={b.name for b in arm.data.bones}
for name,points in body.items():assert points==[tuple(v.co) for v in bpy.data.objects[name].data.vertices],(name,'source body changed')
assert names==set(rest)
for b in arm.data.bones:
 assert max(abs(b.matrix_local[r][c]-rest[b.name][r][c]) for r in range(4) for c in range(4))<1e-7,b.name
report={}
for name in ('Shirt','Shorts'):
 o=bpy.data.objects[name];assert not o.hide_render
 bad=0
 for v in o.data.vertices:
  w=[g.weight for g in v.groups if o.vertex_groups[g.group].name in names and g.weight>1e-6]
  bad+=not w or len(w)>4 or abs(sum(w)-1)>1e-5
 assert bad==0,(name,bad)
 o.data.calc_loop_triangles();areas=[]
 for t in o.data.loop_triangles:
  a,b,c=[o.data.vertices[i].co for i in t.vertices];areas.append((b-a).cross(c-a).length*.5)
 assert min(areas)>1e-10,(name,min(areas))
 assert len(areas)<25000
 assert len(o.data.uv_layers)>=2
 bm=bmesh.new();bm.from_mesh(o.data)
 assert all(len(edge.link_faces)<=2 for edge in bm.edges),(name,'branched render surface')
 bm.free()
 alpha=[v.color[3] for v in o.data.color_attributes['Fabric'].data]
 assert min(alpha)==0 and max(alpha)>.2,(name,min(alpha),max(alpha))
 report[name]={'vertices':len(o.data.vertices),'triangles':len(areas),'minimum_triangle_area_m2':min(areas),'bad_weights':bad,'mobility_range':[min(alpha),max(alpha)]}
for name in ('Shirt_Previous','Shorts_Previous','Head_Original','Collar'):assert bpy.data.objects[name].hide_render,name
for name in ('Shirt_Simulation','Shorts_Simulation'):
 o=bpy.data.objects[name];assert o.hide_render
 bm=bmesh.new();bm.from_mesh(o.data)
 assert all(len(edge.link_faces)<=2 for edge in bm.edges),(name,'branched simulation surface')
 report[name]={'vertices':len(bm.verts),'faces':len(bm.faces),'branched_edges':0}
 bm.free()
(ROOT/'unreal/Saved/GarmentCandidate/master-validation.json').write_text(json.dumps(report,indent=2))
print('GARMENT CANDIDATE CHECK PASSED',report,flush=True)

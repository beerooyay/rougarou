"""Bake local Mixamo locomotion onto the preserved CC4 rig, after build_corey_player.py."""
import bpy, argparse, sys, math
from pathlib import Path
from mathutils import Vector, Quaternion
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--source-dir',type=Path,default=ROOT/'assets/character/corey')
parser.add_argument('--align-rest-directions',action='store_true',help='Retarget an A-pose bind without adding the source T-pose arm drop twice')
parser.add_argument('--gym-jog',action='store_true',help='Use the authored Jogging cycle for the fast gait without a spine correction')
parser.add_argument('--upright-torso',action='store_true',help='Reduce jogging/running spine flexion while preserving the authored rest curve')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
OUT=args.source_dir;LIB=ROOT.parent/'rawah/rawah-lowpoly/assets/characters/anims';EXTRA=ROOT.parent/'rawah/rawah-unused/clips'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'player.blend'));arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
rest={b.name:b.matrix_local.copy() for b in arm.data.bones};local={b.name:b.parent.matrix_local.inverted()@b.matrix_local if b.parent else b.matrix_local.copy() for b in arm.data.bones}
for name,file in {'idle':'Neutral Idle','walk':'Swagger Walk','jog':'Jogging','run':'Sprint','start':'Start Walking','stop':'Stop Walking'}.items():
 # The sprint's deep forward fold does not suit the gym jog. Reuse the
 # upright authored jogging cycle at the faster gait clock for this option.
 if (args.gym_jog or args.upright_torso) and name=='run':file='Jogging'
 path=LIB/(file+'.fbx');path=path if path.exists() else EXTRA/(file+'.fbx');before=set(bpy.data.objects);bpy.ops.import_scene.fbx(filepath=str(path));new=set(bpy.data.objects)-before;src=next(o for o in new if o.type=='ARMATURE');source={b.name.split(':')[-1]:b for b in src.data.bones};pb={b.name.split(':')[-1]:b for b in src.pose.bones};sr={n:src.matrix_world@b.matrix_local for n,b in source.items()};first,last=map(int,src.animation_data.action.frame_range);period={'walk':41,'jog':26,'run':16}.get(name);period=26 if (args.gym_jog or args.upright_torso) and name=='run' else period;offset=0
 if period:
  candidates=[]
  for frame in range(first,first+period):
   bpy.context.scene.frame_set(frame);candidates.append((((src.matrix_world@pb['LeftFoot'].matrix).translation-(src.matrix_world@pb['Hips'].matrix).translation).y,frame-first))
  offset=min(candidates)[1];last=first+period
 aligned={n:m.to_quaternion() for n,m in rest.items()}
 if args.align_rest_directions:
  children={'Spine':'Spine1','Spine1':'Spine2','Spine2':'Neck','Neck':'Head'}
  for side in ('Left','Right'):
   for parent,child in [('Arm','ForeArm'),('ForeArm','Hand'),('Hand','HandMiddle1'),('UpLeg','Leg'),('Leg','Foot'),('Foot','ToeBase')]:children[side+parent]=side+child
   for digit in ('Thumb','Index','Middle','Ring','Pinky'):
    for j in (1,2):children[f'{side}Hand{digit}{j}']=f'{side}Hand{digit}{j+1}'
  for n,child in children.items():
   if n in sr and child in sr and n in rest and child in rest:
    target_axis=(rest[child].translation-rest[n].translation).normalized()
    source_axis=(sr[child].translation-sr[n].translation).normalized()
    aligned[n]=target_axis.rotation_difference(source_axis)@rest[n].to_quaternion()
 arm.animation_data_create();action=bpy.data.actions.new(name);arm.animation_data.action=action;scale=rest['Hips'].translation.z/sr['Hips'].translation.z
 for f in range(first,last+1):
  bpy.context.scene.frame_set(first+(f-first+offset)%period if period else f);desired={}
  for b in arm.data.bones:
   n=b.name;p=b.parent.name if b.parent else None
   q=(src.matrix_world@pb[n].matrix).to_quaternion()@sr[n].to_quaternion().inverted()@aligned[n] if n in pb else (desired[p]@local[n].to_quaternion() if p else rest[n].to_quaternion())
   if args.upright_torso and name in ('jog','run') and n in ('Spine','Spine1','Spine2'):
    q=Quaternion((1,0,0),math.radians(-8)*{'Spine':.5,'Spine1':.8,'Spine2':1}[n])@q
   desired[n]=q.normalized();pose=arm.pose.bones[n];pose.rotation_mode='QUATERNION';pose.rotation_quaternion=(local[n].to_quaternion().inverted()@(desired[p].inverted()@q if p else q)).normalized();pose.location=(0,0,0);pose.scale=(1,1,1)
   if n=='Hips':
    dz=((src.matrix_world@pb[n].matrix).translation.z-sr[n].translation.z)*scale;pose.location=rest[n].to_quaternion().inverted()@Vector((0,0,dz))
   pose.keyframe_insert('rotation_quaternion',frame=f);pose.keyframe_insert('location',frame=f);pose.keyframe_insert('scale',frame=f)
 bpy.context.scene.frame_start=first;bpy.context.scene.frame_end=last;bpy.ops.object.select_all(action='DESELECT');arm.select_set(True);bpy.context.view_layer.objects.active=arm
 bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),use_selection=True,object_types={'ARMATURE'},add_leaf_bones=False,bake_anim=True,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,axis_forward='-Y',axis_up='Z',armature_nodetype='NULL')
 print('BAKED',name,first,last,flush=True)
 for o in new:bpy.data.objects.remove(o,do_unlink=True)
 arm.animation_data.action=None;bpy.data.actions.remove(action)
print('COREY LOCOMOTION READY',flush=True)

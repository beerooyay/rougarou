import fs from 'node:fs';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import * as T from 'three';
import {Skin} from '../../web/skin.js';
import {Climber} from '../../web/climber.js';
import {Sim,Vec2} from '../../web/sim.js';
import {clampRel} from '../../web/human-motion.js';
import {problems,routeHolds,buildGym} from '../../web/gym.js';
import {sceneClearance} from '../../web/hold-contact.js';
const root=fileURLToPath(new URL('../../web/',import.meta.url));
function rig(){const data=fs.readFileSync(root+'models/blaize-player.glb'),g=JSON.parse(data.subarray(20,20+data.readUInt32LE(12))),joints=new Set(g.skins.flatMap(s=>s.joints));const nodes=g.nodes.map((n,i)=>{const b=joints.has(i)?new T.Bone():new T.Object3D();b.name=n.name||'';if(n.translation)b.position.fromArray(n.translation);if(n.rotation)b.quaternion.fromArray(n.rotation);if(n.scale)b.scale.fromArray(n.scale);return b;});g.nodes.forEach((n,i)=>n.children?.forEach(c=>nodes[i].add(nodes[c])));const scene=new T.Scene(),model=new T.Group();g.scenes[g.scene||0].nodes.forEach(i=>model.add(nodes[i]));return {scene,skin:new Skin({scene:model},scene,JSON.parse(fs.readFileSync(root+'human-motion.json')))};}
const routes=problems();routes.forEach(routeHolds);
for(const mode of ['frozen','release','live']){
 const {scene,skin}=rig(),c=new Climber(scene),sim=new Sim(routes);let frozenPose=null,previous=null,max=0,culprit='',jump=0,late=0;
 for(let f=0;f<300;f++){
  if(mode==='release'&&f===90)sim.drop(1);
  if(mode!=='frozen')sim.step(1/60);
  const pose=mode==='frozen'&&frozenPose?frozenPose:c.climbPose(sim,-1,1/60);if(mode==='frozen'&&f===60)frozenPose=pose;skin.pose(pose,1/60);
  skin.root.updateMatrixWorld(true);
  const now=skin.order.map(b=>({q:b.quaternion.clone(),p:b.getWorldPosition(new T.Vector3())}));
  if(previous&&f>60)for(let i=0;i<now.length;i++){if(/Hem|Cloth|Shirt/.test(skin.order[i].name))continue;const a=previous[i].q.angleTo(now[i].q)*180/Math.PI;if(f>240)late=Math.max(late,a);if(a>max){max=a;culprit=skin.order[i].name+' frame '+f;}jump=Math.max(jump,previous[i].p.distanceTo(now[i].p)*100);}
  previous=now;
 }
 assert(max<5.731, `${mode}: joint snapped ${max} degrees (${culprit})`);
 assert(jump<6, `${mode}: landmark jumped ${jump} cm`);
 if(mode==='frozen')assert(late<1, `stationary rig keeps twitching: ${late} degrees`);
 const before=skin.order.map(b=>b.quaternion.clone());
 skin.pose(frozenPose||c.climbPose(sim,-1,0),0);
 skin.order.forEach((b,i)=>assert(before[i].equals(b.quaternion), `paused ${b.name} moved`));
 console.log({mode,maxDegrees:max,maxMoveCm:jump,lateDegrees:late,culprit});
}

// Losing a support used to switch a 20 cm height correction in one frame.
for(const limb of [0,1,2,3]){
 const sim=new Sim(routes),c=new Climber(new T.Scene());let pose;
 for(let f=0;f<180;f++){sim.step(1/60);pose=c.climbPose(sim,-1,1/60);}
 const before=pose.roots[0];
 sim.target=sim.tips[limb].copy();
 const selected=c.climbPose(sim,limb,1/60);
 assert(selected.roots[0].sub(before).len()<.1,'selection changed body height');
 sim.drop(limb);sim.reaching=limb;
 const released=c.climbPose(sim,limb,1/60);
 assert(released.roots[0].sub(selected.roots[0]).len()<2,'support release snapped pelvis');
 assert(c.hips>=42,'pelvis lost its wall clearance');
}
// A free hand and a crimp must produce distinct articulated finger poses.
{
 const {scene,skin}=rig(),c=new Climber(scene),sim=new Sim(routes);
 let pose;
 for(let f=0;f<120;f++){sim.step(1/60);pose=c.climbPose(sim,-1,1/60);skin.pose(pose,1/60);}
 const hand=pose.limbs[2].hand;delete hand.surface;
 const digit=skin.bone.RightHandMiddle2;
 hand.grip='free';for(let f=0;f<90;f++)skin.pose(pose,1/60);
 const open=digit.quaternion.clone();
 hand.grip='crimp';for(let f=0;f<90;f++)skin.pose(pose,1/60);
 const flex=open.angleTo(digit.quaternion)*180/Math.PI;
 assert(flex>15,`finger stuck between free and crimp: ${flex} degrees`);
 console.log({fingerFlexDegrees:flex,supportTransitions:'passed'});
}

// Matching a high hand exposed a shoulder-limit violation between legal targets.
// Follow that with high steps and measure the actual shoe, not the sim's tip.
{
 const {scene,skin}=rig(),c=new Climber(scene),sim=new Sim(routes);
 const shifts=[new Vec2(-1,0),new Vec2(1,0),new Vec2(0,1)];
 let pose,violation=0,worstInward=0;
 const draw=()=>{skin.pose(pose,1/60);
  skin.root.updateMatrixWorld(true);
  const axis=skin.bone.LeftUpLeg.getWorldPosition(new T.Vector3()).sub(skin.bone.RightUpLeg.getWorldPosition(new T.Vector3())).normalize();
  for(const [side,sign] of [['Right',-1],['Left',1]]){
   const knee=skin.bone[side+'Leg'].getWorldPosition(new T.Vector3()),hip=skin.bone[side+'UpLeg'].getWorldPosition(new T.Vector3());
   worstInward=Math.max(worstInward,-knee.sub(hip).dot(axis)*sign*100);
  }
  assert(skin.bodyOffset.dot(new T.Vector3(-pose.back.y,pose.back.z,pose.back.x))>=-1e-7,'contact correction cancelled hip clearance');
  for(const [b,bound] of skin.motion.bounds){
  const rel=skin.motion.neutral.get(b).clone().invert().multiply(b.quaternion);
  violation=Math.max(violation,clampRel(rel,skin.motion.frames.get(b),bound).q.angleTo(rel)*180/Math.PI);
 }};
 const tick=()=>{sim.step(1/60);pose=c.climbPose(sim,-1,1/60);draw();};
 const errors=[];
 for(const [lean,limb,hold] of [[2,2,7],[0,3,7],[-1,1,6],[2,0,2]]){
  if(lean>=0){sim.lean=shifts[lean].mul(.6);for(let i=0;i<30;i++)tick();sim.lean=new Vec2();}
  assert(sim.grab(limb,hold),'fixture hold must be reachable');
  for(let i=0;i<45;i++)tick();
  for(let i=0;i<120;i++)draw();
  skin.root.updateMatrixWorld(true);
  if(limb<2){const target=pose.limbs[limb].foot.toe;
   const shoe=skin.bone[(limb%2?'Left':'Right')+'SoleContact'].getWorldPosition(new T.Vector3());
   const error=shoe.distanceTo(new T.Vector3(-target.y/100,target.z/100,target.x/100))*100;
   errors.push(error);assert(error<6,`high-step shoe missed by ${error} cm`);
  }
 }
 assert(violation<.1,`joint escaped limits by ${violation} degrees`);
 assert(worstInward<4,`knee collapsed inward ${worstInward} cm`);
 console.log({highStepSoleErrorsCm:errors,maxJointLimitErrorDegrees:violation,worstInwardKneeCm:worstInward});
}
// The inverse click mapping must round-trip across different hold sizes/angles.
{
 const sim=new Sim(routes);
 for(const route of routes)for(const hold of route.holds)for(const across of [-.8,0,.8]){
  const point=sim.gripPoint(hold,across),side=new Vec2(-hold.pull.y,hold.pull.x).norm();
  assert(Math.abs(point.sub(hold.point).dot(side)/sim.gripWidth(hold)-across)<1e-8);
 }
}

// A sole point alone can pass while the ankle rolls the shoe sideways.
// Exercise both feet stepping up and returning to either edge of the start hold.
for(const limb of [0,1])for(const across of [-1,1]){
 const {scene,skin}=rig(),c=new Climber(scene),sim=new Sim(routes);let pose;
 const tick=n=>{for(let f=0;f<n;f++){sim.step(1/60);pose=c.climbPose(sim,-1,1/60);skin.pose(pose,1/60);}};
 tick(90);
 assert(sim.grab(limb,4,across),'step fixture must be reachable');tick(90);
 assert(sim.grab(limb,limb,across),'return fixture must be reachable');tick(90);
 skin.root.updateMatrixWorld(true);
 const feet=[0,1].map(l=>{
  const side=l?'Left':'Right',foot=skin.bone[side+'Foot'],part=pose.limbs[l].foot;
  const forward=skin.motion.front.clone().applyQuaternion(skin.restWorld.get(foot).q.clone().invert()).applyQuaternion(foot.getWorldQuaternion(new T.Quaternion()));
  const wanted=part.toe.sub(part.heel),target=new T.Vector3(-part.toe.y/100,part.toe.z/100,part.toe.x/100);
  const angle=forward.angleTo(new T.Vector3(-wanted.y,wanted.z,wanted.x))*180/Math.PI;
  const miss=skin.bone[side+'SoleContact'].getWorldPosition(new T.Vector3()).distanceTo(target)*100;
  // Edging permits toe rotation; do not force a wall-normal shoe and lock the knee.
  assert(angle<75,`${side} shoe turned backward after step/return: ${angle} degrees`);
  assert(miss<1.5,`${side} sole missed after step/return: ${miss} cm`);
  return {side,angle,miss};
 });
 console.log({stepReturn:{limb,across,feet}});
}

// A stationary joint can pass a jitter test. Require actual flexion when a
// target moves, while keeping the same per-frame angular budget.
for(const limb of [0,1,2,3]){
 const {scene,skin}=rig(),c=new Climber(scene),sim=new Sim(routes);let pose;
 for(let f=0;f<120;f++){sim.step(1/60);pose=c.climbPose(sim,-1,1/60);skin.pose(pose,1/60);}
 const part=pose.limbs[limb],side=limb%2?'Left':'Right',joint=skin.bone[side+(limb<2?'Leg':'ForeArm')];
 const before=joint.quaternion.clone(),shift=part.end.mul(0);shift.z=limb<2?30:-25;
 part.end=part.end.add(shift);
 if(limb<2){part.foot.toe=part.foot.toe.add(shift);part.foot.heel=part.foot.heel.add(shift);}
 else{part.hand.contact=part.hand.contact.add(shift);part.hand.palm=part.hand.palm.add(shift);delete part.hand.surface;delete part.hand.clearance;}
 let previous=before,maxStep=0;
 for(let f=0;f<180;f++){skin.pose(pose,1/60);maxStep=Math.max(maxStep,previous.angleTo(joint.quaternion)*180/Math.PI);previous=joint.quaternion.clone();}
 const flex=before.angleTo(joint.quaternion)*180/Math.PI;
 assert(flex>5,`${joint.name} stayed locked after its target moved: ${flex} degrees`);
 assert(maxStep<5.731,`${joint.name} snapped while bending: ${maxStep} degrees`);
 console.log({respondingJoint:joint.name,flexDegrees:flex,maxStepDegrees:maxStep});
}
{
 const route=routes[0],behind=route.map(new Vec2(0,.8),-.5);
 assert(sceneClearance(behind,route.panels,[])<0,'bare wall penetration was missed');
 const gym=buildGym(routes);
 assert(gym.obstacles.length>0,'decorative holds are missing collision descriptors');
 for(const hold of [route.holds[0],gym.obstacles[0]]){
  const surface=hold.surface,x=(surface.min.x+surface.max.x)/2,y=(surface.min.y+surface.max.y)/2;
  const point=surface.origin.mul(0);point.x=x;point.y=y;point.z=surface.height(x,y)-.5;
  assert(sceneClearance(surface.world(point),[],[hold])<0,'non-selected hold penetration was missed');
 }
 console.log({collisionCoverage:'bare wall, route holds, decorative holds'});
}

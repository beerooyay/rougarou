// Integration audit: simulation latching is compared with rendered skeleton contacts.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import * as T from 'three';
import {rig} from './fixture.mjs';
import {Climber} from '../../web/climber.js';
import {Sim,Vec3} from '../../web/sim.js';
import {problems,routeHolds,buildGym} from '../../web/gym.js';
const routes=problems();routes.forEach(routeHolds);
const gym=buildGym(routes),obstacles=[...gym.obstacles,...routes.flatMap(r=>r.holds)];
const rows=[];
for(let route=0;route<routes.length;route++){
 if(process.env.AUDIT_ROUTE && !routes[route].name.includes(process.env.AUDIT_ROUTE))continue;
 const {scene,skin}=rig(),climber=new Climber(scene),sim=new Sim(routes);sim.route=route;sim.restart();
 climber.collisionHolds=()=>obstacles;
 let pose;
 const tick=n=>{for(let f=0;f<n;f++){sim.step(1/60);pose=climber.climbPose(sim,-1,1/60);skin.pose(pose,1/60);}skin.root.updateMatrixWorld(true);};
 const measure=stage=>{
  assert(!sim.fallen,`${routes[route].name}: unexpectedly fell during ${stage}`);
  assert(sim.grips.every((h,l)=>h>=0&&sim.arrived(l)),`${routes[route].name}: missing contact during ${stage}`);
  const contacts=[0,1,2,3].map(l=>{
   const side=l%2?'Left':'Right',target=l<2?pose.limbs[l].foot.toe:pose.limbs[l].hand.contact;
   const b=skin.bone[side+(l<2?'SoleContact':'HandMiddle1')];
   const miss=target?b.getWorldPosition(new T.Vector3()).distanceTo(new T.Vector3(-target.y/100,target.z/100,target.x/100))*100:null;
   const a=skin.bone[side+(l<2?'UpLeg':'Arm')],m=skin.bone[side+(l<2?'Leg':'ForeArm')],e=skin.bone[side+(l<2?'Foot':'Hand')];
   const p=x=>x.getWorldPosition(new T.Vector3()),span=p(a).distanceTo(p(m))+p(m).distanceTo(p(e));
   const error=target?new T.Vector3(-target.y/100,target.z/100,target.x/100).sub(b.getWorldPosition(new T.Vector3())).multiplyScalar(100).toArray():null;
   return {errorCm:error,limb:l,hold:sim.grips[l],latched:sim.arrived(l)&&sim.grips[l]>=0,missCm:miss,extension:p(a).distanceTo(p(e))/span};
  });
  let minDigitClearanceCm=Infinity,worstDigit=null;
  for(const limb of [2,3]){
   const hand=pose.limbs[limb].hand,side=limb%2?'Left':'Right';
   for(const digit of ['Thumb','Index','Middle','Ring','Pinky']){
    const bones=[1,2,3].map(j=>skin.bone[side+'Hand'+digit+j]),points=bones.map(b=>b.getWorldPosition(new T.Vector3()));
    const offset=skin.restWorld.get(bones[2]).p.clone().sub(skin.restWorld.get(bones[1]).p).applyQuaternion(skin.restWorld.get(bones[2]).q.clone().invert()).multiplyScalar(.8);
    points.push(points[2].clone().add(offset.applyQuaternion(bones[2].getWorldQuaternion(new T.Quaternion()))));
    const samples=[...points,...points.slice(1).map((p,i)=>p.clone().lerp(points[i],.5))];
    for(const p of samples){
     const clearance=hand.clearance(new Vec3(p.z*100,-p.x*100,p.y*100));
     if(clearance<minDigitClearanceCm){minDigitClearanceCm=clearance;worstDigit={limb,digit,point:p.toArray(),sample:samples.indexOf(p),ownHoldClearanceCm:hand.surface.clear(new Vec3(p.z*100,-p.x*100,p.y*100))};}
    }
   }
  }
  rows.push({minDigitClearanceCm,worstDigit,route:routes[route].name,stage,bodyOffset:skin.bodyOffset.toArray(),fallen:sim.fallen,hipDepthCm:climber.hips,contacts});
 };
 tick(180);measure('start');
 for(const limb of [0,1,2,3]){
  const choices=sim.holds().map((h,i)=>({h,i})).filter(({i})=>i!==sim.grips[limb]).sort((a,b)=>b.h.point.y-a.h.point.y);
  const choice=choices.find(({i})=>sim.grab(limb,i));
  assert(choice,`${routes[route].name}: no test move for limb ${limb}`);
  tick(150);measure(`move ${limb} to ${choice.i}`);
  assert(sim.grab(limb,limb),`${routes[route].name}: return failed for limb ${limb}`);
  tick(150);measure(`return ${limb}`);
 }
 console.log('audited',routes[route].name);
}
const failures=rows.flatMap(row=>row.contacts.filter(c=>c.latched&&c.missCm>2).map(c=>({route:row.route,stage:row.stage,...c})));
const penetrationFailures=rows.filter(r=>r.minDigitClearanceCm<-.1).map(r=>({route:r.route,stage:r.stage,minDigitClearanceCm:r.minDigitClearanceCm}));
const result={penetrationFailures,scope:'Native skeleton; nine routes; starts and reachable foot/hand moves and returns. Contact tolerance 2 cm. Not a full mesh collision or physical input proof.',rows,failures};
fs.writeFileSync(process.argv[2]||'/tmp/rougarou-climbing-audit.json',JSON.stringify(result,null,2));
console.log(JSON.stringify({poses:rows.length,falseContactCount:failures.length,worstMissCm:Math.max(0,...rows.flatMap(r=>r.contacts.filter(c=>c.latched).map(c=>c.missCm)))}));
console.log(JSON.stringify({sampledDigitPenetrations:penetrationFailures.length,minDigitClearanceCm:Math.min(...rows.map(r=>r.minDigitClearanceCm))}));
process.exitCode=failures.length||penetrationFailures.length?1:0;

// Swing/twist math ported from ceeboozwah/rougarou, commit 3d30a18.
// The adapter below re-expresses its neutral profile on either an A- or T-pose bind.
import * as THREE from 'three';
const V=THREE.Vector3, Q=THREE.Quaternion, DEG=180/Math.PI;
const arc=(a,b)=>new Q().setFromUnitVectors(a,b);
const bearing=f=>({D:new V().crossVectors(f.F,f.T).normalize(),E:new V().crossVectors(f.S,f.T).normalize()});
export function measure(rel, frame) {
	const q = rel.clone().normalize();
	const { D, E } = bearing(frame);
	const T2 = frame.T.clone().applyQuaternion(q);
	const theta = Math.acos(Math.min(1, Math.max(-1, T2.dot(frame.T))));
	const phi = Math.atan2(T2.dot(E), T2.dot(D));
	// what is left once the swing has been taken off is a turn about the bone
	const rest = arc(frame.T, T2).invert().multiply(q);
	let twist = 2 * Math.atan2(rest.x * frame.T.x + rest.y * frame.T.y + rest.z * frame.T.z, rest.w);
	if (twist > Math.PI) twist -= 2 * Math.PI;
	if (twist < -Math.PI) twist += 2 * Math.PI;
	return { theta: theta * DEG, phi: phi * DEG, twist: twist * DEG * frame.side, T2 };
}

// the most the bone may swing in the direction Phi around it (radians), from the four limits
export function reach(lim, phi) {
	const c = Math.cos(phi), s = Math.sin(phi);
	const a = Math.max(0.5, (c >= 0 ? lim.fwd : lim.back) ?? 180), b = Math.max(0.5, (s >= 0 ? lim.out : lim.inn) ?? 180);
	return Math.min(180, 1 / Math.sqrt((c / a) ** 2 + (s / b) ** 2));
}

// hold a turn from rest inside the limits. Returns { q, hit } (q is the same turn if it was inside)
export function clampRel(rel, frame, lim) {
	const m = measure(rel, frame);
	const { D, E } = bearing(frame);
	const phi = m.phi / DEG;
	const most = reach(lim, phi);
	const lo = lim.twist?.[0] ?? -360, hi = lim.twist?.[1] ?? 360;
	const twist = Math.min(hi, Math.max(lo, m.twist));
	if (m.theta <= most + 1e-6 && twist === m.twist) return { q: rel, hit: false, values: m };
	const theta = Math.min(m.theta, most) / DEG;
	const T2 = frame.T.clone().multiplyScalar(Math.cos(theta)).addScaledVector(D, Math.sin(theta) * Math.cos(phi)).addScaledVector(E, Math.sin(theta) * Math.sin(phi)).normalize();
	return { q: arc(frame.T, T2).multiply(new Q().setFromAxisAngle(frame.T, twist / frame.side / DEG)), hit: true, values: m };
}

export class HumanMotion {
 constructor(skin, profile) {
  this.skin=skin;this.profile=profile;this.frames=new Map();this.neutral=new Map();this.bounds=new Map();this.previous=new Map();
  const {bone,order,restLocal,restWorld,refUp,refSide}=skin;
  const front=restWorld.get(bone.LeftToeBase).p.clone().sub(restWorld.get(bone.LeftFoot).p).add(restWorld.get(bone.RightToeBase).p).sub(restWorld.get(bone.RightFoot).p);
  front.addScaledVector(refUp,-front.dot(refUp)).normalize();this.front=front;
  const neutralWorld=new Map();
  for(const b of order) {
   let q=restLocal.get(b).q.clone();
   if(b.name==='LeftArm'||b.name==='RightArm') {
    const next=bone[b.name.replace('Arm','ForeArm')];
    const axis=restWorld.get(next).p.clone().sub(restWorld.get(b).p).normalize();
    const drop=(profile.native?.arm_rest_drop_degrees??32)/DEG;
    const lateral=axis.clone().addScaledVector(refUp,-axis.dot(refUp)).normalize();
    const wanted=lateral.multiplyScalar(Math.cos(drop)).addScaledVector(refUp,-Math.sin(drop)).normalize();
    const world=arc(axis,wanted).multiply(restWorld.get(b).q);
    q=restWorld.get(b.parent).q.clone().invert().multiply(world);
   }
   this.neutral.set(b,q);
   const parent=neutralWorld.get(b.parent)||skin.base;
   neutralWorld.set(b,{p:restLocal.get(b).p.clone().applyQuaternion(parent.q).add(parent.p),q:parent.q.clone().multiply(q)});
  }
  const aliases={Spine:'Waist',Spine1:'Spine01',Spine2:'Spine02',Neck:'NeckTwist01',Head:'Head'};
  const points={Spine:'Spine1',Spine1:'Spine2',Spine2:'Neck',Neck:'Head'};
  const pairs=[['Shoulder','Arm','Clavicle'],['Arm','ForeArm','Upperarm'],['ForeArm','Hand','Forearm'],['Hand','HandMiddle1','Hand'],['UpLeg','Leg','Thigh'],['Leg','Foot','Calf'],['Foot','ToeBase','Foot']];
  for(const side of ['Left','Right'])for(const [part,next,alias] of pairs){aliases[side+part]=(side==='Left'?'L_':'R_')+alias;points[side+part]=side+next;}
  for(const [name,alias] of Object.entries(aliases)) {
   const b=bone[name],bound=profile.limits[alias];if(!b||!bound)continue;
   const here=neutralWorld.get(b),next=neutralWorld.get(bone[points[name]]),parent=neutralWorld.get(b.parent);
   const T=(next?next.p.clone().sub(here.p):here.p.clone().sub(parent.p)).normalize();
   const reference=Math.abs(T.dot(front))>.9?refUp:front;
   const F=T.clone().cross(reference).normalize(),S=T.clone().cross(F).normalize();
   const left=name.startsWith('Left'),arm=alias.includes('Upperarm')||alias.includes('Clavicle');
   const out=arm?refUp:alias.includes('Thigh')||alias.includes('Calf')?refSide.clone().multiplyScalar(left?1:-1):refSide;
   if(S.clone().cross(T).dot(out)<0)S.negate();
   const inv=here.q.clone().invert();this.frames.set(b,{T:T.applyQuaternion(inv),F:F.applyQuaternion(inv),S:S.applyQuaternion(inv),side:left?-1:1});this.bounds.set(b,bound);
  }
 }
 limit(b,world,parent) {
  if(!this.bounds.has(b))return world;
  const neutral=this.neutral.get(b),local=parent.clone().invert().multiply(world);
  const rel=neutral.clone().invert().multiply(local);
  return parent.clone().multiply(neutral).multiply(clampRel(rel,this.frames.get(b),this.bounds.get(b)).q).normalize();
 }
 // Run after FK/IK. Rotations stay bounded in time and never alter joint offsets.
 smooth(delta) {
  const {order,restLocal}=this.skin;
  if(delta<=0)return;
  const max=(this.profile.native?.joint_rate_radians??8)*Math.min(delta,.05);
  for(const b of order) {
   const previous=this.previous.get(b),q=b.quaternion.clone(),cap=b.name.startsWith('Spine')?max*.5:max;
   if(previous){const angle=previous.angleTo(q),rate=cap;if(angle>rate)b.quaternion.copy(previous).slerp(q,rate/angle);}
   if(this.bounds.has(b)) {
    const n=this.neutral.get(b),rel=n.clone().invert().multiply(b.quaternion);
    const constrained=clampRel(rel,this.frames.get(b),this.bounds.get(b)).q;
    b.quaternion.copy(n).multiply(constrained);
   }
   if(previous&&previous.angleTo(b.quaternion)>cap+1e-7){
    for(let attempt=0;attempt<8&&previous.angleTo(b.quaternion)>cap+1e-7;attempt++){
     const angle=previous.angleTo(b.quaternion),next=previous.clone().slerp(b.quaternion,cap/angle*.98);
     if(this.bounds.has(b)){const n=this.neutral.get(b);b.quaternion.copy(n).multiply(clampRel(n.clone().invert().multiply(next),this.frames.get(b),this.bounds.get(b)).q);}else b.quaternion.copy(next);
    }
    if(previous.angleTo(b.quaternion)>cap+1e-7)b.quaternion.copy(previous);
   }
   if(b.name!=='Hips')b.position.copy(restLocal.get(b).p);
   this.previous.set(b,b.quaternion.clone());
  }
 }
 reset(){this.previous.clear();}
}

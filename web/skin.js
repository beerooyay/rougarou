// Native shoulder-v2 player, exported with its original skeleton and sole landmarks.
// bones are aimed joint-to-joint from the same solved pose the mannequin draws: hips from the leg roots, torso from
// the shoulders, limbs through a two-bone solve at the rig's own lengths, with facing frames for knees, soles and palms.

import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { toThree } from './gym.js';
import { Vec3 } from './sim.js';
import { HumanMotion } from './human-motion.js';
import { GarmentPhysics,garmentNames } from './garment-physics.js';

const three = (v) => new THREE.Vector3(...toThree(v));
// Directions change axes, never units. Positions above convert cm to meters.
const unit = (v) => new THREE.Vector3(-v.y,v.z,v.x).normalize();
const sideOf = (limb) => limb % 2 === 0 ? -1 : 1;
// x = the segment direction, y hint orthogonalized into the facing; an orthonormal frame for each bone
const frame = (x, y) => {
	x = x.clone().normalize();
	y = y.clone().sub(x.clone().multiplyScalar(y.dot(x)));
	y = y.lengthSq() > 1e-10 ? y.normalize() : new THREE.Vector3(0, 1, 0);
	const z = x.clone().cross(y);
	return new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(x, y, z));
};
// where the knee should point: out to the side low, folding upward on a high step, never back into the wall
const knee = (side, up, out, sign, height) =>
	side.clone().multiplyScalar(sign * (1 - 0.7 * height)).add(up.clone().multiplyScalar(0.2 + 0.8 * height)).sub(out.clone().multiplyScalar(0.25));

function solve(start, target, hint, upper, lower) {
	upper = Math.max(upper, 0.01); lower = Math.max(lower, 0.01);
	const gap = target.clone().sub(start), distance = gap.length();
	// Match the native rig: clamp reach instead of stretching the skin.
	const reach = Math.min(Math.max(distance, Math.abs(upper - lower) + 0.001), upper + lower - 0.001);
	const direction = distance > 0.001 ? gap.divideScalar(distance) : new THREE.Vector3(0, 0, 1);
	let pole = hint.clone().sub(start);
	pole = pole.sub(direction.clone().multiplyScalar(pole.dot(direction)));
	pole.normalize();
	if (!pole.lengthSq()) pole = direction.clone().cross(new THREE.Vector3(0, 1, 0)).normalize();
	const along = (upper * upper + reach * reach - lower * lower) / (2 * reach);
	const bend = start.clone().add(direction.clone().multiplyScalar(along)).add(pole.multiplyScalar(Math.sqrt(Math.max(0, upper * upper - along * along))));
	const end = start.clone().add(direction.clone().multiplyScalar(reach));
	return { bend, end };
}

export async function loadSkin(scene) {
 // Ship the same player used by the native launcher; retain the older asset as fallback.
 let model = null;
 for (const candidate of ['./models/blaize-player.glb', './models/skin.glb']) {
  const response = await fetch(candidate, { method: 'HEAD' }).catch(() => null);
  if (response?.ok) { model = candidate; break; }
 }
 if (!model) return null;
 const gltf = await new GLTFLoader().loadAsync(model);
	const response=await fetch('./human-motion.json');
	if(!response.ok)throw new Error('human motion profile missing');
	return new Skin(gltf, scene, await response.json());
}

const Upper = ['RightUpLeg', 'LeftUpLeg', 'RightArm', 'LeftArm'];
const Lower = ['RightLeg', 'LeftLeg', 'RightForeArm', 'LeftForeArm'];
const End = ['RightFoot', 'LeftFoot', 'RightHand', 'LeftHand'];
const Tip = ['RightToeBase', 'LeftToeBase', 'RightHandMiddle1', 'LeftHandMiddle1'];
const Fingers = ['Index', 'Middle', 'Ring', 'Pinky'];

export class Skin {
	constructor(gltf, scene, profile) {
		this.root = gltf.scene;
		scene.add(this.root);
		this.bone = {};
		this.order = [];
		this.restLocal = new Map();
		this.restWorld = new Map();
		this.root.traverse(o => {
			if (o.isMesh) { o.frustumCulled = false; if (o.material) o.material.side = THREE.FrontSide; }
			if (o.isBone || o.type === 'Bone') { this.bone[o.name] = o; this.order.push(o); }
		});
		this.root.updateMatrixWorld(true);
		for (const b of this.order) {
			this.restLocal.set(b, { p: b.position.clone(), q: b.quaternion.clone() });
			this.restWorld.set(b, { p: b.getWorldPosition(new THREE.Vector3()), q: b.getWorldQuaternion(new THREE.Quaternion()) });
		}
		this.world = new Map();
		this.wants = new Map();
		// the armature's world frame: the space local bone transforms live in
		const arm = this.order[0]?.parent || this.root;
		this.base = { p: arm.getWorldPosition(new THREE.Vector3()), q: arm.getWorldQuaternion(new THREE.Quaternion()) };
		const at = (n) => this.restWorld.get(this.bone[n]);
		this.refUp = at('Spine2').p.clone().sub(at('Hips').p).normalize();
		this.refSide = at('LeftArm').p.clone().sub(at('RightArm').p).normalize();
		this.refPelvis = at('LeftUpLeg').p.clone().add(at('RightUpLeg').p).divideScalar(2);
		this.motion=new HumanMotion(this,profile);
		this.chainTargets=new Map();this.swivels=[0,0,0,0];this.bodyOffset=new THREE.Vector3();
		this.garments=new GarmentPhysics();
	}

	// where a bone currently lands: a wanted world transform if set, else forward kinematics through posed parents
	worldOf(b) {
		if (this.wants.has(b)) {
   const wanted=this.wants.get(b);
   if(b.name==='Hips')return wanted;
   const parent=b.parent&&this.bone[b.parent.name]?this.worldOf(b.parent):this.base;
   return {p:this.restLocal.get(b).p.clone().applyQuaternion(parent.q).add(parent.p),q:wanted.q};
  }
		if (!this.world.has(b)) {
			const parent = b.parent && this.bone[b.parent.name] ? this.worldOf(b.parent) : this.base;
			const r = this.restLocal.get(b);
			this.world.set(b, { p: r.p.clone().applyQuaternion(parent.q).add(parent.p), q: parent.q.clone().multiply(r.q) });
		}
		return this.world.get(b);
	}

	// drive a bone so its axis points from At toward Direction, keeping Facing in the y-plane
	aim(b, next, at, direction, facing, front, temporal=true) {
		if (direction.lengthSq() < 1e-8) return;
		const here = this.worldOf(b).p;
		const current = next ? this.worldOf(next).p.clone().sub(here).normalize()
			: here.clone().sub(this.worldOf(b.parent).p).normalize();
		const target = direction.clone().normalize();
		const rest = this.restWorld.get(b);
		let q;
		if (facing && front) {
			const r = this.restWorld.get(next);
			const axis = r.p.clone().sub(rest.p).normalize();
			const from = frame(axis, front.clone().normalize());
			const to = frame(target, facing.clone().normalize());
			q = to.multiply(from.invert()).multiply(rest.q);
		} else {
			q = new THREE.Quaternion().setFromUnitVectors(current, target).multiply(this.worldOf(b).q);
		}
		const parent=b.parent&&this.bone[b.parent.name]?this.worldOf(b.parent):this.base;
		q=this.motion.limit(b,q,parent.q,temporal);
		// A pose may rotate joints; only the pelvis can translate the skeleton.
		const p=this.worldOf(b).p.clone();
		this.wants.set(b, {p,q});this.world.clear();
	}

 fitFingers(side,hand) {
  const surface=hand.surface,over=unit(hand.over),inside=unit(hand.out).negate();
  const clear=p=>{const point=new Vec3(p.z*100,-p.x*100,p.y*100);return hand.clearance?hand.clearance(point):surface?surface.clear(point):100;};
  const rest=this.restWorld,world=b=>this.worldOf(b),chain=f=>[1,2,3].map(j=>this.bone[`${side}Hand${f}${j}`]);
  const tipOf=ch=>{
   const last=ch[2],offset=rest.get(last).p.clone().sub(rest.get(ch[1]).p).applyQuaternion(rest.get(last).q.clone().invert());
   return world(last).p.clone().add(offset.multiplyScalar(.8).applyQuaternion(world(last).q));
  };
  // Fit the full target first. Applying velocity limits during candidate search
  // makes every candidate look like the previous open hand and traps the fit.
  const settle=ch=>{
   const locals=ch.map(b=>world(b.parent).q.clone().invert().multiply(world(b).q));
   ch.forEach((b,j)=>{const parent=world(b.parent),q=parent.q.clone().multiply(locals[j]);
    this.wants.set(b,{p:world(b).p.clone(),q:this.motion.limit(b,q,parent.q)});this.world.clear();
   });
  };
  for(const finger of Fingers){
   const ch=chain(finger);if(ch.some(b=>!b))continue;
   const angles=hand.grip==='crimp'?[12,80,55]:hand.grip==='jug'?[16,58,76]:hand.grip==='pinch'?[20,45,64]:hand.grip==='sloper'?[0,4,8]:hand.grip==='pocket'?(finger==='Index'||finger==='Middle'?[20,65,55]:[40,85,70]):[8,16,24];
   const arch=hand.grip==='crimp'?12:hand.grip==='jug'||hand.grip==='pinch'?10:0;
   const apply=(amount,adjust=[0,0,0])=>{
    for(let j=0;j<3;j++){
     this.wants.delete(ch[j]);this.world.clear();
     const a=(Math.max(angles[j],surface?20+12*j:0)*amount*(j===0?1:1+arch/40)-(j===0&&surface?arch:0)+adjust[j])*Math.PI/180;
     const b=ch[j],parent=world(b.parent),carried=parent.q.clone().multiply(this.restLocal.get(b).q);
     const axis=(j<2?rest.get(ch[j+1]).p.clone().sub(rest.get(b).p):rest.get(b).p.clone().sub(rest.get(ch[j-1]).p)).applyQuaternion(rest.get(b).q.clone().invert()).applyQuaternion(carried).normalize();
     const target=over.clone().multiplyScalar(Math.cos(a)).addScaledVector(inside,Math.sin(a)).normalize();
     const turn=new THREE.Quaternion().setFromUnitVectors(axis,target),cap=[48,100,85][j]*Math.PI/180;
     const q=new THREE.Quaternion().slerp(turn,Math.min(1,cap/Math.max(.001,new THREE.Quaternion().angleTo(turn)))).multiply(carried);
     this.wants.set(b,{p:world(b).p.clone(),q:this.motion.limit(b,q,parent.q,false)});this.world.clear();
    }
    const points=ch.map(b=>world(b).p.clone());points.push(tipOf(ch));
    let gap=Infinity;
    for(let i=0;i<points.length;i++){
     gap=Math.min(gap,clear(points[i]));
     if(i)gap=Math.min(gap,clear(points[i].clone().lerp(points[i-1],.5)));
    }
    return {gap,tip:clear(points[3])};
   };
   let best=1,score=Infinity;
   const colliding=surface || (hand.clearance && apply(1).gap<.9);
   if(colliding)for(let i=0;i<=32;i++){
    const amount=-.15+1.4*i/32,{gap,tip}=apply(amount);
    const cost=Math.max(0,.9-gap)**2*1000+Math.abs(tip-1)+1.25*Math.abs(amount-1);
    if(cost<score){score=cost;best=amount;}
   }
   let result=apply(best);
   if(colliding && result.gap<.8){
    let offsets=[0,0,0],score=Math.max(0,.9-result.gap)**2*1000+Math.abs(result.tip-1);
    for(const step of [30,15,8,4,2])for(let j=0;j<3;j++){
     let chosen=offsets;
     for(const sign of [-1,1]){
      const trial=offsets.slice();trial[j]=Math.max(-60,Math.min(60,trial[j]+sign*step));
      const r=apply(best,trial),cost=Math.max(0,.9-r.gap)**2*1000+Math.abs(r.tip-1)+trial.reduce((sum,x)=>sum+x*x,0)*.0003;
      if(cost<score){score=cost;chosen=trial;}
     }
     offsets=chosen;
    }
    apply(best,offsets);
   }
   settle(ch);
  }
  // Thumb motion is relative to its own bind frame, not a wall-facing finger axis.
  const thumb=chain('Thumb');if(thumb.some(b=>!b))return;
  const applyThumb=amount=>{
   for(let j=0;j<3;j++){
    const b=thumb[j];this.wants.delete(b);this.world.clear();
    const parent=world(b.parent),r=this.restLocal.get(b),carried=parent.q.clone().multiply(r.q);
    const axis=over.clone().cross(inside).normalize().applyQuaternion(carried.clone().invert());
    const q=carried.multiply(new THREE.Quaternion().setFromAxisAngle(axis,amount*(j===0?12:16)*Math.PI/180));
    this.wants.set(b,{p:world(b).p.clone(),q:this.motion.limit(b,q,parent.q,false)});this.world.clear();
   }
   const points=thumb.map(b=>world(b).p.clone());points.push(tipOf(thumb));
   return Math.min(...points.map(clear),...points.slice(1).map((p,i)=>clear(p.clone().lerp(points[i],.5))));
  };
  const natural=hand.grip==='free'?.45:1;
  let best=natural,bestClear=-Infinity;
  for(let i=0;i<=12;i++){
   const amount=natural+(-1.8-natural)*i/12,gap=applyThumb(amount);
   if(gap>bestClear){bestClear=gap;best=amount;}
   if(gap>=.9)break;
  }
  applyThumb(best);
  settle(thumb);
 }

	pose(pose,delta=1/60,walking=false) {
  if(delta<=0 && this.motion.previous.size && this.wasWalking===walking)return;
  this.motion.step=delta;
		if(this.wasWalking!==walking){this.motion.reset();this.chainTargets.clear();this.garments.reset();this.bodyOffset.set(0,0,0);this.wasWalking=walking;}
		this.world.clear(); this.wants.clear();
		const pelvis = three(pose.roots[0].add(pose.roots[1]).mul(0.5));
		const chest = three(pose.roots[2].add(pose.roots[3]).mul(0.5));
  if(this.rawPelvis&&this.rawPelvis.distanceTo(pelvis)>1)this.bodyOffset.set(0,0,0);
  this.rawPelvis=pelvis.clone();
  const wallOut=unit(pose.back),lostClearance=this.bodyOffset.dot(wallOut);
  if(lostClearance<0)this.bodyOffset.addScaledVector(wallOut,-lostClearance);
  if(!walking){pelvis.add(this.bodyOffset);chest.add(this.bodyOffset);}
		const up = chest.clone().sub(pelvis).normalize();
		const side = three(pose.roots[3].sub(pose.roots[2])).normalize();
		const back = unit(pose.back);
		// the component transform: rest torso frame to this frame's torso
		const from = frame(this.refSide, this.refUp);
		const to = frame(side, up);
		const turn = to.clone().multiply(from.clone().invert());
		const hips = this.bone['Hips'], restHips = this.restWorld.get(hips);
		if(this.lastPelvis&&this.lastPelvis.distanceTo(pelvis)>1){this.motion.reset();this.chainTargets.clear();}
		this.lastPelvis=pelvis.clone();
		this.wants.set(hips, { p: pelvis, q: this.motion.limit(hips,turn.clone().multiply(restHips.q),this.base.q) });
		const mix=this.motion.profile.native_mix,front=this.motion.front;
		const lift=[2,3].map(l=>Math.max(0,Math.min(1,three(pose.limbs[l].end).sub(three(pose.roots[l])).dot(up)/.5)));
		const extension=(pose.back.z < -.05 ? 16 : 8)*Math.PI/180*Math.min(1,(lift[0]+lift[1])*.55),lateral=12*Math.PI/180*(lift[0]-lift[1]);
		const chestUp=this.refUp.clone().addScaledVector(front,-Math.tan(extension)).addScaledVector(this.refSide,Math.tan(lateral)).normalize();
		const chestTurn=new THREE.Quaternion().setFromUnitVectors(this.refUp,chestUp);
		const put=(name,desired)=>{const b=this.bone[name];if(!b)return;const parent=this.worldOf(b.parent),p=this.worldOf(b).p.clone();this.wants.set(b,{p,q:this.motion.limit(b,desired,parent.q)});this.world.clear();};
		for(const [name,weight] of [['Spine',mix.waist],['Spine1',mix.spine],['Spine2',1]])put(name,turn.clone().multiply(new THREE.Quaternion().slerp(chestTurn,weight)).multiply(this.restWorld.get(this.bone[name]).q));
		const look=three(pose.look).applyQuaternion(turn.clone().invert()).normalize();
		const headTurn=new THREE.Quaternion().setFromUnitVectors(front,look);
		put('Neck',turn.clone().multiply(chestTurn.clone().slerp(headTurn,mix.neck)).multiply(this.restWorld.get(this.bone.Neck).q));
		put('Head',turn.clone().multiply(headTurn).multiply(this.restWorld.get(this.bone.Head).q));

		for (let limb = 0; limb < 4; limb++) {
			const part = pose.limbs[limb], leg = limb < 2;
			const top = this.bone[Upper[limb]], midB = this.bone[Lower[limb]], bot = this.bone[End[limb]], tip = this.bone[Tip[limb]];

			if(!leg){
				const clavicle=this.bone[limb===2?'RightShoulder':'LeftShoulder'];
				const direction=three(part.end).sub(this.worldOf(top).p).normalize();
				const span=this.restWorld.get(top).p.distanceTo(this.restWorld.get(midB).p)+this.restWorld.get(midB).p.distanceTo(this.restWorld.get(bot).p);
    const stretch=Math.max(0,Math.min(1,(three(part.end).distanceTo(this.worldOf(top).p)/span-.8)/.2));
    const angle=Math.min((4+4*stretch)*Math.PI/180,Math.asin(Math.max(0,Math.min(1,direction.dot(up))))*mix.scapula*(.2+.2*stretch))-4*Math.PI/180*(1-stretch);
				const axis=this.restWorld.get(top).p.clone().sub(this.restWorld.get(clavicle).p).normalize();
				const wanted=axis.clone().multiplyScalar(Math.cos(angle)).addScaledVector(this.refUp,Math.sin(angle)).normalize();
				const carry=this.worldOf(clavicle.parent).q.clone().multiply(this.restWorld.get(clavicle.parent).q.clone().invert());
				put(clavicle.name,carry.multiply(new THREE.Quaternion().setFromUnitVectors(axis,wanted)).multiply(this.restWorld.get(clavicle).q));
			}
			const start = this.worldOf(top).p.clone();
			const one = this.restWorld.get(top).p.distanceTo(this.restWorld.get(midB).p);
			const two = this.restWorld.get(midB).p.distanceTo(this.restWorld.get(bot).p);
			// Fit the CC4 shoe to the hold at its toe, using its actual ankle pivot.
			let end = leg ? three(part.foot.toe).addScaledVector(back,.19).addScaledVector(three(part.foot.up),.047) : three(part.end);
   if(walking && !leg && part.hand.grip==='free')end=start.clone().add(three(part.end.sub(pose.roots[limb])).clampLength(0,(one+two)*.97));
   if(!leg && part.hand.contact) {
    const rest=this.restWorld.get(bot),axis=this.restWorld.get(tip).p.clone().sub(rest.p);
    const q=frame(three(part.hand.over),three(part.hand.out).negate()).multiply(frame(axis,this.refUp.clone().negate()).invert()).multiply(rest.q);
    end=three(part.hand.contact).sub(axis.applyQuaternion(rest.q.clone().invert()).applyQuaternion(q));
   }
			let shoeRotation=null;
   if(leg){
    const soleBone=this.bone[(limb===0?'Right':'Left')+'SoleContact'];
    const rest=this.restWorld.get(bot);
    const soleOffset=soleBone?this.restWorld.get(soleBone).p.clone().sub(rest.p):this.motion.front.clone().multiplyScalar(.19).addScaledVector(this.refUp,-.047);
    const wanted=three(part.foot.toe.sub(part.foot.heel)).normalize();
    shoeRotation=frame(wanted,three(part.foot.up)).multiply(frame(this.motion.front,this.refUp).invert()).multiply(rest.q);
    end=three(part.foot.toe).sub(soleOffset.applyQuaternion(rest.q.clone().invert()).applyQuaternion(shoeRotation));
   }
   const height = Math.min(Math.max((end.clone().sub(start).dot(up) * 100 + 20) / 40, 0), 1);
			const hint = leg ? start.clone().add(knee(side, up, back, sideOf(limb), height).multiplyScalar(1))
				: start.clone().add(three(part.bend.sub(pose.roots[limb])));
			let ik = solve(start, end, hint, one, two);
			const out = unit(leg ? pose.back : part.hand.out);
			// Search the elbow/knee's swivel while measuring the constrained FK,
			// rather than choosing an unconstrained bend and stretching to it.
			const axis=ik.end.clone().sub(start).normalize(),center=start.clone().addScaledVector(axis,ik.bend.clone().sub(start).dot(axis));
			const pole=ik.bend.clone().sub(center),around=axis.clone().cross(pole);
			const wallPoint=leg?three(part.foot.toe).addScaledVector(back,-.03):three(part.hand.palm).addScaledVector(out,-.04);
			const trySwivel=angle=>{
				for(const b of [top,midB,bot])this.wants.delete(b);this.world.clear();
				const bend=center.clone().addScaledVector(pole,Math.cos(angle)).addScaledVector(around,Math.sin(angle));
    const upperRest=this.restWorld.get(top).q,lowerRest=this.restWorld.get(midB).q;
    const restAxis=this.restWorld.get(midB).p.clone().sub(this.restWorld.get(top).p).normalize();
        const parent=this.worldOf(top.parent).q,carry=parent.clone().multiply(this.restWorld.get(top.parent).q.clone().invert());
    const d1=bend.clone().sub(start).normalize(),d2=ik.end.clone().sub(bend).normalize();
    let qu=new THREE.Quaternion().setFromUnitVectors(restAxis.clone().applyQuaternion(carry),d1).multiply(carry).multiply(upperRest);
    const hinge=restAxis.clone().cross(this.motion.front.clone().multiplyScalar(leg?-1:1)).normalize().applyQuaternion(upperRest.clone().invert());
    const need=d1.clone().cross(d2),sine=need.length();
    if(sine>.02){
     const here=hinge.applyQuaternion(qu);here.addScaledVector(d1,-here.dot(d1)).normalize();
     const normal=need.normalize(),roll=Math.atan2(d1.dot(here.clone().cross(normal)),here.dot(normal))*Math.min(1,(sine-.02)/.3);
     qu.premultiply(new THREE.Quaternion().setFromAxisAngle(d1,roll));
    }
    qu=this.motion.limit(top,qu,parent,false);this.wants.set(top,{p:start.clone(),q:qu});this.world.clear();
    const middle=this.worldOf(midB).p.clone(),carriedLower=qu.clone().multiply(upperRest.clone().invert()).multiply(lowerRest);
    const lowerDirection=this.restLocal.get(bot).p.clone().normalize().applyQuaternion(carriedLower);
    const ql=new THREE.Quaternion().setFromUnitVectors(lowerDirection,end.clone().sub(middle).normalize()).multiply(carriedLower);
    this.wants.set(midB,{p:middle,q:this.motion.limit(midB,ql,qu,false)});this.world.clear();
    if(leg){const parent=this.worldOf(bot.parent);this.wants.set(bot,{p:this.worldOf(bot).p.clone(),q:this.motion.limit(bot,shoeRotation,parent.q,false)});this.world.clear();}
    const sole=leg?this.bone[(limb===0?'Right':'Left')+'SoleContact']:null;
    const actual=this.worldOf(sole||bot).p,targetPoint=sole?three(part.foot.toe):end,depth=middle.clone().sub(wallPoint).dot(out);
				const kneePenalty=leg&&!walking?Math.max(0,.025-middle.clone().sub(start).dot(side)*sideOf(limb))**2*60000:0;
				const cost=kneePenalty+actual.distanceToSquared(targetPoint)*(leg?100000:300)+(walking?0:Math.max(0,(leg?.10:.07)-depth)**2*10000)+.05*angle**2;
				return {bend,cost};
			};
			let chosen=this.swivels[limb],best=trySwivel(chosen);
			if(!this.motion.previous.size){
    for(let i=0;i<36;i++){const angle=-Math.PI+2*Math.PI*i/36,candidate=trySwivel(angle);if(candidate.cost<best.cost){chosen=angle;best=candidate;}}
   }else for(const offset of [-.1,-.05,.05,.1]){const angle=this.swivels[limb]+offset,candidate=trySwivel(angle);if(candidate.cost+.0001<best.cost){chosen=angle;best=candidate;}}
			const diff=Math.atan2(Math.sin(chosen-this.swivels[limb]),Math.cos(chosen-this.swivels[limb]));
			if(this.motion.previous.size && !leg && !part.hand.held)chosen=this.swivels[limb]+Math.max(-6*delta,Math.min(6*delta,diff));
			this.swivels[limb]=chosen;ik.bend=trySwivel(chosen).bend;

			if (leg) {
    const parent=this.worldOf(bot.parent);
    this.wants.set(bot,{p:this.worldOf(bot).p.clone(),q:this.motion.limit(bot,shoeRotation,parent.q,false)});this.world.clear();
			} else {
				this.aim(bot, tip, ik.end, three(part.hand.over), out.clone().negate(), this.refUp.clone().negate(), false);


			}
   // Optimize constrained FK, including ankle orientation, at fixed bone lengths.
   const landmark=leg?this.bone[(limb===0?'Right':'Left')+'SoleContact']:tip;
   const wantedContact=leg?three(part.foot.toe):part.hand.contact?three(part.hand.contact):null;
   if(landmark && wantedContact){
    const joints=[top,midB,bot],restEnd=this.restWorld.get(bot);
    const desiredBot=leg?shoeRotation:frame(three(part.hand.over),out.clone().negate()).multiply(frame(this.restWorld.get(tip).p.clone().sub(restEnd.p),this.refUp.clone().negate()).invert()).multiply(restEnd.q);
    let locals=joints.map(b=>this.worldOf(b.parent).q.clone().invert().multiply(this.worldOf(b).q));
    const evaluate=values=>{
     for(let j=0;j<3;j++){
      const b=joints[j],parent=this.worldOf(b.parent);
      const q=this.motion.limit(b,parent.q.clone().multiply(values[j]),parent.q,false);
      this.wants.set(b,{p:this.worldOf(b).p.clone(),q});this.world.clear();
     }
     const middle=this.worldOf(midB).p,clear=middle.clone().sub(wallPoint).dot(out);
     let palmPenalty=0;
     if(!leg && part.hand.surface){
      const sideName=limb===2?'Right':'Left';
      for(const digit of ['Thumb','Index','Middle','Pinky']){
       const p=this.worldOf(this.bone[sideName+'Hand'+digit+'1']).p;
       const point=new Vec3(p.z*100,-p.x*100,p.y*100);
       const clearance=(part.hand.clearance?part.hand.clearance(point):part.hand.surface.clear(point))*.01;
       palmPenalty+=Math.max(0,.014-clearance)**2*80000;
      }
     }
     let continuity=0;
     for(const b of joints){const previous=this.motion.previous.get(b);if(previous){const local=this.worldOf(b.parent).q.clone().invert().multiply(this.worldOf(b).q);continuity+=10*previous.angleTo(local)**2;}}
     const kneePenalty=leg&&!walking?Math.max(0,.025-middle.clone().sub(start).dot(side)*sideOf(limb))**2*60000:0;
     // A gentle facing preference allows edging without locking the leg.
     return kneePenalty+continuity+this.worldOf(landmark).p.distanceToSquared(wantedContact)*(leg?100000:10000)+(walking?0:Math.max(0,.06-clear)**2*10000)+20*this.worldOf(bot).q.angleTo(desiredBot)**2+palmPenalty;
    };
    let cost=evaluate(locals);
    if(joints.every(b=>this.motion.previous.has(b))){const previous=joints.map(b=>this.motion.previous.get(b).clone()),score=evaluate(previous);if(score<=cost){locals=previous;cost=score;}evaluate(locals);}

    if(joints.every(b=>this.chainTargets.has(b))){
     const warm=joints.map(b=>this.chainTargets.get(b).clone()),score=evaluate(warm);
     if(score<cost){locals=warm;cost=score;}evaluate(locals);
    }
    for(const degrees of [30,20,12,8,4,2,1,4,2,1,.5])for(let j=0;j<3;j++)for(const axis of [new THREE.Vector3(1,0,0),new THREE.Vector3(0,1,0),new THREE.Vector3(0,0,1)]){
     let chosen=locals;
     for(const sign of [-1,1]){
      const candidate=locals.map(q=>q.clone());candidate[j].multiply(new THREE.Quaternion().setFromAxisAngle(axis,sign*degrees*Math.PI/180));
      const score=evaluate(candidate);if(score<cost){cost=score;chosen=candidate;}
     }
     locals=chosen;evaluate(locals);
    }

   }
   // Solve the full anatomical target before spending this frame's motion
   // budget. Rate-limiting search candidates can trap a flexed chain.
   const chain=[top,midB,bot];
   const localTargets=chain.map(b=>this.worldOf(b.parent).q.clone().invert().multiply(this.worldOf(b).q));
   chain.forEach((b,j)=>{this.chainTargets.set(b,localTargets[j].clone());const parent=this.worldOf(b.parent);
    const q=this.motion.limit(b,parent.q.clone().multiply(localTargets[j]),parent.q);
    this.wants.set(b,{p:this.worldOf(b).p.clone(),q});this.world.clear();
   });
   if(!leg)this.fitFingers(limb===2?'Right':'Left',part.hand);

		}
  if(!walking && delta>0){
   const correction=new THREE.Vector3();let weight=0;
   for(let l=0;l<4;l++){
    const p=pose.limbs[l],name=l%2===0?'Right':'Left';
    if(l>=2&&!p.hand.held || l<2&&p.foot.held===false)continue;
    const bone=this.bone[name+(l<2?'SoleContact':'HandMiddle1')];
    if(!bone)continue;
    const target=l<2?three(p.foot.toe):three(p.hand.contact),w=2;
    correction.addScaledVector(target.sub(this.worldOf(bone).p),w);weight+=w;
   }
   if(weight){
    correction.divideScalar(weight);
    // Contact fitting may shift weight along the wall, but cannot silently
    // cancel the pelvis clearance selected by the climbing pose.
    const inward=correction.dot(back);
    if(inward<0)correction.addScaledVector(back,-inward);
    if(correction.length()>.005)this.bodyOffset.add(correction.multiplyScalar(Math.min(1,delta*8)).clampLength(0,.06*delta)).clampLength(0,.12);
   }
  }
		// write local transforms: driven bones use their wanted world transform, the rest keep bind locals
		this.world.clear();
  const resolved=new Map(this.order.map(b=>[b,this.worldOf(b)]));
  this.world.clear();
		for (const b of this.order) {
			const parent = b.parent && this.world.has(b.parent) ? this.world.get(b.parent)
				: { p: new THREE.Vector3(), q: new THREE.Quaternion() };
			const want = this.wants.has(b)?resolved.get(b):null;
			if (want) {
				const pq = parent.q.clone().invert();
				b.quaternion.copy(pq.clone().multiply(want.q));
				b.position.copy(want.p.clone().sub(parent.p).applyQuaternion(pq));
				this.world.set(b, want);
			} else {
				const r = this.restLocal.get(b);
				b.position.copy(r.p); b.quaternion.copy(r.q);
				this.world.set(b, { p: r.p.clone().applyQuaternion(parent.q).add(parent.p), q: parent.q.clone().multiply(r.q) });
			}
		}
		const pinned=new Set();
  for(let l=0;l<4;l++)if(pose.limbs[l].hand?.held || (!walking && l<2)){
   const side=l%2===0?'Right':'Left';
   for(const b of this.order)if(l<2 ? [side+'UpLeg',side+'Leg',side+'Foot'].includes(b.name) : b.name.startsWith(side+'Hand')||[side+'Arm',side+'ForeArm'].includes(b.name))pinned.add(b);
  }
  // Climbing targets already ease in the simulation and swivel solver. Do not
  // smooth ancestors after fitting contacts: it moves the fitted digits through resin.
  for(const b of this.wants.keys())pinned.add(b);
  this.motion.smooth(delta,pinned);
		if(garmentNames.every(n=>this.bone[n])){
			this.root.updateMatrixWorld(true);
			const point=n=>this.bone[n].getWorldPosition(new THREE.Vector3());
			const goals=garmentNames.map(point),body=[{a:point('Hips'),b:point('Spine2'),radius:.14}];
			for(const side of ['Left','Right']){body.push({a:point(side+'UpLeg'),b:point(side+'Leg'),radius:.08});body.push({a:point(side+'Arm'),b:point(side+'ForeArm'),radius:.05});}
			this.garments.step(goals,body,delta);
			garmentNames.forEach((name,i)=>{const b=this.bone[name],rotation=b.parent.getWorldQuaternion(new THREE.Quaternion()).invert(),scale=b.parent.getWorldScale(new THREE.Vector3());b.position.add(this.garments.offsets[i].clone().applyQuaternion(rotation).divide(scale));});
		}

	}
}

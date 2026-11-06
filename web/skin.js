// the rigged climber: the player2 mesh on the fitted ready-made skeleton, when `models/skin.glb` is present
// (local-only like hands.glb: personal likeness, never committed or deployed; the tube climber stays without it).
// bones are aimed joint-to-joint from the same solved pose the mannequin draws: hips from the leg roots, torso from
// the shoulders, limbs through a two-bone solve at the rig's own lengths, with facing frames for knees, soles and palms.

import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { toThree } from './gym.js';
import { HumanMotion } from './human-motion.js';
import { GarmentPhysics,garmentNames } from './garment-physics.js';

const three = (v) => new THREE.Vector3(...toThree(v));
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

// slide the knee around the hip-ankle line until it clears the wall, keeping both segment lengths
function limit(start, end, point, normal, margin, bend) {
	if (bend.clone().sub(point).dot(normal) >= margin) return bend;
	const axis = end.clone().sub(start).normalize();
	const center = start.clone().add(axis.clone().multiplyScalar(bend.clone().sub(start).dot(axis)));
	const offset = bend.clone().sub(center), radius = offset.length();
	const across = normal.clone().sub(axis.clone().multiplyScalar(normal.dot(axis)));
	if (radius < 0.001 || across.lengthSq() < 0.001) return bend;
	across.normalize();
	const needed = (margin - center.clone().sub(point).dot(normal)) / (radius * across.length());
	const c = Math.min(Math.max(needed, -1), 1);
	let tangent = offset.clone().sub(across.clone().multiplyScalar(offset.dot(across))).normalize();
	if (!tangent.lengthSq()) tangent = axis.clone().cross(across).normalize();
	return center.add(offset.clone().normalize().multiplyScalar(0)).add(
		across.multiplyScalar(c).add(tangent.multiplyScalar(Math.sqrt(Math.max(0, 1 - c * c)))).multiplyScalar(radius));
}

export async function loadSkin(scene) {
	const head = await fetch('./models/corey-player.glb', { method: 'HEAD' }).catch(() => null);
	if (!head?.ok) return null;
	const gltf = await new GLTFLoader().loadAsync('./models/corey-player.glb');
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
		this.swivels=[0,0,0,0];
		this.garments=new GarmentPhysics();
	}

	// where a bone currently lands: a wanted world transform if set, else forward kinematics through posed parents
	worldOf(b) {
		if (this.wants.has(b)) return this.wants.get(b);
		if (!this.world.has(b)) {
			const parent = b.parent && this.bone[b.parent.name] ? this.worldOf(b.parent) : this.base;
			const r = this.restLocal.get(b);
			this.world.set(b, { p: r.p.clone().applyQuaternion(parent.q).add(parent.p), q: parent.q.clone().multiply(r.q) });
		}
		return this.world.get(b);
	}

	// drive a bone so its axis points from At toward Direction, keeping Facing in the y-plane
	aim(b, next, at, direction, facing, front) {
		if (direction.lengthSq() < 1e-8) return;
		const current = this.worldOf(next).p.clone().sub(this.worldOf(b).p).normalize();
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
		q=this.motion.limit(b,q,parent.q);
		// A pose may rotate joints; only the pelvis can translate the skeleton.
		const p=this.worldOf(b).p.clone();
		this.wants.set(b, {p,q});this.world.clear();
	}

	pose(pose,delta=1/60,walking=false) {
		if(this.wasWalking!==walking){this.motion.reset();this.garments.reset();this.wasWalking=walking;}
		this.world.clear(); this.wants.clear();
		const pelvis = three(pose.roots[0].add(pose.roots[1]).mul(0.5));
		const chest = three(pose.roots[2].add(pose.roots[3]).mul(0.5));
		const up = chest.clone().sub(pelvis).normalize();
		const side = three(pose.roots[3].sub(pose.roots[2])).normalize();
		const back = three(pose.back);
		// the component transform: rest torso frame to this frame's torso
		const from = frame(this.refSide, this.refUp);
		const to = frame(side, up);
		const turn = to.clone().multiply(from.clone().invert());
		const hips = this.bone['Hips'], restHips = this.restWorld.get(hips);
		if(this.lastPelvis&&this.lastPelvis.distanceTo(pelvis)>1)this.motion.reset();
		this.lastPelvis=pelvis.clone();
		this.wants.set(hips, { p: pelvis, q: turn.clone().multiply(restHips.q) });
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
				const angle=Math.min(25*Math.PI/180,Math.asin(Math.max(0,Math.min(1,direction.dot(up))))*mix.scapula);
				const axis=this.restWorld.get(top).p.clone().sub(this.restWorld.get(clavicle).p).normalize();
				const wanted=axis.clone().multiplyScalar(Math.cos(angle)).addScaledVector(this.refUp,Math.sin(angle)).normalize();
				const carry=this.worldOf(clavicle.parent).q.clone().multiply(this.restWorld.get(clavicle.parent).q.clone().invert());
				put(clavicle.name,carry.multiply(new THREE.Quaternion().setFromUnitVectors(axis,wanted)).multiply(this.restWorld.get(clavicle).q));
			}
			const start = this.worldOf(top).p.clone();
			const one = this.restWorld.get(top).p.distanceTo(this.restWorld.get(midB).p);
			const two = this.restWorld.get(midB).p.distanceTo(this.restWorld.get(bot).p);
			// Fit the CC4 shoe to the hold at its toe, using its actual ankle pivot.
			const end = leg ? three(part.foot.toe).addScaledVector(back,.25).addScaledVector(three(part.foot.up),.06) : three(part.end);
			const height = Math.min(Math.max((end.clone().sub(start).dot(up) * 100 + 20) / 40, 0), 1);
			const hint = leg ? start.clone().add(knee(side, up, back, sideOf(limb), height).multiplyScalar(1))
				: start.clone().add(three(part.bend.sub(pose.roots[limb])));
			let ik = solve(start, end, hint, one, two);
			const out = three(leg ? pose.back : part.hand.out);
			// Search the elbow/knee's swivel while measuring the constrained FK,
			// rather than choosing an unconstrained bend and stretching to it.
			const axis=ik.end.clone().sub(start).normalize(),center=start.clone().addScaledVector(axis,ik.bend.clone().sub(start).dot(axis));
			const pole=ik.bend.clone().sub(center),around=axis.clone().cross(pole);
			const wallPoint=leg?three(part.foot.toe).addScaledVector(back,-.03):three(part.hand.palm).addScaledVector(out,-.04);
			const trySwivel=angle=>{
				for(const b of [top,midB,bot])this.wants.delete(b);this.world.clear();
				const bend=center.clone().addScaledVector(pole,Math.cos(angle)).addScaledVector(around,Math.sin(angle));
				const frontAxis=this.restWorld.get(tip).p.clone().sub(this.restWorld.get(bot).p);
				this.aim(top,midB,start,bend.clone().sub(start),leg?bend.clone().sub(ik.end):null,leg?frontAxis:null);
				const middle=this.worldOf(midB).p.clone();
				this.aim(midB,bot,middle,ik.end.clone().sub(middle),leg?bend.clone().sub(start):null,leg?frontAxis:null);
				const actual=this.worldOf(bot).p,depth=middle.clone().sub(wallPoint).dot(out);
				const cost=actual.distanceToSquared(end)*300+Math.max(0,(leg?.10:.07)-depth)**2*10000+.05*angle**2;
				return {bend,cost};
			};
			let chosen=this.swivels[limb],best=trySwivel(chosen);
			for(let i=0;i<24;i++){const angle=-Math.PI+2*Math.PI*i/24,candidate=trySwivel(angle);if(candidate.cost<best.cost){chosen=angle;best=candidate;}}
			const diff=Math.atan2(Math.sin(chosen-this.swivels[limb]),Math.cos(chosen-this.swivels[limb]));
			if(this.motion.previous.size)chosen=this.swivels[limb]+Math.max(-6*delta,Math.min(6*delta,diff));
			this.swivels[limb]=chosen;ik.bend=trySwivel(chosen).bend;

			if (leg) {
				const toe = three(part.foot.toe);
				ik.bend = limit(start, ik.end, toe.clone().sub(back.clone().multiplyScalar(0.03)), back, 0.1, ik.bend);
				const front = this.restWorld.get(tip).p.clone().sub(this.restWorld.get(bot).p);
				const sole = three(part.foot.up);
				this.aim(top, midB, start, ik.bend.clone().sub(start), ik.bend.clone().sub(ik.end), front);
				this.aim(midB, bot, this.worldOf(midB).p, ik.end.clone().sub(this.worldOf(midB).p), ik.bend.clone().sub(start), front);
				const shoeAxis=this.motion.front.clone().multiplyScalar(.25).addScaledVector(this.refUp,-.03);
				const sourceFrame=frame(shoeAxis,this.refUp),wantedFrame=frame(toe.clone().sub(this.worldOf(bot).p),sole);
				const rotation=wantedFrame.multiply(sourceFrame.invert()).multiply(this.restWorld.get(bot).q),parent=this.worldOf(bot.parent);
				this.wants.set(bot,{p:this.worldOf(bot).p.clone(),q:this.motion.limit(bot,rotation,parent.q)});this.world.clear();
			} else {
				this.aim(top, midB, start, ik.bend.clone().sub(start));
				this.aim(midB, bot, this.worldOf(midB).p, ik.end.clone().sub(this.worldOf(midB).p));
				this.aim(bot, tip, ik.end, three(part.hand.over), out.clone().negate(), this.refUp.clone().negate());
				const side = limb === 2 ? 'Right' : 'Left', grip = part.hand.grip;
				for (let f = 0; f < 4; f++) {
					for (let j = 1; j <= 3; j++) {
						const index = this.bone[`${side}Hand${Fingers[f]}${j}`], next = this.bone[`${side}Hand${Fingers[f]}${j + 1}`];
						if (!index || !next) continue;
						let angle = j * 8;
						if (grip === 'jug') angle = j * 30;
						else if (grip === 'crimp') angle = j === 1 ? 25 : j === 2 ? 95 : 80;
						else if (grip === 'pinch') angle = j * 22;
						else if (grip === 'smear') angle = 0;
						else if (grip === 'pocket') angle = j * (f === 1 || f === 2 ? 18 : 35);
						const rad = angle * Math.PI / 180;
						const at = this.worldOf(index).p;
						const dir = three(part.hand.over).multiplyScalar(Math.cos(rad)).sub(out.clone().multiplyScalar(Math.sin(rad)));
						this.aim(index, next, at, dir);
					}
				}
			}
		}
		// write local transforms: driven bones use their wanted world transform, the rest keep bind locals
		this.world.clear();
		for (const b of this.order) {
			const parent = b.parent && this.world.has(b.parent) ? this.world.get(b.parent)
				: { p: new THREE.Vector3(), q: new THREE.Quaternion() };
			const want = this.wants.get(b);
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
		this.motion.smooth(delta);
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

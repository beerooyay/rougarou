// the climber: a jointed mannequin in blaize's colors (tee, olive shorts, yellow shoes) drawn from a pose.
// a pose comes from the climbing sim (a port of unreal/Source/Rougarou/ClimberActor.cpp's Draw), from walking the gym, or from a fall.
// solve() bends the elbows and knees, blend() eases between two solved poses, and draw() places the meshes.
// poses are in unreal space, centimeters.

import * as THREE from 'three';
import { Vec2, Vec3 } from './sim.js';
import { toThree } from './gym.js';

const Gap = 30;
const Skin = 0xc78c6b, Tee = 0xe0d9c2, Shorts = 0x3d4a2b, Shoe = 0xe6b814, Hair = 0x2b1d15;
const Y = new THREE.Vector3(0, 1, 0);
const Z = new Vec3(0, 0, 1);
const three = (v) => new THREE.Vector3(...toThree(v));
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const mid = (a, b) => a.add(b).mul(0.5);
// each limb's two bones in centimeters: the sim's limb lengths
export const Bones = [46, 46, 27, 27];
// -1 for the right side, which is the sim's -x
const sideOf = (limb) => limb % 2 === 0 ? -1 : 1;

// a hand at Contact on a wall frame, its fingers along Over: the wrist, and the palm frame the fingers shape around
export function handOn(frame, contact, over, side, grip, shake = 0) {
	let thumb = frame.right.mul(-side);
	thumb = thumb.sub(over.mul(thumb.dot(over))).sub(frame.out.mul(thumb.dot(frame.out))).norm();
	if (thumb.nearZero()) thumb = frame.out.cross(over).norm().mul(-side);
	const palm = contact.sub(over.mul(3.5)).add(frame.out.mul(grip === 'crimp' ? 5.5 : 3.8)).add(thumb.mul(shake));
	return { end: palm.sub(over.mul(5.5)).add(frame.out.mul(0.8)), hand: { palm, over, out: frame.out, thumb, grip } };
}

// elbows and knees: a two-bone bend toward each limb's pole, at the real limb length
export function solve(pose) {
	pose.limbs.forEach((limb, index) => {
		if (limb.bend) return;
		const root = pose.roots[index], length = Bones[index];
		const toward = limb.end.sub(root);
		const reach = Math.min(toward.len(), length * 2 - 0.1);
		const direction = toward.norm();
		const pole = limb.pole.sub(direction.mul(limb.pole.dot(direction))).norm();
		limb.bend = root.add(direction.mul(reach / 2)).add(pole.mul(Math.sqrt(Math.max(0, length * length - reach * reach / 4))));
		limb.joint = root.add(direction.mul(reach));
	});
	return pose;
}

// a pose part way from A to B
export function blend(a, b, t) {
	if (a instanceof Vec3) return a.lerp(b, t);
	if (typeof a === 'number') return a + (b - a) * t;
	if (Array.isArray(a)) return a.map((x, i) => blend(x, b[i], t));
	if (a && typeof a === 'object' && b && typeof b === 'object') {
		const out = {};
		for (const k of Object.keys(a)) out[k] = a[k] == null || b[k] == null ? (t < 0.5 ? a[k] : b[k]) : blend(a[k], b[k], t);
		return out;
	}
	return t < 0.5 ? a : b;
}

// standing and walking on the mats. At is the floor under the hips, Yaw the way the body faces; Phase runs the stride,
// Amp is how hard it is walking (0 standing), Crouch drops the hips. Reach can replace an arm with a hand placed on a hold
export function walkPose(w, clock) {
	const f = new Vec3(Math.cos(w.yaw), Math.sin(w.yaw), 0);
	const r = new Vec3(-f.y, f.x, 0);
	const amp = w.amp ?? 0, crouch = w.crouch ?? 0, phase = w.phase ?? 0;
	const breath = Math.sin(clock * 1.7) * 0.6;
	// the hips ride highest mid-stride, over the planted foot
	const pelvis = w.at.add(Z.mul(97 - crouch - 1.6 * amp * (1 - Math.cos(2 * phase)))).sub(f.mul(crouch * 0.3));
	const chest = pelvis.add(Z.mul(52 + breath - crouch * 0.15)).add(f.mul(4 * amp + crouch * 0.45));
	const roots = [pelvis.add(r.mul(11)), pelvis.sub(r.mul(11)), chest.add(r.mul(20)), chest.sub(r.mul(20))];
	const limbs = [0, 1].map(l => {
		const step = phase + (l === 0 ? 0 : Math.PI);
		const swing = Math.sin(step) * 24 * amp, lift = Math.max(0, Math.cos(step)) * 9 * amp;
		const ankle = w.at.add(r.mul(l === 0 ? 12 : -12)).add(f.mul(swing + crouch * 0.2)).add(Z.mul(8 + lift));
		const pitch = Math.max(0, Math.cos(step)) * amp * 0.25;
		const toe = ankle.add(f.mul(15)).sub(Z.mul(7 + 15 * pitch));
		const heel = ankle.sub(f.mul(5)).sub(Z.mul(6.5 - 5 * pitch));
		return { end: ankle, pole: f.add(r.mul((l === 0 ? 1 : -1) * 0.2)), foot: { toe, heel, up: Z } };
	});
	for (const l of [2, 3]) {
		const s = l === 2 ? 1 : -1;
		if (w.reach?.[l]) { limbs.push(w.reach[l]); continue; }
		// each arm swings with the opposite leg
		const swing = -Math.sin(phase + (l === 2 ? 0 : Math.PI)) * 20 * amp;
		const shoulder = roots[l];
		const wrist = shoulder.sub(Z.mul(49 - crouch * 0.4)).add(f.mul(swing + 5 + crouch * 0.3)).add(r.mul(s * 6));
		const over = wrist.sub(shoulder).norm();
		const out = r.mul(s).sub(over.mul(r.mul(s).dot(over))).norm();
		const thumb = f.sub(over.mul(f.dot(over))).sub(out.mul(f.dot(out))).norm();
		limbs.push({ end: wrist, pole: f.mul(-1).add(r.mul(s * 0.3)), hand: { palm: wrist.add(over.mul(5.5)).sub(out.mul(0.8)), over, out, thumb, grip: 'free' } });
	}
	const head = chest.add(Z.mul(24)).add(f.mul(1 + crouch * 0.1));
	const look = w.look ? w.look.sub(head).norm().add(f.mul(0.8)).norm() : f;
	return { roots, up: chest.sub(pelvis).norm(), back: f.mul(-1), head, look, limbs, marker: null };
}

export class Climber {
	constructor(scene) {
		this.group = new THREE.Group();
		scene.add(this.group);
		const rod = new THREE.CylinderGeometry(1, 1, 1, 12, 1);
		// limbs taper toward the hand or foot: the top of the cylinder lands on the far end of a segment
		const taper = new THREE.CylinderGeometry(0.8, 1, 1, 12, 1);
		const ball = new THREE.SphereGeometry(0.5, 18, 14);
		const paint = {};
		const material = (c) => paint[c] ??= new THREE.MeshStandardMaterial({ color: c, roughness: c === Hair ? 0.95 : 0.75 });
		const make = (geometry, color) => {
			const mesh = new THREE.Mesh(geometry, material(color));
			mesh.castShadow = true;
			this.group.add(mesh);
			return mesh;
		};
		this.rod = (c) => make(rod, c);
		this.taper = (c) => make(taper, c);
		this.ball = (c) => make(ball, c);
		this.pelvis = this.ball(Shorts);
		this.belly = this.ball(Tee);
		this.chest = this.ball(Tee);
		this.neck = this.rod(Skin);
		this.head = this.ball(Skin);
		this.hair = this.ball(Hair);
		this.nose = this.ball(Skin);
		this.limbs = [0, 1, 2, 3].map(l => {
			const arm = l >= 2;
			return { upper: this.taper(arm ? Tee : Shorts), lower: this.taper(Skin), root: this.ball(arm ? Tee : Shorts), joint: this.ball(Skin),
				end: this.ball(arm ? Skin : Shoe), wrist: this.ball(Skin), fingers: arm ? Array.from({ length: 10 }, () => this.rod(Skin)) : [] };
		});
		this.marker = new THREE.Mesh(new THREE.SphereGeometry(0.12, 16, 12), new THREE.MeshBasicMaterial({ color: 0xffe640, wireframe: true, transparent: true, opacity: 0.8 }));
		this.group.add(this.marker);
		this.clock = 0;
		this.reset();
	}

	// forget the eased body shape, for a fresh start on the wall
	reset() { this.turn = 0; this.hips = 18; this.chestDepth = 22; this.look = null; }

	segment(mesh, a, b, radius) {
		const A = three(a), B = three(b);
		const along = B.clone().sub(A);
		const length = along.length();
		mesh.position.copy(A).add(B).multiplyScalar(0.5);
		if (length > 1e-6) mesh.quaternion.setFromUnitVectors(Y, along.multiplyScalar(1 / length));
		mesh.scale.set(radius / 100, Math.max(length, 1e-4), radius / 100);
	}

	// a sphere stretched to Size centimeters, its length along Along and its thickness along Out
	orient(mesh, center, along, out, size) {
		const x = three(along).normalize();
		const o = three(out);
		const z = o.sub(x.clone().multiplyScalar(o.dot(x))).normalize();
		const y = z.clone().cross(x);
		mesh.position.copy(three(center));
		mesh.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(x, y, z));
		mesh.scale.set(size.x / 100, size.y / 100, size.z / 100);
	}

	place(mesh, center, size) {
		mesh.position.copy(three(center));
		mesh.quaternion.identity();
		mesh.scale.setScalar(size / 100);
	}

	// the hand's fingers shaped to the grip: Over is the way the fingers point, Out away from the wall, Thumb toward the thumb side
	shapeFingers(bones, palm, over, out, thumb, grip) {
		let first = [0.9, 0.15], second = [0.6, -0.5], spread = 1;
		if (grip === 'jug' || grip === 'pinch') { first = [1, 0.1]; second = [-0.25, -1]; }
		else if (grip === 'crimp') { first = [0.45, 0.9]; second = [0.3, -1]; spread = 0.85; }
		else if (grip === 'sloper') { first = [1, -0.25]; second = [0.85, -0.5]; spread = 1.25; }
		else if (grip === 'smear') { first = [1, -0.05]; second = [1, -0.1]; spread = 1.2; }
		const lengths = [4.4, 4.9, 4.6, 3.7];
		const dir = (w) => over.mul(w[0]).add(out.mul(w[1])).norm();
		for (let f = 0; f < 4; f++) {
			// index nearest the thumb, out to the pinky
			const knuckle = palm.add(over.mul(4.2)).add(thumb.mul((2.7 - f * 1.8) * spread)).sub(out.mul(0.5));
			let one = first, two = second;
			// a pocket takes the two middle fingers; the others curl out of the way
			if (grip === 'pocket') { const used = f === 1 || f === 2; one = used ? [0.6, -0.8] : [0.4, 0.6]; two = used ? [0.5, -0.9] : [-0.8, -0.4]; }
			const middle = knuckle.add(dir(one).mul(lengths[f] * 0.55));
			const tip = middle.add(dir(two).mul(lengths[f] * 0.45));
			this.segment(bones[f * 2], knuckle, middle, 1.05);
			this.segment(bones[f * 2 + 1], middle, tip, 0.95);
		}
		// the thumb: wrapped over the index on a crimp, opposed across the hold on a pinch, alongside otherwise
		const root = palm.add(thumb.mul(3.6)).sub(over.mul(1.5));
		let one = over.mul(0.6).add(thumb.mul(0.7)), two = over.mul(0.8).sub(thumb.mul(0.2));
		if (grip === 'crimp') { one = over.mul(0.7).add(out.mul(0.6)); two = thumb.mul(-0.9).add(over.mul(0.3)); }
		else if (grip === 'pinch') { one = over.mul(0.4).add(thumb.mul(0.5)).sub(out.mul(0.7)); two = over.mul(0.6).sub(thumb.mul(0.3)).sub(out.mul(0.6)); }
		const middle = root.add(one.norm().mul(3.4));
		this.segment(bones[8], root, middle, 1.3);
		this.segment(bones[9], middle, middle.add(two.norm().mul(2.9)), 1.1);
	}

	// the climbing pose, read off the sim
	climbPose(sim, selected, delta) {
		const face = sim.routes[sim.route];
		const at = (p, depth) => face.map(p, depth);
		const frame = face.at(sim.position.y);
		const ease = Math.min(1, delta * 5);
		// the reaching hand's hip turns into the wall and its shoulder leads, the way a twist or a backstep gains reach
		let mover = sim.reaching >= 2 ? sim.reaching : -1;
		for (let l = 2; l < 4 && mover < 0; l++) if (sim.latch[l] < 0 && sim.grips[l] >= 0) mover = l;
		this.turn += ((mover < 0 ? 0 : sideOf(mover)) - this.turn) * ease;
		const on = (l) => (sim.grips[l] >= 0 || sim.smeared[l]) && sim.arrived(l);
		const feet = [0, 1].filter(on);
		const footY = feet.length ? feet.reduce((a, l) => a + sim.tips[l].y, 0) / feet.length : sim.position.y - 1;
		// long legs keep the hips in close to the wall and a high step pushes them out; straight arms let the chest hang back
		const compress = clamp(1 - (sim.position.y - footY) / 0.75, 0, 1);
		const hangs = [2, 3].filter(on).filter(l => sim.grips[l] >= 0).map(l => sim.distance(l, sim.tips[l]) / (sim.body.lengths[l] * 2));
		const straight = hangs.length ? clamp((Math.max(...hangs) - 0.75) / 0.2, 0, 1) : 0;
		const steep = Math.max(0, face.steep(sim.position.y));
		this.hips += (16 + 22 * compress + 10 * steep * straight - this.hips) * ease;
		this.chestDepth += (20 + 10 * straight + 6 * steep - this.chestDepth) * ease;
		const roots = [0, 1, 2, 3].map(l => at(sim.joint(l), l < 2 ? this.hips - 10 * this.turn * sideOf(l) : this.chestDepth - 5 * this.turn * sideOf(l)));
		const pelvis = mid(roots[0], roots[1]), chest = mid(roots[2], roots[3]);
		const up = chest.sub(pelvis).norm();
		const head = chest.add(up.mul(24)).sub(frame.out.mul(3));
		// the eyes go to whatever is being reached for, or up the wall to the next hold
		let focus;
		if (selected >= 0) focus = sim.target;
		else if (mover >= 0) focus = sim.spot(mover);
		else {
			focus = mid(sim.tips[2], sim.tips[3]).add(new Vec2(0, 0.5));
		}
		const wanted = at(focus, 0).sub(head).norm().add(frame.out.mul(-0.7)).norm();
		this.look = this.look ? this.look.lerp(wanted, Math.min(1, delta * 4)).norm() : wanted;

		const limbs = [];
		let marker = null;
		for (let limb = 0; limb < 4; limb++) {
			const arm = limb >= 2;
			const side = sideOf(limb);
			const held = (sim.grips[limb] >= 0 || sim.smeared[limb]) && sim.arrived(limb);
			const tip = sim.tips[limb];
			const panel = face.at(tip.y);
			const root = roots[limb];
			let contact = at(tip, held ? 3 : Gap * 0.5);
			let part;
			if (arm) {
				let over = panel.up, grip = 'free', shake = 0;
				if (sim.grips[limb] >= 0) {
					const hold = sim.holds()[sim.grips[limb]];
					grip = held ? hold.kind : 'free';
					// fingers point over the lip, away from the way the hold is pulled; a pinch is gripped from below its side
					if (hold.kind !== 'pinch') over = panel.right.mul(-hold.pull.x).add(panel.up.mul(-hold.pull.y)).norm();
				} else if (sim.smeared[limb] && held) grip = 'smear';
				else {
					// a pumped hand off the wall dangles and shakes out
					if (limb !== selected && sim.pump[limb] > 0.05) {
						const flick = Math.sin(this.clock * 15 + limb * 2) * clamp(sim.pump[limb] * 2, 0, 1);
						contact = contact.add(panel.right.mul(flick * 5)).add(panel.out.mul(Math.abs(flick) * 3));
					}
					over = contact.sub(root).norm();
				}
				// a hand near its limit trembles
				shake += Math.min(1, Math.max(0, (sim.strain[limb] - 0.85) * 4)) * Math.sin(this.clock * 70 + limb) * 0.6;
				part = handOn(panel, contact, over, side, grip, shake);
			} else {
				// a climbing shoe: toe on the hold, heel out from the wall and a little low, the ankle above the heel
				const ankle = contact.add(panel.out.mul(14)).add(panel.up.mul(7));
				const heel = contact.add(panel.out.mul(19)).sub(panel.up.mul(1.5));
				part = { end: ankle, foot: { toe: contact, heel, up: panel.up } };
			}
			// elbows drop down and out, knees splay out and lead upward. a foot on the side the body is turning toward drops its knee
			const lean = arm ? [0.6, side * 0.5, -0.6] : [0.35, side * 0.3, 1];
			let pole = frame.out.mul(lean[0]).add(frame.right.mul(lean[1])).add(frame.up.mul(lean[2]));
			if (!arm && held) pole = pole.lerp(frame.up.mul(-1).add(frame.right.mul(-side * 0.7)).add(frame.out.mul(0.3)), clamp(this.turn * side, 0, 1) * 0.85);
			limbs.push({ ...part, pole });
			if (limb === selected) marker = contact;
		}
		return solve({ roots, up, back: frame.out, head, look: this.look, limbs, marker });
	}

	draw(pose) {
		const [hipR, hipL, shoulderR, shoulderL] = pose.roots;
		const pelvis = mid(hipR, hipL), chest = mid(shoulderR, shoulderL);
		const up = chest.sub(pelvis).norm();
		const back = pose.back;
		const across = shoulderL.sub(shoulderR);
		const breath = 1 + Math.sin(this.clock * 1.7) * 0.015;
		this.orient(this.pelvis, pelvis.add(up.mul(3)), hipL.sub(hipR), back, new Vec3(32, 21, 20));
		this.orient(this.belly, pelvis.lerp(chest, 0.42), across.add(hipL.sub(hipR)), back, new Vec3(26, 24, 14.5));
		this.orient(this.chest, chest.sub(up.mul(12)), across, back, new Vec3(36, 33 * breath, 19.5 * breath));
		const face = pose.look.sub(up.mul(pose.look.dot(up))).norm();
		this.segment(this.neck, chest.add(up.mul(2)), pose.head.sub(up.mul(8)), 5.5);
		this.orient(this.head, pose.head, up, face.mul(-1), new Vec3(24, 19, 21));
		this.orient(this.hair, pose.head.add(up.mul(2.5)).sub(face.mul(2.6)), up, face.mul(-1), new Vec3(22, 20.5, 19));
		this.place(this.nose, pose.head.add(face.mul(10)).sub(up.mul(1.5)), 3.6);
		this.marker.visible = !!pose.marker;
		if (pose.marker) this.marker.position.copy(three(pose.marker));
		pose.limbs.forEach((limb, index) => {
			const parts = this.limbs[index];
			const arm = index >= 2;
			const root = pose.roots[index];
			if (arm) {
				const h = limb.hand;
				this.orient(parts.end, h.palm, h.over, h.out, new Vec3(9.5, 8.5, 3.2));
				this.place(parts.wrist, limb.end, 6);
				this.shapeFingers(parts.fingers, h.palm, h.over, h.out, h.thumb, h.grip);
			} else {
				const f = limb.foot;
				this.orient(parts.end, f.toe.add(f.heel).mul(0.5).add(f.up.mul(2)), f.toe.sub(f.heel), f.up, new Vec3(25, 9.5, 9));
				this.place(parts.wrist, limb.end, 8.5);
			}
			this.segment(parts.upper, root, limb.bend, arm ? 6 : 9);
			this.segment(parts.lower, limb.bend, limb.joint ?? limb.end, arm ? 4.8 : 6.8);
			this.place(parts.root, root, arm ? 12 : 15);
			this.place(parts.joint, limb.bend, arm ? 9 : 12.5);
		});
	}
}

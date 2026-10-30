// the climber, drawn from the sim every frame: a jointed mannequin in blaize's colors (tee, olive shorts, yellow shoes)
// with hands that shape to each grip and shoes with ankles. a port of unreal/Source/Rougarou/ClimberActor.cpp's Draw.

import * as THREE from 'three';
import { Vec3 } from './sim.js';
import { toThree } from './gym.js';

const Gap = 30;
const Skin = 0xc78c6b, Tee = 0xe0d9c2, Shorts = 0x3d4a2b, Shoe = 0xe6b814;
const Y = new THREE.Vector3(0, 1, 0);
const three = (v) => new THREE.Vector3(...toThree(v));

export class Climber {
	constructor(scene) {
		this.group = new THREE.Group();
		scene.add(this.group);
		const rod = new THREE.CylinderGeometry(1, 1, 1, 12, 1);
		const ball = new THREE.SphereGeometry(0.5, 16, 12);
		const paint = {};
		const material = (c) => paint[c] ??= new THREE.MeshStandardMaterial({ color: c, roughness: 0.75 });
		const make = (geometry, color) => {
			const mesh = new THREE.Mesh(geometry, material(color));
			mesh.castShadow = true;
			this.group.add(mesh);
			return mesh;
		};
		this.rod = (c) => make(rod, c);
		this.ball = (c) => make(ball, c);
		this.torso = this.rod(Tee);
		this.head = this.ball(Skin);
		this.neck = this.rod(Skin);
		this.shoulders = this.rod(Tee);
		this.pelvis = this.rod(Shorts);
		this.limbs = [0, 1, 2, 3].map(l => {
			const arm = l >= 2;
			return { upper: this.rod(arm ? Tee : Shorts), lower: this.rod(Skin), joint: this.ball(Skin), end: this.ball(arm ? Skin : Shoe), wrist: this.ball(Skin),
				fingers: arm ? Array.from({ length: 10 }, () => this.rod(Skin)) : [] };
		});
		this.marker = new THREE.Mesh(new THREE.SphereGeometry(0.12, 16, 12), new THREE.MeshBasicMaterial({ color: 0xffe640, wireframe: true, transparent: true, opacity: 0.8 }));
		this.group.add(this.marker);
		this.clock = 0;
	}

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

	draw(sim, selected, delta) {
		this.clock += delta;
		const face = sim.routes[sim.route];
		const at = (p, depth) => face.map(p, depth);
		const frame = face.at(sim.position.y);
		const roots = [0, 1, 2, 3].map(l => at(sim.joint(l), Gap - 8));
		const pelvis = roots[0].add(roots[1]).mul(0.5);
		const chest = roots[2].add(roots[3]).mul(0.5);
		const up = chest.sub(pelvis).norm();
		this.segment(this.torso, pelvis.add(up.mul(4)), chest.add(up.mul(6)), 17);
		this.orient(this.head, chest.add(up.mul(24)).sub(frame.out.mul(3)), up, frame.out, new Vec3(24, 20, 19));
		this.segment(this.neck, chest.add(up.mul(4)), chest.add(up.mul(14)).sub(frame.out.mul(2)), 6);
		this.segment(this.shoulders, roots[2], roots[3], 8);
		const hips = roots[1].sub(roots[0]);
		this.segment(this.pelvis, roots[0].sub(hips.mul(0.3)), roots[1].add(hips.mul(0.3)), 13);
		this.marker.visible = false;
		for (let limb = 0; limb < 4; limb++) {
			const parts = this.limbs[limb];
			const arm = limb >= 2;
			const side = limb % 2 === 0 ? -1 : 1;
			const held = (sim.grips[limb] >= 0 || sim.smeared[limb]) && sim.arrived(limb);
			const tip = sim.tips[limb];
			const panel = face.at(tip.y);
			const root = roots[limb];
			const contact = at(tip, held ? 3 : Gap * 0.5);
			let end;
			if (arm) {
				let over = panel.up, grip = 'free';
				let thumb = panel.right.mul(-side);
				if (sim.grips[limb] >= 0) {
					const hold = sim.holds()[sim.grips[limb]];
					grip = held ? hold.kind : 'free';
					// fingers point over the lip, away from the way the hold is pulled; a pinch is gripped from below its side
					if (hold.kind !== 'pinch') over = panel.right.mul(-hold.pull.x).add(panel.up.mul(-hold.pull.y)).norm();
				} else if (sim.smeared[limb] && held) grip = 'smear';
				else over = contact.sub(root).norm();
				thumb = thumb.sub(over.mul(thumb.dot(over))).sub(panel.out.mul(thumb.dot(panel.out))).norm();
				if (thumb.nearZero()) thumb = panel.out.cross(over).norm().mul(-side);
				// a hand near its limit trembles
				const shake = Math.min(1, Math.max(0, (sim.strain[limb] - 0.85) * 4)) * Math.sin(this.clock * 70 + limb) * 0.6;
				const palm = contact.sub(over.mul(3.5)).add(panel.out.mul(grip === 'crimp' ? 5.5 : 3.8)).add(thumb.mul(shake));
				end = palm.sub(over.mul(5.5)).add(panel.out.mul(0.8));
				this.orient(parts.end, palm, over, panel.out, new Vec3(9.5, 8.5, 3.2));
				this.place(parts.wrist, end, 6);
				this.shapeFingers(parts.fingers, palm, over, panel.out, thumb, grip);
			} else {
				// a climbing shoe: toe on the hold, heel out from the wall and a little low, the ankle above the heel
				const ankle = contact.add(panel.out.mul(14)).add(panel.up.mul(7));
				const heel = contact.add(panel.out.mul(19)).sub(panel.up.mul(1.5));
				end = ankle;
				this.orient(parts.end, contact.add(heel).mul(0.5).add(panel.up.mul(2)), contact.sub(heel), panel.up, new Vec3(25, 9.5, 9));
				this.place(parts.wrist, ankle, 8.5);
			}
			// two-bone bend at the sim's real limb length: elbows drop down and out, knees splay out and lead upward
			const length = sim.body.lengths[limb] * 100;
			const toward = end.sub(root);
			const reach = Math.min(toward.len(), length * 2 - 0.1);
			const direction = toward.norm();
			const lean = arm ? [0.6, side * 0.5, -0.6] : [0.35, side * 0.3, 1];
			const hint = frame.out.mul(lean[0]).add(frame.right.mul(lean[1])).add(frame.up.mul(lean[2]));
			const pole = hint.sub(direction.mul(hint.dot(direction))).norm();
			const bend = root.add(direction.mul(reach / 2)).add(pole.mul(Math.sqrt(Math.max(0, length * length - reach * reach / 4))));
			const joint = root.add(direction.mul(reach));
			this.segment(parts.upper, root, bend, arm ? 6 : 9);
			this.segment(parts.lower, bend, joint, arm ? 4.5 : 6.5);
			this.place(parts.joint, bend, arm ? 10 : 13);
			if (limb === selected) {
				this.marker.visible = true;
				this.marker.position.copy(three(contact));
			}
		}
	}
}

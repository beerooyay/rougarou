// real rigged hands, when `models/hands.glb` is present (a local-only test: the source model is personal-use only, so the
// file is never committed or deployed, and the climber keeps its own tube hands without it).
// each hand's wrist bone is placed on the climber's palm frame every frame, and its finger bones curl into the grip.

import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { toThree } from './gym.js';

const three = (v) => new THREE.Vector3(...toThree(v));
const bone = (name) => name.replace(/[[\].:/]/g, '');
// the climber's hand from wrist to the tip of the middle finger, meters
const Length = 0.185;
// degrees of curl at the base, middle and tip joint of the index, middle, ring and pinky fingers, then the thumb
const Grips = {
	free: [[15, 20, 10], [15, 22, 10], [18, 24, 12], [20, 26, 12], [8, 10, 5]],
	jug: [[45, 70, 35], [45, 70, 35], [45, 70, 35], [50, 70, 35], [10, 20, 15]],
	crimp: [[25, 100, -20], [25, 100, -20], [25, 100, -20], [30, 95, -15], [45, 50, 35]],
	sloper: [[10, 15, 8], [10, 15, 8], [10, 15, 8], [12, 15, 8], [5, 5, 5]],
	pinch: [[40, 45, 25], [40, 45, 25], [40, 45, 25], [45, 45, 25], [35, 25, 15]],
	pocket: [[80, 95, 60], [45, 55, 30], [45, 55, 30], [80, 95, 60], [30, 30, 20]],
	smear: [[0, 2, 0], [0, 2, 0], [0, 2, 0], [0, 2, 0], [0, 0, 0]],
};
const Fingers = ['finger_index', 'finger_middle', 'finger_ring', 'finger_pinky'];

export async function loadHands(group) {
	const head = await fetch('./models/hands.glb', { method: 'HEAD' }).catch(() => null);
	if (!head?.ok) return null;
	const gltf = await new GLTFLoader().loadAsync('./models/hands.glb');
	return new RealHands(gltf, group);
}

class RealHands {
	constructor(gltf, group) {
		this.root = gltf.scene;
		// which way a positive curl turns a finger bone about its own x axis
		this.sign = 1;
		group.add(this.root);
		// tinted toward the climber's skin, with the scan's deep wrinkles softened
		this.root.traverse(o => {
			if (!o.isMesh) return;
			o.castShadow = true;
			o.frustumCulled = false;
			o.material.color.set(0xe8b08c);
			if (o.material.normalScale) o.material.normalScale.setScalar(0.45);
		});
		this.root.updateMatrixWorld(true);
		const get = (name) => this.root.getObjectByName(bone(name));
		this.hands = ['L', 'R'].map(side => {
			const wrist = get(`hand.${side}`);
			const at = (name) => get(name).getWorldPosition(new THREE.Vector3());
			const origin = at(`hand.${side}`);
			const over = at(`finger_middle.01.${side}`).sub(origin).normalize();
			const across = at(`finger_index.01.${side}`).sub(at(`finger_pinky.01.${side}`));
			across.sub(over.clone().multiplyScalar(across.dot(over))).normalize();
			// which way the back of the hand faces follows from which hand it is
			const chirality = side === 'L' ? 1 : -1;
			const dorsal = new THREE.Vector3().crossVectors(across, over).multiplyScalar(chirality).normalize();
			const tip = at(`finger_middle.03.${side}`).multiplyScalar(2).sub(at(`finger_middle.02.${side}`));
			const joints = [...Fingers.map(f => [1, 2, 3].map(n => get(`${f}.0${n}.${side}`))), [get(`thumb.01.${side}`), get(`thumb.02.${side}`), get(`thumb.03.${side}`)]];
			return {
				side, wrist, chirality, origin,
				frame: new THREE.Matrix4().makeBasis(across, over, dorsal),
				rest: wrist.matrixWorld.clone(),
				size: tip.distanceTo(origin),
				joints, still: joints.map(j => j.map(b => b.quaternion.clone())),
				curl: joints.map(j => j.map(() => 0)),
			};
		});
	}

	// put the hand of the climber's limb Index on the palm frame, fingers in Grip
	pose(wrist, hand, delta) {
		const over = three(hand.over).normalize();
		const out = three(hand.out);
		out.sub(over.clone().multiplyScalar(out.dot(over))).normalize();
		const thumb = three(hand.thumb);
		thumb.sub(over.clone().multiplyScalar(thumb.dot(over))).sub(out.clone().multiplyScalar(thumb.dot(out))).normalize();
		const chirality = Math.sign(new THREE.Vector3().crossVectors(thumb, over).dot(out)) || 1;
		const model = this.hands.find(h => h.chirality === chirality);
		const desired = new THREE.Matrix4().makeBasis(thumb, over, out);
		const rotate = desired.multiply(model.frame.clone().invert());
		const scale = Length / model.size;
		const place = new THREE.Matrix4().makeTranslation(...three(wrist).toArray())
			.multiply(rotate).multiply(new THREE.Matrix4().makeScale(scale, scale, scale))
			.multiply(new THREE.Matrix4().makeTranslation(-model.origin.x, -model.origin.y, -model.origin.z)).multiply(model.rest);
		const local = model.wrist.parent.matrixWorld.clone().invert().multiply(place);
		local.decompose(model.wrist.position, model.wrist.quaternion, model.wrist.scale);
		// the fingers ease into their grip rather than snapping
		const target = Grips[hand.grip] ?? Grips.free;
		const axis = new THREE.Vector3(1, 0, 0);
		model.joints.forEach((joints, f) => joints.forEach((b, j) => {
			model.curl[f][j] += (target[f][j] - model.curl[f][j]) * Math.min(1, delta * 14);
			b.quaternion.copy(model.still[f][j]).multiply(new THREE.Quaternion().setFromAxisAngle(axis, model.curl[f][j] * Math.PI / 180 * this.sign));
		}));
		return model.side;
	}
}

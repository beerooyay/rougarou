// a soft tube swept along a path: arms and legs that bend smoothly at the elbow and knee, fingers, shoes, sleeves.
// rebuilt every frame from unreal-space points (centimeters); each ring's cross-section is an ellipse with its own width and height.

import * as THREE from 'three';
import { Vec3 } from './sim.js';
import { toThree } from './gym.js';

export class Tube {
	// Open leaves the far end uncapped, for a sleeve or a shorts leg
	constructor(group, material, rings, around, open = false) {
		this.rings = rings;
		this.around = around;
		const count = rings * around + 2;
		const geometry = new THREE.BufferGeometry();
		geometry.setAttribute('position', new THREE.Float32BufferAttribute(new Float32Array(count * 3), 3));
		const index = [];
		for (let r = 0; r + 1 < rings; r++)
			for (let a = 0; a < around; a++) {
				const i = r * around + a, j = r * around + (a + 1) % around;
				index.push(i, j, i + around, j, j + around, i + around);
			}
		// a cap over each end
		const first = rings * around, last = first + 1;
		for (let a = 0; a < around; a++) {
			const b = (a + 1) % around;
			index.push(first, b, a);
			if (!open) index.push(last, (rings - 1) * around + a, (rings - 1) * around + b);
		}
		geometry.setIndex(index);
		this.mesh = new THREE.Mesh(geometry, material);
		if (open) material.side = THREE.DoubleSide;
		this.mesh.castShadow = true;
		this.mesh.frustumCulled = false;
		group.add(this.mesh);
	}

	// Path(t) for t in 0..1 is the centerline; Shape(t) is [width, height]; Side picks which way the width runs
	update(path, shape, side) {
		const positions = this.mesh.geometry.attributes.position;
		const { rings, around } = this;
		for (let r = 0; r < rings; r++) {
			const t = r / (rings - 1);
			const center = path(t);
			const tangent = path(Math.min(1, t + 0.02)).sub(path(Math.max(0, t - 0.02))).norm();
			let a1 = side.sub(tangent.mul(side.dot(tangent)));
			if (a1.len() < 1e-3) a1 = new Vec3(0, 0, 1).cross(tangent);
			a1 = a1.norm();
			const a2 = tangent.cross(a1);
			const [w, h] = shape(t);
			for (let a = 0; a < around; a++) {
				const angle = a / around * Math.PI * 2;
				const p = center.add(a1.mul(Math.cos(angle) * w / 2)).add(a2.mul(Math.sin(angle) * h / 2));
				positions.setXYZ(r * around + a, ...toThree(p));
			}
		}
		positions.setXYZ(rings * around, ...toThree(path(0)));
		positions.setXYZ(rings * around + 1, ...toThree(path(1)));
		positions.needsUpdate = true;
		this.mesh.geometry.computeVertexNormals();
	}
}

// a path through A, the joint B and C, with the corner at B rounded over Round centimeters. also returns where along it B falls
export function bent(a, b, c, round) {
	const one = a.dist(b), two = b.dist(c), total = Math.max(one + two, 1e-3);
	const r = Math.min(round, one * 0.45, two * 0.45);
	const straight = (s) => s < one ? a.lerp(b, s / Math.max(one, 1e-6)) : b.lerp(c, (s - one) / Math.max(two, 1e-6));
	const path = (t) => {
		const s = t * total;
		if (r <= 0.01 || Math.abs(s - one) >= r) return straight(s);
		const p = straight(one - r), q = straight(one + r), u = (s - one + r) / (2 * r);
		return p.mul((1 - u) * (1 - u)).add(b.mul(2 * u * (1 - u))).add(q.mul(u * u));
	};
	path.knee = one / total;
	return path;
}

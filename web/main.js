// rougarou in the browser: the gym, the climber and the controls, for mouse, keyboard and touch.
// a port of unreal/Source/Rougarou/RougarouGame.cpp's controller and camera.

import * as THREE from 'three';
import { Sim, Vec2, Vec3, Names } from './sim.js';
import { problems, buildGym, toThree, Overview, Focus } from './gym.js';
import { Climber } from './climber.js';

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const three = (v) => new THREE.Vector3(...toThree(v));
const unreal = (t) => new Vec3(t.z * 100, -t.x * 100, t.y * 100);
const $ = (id) => document.getElementById(id);

const routes = problems();
const sim = new Sim(routes);
const asked = parseInt(new URLSearchParams(location.search).get('route') ?? '0', 10);
sim.route = clamp(Number.isFinite(asked) ? asked : 0, 0, routes.length - 1);
sim.restart();

// ---------------------------------------------------------------- the scene

const canvas = $('view');
const touchy = matchMedia('(pointer: coarse)').matches;
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(devicePixelRatio, touchy ? 2 : 2));
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 0.92;
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x15151a);
const camera = new THREE.PerspectiveCamera(50, 1, 0.05, 80);

scene.add(new THREE.HemisphereLight(0xfff3e2, 0x2a241e, 1.05));
// one key light from over the camera's shoulder so hold shadows fall short and soft onto the wall; it follows the climber
const key = new THREE.DirectionalLight(0xffffff, 2.1);
key.castShadow = true;
key.shadow.mapSize.set(touchy ? 1024 : 2048, touchy ? 1024 : 2048);
Object.assign(key.shadow.camera, { left: -4, right: 4, top: 4, bottom: -4, near: 0.5, far: 40 });
key.shadow.bias = -0.0004;
key.shadow.normalBias = 0.02;
scene.add(key, key.target);
const keyDirection = new THREE.Vector3(0.27, -0.616, -0.74).normalize();
const fill = new THREE.PointLight(0xffffff, 1.4, 22, 1);
fill.position.copy(three(new Vec3(420, -150, 260)));
scene.add(fill);

const gym = buildGym(routes);
const soup = (sink, color) => {
	const geometry = new THREE.BufferGeometry();
	geometry.setAttribute('position', new THREE.Float32BufferAttribute(sink.positions, 3));
	geometry.setAttribute('normal', new THREE.Float32BufferAttribute(sink.normals, 3));
	const material = new THREE.MeshStandardMaterial({ roughness: 0.82, metalness: 0 });
	material.color.setRGB(...color);
	const mesh = new THREE.Mesh(geometry, material);
	mesh.receiveShadow = true;
	scene.add(mesh);
	return mesh;
};
soup(gym.boards[0], [0.75, 0.55, 0.33]);
soup(gym.boards[1], [0.03, 0.03, 0.035]);
gym.paint.forEach((sink, i) => { soup(sink, gym.palette[i]).castShadow = true; });
soup(gym.shadow, [0.01, 0.01, 0.01]);
// the blockout: one instanced box batch per color
{
	const batches = new Map();
	for (const b of gym.blocks) {
		const k = b.color.join(',') + b.shadows;
		if (!batches.has(k)) batches.set(k, []);
		batches.get(k).push(b);
	}
	const box = new THREE.BoxGeometry(1, 1, 1);
	const m = new THREE.Matrix4(), q = new THREE.Quaternion();
	for (const list of batches.values()) {
		const material = new THREE.MeshStandardMaterial({ roughness: 0.9 });
		material.color.setRGB(...list[0].color);
		if (list[0].color[0] > 0.95) material.emissive.setRGB(1, 1, 1);
		const mesh = new THREE.InstancedMesh(box, material, list.length);
		list.forEach((b, i) => mesh.setMatrixAt(i, m.compose(three(b.center), q, new THREE.Vector3(b.size.y / 100, b.size.z / 100, b.size.x / 100))));
		mesh.receiveShadow = true;
		mesh.castShadow = list[0].shadows;
		scene.add(mesh);
	}
}
for (const l of gym.lights) {
	const light = new THREE.PointLight(new THREE.Color().setRGB(...l.color), l.intensity * 0.35, l.range, 1);
	light.position.copy(three(l.at));
	scene.add(light);
}

const climber = new Climber(scene);

// ---------------------------------------------------------------- controls

let limb = -1;
// where the lifted limb was, for putting it back
let previous = -1, pressed = false, post = new Vec2();
let aim = new Vec2();
const pointer = { x: 0, y: 0, inside: false, touch: false };
let drag = null;
let joystick = null;
const keys = new Set();
let wide = false, elapsed = 0, falls = 0;

function cursor(px, py) {
	// meet the problem's panels: the nearest one the ray hits within its stretch of the wall wins
	const rect = canvas.getBoundingClientRect();
	const ndc = new THREE.Vector2(((px - rect.left) / rect.width) * 2 - 1, -((py - rect.top) / rect.height) * 2 + 1);
	const ray = new THREE.Raycaster();
	ray.setFromCamera(ndc, camera);
	const origin = unreal(ray.ray.origin), direction = unreal(ray.ray.direction).norm();
	const face = routes[sim.route];
	let nearest = Infinity, out = null;
	face.panels.forEach((panel, index) => {
		const facing = direction.dot(panel.out);
		if (Math.abs(facing) < 1e-4) return;
		const distance = panel.origin.sub(origin).dot(panel.out) / facing;
		if (distance <= 0 || distance >= nearest) return;
		const hit = origin.add(direction.mul(distance)).sub(panel.origin);
		const up = hit.dot(panel.up) / 100;
		if (!(index === 0 || up >= -0.05) || !(index + 1 === face.panels.length || up <= panel.length + 0.05)) return;
		nearest = distance;
		out = new Vec2(hit.dot(panel.right) / 100, panel.start + up);
	});
	return out;
}

function screenOf(point) {
	const v = three(routes[sim.route].map(point, 3)).project(camera);
	const rect = canvas.getBoundingClientRect();
	return { x: rect.left + (v.x + 1) / 2 * rect.width, y: rect.top + (1 - v.y) / 2 * rect.height, front: v.z < 1 };
}

// the hand or foot under the pointer, by screen distance so fingertips on a phone can find it
function pick(px, py) {
	let best = -1, closest = touchy ? 60 : 44;
	for (let i = 0; i < 4; i++) {
		const s = screenOf(sim.tips[i]);
		const gap = Math.hypot(s.x - px, s.y - py);
		if (s.front && gap < closest) { closest = gap; best = i; }
	}
	return best;
}

function lift(index) {
	// lifting a limb takes it off the wall: the other three carry the weight from this moment
	limb = index;
	previous = sim.grips[index];
	pressed = sim.smeared[index];
	post = sim.posts[index].copy();
	aim = sim.tips[index].copy();
	sim.drop(index);
}

function place() {
	// commit whatever is under the hand or foot; a bad hold or a bare-wall press is allowed and physics decides if it sticks
	// the hold under the pointer wins if the body can still get there by reaching; otherwise whatever the fingertips touch
	const nearest = (point) => {
		let found = -1, closest = 0.15;
		sim.holds().forEach((h, i) => { const gap = h.point.dist(point); if (gap < closest) { closest = gap; found = i; } });
		return found;
	};
	const wanted = nearest(sim.target), touched = nearest(aim);
	if (wanted >= 0 && sim.grab(limb, wanted)) { limb = -1; return; }
	if (touched < 0 || !sim.grab(limb, touched)) sim.smear(limb, aim);
	limb = -1;
}

function cancel() {
	// put the limb back where it was if the body can still get it there, otherwise it hangs
	if (limb < 0) return;
	if (previous >= 0) sim.grab(limb, previous);
	else if (pressed) sim.smear(limb, post);
	limb = -1;
}

function letGo() { limb = -1; }

function switchRoute(step) {
	sim.route = (sim.route + step + routes.length) % routes.length;
	sim.restart();
	limb = -1;
	$('sent').hidden = true;
	history.replaceState(null, '', `?route=${sim.route}`);
}

function retry() { sim.restart(); limb = -1; $('sent').hidden = true; }

canvas.addEventListener('contextmenu', (e) => e.preventDefault());
canvas.addEventListener('pointerdown', (e) => {
	canvas.setPointerCapture(e.pointerId);
	Object.assign(pointer, { x: e.clientX, y: e.clientY, inside: true, touch: e.pointerType !== 'mouse' });
	$('tip').classList.add('gone');
	if (e.button === 2) { cancel(); return; }
	if (limb >= 0) { place(); return; }
	const found = pick(e.clientX, e.clientY);
	if (found >= 0) {
		lift(found);
		drag = { x: e.clientX, y: e.clientY, moved: false };
		return;
	}
	// a drag on empty wall shifts the hips, like a thumbstick
	joystick = { x: e.clientX, y: e.clientY, dx: 0, dy: 0 };
});
canvas.addEventListener('pointermove', (e) => {
	Object.assign(pointer, { x: e.clientX, y: e.clientY, inside: true, touch: e.pointerType !== 'mouse' });
	if (drag && Math.hypot(e.clientX - drag.x, e.clientY - drag.y) > 12) drag.moved = true;
	if (joystick) { joystick.dx = e.clientX - joystick.x; joystick.dy = e.clientY - joystick.y; }
});
const release = (e) => {
	if (drag) {
		// dragged: drop it where it is. tapped: it stays lifted until the next tap places it
		if (drag.moved && limb >= 0) place();
		drag = null;
	}
	joystick = null;
	if (e.pointerType !== 'mouse') pointer.inside = false;
};
canvas.addEventListener('pointerup', release);
canvas.addEventListener('pointercancel', release);
canvas.addEventListener('pointerleave', (e) => { if (e.pointerType === 'mouse') pointer.inside = false; });

addEventListener('keydown', (e) => {
	const k = e.key.toLowerCase();
	keys.add(k);
	if (k === 'r') retry();
	if (k === ']') switchRoute(1);
	if (k === '[') switchRoute(-1);
	if (k === 'g') wide = !wide;
	if (k === 'escape') cancel();
	if (k === 'x') letGo();
});
addEventListener('keyup', (e) => keys.delete(e.key.toLowerCase()));
addEventListener('blur', () => keys.clear());

$('prev').onclick = () => switchRoute(-1);
$('next').onclick = () => switchRoute(1);
$('retry').onclick = retry;
$('wide').onclick = () => { wide = !wide; };
$('back').onclick = cancel;
$('hang').onclick = letGo;
$('again').onclick = retry;
$('onward').onclick = () => switchRoute(1);
$('tip').textContent = touchy
	? 'drag a hand or foot onto a hold. drag empty wall to shift your hips.'
	: 'click a hand or foot, move the mouse, click to place it. drag empty wall or use wasd to shift your hips.';

// ---------------------------------------------------------------- the loop

function resize() {
	const w = innerWidth, h = innerHeight;
	renderer.setSize(w, h, false);
	camera.aspect = w / h;
	// keep the unreal camera's 55 degree horizontal view in landscape; a tall phone gets a taller view instead
	camera.fov = camera.aspect >= 1 ? THREE.MathUtils.radToDeg(2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(27.5)) / camera.aspect)) : 64;
	camera.updateProjectionMatrix();
}
addEventListener('resize', resize);
resize();

let behind = new Vec3(1, 0, 0);
let view = Overview, look = Focus;
let banked = 0, last = performance.now();
const bars = [$('pumpL'), $('pumpR')];

function frame(now) {
	const real = (now - last) / 1000;
	const delta = Math.min(real, 0.1);
	last = now;
	if (sim.falls !== falls) { falls = sim.falls; limb = -1; }

	// the lifted hand or foot follows the pointer until the limb runs out of length; on a phone it rides a little above the finger
	const point = pointer.inside ? cursor(pointer.x, pointer.y - (pointer.touch && drag ? 50 : 0)) : null;
	if (point && limb >= 0) aim = sim.clamp(limb, point);
	sim.reaching = limb;
	sim.target = point && limb >= 0 ? point : aim;
	// wasd or the thumbstick drag shifts the hips; the camera sits behind the climber, so screen right is sim -x
	let lean = new Vec2((keys.has('a') ? 1 : 0) - (keys.has('d') ? 1 : 0), (keys.has('w') ? 1 : 0) - (keys.has('s') ? 1 : 0));
	if (joystick) lean = lean.add(new Vec2(-joystick.dx / 70, -joystick.dy / 70)).clampLen(1);
	sim.lean = lean.mul(0.6);

	banked = Math.min(banked + delta, 0.25);
	while (banked >= 1 / 60) { sim.step(1 / 60); banked -= 1 / 60; }
	climber.draw(sim, limb, delta);

	// the opening shot holds the whole gym, then swoops in; g flips back to it
	elapsed += real;
	const overview = wide || elapsed < 2.5;
	const face = routes[sim.route];
	const panel = face.at(sim.position.y);
	const hips = face.map(sim.position, 0);
	const out = new Vec3(panel.out.x, panel.out.y, 0);
	// under a roof the panel faces the floor; keep the last good direction
	if (out.len() > 0.3) behind = out.norm();
	const tall = camera.aspect < 1;
	const lift = tall ? 45 : 0;
	const goal = overview ? Overview : hips.add(behind.mul(face.stand * (tall ? 1.2 : 1))).add(panel.right.mul(face.aside)).add(new Vec3(0, 0, 30 + lift));
	const target = overview ? Focus : hips.add(new Vec3(0, 0, 25 + lift));
	const speed = overview || view.dist(goal) > 300 ? 1.6 : 4;
	view = view.lerp(goal, Math.min(1, delta * speed));
	look = look.lerp(target, Math.min(1, delta * speed * 2));
	camera.position.copy(three(view));
	camera.lookAt(three(look));
	const center = three(hips);
	key.target.position.copy(center);
	key.position.copy(center).addScaledVector(keyDirection, -15);

	// the hud
	$('route').textContent = `${face.name}  ${sim.route + 1}/${routes.length}`;
	const sliding = Math.max(sim.strain[2], sim.strain[3]) > 0.9;
	$('status').textContent = limb >= 0 ? `${Names[limb]} off the wall` : sliding ? 'fingers sliding: get your feet under you' : sim.topped > 0 && !sim.won ? 'matched the top, hold it...' : (sim.hint || 'pick a hand or foot');
	$('status').classList.toggle('warn', sliding && limb < 0);
	bars[0].style.width = `${sim.pump[3] * 100}%`;
	bars[1].style.width = `${sim.pump[2] * 100}%`;
	$('back').hidden = limb < 0;
	$('hang').hidden = limb < 0;
	if (sim.won && $('sent').hidden) $('sent').hidden = false;
	renderer.render(scene, camera);
	requestAnimationFrame(frame);
}
$('loading').remove();
requestAnimationFrame(frame);
// a handle for poking at the climb from the console
window.rougarou = { sim, routes, screenOf, place, lift };

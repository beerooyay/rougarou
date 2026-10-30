// rougarou in the browser: walk the gym, pull onto a problem at its start holds, climb it, fall off it, get back up.
// for mouse, keyboard and touch. the climbing controls and camera are a port of unreal/Source/Rougarou/RougarouGame.cpp.

import * as THREE from 'three';
import { Sim, Vec2, Vec3, Names } from './sim.js';
import { problems, buildGym, toThree, Overview, Focus, tape } from './gym.js';
import { Climber, walkPose, blend, handOn, solve } from './climber.js';
import { Ragdoll } from './ragdoll.js';
import { World, floorAt } from './world.js';

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const three = (v) => new THREE.Vector3(...toThree(v));
const unreal = (t) => new Vec3(t.z * 100, -t.x * 100, t.y * 100);
const $ = (id) => document.getElementById(id);
const Z = new Vec3(0, 0, 1);
const ease = (t) => t * t * (3 - 2 * t);
const mid = (a, b) => a.add(b).mul(0.5);
// the shortest way round from angle A toward B, at most Step
const turn = (a, b, step) => { const gap = Math.atan2(Math.sin(b - a), Math.cos(b - a)); return a + clamp(gap, -step, step); };

const routes = problems();
const sim = new Sim(routes);
// ?route=n skips the walk and starts on that problem
const asked = parseInt(new URLSearchParams(location.search).get('route') ?? '', 10);
sim.route = Number.isFinite(asked) ? clamp(asked, 0, routes.length - 1) : 0;
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
const soup = (sink, color, roughness = 0.82) => {
	const geometry = new THREE.BufferGeometry();
	geometry.setAttribute('position', new THREE.Float32BufferAttribute(sink.positions, 3));
	geometry.setAttribute('normal', new THREE.Float32BufferAttribute(sink.normals, 3));
	const material = new THREE.MeshStandardMaterial({ roughness, metalness: 0 });
	material.color.setRGB(...color);
	const mesh = new THREE.Mesh(geometry, material);
	mesh.receiveShadow = true;
	scene.add(mesh);
	return mesh;
};
// pale birch plywood and charcoal panels, holds in satin resin
soup(gym.boards[0], [0.84, 0.68, 0.47]);
soup(gym.boards[1], [0.055, 0.055, 0.06], 0.9);
gym.paint.forEach((sink, i) => { soup(sink, gym.palette[i], 0.5).castShadow = true; });
// the bolt holes, one instanced batch
{
	const hole = new THREE.CylinderGeometry(0.0075, 0.0075, 0.002, 7);
	const mesh = new THREE.InstancedMesh(hole, new THREE.MeshBasicMaterial({ color: 0x14110d }), gym.holes.length);
	const m = new THREE.Matrix4(), q = new THREE.Quaternion(), one = new THREE.Vector3(1, 1, 1), up = new THREE.Vector3(0, 1, 0);
	gym.holes.forEach((h, i) => {
		q.setFromUnitVectors(up, three(h.normal.mul(100)).normalize());
		mesh.setMatrixAt(i, m.compose(three(h.at), q, one));
	});
	scene.add(mesh);
}
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

const world = new World(gym.blocks);
// where to stand under each problem's start holds
const spots = routes.map(r => world.startSpot(r));

// a grade tag over every problem, the way a gym tapes its starts
const tags = routes.map((route, index) => {
	const paper = document.createElement('canvas');
	paper.width = 512;
	paper.height = 200;
	const g = paper.getContext('2d');
	g.fillStyle = 'rgba(16, 16, 20, 0.84)';
	g.beginPath();
	g.roundRect(0, 0, 512, 200, 36);
	g.fill();
	g.fillStyle = `rgb(${tape(index).map(c => Math.round(c * 255)).join(',')})`;
	g.beginPath();
	g.roundRect(20, 20, 26, 160, 13);
	g.fill();
	g.fillStyle = '#f3efe6';
	g.font = '800 104px ui-rounded, system-ui, sans-serif';
	g.fillText(route.grade, 72, 112);
	g.fillStyle = 'rgba(243, 239, 230, 0.72)';
	g.font = '700 46px ui-rounded, system-ui, sans-serif';
	g.fillText(route.name, 74, 168);
	const map = new THREE.CanvasTexture(paper);
	map.colorSpace = THREE.SRGBColorSpace;
	const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map, transparent: true }));
	sprite.scale.set(0.82, 0.32, 1);
	sprite.position.copy(three(spots[index].at.add(Z.mul(255))));
	scene.add(sprite);
	return sprite;
});
// rings that pulse on the start holds of the problem in front of you
const rings = [0, 1].map(() => {
	const ring = new THREE.Mesh(new THREE.TorusGeometry(0.12, 0.009, 8, 40), new THREE.MeshBasicMaterial({ color: 0xffe640, transparent: true, opacity: 0.85 }));
	scene.add(ring);
	return ring;
});

// ---------------------------------------------------------------- state

// walk the gym, mount a problem, climb, fall, rise off the mat, or hop down
let mode = Number.isFinite(asked) ? 'climb' : 'walk';
const walker = { at: new Vec3(450, 0, 28), yaw: Math.PI, heading: new Vec3(-1, 0, 0), phase: 0, speed: 0, amp: 0, near: -1,
	auto: null, autoTime: 0, want: [false, false], lift: [0, 0] };
let camYaw = Math.PI, camPitch = -0.3;
let pose = null, ragdoll = null, stages = null;
let note = '', noted = 0;

// ---------------------------------------------------------------- controls

let limb = -1;
// where the lifted limb was, for putting it back
let previous = -1, pressed = false, post = new Vec2();
let aim = new Vec2();
const pointer = { x: 0, y: 0, inside: false, touch: false };
let drag = null, joystick = null, stick = null, looking = null;
const keys = new Set();
let wide = false, elapsed = 0;

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

// ---------------------------------------------------------------- moving between walking and climbing

const distanceTo = (spot) => Math.hypot(spot.at.x - walker.at.x, spot.at.y - walker.at.y);

// a start hand: walk up under the problem if needed, then reach it up onto its start hold
function reachFor(k) {
	if (walker.near < 0) return;
	walker.want[k] = true;
	const spot = spots[walker.near];
	if (distanceTo(spot) > 18) { walker.auto = spot; walker.autoTime = 0; }
}
function pullOn() { if (mode === 'walk') { reachFor(0); reachFor(1); } }

// a tap or click on the wall in front of you: a start hold gets that hand, anywhere else walks you up to it
function tryGrab(px, py) {
	if (walker.near < 0) return;
	const point = cursor(px, py);
	if (!point) return;
	const holds = routes[walker.near].holds;
	const k = [0, 1].find(k => holds[2 + k].point.dist(point) < 0.3);
	if (k !== undefined) reachFor(k);
	else if (point.y < 2.6) { walker.auto = spots[walker.near]; walker.autoTime = 0; }
}

// the stages of a scripted move, each easing the body from the last pose into the next
function script(list, done) { stages = { from: pose, t: 0, list, done }; }
function runStages(delta) {
	const stage = stages.list[0];
	stages.t += delta;
	const k = Math.min(1, stages.t / stage.length);
	const target = stage.pose();
	pose = blend(stages.from, target, stage.curve(k));
	if (k < 1) return;
	stages.from = target;
	stages.list.shift();
	stages.t = 0;
	if (stages.list.length) return;
	const done = stages.done;
	stages = null;
	done();
}

function mount() {
	if (document.pointerLockElement) document.exitPointerLock();
	sim.route = walker.near;
	sim.restart();
	climber.reset();
	walker.want = [false, false];
	walker.lift = [0, 0];
	mode = 'mount';
	limb = -1;
	// the feet step up onto their start holds as the hands take the weight
	script([{ pose: () => climber.climbPose(sim, -1, 1 / 60), length: 0.75, curve: ease }], () => { mode = 'climb'; });
}

function toWalk() { mode = 'walk'; camYaw = walker.yaw; limb = -1; }

function stand(at) {
	walker.at = at;
	walker.yaw = spots[sim.route].yaw;
	walker.heading = new Vec3(Math.cos(walker.yaw), Math.sin(walker.yaw), 0);
	walker.speed = 0;
	walker.amp = 0;
	walker.auto = null;
	walker.near = sim.route;
}

function startFall() {
	const face = routes[sim.route], panel = face.at(sim.position.y);
	const velocity = panel.right.mul(sim.velocity.x * 100).add(panel.up.mul(sim.velocity.y * 100));
	ragdoll = new Ragdoll(pose, velocity, face);
	note = sim.hint;
	noted = 0;
	mode = 'fall';
	limb = -1;
	drag = null;
	joystick = null;
}

function getUp() {
	const pelvis = mid(pose.roots[0], pose.roots[1]);
	const at = world.clear(new Vec3(pelvis.x, pelvis.y, 0), spots[sim.route].out);
	stand(at);
	ragdoll = null;
	mode = 'rise';
	// roll up to a crouch, then stand
	script([{ pose: () => solve(walkPose({ at, yaw: walker.yaw, crouch: 48 }, climber.clock)), length: 0.75, curve: ease },
		{ pose: walkerPose, length: 0.55, curve: ease }], toWalk);
}

function jumpDown() {
	if (mode !== 'climb') return;
	const face = routes[sim.route], spot = spots[sim.route];
	const hips = face.map(sim.position, 0);
	const at = world.clear(new Vec3(hips.x, hips.y, 0).add(spot.out.mul(40)), spot.out);
	// the drop takes as long as falling from that height does
	const time = clamp(Math.sqrt(2 * Math.max(0, hips.z - 100) / 980), 0.25, 0.9);
	for (let l = 0; l < 4; l++) sim.drop(l);
	$('sent').hidden = true;
	stand(at);
	mode = 'hop';
	script([{ pose: () => solve(walkPose({ at, yaw: walker.yaw, crouch: 52 }, climber.clock)), length: time, curve: (t) => t * t },
		{ pose: walkerPose, length: 0.55, curve: ease }], toWalk);
}

function retry() {
	if (mode !== 'climb' && mode !== 'fall') return;
	sim.restart();
	climber.reset();
	ragdoll = null;
	mode = 'climb';
	limb = -1;
	$('sent').hidden = true;
}

// walk over to the previous or next problem
function visit(step) {
	const from = mode === 'walk' && walker.near < 0 ? sim.route : (walker.near >= 0 && mode === 'walk' ? walker.near : sim.route);
	sim.route = (from + step + routes.length) % routes.length;
	const spot = spots[sim.route];
	stand(world.clear(spot.at.add(spot.out.mul(130)), spot.out));
	stages = null;
	ragdoll = null;
	$('sent').hidden = true;
	toWalk();
}

// ---------------------------------------------------------------- input

canvas.addEventListener('contextmenu', (e) => e.preventDefault());
canvas.addEventListener('pointerdown', (e) => {
	canvas.setPointerCapture(e.pointerId);
	Object.assign(pointer, { x: e.clientX, y: e.clientY, inside: true, touch: e.pointerType !== 'mouse' });
	$('tip').classList.add('gone');
	if (mode === 'walk' && e.pointerType === 'mouse') {
		// a mouse takes over the camera like any third-person game: click to look around freely, esc to get the cursor back.
		// while it is held, a click grabs whatever start hold the crosshair is on
		if (document.pointerLockElement === canvas) { tryGrab(innerWidth / 2, innerHeight / 2); return; }
		canvas.requestPointerLock?.()?.catch?.(() => {});
	}
	if (mode === 'walk') {
		// on a phone the left thumb walks and the right thumb looks; a mouse can also drag to look
		if (e.pointerType !== 'mouse' && e.clientX < innerWidth * 0.45 && !stick) {
			stick = { id: e.pointerId, x: e.clientX, y: e.clientY, dx: 0, dy: 0 };
			Object.assign($('stick').style, { left: `${e.clientX}px`, top: `${e.clientY}px` });
			$('stick').hidden = false;
		} else if (!looking) looking = { id: e.pointerId, x: e.clientX, y: e.clientY, moved: false };
		return;
	}
	if (mode !== 'climb') return;
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
	if (document.pointerLockElement === canvas) {
		camYaw += e.movementX * 0.0025;
		camPitch = clamp(camPitch - e.movementY * 0.002, -0.85, 0.3);
		return;
	}
	Object.assign(pointer, { x: e.clientX, y: e.clientY, inside: true, touch: e.pointerType !== 'mouse' });
	if (stick && e.pointerId === stick.id) {
		stick.dx = e.clientX - stick.x;
		stick.dy = e.clientY - stick.y;
		const knob = new Vec2(stick.dx, stick.dy).clampLen(40);
		$('stick').firstChild.style.transform = `translate(${knob.x}px, ${knob.y}px)`;
	}
	if (looking && e.pointerId === looking.id) {
		const dx = e.clientX - looking.x, dy = e.clientY - looking.y;
		if (Math.abs(dx) + Math.abs(dy) > 3) looking.moved = looking.moved || Math.hypot(e.clientX - looking.x, e.clientY - looking.y) > 3;
		camYaw += dx * 0.006;
		camPitch = clamp(camPitch - dy * 0.004, -0.85, 0.3);
		looking.x = e.clientX;
		looking.y = e.clientY;
	}
	if (drag && Math.hypot(e.clientX - drag.x, e.clientY - drag.y) > 12) drag.moved = true;
	if (joystick) { joystick.dx = e.clientX - joystick.x; joystick.dy = e.clientY - joystick.y; }
});
const release = (e) => {
	if (stick && e.pointerId === stick.id) { stick = null; $('stick').hidden = true; $('stick').firstChild.style.transform = ''; }
	if (looking && e.pointerId === looking.id) {
		// a tap rather than a drag reaches for whatever start hold it lands on
		if (!looking.moved && mode === 'walk') tryGrab(e.clientX, e.clientY);
		looking = null;
	}
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
	if (k === ']') visit(1);
	if (k === '[') visit(-1);
	if (k === 'g') wide = !wide;
	if (k === 'e') pullOn();
	if (k === 'j') jumpDown();
	if (k === 'escape') cancel();
	if (k === 'x') letGo();
});
addEventListener('keyup', (e) => keys.delete(e.key.toLowerCase()));
addEventListener('blur', () => keys.clear());

$('prev').onclick = () => visit(-1);
$('next').onclick = () => visit(1);
$('retry').onclick = retry;
$('pull').onclick = pullOn;
$('wide').onclick = () => { wide = !wide; };
$('back').onclick = cancel;
$('hang').onclick = letGo;
$('drop').onclick = jumpDown;
$('again').onclick = retry;
$('onward').onclick = jumpDown;
$('tip').textContent = touchy
	? 'left thumb walks, right thumb looks. walk up to a problem and tap its two start holds to pull on.'
	: 'wasd to walk, click to look around with the mouse. walk up to a problem and click its two start holds to pull on.';
const climbTip = touchy
	? 'drag a hand or foot onto a hold. drag empty wall to shift your hips. bare wall is for balance, not pulling.'
	: 'click a hand or foot, move the mouse, click to place it. drag empty wall or use wasd to shift your hips.';

// ---------------------------------------------------------------- each mode's frame

function walk(delta) {
	const f = new Vec3(Math.cos(camYaw), Math.sin(camYaw), 0), r = new Vec3(-f.y, f.x, 0);
	const held = (...names) => names.some(n => keys.has(n)) ? 1 : 0;
	let ix = held('d', 'arrowright') - held('a', 'arrowleft'), iy = held('w', 'arrowup') - held('s', 'arrowdown');
	if (stick) { ix += stick.dx / 50; iy -= stick.dy / 50; }
	let wish = f.mul(iy).add(r.mul(ix));
	if (wish.len() > 1) wish = wish.norm();
	if (wish.len() > 0.15) { walker.auto = null; walker.want = [false, false]; }
	if (walker.auto) {
		walker.autoTime += delta;
		const gap = walker.auto.at.sub(walker.at);
		const flat = new Vec3(gap.x, gap.y, 0);
		if (flat.len() < 5 || walker.autoTime > 6) walker.auto = null;
		else wish = flat.norm().mul(clamp(flat.len() / 45, 0.3, 1));
	}
	const pace = keys.has('shift') ? 1.8 : 1;
	walker.speed += (wish.len() * 165 * pace - walker.speed) * Math.min(1, delta * 8);
	if (wish.len() > 0.05) {
		walker.heading = wish.norm();
		walker.yaw = turn(walker.yaw, Math.atan2(wish.y, wish.x), delta * 10);
	}
	const next = world.move(walker.at, walker.heading.mul(walker.speed * delta));
	const moved = Math.hypot(next.x - walker.at.x, next.y - walker.at.y);
	walker.phase += moved / 130 * Math.PI * 2;
	walker.at = new Vec3(next.x, next.y, walker.at.z + (floorAt(next.x, next.y) - walker.at.z) * Math.min(1, delta * 14));
	walker.amp += (clamp(moved / Math.max(delta, 1e-3) / 165, 0, 1.4) - walker.amp) * Math.min(1, delta * 10);
	let near = -1, closest = 170;
	spots.forEach((s, i) => { const d = distanceTo(s); if (d < closest) { closest = d; near = i; } });
	walker.near = near;
	if (near >= 0) sim.route = near;
	// under the start: turn to the wall, then reach each asked-for hand up onto its hold
	const spot = near >= 0 ? spots[near] : null;
	const there = spot && !walker.auto && distanceTo(spot) < 25;
	if (there && walker.want.some(Boolean)) walker.yaw = turn(walker.yaw, spot.yaw, delta * 6);
	const facing = spot && Math.abs(Math.atan2(Math.sin(spot.yaw - walker.yaw), Math.cos(spot.yaw - walker.yaw))) < 0.35;
	for (let k = 0; k < 2; k++) walker.lift[k] = clamp(walker.lift[k] + (there && facing && walker.want[k] ? delta / 0.4 : -delta / 0.3), 0, 1);
	pose = walkerPose();
	if (walker.lift[0] >= 1 && walker.lift[1] >= 1) mount();
}

function walkerPose() {
	const reach = [];
	let look = null;
	if (walker.near >= 0) {
		const face = routes[walker.near];
		look = face.map(new Vec2(0, 1.8));
		for (let k = 0; k < 2; k++) {
			if (walker.lift[k] <= 0) continue;
			const index = 2 + k, side = k === 0 ? -1 : 1, hold = face.holds[index], frame = face.at(hold.point.y);
			const over = hold.kind === 'pinch' ? frame.up : frame.right.mul(-hold.pull.x).add(frame.up.mul(-hold.pull.y)).norm();
			const placed = handOn(frame, face.map(hold.point, 3), over, side, walker.lift[k] > 0.9 ? hold.kind : 'free');
			reach[index] = { ...placed, pole: frame.out.mul(0.6).add(frame.right.mul(side * 0.5)).add(frame.up.mul(-0.6)), weight: walker.lift[k] };
		}
	}
	const p = walkPose({ at: walker.at, yaw: walker.yaw, phase: walker.phase, amp: walker.amp, look }, climber.clock);
	for (const index of [2, 3]) if (reach[index]) p.limbs[index] = blend(p.limbs[index], reach[index], ease(reach[index].weight));
	return solve(p);
}

function climb(delta) {
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
	while (banked >= 1 / 60 && !sim.fallen) { sim.step(1 / 60); banked -= 1 / 60; }
	pose = climber.climbPose(sim, limb, delta);
	if (sim.fallen) startFall();
}

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
const loads = [0, 1, 2, 3].map(i => $(`load${i}`));
const show = (id, on) => { $(id).hidden = !on; };

function frame(now) {
	const real = (now - last) / 1000;
	last = now;
	update(real);
	requestAnimationFrame(frame);
}

function update(real) {
	const delta = Math.min(real, 0.1);
	climber.clock += delta;
	noted += delta;

	if (mode === 'walk') walk(delta);
	else if (mode === 'climb') climb(delta);
	else if (mode === 'fall') {
		ragdoll.step(delta);
		pose = ragdoll.pose();
		if (ragdoll.settled() || ragdoll.time > 3.5) getUp();
	} else if (stages) runStages(delta);
	climber.draw(pose);

	// the opening shot holds the whole gym, then swoops in; g flips back to it
	elapsed += real;
	const overview = wide || elapsed < 2.5;
	const tall = camera.aspect < 1;
	const face = routes[sim.route];
	const pelvis = mid(pose.roots[0], pose.roots[1]);
	let goal, target, speed;
	if (overview) { goal = Overview; target = Focus; speed = 1.6; }
	else if (mode === 'walk') {
		// over the shoulder, pulled in rather than looking through a wall
		const direction = new Vec3(Math.cos(camYaw) * Math.cos(camPitch), Math.sin(camYaw) * Math.cos(camPitch), Math.sin(camPitch));
		target = walker.at.add(Z.mul(tall ? 160 : 145));
		const reach = tall ? 400 : 330;
		goal = target.sub(direction.mul(reach));
		for (let d = 40; d <= reach; d += 15) {
			const p = target.sub(direction.mul(d));
			if (world.solid(p, 12) || p.z < floorAt(p.x, p.y) + 15) { goal = target.sub(direction.mul(Math.max(40, d - 20))); break; }
		}
		speed = view.dist(goal) > 400 ? 2.5 : 7;
	} else {
		const panel = face.at(mode === 'climb' ? sim.position.y : 1.2);
		const out = new Vec3(panel.out.x, panel.out.y, 0);
		// under a roof the panel faces the floor; keep the last good direction
		if (out.len() > 0.3) behind = out.norm();
		const lift = tall ? 45 : 0;
		const back = face.stand * (tall ? 1.2 : 1) * (mode === 'climb' || mode === 'mount' ? 1 : 0.8);
		goal = pelvis.add(behind.mul(back)).add(panel.right.mul(face.aside)).add(new Vec3(0, 0, 30 + lift));
		target = pelvis.add(new Vec3(0, 0, 25 + lift));
		speed = view.dist(goal) > 300 ? 1.6 : 4;
	}
	view = view.lerp(goal, Math.min(1, delta * speed));
	look = look.lerp(target, Math.min(1, delta * speed * 2));
	camera.position.copy(three(view));
	camera.lookAt(three(look));
	const center = three(pelvis);
	key.target.position.copy(center);
	key.position.copy(center).addScaledVector(keyDirection, -15);

	// the tags and the start rings belong to the gym floor, not the climb
	const roaming = mode === 'walk' || mode === 'rise' || mode === 'hop' || mode === 'fall';
	tags.forEach(t => { t.visible = roaming; });
	rings.forEach((ring, k) => {
		ring.visible = mode === 'walk' && walker.near >= 0 && walker.lift[k] < 1;
		if (!ring.visible) return;
		const r = routes[walker.near], hold = r.holds[2 + k].point;
		ring.position.copy(three(r.map(hold, 5)));
		ring.lookAt(three(r.map(hold, 60)));
		ring.scale.setScalar(1 + 0.12 * Math.sin(climber.clock * 5 + k));
	});

	// the hud
	const climbing = mode === 'climb' || mode === 'mount';
	const near = walker.near >= 0 ? routes[walker.near] : null;
	if (climbing || (mode === 'fall')) $('route').textContent = `${face.name}  ${face.grade}  ${sim.route + 1}/${routes.length}`;
	else $('route').textContent = near ? `${near.name}  ${near.grade}` : 'the rock lounge';
	const sliding = Math.max(sim.strain[2], sim.strain[3]) > 0.9;
	let status;
	if (mode === 'fall') status = note;
	else if (climbing) status = limb >= 0 ? `${Names[limb]} off the wall` : sliding ? 'fingers sliding: get your feet under you' : sim.topped > 0 && !sim.won ? 'matched the top, hold it...' : (sim.hint || 'pick a hand or foot');
	else if (note && noted < 4) status = note;
	else if (near) status = walker.want.some(Boolean) ? 'reaching for the start...' : touchy ? 'tap the two start holds to pull on' : 'click the two start holds to pull on, or press e';
	else status = 'walk up to a problem: each tag shows its grade';
	$('status').textContent = status;
	$('status').classList.toggle('warn', (climbing && sliding && limb < 0) || mode === 'fall');
	show('pump', climbing);
	show('weight', climbing);
	bars[0].style.width = `${sim.pump[3] * 100}%`;
	bars[1].style.width = `${sim.pump[2] * 100}%`;
	const total = sim.loads.reduce((a, b) => a + b, 0) || 1;
	loads.forEach((bar, i) => { bar.style.width = `${sim.loads[i] / total * 100}%`; });
	show('back', mode === 'climb' && limb >= 0);
	show('hang', mode === 'climb' && limb >= 0);
	show('drop', mode === 'climb' && limb < 0);
	show('retry', mode === 'climb' || mode === 'fall');
	show('pull', mode === 'walk' && !!near);
	const locked = document.pointerLockElement === canvas;
	$('cross').hidden = !(locked && mode === 'walk');
	$('keys').textContent = climbing
		? 'wasd hips · esc put back · x hang · j jump down · r retry · g gym'
		: locked ? 'wasd walk · shift jog · mouse looks · click a start hold · e pull on · esc frees the mouse'
		: 'wasd walk · shift jog · click to look around · e pull on · [ ] next problem · g gym';
	if (mode === 'mount' && !$('tip').classList.contains('gone') && $('tip').textContent !== climbTip) $('tip').textContent = climbTip;
	if (sim.won && $('sent').hidden && mode === 'climb') $('sent').hidden = false;
	renderer.render(scene, camera);
}
if (mode === 'climb') pose = climber.climbPose(sim, -1, 1 / 60);
else { walker.near = -1; pose = walkerPose(); }
$('loading').remove();
requestAnimationFrame(frame);
// a handle for poking at the game from the console
window.rougarou = { sim, routes, spots, walker, world, screenOf, place, lift, pullOn, jumpDown, visit, retry, get mode() { return mode; }, get pose() { return pose; }, view: () => ({ camYaw, camPitch }),
	// run the game forward without waiting on the browser's frame clock
	tick: (seconds) => { for (let t = 0; t < seconds; t += 1 / 60) update(1 / 60); } };

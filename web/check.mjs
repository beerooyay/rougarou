// node web/check.mjs: every problem must hold a static hang and be sendable by a greedy climber.
// the same check as unreal/Source/Rougarou/ClimbSimTest.cpp, run against the web port.

import { Sim, Vec2 } from './sim.js';
import { problems } from './gym.js';

const routes = problems();
const sim = new Sim(routes);
const settle = (frames) => { for (let f = 0; f < frames; f++) sim.step(1 / 60); sim.lean = new Vec2(); };
const save = () => structuredClone({ ...sim, routes: null, body: null });
const load = (state) => { for (const [k, v] of Object.entries(structuredClone(state))) if (k !== 'routes' && k !== 'body') sim[k] = v; restore(sim); };
// structuredClone drops the vector classes; put them back
const restore = (s) => {
	const fix = (v) => new Vec2(v.x, v.y);
	for (const k of ['position', 'velocity', 'lean', 'target', 'goal']) s[k] = fix(s[k]);
	s.posts = s.posts.map(fix);
	s.tips = s.tips.map(fix);
};
const shifts = [new Vec2(-1, 0), new Vec2(1, 0), new Vec2(0, 1), new Vec2(0, -1)];
let failed = 0;

for (sim.route = 0; sim.route < routes.length; sim.route++) {
	const name = routes[sim.route].name;
	sim.restart();
	settle(180);
	if (sim.grips.some(g => g < 0)) { console.log(`${name}: initial grips must survive a static hang`); failed++; }
	sim.restart();
	const score = () => {
		let total = sim.won ? 1000 : 0;
		for (let limb = 0; limb < 4; limb++) {
			if (sim.grips[limb] < 0) { total -= limb < 2 ? 0.1 : 0.3; continue; }
			const point = sim.holds()[sim.grips[limb]].point;
			total += point.y * (limb >= 2 ? 1 : 0.3);
			if (limb < 2 && point.y > sim.position.y) total -= 1;
		}
		return total;
	};
	let moves = 0;
	for (let move = 0; move < 24 && !sim.won; move++) {
		const saved = save();
		let best = score() + 0.01, choice = null;
		for (let lean = -1; lean < 4; lean++)
			for (let limb = 0; limb < 4; limb++)
				for (let hold = 0; hold < sim.holds().length; hold++) {
					if (sim.grips[limb] === hold) continue;
					load(saved);
					if (lean >= 0) {
						sim.lean = shifts[lean].mul(0.6);
						settle(30);
						if (sim.grips[2] < 0 || sim.grips[3] < 0) continue;
					}
					if (!sim.grab(limb, hold)) continue;
					settle(45);
					if (sim.grips[2] < 0 || sim.grips[3] < 0) continue;
					const result = score();
					if (result > best) { best = result; choice = [lean, limb, hold]; }
				}
		load(saved);
		if (!choice) break;
		if (choice[0] >= 0) { sim.lean = shifts[choice[0]].mul(0.6); settle(30); }
		sim.grab(choice[1], choice[2]);
		settle(45);
		moves++;
		if (process.env.TRACE) console.log(`  ${name}: lean ${choice[0]} limb ${choice[1]} to ${choice[2]} y=${sim.position.y.toFixed(2)} grips=${sim.grips} won=${sim.won}`);
	}
	console.log(`${sim.won ? 'sent' : 'FAILED'}  ${name} in ${moves} moves`);
	if (!sim.won) failed++;
}
process.exit(failed ? 1 : 0);

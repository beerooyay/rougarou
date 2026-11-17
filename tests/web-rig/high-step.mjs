import assert from 'node:assert/strict';
import * as T from 'three';
import {rig} from './fixture.mjs';
import {Climber} from '../../web/climber.js';
import {Sim, Vec2} from '../../web/sim.js';
import {buildGym, problems, routeHolds} from '../../web/gym.js';

const routes = problems();
routes.forEach(routeHolds);
const gym = buildGym(routes);
const {scene, skin} = rig();
const climber = new Climber(scene);
const sim = new Sim(routes);
const obstacles = [...gym.obstacles, ...routes.flatMap(route => route.holds)];
climber.collisionHolds = () => obstacles;
let pose;
function advance(frames) {
 for (let i = 0; i < frames; i++) {
  sim.step(1 / 60);
  pose = climber.climbPose(sim, -1, 1 / 60);
  skin.pose(pose, 1 / 60);
 }
 skin.root.updateMatrixWorld(true);
}

advance(180);
const steps = [
 [new Vec2(0, .6), 2, 7],
 [new Vec2(-.6, 0), 3, 7],
 [null, 1, 6],
 [new Vec2(0, .6), 0, 2],
 [null, 3, 9],
];
const at = name => skin.bone[name].getWorldPosition(new T.Vector3());
for (const [lean, limb, hold] of steps) {
 if (lean) {
  sim.lean = lean;
  advance(30);
  sim.lean = new Vec2();
 }
 assert(sim.grab(limb, hold), sim.hint);
 advance(180);
 assert(!sim.fallen, 'high-step sequence fell');
 if (sim.grips[0] !== 2) continue;
 assert(pose.limbs[0].foot.held, 'high foot never planted');
 const hip = at('RightUpLeg');
 const knee = at('RightLeg');
 const ankle = at('RightFoot');
 const sole = at('RightSoleContact');
 const {toe} = pose.limbs[0].foot;
 const back = new T.Vector3(-pose.back.y, pose.back.z, pose.back.x).normalize();
 const targetAnkle = skin.limbTarget(pose, 0, back, hip).end;
 const miss = sole.distanceTo(new T.Vector3(-toe.y / 100, toe.z / 100, toe.x / 100));
 const ankleMiss = ankle.distanceTo(targetAnkle);
 const kneeAngle = hip.clone().sub(knee).angleTo(ankle.clone().sub(knee)) * 180 / Math.PI;
 console.log({hold, soleMissCm: miss * 100, ankleMissCm: ankleMiss * 100, kneeAngle});
 assert(miss < .02, 'high-step shoe missed its hold');
 assert(ankleMiss < .15, 'toe touched with the ankle far from its shoe target');
 assert(kneeAngle < 140, 'high-step knee stayed straight');
}

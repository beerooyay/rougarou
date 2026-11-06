// Coarse garment-tip cloth: spring masses with inertial lag and capsule contact.
// Mirrors native GarmentPhysics.h; web coordinates are meters.
import * as THREE from 'three';
export const garmentNames=['HemFront','HemBack','HemLeft','HemRight','SleeveLeft','SleeveRight','CuffLeft','CuffRight'];
export class GarmentPhysics {
 constructor(){this.reset();}
 reset(){this.initialized=false;this.offsets=garmentNames.map(()=>new THREE.Vector3());this.velocity=garmentNames.map(()=>new THREE.Vector3());this.last=[];this.lastVelocity=garmentNames.map(()=>new THREE.Vector3());}
 step(goals,body,delta){
  if(this.initialized&&goals.some((p,i)=>p.distanceTo(this.last[i])>1.2))this.reset();
  if(!this.initialized){this.last=goals.map(p=>p.clone());this.initialized=true;return;}
  if(delta<=0)return;
  const dt=Math.max(.001,Math.min(.05,delta));
  for(let i=0;i<8;i++){
   const targetVelocity=goals[i].clone().sub(this.last[i]).divideScalar(dt),accel=targetVelocity.clone().sub(this.lastVelocity[i]).divideScalar(dt).clampLength(0,35);
   const force=accel.multiplyScalar(-.45).add(new THREE.Vector3(0,-1,0)),max=i<4?.01:i<6?.005:.007;
   for(let left=dt;left>1e-8;){
    const h=Math.min(left,1/120);left-=h;
    this.velocity[i].addScaledVector(force.clone().addScaledVector(this.offsets[i],-130).addScaledVector(this.velocity[i],-20),h);
    this.offsets[i].addScaledVector(this.velocity[i],h).clampLength(0,max);
    const at=goals[i].clone().add(this.offsets[i]);
    for(const capsule of body){
     const axis=capsule.b.clone().sub(capsule.a),t=Math.max(0,Math.min(1,at.clone().sub(capsule.a).dot(axis)/Math.max(.0001,axis.lengthSq()))),near=capsule.a.clone().addScaledVector(axis,t),gap=at.clone().sub(near),distance=gap.length(),radius=capsule.radius+.01;
     if(distance<radius&&distance>.0001){const normal=gap.divideScalar(distance);at.copy(near).addScaledVector(normal,radius);const inward=this.velocity[i].dot(normal);if(inward<0)this.velocity[i].addScaledVector(normal,-inward);}
    }
    this.offsets[i].copy(at).sub(goals[i]).clampLength(0,max);
   }
   this.last[i].copy(goals[i]);this.lastVelocity[i].copy(targetVelocity);
  }
 }
}

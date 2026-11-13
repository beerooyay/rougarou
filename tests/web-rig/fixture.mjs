import fs from 'node:fs';
import {fileURLToPath} from 'node:url';
import * as T from 'three';
import {Skin} from '../../web/skin.js';

const root=fileURLToPath(new URL('../../web/',import.meta.url));

// Use the shipped character's actual hierarchy and bind transforms. No invented
// test limb lengths; mesh rendering is checked separately in the browser.
export function rig(){
 const data=fs.readFileSync(root+'models/blaize-player.glb');
 const gltf=JSON.parse(data.subarray(20,20+data.readUInt32LE(12)));
 const joints=new Set(gltf.skins.flatMap(s=>s.joints));
 const nodes=gltf.nodes.map((n,i)=>{
  const bone=joints.has(i)?new T.Bone():new T.Object3D();
  bone.name=n.name||'';
  if(n.translation)bone.position.fromArray(n.translation);
  if(n.rotation)bone.quaternion.fromArray(n.rotation);
  if(n.scale)bone.scale.fromArray(n.scale);
  return bone;
 });
 gltf.nodes.forEach((n,i)=>n.children?.forEach(c=>nodes[i].add(nodes[c])));
 const scene=new T.Scene(),model=new T.Group();
 gltf.scenes[gltf.scene||0].nodes.forEach(i=>model.add(nodes[i]));
 const profile=JSON.parse(fs.readFileSync(root+'human-motion.json'));
 return {scene,skin:new Skin({scene:model},scene,profile)};
}

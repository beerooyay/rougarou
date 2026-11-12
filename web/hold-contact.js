// Contact surfaces sampled from the same triangles used to render each hold.
// World units match the native solver: Unreal-space centimeters.
import { Vec3 } from './sim.js';
const clamp=(x,a,b)=>Math.max(a,Math.min(b,x));
export class HoldContact {
 constructor(positions,origin,up,out) {
  this.origin=origin;this.up=up.norm();this.out=out.norm();this.side=this.out.cross(this.up);this.cells=26;
  const points=[];
  for(let i=0;i<positions.length;i+=3)points.push(this.local(new Vec3(positions[i+2]*100,-positions[i]*100,positions[i+1]*100)));
  this.min=new Vec3(...['x','y','z'].map(k=>Math.min(...points.map(p=>p[k]))));
  this.max=new Vec3(...['x','y','z'].map(k=>Math.max(...points.map(p=>p[k]))));
  this.heights=new Float32Array(this.cells*this.cells);
  // Rasterize the frontmost triangle at every sample, including concave scoops.
  for(let t=0;t<points.length;t+=3){
   const [a,b,c]=points.slice(t,t+3),den=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
   if(Math.abs(den)<1e-9)continue;
   const grid=(v,k)=>clamp(Math.floor((v-this.min[k])/(this.max[k]-this.min[k])*(this.cells-1)),0,this.cells-1);
   for(let j=grid(Math.min(a.y,b.y,c.y),'y');j<=Math.min(25,grid(Math.max(a.y,b.y,c.y),'y')+1);j++)
    for(let i=grid(Math.min(a.x,b.x,c.x),'x');i<=Math.min(25,grid(Math.max(a.x,b.x,c.x),'x')+1);i++){
     const x=this.min.x+(this.max.x-this.min.x)*i/25,y=this.min.y+(this.max.y-this.min.y)*j/25;
     const u=((b.y-c.y)*(x-c.x)+(c.x-b.x)*(y-c.y))/den,v=((c.y-a.y)*(x-c.x)+(a.x-c.x)*(y-c.y))/den;
     if(u>=-1e-5&&v>=-1e-5&&u+v<=1.00001)this.heights[j*26+i]=Math.max(this.heights[j*26+i],u*a.z+v*b.z+(1-u-v)*c.z);
    }
  }
 }
 local(p){const d=p.sub(this.origin);return new Vec3(d.dot(this.up),d.dot(this.side),d.dot(this.out));}
 world(p){return this.origin.add(this.up.mul(p.x)).add(this.side.mul(p.y)).add(this.out.mul(p.z));}
 height(x,y){
  if(x<this.min.x||x>this.max.x||y<this.min.y||y>this.max.y)return 0;
  const u=(x-this.min.x)/(this.max.x-this.min.x)*25,v=(y-this.min.y)/(this.max.y-this.min.y)*25;
  const i=Math.min(24,Math.floor(u)),j=Math.min(24,Math.floor(v)),a=u-i,b=v-j,h=this.heights;
  return (h[j*26+i]*(1-a)+h[j*26+i+1]*a)*(1-b)+(h[(j+1)*26+i]*(1-a)+h[(j+1)*26+i+1]*a)*b;
 }
 clear(p){const l=this.local(p);return l.z-this.height(l.x,l.y);}
 hand(kind,offset=new Vec3()){
  const l=this.local(this.origin.add(offset));l.x+=this.max.x*(kind==='crimp'?.34:.60);l.z=this.height(l.x,l.y);
  let point=this.world(l),lift=0;
  for(const down of [0,3,6,9])for(const across of [-2.4,0,2.4])
   lift=Math.max(lift,1.6-this.clear(point.sub(this.up.mul(down)).add(this.side.mul(across)).add(this.out.mul(2.5))));
  return point.add(this.out.mul(lift+(kind==='crimp'?6.8:5)));
 }
 foot(offset=new Vec3()){
  const l=this.local(this.origin.add(offset)),target=this.max.z*.60;
  // The shoe stands on the upper shoulder rather than the wall/hold center.
  for(let i=0;i<=100;i++){
   const x=this.max.x-(this.max.x-this.min.x)*i/100;
   if(this.height(x,l.y)>=target)return this.world(new Vec3(x,l.y,target));
  }
  return this.world(new Vec3(l.x,l.y,this.height(l.x,l.y)));
 }
}

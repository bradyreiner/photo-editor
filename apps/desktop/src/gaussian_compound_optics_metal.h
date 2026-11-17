#pragma once
#include <string>

// GPU counterpart of the independently tested double-precision optical core.
// No scene renderer or display-space blur is involved in these camera rays.
inline const std::string gaussian_compound_optics_msl=R"METAL(
#include <metal_stdlib>
using namespace metal;
struct CompoundSurface { float4 shape; float4 medium; };
struct CompoundCamera { uint4 counts; float4 gate; float4 rear; float4 iris; };
struct CompoundOpticalRay { float3 origin; float3 direction; float weight; float transmission; };
bool compound_surface_hit(float3 origin,float3 direction,float vertex_z,float radius,
    thread float3& point,thread float3& normal,thread float& distance) {
  if (radius==0) {
    if (abs(direction.z)<1e-15f) return false;
    distance=(vertex_z-origin.z)/direction.z;
    if (distance < -1e-5f) return false;
    distance=max(0.0f,distance); point=origin+distance*direction; normal=float3(0,0,1); return true;
  }
  const float3 relative=origin-float3(0,0,vertex_z);
  const float b=2*(dot(relative,direction)+radius*direction.z);
  const float c=dot(relative,relative)+2*radius*relative.z;
  const float discriminant=fma(b,b,-4*c);
  if (discriminant<0) return false;
  const float q=-0.5f*(b+copysign(sqrt(discriminant),b));
  const float other=q!=0 ? c/q : -b/2;
  const float2 roots=float2(min(q,other),max(q,other));
  for (uint i=0;i<2;++i) {
    float t=roots[i]; if (t < -1e-5f || !isfinite(t)) continue;
    t=max(0.0f,t); const float3 p=origin+t*direction;
    const float3 n=normalize(p-float3(0,0,vertex_z-radius));
    if (n.z*radius < -1e-6f) continue;
    point=p; normal=n; distance=t; return true;
  }
  return false;
}
float compound_fresnel(float cosine,float ni,float nt) {
  if (ni==nt) return 0;
  cosine=clamp(abs(cosine),0.0f,1.0f);
  const float eta=ni/nt,sin2=eta*eta*(1-cosine*cosine);
  if (sin2>=1) return 1;
  const float transmitted=sqrt(1-sin2);
  const float s=(ni*cosine-nt*transmitted)/(ni*cosine+nt*transmitted);
  const float p=(nt*cosine-ni*transmitted)/(nt*cosine+ni*transmitted);
  return (s*s+p*p)*0.5f;
}
bool compound_iris_hit(float2 p,float radius,constant CompoundCamera& camera) {
  const float radial=length(p);
  if (radial>radius*(1+1e-7f)) return false;
  if (camera.counts.z==0 || camera.iris.y==1 || radial==0) return true;
  const float half_sector=M_PI_F/float(camera.counts.z);
  float angle=fmod(atan2(p.y,p.x)-camera.iris.z,2*half_sector);
  if (angle<0) angle+=2*half_sector;
  const float boundary=cos(half_sector)/cos(angle-half_sector);
  return radial<=radius*(boundary+camera.iris.y*(1-boundary))*(1+1e-7f);
}
CompoundOpticalRay compound_camera_ray(float2 film_sample,float2 pupil_sample,
    constant CompoundCamera& camera,constant CompoundSurface* surfaces) {
  CompoundOpticalRay result; result.origin=0; result.direction=0; result.weight=0; result.transmission=0;
  const float3 film=float3((0.5f-film_sample)*camera.gate.xy,camera.gate.z);
  const float distance=camera.gate.w;
  const float factor=distance/(distance+camera.rear.z);
  const float2 p0=film.xy+factor*(-camera.rear.y-film.xy);
  const float2 p1=film.xy+factor*( camera.rear.y-film.xy);
  const float2 low=min(min(p0,p1),float2(-camera.rear.y));
  const float2 high=max(max(p0,p1),float2(camera.rear.y));
  float3 origin=film;
  float3 direction=normalize(float3(mix(low,high,pupil_sample),camera.rear.x)-film);
  const float cosine=direction.z;
  float transmission=1;
  for (int i=int(camera.counts.x)-1;i>=0;--i) {
    const CompoundSurface surface=surfaces[i];
    float3 p,n; float segment;
    if (!compound_surface_hit(origin,direction,surface.shape.x,surface.shape.y,p,n,segment)) return result;
    const float radius=uint(i)==camera.counts.y && camera.iris.x>0 ? camera.iris.x : surface.shape.z;
    if (length(p.xy)>radius*(1+1e-7f) || (uint(i)==camera.counts.y && !compound_iris_hit(p.xy,radius,camera))) return result;
    transmission*=exp(-surface.medium.z*segment);
    origin=p;
    if (surface.shape.w==0) {
      if (dot(direction,n)>0) n=-n;
      const float ci=clamp(-dot(direction,n),0.0f,1.0f),eta=surface.medium.x/surface.medium.y;
      const float discriminant=1-eta*eta*(1-ci*ci);
      if (discriminant<0) return result;
      transmission*=1-compound_fresnel(ci,surface.medium.x,surface.medium.y);
      direction=normalize(eta*direction+(eta*ci-sqrt(discriminant))*n);
    }
  }
  result.origin=origin-float3(0,0,camera.gate.z);
  result.direction=direction;
  const float2 extent=high-low;
  result.weight=extent.x*extent.y*cosine*cosine*cosine*cosine/(distance*distance)*transmission;
  result.transmission=transmission;
  return result;
}
)METAL";

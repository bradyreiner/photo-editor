#pragma once
#include <string>
// Standard real SH basis through l=3; COLMAP +Z forward, +Y down. The 0.3
// pixel covariance floor matches the selected Brush training convention.
inline const std::string gaussian_splat_msl = std::string(R"METAL(
#include <metal_stdlib>
using namespace metal;
#if LOOKSY_SCAN_RT
#include <metal_raytracing>
using namespace metal::raytracing;
#define SCAN_RT_PARAMETERS , primitive_acceleration_structure scan_acceleration, intersection_function_table<> scan_functions
#define SCAN_RT_ARGUMENTS , scan_acceleration, scan_functions
#define SCAN_RADIANCE_ARGUMENTS , scan_acceleration, scan_radiance_functions
#define SCAN_PATH_PARAMETERS , primitive_acceleration_structure scan_acceleration, intersection_function_table<> scan_functions, intersection_function_table<> scan_radiance_functions
#define SCAN_PATH_ARGUMENTS , scan_acceleration, scan_functions, scan_radiance_functions
#else
#define SCAN_RT_PARAMETERS
#define SCAN_RT_ARGUMENTS
#define SCAN_RADIANCE_ARGUMENTS
#define SCAN_PATH_PARAMETERS
#define SCAN_PATH_ARGUMENTS
#endif
struct Point { float4 center_opacity; float4 a; float4 b; float sh[48]; };
struct Motion { float4 velocity_time; float4 normalization; float4 factor0; float4 factor1; float4 factor2; };
struct Appearance { float sh[96]; };
struct Camera { float4x4 view; float4 intrinsics; float4 image; float4 position; float4 temporal; float4 raster; float4 aperture; float4 aperture_world; float4 hybrid; float4 atmosphere; float4 medium; float4 medium_grid; };
struct Vertex { float4 position [[position]]; float2 delta; float3 conic [[flat]]; float4 color [[flat]]; float depth [[flat]]; };
struct Solid { float4 center_kind; float4 axis_x; float4 axis_y; float4 axis_z; float4 color_roughness; float4 material; uint4 geometry; };
struct Light { float4 vector_kind; float4 color_intensity; float4 axis_u; float4 axis_v; float4 shape; };
struct LightIntegration { uint first; uint count; uint total; uint flags; uint point_offset; uint point_count; uint pixel_y; uint reserved; };
struct LightSample { float3 direction; float distance; float weight; };
uint light_hash(uint value) { value ^= value >> 16; value *= 0x7feb352d; value ^= value >> 15; value *= 0x846ca68b; return value ^ (value >> 16); }
float2 light_sample_2d(uint index,uint total,uint seed) {
  uint bits = index; bits = (bits << 16) | (bits >> 16);
  bits = ((bits & 0x55555555) << 1) | ((bits & 0xaaaaaaaa) >> 1);
  bits = ((bits & 0x33333333) << 2) | ((bits & 0xcccccccc) >> 2);
  bits = ((bits & 0x0f0f0f0f) << 4) | ((bits & 0xf0f0f0f0) >> 4);
  bits = ((bits & 0x00ff00ff) << 8) | ((bits & 0xff00ff00) >> 8);
  return float2((float(index)+0.5)/float(total),fract(float(bits)*2.3283064365386963e-10+float(light_hash(seed))*2.3283064365386963e-10));
}
bool finite_light(Light light) { return light.vector_kind.w >= 2 || (light.vector_kind.w == 0 && light.shape.y > 0); }
float3 light_cone_direction(float3 axis,float one_minus_cos,float2 sample) {
  const float cosine = 1-sample.x*one_minus_cos;
  const float sine = sqrt(max(0.0,sample.x*one_minus_cos*(2-sample.x*one_minus_cos)));
  const float3 u = normalize(cross(abs(axis.z) < 0.999 ? float3(0,0,1) : float3(0,1,0),axis));
  const float3 v = cross(axis,u); const float phi = 2*M_PI_F*sample.y;
  return axis*cosine+(u*cos(phi)+v*sin(phi))*sine;
}
LightSample sample_light(Light light,float3 delta,uint index,uint total,uint seed) {
  LightSample result; result.direction = 0; result.distance = 1e30; result.weight = 0;
  const float kind = light.vector_kind.w;
  const float2 sample = light_sample_2d(index,total,seed);
  if (kind == 0) {
    const float3 axis = normalize(delta);
    result.direction = light.shape.y > 0 ? light_cone_direction(axis,2*pow(sin(light.shape.y*0.5),2.0),sample) : axis;
    result.weight = 1; return result;
  }
  if (kind == 2) delta += light.axis_u.xyz*((sample.x-0.5)*light.axis_u.w)+light.axis_v.xyz*((sample.y-0.5)*light.axis_v.w);
  const float distance = length(delta); if (distance < 1e-5 && kind != 1) return result;
  const float3 axis = delta/max(distance,1e-10);
  if (kind == 3) {
    const float radius = light.shape.x;
    if (distance <= radius) return result; // One-sided outward emission.
    const float sin_squared = (radius/distance)*(radius/distance);
    const float one_minus_cos = sin_squared/(1+sqrt(max(0.0,1-sin_squared)));
    result.direction = light_cone_direction(axis,one_minus_cos,sample);
    const float3 perpendicular = cross(delta,result.direction);
    result.distance = dot(delta,result.direction)-sqrt(max(0.0,radius*radius-dot(perpendicular,perpendicular)));
    result.weight = 2*M_PI_F*one_minus_cos; return result;
  }
  result.direction = axis; result.distance = distance;
  result.weight = kind == 2 ? light.axis_u.w*light.axis_v.w*max(0.0,dot(cross(light.axis_u.xyz,light.axis_v.xyz),-axis))/max(distance*distance,1e-6) : 1.0/max(distance*distance,1e-6);
  return result;
}
struct Composition { uint objects; uint lights; float ambient; float reflections; float4 capture; uint4 ray_modes; };
// Primary visibility is independent of illumination. Emitters do not become
// authored solids or alter the existing shadow/BRDF sampling when shown.
float primary_light_hit(Light light,float3 origin,float3 ray,thread float3& normal) {
  if (light.shape.z <= 0) return 1e30;
  if (light.vector_kind.w == 2) {
    normal = cross(light.axis_u.xyz,light.axis_v.xyz);
    const float denominator = dot(ray,normal); if (abs(denominator) < 1e-8) return 1e30;
    const float distance = -dot(origin,normal)/denominator; if (distance <= 1e-5) return 1e30;
    const float3 offset = origin+ray*distance;
    if (abs(dot(offset,light.axis_u.xyz)) > light.axis_u.w*0.5 || abs(dot(offset,light.axis_v.xyz)) > light.axis_v.w*0.5) return 1e30;
    return distance;
  }
  if (light.vector_kind.w == 3) {
    const float b = dot(origin,ray), radius = light.shape.x;
    const float3 perpendicular = cross(origin,ray);
    const float discriminant = radius*radius-dot(perpendicular,perpendicular); if (discriminant < 0) return 1e30;
    const float root = sqrt(discriminant);
    float distance = -b-root; if (distance <= 1e-5) distance = -b+root;
    if (distance <= 1e-5) return 1e30;
    normal = normalize(origin+ray*distance); return distance;
  }
  return 1e30;
}
float3 decode_srgb(float3 encoded);
float3 encode_srgb(float3 linear);
struct MeshTriangle { float4 position; float4 edge1; float4 edge2; float4 normal0; float4 normal1; float4 normal2; float4 uv01; float4 uv2_material; };
struct SurfaceMaterial { float4 color_roughness; float4 emission_metallic; float4 uv_transform; uint4 image; };
struct MeshNode { float4 lower; float4 upper; uint4 links; };
struct ScanParticle { float4 center_opacity; float4 row0; float4 row1; float4 row2; };
struct ScanVisibility { uint nodes; float alpha_limit; float cutoff; float termination; };
struct PrimaryRayCapture { uint4 counts; float4 lens; float4 pupil; float4 optics; };
constant bool spatial_rays [[function_constant(0)]];
constant bool multiple_surface_paths [[function_constant(1)]];
#define SCAN_GATHER_CAPACITY 32
// Exact lexicographic (peak distance, particle ID) batches. Equal-depth
// particles retain a stable order and are never skipped at a batch boundary.
struct ScanGather {
  float cursor, maximum, alpha_limit, cutoff;
  uint cursor_id, started, count;
  float distances[SCAN_GATHER_CAPACITY], alphas[SCAN_GATHER_CAPACITY]; uint ids[SCAN_GATHER_CAPACITY];
};
bool scan_after_cursor(float t,uint id,float cursor,uint cursor_id,uint started) {
  return started == 0 || t > cursor || (t == cursor && id > cursor_id);
}
bool scan_before(float a,uint a_id,float b,uint b_id) {
  return a < b || (a == b && a_id < b_id);
}
float4 scan_peak(ScanParticle p,float3 origin,float3 ray,float maximum) {
  const float3 relative = origin-p.center_opacity.xyz;
  const float3 o(dot(p.row0.xyz,relative),dot(p.row1.xyz,relative),dot(p.row2.xyz,relative));
  const float3 d(dot(p.row0.xyz,ray),dot(p.row1.xyz,ray),dot(p.row2.xyz,ray));
  const float denominator = dot(d,d); if (denominator <= 0) return float4(0,1e30,1e30,1e30);
  const float t = clamp(-dot(o,d)/denominator,0.0,maximum);
  const float3 closest = fma(d,float3(t),o);
  return float4(t,closest);
}
#if LOOKSY_SCAN_RT
struct ScanPayload { float transmittance; float alpha_limit; float cutoff; float termination; };
struct ScanIntersection { bool accept [[accept_intersection]]; float distance [[distance]]; };
[[intersection(bounding_box)]]
ScanIntersection scan_particle_intersection(float3 origin [[origin]],float3 ray [[direction]],
    float minimum [[min_distance]],float maximum [[max_distance]],uint id [[primitive_id]],
    ray_data ScanPayload& payload [[payload]],device const ScanParticle* particles [[buffer(0)]]) {
  const ScanParticle p = particles[id]; const float3 relative = origin-p.center_opacity.xyz;
  const float3 o(dot(p.row0.xyz,relative),dot(p.row1.xyz,relative),dot(p.row2.xyz,relative));
  const float3 d(dot(p.row0.xyz,ray),dot(p.row1.xyz,ray),dot(p.row2.xyz,ray));
  const float denominator = dot(d,d); if (denominator <= 0) return {false,0};
  const float t = clamp(-dot(o,d)/denominator,minimum,maximum);
  const float3 closest = fma(d,float3(t),o);
  const float alpha = min(payload.alpha_limit,p.center_opacity.w*exp(-0.5*dot(closest,closest)));
  if (alpha >= payload.cutoff) payload.transmittance *= 1-alpha;
  // Native traversal is unordered. Product visibility is order independent;
  // disable duplicate primitive callbacks in the geometry descriptor on CPU.
  if (payload.transmittance <= payload.termination) { payload.transmittance = 0; return {true,t}; }
  return {false,0};
}
[[intersection(bounding_box)]]
ScanIntersection scan_radiance_intersection(float3 origin [[origin]],float3 ray [[direction]],float maximum [[max_distance]],
    uint id [[primitive_id]],ray_data ScanGather& q [[payload]],device const ScanParticle* particles [[buffer(0)]]) {
  // Use the original physical segment, not the intersector's shrinking search
  // bound. Clamping to a search bound would invent response at batch edges.
  const ScanParticle particle = particles[id]; const uint source_id = uint(particle.row2.w);
  const float4 peak = scan_peak(particle,origin,ray,q.maximum);
  if (peak.x > maximum || !scan_after_cursor(peak.x,source_id,q.cursor,q.cursor_id,q.started)) return {false,0};
  // Range/order rejections need no opacity evaluation or insertion walk.
  if (q.count == SCAN_GATHER_CAPACITY && !scan_before(peak.x,source_id,q.distances[0],q.ids[0])) return {false,0};
  const float2 hit(peak.x,min(q.alpha_limit,particle.center_opacity.w*exp(-0.5*dot(peak.yzw,peak.yzw))));
  if (hit.y < q.cutoff) return {false,0};
  // The root is the farthest retained (distance, source ID). Replacement only
  // moves a logarithmic number of entries; order the completed batch once.
  uint position;
  if (q.count < SCAN_GATHER_CAPACITY) {
    position = q.count++;
    while (position > 0) {
      const uint parent = (position-1)/2;
      if (!scan_before(q.distances[parent],q.ids[parent],hit.x,source_id)) break;
      q.distances[position] = q.distances[parent]; q.alphas[position] = q.alphas[parent]; q.ids[position] = q.ids[parent]; position = parent;
    }
  } else {
    position = 0;
    while (2*position+1 < q.count) {
      uint child = 2*position+1;
      if (child+1 < q.count && scan_before(q.distances[child],q.ids[child],q.distances[child+1],q.ids[child+1])) ++child;
      if (!scan_before(hit.x,source_id,q.distances[child],q.ids[child])) break;
      q.distances[position] = q.distances[child]; q.alphas[position] = q.alphas[child]; q.ids[position] = q.ids[child]; position = child;
    }
  }
  q.distances[position] = hit.x; q.alphas[position] = hit.y; q.ids[position] = source_id;
  // Bound the native search after eight retained responses, while allowing up
  // to 32 closer responses to accumulate. A partial batch is valid: the next
  // query resumes after its actual last peak on the original physical segment.
  // Only report this primitive's own peak when it is the farthest retained one.
  return {q.count >= 8 && q.ids[0] == source_id,hit.x};
}
// Capture receivers are Gaussian centers, not an offset solid surface. Exclude
// only the receiving particle by its stable source index; all other captured
// opacity remains a visibility field. Keep the legacy shadow payload unchanged.
struct CaptureShadowPayload { float transmittance, alpha_limit, cutoff, termination; uint receiver; };
[[intersection(bounding_box)]]
ScanIntersection capture_shadow_intersection(float3 origin [[origin]],float3 ray [[direction]],
    float minimum [[min_distance]],float maximum [[max_distance]],uint id [[primitive_id]],
    ray_data CaptureShadowPayload& payload [[payload]],device const ScanParticle* particles [[buffer(0)]]) {
  const ScanParticle p = particles[id];
  if (uint(p.row2.w) == payload.receiver) return {false,0};
  const float3 relative = origin-p.center_opacity.xyz;
  const float3 o(dot(p.row0.xyz,relative),dot(p.row1.xyz,relative),dot(p.row2.xyz,relative));
  const float3 d(dot(p.row0.xyz,ray),dot(p.row1.xyz,ray),dot(p.row2.xyz,ray));
  const float denominator = dot(d,d); if (denominator <= 0) return {false,0};
  const float t = clamp(-dot(o,d)/denominator,minimum,maximum);
  const float3 closest = fma(d,float3(t),o);
  const float alpha = min(payload.alpha_limit,p.center_opacity.w*exp(-0.5*dot(closest,closest)));
  if (alpha >= payload.cutoff) payload.transmittance *= 1-alpha;
  if (payload.transmittance <= payload.termination) { payload.transmittance = 0; return {true,t}; }
  return {false,0};
}
#endif
float scan_bounds(MeshNode node,float3 origin,float3 ray,float distance,float minimum = 0) {
  float enter = minimum, leave = distance;
  for (uint axis = 0; axis < 3; ++axis) {
    const float margin = 4.768371582e-7*max(1.0,max(abs(node.lower[axis]),abs(node.upper[axis])));
    if (abs(ray[axis]) < 1e-20) { if (origin[axis] < node.lower[axis]-margin || origin[axis] > node.upper[axis]+margin) leave = -1; }
    else {
      const float a = (node.lower[axis]-margin-origin[axis])/ray[axis], b = (node.upper[axis]+margin-origin[axis])/ray[axis];
      enter = max(enter,min(a,b)); leave = min(leave,max(a,b));
    }
  }
  return leave < enter ? 1e30 : enter;
}
float scan_transmittance(float3 origin,float3 ray,float distance,device const MeshNode* nodes,
    device const ScanParticle* particles,constant ScanVisibility& settings SCAN_RT_PARAMETERS) {
  if (settings.nodes == 0) return 1;
#if LOOKSY_SCAN_RT
  metal::raytracing::ray r; r.origin = origin; r.direction = ray; r.min_distance = 0; r.max_distance = distance;
  intersector<> trace; trace.assume_geometry_type(geometry_type::bounding_box); trace.accept_any_intersection(true);
  ScanPayload payload{1,settings.alpha_limit,settings.cutoff,settings.termination};
  trace.intersect(r,scan_acceleration,scan_functions,payload);
  return payload.transmittance;
#else
  // The median-built tree has depth <= 19 at the two-million-point cap.
  // Visit nearer bounds first so opaque surfaces terminate before distant
  // backdrop particles. The stack holds deferred siblings, never particle hits.
  float transmittance = 1; uint stack[32]; uint top = 1; stack[0] = 0;
  while (top > 0) {
    const uint index = stack[--top]; const MeshNode node = nodes[index];
    if (scan_bounds(node,origin,ray,distance) >= 1e30) continue;
    if (node.links.y == 0) {
      const uint left = index+1, right = nodes[left].links.z;
      const float a = scan_bounds(nodes[left],origin,ray,distance), b = scan_bounds(nodes[right],origin,ray,distance);
      if (a < b) { if (b < 1e30) stack[top++] = right; if (a < 1e30) stack[top++] = left; }
      else { if (a < 1e30) stack[top++] = left; if (b < 1e30) stack[top++] = right; }
      continue;
    }
    for (uint j = 0; j < node.links.y; ++j) {
      const ScanParticle p = particles[node.links.x+j]; const float3 relative = origin-p.center_opacity.xyz;
      const float3 o(dot(p.row0.xyz,relative),dot(p.row1.xyz,relative),dot(p.row2.xyz,relative));
      const float3 d(dot(p.row0.xyz,ray),dot(p.row1.xyz,ray),dot(p.row2.xyz,ray));
      const float denominator = dot(d,d); if (denominator <= 0) continue;
      const float t = clamp(-dot(o,d)/denominator,0.0,distance);
      const float3 closest = fma(d,float3(t),o);
      const float alpha = min(settings.alpha_limit,p.center_opacity.w*exp(-0.5*dot(closest,closest)));
      if (alpha >= settings.cutoff) transmittance *= 1-alpha;
      if (transmittance <= settings.termination) return 0;
    }
  }
  return transmittance;
#endif
}

float capture_transmittance(float3 origin,float3 ray,float distance,device const MeshNode* nodes,
    device const ScanParticle* particles,constant ScanVisibility& settings,uint receiver SCAN_RT_PARAMETERS) {
  if (settings.nodes == 0) return 1;
#if LOOKSY_SCAN_RT
  metal::raytracing::ray r; r.origin = origin; r.direction = ray; r.min_distance = 0; r.max_distance = distance;
  intersector<> trace; trace.assume_geometry_type(geometry_type::bounding_box); trace.accept_any_intersection(true);
  CaptureShadowPayload payload{1,settings.alpha_limit,settings.cutoff,settings.termination,receiver};
  trace.intersect(r,scan_acceleration,scan_functions,payload);
  return payload.transmittance;
#else
  // The median-built tree has depth <= 19 at the two-million-point cap.
  // Visit nearer bounds first so opaque surfaces terminate before distant
  // backdrop particles. The stack holds deferred siblings, never particle hits.
  float transmittance = 1; uint stack[32]; uint top = 1; stack[0] = 0;
  while (top > 0) {
    const uint index = stack[--top]; const MeshNode node = nodes[index];
    if (scan_bounds(node,origin,ray,distance) >= 1e30) continue;
    if (node.links.y == 0) {
      const uint left = index+1, right = nodes[left].links.z;
      const float a = scan_bounds(nodes[left],origin,ray,distance), b = scan_bounds(nodes[right],origin,ray,distance);
      if (a < b) { if (b < 1e30) stack[top++] = right; if (a < 1e30) stack[top++] = left; }
      else { if (a < 1e30) stack[top++] = left; if (b < 1e30) stack[top++] = right; }
      continue;
    }
    for (uint j = 0; j < node.links.y; ++j) {
      const ScanParticle p = particles[node.links.x+j]; if (uint(p.row2.w) == receiver) continue;
      const float3 relative = origin-p.center_opacity.xyz;
      const float3 o(dot(p.row0.xyz,relative),dot(p.row1.xyz,relative),dot(p.row2.xyz,relative));
      const float3 d(dot(p.row0.xyz,ray),dot(p.row1.xyz,ray),dot(p.row2.xyz,ray));
      const float denominator = dot(d,d); if (denominator <= 0) continue;
      const float t = clamp(-dot(o,d)/denominator,0.0,distance);
      const float3 closest = fma(d,float3(t),o);
      const float alpha = min(settings.alpha_limit,p.center_opacity.w*exp(-0.5*dot(closest,closest)));
      if (alpha >= settings.cutoff) transmittance *= 1-alpha;
      if (transmittance <= settings.termination) return 0;
    }
  }
  return transmittance;
#endif
}
ScanGather scan_gather(float3 origin,float3 ray,float maximum,float cursor,uint cursor_id,uint started,
    device const MeshNode* nodes,device const ScanParticle* particles,constant ScanVisibility& settings SCAN_RT_PARAMETERS) {
  ScanGather q; q.cursor = cursor; q.cursor_id = cursor_id; q.started = started; q.count = 0;
  q.maximum = maximum; q.alpha_limit = settings.alpha_limit; q.cutoff = settings.cutoff;
  for (uint i = 0; i < SCAN_GATHER_CAPACITY; ++i) { q.distances[i] = 1e30; q.alphas[i] = 0; q.ids[i] = 0xffffffff; }
  if (settings.nodes == 0) return q;
  // March the acceleration query as well as the ordered response cursor.
  // Re-querying the entire ray would revisit every earlier particle in every
  // batch. One predecessor float keeps equal-depth IDs and endpoint peaks.
  const float minimum = started != 0 ? max(0.0,nextafter(cursor,-1e30)) : 0.0;
#if LOOKSY_SCAN_RT
  metal::raytracing::ray r; r.origin = origin; r.direction = ray; r.min_distance = minimum; r.max_distance = maximum;
  intersector<> trace; trace.assume_geometry_type(geometry_type::bounding_box); trace.accept_any_intersection(false);
  trace.intersect(r,scan_acceleration,scan_functions,q);
#else
  uint stack[32]; uint top = 1; stack[0] = 0;
  while (top > 0) {
    const uint index = stack[--top]; const MeshNode node = nodes[index];
    const float limit = q.count == SCAN_GATHER_CAPACITY ? min(maximum,q.distances[0]) : maximum;
    if (scan_bounds(node,origin,ray,limit,minimum) >= 1e30) continue;
    if (node.links.y == 0) {
      const uint left = index+1, right = nodes[left].links.z;
      const float a = scan_bounds(nodes[left],origin,ray,limit,minimum), b = scan_bounds(nodes[right],origin,ray,limit,minimum);
      if (a < b) { if (b < 1e30) stack[top++] = right; if (a < 1e30) stack[top++] = left; }
      else { if (a < 1e30) stack[top++] = left; if (b < 1e30) stack[top++] = right; }
      continue;
    }
    for (uint j = 0; j < node.links.y; ++j) {
      const ScanParticle p = particles[node.links.x+j]; const uint id = uint(p.row2.w);
      const float4 peak = scan_peak(p,origin,ray,maximum);
      if (!scan_after_cursor(peak.x,id,cursor,cursor_id,started)) continue;
      if (q.count == SCAN_GATHER_CAPACITY && !scan_before(peak.x,id,q.distances[0],q.ids[0])) continue;
      const float2 hit(peak.x,min(settings.alpha_limit,p.center_opacity.w*exp(-0.5*dot(peak.yzw,peak.yzw))));
      if (hit.y < settings.cutoff) continue;
      uint position;
      if (q.count < SCAN_GATHER_CAPACITY) {
        position = q.count++;
        while (position > 0) {
          const uint parent = (position-1)/2;
          if (!scan_before(q.distances[parent],q.ids[parent],hit.x,id)) break;
          q.distances[position] = q.distances[parent]; q.alphas[position] = q.alphas[parent]; q.ids[position] = q.ids[parent]; position = parent;
        }
      } else {
        position = 0;
        while (2*position+1 < q.count) {
          uint child = 2*position+1;
          if (child+1 < q.count && scan_before(q.distances[child],q.ids[child],q.distances[child+1],q.ids[child+1])) ++child;
          if (!scan_before(hit.x,id,q.distances[child],q.ids[child])) break;
          q.distances[position] = q.distances[child]; q.alphas[position] = q.alphas[child]; q.ids[position] = q.ids[child]; position = child;
        }
      }
      q.distances[position] = hit.x; q.alphas[position] = hit.y; q.ids[position] = id;
    }
  }
#endif
  // Heap extraction produces the same ascending, stable source order consumed
  // by opacity compositing, independently of native/software traversal order.
  for (uint end = q.count; end > 1; --end) {
    const float distance = q.distances[end-1], alpha = q.alphas[end-1]; const uint id = q.ids[end-1];
    q.distances[end-1] = q.distances[0]; q.alphas[end-1] = q.alphas[0]; q.ids[end-1] = q.ids[0];
    uint position = 0;
    while (2*position+1 < end-1) {
      uint child = 2*position+1;
      if (child+1 < end-1 && scan_before(q.distances[child],q.ids[child],q.distances[child+1],q.ids[child+1])) ++child;
      if (!scan_before(distance,id,q.distances[child],q.ids[child])) break;
      q.distances[position] = q.distances[child]; q.alphas[position] = q.alphas[child]; q.ids[position] = q.ids[child]; position = child;
    }
    q.distances[position] = distance; q.alphas[position] = alpha; q.ids[position] = id;
  }
  return q;
}
float mesh_hit(uint root, float3 origin, float3 ray, device const MeshNode* nodes,
    device const MeshTriangle* triangles, thread float3& normal, thread uint& triangle_index, thread float2& barycentric) {
  // Conservative float bounds and a one-millionth barycentric edge allowance
  // prevent pinholes under tiny pupil shifts. Near-equal hits use stable IDs.
  float nearest = 1e30; uint stack[64]; uint top = 1; stack[0] = root;
  while (top > 0) {
    const MeshNode node = nodes[stack[--top]];
    float enter = 0, leave = nearest+4e-7*max(1.0,abs(nearest));
    for (uint axis = 0; axis < 3; ++axis) {
      const float margin = 4.768371582e-7*max(1.0,max(abs(node.lower[axis]),abs(node.upper[axis])));
      if (abs(ray[axis]) < 1e-20) { if (origin[axis] < node.lower[axis]-margin || origin[axis] > node.upper[axis]+margin) leave = -1; }
      else {
        const float a = (node.lower[axis]-margin-origin[axis])/ray[axis], b = (node.upper[axis]+margin-origin[axis])/ray[axis];
        enter = max(enter,min(a,b)); leave = min(leave,max(a,b));
      }
    }
    if (leave < enter) continue;
    if (node.links.w == 0) { stack[top++] = node.links.x; stack[top++] = node.links.y; continue; }
    for (uint j = 0; j < node.links.w; ++j) {
      const MeshTriangle t = triangles[node.links.z+j];
      const float3 p = cross(ray,t.edge2.xyz); const float det = dot(t.edge1.xyz,p);
      if (abs(det) < 1e-20) continue;
      const float3 delta = origin-t.position.xyz; const float u = dot(delta,p)/det;
      if (u < -1e-6 || u > 1+1e-6) continue;
      const float3 q = cross(delta,t.edge1.xyz); const float v = dot(ray,q)/det;
      if (v < -1e-6 || u+v > 1+1e-6) continue;
      const float distance = dot(t.edge2.xyz,q)/det;
      const float tie = 4e-7*max(1.0,abs(distance));
      if (distance > 1e-5 && (distance < nearest-tie || (abs(distance-nearest) <= tie && node.links.z+j < triangle_index))) {
        nearest = distance; triangle_index = node.links.z+j; barycentric = max(float2(u,v),0.0);
        barycentric /= max(1.0,barycentric.x+barycentric.y);
        float3 interpolated = t.normal0.xyz*(1-barycentric.x-barycentric.y)+t.normal1.xyz*barycentric.x+t.normal2.xyz*barycentric.y;
        if (dot(interpolated,interpolated) < 1e-12) interpolated = cross(t.edge1.xyz,t.edge2.xyz);
        normal = normalize(interpolated); if (dot(normal,ray) > 0) normal = -normal;
      }
    }
  }
  return nearest;
}
float solid_hit(Solid solid, float3 relative_origin, float3 direction, thread float3& normal, device const MeshNode* nodes, device const MeshTriangle* triangles, thread uint& triangle_index, thread float2& barycentric) {
  triangle_index = 0xffffffff; barycentric = 0;
  const float3 origin = float3(dot(relative_origin, solid.axis_x.xyz)*solid.axis_x.w,
      dot(relative_origin, solid.axis_y.xyz)*solid.axis_y.w, dot(relative_origin, solid.axis_z.xyz)*solid.axis_z.w);
  const float3 ray = float3(dot(direction, solid.axis_x.xyz)*solid.axis_x.w,
      dot(direction, solid.axis_y.xyz)*solid.axis_y.w, dot(direction, solid.axis_z.xyz)*solid.axis_z.w);
  float distance = 1e30; float3 local_normal = 0;
  if (solid.center_kind.w == 0) {
    const float a = dot(ray, ray), b = dot(origin, ray), c = dot(origin, origin)-1;
    const float discriminant = b*b-a*c;
    if (discriminant < 0 || a <= 0) return 1e30;
    const float root = sqrt(discriminant);
    distance = (-b-root)/a;
    if (distance <= 1e-5) distance = (-b+root)/a;
    if (distance <= 1e-5) return 1e30;
    local_normal = origin+ray*distance;
  } else if (solid.center_kind.w == 2) {
    distance = mesh_hit(solid.geometry.x,origin,ray,nodes,triangles,local_normal,triangle_index,barycentric);
    if (distance >= 1e29) return distance;
  } else {
    float enter = -1e30, leave = 1e30; float3 enter_normal = 0, leave_normal = 0;
    for (uint axis = 0; axis < 3; ++axis) {
      if (abs(ray[axis]) < 1e-12) { if (abs(origin[axis]) > 1) return 1e30; continue; }
      const float first = (-1-origin[axis])/ray[axis], last = (1-origin[axis])/ray[axis];
      float3 n = 0; n[axis] = ray[axis] > 0 ? -1.0 : 1.0;
      if (min(first,last) > enter) { enter = min(first,last); enter_normal = n; }
      if (max(first,last) < leave) { leave = max(first,last); leave_normal = -n; }
    }
    if (leave < max(enter, 1e-5)) return 1e30;
    distance = enter > 1e-5 ? enter : leave; local_normal = enter > 1e-5 ? enter_normal : leave_normal;
  }
  normal = normalize(solid.axis_x.xyz*local_normal.x*solid.axis_x.w +
      solid.axis_y.xyz*local_normal.y*solid.axis_y.w + solid.axis_z.xyz*local_normal.z*solid.axis_z.w);
  return distance;
}
float solid_hit(Solid solid, float3 relative_origin, float3 direction, thread float3& normal, device const MeshNode* nodes, device const MeshTriangle* triangles) {
  uint triangle_index; float2 barycentric;
  return solid_hit(solid,relative_origin,direction,normal,nodes,triangles,triangle_index,barycentric);
}
float2 triangle_uv(MeshTriangle triangle, float2 barycentric) {
  return triangle.uv01.xy*(1-barycentric.x-barycentric.y)+triangle.uv01.zw*barycentric.x+triangle.uv2_material.xy*barycentric.y;
}
float2 triangle_uv_ray(MeshTriangle triangle,float3 origin,float3 ray,float2 fallback) {
  const float3 p = cross(ray,triangle.edge2.xyz); const float determinant = dot(triangle.edge1.xyz,p);
  if (abs(determinant) < 1e-20) return fallback+1;
  const float3 delta = origin-triangle.position.xyz, q = cross(delta,triangle.edge1.xyz);
  return triangle_uv(triangle,float2(dot(delta,p),dot(ray,q))/determinant);
}
// A covariance's shortest axis estimates an unoriented surface normal. The
// capture contains no true normals and this cannot remove baked illumination.
kernel void derive_capture_normals(device const Point* points [[buffer(0)]], device const Motion* motions [[buffer(1)]],
    device float4* normals [[buffer(2)]], constant uint4& layout [[buffer(3)]], uint i [[thread_position_in_grid]]) {
  i += layout.z;
  if (i >= layout.x) return;
  const Point p = points[i];
  float3x3 a(float3(p.a.x,p.a.y,p.a.z),float3(p.a.y,p.a.w,p.b.x),float3(p.a.z,p.b.x,p.b.y));
  if (layout.y > 0) {
    const Motion m = motions[i]; const float3x3 factor(m.factor0.xyz,m.factor1.xyz,m.factor2.xyz);
    a = factor*transpose(factor);
  }
  float3x3 eigen(1.0);
  for (uint iteration = 0; iteration < 12; ++iteration) {
    uint p = 0, q = 1;
    if (abs(a[2][0]) > abs(a[q][p])) { p = 0; q = 2; }
    if (abs(a[2][1]) > abs(a[q][p])) { p = 1; q = 2; }
    if (abs(a[q][p]) < 1e-7*max(1e-20,max(abs(a[0][0]),max(abs(a[1][1]),abs(a[2][2]))))) break;
    const float angle = 0.5*atan2(2*a[q][p],a[q][q]-a[p][p]);
    const float c = cos(angle), s = sin(angle), app = a[p][p], aqq = a[q][q], apq = a[q][p];
    for (uint k = 0; k < 3; ++k) if (k != p && k != q) {
      const float akp = a[p][k], akq = a[q][k];
      a[p][k] = a[k][p] = c*akp-s*akq; a[q][k] = a[k][q] = s*akp+c*akq;
    }
    a[p][p] = c*c*app-2*c*s*apq+s*s*aqq; a[q][q] = s*s*app+2*c*s*apq+c*c*aqq;
    a[q][p] = a[p][q] = 0;
    const float3 ep = eigen[p], eq = eigen[q]; eigen[p] = c*ep-s*eq; eigen[q] = s*ep+c*eq;
  }
  uint axis = a[0][0] < a[1][1] ? 0 : 1; if (a[2][2] < a[axis][axis]) axis = 2;
  const float largest = max(a[0][0],max(a[1][1],a[2][2]));
  const float confidence = 1-max(0.0,a[axis][axis])/max(largest,1e-20);
  const float3 normal = normalize(eigen[axis]);
  normals[i] = all(isfinite(normal)) ? float4(normal,confidence) : float4(0);
}
float3 captured_object_emission(Solid solid,uint triangle_id,device const MeshTriangle* triangles,device const SurfaceMaterial* materials) {
  if (solid.geometry.y > 0 && triangle_id != 0xffffffff) {
    const uint slot = uint(triangles[triangle_id].uv2_material.z);
    if (slot < solid.geometry.z) return materials[solid.geometry.y-1+slot].emission_metallic.xyz;
  }
  return decode_srgb(solid.color_roughness.xyz)*solid.material.y;
}
float emission_bound_cap(float4 bounds,float3 origin) {
  const float d2 = dot(bounds.xyz-origin,bounds.xyz-origin), r2 = bounds.w*bounds.w;
  if (d2 <= r2) return 2; // Receiver inside a conservative bound: full sphere.
  const float sine2 = min(1.0,r2/d2);
  return sine2/(1+sqrt(max(0.0,1-sine2)));
}
float emission_bounds_pdf(float3 ray,float3 origin,constant float4* bounds,uint objects,uint emitters) {
  float pdf = 0;
  for (uint j = 0; j < objects; ++j) if (bounds[j].w > 0) {
    const float cap = emission_bound_cap(bounds[j],origin);
    const float3 axis = normalize(bounds[j].xyz-origin);
    const float3 perpendicular = cross(axis,ray);
    if (cap >= 2 || (dot(axis,ray) > 0 && dot(perpendicular,perpendicular) <= cap*(2-cap)*(1+1e-5)))
      pdf += 1/(2*M_PI_F*cap);
  }
  return pdf/float(emitters);
}
kernel void shade_capture(device const Point* points [[buffer(0)]], device const Motion* motions [[buffer(1)]],
    device const float4* normals [[buffer(2)]], device float4* illumination [[buffer(3)]],
    constant Camera& camera [[buffer(4)]], constant Composition& composition [[buffer(5)]],
    constant Solid* solids [[buffer(6)]], constant Light* lights [[buffer(7)]], constant uint& count [[buffer(8)]],
    device const MeshNode* nodes [[buffer(9)]], device const MeshTriangle* triangles [[buffer(10)]], constant LightIntegration& integration [[buffer(11)]],
    device const SurfaceMaterial* materials [[buffer(12)]],device const MeshNode* scan_nodes [[buffer(13)]],
    device const ScanParticle* scan_particles [[buffer(14)]],constant ScanVisibility& scan [[buffer(15)]],
#if LOOKSY_SCAN_RT
    primitive_acceleration_structure scan_acceleration [[buffer(16)]],intersection_function_table<> scan_functions [[buffer(17)]],
#endif
    constant float4* emission_bounds [[buffer(18)]], device float4* shadow_accumulation [[buffer(19)]],
    uint i [[thread_position_in_grid]]) {
  i += integration.point_offset;
  if (i >= count) return;
  float3 center = points[i].center_opacity.xyz;
  if (camera.temporal.y > 0) center += motions[i].velocity_time.xyz*(camera.temporal.x-motions[i].velocity_time.w);
  const float3 view = camera.position.xyz-center;
  const float4 estimate = normals[i];
  float3 normal = estimate.w < 0.05 ? view*rsqrt(max(dot(view,view),1e-20)) : estimate.xyz;
  if (dot(normal,view) < 0) normal = -normal;
  const bool receiver = (composition.ray_modes.w & 8u) != 0;
  float3 irradiance(composition.ambient), reference(composition.ambient);
  for (uint j = 0; j < composition.lights; ++j) {
    const Light light = lights[j]; if (light.color_intensity.w <= 0) continue; const float3 delta = light.vector_kind.w == 0 ? light.vector_kind.xyz : light.vector_kind.xyz-center;
    const bool finite = finite_light(light); const uint samples = finite ? integration.count : 1;
    float3 contribution(0), unoccluded(0);
    for (uint sample = 0; sample < samples; ++sample) {
      const LightSample source = sample_light(light,delta,finite ? integration.first+sample : 0,finite ? integration.total : 1,light_hash(i)^light_hash(j+1));
      const float3 l = source.direction; const float nl = max(0.0,dot(normal,l)); if (nl <= 0 || source.weight <= 0) continue;
      bool blocked = false;
      for (uint k = 0; k < composition.objects; ++k) {
        float3 ignored;
        const float hit = solid_hit(solids[k],(center-solids[k].center_kind.xyz)+normal*1e-5,l,ignored,nodes,triangles);
        if (hit < source.distance-1e-5) { blocked = true; break; }
      }
      const float3 incoming = decode_srgb(light.color_intensity.xyz)*light.color_intensity.w*source.weight*nl/M_PI_F;
      contribution += incoming*(blocked ? 1-composition.capture.y : 1);
      if (receiver) unoccluded += incoming;
    }
    irradiance += contribution/float(samples);
    if (receiver) reference += unoccluded/float(samples);
  }
  if ((composition.ray_modes.w & 1u) != 0) {
    const float3 tangent = normalize(cross(abs(normal.z) < 0.999 ? float3(0,0,1) : float3(0,1,0),normal));
    const float3 bitangent = cross(normal,tangent), origin = center+normal*1e-5;
    float3 contribution(0);
    uint emitters = 0; float cap_sum = 0;
    if ((composition.ray_modes.w & 4u) != 0) for (uint object = 0; object < composition.objects; ++object) if (emission_bounds[object].w > 0) {
      ++emitters; cap_sum += emission_bound_cap(emission_bounds[object],origin);
    }
    const bool targeted = emitters > 0 && integration.total >= 2;
    // Small bounds benefit from spending seven of eight rays on emitters.
    // Retain a cosine stratum for full support and use the exact allocation
    // fractions in the mixture PDF. Broad/containing bounds keep a 50/50 mix.
    const uint stride = targeted && (integration.total%8u) == 0 && cap_sum < 0.125 ? 8u : 2u;
    const uint cosine_count = integration.total/stride;
    const float cosine_fraction = 1/float(stride);
    for (uint sample = integration.first; sample < integration.first+integration.count; ++sample) {
      const bool light_choice = targeted && (sample%stride) != 0;
      const uint sample_index = targeted ? light_choice ? sample-sample/stride-1 : sample/stride : sample;
      const uint sample_count = targeted ? light_choice ? integration.total-cosine_count : cosine_count : integration.total;
      const float2 uv = light_sample_2d(sample_index,sample_count,light_hash(i)^(light_choice ? 0x9e3779b9 : 0xc2b2ae35));
      const float radius = sqrt(uv.x), phi = 2*M_PI_F*uv.y;
      float3 ray = tangent*(radius*cos(phi))+bitangent*(radius*sin(phi))+normal*sqrt(1-uv.x);
      if (light_choice) {
        const float selector = min(uv.x*float(emitters),float(emitters)-1e-6);
        const uint selected = uint(selector); uint ordinal = 0;
        for (uint object = 0; object < composition.objects; ++object) if (emission_bounds[object].w > 0) {
          if (ordinal++ != selected) continue;
          const float4 bound = emission_bounds[object]; const float cap = emission_bound_cap(bound,origin);
          const float3 axis = cap >= 2 ? normal : normalize(bound.xyz-origin);
          ray = light_cone_direction(axis,cap,float2(selector-float(selected),uv.y)); break;
        }
      }
      const float nl = max(0.0,dot(normal,ray));
      if (targeted && nl <= 0) continue;
      const float sample_weight = targeted ? (nl/M_PI_F)/(cosine_fraction*nl/M_PI_F+(1-cosine_fraction)*emission_bounds_pdf(ray,origin,emission_bounds,composition.objects,emitters)) : 1;
      float nearest = 1e30, emitting_distance = 1e30; float3 emission(0);
      for (uint object = 0; object < composition.objects; ++object) {
        float3 ignored; uint triangle_id; float2 barycentric;
        const float distance = solid_hit(solids[object],origin-solids[object].center_kind.xyz,ray,ignored,nodes,triangles,triangle_id,barycentric);
        nearest = min(nearest,distance);
        if (distance < emitting_distance) {
          const float3 value = captured_object_emission(solids[object],triangle_id,triangles,materials);
          if (any(value > 0)) { emission = value; emitting_distance = distance; }
        }
      }
      if (emitting_distance >= 1e29) continue;
      float visibility = nearest < emitting_distance-1e-5 ? 0 : 1;
      if (visibility > 0 && (composition.ray_modes.w & 2u) != 0 && composition.capture.y > 0)
        visibility = capture_transmittance(origin,ray,max(0.0,emitting_distance-1e-5),scan_nodes,scan_particles,scan,i SCAN_RT_ARGUMENTS);
      visibility = 1-composition.capture.y+composition.capture.y*visibility;
      // Cosine sampling cancels Lambert's cosine/pi. This estimates authored
      // material emission only, without sampling explicit lights a second time.
      contribution += emission*visibility*exp(-camera.hybrid.z*emitting_distance)*sample_weight;
    }
    irradiance += contribution/float(integration.count);
  }
  if (receiver) {
    // Accumulate numerator and denominator with identical finite-source samples.
    // Averaging batch ratios would bias penumbrae when source weights vary.
    if (integration.first > 0) {
      const float first = float(integration.first), count = float(integration.count), total = first+count;
      irradiance = (shadow_accumulation[2*i].xyz*first+irradiance*count)/total;
      reference = (shadow_accumulation[2*i+1].xyz*first+reference*count)/total;
    }
    shadow_accumulation[2*i] = float4(irradiance,0);
    shadow_accumulation[2*i+1] = float4(reference,0);
    const float3 ratio(reference.x > 0 ? irradiance.x/reference.x : 1,
        reference.y > 0 ? irradiance.y/reference.y : 1, reference.z > 0 ? irradiance.z/reference.z : 1);
    illumination[i] = float4(clamp(ratio,0.0,1.0)*composition.capture.x,1);
    return;
  }
  float3 value = irradiance*composition.capture.x;
  if (integration.first > 0) value = (illumination[i].xyz*float(integration.first)+value*float(integration.count))/float(integration.first+integration.count);
  illumination[i] = float4(value,1);
}
)METAL") + R"METAL(
// Integral of a homogeneous single-scattering medium. Stratify by extinction
// mass rather than distance so long, optically thick rays do not miss the
// visible foreground. The base fog term is analytic. Explicit source terms use
// the same finite-source sampler and geometry/scan visibility as surface light.
// Directional intensity retains the existing creative haze gain of 0.05.
uint medium_seed(float3 origin,float3 ray) {
  return light_hash(as_type<uint>(origin.x)) ^ light_hash(as_type<uint>(origin.y)) ^ light_hash(as_type<uint>(origin.z)) ^
      light_hash(as_type<uint>(ray.x)) ^ light_hash(as_type<uint>(ray.y)) ^ light_hash(as_type<uint>(ray.z));
}
float4 medium_integral(float3 origin,float3 ray,float distance,uint samples,uint seed,
    constant Camera& camera,constant Composition& composition,constant Solid* solids,constant Light* lights,
    device const MeshNode* nodes,device const MeshTriangle* triangles,
    device const MeshNode* scan_nodes,device const ScanParticle* particles,constant ScanVisibility& scan SCAN_RT_PARAMETERS) {
  const float optical_depth = camera.hybrid.z*max(0.0,distance);
  const float transmittance = exp(-optical_depth);
  const float mass = optical_depth < 1e-3 ? optical_depth*(1-optical_depth*(0.5-optical_depth/6)) : 1-transmittance;
  const float3 tint = decode_srgb(camera.atmosphere.xyz);
  float3 radiance = tint*mass;
  if (mass <= 0 || !any(tint > 0)) return float4(radiance,transmittance);
  const float g = camera.atmosphere.w;
  for (uint s = 0; s < samples; ++s) {
    const float u = (float(s)+0.5)/float(samples);
    const float t = optical_depth < 1e-4 ? distance*u : -log(1-min(u*mass,0.99999994))/camera.hybrid.z;
    const float3 position = origin+ray*t;
    for (uint j = 0; j < composition.lights; ++j) {
      const Light light = lights[j]; if (light.color_intensity.w <= 0) continue;
      const uint salt = light_hash(seed ^ light_hash(j+1) ^ light_hash(s+1));
      const float3 delta = light.vector_kind.w == 0 ? light.vector_kind.xyz : light.vector_kind.xyz-position;
      const LightSample source = sample_light(light,delta,salt % max(samples,1u),max(samples,1u),light_hash(salt));
      if (source.weight <= 0) continue;
      bool blocked = false;
      for (uint k = 0; k < composition.objects; ++k) {
        float3 ignored;
        if (solid_hit(solids[k],position-solids[k].center_kind.xyz,source.direction,ignored,nodes,triangles) < source.distance-1e-5) { blocked = true; break; }
      }
      if (blocked) continue;
      const float visibility = scan.nodes > 0 ? scan_transmittance(position,source.direction,max(0.0,source.distance-1e-5),scan_nodes,particles,scan SCAN_RT_ARGUMENTS) : 1;
      const float cosine = dot(ray,source.direction);
      const float phase = (1-g*g)/pow(max(1e-6,1+g*g-2*g*cosine),1.5);
      // Finite sources lose energy along their source-to-volume segment.
      const float incident = light.vector_kind.w == 0 ? 1 : exp(-camera.hybrid.z*source.distance);
      radiance += tint*(mass/float(samples))*0.05*decode_srgb(light.color_intensity.xyz)*light.color_intensity.w*source.weight*phase*visibility*incident;
    }
  }
  return float4(min(radiance,65504.0),transmittance);
}
// The prefix is parameterized by extinction mass u=1-exp(-sigma*t).
// One visibility sample per stratum serves every primary layer on this ray.
// Global grid coordinates and seeds are invariant under export tile partition.
kernel void sample_haze_field(texture3d<float,access::write> field [[texture(0)]],
    constant Camera& camera [[buffer(0)]],constant Composition& composition [[buffer(1)]],constant Solid* solids [[buffer(2)]],constant Light* lights [[buffer(3)]],
    device const MeshNode* nodes [[buffer(4)]],device const MeshTriangle* triangles [[buffer(5)]],
    device const MeshNode* scan_nodes [[buffer(6)]],device const ScanParticle* particles [[buffer(7)]],constant ScanVisibility& scan [[buffer(8)]],
#if LOOKSY_SCAN_RT
    primitive_acceleration_structure scan_acceleration [[buffer(9)]],intersection_function_table<> scan_functions [[buffer(10)]],
#endif
    constant uint2& band [[buffer(11)]],uint3 xyz [[thread_position_in_grid]]) {
  uint2 xy = xyz.xy;
  xy.y += band.x;
  if (xy.x >= field.get_width() || xy.y >= field.get_height() || xy.y >= band.x+band.y) return;
  const float2 pixel = (float2(xy)+camera.medium_grid.xy)*camera.medium.z+0.5;
  const float3 camera_ray = normalize(float3((pixel-camera.intrinsics.zw)/camera.intrinsics.xy,1));
  const float3x3 rotation(camera.view[0].xyz,camera.view[1].xyz,camera.view[2].xyz);
  const float3 ray = transpose(rotation)*camera_ray, origin = camera.position.xyz+camera.aperture_world.xyz;
  const uint seed = light_hash(as_type<uint>(int(pixel.x))) ^ light_hash(as_type<uint>(int(pixel.y)));
  const uint count = field.get_depth(), s = xyz.z;
  if (s >= count) return;
  const float3 tint = decode_srgb(camera.atmosphere.xyz); const float g = camera.atmosphere.w;
  float3 contribution(0);
  const float u = (float(s)+0.5)/float(count), t = -log(1-u)/camera.hybrid.z;
  const float3 position = origin+ray*t;
  for (uint j = 0; j < composition.lights; ++j) {
    const Light light = lights[j]; if (light.color_intensity.w <= 0) continue;
    const uint salt = light_hash(seed ^ light_hash(j+1) ^ light_hash(s+1));
    const float3 delta = light.vector_kind.w == 0 ? light.vector_kind.xyz : light.vector_kind.xyz-position;
    const LightSample source = sample_light(light,delta,salt%count,count,light_hash(salt));
    if (source.weight <= 0) continue;
    bool blocked = false;
    for (uint k = 0; k < composition.objects; ++k) {
      float3 ignored;
      if (solid_hit(solids[k],position-solids[k].center_kind.xyz,source.direction,ignored,nodes,triangles) < source.distance-1e-5) { blocked = true; break; }
    }
    if (blocked) continue;
    const float visibility = scan.nodes > 0 ? scan_transmittance(position,source.direction,max(0.0,source.distance-1e-5),scan_nodes,particles,scan SCAN_RT_ARGUMENTS) : 1;
    const float cosine = dot(ray,source.direction);
    const float phase = (1-g*g)/pow(max(1e-6,1+g*g-2*g*cosine),1.5);
    const float incident = light.vector_kind.w == 0 ? 1 : exp(-camera.hybrid.z*source.distance);
    contribution += tint/float(count)*0.05*decode_srgb(light.color_intensity.xyz)*light.color_intensity.w*source.weight*phase*visibility*incident;
  }
  field.write(float4(contribution,0),uint3(xy,s));
}
kernel void integrate_haze_field(texture3d<float,access::read> samples [[texture(0)]],texture3d<float,access::write> field [[texture(1)]],
    uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= field.get_width() || xy.y >= field.get_height()) return;
  float3 prefix(0); field.write(float4(0),uint3(xy,0));
  for (uint s = 0; s < samples.get_depth(); ++s) {
    prefix += samples.read(uint3(xy,s)).rgb;
    field.write(float4(min(prefix,65504.0),0),uint3(xy,s+1));
  }
}
float4 primary_medium(texture3d<float> field,float2 pixel,float distance,constant Camera& camera) {
  const float optical_depth = camera.hybrid.z*max(0.0,distance), transmittance = exp(-optical_depth);
  const float mass = optical_depth < 1e-3 ? optical_depth*(1-optical_depth*(0.5-optical_depth/6)) : 1-transmittance;
  const float2 grid = (pixel-0.5)/camera.medium.z-camera.medium_grid.xy;
  const float3 uv = float3((grid+0.5)/camera.medium_grid.zw,(mass*camera.medium.w+0.5)/(camera.medium.w+1));
  constexpr sampler interpolate(coord::normalized,address::clamp_to_edge,filter::linear);
  const float3 radiance = decode_srgb(camera.atmosphere.xyz)*mass+field.sample(interpolate,uv).rgb;
  return float4(min(radiance,65504.0),transmittance);
}
kernel void shadowed_haze(texture2d<float,access::write> haze [[texture(0)]],texture2d<float,access::read_write> surface [[texture(1)]],texture3d<float> field [[texture(2)]],
    constant Camera& camera [[buffer(0)]],constant uint2& band [[buffer(1)]],uint2 xy [[thread_position_in_grid]]) {
  xy.y += band.x;
  if (xy.x >= haze.get_width() || xy.y >= haze.get_height() || xy.y >= band.x+band.y) return;
  const float2 pixel = float2(xy)+camera.raster.xy+0.5;
  const float3 ray = normalize(float3((pixel-camera.intrinsics.zw)/camera.intrinsics.xy,1));
  const float4 background = primary_medium(field,pixel,camera.hybrid.w,camera);
  haze.write(float4(background.rgb,ray.z),xy);
  if (camera.hybrid.x > 0) {
    const float4 hit = surface.read(xy);
    if (hit.w < 1e29) {
      const float4 fog = primary_medium(field,pixel,hit.w/max(ray.z,1e-6),camera);
      surface.write(float4(fog.rgb+hit.rgb*fog.w,hit.w),xy);
    }
  }
}
kernel void render_haze(texture2d<float,access::write> output [[texture(0)]], constant Camera& camera [[buffer(0)]],
    constant Composition& composition [[buffer(1)]], constant Light* lights [[buffer(2)]], uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= output.get_width() || xy.y >= output.get_height()) return;
  const float2 pixel = float2(xy)+camera.raster.xy+0.5;
  const float3 ray = normalize(float3((pixel-camera.intrinsics.zw)/camera.intrinsics.xy,1));
  const float3x3 rotation(camera.view[0].xyz,camera.view[1].xyz,camera.view[2].xyz);
  const float3 world_ray = transpose(rotation)*ray;
  const float g = camera.atmosphere.w;
  float3 scattering(1);
  for (uint i = 0; i < composition.lights; ++i) {
    const Light light = lights[i]; if (light.vector_kind.w != 0) continue;
    const float cosine = dot(world_ray,normalize(light.vector_kind.xyz));
    const float phase = (1-g*g)/pow(max(1e-6,1+g*g-2*g*cosine),1.5);
    scattering += 0.05*decode_srgb(light.color_intensity.xyz)*light.color_intensity.w*phase;
  }
  output.write(float4(min(decode_srgb(camera.atmosphere.xyz)*scattering,65504.0),ray.z),xy);
}
kernel void complete_haze(texture2d<float,access::read_write> accumulation [[texture(0)]],
    texture2d<float,access::read> haze [[texture(1)]], constant Camera& camera [[buffer(0)]], uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= accumulation.get_width() || xy.y >= accumulation.get_height()) return;
  const float4 front = accumulation.read(xy);
  const float3 background = encode_srgb(haze.read(xy).rgb*(camera.medium.x > 0 ? 1 : 1-exp(-camera.hybrid.z*camera.hybrid.w)));
  accumulation.write(float4(front.rgb+front.a*background,front.a),xy);
}
vertex Vertex splat_vertex(uint vertex_id [[vertex_id]], uint instance [[instance_id]],
    device const Point* points [[buffer(0)]], device const uint* order [[buffer(1)]],
    constant Camera& camera [[buffer(2)]], device const Motion* motions [[buffer(3)]],
    device const Appearance* appearances [[buffer(4)]], device const float4* illumination [[buffer(5)]]) {
  Vertex out;
  out.position = float4(2, 2, 2, 1); out.delta = 0; out.conic = 0; out.color = 0; out.depth = 0;
  const uint point_index = order[instance];
  const Point p = points[point_index];
  float3 world_center = p.center_opacity.xyz;
  float opacity = p.center_opacity.w;
  float dt = 0;
  if (camera.temporal.y > 0) {
    const Motion motion = motions[point_index];
    dt = camera.temporal.x - motion.velocity_time.w;
    const float standardized_time = dt * motion.normalization.x;
    opacity *= exp(-0.5 * standardized_time * standardized_time);
    if (opacity < 1.0 / 255.0) return out;
    world_center = fma(motion.velocity_time.xyz, float3(dt), world_center);
  }
  float3 center = (camera.view * float4(world_center, 1)).xyz;
  center.xy -= camera.aperture.xy;
  if (center.z < 0.01 || center.z > 1e10 || !all(isfinite(center))) return out;
  const float3x3 covariance(float3(p.a.x, p.a.y, p.a.z), float3(p.a.y, p.a.w, p.b.x), float3(p.a.z, p.b.x, p.b.y));
  const float3x3 rotation(camera.view[0].xyz, camera.view[1].xyz, camera.view[2].xyz);
  const float3x3 view_cov = rotation * covariance * transpose(rotation);
  const float2 focal = camera.intrinsics.xy;
  const float2 principal = camera.intrinsics.zw;
  const float2 uv = center.xy / center.z;
  const float2 limited = clamp(uv, (-0.15 * camera.image.xy - principal) / focal,
      (1.15 * camera.image.xy - principal) / focal);
  const float3 jx(focal.x / center.z, 0, -focal.x * limited.x / center.z);
  const float3 jy(0, focal.y / center.z, -focal.y * limited.y / center.z);
  float a, b, c;
  if (camera.temporal.y > 0) {
    const Motion motion = motions[point_index];
    const float3x3 factor(motion.factor0.xyz, motion.factor1.xyz, motion.factor2.xyz);
    const float3x3 projected = transpose(rotation * factor);
    const float3 px = projected * jx, py = projected * jy;
    a = dot(px, px) + 0.3;
    b = dot(px, py);
    c = dot(py, py) + 0.3;
  } else {
    a = dot(jx, view_cov * jx) + 0.3;
    b = dot(jx, view_cov * jy);
    c = dot(jy, view_cov * jy) + 0.3;
  }
  const float determinant = a * c - b * b;
  if (determinant <= 0 || !isfinite(determinant) || p.center_opacity.w < 1.0 / 255.0) return out;
  const float power_limit = log(255.0 * p.center_opacity.w);
  const float2 extent = sqrt(2.0 * power_limit * float2(a, c));
  const float2 pixel = focal * uv + principal;
  if (any(pixel + extent < min(float2(0),camera.raster.xy)) ||
      any(pixel - extent > max(camera.image.xy,camera.raster.xy+camera.raster.zw))) return out;
  // A strip emits the same two triangles with four shader invocations instead
  // of six: covariance projection and full SH evaluation are shared at corners.
  const float2 corners[4] = {float2(-1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
  out.delta = corners[vertex_id] * extent;
  const float2 position = pixel + out.delta;
  out.position = float4((position.x-camera.raster.x) / camera.raster.z * 2 - 1,
      1 - (position.y-camera.raster.y) / camera.raster.w * 2, 0, 1);
  out.conic = float3(c, -b, a) / determinant;
  // Subtract the base origin first; adding the tiny pupil offset to a large
  // world position would quantize the SH viewing direction as well.
  const float3 displacement = (world_center - camera.position.xyz) - camera.aperture_world.xyz;
  const float3 direction = displacement * rsqrt(max(dot(displacement, displacement), 1e-20));
  const float x = direction.x, y = direction.y, z = direction.z;
  const float xx = x*x, yy = y*y, zz = z*z;
  const float basis[16] = {
    0.2820947918, -0.4886025119*y, 0.4886025119*z, -0.4886025119*x,
    1.0925484306*x*y, -1.0925484306*y*z, 0.3153915653*(3*zz-1), -1.0925484306*x*z, 0.5462742153*(xx-yy),
    -0.5900435899*y*(3*xx-yy), 2.8906114426*x*y*z, -0.4570457995*y*(5*zz-1),
    0.3731763326*z*(5*zz-3), -0.4570457995*x*(5*zz-1), 1.4453057213*z*(xx-yy), -0.5900435899*x*(xx-3*yy)
  };
  float3 color(0.5);
  const uint coefficients = uint(camera.image.z + 1) * uint(camera.image.z + 1);
  for (uint i = 0; i < coefficients; ++i) color += basis[i] * float3(p.sh[3*i], p.sh[3*i+1], p.sh[3*i+2]);
  if (camera.temporal.z > 0) {
    for (uint order_t = 1; order_t <= uint(camera.temporal.z); ++order_t) {
      const float weight = cos(6.283185307179586 * float(order_t) * (dt / camera.temporal.w));
      for (uint i = 0; i < coefficients; ++i) {
        const uint index = (order_t-1)*48 + 3*i;
        color += weight * basis[i] * float3(appearances[point_index].sh[index],
            appearances[point_index].sh[index+1], appearances[point_index].sh[index+2]);
      }
    }
  }
  // SH evaluation can be negative. Clamp each radiance contribution before
  // compositing, not only the finished pixel; negative light must not subtract
  // already accumulated color. Values above one remain valid until output.
  color = max(color,float3(0));
  if (camera.hybrid.y > 0 && (camera.hybrid.y < 2 || any(illumination[point_index].xyz != float3(1)))) color = encode_srgb(decode_srgb(color)*illumination[point_index].xyz);
  out.color = float4(color, opacity);
  out.depth = center.z;
  return out;
}
fragment float4 splat_fragment(Vertex in [[stage_in]], float4 destination [[color(0)]],
    texture2d<float, access::read> solids [[texture(0)]], texture2d<float, access::read> haze [[texture(1)]],texture3d<float> field [[texture(2)]], constant Camera& camera [[buffer(0)]]) {
  // Standard raster splats have one depth per center. Solid silhouettes use
  // exact ray intersections; the Gaussian depth extent is not a surface mesh.
  if (camera.hybrid.x > 0 && in.depth >= solids.read(uint2(in.position.xy)).w) discard_fragment();
  const float exponent = -0.5 * (in.conic.x * in.delta.x * in.delta.x +
      2 * in.conic.y * in.delta.x * in.delta.y + in.conic.z * in.delta.y * in.delta.y);
  const float alpha = min(camera.image.w, in.color.a * exp(exponent));
  if (alpha < 1.0 / 255.0 || exponent > 0) discard_fragment();
  const float next_transmittance = destination.a * (1 - alpha);
  // Same front-to-back termination convention as the training rasterizer.
  if (next_transmittance <= 1e-4) return float4(destination.rgb, 0);
  float3 color = in.color.rgb;
  if (camera.medium.x > 0) {
    const float2 pixel = in.position.xy+camera.raster.xy;
    const float3 ray = normalize(float3((pixel-camera.intrinsics.zw)/camera.intrinsics.xy,1));
    const float4 fog = primary_medium(field,pixel,in.depth/max(ray.z,1e-6),camera);
    color = encode_srgb(fog.rgb+decode_srgb(color)*fog.w);
  } else if (camera.hybrid.z > 0) {
    const float4 fog = haze.read(uint2(in.position.xy));
    const float transmittance = exp(-camera.hybrid.z*in.depth/max(fog.w,1e-6));
    color = encode_srgb(mix(fog.rgb,decode_srgb(color),transmittance));
  }
  return float4(destination.rgb + destination.a * alpha * color, next_transmittance);
}
float3 decode_srgb(float3 encoded) {
  return select(pow((encoded + 0.055) / 1.055, float3(2.4)), encoded / 12.92, encoded <= 0.04045);
}
float3 encode_srgb(float3 linear) {
  return select(1.055*pow(max(linear, 0.0), float3(1.0/2.4))-0.055, 12.92*linear, linear <= 0.0031308);
}
float3 scan_reflection(texture2d_array<float> environment, float3 ray, float roughness) {
  const float3 magnitude = abs(ray); float2 uv; uint face;
  if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
    face = ray.x > 0 ? 0 : 1; uv = float2(ray.x > 0 ? -ray.z : ray.z,ray.y)/magnitude.x;
  } else if (magnitude.y >= magnitude.z) {
    face = ray.y > 0 ? 2 : 3; uv = float2(ray.x,ray.y > 0 ? -ray.z : ray.z)/magnitude.y;
  } else {
    face = ray.z > 0 ? 4 : 5; uv = float2(ray.z > 0 ? ray.x : -ray.x,ray.y)/magnitude.z;
  }
  constexpr sampler filtered(coord::normalized, address::clamp_to_edge, filter::linear, mip_filter::linear);
  const float lod = roughness*float(environment.get_num_mip_levels()-1);
  return environment.sample(filtered,uv*0.5+0.5,face,level(lod)).rgb;
}
kernel void pack_scan_probe(texture2d<float, access::read> source [[texture(0)]],
    texture2d_array<float, access::write> probe [[texture(1)]], constant uint& face [[buffer(0)]],
    uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= source.get_width() || xy.y >= source.get_height()) return;
  // Preserve reconstructed over-white light and filter in linear light. This
  // does not recover clipped capture highlights or calibrate sensor radiance.
  const float3 radiance = min(decode_srgb(max(source.read(xy).rgb,0.0)),65504.0);
  probe.write(float4(radiance,1),xy,face);
}
float3 scan_ray_color(uint id,float3 ray,constant Camera& camera,device const Point* points,
    device const Motion* motions,device const Appearance* appearances,device const float4* illumination) {
  const Point p = points[id]; const float x = ray.x, y = ray.y, z = ray.z;
  const float xx = x*x, yy = y*y, zz = z*z;
  const float basis[16] = {
    0.2820947918, -0.4886025119*y, 0.4886025119*z, -0.4886025119*x,
    1.0925484306*x*y, -1.0925484306*y*z, 0.3153915653*(3*zz-1), -1.0925484306*x*z, 0.5462742153*(xx-yy),
    -0.5900435899*y*(3*xx-yy), 2.8906114426*x*y*z, -0.4570457995*y*(5*zz-1),
    0.3731763326*z*(5*zz-3), -0.4570457995*x*(5*zz-1), 1.4453057213*z*(xx-yy), -0.5900435899*x*(xx-3*yy)
  };
  float3 color(0.5); const uint coefficients = uint(camera.image.z+1)*uint(camera.image.z+1);
  for (uint i = 0; i < coefficients; ++i) color += basis[i]*float3(p.sh[3*i],p.sh[3*i+1],p.sh[3*i+2]);
  if (camera.temporal.z > 0) {
    const float dt = camera.temporal.x-motions[id].velocity_time.w;
    for (uint order_t = 1; order_t <= uint(camera.temporal.z); ++order_t) {
      const float weight = cos(6.283185307179586*float(order_t)*(dt/camera.temporal.w));
      for (uint i = 0; i < coefficients; ++i) {
        const uint offset = (order_t-1)*48+3*i;
        color += weight*basis[i]*float3(appearances[id].sh[offset],appearances[id].sh[offset+1],appearances[id].sh[offset+2]);
      }
    }
  }
  color = max(color,float3(0));
  if (camera.hybrid.y > 0 && (camera.hybrid.y < 2 || any(illumination[id].xyz != float3(1)))) color = encode_srgb(decode_srgb(color)*illumination[id].xyz);
  return color;
}
float3 scan_ray_fog(float3 ray,constant Camera& camera,constant Composition& composition,constant Light* lights) {
  float3 scattering(1); const float g = camera.atmosphere.w;
  for (uint i = 0; i < composition.lights; ++i) {
    const Light light = lights[i]; if (light.vector_kind.w != 0) continue;
    const float cosine = dot(ray,normalize(light.vector_kind.xyz));
    const float phase = (1-g*g)/pow(max(1e-6,1+g*g-2*g*cosine),1.5);
    scattering += 0.05*decode_srgb(light.color_intensity.xyz)*light.color_intensity.w*phase;
  }
  return min(decode_srgb(camera.atmosphere.xyz)*scattering,65504.0);
}
float3 scan_field_radiance_model(float3 origin,float3 ray,float maximum,float3 terminal,bool linear_transport,
    constant Camera& camera,constant Composition& composition,constant Light* lights,
    constant Solid* solids,device const MeshNode* mesh_nodes,device const MeshTriangle* triangles,
    device const MeshNode* nodes,device const ScanParticle* particles,constant ScanVisibility& settings,
    device const Point* points,device const Motion* motions,device const Appearance* appearances,device const float4* illumination SCAN_PATH_PARAMETERS) {
  float3 encoded(0), linear(0); float transmittance = 1, cursor = 0; uint cursor_id = 0, started = 0;
  const float3 fog = camera.hybrid.z > 0 ? scan_ray_fog(ray,camera,composition,lights) : float3(0);
  while (transmittance > settings.termination) {
    const ScanGather batch = scan_gather(origin,ray,maximum,cursor,cursor_id,started,nodes,particles,settings SCAN_RADIANCE_ARGUMENTS);
    if (batch.count == 0) break;
    for (uint j = 0; j < batch.count; ++j) {
      float3 color = scan_ray_color(batch.ids[j],ray,camera,points,motions,appearances,illumination);
      if (camera.hybrid.z > 0) {
        if (camera.medium.x > 0) {
          const float4 volume = medium_integral(origin,ray,batch.distances[j],uint(camera.medium.y),medium_seed(origin,ray),camera,composition,solids,lights,
              mesh_nodes,triangles,nodes,particles,settings SCAN_RT_ARGUMENTS);
          color = encode_srgb(volume.rgb+decode_srgb(color)*volume.w);
        } else color = encode_srgb(mix(fog,decode_srgb(color),exp(-camera.hybrid.z*batch.distances[j])));
      }
      if (linear_transport) linear += transmittance*batch.alphas[j]*decode_srgb(color);
      else encoded += transmittance*batch.alphas[j]*color;
      transmittance *= 1-batch.alphas[j];
      if (transmittance <= settings.termination) { transmittance = 0; break; }
    }
    cursor = batch.distances[batch.count-1]; cursor_id = batch.ids[batch.count-1]; started = 1;
  }
  if (camera.hybrid.z > 0) {
    if (camera.medium.x > 0) {
      const float4 volume = medium_integral(origin,ray,maximum < 1e29 ? maximum : camera.hybrid.w,uint(camera.medium.y),medium_seed(origin,ray),camera,composition,solids,lights,
          mesh_nodes,triangles,nodes,particles,settings SCAN_RT_ARGUMENTS);
      terminal = volume.rgb+(maximum < 1e29 ? terminal*volume.w : float3(0));
    } else if (maximum < 1e29) terminal = mix(fog,terminal,exp(-camera.hybrid.z*maximum));
    else terminal = fog*(1-exp(-camera.hybrid.z*camera.hybrid.w));
  }
  // Retain the captured sRGB opacity-compositing contract, then convert once
  // to linear light for the receiving BRDF, as with the existing probe path.
  return linear_transport ? linear+transmittance*terminal : decode_srgb(encoded+transmittance*encode_srgb(terminal));
}
// Legacy one-surface scenes retain their trained sRGB alpha convention.
float3 scan_field_radiance(float3 origin,float3 ray,float maximum,float3 terminal,
    constant Camera& camera,constant Composition& composition,constant Light* lights,
    constant Solid* solids,device const MeshNode* mesh_nodes,device const MeshTriangle* triangles,
    device const MeshNode* nodes,device const ScanParticle* particles,constant ScanVisibility& settings,
    device const Point* points,device const Motion* motions,device const Appearance* appearances,device const float4* illumination SCAN_PATH_PARAMETERS) {
  return scan_field_radiance_model(origin,ray,maximum,terminal,false,camera,composition,lights,solids,mesh_nodes,triangles,nodes,particles,settings,
      points,motions,appearances,illumination SCAN_PATH_ARGUMENTS);
}
float3 ggx_reflection_ray(float3 normal,float3 view,float roughness,float3 f0,float2 sample,thread float3& weight) {
  const float3 tangent = normalize(cross(abs(normal.z) < 0.999 ? float3(0,0,1) : float3(0,1,0),normal)), bitangent = cross(normal,tangent);
  const float alpha = roughness*roughness;
  const float3 v(dot(view,tangent),dot(view,bitangent),max(1e-6,dot(view,normal)));
  const float3 stretched = normalize(float3(alpha*v.xy,v.z));
  const float lensq = dot(stretched.xy,stretched.xy);
  const float3 t1 = lensq > 0 ? float3(-stretched.y,stretched.x,0)*rsqrt(lensq) : float3(1,0,0), t2 = cross(stretched,t1);
  const float radius = sqrt(sample.x), phi = 2*M_PI_F*sample.y, disk_x = radius*cos(phi);
  const float blend = 0.5*(1+stretched.z), disk_y = mix(sqrt(max(0.0,1-disk_x*disk_x)),radius*sin(phi),blend);
  const float3 disk = disk_x*t1+disk_y*t2+sqrt(max(0.0,1-disk_x*disk_x-disk_y*disk_y))*stretched;
  const float3 local_half = normalize(float3(alpha*disk.xy,max(0.0,disk.z)));
  const float3 half_vector = tangent*local_half.x+bitangent*local_half.y+normal*local_half.z;
  const float3 ray = normalize(reflect(-view,half_vector)); const float nl = dot(normal,ray);
  if (nl <= 0) { weight = 0; return ray; }
  const float nv = max(1e-6,v.z), a2 = alpha*alpha;
  const float lambda_v = 0.5*(sqrt(1+a2*(1-nv*nv)/(nv*nv))-1);
  const float lambda_l = 0.5*(sqrt(1+a2*(1-nl*nl)/(nl*nl))-1);
  const float3 fresnel = f0+(1-f0)*pow(1-max(0.0,dot(view,half_vector)),5.0);
  weight = fresnel*(1+lambda_v)/(1+lambda_v+lambda_l);
  return ray;
}
struct ReflectionTerminal {
  float distance; float3 radiance;
  float3 position, normal, base; float roughness, metallic, epsilon; uint object;
};
ReflectionTerminal reflection_terminal(float3 origin,float3 ray,uint source_object,float source_distance,uint sample,uint total,uint seed,bool include_emitters,bool continue_path,bool camera_primary,
    constant Camera& camera,constant Composition& composition,constant Solid* solids,constant Light* lights,
    device const MeshNode* nodes,device const MeshTriangle* triangles,device const SurfaceMaterial* materials,
    array<texture2d<float>,32> images,texture2d_array<float> environment,
    device const MeshNode* scan_nodes,device const ScanParticle* scan_particles,constant ScanVisibility& scan SCAN_RT_PARAMETERS) {
  ReflectionTerminal result; result.distance = 1e30; result.radiance = 0; result.object = 0xffffffff;
  uint selected = 0xffffffff, triangle_id = 0xffffffff; float2 barycentric(0); float3 normal(0);
  for (uint i = 0; i < composition.objects; ++i) {
    if (i == source_object && solids[i].center_kind.w != 2) continue;
    float3 candidate_normal; uint candidate_triangle; float2 candidate_barycentric;
    const float distance = solid_hit(solids[i],origin-solids[i].center_kind.xyz,ray,candidate_normal,nodes,triangles,candidate_triangle,candidate_barycentric);
    if (distance < result.distance) { result.distance = distance; selected = i; normal = candidate_normal; triangle_id = candidate_triangle; barycentric = candidate_barycentric; }
  }
  uint source = 0xffffffff;
  for (uint i = 0; i < composition.lights; ++i) {
    Light light = lights[i];
    if (camera_primary) { if (light.shape.z <= 0) continue; }
    else light.shape.z = 1; // Camera visibility does not hide a source from reflections.
    float3 candidate_normal;
    const float distance = primary_light_hit(light,origin-light.vector_kind.xyz,ray,candidate_normal);
    if (distance < result.distance) { result.distance = distance; source = i; normal = candidate_normal; }
  }
  if (source != 0xffffffff) {
    const Light light = lights[source];
    result.radiance = include_emitters && dot(normal,-ray) > 0 ? decode_srgb(light.color_intensity.xyz)*light.color_intensity.w : float3(0);
    return result;
  }
  if (selected == 0xffffffff || result.distance >= 1e29) return result;
  const Solid solid = solids[selected]; const float3 position = origin+ray*result.distance, view = -ray;
  float3 base = decode_srgb(solid.color_roughness.xyz), emission = base*solid.material.y;
  float roughness = solid.color_roughness.w, metallic = solid.material.x;
  if (solid.geometry.y > 0 && triangle_id != 0xffffffff) {
    const MeshTriangle triangle = triangles[triangle_id]; const uint slot = uint(triangle.uv2_material.z);
    if (slot < solid.geometry.z) {
      const SurfaceMaterial material = materials[solid.geometry.y-1+slot];
      base *= material.color_roughness.xyz; emission = material.emission_metallic.xyz;
      roughness = material.color_roughness.w; metallic = material.emission_metallic.w;
      if (material.image.y > 0 && triangle.uv2_material.w > 0) {
        const float2 source_uv = triangle_uv(triangle,barycentric);
        const float3 relative = origin-solid.center_kind.xyz;
        const float3 local_origin(dot(relative,solid.axis_x.xyz)*solid.axis_x.w,dot(relative,solid.axis_y.xyz)*solid.axis_y.w,dot(relative,solid.axis_z.xyz)*solid.axis_z.w);
        const float3 tangent = normalize(cross(abs(ray.z) < 0.999 ? float3(0,0,1) : float3(0,1,0),ray)), bitangent = cross(ray,tangent);
        const float angular = (source_distance+result.distance)/max(1e-5,result.distance)/max(1.0,min(camera.intrinsics.x,camera.intrinsics.y));
        const float3 world_dx = ray+tangent*angular, world_dy = ray+bitangent*angular;
        const float3 local_dx(dot(world_dx,solid.axis_x.xyz)*solid.axis_x.w,dot(world_dx,solid.axis_y.xyz)*solid.axis_y.w,dot(world_dx,solid.axis_z.xyz)*solid.axis_z.w);
        const float3 local_dy(dot(world_dy,solid.axis_x.xyz)*solid.axis_x.w,dot(world_dy,solid.axis_y.xyz)*solid.axis_y.w,dot(world_dy,solid.axis_z.xyz)*solid.axis_z.w);
        float2 uv = source_uv*material.uv_transform.xy+material.uv_transform.zw;
        float2 dx = (triangle_uv_ray(triangle,local_origin,local_dx,source_uv)-source_uv)*material.uv_transform.xy;
        float2 dy = (triangle_uv_ray(triangle,local_origin,local_dy,source_uv)-source_uv)*material.uv_transform.xy;
        uv.y = 1-uv.y; dx.y = -dx.y; dy.y = -dy.y;
        constexpr sampler repeat_color(coord::normalized,address::repeat,filter::linear,mip_filter::linear,max_anisotropy(8));
        constexpr sampler clamp_color(coord::normalized,address::clamp_to_edge,filter::linear,mip_filter::linear,max_anisotropy(8));
        const uint image = material.image.x;
        base *= material.image.z > 0 ? images[image].sample(clamp_color,uv,gradient2d(dx,dy)).rgb : images[image].sample(repeat_color,uv,gradient2d(dx,dy)).rgb;
      }
    }
  }
  result.radiance = base*composition.ambient+emission;
  const float3 f0 = mix(float3(0.04),base,metallic); const float nv = max(1e-5,dot(normal,view));
  const float epsilon = max(1e-5,min(1.0/solid.axis_x.w,min(1.0/solid.axis_y.w,1.0/solid.axis_z.w))*1e-4);
  result.position = position; result.normal = normal; result.base = base;
  result.roughness = roughness; result.metallic = metallic; result.epsilon = epsilon; result.object = selected;
  if (composition.capture.z > 0 && composition.ray_modes.y == 0 && metallic < 1 && any(base > 0)) {
    const uint index = light_hash(sample ^ seed ^ 0x165667b1) % total;
    const float2 uv = light_sample_2d(index,total,light_hash(seed+sample*0x85ebca6b));
    const float3 tangent = normalize(cross(abs(normal.z) < 0.999 ? float3(0,0,1) : float3(0,1,0),normal)), bitangent = cross(normal,tangent);
    const float radius = sqrt(uv.x), phi = 2*M_PI_F*uv.y;
    const float3 l = tangent*(radius*cos(phi))+bitangent*(radius*sin(phi))+normal*sqrt(1-uv.x);
    bool blocked = false;
    for (uint j = 0; j < composition.objects; ++j) {
      float3 ignored;
      if (solid_hit(solids[j],position-solids[j].center_kind.xyz+normal*epsilon,l,ignored,nodes,triangles) < 1e29) { blocked = true; break; }
    }
    if (!blocked) {
      const float visibility = composition.capture.w > 0 ? scan_transmittance(position+normal*epsilon,l,1e30,scan_nodes,scan_particles,scan SCAN_RT_ARGUMENTS) : 1.0;
      const float3 fresnel = f0+(1-f0)*pow(1-nv,5.0);
      result.radiance += scan_reflection(environment,l,0)*(1-fresnel)*(1-metallic)*base*composition.capture.z*visibility;
    }
  }
  for (uint i = 0; i < composition.lights; ++i) {
    const Light light = lights[i]; if (light.color_intensity.w <= 0) continue;
    const uint index = light_hash(sample ^ seed ^ light_hash(i+1)) % total;
    const float3 delta = light.vector_kind.w == 0 ? light.vector_kind.xyz : light.vector_kind.xyz-position;
    const LightSample source_sample = sample_light(light,delta,index,total,light_hash(seed+sample*0x9e3779b9+i));
    const float3 l = source_sample.direction; const float nl = max(0.0,dot(normal,l)); if (nl <= 0 || source_sample.weight <= 0) continue;
    bool blocked = false;
    for (uint j = 0; j < composition.objects; ++j) {
      if (j == selected && solids[j].center_kind.w != 2) continue;
      float3 ignored;
      if (solid_hit(solids[j],position-solids[j].center_kind.xyz+normal*epsilon,l,ignored,nodes,triangles) < source_sample.distance-epsilon) { blocked = true; break; }
    }
    if (blocked) continue;
    const float visibility = composition.capture.w > 0 ? scan_transmittance(position+normal*epsilon,l,max(0.0,source_sample.distance-epsilon),scan_nodes,scan_particles,scan SCAN_RT_ARGUMENTS) : 1.0;
    const float3 half_vector = normalize(l+view); const float nh = max(0.0,dot(normal,half_vector)), vh = max(0.0,dot(view,half_vector));
    const float a = roughness*roughness, a2 = a*a, denominator = nh*nh*(a2-1)+1;
    const float distribution = a2/max(1e-10,M_PI_F*denominator*denominator), k = (roughness+1)*(roughness+1)/8;
    const float geometry = nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);
    const float3 fresnel = f0+(1-f0)*pow(1-vh,5.0);
    // Continuing spatial specular paths sample finite source emission. Their
    // direct specular term is partitioned exactly as at the primary surface.
    const float3 specular = continue_path && composition.ray_modes.x > 0 && light.vector_kind.w >= 2 ? float3(0) : distribution*geometry*fresnel/max(4*nv*nl,1e-5);
    const float3 brdf = (1-fresnel)*(1-metallic)*base/M_PI_F+specular;
    result.radiance += brdf*decode_srgb(light.color_intensity.xyz)*light.color_intensity.w*source_sample.weight*nl*visibility;
  }
  // Local shading at a path vertex; continuation is accumulated separately.
  return result;
}
)METAL" + R"METAL(

// Bounded multi-surface paths use linear radiance and alpha transport. Keep
// the chain explicit so every segment gets its own capture and haze integral;
// light-source/lobe weights must never be placed inside a nonlinear encoding.
struct SurfacePathSegment { float3 origin, ray, local, weight; float distance; };
float3 spatial_path_radiance(float3 origin,float3 ray,uint source_object,float source_distance,uint sample,uint total,uint seed,bool include_emitters,
    bool camera_primary,
    constant Camera& camera,constant Composition& composition,constant Solid* solids,constant Light* lights,
    device const MeshNode* nodes,device const MeshTriangle* triangles,device const SurfaceMaterial* materials,
    array<texture2d<float>,32> images,texture2d_array<float> environment,
    device const MeshNode* scan_nodes,device const ScanParticle* scan_particles,constant ScanVisibility& scan,
    device const Point* points,device const Motion* motions,device const Appearance* appearances,device const float4* illumination SCAN_PATH_PARAMETERS) {
  SurfacePathSegment segments[9]; uint count = 0;
  const uint limit = clamp(composition.ray_modes.z+(camera_primary ? 1u : 0u),1u,9u);
  for (uint depth = 0; depth < limit; ++depth) {
    const bool continuing = depth+1 < limit;
    const uint bounce_seed = light_hash(seed ^ (depth+1)*0x9e3779b9);
    const bool primary_segment = camera_primary && depth == 0;
    // Keep direct surface lighting on the camera-visible segment. Later
    // segments partition finite-source specular energy into the GGX path.
    const bool partition_specular = continuing && (!primary_segment || composition.ray_modes.x > 0);
    const ReflectionTerminal hit = reflection_terminal(origin,ray,source_object,source_distance,sample,total,bounce_seed,include_emitters,partition_specular,primary_segment,
        camera,composition,solids,lights,nodes,triangles,materials,images,environment,scan_nodes,scan_particles,scan SCAN_RT_ARGUMENTS);
    segments[depth].origin = origin; segments[depth].ray = ray;
    segments[depth].local = hit.radiance; segments[depth].distance = hit.distance; segments[depth].weight = 0; count = depth+1;
    if (!continuing || hit.object == 0xffffffff) break;
    const float3 view = -ray, f0 = mix(float3(0.04),hit.base,hit.metallic);
    const bool specular = composition.ray_modes.x > 0 && any(f0 > 0);
    const bool diffuse = composition.ray_modes.y > 0 && composition.capture.z > 0 && hit.metallic < 1 && any(hit.base > 0);
    if (!specular && !diffuse) break;
    // Choose one lobe and divide by its selection probability. This keeps the
    // path count linear in depth instead of branching into a ray tree.
    const float nv = max(1e-5,dot(hit.normal,view));
    const float3 fresnel = f0+(1-f0)*pow(1-nv,5.0);
    const float3 diffuse_weight = (1-fresnel)*(1-hit.metallic)*hit.base*composition.capture.z;
    const float specular_energy = max(fresnel.x,max(fresnel.y,fresnel.z));
    const float diffuse_energy = max(diffuse_weight.x,max(diffuse_weight.y,diffuse_weight.z));
    const float specular_probability = specular && diffuse ? clamp(specular_energy/max(1e-8,specular_energy+diffuse_energy),0.05,0.95) : specular ? 1 : 0;
    const bool choose_specular = float(light_hash(sample ^ bounce_seed ^ 0x165667b1))*2.3283064365386963e-10 < specular_probability;
    const float probability = choose_specular ? specular_probability : 1-specular_probability;
    // Every bounce needs a distinct stratification. Merely rotating the
    // azimuth leaves the cosine identical at all vertices and biases products
    // of Fresnel/visibility factors. These reversible operations permute the
    // power-of-two sample domain without dropping or repeating a stratum.
    const uint mask = total-1;
    uint index = (sample ^ bounce_seed) & mask;
    index ^= index >> 4; index = (index*(light_hash(bounce_seed)|1u)) & mask;
    index ^= index >> 5; index = (index*(light_hash(bounce_seed ^ 0x27d4eb2f)|1u)) & mask;
    const float2 uv = light_sample_2d(index,total,bounce_seed ^ 0x85ebca6b);
    float3 weight, next_ray;
    if (choose_specular) next_ray = ggx_reflection_ray(hit.normal,view,hit.roughness,f0,uv,weight);
    else {
      const float3 tangent = normalize(cross(abs(hit.normal.z) < 0.999 ? float3(0,0,1) : float3(0,1,0),hit.normal)), bitangent = cross(hit.normal,tangent);
      const float radius = sqrt(uv.x), phi = 2*M_PI_F*uv.y;
      next_ray = tangent*(radius*cos(phi))+bitangent*(radius*sin(phi))+hit.normal*sqrt(1-uv.x);
      weight = diffuse_weight;
    }
    if (all(weight <= 0) || !all(isfinite(weight)) || !all(isfinite(next_ray))) break;
    segments[depth].weight = weight/probability;
    origin = hit.position+hit.normal*hit.epsilon; ray = next_ray;
    source_object = hit.object; source_distance += hit.distance; include_emitters = choose_specular;
  }
  float3 incoming(0);
  for (uint remaining = count; remaining > 0; --remaining) {
    const SurfacePathSegment segment = segments[remaining-1];
    incoming = scan_field_radiance_model(segment.origin,segment.ray,segment.distance,segment.local+segment.weight*incoming,true,
        camera,composition,lights,solids,nodes,triangles,scan_nodes,scan_particles,scan,points,motions,appearances,illumination SCAN_PATH_ARGUMENTS);
  }
  return incoming;
}

float radical_inverse_base(uint value,uint base) {
  float result = 0, factor = 1.0/float(base);
  while (value > 0) { result += float(value%base)*factor; value /= base; factor /= float(base); }
  return result;
}
// Primary photographic capture traces every pupil sample through the complete
// scene. Gaussian responses are gathered in stable depth/source-ID order and
// linearly composited in front of the nearest solid, material or visible lamp.
kernel void trace_primary_capture(texture2d<float, access::read_write> output [[texture(0)]],
    texture2d_array<float> environment [[texture(1)]],
    array<texture2d<float>,32> color_images [[texture(3)]],
    constant Camera& camera [[buffer(0)]], constant Composition& composition [[buffer(1)]],
    constant Light* lights [[buffer(2)]], constant Solid* solids [[buffer(3)]],
    device const MeshNode* mesh_nodes [[buffer(4)]], device const MeshTriangle* triangles [[buffer(5)]],
    device const SurfaceMaterial* materials [[buffer(6)]],
    device const MeshNode* scan_nodes [[buffer(7)]], device const ScanParticle* scan_particles [[buffer(8)]],
    constant ScanVisibility& scan [[buffer(9)]], device const Point* points [[buffer(10)]],
    device const Motion* motions [[buffer(11)]], device const Appearance* appearances [[buffer(12)]],
    device const float4* illumination [[buffer(13)]], constant PrimaryRayCapture& capture [[buffer(14)]],
    device const float2* pupil_samples [[buffer(15)]],
#if LOOKSY_SCAN_RT
    primitive_acceleration_structure scan_acceleration [[buffer(16)]],
    intersection_function_table<> scan_functions [[buffer(17)]],
    intersection_function_table<> scan_radiance_functions [[buffer(18)]],
#endif
    uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= output.get_width() || xy.y >= output.get_height() || capture.counts.z == 0) return;
  const float2 raster = float2(xy)+camera.raster.xy;
  const uint pixel_seed = light_hash(uint(raster.x)*0x9e3779b9u ^ uint(raster.y)*0x85ebca6bu ^ capture.counts.w);
  const uint sample_count = capture.counts.x;
  const float stretch = sqrt(capture.lens.z);
  const uint symmetry = uint(capture.pupil.y);
  const uint rotation_step = symmetry > 0 ? light_hash(pixel_seed^0x68bc21ebu)%symmetry : 0;
  const float rotation = symmetry > 0 ? 6.283185307179586*float(rotation_step)/float(symmetry)
      : 6.283185307179586*float(light_hash(pixel_seed^0x68bc21ebu))*2.3283064365386963e-10;
  const float cosine = cos(rotation), sine = sin(rotation);
  const float3x3 view_rotation(camera.view[0].xyz,camera.view[1].xyz,camera.view[2].xyz);
  const uint light_total = max(1u,uint(camera.medium.y));
  float3 mean = capture.counts.y == 0 ? float3(0) : output.read(xy).rgb;
  float total_weight = capture.counts.y == 0 ? 0 : output.read(xy).a;
  for (uint batch_sample = 0; batch_sample < capture.counts.z; ++batch_sample) {
    const uint sample_index = capture.counts.y+batch_sample;
    if (sample_index >= sample_count) break;
    const float2 stored_pupil = pupil_samples[sample_index];
    // Rotate in normalized pupil coordinates. Quantized rotations follow the
    // pupil's exact symmetry, so per-pixel scrambling never changes its shape.
    const float2 normalized_pupil(stored_pupil.x/capture.lens.y*stretch,
        stored_pupil.y/capture.lens.y/stretch);
    const float2 rotated_pupil(cosine*normalized_pupil.x-sine*normalized_pupil.y,
        sine*normalized_pupil.x+cosine*normalized_pupil.y);
    const float2 pupil(capture.lens.y*rotated_pupil.x/stretch,capture.lens.y*rotated_pupil.y*stretch);
    const float pupil_radius_squared = dot(normalized_pupil,normalized_pupil);
    const float sample_weight = capture.pupil.w > 0
        ? pow(max(0.0,1-pupil_radius_squared),capture.pupil.w) : 1.0;
    const uint jitter_seed = light_hash(pixel_seed^(sample_index*0x85ebca6bu));
    float2 jitter(radical_inverse_base(sample_index+1,2),radical_inverse_base(sample_index+1,3));
    jitter = fract(jitter+float2(float(light_hash(jitter_seed^0xa511e9b3u)),
        float(light_hash(jitter_seed^0x63d83595u)))*2.3283064365386963e-10);
    const float2 pixel = raster+jitter;
    const float2 field = (pixel-camera.intrinsics.zw)/camera.intrinsics.xy;
    const float distance = capture.lens.x;
    const float field_radius_squared = dot(field,field);
    const float field_weight = field_radius_squared/(1+field_radius_squared);
    const float2 pupil_astigmatism = field_radius_squared > 0
        ? 2*field*dot(field,pupil)/field_radius_squared-pupil : float2(0);
    const float2 chief = field*(1+capture.optics.x*field_weight);
    const float2 aberration = field_weight/distance*((capture.optics.x+capture.optics.z)*pupil+
        capture.optics.y*pupil_astigmatism);
    const float2 focus_field = chief-aberration;
    const float3 camera_ray = normalize(float3(focus_field*distance-pupil,distance));
    const float3 ray = normalize(transpose(view_rotation)*camera_ray);
    const float3 origin = camera.position.xyz+transpose(view_rotation)*float3(pupil,0);
    float3 sample_value = 0;
    if (capture.pupil.z <= 0 || dot(pupil*(1-capture.lens.w/distance)+field*capture.lens.w,
        pupil*(1-capture.lens.w/distance)+field*capture.lens.w) <= (capture.lens.y*capture.pupil.x)*(capture.lens.y*capture.pupil.x)) {
      const uint light_index = (sample_index+pixel_seed%light_total)%light_total;
      if (spatial_rays) {
        sample_value = spatial_path_radiance(origin,ray,0xffffffffu,0,light_index,light_total,
            pixel_seed^0x27d4eb2fu,true,true,camera,composition,solids,lights,mesh_nodes,triangles,materials,
            color_images,environment,scan_nodes,scan_particles,scan,points,motions,appearances,illumination SCAN_PATH_ARGUMENTS);
      } else {
        const ReflectionTerminal terminal = reflection_terminal(origin,ray,0xffffffffu,0,light_index,light_total,
            pixel_seed^0x27d4eb2fu,true,false,true,camera,composition,solids,lights,mesh_nodes,triangles,materials,
            color_images,environment,scan_nodes,scan_particles,scan SCAN_RT_ARGUMENTS);
        sample_value = scan_field_radiance_model(origin,ray,terminal.distance,terminal.radiance,true,
            camera,composition,lights,solids,mesh_nodes,triangles,scan_nodes,scan_particles,scan,points,motions,
            appearances,illumination SCAN_PATH_ARGUMENTS);
      }
    }
    const float next_weight = total_weight+sample_weight;
    if (sample_weight > 0 && next_weight > 0)
      mean += (sample_value-mean)*(sample_weight/next_weight);
    total_weight = next_weight;
  }
  output.write(float4(mean,total_weight),xy);
}

)METAL" + R"METAL(
kernel void render_solids(texture2d<float, access::read_write> output [[texture(0)]],
    texture2d_array<float> environment [[texture(1)]], texture2d<float,access::read> haze [[texture(2)]],
    constant Camera& camera [[buffer(0)]], constant Composition& composition [[buffer(1)]],
    constant Solid* solids [[buffer(2)]], constant Light* lights [[buffer(3)]],
    device const MeshNode* nodes [[buffer(4)]], device const MeshTriangle* triangles [[buffer(5)]], device const SurfaceMaterial* materials [[buffer(6)]],
    array<texture2d<float>,32> color_images [[texture(3)]], constant LightIntegration& integration [[buffer(7)]],
    device const MeshNode* scan_nodes [[buffer(8)]], device const ScanParticle* scan_particles [[buffer(9)]],
    constant ScanVisibility& scan [[buffer(10)]],
#if LOOKSY_SCAN_RT
    primitive_acceleration_structure scan_acceleration [[buffer(11)]], intersection_function_table<> scan_functions [[buffer(12)]],
    intersection_function_table<> scan_radiance_functions [[buffer(13)]],
#endif
    device const Point* scan_points [[buffer(14)]], device const Appearance* scan_appearances [[buffer(15)]],
    device const float4* scan_illumination [[buffer(16)]], device const Motion* scan_motions [[buffer(17)]],
    uint2 xy [[thread_position_in_grid]]) {
  xy.y += integration.pixel_y;
  if (xy.x >= output.get_width() || xy.y >= output.get_height()) return;
  const float2 pixel = float2(xy)+camera.raster.xy+0.5;
  const float3 camera_ray = normalize(float3((pixel-camera.intrinsics.zw)/camera.intrinsics.xy, 1));
  const float3x3 rotation(camera.view[0].xyz, camera.view[1].xyz, camera.view[2].xyz);
  const float3 direction = transpose(rotation)*camera_ray;
  float nearest = 1e30; uint selected = 0, selected_triangle = 0xffffffff; float2 selected_barycentric = 0; float3 normal = 0;
  for (uint i = 0; i < composition.objects; ++i) {
    float3 candidate_normal; uint candidate_triangle; float2 candidate_barycentric;
    const float3 relative = (camera.position.xyz-solids[i].center_kind.xyz)+camera.aperture_world.xyz;
    const float hit = solid_hit(solids[i], relative, direction, candidate_normal,nodes,triangles,candidate_triangle,candidate_barycentric);
    if (hit < nearest && hit*camera_ray.z >= 0.01) { nearest = hit; normal = candidate_normal; selected = i; selected_triangle = candidate_triangle; selected_barycentric = candidate_barycentric; }
  }
  uint selected_light = 0xffffffff;
  for (uint i = 0; i < composition.lights; ++i) {
    float3 candidate_normal;
    const float3 relative = (camera.position.xyz-lights[i].vector_kind.xyz)+camera.aperture_world.xyz;
    const float hit = primary_light_hit(lights[i],relative,direction,candidate_normal);
    if (hit < nearest && hit*camera_ray.z >= 0.01) { nearest = hit; normal = candidate_normal; selected_light = i; }
  }
  if (nearest >= 1e29) { output.write(float4(0,0,0,1e30), xy); return; }
  if (selected_light != 0xffffffff) {
    const Light light = lights[selected_light];
    float3 radiance = dot(normal,-direction) > 0 ? decode_srgb(light.color_intensity.xyz)*light.color_intensity.w : float3(0);
    if (integration.first > 0) radiance = (output.read(xy).rgb*float(integration.first)+radiance*float(integration.count))/float(integration.first+integration.count);
    if (integration.flags > 0) { output.write(float4(radiance,nearest*camera_ray.z),xy); return; }
    if (camera.hybrid.z > 0 && camera.medium.x == 0) radiance = mix(haze.read(xy).rgb,radiance,exp(-camera.hybrid.z*nearest));
    output.write(float4(encode_srgb(radiance),nearest*camera_ray.z),xy); return;
  }
  const Solid solid = solids[selected];
  float3 base = decode_srgb(solid.color_roughness.xyz), emission = base*solid.material.y;
  float roughness = solid.color_roughness.w, metallic = solid.material.x;
  if (solid.geometry.y > 0 && selected_triangle != 0xffffffff) {
    const MeshTriangle triangle = triangles[selected_triangle];
    const uint slot = uint(triangle.uv2_material.z);
    if (slot < solid.geometry.z) {
      const SurfaceMaterial material = materials[solid.geometry.y-1+slot];
      roughness = material.color_roughness.w; metallic = material.emission_metallic.w;
      base *= material.color_roughness.xyz; emission = material.emission_metallic.xyz;
      if (material.image.y > 0 && triangle.uv2_material.w > 0) {
        const float2 source_uv = triangle_uv(triangle,selected_barycentric);
        const float3 relative = (camera.position.xyz-solid.center_kind.xyz)+camera.aperture_world.xyz;
        const float3 origin = float3(dot(relative,solid.axis_x.xyz)*solid.axis_x.w,dot(relative,solid.axis_y.xyz)*solid.axis_y.w,dot(relative,solid.axis_z.xyz)*solid.axis_z.w);
        const float3 world_dx = transpose(rotation)*normalize(float3((pixel+float2(1,0)-camera.intrinsics.zw)/camera.intrinsics.xy,1));
        const float3 world_dy = transpose(rotation)*normalize(float3((pixel+float2(0,1)-camera.intrinsics.zw)/camera.intrinsics.xy,1));
        const float3 ray_dx = float3(dot(world_dx,solid.axis_x.xyz)*solid.axis_x.w,dot(world_dx,solid.axis_y.xyz)*solid.axis_y.w,dot(world_dx,solid.axis_z.xyz)*solid.axis_z.w);
        const float3 ray_dy = float3(dot(world_dy,solid.axis_x.xyz)*solid.axis_x.w,dot(world_dy,solid.axis_y.xyz)*solid.axis_y.w,dot(world_dy,solid.axis_z.xyz)*solid.axis_z.w);
        float2 uv = source_uv*material.uv_transform.xy+material.uv_transform.zw;
        float2 dx = (triangle_uv_ray(triangle,origin,ray_dx,source_uv)-source_uv)*material.uv_transform.xy;
        float2 dy = (triangle_uv_ray(triangle,origin,ray_dy,source_uv)-source_uv)*material.uv_transform.xy;
        uv.y = 1-uv.y; dx.y = -dx.y; dy.y = -dy.y;
        constexpr sampler repeat_color(coord::normalized,address::repeat,filter::linear,mip_filter::linear,max_anisotropy(8));
        constexpr sampler clamp_color(coord::normalized,address::clamp_to_edge,filter::linear,mip_filter::linear,max_anisotropy(8));
        const uint image = material.image.x;
        base *= material.image.z > 0 ? color_images[image].sample(clamp_color,uv,gradient2d(dx,dy)).rgb : color_images[image].sample(repeat_color,uv,gradient2d(dx,dy)).rgb;
      }
    }
  }
  const float3 view = -direction, f0 = mix(float3(0.04), base, metallic);
  const float3 hit_relative = direction*nearest+camera.aperture_world.xyz;
  float3 radiance = base*(composition.ambient*(composition.reflections > 0 ? 1-metallic : 1))+emission;
  const float nv = max(1e-5, dot(normal, view));
  const float3 fresnel = f0+(1-f0)*pow(1-nv,5.0);
  if (composition.reflections > 0) {
    if (spatial_rays && composition.ray_modes.x > 0) {
      const uint seed = uint(pixel.x)*0x9e3779b9 ^ uint(pixel.y)*0x85ebca6b ^ 0x27d4eb2f;
      const float epsilon = max(1e-5,min(1.0/solid.axis_x.w,min(1.0/solid.axis_y.w,1.0/solid.axis_z.w))*1e-4);
      const float3 origin = camera.position.xyz+hit_relative+normal*epsilon; float3 reflected(0);
      for (uint sample = integration.first; sample < integration.first+integration.count; ++sample) {
        float3 weight; const float3 ray = ggx_reflection_ray(normal,view,roughness,f0,light_sample_2d(sample,integration.total,seed),weight);
        if (all(weight <= 0)) continue;
        if (multiple_surface_paths) {
          reflected += weight*spatial_path_radiance(origin,ray,selected,nearest,sample,integration.total,seed,true,false,
              camera,composition,solids,lights,nodes,triangles,materials,color_images,environment,scan_nodes,scan_particles,scan,
              scan_points,scan_motions,scan_appearances,scan_illumination SCAN_PATH_ARGUMENTS);
        } else {
        const ReflectionTerminal terminal = reflection_terminal(origin,ray,selected,nearest,sample,integration.total,seed,true,false,false,
            camera,composition,solids,lights,nodes,triangles,materials,color_images,environment,scan_nodes,scan_particles,scan SCAN_RT_ARGUMENTS);
        reflected += weight*scan_field_radiance(origin,ray,terminal.distance,terminal.radiance,camera,composition,lights,solids,nodes,triangles,scan_nodes,scan_particles,scan,
            scan_points,scan_motions,scan_appearances,scan_illumination SCAN_PATH_ARGUMENTS);
        }
      }
      radiance += reflected/float(integration.count);
    } else radiance += scan_reflection(environment,reflect(direction,normal),roughness)*fresnel;
  }
  const uint pixel_seed = uint(pixel.x)*0x9e3779b9 ^ uint(pixel.y)*0x85ebca6b;
  if (composition.capture.z > 0 && metallic < 1 && any(base > 0)) {
    const float3 tangent = normalize(cross(abs(normal.z) < 0.999 ? float3(0,0,1) : float3(0,1,0),normal));
    const float3 bitangent = cross(normal,tangent);
    const float epsilon = max(1e-5,min(1.0/solid.axis_x.w,min(1.0/solid.axis_y.w,1.0/solid.axis_z.w))*1e-4);
    float3 diffuse = 0;
    for (uint sample = integration.first; sample < integration.first+integration.count; ++sample) {
      const float2 uv = light_sample_2d(sample,integration.total,pixel_seed ^ 0xc2b2ae35);
      const float phi = 2*M_PI_F*uv.y, radius = sqrt(uv.x);
      const float3 l = tangent*(radius*cos(phi))+bitangent*(radius*sin(phi))+normal*sqrt(1-uv.x);
      if (spatial_rays && composition.ray_modes.y > 0) {
        const float3 origin = camera.position.xyz+hit_relative+normal*epsilon;
        // Finite source emission is already light-sampled below. Its surface
        // still occludes captured radiance; authored material emission has no
        // separate light estimator and is sampled by these cosine rays.
        if (multiple_surface_paths) {
          diffuse += spatial_path_radiance(origin,l,selected,nearest,sample,integration.total,pixel_seed ^ 0xc2b2ae35,false,false,
              camera,composition,solids,lights,nodes,triangles,materials,color_images,environment,scan_nodes,scan_particles,scan,
              scan_points,scan_motions,scan_appearances,scan_illumination SCAN_PATH_ARGUMENTS);
        } else {
        const ReflectionTerminal terminal = reflection_terminal(origin,l,selected,nearest,sample,integration.total,pixel_seed ^ 0xc2b2ae35,false,false,false,
            camera,composition,solids,lights,nodes,triangles,materials,color_images,environment,scan_nodes,scan_particles,scan SCAN_RT_ARGUMENTS);
        diffuse += scan_field_radiance(origin,l,terminal.distance,terminal.radiance,camera,composition,lights,solids,nodes,triangles,scan_nodes,scan_particles,scan,
            scan_points,scan_motions,scan_appearances,scan_illumination SCAN_PATH_ARGUMENTS);
        }
        continue;
      }
      bool blocked = false;
      for (uint j = 0; j < composition.objects; ++j) {
        float3 ignored;
        const float3 relative = (camera.position.xyz-solids[j].center_kind.xyz)+hit_relative+normal*epsilon;
        if (solid_hit(solids[j],relative,l,ignored,nodes,triangles) < 1e29) { blocked = true; break; }
      }
      if (!blocked) diffuse += scan_reflection(environment,l,0)*(composition.capture.w > 0 ? scan_transmittance(camera.position.xyz+hit_relative+normal*epsilon,l,1e30,scan_nodes,scan_particles,scan SCAN_RT_ARGUMENTS) : 1.0);
    }
    // Cosine-weighted PDF cancels Lambert's cosine/pi. Keep the captured light
    // in linear radiance and reserve reflected energy for the Fresnel lobe.
    radiance += diffuse/float(integration.count)*(1-fresnel)*(1-metallic)*base*composition.capture.z;
  }
  for (uint i = 0; i < composition.lights; ++i) {
    const Light light = lights[i]; if (light.color_intensity.w <= 0) continue;
    const float3 delta = light.vector_kind.w == 0 ? light.vector_kind.xyz : (light.vector_kind.xyz-camera.position.xyz)-hit_relative;
    const bool finite = finite_light(light); const uint samples = finite ? integration.count : 1;
    float3 contribution(0);
    for (uint sample = 0; sample < samples; ++sample) {
      const LightSample source = sample_light(light,delta,finite ? integration.first+sample : 0,finite ? integration.total : 1,pixel_seed^light_hash(i+1));
      const float3 l = source.direction; const float nl = max(0.0,dot(normal,l)); if (nl <= 0 || source.weight <= 0) continue;
      const float epsilon = max(1e-5, min(1.0/solid.axis_x.w, min(1.0/solid.axis_y.w,1.0/solid.axis_z.w))*1e-4);
      bool shadow = false;
      for (uint j = 0; j < composition.objects; ++j) {
        if (j == selected && solids[j].center_kind.w != 2) continue;
        float3 ignored;
        const float3 relative = (camera.position.xyz-solids[j].center_kind.xyz)+hit_relative+normal*epsilon;
        const float hit = solid_hit(solids[j],relative,l,ignored,nodes,triangles);
        if (hit < source.distance-epsilon) { shadow = true; break; }
      }
      if (shadow) continue;
      const float visibility = composition.capture.w > 0 ? scan_transmittance(camera.position.xyz+hit_relative+normal*epsilon,l,max(0.0,source.distance-epsilon),scan_nodes,scan_particles,scan SCAN_RT_ARGUMENTS) : 1.0;
      if (visibility <= 0) continue;
    const float3 h = normalize(l+view);
    const float nh = max(0.0,dot(normal,h)), vh = max(0.0,dot(view,h));
    const float a = roughness*roughness, a2 = a*a, denominator = nh*nh*(a2-1)+1;
    const float d = a2/max(1e-10, M_PI_F*denominator*denominator);
    const float k = (roughness+1)*(roughness+1)/8;
    const float g = nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);
    const float3 f = f0+(1-f0)*pow(1-vh,5.0);
    // Spatial BSDF rays already sample finite-source specular emission. Retain
    // light-sampled diffuse and delta-light specular without double counting.
    const float3 specular = spatial_rays && composition.ray_modes.x > 0 && light.vector_kind.w >= 2 ? float3(0) : d*g*f/max(4*nv*nl,1e-5);
    const float3 brdf = (1-f)*(1-metallic)*base/M_PI_F + specular;
      contribution += brdf*decode_srgb(light.color_intensity.xyz)*light.color_intensity.w*source.weight*nl*visibility;
    }
    radiance += contribution/float(samples);
  }
  if (integration.first > 0) radiance = (output.read(xy).rgb*float(integration.first)+radiance*float(integration.count))/float(integration.first+integration.count);
  if (integration.flags > 0) { output.write(float4(radiance,nearest*camera_ray.z),xy); return; }
  // Match the existing display-referred splat compositing domain. Highlights
  // remain unclipped until the shared exposure/film/output conversion.
  if (camera.hybrid.z > 0 && camera.medium.x == 0) radiance = mix(haze.read(xy).rgb,radiance,exp(-camera.hybrid.z*nearest));
  output.write(float4(encode_srgb(radiance),nearest*camera_ray.z),xy);
}
kernel void finish_soft_solids(texture2d<float,access::read_write> surface [[texture(0)]], texture2d<float,access::read> haze [[texture(1)]],
    constant Camera& camera [[buffer(0)]], uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= surface.get_width() || xy.y >= surface.get_height()) return;
  const float4 hit = surface.read(xy); if (hit.w >= 1e29) return;
  float3 radiance = hit.rgb;
  if (camera.hybrid.z > 0 && camera.medium.x == 0) { const float4 fog = haze.read(xy); radiance = mix(fog.rgb,radiance,exp(-camera.hybrid.z*hit.w/max(fog.w,1e-6))); }
  surface.write(float4(encode_srgb(radiance),hit.w),xy);
}
kernel void composite_solids(texture2d<float, access::read_write> accumulation [[texture(0)]],
    texture2d<float, access::read> solids [[texture(1)]], uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= accumulation.get_width() || xy.y >= accumulation.get_height()) return;
  const float4 surface = solids.read(xy);
  if (surface.w >= 1e29) return;
  const float4 front = accumulation.read(xy);
  accumulation.write(float4(front.rgb+front.a*surface.rgb,0),xy);
}
struct Pupil { float4 point; float4 intrinsics; float4 raster; float4 optics; float4 aperture_sample; float4 softness; };
float3 sample_ray_linear(texture2d<float, access::read> source, float2 p) {
  const float2 q = clamp(p,0.0,float2(source.get_width()-1,source.get_height()-1));
  const uint2 a = uint2(floor(q)), b = min(a+1,uint2(source.get_width()-1,source.get_height()-1));
  const float2 f = fract(q);
  return mix(mix(decode_srgb(max(source.read(a).rgb,0.0)),decode_srgb(max(source.read(uint2(b.x,a.y)).rgb,0.0)),f.x),
      mix(decode_srgb(max(source.read(uint2(a.x,b.y)).rgb,0.0)),decode_srgb(max(source.read(b).rgb,0.0)),f.x),f.y);
}
kernel void integrate_aperture(texture2d<float, access::read> sample [[texture(0)]],
    texture2d<float, access::read_write> mean [[texture(1)]], constant uint& index [[buffer(0)]],
    constant Pupil& pupil [[buffer(1)]],
    uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= mean.get_width() || xy.y >= mean.get_height()) return;
  const float2 field = (float2(xy)+pupil.raster.xy+0.5-pupil.intrinsics.zw)/pupil.intrinsics.xy;
  const bool mapped = any(pupil.optics.xyz != 0);
  float2 ray = field-pupil.aperture_sample.xy*pupil.optics.w;
  float3 rgb;
  if (mapped) {
    const float r2 = dot(field,field), weight = r2/(1+r2);
    const float2 p = pupil.aperture_sample.xy;
    const float2 astig = r2 > 0 ? 2*field*dot(field,p)/r2-p : float2(0);
    const float2 extra = weight*pupil.optics.w*((pupil.optics.x+pupil.optics.z)*p+pupil.optics.y*astig);
    const float2 chief = field*(1+pupil.optics.x*weight);
    ray = chief-p*pupil.optics.w-extra;
    const float2 source_pixel = pupil.intrinsics.zw+pupil.intrinsics.xy*(chief-extra);
    rgb = sample_ray_linear(sample,source_pixel-pupil.aperture_sample.zw-0.5);
  } else rgb = decode_srgb(max(sample.read(xy).rgb,0.0));
  if (pupil.raster.w > 0) {
    const float2 barrel = mapped ? pupil.point.xy+pupil.point.z*ray : pupil.point.xy*pupil.raster.z+pupil.point.z*field;
    if (dot(barrel,barrel) > pupil.point.w*pupil.point.w) rgb = 0;
  }
  const float2 normalized_pupil(pupil.point.x*sqrt(pupil.softness.y),pupil.point.y/sqrt(pupil.softness.y));
  const float radius_squared = dot(normalized_pupil,normalized_pupil);
  const float sample_weight = pupil.softness.x > 0 ? pow(max(0.0,1-radius_squared),pupil.softness.x) : 1.0;
  const float4 previous = index == 0 ? float4(0) : mean.read(xy);
  const float next_weight = previous.a+sample_weight;
  const float3 value = sample_weight > 0 && next_weight > 0
      ? (previous.a > 0 ? previous.rgb+(rgb-previous.rgb)*(sample_weight/next_weight) : rgb) : previous.rgb;
  mean.write(float4(value,next_weight), xy);
}
struct Film {
  float grain_amount, grain_size, halation_amount, halation_radius; float threshold; uint seed; float padding[2];
  float4 canvas; float4 plane; float4 returning; float4 modes[128];
};
// Zero extension outside the image: do not replicate bright border emitters.
kernel void halation_horizontal(texture2d<float, access::read> source [[texture(0)]],
    texture2d<float, access::write> glow [[texture(1)]], constant float4& photo [[buffer(0)]],
    constant Film& film [[buffer(1)]], uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= glow.get_width() || xy.y >= glow.get_height()) return;
  const float sigma = max(0.25,film.plane.z>0 ? film.returning.w/film.plane.x :
      film.halation_radius * max(film.canvas.x, film.canvas.y) / 2048.0);
  const int radius = int(ceil(3 * sigma));
  float3 sum = 0; float weight = 0;
  for (int i = -radius; i <= radius; ++i) {
    const float w = exp(-0.5 * float(i*i) / (sigma*sigma));
    weight += w;
    const int x = int(xy.x) + i;
    if (x < 0 || x >= int(source.get_width())) continue;
    float3 rgb = max(source.read(uint2(uint(x), xy.y)).rgb, 0.0);
    rgb = (photo.z != 0 ? rgb : decode_srgb(rgb)) * exp2(photo.x);
    if (film.plane.z>0 && film.padding[0]>0) {
      const float luminance = dot(rgb,float3(0.2126,0.7152,0.0722));
      rgb *= max(0.0,luminance-film.threshold)/max(luminance,1e-10);
    }
    sum += w * (film.plane.z>0 ? rgb : float3(max(0.0,dot(rgb,float3(0.2126,0.7152,0.0722))-film.threshold)));
  }
  glow.write(float4(sum / weight,1), xy);
}
// Camera post effects operate on resolved, exposed linear light before the
// display shoulder and quantization. Highlights retain their RGB color.
struct CameraPost { float4 bloom_glare; float4 glare; float4 color; };
float3 exposed_source(texture2d<float, access::read> source, uint2 xy, float4 photo) {
  const float3 c = max(source.read(xy).rgb, 0.0);
  return (photo.z != 0 ? c : decode_srgb(c)) * exp2(photo.x);
}
float3 film_linear(texture2d<float, access::read> source, texture2d<float, access::read> glow,
    uint2 xy, float4 photo, constant Film& film) {
  float3 rgb = exposed_source(source, xy, photo);
  if (film.halation_amount > 0) {
    const float sigma = max(0.25,film.plane.z>0 ? film.returning.w/film.plane.y :
        film.halation_radius * max(film.canvas.x, film.canvas.y) / 2048.0);
    const int radius = int(ceil(3 * sigma));
    float3 sum = 0; float weight = 0;
    for (int i = -radius; i <= radius; ++i) {
      const float w = exp(-0.5 * float(i*i) / (sigma*sigma)); weight += w;
      const int y = int(xy.y) + i;
      if (y >= 0 && y < int(glow.get_height())) {
        const float3 value=glow.read(uint2(xy.x,uint(y))).rgb;
        sum+=w*(film.plane.z>0 ? value : float3(value.r));
      }
    }
    rgb += film.halation_amount * sum / weight * (film.plane.z>0 ? film.returning.rgb : float3(1.0,0.15,0.025));
  }
  return rgb;
}
kernel void camera_highlights(texture2d<float, access::read> source [[texture(0)]],
    texture2d<float, access::write> highlights [[texture(1)]], constant float4& photo [[buffer(0)]],
    constant CameraPost& post [[buffer(1)]], uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= source.get_width() || xy.y >= source.get_height()) return;
  const float3 rgb = exposed_source(source,xy,photo);
  const float l = dot(rgb,float3(0.2126,0.7152,0.0722));
  highlights.write(float4(rgb * max(0.0,l-post.color.x)/max(l,1e-10),1),xy);
}
kernel void camera_bloom_horizontal(texture2d<float, access::read> highlights [[texture(0)]],
    texture2d<float, access::write> horizontal [[texture(1)]], constant CameraPost& post [[buffer(0)]],
    constant Film& film [[buffer(1)]], uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= highlights.get_width() || xy.y >= highlights.get_height()) return;
  const float sigma = max(0.25,post.bloom_glare.y*max(film.canvas.x,film.canvas.y)/2048.0);
  const int radius = int(ceil(3*sigma));
  float3 sum = 0; float norm = 0;
  for (int i = -radius; i <= radius; ++i) {
    const float w = exp(-0.5*float(i*i)/(sigma*sigma)); norm += w;
    const int x = int(xy.x)+i;
    if (x >= 0 && x < int(highlights.get_width())) sum += w*highlights.read(uint2(uint(x),xy.y)).rgb;
  }
  horizontal.write(float4(sum/norm,1),xy);
}
kernel void camera_post(texture2d<float, access::read> source [[texture(0)]],
    texture2d<float, access::read> glow [[texture(1)]],
    texture2d<float, access::sample> highlights [[texture(2)]],
    texture2d<float, access::read> bloom_horizontal [[texture(3)]],
    texture2d<float, access::write> output [[texture(4)]], constant float4& photo [[buffer(0)]],
    constant Film& film [[buffer(1)]], constant CameraPost& post [[buffer(2)]],
    uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= output.get_width() || xy.y >= output.get_height()) return;
  float3 rgb = film_linear(source,glow,xy,photo,film);
  const float scale = max(film.canvas.x,film.canvas.y)/2048.0;
  if (post.bloom_glare.x > 0) {
    const float sigma = max(0.25,post.bloom_glare.y*scale);
    const int radius = int(ceil(3*sigma)); float3 sum = 0; float norm = 0;
    for (int i = -radius; i <= radius; ++i) {
      const float w = exp(-0.5*float(i*i)/(sigma*sigma)); norm += w;
      const int y = int(xy.y)+i;
      if (y >= 0 && y < int(bloom_horizontal.get_height())) sum += w*bloom_horizontal.read(uint2(xy.x,uint(y))).rgb;
    }
    rgb += post.bloom_glare.x*sum/norm;
  }
  if (post.bloom_glare.z > 0) {
    // Unit-pixel quadrature avoids gaps around small emitters. Normalize each
    // color and all rays together so ray count/spectral intensity retain energy.
    constexpr sampler s(coord::pixel,address::clamp_to_zero,filter::linear);
    const float length = max(1.0,post.bloom_glare.w*scale);
    const int radius = int(ceil(length)), rays = int(post.glare.x);
    float3 sum = 0, norm = 0;
    for (int ray = 0; ray < rays; ++ray) {
      const float angle = post.glare.y + 3.141592653589793*float(ray)/float(rays);
      const float2 axis = float2(cos(angle),sin(angle));
      for (int i = -radius; i <= radius; ++i) {
        const float t = abs(float(i))/length;
        const float3 wave = 0.5+0.5*cos(6.283185307179586*(t*1.5-float3(0,0.3333333333,0.6666666667)));
        const float3 w = exp(-3*t)*mix(float3(1),0.1+wave*wave,post.glare.z);
        norm += w;
        sum += w*highlights.sample(s,float2(xy)+0.5+float(i)*axis).rgb;
      }
    }
    rgb += post.bloom_glare.z*sum/norm;
  }
  output.write(float4(rgb,1),xy);
}
// Bilinear interpolation of the already resolved linear image. Clamp at the
// actual canvas border; tiled renders supply enough overlap for every channel.
float3 post_bilinear(texture2d<float, access::read> source, float2 p) {
  const float2 q = clamp(p,0.0,float2(source.get_width()-1,source.get_height()-1));
  const uint2 a = uint2(floor(q)), b = min(a+1,uint2(source.get_width()-1,source.get_height()-1));
  const float2 f = fract(q);
  return mix(mix(source.read(a).rgb,source.read(uint2(b.x,a.y)).rgb,f.x),
      mix(source.read(uint2(a.x,b.y)).rgb,source.read(b).rgb,f.x),f.y);
}
uint grain_hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }
float grain_cell(uint2 p, uint seed) { return float(grain_hash(p.x ^ grain_hash(p.y ^ seed)) & 0xffffffu) / 8388607.5 - 1.0; }
float grain_noise(float2 p, uint seed) {
  const uint2 cell = uint2(floor(p));
  const float2 f = fract(p), t = f*f*(3.0-2.0*f);
  return mix(mix(grain_cell(cell, seed), grain_cell(cell+uint2(1,0), seed), t.x),
      mix(grain_cell(cell+uint2(0,1), seed), grain_cell(cell+uint2(1,1), seed), t.x), t.y);
}
float3 film_density_grain(float3 rgb,uint2 xy,constant Film& film) {
  // A fixed continuous field in microns, analytically averaged over this pixel.
  // Shot exposure determines density granularity; it is not render-sample noise.
  const float2 p=(float2(xy)+film.canvas.zw+0.5-film.canvas.xy*0.5)*film.plane.xy;
  float noise=0;
  for (uint i=0;i<128;++i) noise+=film.modes[i].w*sin(dot(film.modes[i].xy,p)+film.modes[i].z);
  const float exposure=dot(rgb,float3(0.2126,0.7152,0.0722));
  const float occupancy=exposure/(1+exposure);
  const float sigma=2*0.08*film.grain_amount*sqrt(occupancy*(1-occupancy))*2.302585093;
  // Gaussian density approximation: compensate the log-normal mean in linear
  // exposure. Spatial averaging reduces variance, never renormalize it away.
  return rgb*exp(sigma*noise-0.5*sigma*sigma*film.plane.w);
}
kernel void encode_output(texture2d<float, access::read> linear [[texture(0)]],
    texture2d<float, access::write> output [[texture(1)]], constant float4& photo [[buffer(0)]],
    constant Film& film [[buffer(1)]], texture2d<float, access::read> glow [[texture(2)]],
    constant CameraPost& post [[buffer(2)]], constant float4& tone [[buffer(3)]],
    constant float4& grade [[buffer(4)]], uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= output.get_width() || xy.y >= output.get_height()) return;
  // Neutral is an exact bypass, preserving the training renderer's output.
  float3 encoded = max(linear.read(xy).rgb, 0.0);
  bool color_changed = photo.x != 0 || photo.y != 0 || photo.z != 0 || post.color.z != 1 || tone.x != 1 || tone.y != 0 || any(grade != 0);
  if (color_changed || film.halation_amount > 0 || (film.plane.z>0 && film.grain_amount>0)) {
    float3 rgb = film_linear(linear,glow,xy,photo,film);
    if (post.color.y > 0) {
      const float2 delta = (float2(xy)+film.canvas.zw+0.5-film.canvas.xy*0.5)*post.color.y/1024.0;
      rgb.r = post_bilinear(linear,float2(xy)+delta).r;
      rgb.b = post_bilinear(linear,float2(xy)-delta).b;
    }
    color_changed = color_changed || any(rgb != exposed_source(linear,xy,photo));
    if (film.plane.z>0 && film.grain_amount>0) {
      rgb=film_density_grain(rgb,xy,film); color_changed=true;
    }
    // Creative white balance relative to the captured color. Neutral is exact;
    // warmth/tint are not claims about the source camera's illuminant Kelvin.
    if (grade.x != 0 || grade.y != 0) {
      const float3 gain = exp2(float3(0.4*grade.x+0.2*grade.y,-0.2*grade.y,-0.4*grade.x+0.2*grade.y));
      rgb *= gain/dot(gain,float3(0.2126,0.7152,0.0722));
    }
    const float gray = dot(rgb,float3(0.2126,0.7152,0.0722));
    if (grade.z != 0) {
      const float peak = max(rgb.r,max(rgb.g,rgb.b)), low = min(rgb.r,min(rgb.g,rgb.b));
      const float saturation = peak > 0 ? (peak-low)/peak : 0;
      rgb = max(mix(float3(gray),rgb,1+grade.z*(1-saturation)),0.0);
    }
    if (post.color.z != 1) rgb = max(mix(float3(gray),rgb,post.color.z),0.0);
    float luminance = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    if (photo.y == 2) rgb = float3(luminance);
    if (photo.y != 0) {
      // Generic highlight shoulder, not a measured film stock. Scale RGB
      // together to preserve chromaticity; use max RGB to avoid channel clipping.
      const float peak = max(rgb.r, max(rgb.g, rgb.b));
      if (peak > 0.6) rgb *= (0.6 + 0.4 * (1 - exp(-(peak - 0.6) / 0.4))) / peak;
    }
    // Apply once after optical/light/post integration and the creative look.
    // Scale all channels together to preserve RGB ratios, including HDR colors.
    const float tone_luminance = dot(rgb,float3(0.2126,0.7152,0.0722));
    if (tone.x != 1 && tone_luminance > 0) {
      rgb *= (0.18*pow(tone_luminance/0.18,tone.x))/tone_luminance;
    }
    const float peak = max(rgb.r,max(rgb.g,rgb.b));
    if (tone.y > 0) {
      const float headroom = 0.8*tone.y, knee = 1-headroom;
      if (peak > knee) rgb *= (knee+headroom*(1-exp(-(peak-knee)/headroom)))/peak;
    }
    if (grade.w > 0) rgb = mix(rgb,float3(1),grade.w*pow(clamp(1-dot(rgb,float3(0.2126,0.7152,0.0722)),0.0,1.0),2.0));
    if (color_changed) encoded = select(1.055 * pow(rgb, float3(1.0 / 2.4)) - 0.055,
        12.92 * rgb, rgb <= 0.0031308);
  }
  encoded = clamp(encoded, 0.0, 1.0);
  if (film.grain_amount > 0 && film.plane.z==0) {
    const float pixel_size = film.grain_size * max(film.canvas.x, film.canvas.y) / 2048.0;
    // Keep the noise lattice at least one output pixel wide; attenuate unresolved
    // grain in small previews. Seed is fixed per saved shot, never wall-clock time.
    const float noise = grain_noise((float2(xy)+film.canvas.zw+0.5) / max(1.0, pixel_size), film.seed);
    encoded += film.grain_amount * 0.12 * min(1.0, pixel_size) * noise * sqrt(encoded*(1.0-encoded));
  }
  output.write(float4(clamp(encoded, 0.0, 1.0), 1), xy);
}
)METAL";

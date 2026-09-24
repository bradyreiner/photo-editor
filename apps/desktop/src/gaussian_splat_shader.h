#pragma once
// Standard real SH basis through l=3; COLMAP +Z forward, +Y down. The 0.3
// pixel covariance floor matches the selected Brush training convention.
inline constexpr const char* gaussian_splat_msl = R"METAL(
#include <metal_stdlib>
using namespace metal;
struct Point { float4 center_opacity; float4 a; float4 b; float sh[48]; };
struct Motion { float4 velocity_time; float4 normalization; float4 factor0; float4 factor1; float4 factor2; };
struct Appearance { float sh[96]; };
struct Camera { float4x4 view; float4 intrinsics; float4 image; float4 position; float4 temporal; float4 raster; float4 aperture; float4 aperture_world; };
struct Vertex { float4 position [[position]]; float2 delta; float3 conic [[flat]]; float4 color [[flat]]; };
vertex Vertex splat_vertex(uint vertex_id [[vertex_id]], uint instance [[instance_id]],
    device const Point* points [[buffer(0)]], device const uint* order [[buffer(1)]],
    constant Camera& camera [[buffer(2)]], device const Motion* motions [[buffer(3)]],
    device const Appearance* appearances [[buffer(4)]]) {
  Vertex out;
  out.position = float4(2, 2, 2, 1); out.delta = 0; out.conic = 0; out.color = 0;
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
  if (any(pixel + extent < 0) || any(pixel - extent > camera.image.xy)) return out;
  const float2 corners[6] = {float2(-1,-1),float2(1,-1),float2(-1,1),float2(-1,1),float2(1,-1),float2(1,1)};
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
  out.color = float4(max(color, float3(0)), opacity);
  return out;
}
fragment float4 splat_fragment(Vertex in [[stage_in]], float4 destination [[color(0)]]) {
  const float exponent = -0.5 * (in.conic.x * in.delta.x * in.delta.x +
      2 * in.conic.y * in.delta.x * in.delta.y + in.conic.z * in.delta.y * in.delta.y);
  const float alpha = min(0.999, in.color.a * exp(exponent));
  if (alpha < 1.0 / 255.0 || exponent > 0) discard_fragment();
  const float next_transmittance = destination.a * (1 - alpha);
  // Same front-to-back termination convention as the training rasterizer.
  if (next_transmittance <= 1e-4) return float4(destination.rgb, 0);
  return float4(destination.rgb + destination.a * alpha * in.color.rgb, next_transmittance);
}
float3 decode_srgb(float3 encoded) {
  return select(pow((encoded + 0.055) / 1.055, float3(2.4)), encoded / 12.92, encoded <= 0.04045);
}
kernel void integrate_aperture(texture2d<float, access::read> sample [[texture(0)]],
    texture2d<float, access::read_write> mean [[texture(1)]], constant uint& index [[buffer(0)]],
    uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= mean.get_width() || xy.y >= mean.get_height()) return;
  const float3 rgb = decode_srgb(max(sample.read(xy).rgb, 0.0));
  const float3 value = index == 0 ? rgb : mean.read(xy).rgb + (rgb - mean.read(xy).rgb) / float(index + 1);
  mean.write(float4(value, 1), xy);
}
struct Film { float grain_amount, grain_size, halation_amount, halation_radius; float threshold; uint seed; float padding[2]; float4 canvas; };
// Zero extension outside the image: do not replicate bright border emitters.
kernel void halation_horizontal(texture2d<float, access::read> source [[texture(0)]],
    texture2d<float, access::write> glow [[texture(1)]], constant float4& photo [[buffer(0)]],
    constant Film& film [[buffer(1)]], uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= glow.get_width() || xy.y >= glow.get_height()) return;
  const float sigma = max(0.25, film.halation_radius * max(film.canvas.x, film.canvas.y) / 2048.0);
  const int radius = int(ceil(3 * sigma));
  float sum = 0, weight = 0;
  for (int i = -radius; i <= radius; ++i) {
    const float w = exp(-0.5 * float(i*i) / (sigma*sigma));
    weight += w;
    const int x = int(xy.x) + i;
    if (x < 0 || x >= int(source.get_width())) continue;
    float3 rgb = max(source.read(uint2(uint(x), xy.y)).rgb, 0.0);
    rgb = (photo.z != 0 ? rgb : decode_srgb(rgb)) * exp2(photo.x);
    sum += w * max(0.0, dot(rgb, float3(0.2126, 0.7152, 0.0722)) - film.threshold);
  }
  glow.write(float4(sum / weight), xy);
}
uint grain_hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }
float grain_cell(uint2 p, uint seed) { return float(grain_hash(p.x ^ grain_hash(p.y ^ seed)) & 0xffffffu) / 8388607.5 - 1.0; }
float grain_noise(float2 p, uint seed) {
  const uint2 cell = uint2(floor(p));
  const float2 f = fract(p), t = f*f*(3.0-2.0*f);
  return mix(mix(grain_cell(cell, seed), grain_cell(cell+uint2(1,0), seed), t.x),
      mix(grain_cell(cell+uint2(0,1), seed), grain_cell(cell+uint2(1,1), seed), t.x), t.y);
}
kernel void encode_output(texture2d<float, access::read> linear [[texture(0)]],
    texture2d<float, access::write> output [[texture(1)]], constant float4& photo [[buffer(0)]],
    constant Film& film [[buffer(1)]], texture2d<float, access::read> glow [[texture(2)]],
    uint2 xy [[thread_position_in_grid]]) {
  if (xy.x >= output.get_width() || xy.y >= output.get_height()) return;
  // Neutral is an exact bypass, preserving the training renderer's output.
  float3 encoded = max(linear.read(xy).rgb, 0.0);
  bool color_changed = photo.x != 0 || photo.y != 0 || photo.z != 0;
  if (photo.x != 0 || photo.y != 0 || photo.z != 0 || film.halation_amount > 0) {
    // Decode AFTER compositing: SHs were trained in display-referred sRGB.
    // Linearized exposure cannot recover clipped source highlights or sensor light.
    float3 rgb = (photo.z != 0 ? encoded : decode_srgb(encoded)) * exp2(photo.x);
    if (film.halation_amount > 0) {
      const float sigma = max(0.25, film.halation_radius * max(film.canvas.x, film.canvas.y) / 2048.0);
      const int radius = int(ceil(3 * sigma));
      float sum = 0, weight = 0;
      for (int i = -radius; i <= radius; ++i) {
        const float w = exp(-0.5 * float(i*i) / (sigma*sigma));
        weight += w;
        const int y = int(xy.y) + i;
        if (y >= 0 && y < int(glow.get_height())) sum += w * glow.read(uint2(xy.x, uint(y))).r;
      }
      rgb += film.halation_amount * sum / weight * float3(1.0, 0.15, 0.025);
      color_changed = color_changed || sum > 0;
    }
    float luminance = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    if (photo.y == 2) rgb = float3(luminance);
    if (photo.y != 0) {
      // Generic highlight shoulder, not a measured film stock. Scale RGB
      // together to preserve chromaticity; use max RGB to avoid channel clipping.
      const float peak = max(rgb.r, max(rgb.g, rgb.b));
      if (peak > 0.6) rgb *= (0.6 + 0.4 * (1 - exp(-(peak - 0.6) / 0.4))) / peak;
    }
    if (color_changed) encoded = select(1.055 * pow(rgb, float3(1.0 / 2.4)) - 0.055,
        12.92 * rgb, rgb <= 0.0031308);
  }
  encoded = clamp(encoded, 0.0, 1.0);
  if (film.grain_amount > 0) {
    const float pixel_size = film.grain_size * max(film.canvas.x, film.canvas.y) / 2048.0;
    // Keep the noise lattice at least one output pixel wide; attenuate unresolved
    // grain in small previews. Seed is fixed per saved shot, never wall-clock time.
    const float noise = grain_noise((float2(xy)+film.canvas.zw+0.5) / max(1.0, pixel_size), film.seed);
    encoded += film.grain_amount * 0.12 * min(1.0, pixel_size) * noise * sqrt(encoded*(1.0-encoded));
  }
  output.write(float4(clamp(encoded, 0.0, 1.0), 1), xy);
}
)METAL";

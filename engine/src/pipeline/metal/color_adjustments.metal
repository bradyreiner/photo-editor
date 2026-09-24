#include <metal_stdlib>
using namespace metal;

struct ColorAdjustmentUniforms {
  float exposure_gain;
  float wb_red;
  float wb_green;
  float wb_blue;
  float contrast;
  float highlights;
  float shadows;
  float whites;
  float blacks;
  float curve_shadows;
  float curve_darks;
  float curve_lights;
  float curve_highlights;
  float saturation_strength;
  float vibrance_strength;
  float dehaze_sat_contrib;
  float hue_adjustment;
  float saturation_adjustment;
  float luminance_adjustment;
  int lut_size;
  int width;
  int height;
};

struct SceneExposureUniforms {
  float gain;
  uint pixel_count;
};

struct ToneStageUniforms {
  float luminance_red;
  float luminance_green;
  float luminance_blue;
  float minimum_stops;
  float stop_step;
  float middle_gray;
  float output_headroom;
  uint sample_count;
  uint pixel_count;
};

struct CurveStageUniforms {
  float luminance_red;
  float luminance_green;
  float luminance_blue;
  float output_headroom;
  uint sample_count;
  uint pixel_count;
  uint active_mask;
  uint padding;
};

struct LookStageUniforms {
  float rgb_to_xyz_d65[9];
  float xyz_d65_to_rgb[9];
  float luminance_coefficients[3];
  float rgb_mixer[9];
  float black_and_white_mixer[8];
  float grading_hue[3];
  float grading_saturation[3];
  float grading_luminance[3];
  float mixer_hue[8];
  float mixer_saturation[8];
  float mixer_luminance[8];
  float selective_cyan[8];
  float selective_magenta[8];
  float selective_yellow[8];
  float selective_black[8];
  float harmony_anchors[4];
  float saturation_strength;
  float vibrance_strength;
  float grading_amount;
  float grading_balance;
  float harmony_strength;
  float output_headroom;
  float replacement_source_hue;
  float replacement_target_oklab_hue;
  float replacement_range;
  float replacement_softness;
  float replacement_strength;
  float skin_hue;
  float skin_range;
  float skin_strength;
  uint mixer_model;
  uint harmony_anchor_count;
  uint replacement_preserve_luminance;
  uint replacement_preserve_saturation;
  uint active_mask;
  uint pixel_count;
};

struct UserLutStageUniforms {
  float working_to_input[9];
  float output_to_working[9];
  float domain_min[3];
  float domain_max[3];
  float strength;
  uint input_transfer;
  uint output_transfer;
  uint lut_size;
  uint pixel_count;
};

struct GeometryStageUniforms {
  float distortion_strength;
  float chromatic_aberration_strength;
  float vertical_perspective;
  float horizontal_perspective;
  float transform_rotation;
  float transform_scale;
  float transform_skew_x;
  float transform_skew_y;
  float liquify_strength;
  float liquify_center_x;
  float liquify_center_y;
  float liquify_radius;
  float center_x;
  float center_y;
  float maximum_radius;
  uint width;
  uint height;
  uint content_aware_crop_fill;
};

struct HdrLocalToneUniforms {
  float luminance_red;
  float luminance_green;
  float luminance_blue;
  float strength;
  float output_headroom;
  uint width;
  uint height;
};

struct CompiledFillLayerUniform {
  float4 color;
  float opacity;
  uint blend_mode;
  uint mask_offset;
  uint mask_width;
  uint mask_height;
  uint mask_inverted;
  float mask_opacity;
  float mask_density;
  uint has_mask;
  uint clipping_mask;
  uint kind;
  uint pixel_offset;
  uint pixel_width;
  uint pixel_height;
  int x;
  int y;
  float transform_scale;
  float transform_rotation;
  float exposure_gain;
  uint tone_offset;
  uint has_tone;
  uint has_white_balance;
  float white_balance[9];
  uint has_look;
  float rgb_to_xyz_d65[9];
  float xyz_d65_to_rgb[9];
  float look_luminance[3];
  float saturation_strength;
  float vibrance_strength;
  float look_output_headroom;
  uint has_local_chroma;
  float hue_shift;
  float saturation_shift;
  float colorize_hue;
  float colorize_amount;
  uint advanced_tone_offset;
  uint has_advanced_tone;
  uint curve_offsets[6];
  uint curve_active_mask;
  uint has_curves;
  uint has_downstream_look;
  float lens_vignette;
  float dehaze;
};

struct FillLayerUniforms {
  CompiledFillLayerUniform layers[16];
  float blend_headroom;
  uint layer_count;
  uint width;
  uint height;
  float luminance_red;
  float luminance_green;
  float luminance_blue;
  float tone_stop_step;
  float output_headroom;
  float rgb_to_xyz_d65[9];
  float xyz_d65_to_rgb[9];
  uint layer_begin;
  uint layer_end;
  uint source_only;
  uint composite_only;
  uint perceptual_blend_gamut_mode;
};

struct RetainedSpatialDetailUniforms {
  float luminance_red;
  float luminance_green;
  float luminance_blue;
  float texture_strength;
  float clarity_strength;
  float dehaze_strength;
  float output_headroom;
  uint width;
  uint height;
  uint active_mask;
};

struct RetainedFineDetailUniforms {
  float rgb_to_xyz_d65[9];
  float xyz_d65_to_rgb[9];
  float sharpen_strength;
  float sharpen_gain;
  float sharpen_mask_strength;
  float luminance_nr_strength;
  float color_nr_strength;
  uint width;
  uint height;
  uint active_mask;
};

kernel void apply_scene_exposure(
    device float4* pixels [[buffer(0)]],
    constant SceneExposureUniforms& uniforms [[buffer(1)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.pixel_count) {
    return;
  }
  float4 pixel = pixels[gid];
  float previous_layer_alpha = 1.0f;
  pixels[gid] = float4(pixel.rgb * uniforms.gain, pixel.a);
}

static float tone_stage_luminance(
    float3 rgb,
    constant ToneStageUniforms& uniforms) {
  return rgb.r * uniforms.luminance_red +
         rgb.g * uniforms.luminance_green +
         rgb.b * uniforms.luminance_blue;
}

static float tone_stage_target(
    float luminance_value,
    device const float* samples,
    constant ToneStageUniforms& uniforms) {
  if (luminance_value <= 0.0f) {
    return 0.0f;
  }
  const float stops = log2(
      max(1.0e-8f, luminance_value) / uniforms.middle_gray);
  const float position = clamp(
      (stops - uniforms.minimum_stops) / uniforms.stop_step,
      0.0f,
      float(uniforms.sample_count - 1u));
  const uint lower = min(
      uint(position), uniforms.sample_count - 2u);
  return mix(samples[lower], samples[lower + 1u], position - float(lower));
}

kernel void apply_global_tone_luminance(
    device float4* pixels [[buffer(0)]],
    device const float* samples [[buffer(1)]],
    constant ToneStageUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.pixel_count) {
    return;
  }
  const float4 source = pixels[gid];
  const float source_luminance = tone_stage_luminance(source.rgb, uniforms);
  const float target_luminance = max(
      0.0f,
      tone_stage_target(source_luminance, samples, uniforms));
  const float maximum_channel = max(source.r, max(source.g, source.b));
  float safe_target = target_luminance;
  if (maximum_channel <= 1.0e-9f || source_luminance <= 1.0e-9f) {
    safe_target = min(safe_target, uniforms.output_headroom);
  } else {
    safe_target = min(
        safe_target,
        max(
            0.0f,
            uniforms.output_headroom * source_luminance / maximum_channel));
  }

  const float maximum_magnitude = max(
      abs(source.r), max(abs(source.g), abs(source.b)));
  float3 adjusted = source.rgb;
  if (maximum_magnitude <= 1.0e-9f) {
    adjusted = float3(safe_target);
  } else if (source_luminance > 1.0e-6f) {
    adjusted *= safe_target / source_luminance;
  }

  const float adjusted_luminance = max(
      0.0f, tone_stage_luminance(adjusted, uniforms));
  const float nonnegative_source_luminance = max(0.0f, source_luminance);
  const float lift_stops = log2(
      (adjusted_luminance + 1.0e-6f) /
      (nonnegative_source_luminance + 1.0e-6f));
  if (lift_stops > 0.0f) {
    const float channel_spread = max(
        abs(source.r - source.g),
        max(abs(source.g - source.b), abs(source.r - source.b)));
    const float shadow_weight =
        1.0f - smoothstep(0.012f, 0.16f, nonnegative_source_luminance);
    const float lift_weight = smoothstep(0.12f, 1.20f, lift_stops);
    const float contamination_weight =
        1.0f - smoothstep(0.025f, 0.12f, channel_spread);
    const float attenuation = clamp(
        shadow_weight * lift_weight * contamination_weight * 0.56f,
        0.0f,
        0.56f);
    adjusted = adjusted_luminance +
               (adjusted - adjusted_luminance) * (1.0f - attenuation);
  }
  pixels[gid] = float4(adjusted, source.a);
}

static float hdr_local_luminance(
    float3 rgb,
    constant HdrLocalToneUniforms& uniforms) {
  return rgb.r * uniforms.luminance_red +
         rgb.g * uniforms.luminance_green +
         rgb.b * uniforms.luminance_blue;
}

kernel void extract_retained_hdr_local_tone(
    device const float4* pixels [[buffer(0)]],
    device float* log_luminance [[buffer(1)]],
    constant HdrLocalToneUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  const uint total = uniforms.width * uniforms.height;
  if (gid >= total) return;
  log_luminance[gid] = log2(max(
      1.0e-6f, hdr_local_luminance(pixels[gid].rgb, uniforms)));
}

kernel void apply_retained_hdr_local_tone(
    device float4* pixels [[buffer(0)]],
    device const float* log_luminance [[buffer(1)]],
    device const float* local_log [[buffer(2)]],
    constant HdrLocalToneUniforms& uniforms [[buffer(3)]],
    uint gid [[thread_position_in_grid]]) {
  const uint total = uniforms.width * uniforms.height;
  if (gid >= total) return;
  const float4 source = pixels[gid];
  const float target_log = log_luminance[gid];
  const float local = local_log[gid];
  constexpr float kCompressionStart = -0.4739311883324123f;
  const float local_excess = max(0.0f, local - kCompressionStart);
  const float highlight_weight = smoothstep(
      kCompressionStart, kCompressionStart + 2.2f, local);
  const float mapped = exp2(
      target_log - local_excess * uniforms.strength * 0.58f +
      (target_log - local) * uniforms.strength * highlight_weight * 0.45f);
  const float source_luminance = hdr_local_luminance(source.rgb, uniforms);
  const float maximum_channel = max(source.r, max(source.g, source.b));
  float safe_target = max(0.0f, mapped);
  if (maximum_channel <= 1.0e-9f || source_luminance <= 1.0e-9f) {
    safe_target = min(safe_target, uniforms.output_headroom);
  } else {
    safe_target = min(
        safe_target,
        max(0.0f, uniforms.output_headroom * source_luminance / maximum_channel));
  }
  const float maximum_magnitude = max(
      abs(source.r), max(abs(source.g), abs(source.b)));
  float3 adjusted = source.rgb;
  if (maximum_magnitude <= 1.0e-9f) {
    adjusted = float3(safe_target);
  } else if (source_luminance > 1.0e-6f) {
    adjusted *= safe_target / source_luminance;
  }
  pixels[gid] = float4(adjusted, source.a);
}

static float retained_fill_blend(
    float base, float source, float headroom, uint mode) {
  if (mode == 0u) return source;
  const float b = max(0.0f, base);
  const float s = max(0.0f, source);
  if (mode == 1u) return b * s;
  if (mode == 2u) return headroom - (headroom - b) * (headroom - s) / headroom;
  if (mode == 3u) return abs(b - s);
  if (mode == 4u) return max(b, s);
  if (mode == 5u) return min(b, s);
  const float bn = clamp(b / headroom, 0.0f, 1.0f);
  const float sn = clamp(s / headroom, 0.0f, 1.0f);
  if (mode == 6u) {
    return (bn <= 0.5f ? 2.0f * bn * sn
                       : 1.0f - 2.0f * (1.0f - bn) * (1.0f - sn)) * headroom;
  }
  if (mode == 7u) {
    const float d = bn <= 0.25f
        ? ((16.0f * bn - 12.0f) * bn + 4.0f) * bn : sqrt(bn);
    return (sn <= 0.5f
        ? bn - (1.0f - 2.0f * sn) * bn * (1.0f - bn)
        : bn + (2.0f * sn - 1.0f) * (d - bn)) * headroom;
  }
  if (mode == 8u) {
    return (sn <= 0.5f ? 2.0f * sn * bn
                       : 1.0f - 2.0f * (1.0f - sn) * (1.0f - bn)) * headroom;
  }
  return clamp(b + 2.0f * s - headroom, 0.0f, headroom);
}

static float3 retained_blend_multiply(
    constant float* matrix, float3 value) {
  return float3(
      matrix[0] * value.r + matrix[1] * value.g + matrix[2] * value.b,
      matrix[3] * value.r + matrix[4] * value.g + matrix[5] * value.b,
      matrix[6] * value.r + matrix[7] * value.g + matrix[8] * value.b);
}

static float retained_blend_cbrt(float value) {
  return copysign(pow(abs(value), 1.0f / 3.0f), value);
}

static float3 retained_blend_rgb_to_oklab(
    float3 rgb, constant FillLayerUniforms& uniforms) {
  const float3 xyz = retained_blend_multiply(uniforms.rgb_to_xyz_d65, rgb);
  const float l = 0.8189330101f * xyz.x + 0.3618667424f * xyz.y -
                  0.1288597137f * xyz.z;
  const float m = 0.0329845436f * xyz.x + 0.9293118715f * xyz.y +
                  0.0361456387f * xyz.z;
  const float s = 0.0482003018f * xyz.x + 0.2643662691f * xyz.y +
                  0.6338517070f * xyz.z;
  const float lr = retained_blend_cbrt(l);
  const float mr = retained_blend_cbrt(m);
  const float sr = retained_blend_cbrt(s);
  return float3(
      0.2104542553f * lr + 0.7936177850f * mr - 0.0040720468f * sr,
      1.9779984951f * lr - 2.4285922050f * mr + 0.4505937099f * sr,
      0.0259040371f * lr + 0.7827717662f * mr - 0.8086757660f * sr);
}

static float3 retained_blend_oklab_to_rgb(
    float lightness, float chroma, float hue,
    constant FillLayerUniforms& uniforms) {
  const float a = chroma * cos(hue);
  const float b = chroma * sin(hue);
  const float lr = lightness + 0.3963377774f * a + 0.2158037573f * b;
  const float mr = lightness - 0.1055613458f * a - 0.0638541728f * b;
  const float sr = lightness - 0.0894841775f * a - 1.2914855480f * b;
  const float l = lr * lr * lr;
  const float m = mr * mr * mr;
  const float s = sr * sr * sr;
  const float3 xyz = float3(
      1.2270138511f * l - 0.5577999807f * m + 0.2812561490f * s,
      -0.0405801784f * l + 1.1122568696f * m - 0.0716766787f * s,
      -0.0763812845f * l - 0.4214819784f * m + 1.5861632204f * s);
  return retained_blend_multiply(uniforms.xyz_d65_to_rgb, xyz);
}

static bool retained_blend_in_gamut(
    float3 rgb, constant FillLayerUniforms& uniforms) {
  return all(rgb >= float3(-1.0e-6f)) &&
         all(rgb <= float3(uniforms.blend_headroom + 1.0e-6f));
}

static float3 retained_blend_fit(
    float3 rgb, constant FillLayerUniforms& uniforms) {
  rgb = max(rgb, float3(0.0f));
  const float maximum = max(rgb.r, max(rgb.g, rgb.b));
  return maximum > uniforms.blend_headroom
      ? rgb * (uniforms.blend_headroom / maximum) : rgb;
}

static float4 retained_blend_search_chroma(
    float lightness, float chroma, float hue,
    constant FillLayerUniforms& uniforms) {
  const float ceiling = pow(uniforms.blend_headroom, 1.0f / 3.0f);
  lightness = clamp(lightness, 0.0f, ceiling);
  chroma = max(0.0f, chroma);
  float3 candidate = retained_blend_oklab_to_rgb(
      lightness, chroma, hue, uniforms);
  if (uniforms.perceptual_blend_gamut_mode == 1u ||
      retained_blend_in_gamut(candidate, uniforms)) {
    return float4(retained_blend_fit(candidate, uniforms), chroma);
  }
  float low = 0.0f;
  float high = chroma;
  for (uint iteration = 0u; iteration < 16u; ++iteration) {
    const float mid = (low + high) * 0.5f;
    candidate = retained_blend_oklab_to_rgb(
        lightness, mid, hue, uniforms);
    if (retained_blend_in_gamut(candidate, uniforms)) low = mid;
    else high = mid;
  }
  candidate = retained_blend_oklab_to_rgb(lightness, low, hue, uniforms);
  return float4(retained_blend_fit(candidate, uniforms), low);
}

static float3 retained_fill_blend_pixel(
    float3 base, float3 source, uint mode,
    constant FillLayerUniforms& uniforms) {
  if (mode < 10u) {
    return float3(
        retained_fill_blend(base.r, source.r, uniforms.blend_headroom, mode),
        retained_fill_blend(base.g, source.g, uniforms.blend_headroom, mode),
        retained_fill_blend(base.b, source.b, uniforms.blend_headroom, mode));
  }
  const float3 base_lab = retained_blend_rgb_to_oklab(base, uniforms);
  const float3 source_lab = retained_blend_rgb_to_oklab(source, uniforms);
  const float ceiling = pow(uniforms.blend_headroom, 1.0f / 3.0f);
  float lightness = clamp(base_lab.x, 0.0f, ceiling);
  float chroma = length(base_lab.yz);
  float hue = atan2(base_lab.z, base_lab.y);
  const float source_chroma = length(source_lab.yz);
  const float source_hue = source_chroma > 1.0e-6f
      ? atan2(source_lab.z, source_lab.y) : hue;
  if (mode == 10u) hue = source_hue;
  else if (mode == 11u) chroma = source_chroma;
  else if (mode == 12u) {
    hue = source_hue;
    chroma = source_chroma;
  } else lightness = clamp(source_lab.x, 0.0f, ceiling);
  if (chroma <= 1.0e-9f) {
    return retained_blend_fit(
        retained_blend_oklab_to_rgb(lightness, 0.0f, 0.0f, uniforms),
        uniforms);
  }
  float4 best = retained_blend_search_chroma(
      lightness, chroma, hue, uniforms);
  float best_lightness = lightness;
  if (best.w > max(1.0e-4f, chroma * 0.02f) ||
      uniforms.perceptual_blend_gamut_mode == 2u) return best.rgb;
  for (uint iteration = 1u; iteration <= 12u; ++iteration) {
    const float candidate_lightness = mix(
        lightness, 0.5f, float(iteration) / 12.0f);
    const float4 candidate = retained_blend_search_chroma(
        candidate_lightness, chroma, hue, uniforms);
    if (candidate.w > best.w + 1.0e-6f ||
        (abs(candidate.w - best.w) <= 1.0e-6f &&
         abs(candidate_lightness - lightness) <
             abs(best_lightness - lightness))) {
      best = candidate;
      best_lightness = candidate_lightness;
    }
  }
  return best.rgb;
}

static float retained_layer_luminance(
    float3 rgb, constant FillLayerUniforms& uniforms) {
  return rgb.r * uniforms.luminance_red +
         rgb.g * uniforms.luminance_green +
         rgb.b * uniforms.luminance_blue;
}

static float retained_layer_tone_target(
    float value,
    device const float* samples,
    uint offset,
    constant FillLayerUniforms& uniforms) {
  if (value <= 0.0f) return 0.0f;
  const float stops = log2(max(1.0e-8f, value) / 0.18f);
  const float position = clamp(
      (stops + 20.0f) / uniforms.tone_stop_step, 0.0f, 16384.0f);
  const uint lower = min(uint(position), 16383u);
  return mix(
      samples[offset + lower], samples[offset + lower + 1u],
      position - float(lower));
}

static float3 retained_layer_tone(
    float3 source,
    device const float* samples,
    uint offset,
    constant FillLayerUniforms& uniforms) {
  const float source_luminance = retained_layer_luminance(source, uniforms);
  const float target = max(
      0.0f, retained_layer_tone_target(
          source_luminance, samples, offset, uniforms));
  const float maximum_channel = max(source.r, max(source.g, source.b));
  float safe_target = target;
  if (maximum_channel <= 1.0e-9f || source_luminance <= 1.0e-9f) {
    safe_target = min(safe_target, uniforms.output_headroom);
  } else {
    safe_target = min(safe_target, max(
        0.0f, uniforms.output_headroom * source_luminance / maximum_channel));
  }
  const float maximum_magnitude = max(
      abs(source.r), max(abs(source.g), abs(source.b)));
  float3 adjusted = source;
  if (maximum_magnitude <= 1.0e-9f) {
    adjusted = float3(safe_target);
  } else if (source_luminance > 1.0e-6f) {
    adjusted *= safe_target / source_luminance;
  }
  const float adjusted_luminance = max(
      0.0f, retained_layer_luminance(adjusted, uniforms));
  const float nonnegative_source = max(0.0f, source_luminance);
  const float lift_stops = log2(
      (adjusted_luminance + 1.0e-6f) / (nonnegative_source + 1.0e-6f));
  if (lift_stops > 0.0f) {
    const float spread = max(
        abs(source.r - source.g),
        max(abs(source.g - source.b), abs(source.r - source.b)));
    const float attenuation = clamp(
        (1.0f - smoothstep(0.012f, 0.16f, nonnegative_source)) *
            smoothstep(0.12f, 1.20f, lift_stops) *
            (1.0f - smoothstep(0.025f, 0.12f, spread)) * 0.56f,
        0.0f, 0.56f);
    adjusted = adjusted_luminance +
               (adjusted - adjusted_luminance) * (1.0f - attenuation);
  }
  return adjusted;
}

static float retained_layer_curve_sample(
    device const float* samples, uint offset, float value) {
  if (value > 1.0f) return value * samples[offset + 4096u];
  if (value < 0.0f) return value + samples[offset];
  const float position = clamp(value, 0.0f, 1.0f) * 4096.0f;
  const uint lower = min(uint(position), 4095u);
  return mix(
      samples[offset + lower], samples[offset + lower + 1u],
      position - float(lower));
}

static float3 retained_layer_curve_scale(
    float3 source, float target, constant FillLayerUniforms& uniforms) {
  const float source_luminance = retained_layer_luminance(source, uniforms);
  const float maximum_channel = max(source.r, max(source.g, source.b));
  float safe_target = max(0.0f, target);
  if (maximum_channel <= 1.0e-9f || source_luminance <= 1.0e-9f) {
    safe_target = min(safe_target, uniforms.output_headroom);
  } else {
    safe_target = min(safe_target, max(
        0.0f, uniforms.output_headroom * source_luminance / maximum_channel));
  }
  const float maximum_magnitude = max(
      abs(source.r), max(abs(source.g), abs(source.b)));
  float3 adjusted = source;
  if (maximum_magnitude <= 1.0e-9f) adjusted = float3(safe_target);
  else if (source_luminance > 1.0e-6f) adjusted *= safe_target / source_luminance;
  const float adjusted_luminance = max(
      0.0f, retained_layer_luminance(adjusted, uniforms));
  const float nonnegative_source = max(0.0f, source_luminance);
  const float lift_stops = log2(
      (adjusted_luminance + 1.0e-6f) / (nonnegative_source + 1.0e-6f));
  if (lift_stops <= 0.0f) return adjusted;
  const float spread = max(
      abs(source.r - source.g),
      max(abs(source.g - source.b), abs(source.r - source.b)));
  const float attenuation = clamp(
      (1.0f - smoothstep(0.012f, 0.16f, nonnegative_source)) *
          smoothstep(0.12f, 1.20f, lift_stops) *
          (1.0f - smoothstep(0.025f, 0.12f, spread)) * 0.56f,
      0.0f, 0.56f);
  return adjusted_luminance +
         (adjusted - adjusted_luminance) * (1.0f - attenuation);
}

static float3 retained_layer_curves(
    float3 adjusted, device const float* samples,
    thread const CompiledFillLayerUniform& layer,
    constant FillLayerUniforms& uniforms) {
  if ((layer.curve_active_mask & 1u) != 0u) {
    const float luminance = retained_layer_luminance(adjusted, uniforms);
    const float target = luminance > 1.0f
        ? clamp(luminance, 0.0f, uniforms.output_headroom)
        : retained_layer_curve_sample(samples, layer.curve_offsets[0], luminance);
    adjusted = retained_layer_curve_scale(adjusted, target, uniforms);
  }
  if ((layer.curve_active_mask & 2u) != 0u) adjusted = float3(
      retained_layer_curve_sample(samples, layer.curve_offsets[1], adjusted.r),
      retained_layer_curve_sample(samples, layer.curve_offsets[1], adjusted.g),
      retained_layer_curve_sample(samples, layer.curve_offsets[1], adjusted.b));
  if ((layer.curve_active_mask & 4u) != 0u) adjusted =
      retained_layer_curve_scale(adjusted, retained_layer_curve_sample(
          samples, layer.curve_offsets[2],
          retained_layer_luminance(adjusted, uniforms)), uniforms);
  if ((layer.curve_active_mask & 8u) != 0u) adjusted.r =
      retained_layer_curve_sample(samples, layer.curve_offsets[3], adjusted.r);
  if ((layer.curve_active_mask & 16u) != 0u) adjusted.g =
      retained_layer_curve_sample(samples, layer.curve_offsets[4], adjusted.g);
  if ((layer.curve_active_mask & 32u) != 0u) adjusted.b =
      retained_layer_curve_sample(samples, layer.curve_offsets[5], adjusted.b);
  return adjusted;
}

static float3 look_multiply(constant float* matrix, float3 value);
static float look_luminance(
    float3 rgb, constant LookStageUniforms& uniforms);
static float look_rgb_hue(float3 rgb);
static float look_channel_weight(float hue, uint channel);
static float3 look_apply_saturation_vibrance(
    float3 input, constant LookStageUniforms& uniforms);

static float3 retained_layer_downstream_look(
    float3 adjusted, constant LookStageUniforms& uniforms) {
  if ((uniforms.active_mask & 1u) != 0u) {
    adjusted = max(float3(0.0f), look_multiply(uniforms.rgb_mixer, adjusted));
  }
  if ((uniforms.active_mask & 2u) != 0u) {
    float gray = look_luminance(adjusted, uniforms);
    const float hue = look_rgb_hue(adjusted);
    if (hue >= 0.0f) {
      for (uint channel = 0u; channel < 8u; ++channel) {
        gray *= max(0.0f, 1.0f + look_channel_weight(hue, channel) *
                                  uniforms.black_and_white_mixer[channel] * 0.85f);
      }
    }
    adjusted = float3(max(0.0f, gray));
  }
  if ((uniforms.active_mask & (4u | 8u | 32u | 64u | 128u)) != 0u) {
    adjusted = look_apply_saturation_vibrance(adjusted, uniforms);
  }
  return adjusted;
}

static float3 retained_layer_point_effects(
    float3 input, thread const CompiledFillLayerUniform& layer,
    uint x, uint y, constant FillLayerUniforms& uniforms) {
  float3 output = input;
  if (abs(layer.lens_vignette) > 1.0e-6f &&
      uniforms.width > 1u && uniforms.height > 1u) {
    const float2 center = float2(uniforms.width - 1u, uniforms.height - 1u) * 0.5f;
    const float radius = length(float2(x, y) - center) / max(1.0f, length(center));
    output *= max(0.20f, 1.0f + layer.lens_vignette *
        smoothstep(0.22f, 1.0f, radius) * 0.85f);
  }
  if (abs(layer.dehaze) > 1.0e-9f) {
    const float3 xyz = look_multiply(uniforms.rgb_to_xyz_d65, output);
    const float l = 0.8189330101f * xyz.x + 0.3618667424f * xyz.y - 0.1288597137f * xyz.z;
    const float m = 0.0329845436f * xyz.x + 0.9293118715f * xyz.y + 0.0361456387f * xyz.z;
    const float s = 0.0482003018f * xyz.x + 0.2643662691f * xyz.y + 0.6338517070f * xyz.z;
    const float lr = retained_blend_cbrt(l), mr = retained_blend_cbrt(m), sr = retained_blend_cbrt(s);
    const float lightness = 0.2104542553f * lr + 0.7936177850f * mr - 0.0040720468f * sr;
    float a = 1.9779984951f * lr - 2.4285922050f * mr + 0.4505937099f * sr;
    float b = 0.0259040371f * lr + 0.7827717662f * mr - 0.8086757660f * sr;
    if (length(float2(a, b)) > 1.0e-6f) {
      const float scale = max(0.0f, 1.0f + layer.dehaze * 0.10f);
      a *= scale; b *= scale;
      const float nl = lightness + 0.3963377774f * a + 0.2158037573f * b;
      const float nm = lightness - 0.1055613458f * a - 0.0638541728f * b;
      const float ns = lightness - 0.0894841775f * a - 1.2914855480f * b;
      output = max(float3(0.0f), look_multiply(uniforms.xyz_d65_to_rgb, float3(
          1.2270138511f * nl * nl * nl - 0.5577999807f * nm * nm * nm + 0.2812561490f * ns * ns * ns,
          -0.0405801784f * nl * nl * nl + 1.1122568696f * nm * nm * nm - 0.0716766787f * ns * ns * ns,
          -0.0763812845f * nl * nl * nl - 0.4214819784f * nm * nm * nm + 1.5861632204f * ns * ns * ns)));
      const float maximum = max(output.r, max(output.g, output.b));
      if (maximum > uniforms.output_headroom) output *= uniforms.output_headroom / maximum;
    }
  }
  return output;
}

static float3 retained_layer_multiply(
    thread const float* matrix, float3 value) {
  return float3(
      matrix[0] * value.x + matrix[1] * value.y + matrix[2] * value.z,
      matrix[3] * value.x + matrix[4] * value.y + matrix[5] * value.z,
      matrix[6] * value.x + matrix[7] * value.y + matrix[8] * value.z);
}

static float3 retained_layer_look(
    float3 input, thread const CompiledFillLayerUniform& layer) {
  const float spread = max(
      abs(input.r - input.g), max(abs(input.g - input.b), abs(input.r - input.b)));
  if (spread <= 1.0e-6f) return input;
  const float3 xyz = retained_layer_multiply(layer.rgb_to_xyz_d65, input);
  const float l = 0.8189330101f * xyz.x + 0.3618667424f * xyz.y - 0.1288597137f * xyz.z;
  const float m = 0.0329845436f * xyz.x + 0.9293118715f * xyz.y + 0.0361456387f * xyz.z;
  const float s = 0.0482003018f * xyz.x + 0.2643662691f * xyz.y + 0.6338517070f * xyz.z;
  const float l_root = retained_blend_cbrt(l), m_root = retained_blend_cbrt(m), s_root = retained_blend_cbrt(s);
  const float lightness = clamp(
      0.2104542553f * l_root + 0.7936177850f * m_root - 0.0040720468f * s_root,
      0.0f, retained_blend_cbrt(layer.look_output_headroom));
  float a = 1.9779984951f * l_root - 2.4285922050f * m_root + 0.4505937099f * s_root;
  float b = 0.0259040371f * l_root + 0.7827717662f * m_root - 0.8086757660f * s_root;
  const float original_chroma = length(float2(a, b));
  float chroma = original_chroma <= 1.0e-4f ? 0.0f : original_chroma;
  const float vibrance_weight = 1.0f - smoothstep(0.035f, 0.24f, chroma);
  chroma *= max(
      0.0f, 1.0f + layer.vibrance_strength * vibrance_weight * 0.82f);
  float scale = original_chroma > 1.0e-9f ? chroma / original_chroma : 0.0f;
  a *= scale;
  b *= scale;
  chroma *= layer.saturation_strength >= 0.0f
      ? max(0.0f, 1.0f + layer.saturation_strength * 0.85f)
      : max(0.0f, 1.0f + layer.saturation_strength);
  const float current_chroma = length(float2(a, b));
  scale = current_chroma > 1.0e-9f ? chroma / current_chroma : 0.0f;
  a *= scale;
  b *= scale;
  const float next_l = lightness + 0.3963377774f * a + 0.2158037573f * b;
  const float next_m = lightness - 0.1055613458f * a - 0.0638541728f * b;
  const float next_s = lightness - 0.0894841775f * a - 1.2914855480f * b;
  const float3 oklab_rgb = float3(
      1.2270138511f * next_l * next_l * next_l -
          0.5577999807f * next_m * next_m * next_m +
          0.2812561490f * next_s * next_s * next_s,
      -0.0405801784f * next_l * next_l * next_l +
          1.1122568696f * next_m * next_m * next_m -
          0.0716766787f * next_s * next_s * next_s,
      -0.0763812845f * next_l * next_l * next_l -
          0.4214819784f * next_m * next_m * next_m +
          1.5861632204f * next_s * next_s * next_s);
  float3 output = retained_layer_multiply(layer.xyz_d65_to_rgb, oklab_rgb);
  if (layer.saturation_strength <= -0.999999f) {
    const float neutral = output.r * layer.look_luminance[0] +
                          output.g * layer.look_luminance[1] +
                          output.b * layer.look_luminance[2];
    output = float3(neutral);
  }
  return output;
}

static float retained_layer_wrap(float value) {
  value = fmod(value, 6.28318530717958647692f);
  return value < 0.0f ? value + 6.28318530717958647692f : value;
}

static float retained_layer_shortest_delta(float from, float to) {
  float delta = fmod(to - from, 6.28318530717958647692f);
  if (delta > 3.14159265358979323846f) delta -= 6.28318530717958647692f;
  if (delta < -3.14159265358979323846f) delta += 6.28318530717958647692f;
  return delta;
}

static float3 retained_layer_local_chroma(
    float3 input, thread const CompiledFillLayerUniform& layer) {
  const float3 xyz = retained_layer_multiply(layer.rgb_to_xyz_d65, input);
  const float l = 0.8189330101f * xyz.x + 0.3618667424f * xyz.y - 0.1288597137f * xyz.z;
  const float m = 0.0329845436f * xyz.x + 0.9293118715f * xyz.y + 0.0361456387f * xyz.z;
  const float s = 0.0482003018f * xyz.x + 0.2643662691f * xyz.y + 0.6338517070f * xyz.z;
  const float lr = retained_blend_cbrt(l), mr = retained_blend_cbrt(m), sr = retained_blend_cbrt(s);
  const float lightness = 0.2104542553f * lr + 0.7936177850f * mr - 0.0040720468f * sr;
  const float a = 1.9779984951f * lr - 2.4285922050f * mr + 0.4505937099f * sr;
  const float b = 0.0259040371f * lr + 0.7827717662f * mr - 0.8086757660f * sr;
  float chroma = length(float2(a, b));
  float hue = retained_layer_wrap(atan2(b, a));
  if (chroma <= 1.0e-6f) {
    hue = layer.colorize_hue;
  } else {
    chroma *= max(
        0.0f, 1.0f + layer.vibrance_strength *
            (1.0f - smoothstep(0.035f, 0.24f, chroma)) * 0.82f);
    chroma *= layer.saturation_strength >= 0.0f
        ? max(0.0f, 1.0f + layer.saturation_strength * 0.85f)
        : max(0.0f, 1.0f + layer.saturation_strength);
    chroma *= clamp(
        1.0f - 0.20f * smoothstep(0.78f, 0.98f, lightness) *
                           smoothstep(0.035f, 0.22f, chroma),
        0.78f, 1.0f);
  }
  hue = retained_layer_wrap(hue + layer.hue_shift);
  chroma *= max(0.0f, 1.0f + layer.saturation_shift);
  if (layer.colorize_amount > 1.0e-6f) {
    hue = retained_layer_wrap(
        hue + retained_layer_shortest_delta(hue, layer.colorize_hue) *
                  layer.colorize_amount);
    chroma = mix(chroma, max(chroma, 0.18f), layer.colorize_amount);
  }
  const float next_a = chroma * cos(hue), next_b = chroma * sin(hue);
  const float next_l = lightness + 0.3963377774f * next_a + 0.2158037573f * next_b;
  const float next_m = lightness - 0.1055613458f * next_a - 0.0638541728f * next_b;
  const float next_s = lightness - 0.0894841775f * next_a - 1.2914855480f * next_b;
  return retained_layer_multiply(layer.xyz_d65_to_rgb, float3(
      1.2270138511f * next_l * next_l * next_l -
          0.5577999807f * next_m * next_m * next_m +
          0.2812561490f * next_s * next_s * next_s,
      -0.0405801784f * next_l * next_l * next_l +
          1.1122568696f * next_m * next_m * next_m -
          0.0716766787f * next_s * next_s * next_s,
      -0.0763812845f * next_l * next_l * next_l -
          0.4214819784f * next_m * next_m * next_m +
          1.5861632204f * next_s * next_s * next_s));
}

kernel void apply_retained_fill_layers(
    device float4* pixels [[buffer(0)]],
    device const uchar* masks [[buffer(1)]],
    device const float4* layer_pixels [[buffer(2)]],
    device const float* tone_samples [[buffer(3)]],
    device const float* curve_samples [[buffer(4)]],
    constant LookStageUniforms* layer_looks [[buffer(5)]],
    constant FillLayerUniforms& uniforms [[buffer(6)]],
    device float* layer_alpha [[buffer(7)]],
    device float4* layer_intermediate [[buffer(8)]],
    uint gid [[thread_position_in_grid]]) {
  float4 pixel = pixels[gid];
  float previous_layer_alpha = uniforms.layer_begin == 0u
      ? 1.0f : layer_alpha[gid];
  const uint x = gid % uniforms.width;
  const uint y = gid / uniforms.width;
  // Deferred adjustment layers apply optics after their LUT, and dehaze
  // after geometry/luminance extraction, matching the CPU stage ordering.
  if (uniforms.source_only == 2u || uniforms.source_only == 3u) {
    CompiledFillLayerUniform layer = uniforms.layers[uniforms.layer_begin];
    if (uniforms.source_only == 2u) layer.dehaze = 0.0f;
    else layer.lens_vignette = 0.0f;
    const float4 source = layer_intermediate[gid];
    layer_intermediate[gid] = float4(
        retained_layer_point_effects(source.rgb, layer, x, y, uniforms), source.a);
    return;
  }
  for (uint index = uniforms.layer_begin; index < uniforms.layer_end; ++index) {
    const CompiledFillLayerUniform layer = uniforms.layers[index];
    const bool adjustment = layer.kind == 2u;
    float4 source = uniforms.composite_only != 0u
        ? layer_intermediate[gid] : (adjustment ? pixel : layer.color);
    if (uniforms.composite_only == 0u && adjustment &&
        layer.has_white_balance != 0u) {
      const float3 rgb = source.rgb;
      source = float4(
          layer.white_balance[0] * rgb.r +
              layer.white_balance[1] * rgb.g +
              layer.white_balance[2] * rgb.b,
          layer.white_balance[3] * rgb.r +
              layer.white_balance[4] * rgb.g +
              layer.white_balance[5] * rgb.b,
          layer.white_balance[6] * rgb.r +
              layer.white_balance[7] * rgb.g +
              layer.white_balance[8] * rgb.b,
          source.a);
    }
    if (uniforms.composite_only == 0u && adjustment)
      source.rgb *= layer.exposure_gain;
    if (layer.kind == 1u) {
      const float center_x = float(layer.x) +
          float(layer.pixel_width) * layer.transform_scale * 0.5f;
      const float center_y = float(layer.y) +
          float(layer.pixel_height) * layer.transform_scale * 0.5f;
      const float dx = float(x) + 0.5f - center_x;
      const float dy = float(y) + 0.5f - center_y;
      const float cosine = cos(layer.transform_rotation);
      const float sine = sin(layer.transform_rotation);
      const float local_x = (dx * cosine - dy * sine) / layer.transform_scale +
          float(layer.pixel_width) * 0.5f - 0.5f;
      const float local_y = (dx * sine + dy * cosine) / layer.transform_scale +
          float(layer.pixel_height) * 0.5f - 0.5f;
      const float radius = max(1.0f, 1.0f / layer.transform_scale);
      if (local_x <= -radius || local_y <= -radius ||
          local_x >= float(layer.pixel_width - 1u) + radius || local_y >= float(layer.pixel_height - 1u) + radius) {
        previous_layer_alpha = 0.0f;
        continue;
      }
      if (layer.transform_scale < 1.0f) {
        const int2 lower = int2(ceil(float2(local_x, local_y) - radius));
        const int2 upper = int2(floor(float2(local_x, local_y) + radius));
        float norm_x = 0.0f, norm_y = 0.0f;
        for (int sx = lower.x; sx <= upper.x; ++sx) norm_x += max(0.0f, 1.0f-abs(local_x-float(sx))/radius);
        for (int sy = lower.y; sy <= upper.y; ++sy) norm_y += max(0.0f, 1.0f-abs(local_y-float(sy))/radius);
        float4 associated = float4(0.0f);
        for (int sy = max(lower.y, 0); sy <= min(upper.y, int(layer.pixel_height)-1); ++sy) {
          const float wy = max(0.0f, 1.0f-abs(local_y-float(sy))/radius);
          for (int sx = max(lower.x, 0); sx <= min(upper.x, int(layer.pixel_width)-1); ++sx) {
            const float4 texel = layer_pixels[layer.pixel_offset + uint(sy)*layer.pixel_width + uint(sx)];
            const float weight = wy*max(0.0f, 1.0f-abs(local_x-float(sx))/radius)*clamp(texel.a, 0.0f, 1.0f);
            if (weight > 0.0f) associated += float4(texel.rgb*weight, weight);
          }
        }
        source = associated.a > 0.0f ? float4(associated.rgb/associated.a, associated.a/(norm_x*norm_y)) : float4(0.0f);
      } else {
      const int2 origin = int2(floor(float2(local_x, local_y)));
      const float2 fraction = float2(local_x, local_y) - float2(origin);
      if (all(fraction == float2(0.0f))) {
        source = layer_pixels[layer.pixel_offset + uint(origin.y) * layer.pixel_width + uint(origin.x)];
      } else {
        float4 associated = float4(0.0f);
        for (int iy = 0; iy < 2; ++iy) for (int ix = 0; ix < 2; ++ix) {
          const int2 p = origin + int2(ix, iy);
          if (p.x < 0 || p.y < 0 || p.x >= int(layer.pixel_width) || p.y >= int(layer.pixel_height)) continue;
          const float4 texel = layer_pixels[layer.pixel_offset + uint(p.y) * layer.pixel_width + uint(p.x)];
          const float weight = (ix ? fraction.x : 1.0f - fraction.x) *
              (iy ? fraction.y : 1.0f - fraction.y) * clamp(texel.a, 0.0f, 1.0f);
          if (weight > 0.0f) associated += float4(texel.rgb * weight, weight);
        }
        source = associated.a > 0.0f ? float4(associated.rgb / associated.a, associated.a) : float4(0.0f);
      }
      }
    }
    if (uniforms.composite_only == 0u && adjustment && layer.has_tone != 0u) {
      source = float4(
          retained_layer_tone(
              source.rgb, tone_samples, layer.tone_offset, uniforms),
          source.a);
    }
    if (uniforms.composite_only == 0u && adjustment &&
        layer.has_local_chroma != 0u) {
      source = float4(retained_layer_local_chroma(source.rgb, layer), source.a);
    } else if (uniforms.composite_only == 0u && adjustment &&
               layer.has_look != 0u) {
      source = float4(retained_layer_look(source.rgb, layer), source.a);
    }
    if (uniforms.composite_only == 0u && adjustment &&
        layer.has_advanced_tone != 0u) {
      source = float4(
          retained_layer_tone(
              source.rgb, tone_samples, layer.advanced_tone_offset, uniforms),
          source.a);
    }
    if (uniforms.composite_only == 0u && adjustment && layer.has_curves != 0u) {
      source = float4(
          retained_layer_curves(
              source.rgb, curve_samples, layer, uniforms),
          source.a);
    }
    if (uniforms.composite_only == 0u && adjustment &&
        layer.has_downstream_look != 0u) {
      source = float4(
          retained_layer_downstream_look(source.rgb, layer_looks[index]),
          source.a);
    }
    if (uniforms.composite_only == 0u && uniforms.source_only == 0u && adjustment) source = float4(
        retained_layer_point_effects(source.rgb, layer, x, y, uniforms), source.a);
    if (uniforms.source_only != 0u) {
      layer_intermediate[gid] = source;
      return;
    }
    float mask_weight = 1.0f;
    if (layer.has_mask != 0u) {
      const float nx = uniforms.width > 1u
          ? float(x) / float(uniforms.width - 1u) : 0.0f;
      const float ny = uniforms.height > 1u
          ? float(y) / float(uniforms.height - 1u) : 0.0f;
      const uint mx = min(
          uint(floor(nx * float(layer.mask_width - 1u) + 0.5f)),
          layer.mask_width - 1u);
      const uint my = min(
          uint(floor(ny * float(layer.mask_height - 1u) + 0.5f)),
          layer.mask_height - 1u);
      mask_weight = float(
          masks[layer.mask_offset + my * layer.mask_width + mx]) / 255.0f;
      if (layer.mask_inverted != 0u) mask_weight = 1.0f - mask_weight;
      mask_weight = clamp(
          min(mask_weight, layer.mask_density) * layer.mask_opacity,
          0.0f, 1.0f);
    }
    const float composition_weight = clamp(
        layer.opacity * mask_weight *
            (layer.clipping_mask != 0u ? previous_layer_alpha : 1.0f),
        0.0f, 1.0f);
    const float alpha = adjustment
        ? composition_weight : clamp(source.a * composition_weight, 0.0f, 1.0f);
    previous_layer_alpha = alpha;
    const float3 blended = retained_fill_blend_pixel(
        pixel.rgb, source.rgb, layer.blend_mode, uniforms);
    if (adjustment) pixel = float4(mix(pixel.rgb, blended, alpha), pixel.a);
    else if (alpha > 0.0f) {
      const float ba = clamp(pixel.a, 0.0f, 1.0f);
      const float output_alpha = alpha + ba * (1.0f - alpha);
      if (ba >= 1.0f) pixel = float4(mix(pixel.rgb, blended, alpha), output_alpha);
      else if (ba <= 0.0f) pixel = float4(source.rgb, alpha);
      else pixel = float4((alpha * mix(source.rgb, blended, ba) +
          (1.0f - alpha) * ba * pixel.rgb) / output_alpha, output_alpha);
    }
  }
  layer_alpha[gid] = previous_layer_alpha;
  pixels[gid] = pixel;
}

static float curve_stage_luminance(
    float3 rgb,
    constant CurveStageUniforms& uniforms) {
  return rgb.r * uniforms.luminance_red +
         rgb.g * uniforms.luminance_green +
         rgb.b * uniforms.luminance_blue;
}

static float sample_curve_stage_lut(
    device const float* samples,
    constant CurveStageUniforms& uniforms,
    uint block,
    float value) {
  const uint offset = block * uniforms.sample_count;
  if (value > 1.0f) return value * samples[offset + uniforms.sample_count - 1u];
  if (value < 0.0f) return value + samples[offset];
  const float position = clamp(value, 0.0f, 1.0f) *
                         float(uniforms.sample_count - 1u);
  const uint lower = min(uint(position), uniforms.sample_count - 2u);
  return mix(
      samples[offset + lower],
      samples[offset + lower + 1u],
      position - float(lower));
}

static float3 curve_stage_scale_to_luminance(
    float3 source,
    float target,
    constant CurveStageUniforms& uniforms) {
  const float source_luminance = curve_stage_luminance(source, uniforms);
  const float maximum_channel = max(source.r, max(source.g, source.b));
  float safe_target = max(0.0f, target);
  if (maximum_channel <= 1.0e-9f || source_luminance <= 1.0e-9f) {
    safe_target = min(safe_target, uniforms.output_headroom);
  } else {
    safe_target = min(
        safe_target,
        max(0.0f, uniforms.output_headroom * source_luminance / maximum_channel));
  }
  const float maximum_magnitude = max(
      abs(source.r), max(abs(source.g), abs(source.b)));
  float3 adjusted = source;
  if (maximum_magnitude <= 1.0e-9f) {
    adjusted = float3(safe_target);
  } else if (source_luminance > 1.0e-6f) {
    adjusted *= safe_target / source_luminance;
  }
  const float adjusted_luminance = max(
      0.0f, curve_stage_luminance(adjusted, uniforms));
  const float nonnegative_source = max(0.0f, source_luminance);
  const float lift_stops = log2(
      (adjusted_luminance + 1.0e-6f) /
      (nonnegative_source + 1.0e-6f));
  if (lift_stops > 0.0f) {
    const float spread = max(
        abs(source.r - source.g),
        max(abs(source.g - source.b), abs(source.r - source.b)));
    const float attenuation = clamp(
        (1.0f - smoothstep(0.012f, 0.16f, nonnegative_source)) *
            smoothstep(0.12f, 1.20f, lift_stops) *
            (1.0f - smoothstep(0.025f, 0.12f, spread)) * 0.56f,
        0.0f,
        0.56f);
    adjusted = adjusted_luminance +
               (adjusted - adjusted_luminance) * (1.0f - attenuation);
  }
  return adjusted;
}

kernel void apply_compiled_curve_stage(
    device float4* pixels [[buffer(0)]],
    device const float* samples [[buffer(1)]],
    constant CurveStageUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.pixel_count) {
    return;
  }
  const float alpha = pixels[gid].a;
  float3 adjusted = pixels[gid].rgb;
  if ((uniforms.active_mask & 1u) != 0u) {
    const float source_luminance = curve_stage_luminance(adjusted, uniforms);
    const float target = source_luminance > 1.0f
        ? clamp(source_luminance, 0.0f, uniforms.output_headroom)
        : sample_curve_stage_lut(samples, uniforms, 0u, source_luminance);
    adjusted = curve_stage_scale_to_luminance(adjusted, target, uniforms);
  }
  if ((uniforms.active_mask & 2u) != 0u) {
    adjusted = float3(
        sample_curve_stage_lut(samples, uniforms, 1u, adjusted.r),
        sample_curve_stage_lut(samples, uniforms, 1u, adjusted.g),
        sample_curve_stage_lut(samples, uniforms, 1u, adjusted.b));
  }
  if ((uniforms.active_mask & 4u) != 0u) {
    adjusted = curve_stage_scale_to_luminance(
        adjusted,
        sample_curve_stage_lut(
            samples, uniforms, 2u, curve_stage_luminance(adjusted, uniforms)),
        uniforms);
  }
  if ((uniforms.active_mask & 8u) != 0u) {
    adjusted.r = sample_curve_stage_lut(samples, uniforms, 3u, adjusted.r);
  }
  if ((uniforms.active_mask & 16u) != 0u) {
    adjusted.g = sample_curve_stage_lut(samples, uniforms, 4u, adjusted.g);
  }
  if ((uniforms.active_mask & 32u) != 0u) {
    adjusted.b = sample_curve_stage_lut(samples, uniforms, 5u, adjusted.b);
  }
  pixels[gid] = float4(adjusted, alpha);
}

static float3 look_multiply(constant float* matrix, float3 value) {
  return float3(
      matrix[0] * value.r + matrix[1] * value.g + matrix[2] * value.b,
      matrix[3] * value.r + matrix[4] * value.g + matrix[5] * value.b,
      matrix[6] * value.r + matrix[7] * value.g + matrix[8] * value.b);
}

static float look_luminance(float3 rgb, constant LookStageUniforms& uniforms) {
  return rgb.r * uniforms.luminance_coefficients[0] +
         rgb.g * uniforms.luminance_coefficients[1] +
         rgb.b * uniforms.luminance_coefficients[2];
}

static float look_rgb_hue(float3 rgb) {
  const float3 nonnegative = max(rgb, float3(0.0f));
  const float maximum = max(nonnegative.r, max(nonnegative.g, nonnegative.b));
  const float minimum = min(nonnegative.r, min(nonnegative.g, nonnegative.b));
  const float delta = maximum - minimum;
  if (delta <= 1.0e-9f) return -1.0f;
  float hue = 0.0f;
  if (maximum == nonnegative.r) {
    hue = (nonnegative.g - nonnegative.b) / delta;
    if (hue < 0.0f) hue += 6.0f;
  } else if (maximum == nonnegative.g) {
    hue = (nonnegative.b - nonnegative.r) / delta + 2.0f;
  } else {
    hue = (nonnegative.r - nonnegative.g) / delta + 4.0f;
  }
  return hue / 6.0f;
}

static float look_channel_weight(float hue, uint channel) {
  float center = 0.0f;
  float inner = 0.05f;
  float outer = 0.10f;
  switch (channel) {
    case 1: center = 0.09f; inner = 0.04f; outer = 0.09f; break;
    case 2: center = 0.15f; inner = 0.04f; outer = 0.08f; break;
    case 3: center = 0.32f; inner = 0.10f; outer = 0.18f; break;
    case 4: center = 0.50f; inner = 0.06f; outer = 0.10f; break;
    case 5: center = 0.64f; inner = 0.08f; outer = 0.13f; break;
    case 6: center = 0.78f; inner = 0.06f; outer = 0.10f; break;
    case 7: center = 0.88f; inner = 0.05f; outer = 0.10f; break;
    default: break;
  }
  float distance = abs(hue - center);
  distance = min(distance, 1.0f - distance);
  if (distance <= inner) return 1.0f;
  if (distance >= outer) return 0.0f;
  return 1.0f - smoothstep(inner, outer, distance);
}

static float look_wrap_radians(float value) {
  value = fmod(value, 6.28318530717958647692f);
  return value < 0.0f ? value + 6.28318530717958647692f : value;
}

static float look_circular_weight(float value, float center, float radius) {
  float distance = abs(value - center);
  distance = min(distance, 1.0f - distance);
  if (distance >= radius) return 0.0f;
  const float unit = 1.0f - distance / radius;
  return unit * unit * (3.0f - 2.0f * unit);
}

static float look_feathered_weight(
    float value,
    float center,
    float radius,
    float softness) {
  float distance = abs(value - center);
  distance = min(distance, 1.0f - distance);
  if (distance >= radius) return 0.0f;
  const float feather = softness * radius;
  const float inner = radius - feather;
  if (distance <= inner || feather <= 1.0e-9f) return 1.0f;
  return 1.0f - smoothstep(inner, radius, distance);
}

static float look_shortest_angular_delta(float from, float to) {
  float delta = fmod(to - from, 6.28318530717958647692f);
  if (delta > 3.14159265358979323846f) delta -= 6.28318530717958647692f;
  if (delta < -3.14159265358979323846f) delta += 6.28318530717958647692f;
  return delta;
}

static float3 look_oklab_to_rgb(
    float lightness,
    float a,
    float b,
    constant LookStageUniforms& uniforms) {
  const float next_l = lightness + 0.3963377774f * a + 0.2158037573f * b;
  const float next_m = lightness - 0.1055613458f * a - 0.0638541728f * b;
  const float next_s = lightness - 0.0894841775f * a - 1.2914855480f * b;
  const float linear_l = next_l * next_l * next_l;
  const float linear_m = next_m * next_m * next_m;
  const float linear_s = next_s * next_s * next_s;
  return look_multiply(
      uniforms.xyz_d65_to_rgb,
      float3(
          1.2270138511f * linear_l - 0.5577999807f * linear_m + 0.2812561490f * linear_s,
          -0.0405801784f * linear_l + 1.1122568696f * linear_m - 0.0716766787f * linear_s,
          -0.0763812845f * linear_l - 0.4214819784f * linear_m + 1.5861632204f * linear_s));
}

static void look_apply_selective(
    float amount,
    float target_hue,
    float weight,
    thread float& chroma,
    thread float& hue) {
  if (abs(amount) <= 1.0e-9f) return;
  const float2 current = chroma * float2(cos(hue), sin(hue));
  const float direction = target_hue * 6.28318530717958647692f +
                          (amount < 0.0f ? 3.14159265358979323846f : 0.0f);
  const float2 next = current + abs(amount) * weight * 0.07f *
                      float2(cos(direction), sin(direction));
  chroma = length(next);
  hue = look_wrap_radians(atan2(next.y, next.x));
}

static float3 look_apply_saturation_vibrance(
    float3 input,
    constant LookStageUniforms& uniforms) {
  const float spread = max(
      abs(input.r - input.g), max(abs(input.g - input.b), abs(input.r - input.b)));
  if (spread <= 1.0e-6f && (uniforms.active_mask & 8u) == 0u) return input;
  const float3 xyz = look_multiply(uniforms.rgb_to_xyz_d65, input);
  const float l = 0.8189330101f * xyz.x + 0.3618667424f * xyz.y - 0.1288597137f * xyz.z;
  const float m = 0.0329845436f * xyz.x + 0.9293118715f * xyz.y + 0.0361456387f * xyz.z;
  const float s = 0.0482003018f * xyz.x + 0.2643662691f * xyz.y + 0.6338517070f * xyz.z;
  const float l_root = retained_blend_cbrt(l), m_root = retained_blend_cbrt(m), s_root = retained_blend_cbrt(s);
  float lightness = clamp(
      0.2104542553f * l_root + 0.7936177850f * m_root - 0.0040720468f * s_root,
      0.0f, retained_blend_cbrt(uniforms.output_headroom));
  float a = 1.9779984951f * l_root - 2.4285922050f * m_root + 0.4505937099f * s_root;
  float b = 0.0259040371f * l_root + 0.7827717662f * m_root - 0.8086757660f * s_root;
  const float original_chroma = length(float2(a, b));
  float chroma = original_chroma <= 1.0e-4f ? 0.0f : original_chroma;
  float skin_scale = 1.0f;
  if ((uniforms.active_mask & 256u) != 0u) {
    const float skin_hue = look_wrap_radians(atan2(b, a)) /
                           6.28318530717958647692f;
    const float skin_weight = look_circular_weight(
        skin_hue, uniforms.skin_hue, uniforms.skin_range) *
        smoothstep(0.02f, 0.12f, original_chroma) * uniforms.skin_strength;
    skin_scale = 1.0f - 0.95f * skin_weight;
  }
  const float low_chroma_weight = 1.0f - smoothstep(0.035f, 0.24f, chroma);
  chroma *= max(0.0f, 1.0f + uniforms.vibrance_strength *
      low_chroma_weight * skin_scale * 0.82f);
  float chroma_scale = original_chroma > 1.0e-9f ? chroma / original_chroma : 0.0f;
  a *= chroma_scale;
  b *= chroma_scale;
  if ((uniforms.active_mask & 32u) != 0u) {
    chroma = length(float2(a, b));
    float hue = look_wrap_radians(atan2(b, a));
    const float selection_hue = uniforms.mixer_model == 1u || uniforms.mixer_model == 2u
                                    ? look_rgb_hue(input)
                                    : hue / 6.28318530717958647692f;
    if (selection_hue >= 0.0f) {
      const float hue_scale = uniforms.mixer_model == 2u ? 0.16f : 0.22f;
      const float saturation_scale = uniforms.mixer_model == 3u ? 0.72f :
                                     uniforms.mixer_model == 2u ? 0.92f : 1.15f;
      const float luminance_scale = uniforms.mixer_model == 2u ? 0.14f :
                                    uniforms.mixer_model == 3u ? 0.20f : 0.30f;
      for (uint channel = 0u; channel < 8u; ++channel) {
        const float weight = look_channel_weight(selection_hue, channel) * skin_scale;
        if (weight <= 0.0f) continue;
        hue = look_wrap_radians(
            hue + weight * uniforms.mixer_hue[channel] * hue_scale *
                      6.28318530717958647692f);
        chroma *= max(0.0f, 1.0f + weight *
            uniforms.mixer_saturation[channel] * saturation_scale);
        lightness += weight * uniforms.mixer_luminance[channel] * luminance_scale;
        look_apply_selective(uniforms.selective_cyan[channel], 0.50f, weight, chroma, hue);
        look_apply_selective(uniforms.selective_magenta[channel], 0.88f, weight, chroma, hue);
        look_apply_selective(uniforms.selective_yellow[channel], 0.15f, weight, chroma, hue);
        lightness -= weight * uniforms.selective_black[channel] * 0.18f;
      }
      a = chroma * cos(hue);
      b = chroma * sin(hue);
    }
  }
  float replacement_luminance = -1.0f;
  float replacement_weight = 0.0f;
  if ((uniforms.active_mask & 128u) != 0u) {
    const float selection_hue = look_rgb_hue(input);
    if (selection_hue >= 0.0f) {
      const float weight = look_feathered_weight(
          selection_hue, uniforms.replacement_source_hue,
          uniforms.replacement_range, uniforms.replacement_softness) *
          uniforms.replacement_strength * skin_scale;
      if (weight > 1.0e-6f) {
        chroma = length(float2(a, b));
        float hue = look_wrap_radians(atan2(b, a));
        if (uniforms.replacement_preserve_luminance != 0u) {
          replacement_luminance = look_luminance(
              look_oklab_to_rgb(lightness, a, b, uniforms), uniforms);
          replacement_weight = weight;
        }
        hue = look_wrap_radians(hue + look_shortest_angular_delta(
            hue, uniforms.replacement_target_oklab_hue) * weight);
        if (uniforms.replacement_preserve_saturation == 0u) {
          chroma = max(chroma, mix(chroma, 0.16f, weight));
        }
        a = chroma * cos(hue);
        b = chroma * sin(hue);
      }
    }
  }
  if ((uniforms.active_mask & 64u) != 0u) {
    chroma = length(float2(a, b));
    float hue = look_wrap_radians(atan2(b, a));
    const float normalized_hue = hue / 6.28318530717958647692f;
    float best = uniforms.harmony_anchors[0];
    float best_distance = 1.0f;
    for (uint anchor = 0u; anchor < uniforms.harmony_anchor_count; ++anchor) {
      float distance = abs(normalized_hue - uniforms.harmony_anchors[anchor]);
      distance = min(distance, 1.0f - distance);
      if (distance < best_distance) {
        best_distance = distance;
        best = uniforms.harmony_anchors[anchor];
      }
    }
    float delta = fmod(best * 6.28318530717958647692f - hue +
                       3.14159265358979323846f,
                       6.28318530717958647692f);
    if (delta < 0.0f) delta += 6.28318530717958647692f;
    delta -= 3.14159265358979323846f;
    hue = look_wrap_radians(hue + delta * uniforms.harmony_strength * skin_scale);
    a = chroma * cos(hue);
    b = chroma * sin(hue);
  }
  if ((uniforms.active_mask & 8u) != 0u) {
    const float balance_shift = uniforms.grading_balance * 0.18f;
    const float selection_lightness = clamp(lightness, 0.0f, 1.0f);
    const float shadow_weight = 1.0f - smoothstep(
        0.15f - balance_shift, 0.45f - balance_shift, selection_lightness);
    const float highlight_weight = smoothstep(
        0.55f - balance_shift, 0.85f - balance_shift, selection_lightness);
    const float weights[3] = {
        shadow_weight,
        max(0.0f, 1.0f - shadow_weight - highlight_weight),
        highlight_weight};
    for (uint grade = 0u; grade < 3u; ++grade) {
      const float scaled_weight = weights[grade] * uniforms.grading_amount * skin_scale;
      const float intensity = abs(uniforms.grading_saturation[grade]) *
                              0.18f * scaled_weight;
      const float hue_radians = uniforms.grading_hue[grade] *
                                6.28318530717958647692f;
      a += intensity * cos(hue_radians);
      b += intensity * sin(hue_radians);
      lightness += uniforms.grading_luminance[grade] * 0.20f * scaled_weight;
    }
    chroma = length(float2(a, b));
  }
  chroma *= uniforms.saturation_strength >= 0.0f
      ? max(0.0f, 1.0f + uniforms.saturation_strength * 0.85f)
      : max(0.0f, 1.0f + uniforms.saturation_strength);
  const float graded_chroma = length(float2(a, b));
  chroma_scale = graded_chroma > 1.0e-9f ? chroma / graded_chroma : 0.0f;
  a *= chroma_scale;
  b *= chroma_scale;
  float3 output = look_oklab_to_rgb(lightness, a, b, uniforms);
  if (replacement_luminance >= 0.0f && replacement_weight > 1.0e-6f) {
    const float source_luminance = look_luminance(output, uniforms);
    const float target = mix(
        source_luminance,
        clamp(replacement_luminance, 0.0f, uniforms.output_headroom),
        clamp(replacement_weight, 0.0f, 1.0f));
    const float maximum = max(abs(output.r), max(abs(output.g), abs(output.b)));
    float3 adjusted = output;
    if (maximum <= 1.0e-9f) {
      adjusted = float3(max(0.0f, target));
    } else if (source_luminance > 1.0e-6f) {
      adjusted *= max(0.0f, target) / source_luminance;
    }
    const float adjusted_luminance = max(0.0f, look_luminance(adjusted, uniforms));
    const float nonnegative_source = max(0.0f, source_luminance);
    const float lift_stops = log2(
        (adjusted_luminance + 1.0e-6f) / (nonnegative_source + 1.0e-6f));
    if (lift_stops > 0.0f) {
      const float spread = max(
          abs(output.r - output.g),
          max(abs(output.g - output.b), abs(output.r - output.b)));
      const float attenuation = clamp(
          (1.0f - smoothstep(0.012f, 0.16f, nonnegative_source)) *
          smoothstep(0.12f, 1.20f, lift_stops) *
          (1.0f - smoothstep(0.025f, 0.12f, spread)) * 0.56f,
          0.0f, 0.56f);
      adjusted = adjusted_luminance +
                 (adjusted - adjusted_luminance) * (1.0f - attenuation);
    }
    output = adjusted;
  }
  if (uniforms.saturation_strength <= -0.999999f &&
      (uniforms.active_mask & (16u | 128u)) == 0u) {
    output = float3(look_luminance(output, uniforms));
  }
  return output;
}

kernel void apply_compiled_look_stage(
    device float4* pixels [[buffer(0)]],
    constant LookStageUniforms& uniforms [[buffer(1)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.pixel_count) return;
  const float alpha = pixels[gid].a;
  float3 adjusted = pixels[gid].rgb;
  if ((uniforms.active_mask & 1u) != 0u) {
    adjusted = max(float3(0.0f), look_multiply(uniforms.rgb_mixer, adjusted));
  }
  if ((uniforms.active_mask & 2u) != 0u) {
    float gray = look_luminance(adjusted, uniforms);
    const float hue = look_rgb_hue(adjusted);
    if (hue >= 0.0f) {
      for (uint channel = 0u; channel < 8u; ++channel) {
        gray *= max(0.0f, 1.0f + look_channel_weight(hue, channel) *
                                  uniforms.black_and_white_mixer[channel] * 0.85f);
      }
    }
    adjusted = float3(max(0.0f, gray));
  }
  if ((uniforms.active_mask & (4u | 8u | 32u | 64u | 128u)) != 0u) {
    adjusted = look_apply_saturation_vibrance(adjusted, uniforms);
  }
  pixels[gid] = float4(adjusted, alpha);
}

static float clamp_unit(float v) { return clamp(v, 0.0f, 1.0f); }

static float luminance(float3 rgb) {
  return rgb.r * 0.2126f + rgb.g * 0.7152f + rgb.b * 0.0722f;
}

// Trilinear 3D LUT interpolation
static float4 apply_lut_3d(
    float3 rgb,
    device const float4* lut,
    int lut_size) {
  float size_m1 = float(lut_size - 1);
  float rf = clamp_unit(rgb.r) * size_m1;
  float gf = clamp_unit(rgb.g) * size_m1;
  float bf = clamp_unit(rgb.b) * size_m1;
  int ri = min(int(rf), lut_size - 2);
  int gi = min(int(gf), lut_size - 2);
  int bi = min(int(bf), lut_size - 2);
  float fr = rf - float(ri);
  float fg = gf - float(gi);
  float fb = bf - float(bi);

  // 8-corner trilinear interpolation
  float4 c000 = lut[(ri * lut_size + gi) * lut_size + bi];
  float4 c001 = lut[(ri * lut_size + gi) * lut_size + bi + 1];
  float4 c010 = lut[(ri * lut_size + (gi + 1)) * lut_size + bi];
  float4 c011 = lut[(ri * lut_size + (gi + 1)) * lut_size + bi + 1];
  float4 c100 = lut[((ri + 1) * lut_size + gi) * lut_size + bi];
  float4 c101 = lut[((ri + 1) * lut_size + gi) * lut_size + bi + 1];
  float4 c110 = lut[((ri + 1) * lut_size + (gi + 1)) * lut_size + bi];
  float4 c111 = lut[((ri + 1) * lut_size + (gi + 1)) * lut_size + bi + 1];

  float4 c00 = mix(c000, c001, fb);
  float4 c01 = mix(c010, c011, fb);
  float4 c10 = mix(c100, c101, fb);
  float4 c11 = mix(c110, c111, fb);
  float4 c0 = mix(c00, c01, fg);
  float4 c1 = mix(c10, c11, fg);
  return mix(c0, c1, fr);
}

// Fast path: apply precomputed 3D LUT (no masks, no spatial effects)
kernel void apply_color_lut(
    device float4* pixels [[buffer(0)]],
    device const float4* lut [[buffer(1)]],
    constant ColorAdjustmentUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  float4 pixel = pixels[gid];
  float4 result = apply_lut_3d(pixel.rgb, lut, uniforms.lut_size);
  pixels[gid] = float4(result.rgb, pixel.a);
}

static float3 user_lut_multiply(constant float* matrix, float3 value) {
  return float3(
      matrix[0] * value.r + matrix[1] * value.g + matrix[2] * value.b,
      matrix[3] * value.r + matrix[4] * value.g + matrix[5] * value.b,
      matrix[6] * value.r + matrix[7] * value.g + matrix[8] * value.b);
}

static float user_lut_signed_power(float value, float exponent) {
  return copysign(pow(abs(value), exponent), value);
}

static float user_lut_encode_transfer(float value, uint transfer) {
  const float magnitude = abs(value);
  if (transfer == 2u) {
    const float encoded = magnitude <= 0.0031308f ? magnitude * 12.92f
        : 1.055f * pow(magnitude, 1.0f / 2.4f) - 0.055f;
    return copysign(encoded, value);
  }
  if (transfer == 3u) return user_lut_signed_power(value, 1.0f / 1.8f);
  if (transfer == 4u) return user_lut_signed_power(value, 1.0f / 2.2f);
  if (transfer == 5u) return user_lut_signed_power(value, 1.0f / 2.4f);
  if (transfer == 6u) {
    constexpr float alpha = 1.09929682680944f;
    constexpr float beta = 0.018053968510807f;
    const float encoded = magnitude < beta ? 4.5f * magnitude
        : alpha * pow(magnitude, 0.45f) - (alpha - 1.0f);
    return copysign(encoded, value);
  }
  if (transfer == 7u) return value <= 0.0078125f
      ? 10.5402377416545f * value + 0.0729055341958355f
      : (log2(value) + 9.72f) / 17.52f;
  return value;
}

static float user_lut_decode_transfer(float value, uint transfer) {
  const float magnitude = abs(value);
  if (transfer == 2u) {
    const float decoded = magnitude <= 0.04045f ? magnitude / 12.92f
        : pow((magnitude + 0.055f) / 1.055f, 2.4f);
    return copysign(decoded, value);
  }
  if (transfer == 3u) return user_lut_signed_power(value, 1.8f);
  if (transfer == 4u) return user_lut_signed_power(value, 2.2f);
  if (transfer == 5u) return user_lut_signed_power(value, 2.4f);
  if (transfer == 6u) {
    constexpr float alpha = 1.09929682680944f;
    constexpr float beta = 0.018053968510807f;
    const float decoded = magnitude < 4.5f * beta ? magnitude / 4.5f
        : pow((magnitude + alpha - 1.0f) / alpha, 1.0f / 0.45f);
    return copysign(decoded, value);
  }
  if (transfer == 7u) {
    constexpr float maximum_encoded = 1.4679963120447153f;
    if (value <= 0.155251141552511f) {
      return (value - 0.0729055341958355f) / 10.5402377416545f;
    }
    return value < maximum_encoded ? exp2(value * 17.52f - 9.72f) : 65504.0f;
  }
  return value;
}

kernel void apply_user_lut_stage(
    device float4* pixels [[buffer(0)]],
    device const float4* lut [[buffer(1)]],
    constant UserLutStageUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.pixel_count) return;
  const float4 source = pixels[gid];
  float3 lut_input = user_lut_multiply(uniforms.working_to_input, source.rgb);
  lut_input = float3(
      user_lut_encode_transfer(lut_input.r, uniforms.input_transfer),
      user_lut_encode_transfer(lut_input.g, uniforms.input_transfer),
      user_lut_encode_transfer(lut_input.b, uniforms.input_transfer));
  const float3 domain_min = float3(
      uniforms.domain_min[0], uniforms.domain_min[1], uniforms.domain_min[2]);
  const float3 domain_max = float3(
      uniforms.domain_max[0], uniforms.domain_max[1], uniforms.domain_max[2]);
  const float3 normalized = clamp(
      (lut_input - domain_min) / (domain_max - domain_min), 0.0f, 1.0f);
  float3 lut_output = apply_lut_3d(
      normalized, lut, int(uniforms.lut_size)).rgb;
  const float spread = max(
      abs(lut_input.r - lut_input.g),
      max(abs(lut_input.g - lut_input.b), abs(lut_input.r - lut_input.b)));
  if (spread < 1.0e-9f) {
    lut_output = float3((lut_output.r + lut_output.g + lut_output.b) / 3.0f);
  }
  lut_output = float3(
      user_lut_decode_transfer(lut_output.r, uniforms.output_transfer),
      user_lut_decode_transfer(lut_output.g, uniforms.output_transfer),
      user_lut_decode_transfer(lut_output.b, uniforms.output_transfer));
  const float3 graded = user_lut_multiply(
      uniforms.output_to_working, lut_output);
  pixels[gid] = float4(mix(source.rgb, graded, uniforms.strength), source.a);
}

static float geometry_mirrored_coordinate(float value, uint extent) {
  if (extent <= 1u) return 0.0f;
  const float maximum = float(extent - 1u);
  const float period = maximum * 2.0f;
  float mirrored = fmod(value, period);
  if (mirrored < 0.0f) mirrored += period;
  return mirrored <= maximum ? mirrored : period - mirrored;
}

static float4 geometry_sample_bilinear(
    device const float4* source,
    float x,
    float y,
    constant GeometryStageUniforms& uniforms) {
  if (uniforms.content_aware_crop_fill != 0u) {
    x = geometry_mirrored_coordinate(x, uniforms.width);
    y = geometry_mirrored_coordinate(y, uniforms.height);
  }
  x = clamp(x, 0.0f, float(uniforms.width - 1u));
  y = clamp(y, 0.0f, float(uniforms.height - 1u));
  const uint x0 = uint(floor(x));
  const uint y0 = uint(floor(y));
  const uint x1 = min(uniforms.width - 1u, x0 + 1u);
  const uint y1 = min(uniforms.height - 1u, y0 + 1u);
  const float fx = x - float(x0);
  const float fy = y - float(y0);
  const float4 top = mix(
      source[y0 * uniforms.width + x0],
      source[y0 * uniforms.width + x1],
      fx);
  const float4 bottom = mix(
      source[y1 * uniforms.width + x0],
      source[y1 * uniforms.width + x1],
      fx);
  return mix(top, bottom, fy);
}

kernel void apply_geometry_stage(
    device const float4* source [[buffer(0)]],
    device float4* destination [[buffer(1)]],
    constant GeometryStageUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  const uint pixel_count = uniforms.width * uniforms.height;
  if (gid >= pixel_count) return;
  const uint x = gid % uniforms.width;
  const uint y = gid / uniforms.width;
  float dx = float(x) - uniforms.center_x;
  float dy = float(y) - uniforms.center_y;
  if (abs(uniforms.transform_rotation) > 1.0e-6f ||
      abs(uniforms.transform_scale - 1.0f) > 1.0e-6f ||
      abs(uniforms.transform_skew_x) > 1.0e-6f ||
      abs(uniforms.transform_skew_y) > 1.0e-6f) {
    dx /= uniforms.transform_scale;
    dy /= uniforms.transform_scale;
    const float skewed_dx = dx + dy * uniforms.transform_skew_x;
    const float skewed_dy = dy + dx * uniforms.transform_skew_y;
    const float cosine = cos(-uniforms.transform_rotation);
    const float sine = sin(-uniforms.transform_rotation);
    dx = skewed_dx * cosine - skewed_dy * sine;
    dy = skewed_dx * sine + skewed_dy * cosine;
  }
  const float radius = length(float2(dx, dy)) / uniforms.maximum_radius;
  const float radius_squared = radius * radius;
  const float distortion_scale = 1.0f / max(
      0.40f,
      1.0f + uniforms.distortion_strength *
                 (0.55f * radius_squared +
                  0.20f * radius_squared * radius_squared));
  const float normalized_dx = uniforms.center_x > 0.0f
      ? dx / uniforms.center_x : 0.0f;
  const float normalized_dy = uniforms.center_y > 0.0f
      ? dy / uniforms.center_y : 0.0f;
  const float vertical_scale = max(
      0.35f,
      1.0f + uniforms.vertical_perspective * normalized_dy * 0.45f);
  const float horizontal_scale = max(
      0.35f,
      1.0f + uniforms.horizontal_perspective * normalized_dx * 0.45f);
  float sample_x = uniforms.center_x +
                   dx * distortion_scale * vertical_scale;
  float sample_y = uniforms.center_y +
                   dy * distortion_scale * horizontal_scale;
  if (abs(uniforms.liquify_strength) > 1.0e-6f) {
    const float liquify_dx = sample_x - uniforms.liquify_center_x;
    const float liquify_dy = sample_y - uniforms.liquify_center_y;
    const float liquify_distance = length(float2(liquify_dx, liquify_dy));
    if (liquify_distance < uniforms.liquify_radius) {
      const float falloff = 1.0f - smoothstep(
          0.0f,
          1.0f,
          liquify_distance / max(1.0f, uniforms.liquify_radius));
      const float liquify_scale = clamp(
          1.0f - uniforms.liquify_strength * 0.55f * falloff,
          0.30f,
          2.50f);
      sample_x = uniforms.liquify_center_x + liquify_dx * liquify_scale;
      sample_y = uniforms.liquify_center_y + liquify_dy * liquify_scale;
    }
  }
  float4 corrected = geometry_sample_bilinear(
      source, sample_x, sample_y, uniforms);
  if (uniforms.chromatic_aberration_strength > 1.0e-6f &&
      radius > 1.0e-6f) {
    const float inverse_length = 1.0f / max(1.0e-6f, length(float2(dx, dy)));
    const float radial_x = dx * inverse_length;
    const float radial_y = dy * inverse_length;
    const float shift = uniforms.chromatic_aberration_strength *
                        radius_squared * 2.2f;
    const float4 red_sample = geometry_sample_bilinear(
        source,
        sample_x - radial_x * shift * 1.15f,
        sample_y - radial_y * shift * 1.15f,
        uniforms);
    const float4 blue_sample = geometry_sample_bilinear(
        source,
        sample_x - radial_x * shift * 0.85f,
        sample_y - radial_y * shift * 0.85f,
        uniforms);
    corrected.r = red_sample.r;
    corrected.b = blue_sample.b;
  }
  destination[gid] = float4(corrected.rgb, clamp(corrected.a, 0.0f, 1.0f));
}

static float retained_spatial_luminance(
    float3 rgb,
    constant RetainedSpatialDetailUniforms& uniforms) {
  return rgb.r * uniforms.luminance_red +
         rgb.g * uniforms.luminance_green +
         rgb.b * uniforms.luminance_blue;
}

static float retained_spatial_bell_weight(
    float value, float center, float radius) {
  if (radius <= 0.0f) return 0.0f;
  const float distance = abs(value - center) / radius;
  if (distance >= 1.0f) return 0.0f;
  const float amount = 1.0f - distance;
  return amount * amount * (3.0f - 2.0f * amount);
}

static float4 retained_spatial_scale_to_luminance(
    float4 source,
    float target,
    constant RetainedSpatialDetailUniforms& uniforms) {
  const float source_luminance = retained_spatial_luminance(
      source.rgb, uniforms);
  const float maximum_channel = max(source.r, max(source.g, source.b));
  float safe_target = max(0.0f, target);
  if (maximum_channel <= 1.0e-9f || source_luminance <= 1.0e-9f) {
    safe_target = min(safe_target, uniforms.output_headroom);
  } else {
    safe_target = min(
        safe_target,
        max(0.0f, uniforms.output_headroom *
                  source_luminance / maximum_channel));
  }
  const float maximum_magnitude = max(
      abs(source.r), max(abs(source.g), abs(source.b)));
  float3 adjusted = source.rgb;
  if (maximum_magnitude <= 1.0e-9f) {
    adjusted = float3(safe_target);
  } else if (source_luminance > 1.0e-6f) {
    adjusted *= safe_target / source_luminance;
  }
  const float adjusted_luminance = max(
      0.0f, retained_spatial_luminance(adjusted, uniforms));
  const float nonnegative_source = max(0.0f, source_luminance);
  const float lift_stops = log2(
      (adjusted_luminance + 1.0e-6f) /
      (nonnegative_source + 1.0e-6f));
  if (lift_stops > 0.0f) {
    const float spread = max(
        abs(source.r - source.g),
        max(abs(source.g - source.b), abs(source.r - source.b)));
    const float attenuation = clamp(
        (1.0f - smoothstep(0.012f, 0.16f, nonnegative_source)) *
        smoothstep(0.12f, 1.20f, lift_stops) *
        (1.0f - smoothstep(0.025f, 0.12f, spread)) * 0.56f,
        0.0f,
        0.56f);
    adjusted = adjusted_luminance +
               (adjusted - adjusted_luminance) * (1.0f - attenuation);
  }
  return float4(adjusted, source.a);
}

kernel void extract_retained_spatial_luminance(
    device const float4* pixels [[buffer(0)]],
    device float* luminance_buffer [[buffer(1)]],
    constant RetainedSpatialDetailUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.width * uniforms.height) return;
  luminance_buffer[gid] = retained_spatial_luminance(
      pixels[gid].rgb, uniforms);
}

kernel void apply_retained_spatial_detail(
    device float4* pixels [[buffer(0)]],
    device const float* luminance_buffer [[buffer(1)]],
    device const float* texture_blur [[buffer(2)]],
    device const float* clarity_blur [[buffer(3)]],
    device const float* haze_blur [[buffer(4)]],
    constant RetainedSpatialDetailUniforms& uniforms [[buffer(5)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.width * uniforms.height) return;
  const uint x = gid % uniforms.width;
  const uint y = gid / uniforms.width;
  const float luminance = luminance_buffer[gid];
  float local_activity = 0.0f;
  if (x > 0u) local_activity = max(
      local_activity, abs(luminance - luminance_buffer[gid - 1u]));
  if (x + 1u < uniforms.width) local_activity = max(
      local_activity, abs(luminance - luminance_buffer[gid + 1u]));
  if (y > 0u) local_activity = max(
      local_activity, abs(luminance - luminance_buffer[gid - uniforms.width]));
  if (y + 1u < uniforms.height) local_activity = max(
      local_activity, abs(luminance - luminance_buffer[gid + uniforms.width]));
  const float midtone_weight = retained_spatial_bell_weight(
      luminance, 0.5f, 0.42f);
  const float shadow_guard = smoothstep(0.025f, 0.12f, luminance);
  const float highlight_headroom =
      1.0f - smoothstep(0.74f, 0.98f, luminance);
  const float tone_guard = shadow_guard * highlight_headroom;
  float target_luminance = luminance;
  if ((uniforms.active_mask & 1u) != 0u) {
    const float signal = luminance - texture_blur[gid];
    const float broad_edge_signal =
        (uniforms.active_mask & 2u) != 0u
            ? luminance - clarity_blur[gid]
            : local_activity;
    const float broad_edge_guard = 1.0f -
        smoothstep(0.08f, 0.36f, abs(broad_edge_signal)) * 0.72f;
    const float gain = uniforms.texture_strength >= 0.0f ? 1.30f : 0.48f;
    target_luminance += uniforms.texture_strength * signal *
        (0.46f + midtone_weight * 0.70f) * broad_edge_guard *
        tone_guard * gain;
  }
  if ((uniforms.active_mask & 2u) != 0u) {
    const float signal = luminance - clarity_blur[gid];
    const float edge_limit = 1.0f -
        smoothstep(0.24f, 0.58f, abs(signal)) * 0.28f;
    const float structure_gate = smoothstep(0.003f, 0.035f, local_activity);
    const float gain = uniforms.clarity_strength >= 0.0f ? 1.08f : 0.46f;
    target_luminance += uniforms.clarity_strength * signal *
        (0.36f + midtone_weight * 0.82f) * tone_guard *
        edge_limit * structure_gate * gain;
  }
  if ((uniforms.active_mask & 4u) != 0u) {
    const float signal = clamp(
        luminance - haze_blur[gid], -0.18f, 0.18f);
    const float activity_gate = smoothstep(0.0015f, 0.018f, local_activity);
    const float edge_guard = 1.0f -
        smoothstep(0.11f, 0.18f, abs(signal)) * 0.95f;
    const float gain = uniforms.dehaze_strength >= 0.0f ? 0.95f : 0.36f;
    target_luminance += uniforms.dehaze_strength * signal *
        (0.52f + midtone_weight * 0.78f) * highlight_headroom *
        activity_gate * edge_guard * gain;
    const float shadow_weight =
        1.0f - smoothstep(0.08f, 0.20f, luminance);
    const float structure_weight = smoothstep(0.008f, 0.065f, abs(signal)) *
        activity_gate * edge_guard;
    if (uniforms.dehaze_strength > 0.0f) {
      target_luminance *= max(
          0.0f,
          1.0f - uniforms.dehaze_strength * shadow_weight *
                     structure_weight * 0.52f);
      target_luminance *= max(
          0.0f,
          1.0f - uniforms.dehaze_strength * shadow_weight *
                     activity_gate * edge_guard * 0.035f);
    } else {
      target_luminance += (-uniforms.dehaze_strength) * shadow_weight *
                          structure_weight * 0.075f;
    }
  }
  pixels[gid] = retained_spatial_scale_to_luminance(
      pixels[gid], target_luminance, uniforms);
}

static float3 retained_detail_multiply(
    constant float* matrix, float3 value) {
  return float3(
      matrix[0] * value.r + matrix[1] * value.g + matrix[2] * value.b,
      matrix[3] * value.r + matrix[4] * value.g + matrix[5] * value.b,
      matrix[6] * value.r + matrix[7] * value.g + matrix[8] * value.b);
}

static float retained_detail_cbrt(float value) {
  return copysign(pow(abs(value), 1.0f / 3.0f), value);
}

static float3 retained_detail_rgb_to_oklab(
    float3 rgb,
    constant RetainedFineDetailUniforms& uniforms) {
  const float3 xyz = retained_detail_multiply(uniforms.rgb_to_xyz_d65, rgb);
  const float l = 0.8189330101f * xyz.x + 0.3618667424f * xyz.y -
                  0.1288597137f * xyz.z;
  const float m = 0.0329845436f * xyz.x + 0.9293118715f * xyz.y +
                  0.0361456387f * xyz.z;
  const float s = 0.0482003018f * xyz.x + 0.2643662691f * xyz.y +
                  0.6338517070f * xyz.z;
  const float lr = retained_detail_cbrt(l);
  const float mr = retained_detail_cbrt(m);
  const float sr = retained_detail_cbrt(s);
  return float3(
      0.2104542553f * lr + 0.7936177850f * mr - 0.0040720468f * sr,
      1.9779984951f * lr - 2.4285922050f * mr + 0.4505937099f * sr,
      0.0259040371f * lr + 0.7827717662f * mr - 0.8086757660f * sr);
}

static float3 retained_detail_oklab_to_rgb(
    float lightness,
    float a,
    float b,
    constant RetainedFineDetailUniforms& uniforms) {
  const float lr = lightness + 0.3963377774f * a + 0.2158037573f * b;
  const float mr = lightness - 0.1055613458f * a - 0.0638541728f * b;
  const float sr = lightness - 0.0894841775f * a - 1.2914855480f * b;
  const float l = lr * lr * lr;
  const float m = mr * mr * mr;
  const float s = sr * sr * sr;
  const float3 xyz = float3(
      1.2270138511f * l - 0.5577999807f * m + 0.2812561490f * s,
      -0.0405801784f * l + 1.1122568696f * m - 0.0716766787f * s,
      -0.0763812845f * l - 0.4214819784f * m + 1.5861632204f * s);
  return retained_detail_multiply(uniforms.xyz_d65_to_rgb, xyz);
}

kernel void extract_retained_fine_detail(
    device const float4* pixels [[buffer(0)]],
    device float* lightness [[buffer(1)]],
    device float* chroma_a [[buffer(2)]],
    device float* chroma_b [[buffer(3)]],
    constant RetainedFineDetailUniforms& uniforms [[buffer(4)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.width * uniforms.height) return;
  const float3 oklab = retained_detail_rgb_to_oklab(
      pixels[gid].rgb, uniforms);
  lightness[gid] = oklab.x;
  chroma_a[gid] = oklab.y;
  chroma_b[gid] = oklab.z;
}

kernel void apply_retained_sharpen(
    device float* lightness [[buffer(0)]],
    device const float* sharp_blur [[buffer(1)]],
    device const float* edge_blur [[buffer(2)]],
    constant RetainedFineDetailUniforms& uniforms [[buffer(3)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.width * uniforms.height) return;
  const float detail_signal = lightness[gid] - sharp_blur[gid];
  float mask_gate = 1.0f;
  if (uniforms.sharpen_mask_strength > 1.0e-6f) {
    const float edge_strength = abs(lightness[gid] - edge_blur[gid]);
    mask_gate = smoothstep(
        0.0015f,
        0.012f + 0.06f * uniforms.sharpen_mask_strength,
        edge_strength);
  }
  lightness[gid] += detail_signal * uniforms.sharpen_strength *
                    uniforms.sharpen_gain * mask_gate;
}

kernel void apply_retained_luminance_nr(
    device float* lightness [[buffer(0)]],
    device const float* nr_blur [[buffer(1)]],
    device const float* edge_blur [[buffer(2)]],
    constant RetainedFineDetailUniforms& uniforms [[buffer(3)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.width * uniforms.height) return;
  const float edge = abs(lightness[gid] - edge_blur[gid]);
  const float preserve = 1.0f - smoothstep(0.005f, 0.04f, edge);
  const float blend = uniforms.luminance_nr_strength * 0.85f * preserve;
  lightness[gid] = mix(lightness[gid], nr_blur[gid], blend);
}

kernel void apply_retained_color_nr(
    device float* chroma_a [[buffer(0)]],
    device float* chroma_b [[buffer(1)]],
    device const float* a_blur [[buffer(2)]],
    device const float* b_blur [[buffer(3)]],
    constant RetainedFineDetailUniforms& uniforms [[buffer(4)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.width * uniforms.height) return;
  const float blend = uniforms.color_nr_strength * 0.90f;
  chroma_a[gid] = mix(chroma_a[gid], a_blur[gid], blend);
  chroma_b[gid] = mix(chroma_b[gid], b_blur[gid], blend);
}

kernel void compose_retained_fine_detail(
    device float4* pixels [[buffer(0)]],
    device const float* lightness [[buffer(1)]],
    device const float* chroma_a [[buffer(2)]],
    device const float* chroma_b [[buffer(3)]],
    constant RetainedFineDetailUniforms& uniforms [[buffer(4)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.width * uniforms.height) return;
  pixels[gid] = float4(
      retained_detail_oklab_to_rgb(
          lightness[gid], chroma_a[gid], chroma_b[gid], uniforms),
      pixels[gid].a);
}

// Linear mask weight computation
static float linear_mask_weight(
    float2 normalized_pos,
    float position,
    float rotation_rad,
    float feather) {
  float cos_r = cos(rotation_rad);
  float sin_r = sin(rotation_rad);
  float axis = (normalized_pos.x - 0.5f) * cos_r + (normalized_pos.y - 0.5f) * sin_r + 0.5f;
  return clamp_unit((position - axis) / max(0.05f, feather) + 0.5f);
}

// Radial mask weight computation
static float radial_mask_weight(
    float2 normalized_pos,
    float2 center,
    float2 radius,
    float rotation_rad,
    float feather) {
  float cos_r = cos(rotation_rad);
  float sin_r = sin(rotation_rad);
  float2 translated = normalized_pos - center;
  float local_x = translated.x * cos_r + translated.y * sin_r;
  float local_y = -translated.x * sin_r + translated.y * cos_r;
  float2 safe_radius = max(radius, float2(0.05f));
  float dist = sqrt((local_x * local_x) / (safe_radius.x * safe_radius.x) +
                     (local_y * local_y) / (safe_radius.y * safe_radius.y));
  float inner = max(0.0f, 1.0f - clamp(feather, 0.05f, 1.0f));
  if (dist <= inner) return 1.0f;
  if (dist >= 1.0f) return 0.0f;
  float t = clamp_unit((dist - inner) / (1.0f - inner));
  return 1.0f - t * t * (3.0f - 2.0f * t);
}

struct MaskUniforms {
  int has_linear_mask;
  float linear_exposure;
  float linear_temperature;
  float linear_feather;
  float linear_position;
  float linear_rotation_rad;
  int has_radial_mask;
  float radial_exposure;
  float radial_temperature;
  float radial_feather;
  float radial_center_x;
  float radial_center_y;
  float radial_radius_x;
  float radial_radius_y;
  float radial_rotation_rad;
  float radial_wb_red;
  float radial_wb_green;
  float radial_wb_blue;
  float linear_wb_red;
  float linear_wb_green;
  float linear_wb_blue;
  int width;
  int height;
};

struct BlurUniforms {
  int width;
  int height;
  int radius;
};

struct GuidedFilterUniforms {
  uint pixel_count;
  float epsilon;
};

// Apply local adjustment masks (spatial, runs before LUT)
kernel void apply_masks(
    device float4* pixels [[buffer(0)]],
    constant MaskUniforms& uniforms [[buffer(1)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  int x = int(gid) % uniforms.width;
  int y = int(gid) / uniforms.width;
  float nx = uniforms.width > 1 ? float(x) / float(uniforms.width - 1) : 0.5f;
  float ny = uniforms.height > 1 ? float(y) / float(uniforms.height - 1) : 0.5f;

  float4 pixel = pixels[gid];

  if (uniforms.has_linear_mask) {
    float w = linear_mask_weight(float2(nx, ny), uniforms.linear_position,
                                 uniforms.linear_rotation_rad, uniforms.linear_feather);
    if (w > 0.0f) {
      float gain = pow(2.0f, uniforms.linear_exposure * w);
      pixel.r *= (1.0f + (uniforms.linear_wb_red - 1.0f) * w) * gain;
      pixel.g *= (1.0f + (uniforms.linear_wb_green - 1.0f) * w) * gain;
      pixel.b *= (1.0f + (uniforms.linear_wb_blue - 1.0f) * w) * gain;
    }
  }

  if (uniforms.has_radial_mask) {
    float w = radial_mask_weight(
        float2(nx, ny),
        float2(uniforms.radial_center_x, uniforms.radial_center_y),
        float2(uniforms.radial_radius_x, uniforms.radial_radius_y),
        uniforms.radial_rotation_rad, uniforms.radial_feather);
    if (w > 0.0f) {
      float gain = pow(2.0f, uniforms.radial_exposure * w);
      pixel.r *= (1.0f + (uniforms.radial_wb_red - 1.0f) * w) * gain;
      pixel.g *= (1.0f + (uniforms.radial_wb_green - 1.0f) * w) * gain;
      pixel.b *= (1.0f + (uniforms.radial_wb_blue - 1.0f) * w) * gain;
    }
  }

  pixels[gid] = pixel;
}

kernel void box_blur_horizontal(
    device const float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    constant BlurUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  const int x = int(gid) % uniforms.width;
  const int y = int(gid) / uniforms.width;
  const int kernel_width = uniforms.radius * 2 + 1;
  float sum = 0.0f;
  for (int sample = -uniforms.radius; sample <= uniforms.radius; ++sample) {
    const int clamped_x = clamp(x + sample, 0, uniforms.width - 1);
    sum += input[y * uniforms.width + clamped_x];
  }
  output[gid] = sum / float(kernel_width);
}

kernel void box_blur_vertical(
    device const float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    constant BlurUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  const int x = int(gid) % uniforms.width;
  const int y = int(gid) / uniforms.width;
  const int kernel_width = uniforms.radius * 2 + 1;
  float sum = 0.0f;
  for (int sample = -uniforms.radius; sample <= uniforms.radius; ++sample) {
    const int clamped_y = clamp(y + sample, 0, uniforms.height - 1);
    sum += input[clamped_y * uniforms.width + x];
  }
  output[gid] = sum / float(kernel_width);
}

kernel void retained_guided_square(
    device const float* input [[buffer(0)]],
    device float* correlation [[buffer(1)]],
    constant GuidedFilterUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.pixel_count) return;
  correlation[gid] = input[gid] * input[gid];
}

kernel void retained_guided_coefficients(
    device const float* input [[buffer(0)]],
    device const float* mean [[buffer(1)]],
    device const float* mean_correlation [[buffer(2)]],
    device float* coefficient_a [[buffer(3)]],
    device float* coefficient_b [[buffer(4)]],
    constant GuidedFilterUniforms& uniforms [[buffer(5)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.pixel_count) return;
  const float variance = max(
      0.0f, mean_correlation[gid] - mean[gid] * mean[gid]);
  const float a = variance /
      (variance + max(1.0e-8f, uniforms.epsilon));
  coefficient_a[gid] = a;
  coefficient_b[gid] = mean[gid] * (1.0f - a);
}

kernel void retained_guided_compose(
    device const float* input [[buffer(0)]],
    device const float* mean_a [[buffer(1)]],
    device const float* mean_b [[buffer(2)]],
    device float* output [[buffer(3)]],
    constant GuidedFilterUniforms& uniforms [[buffer(4)]],
    uint gid [[thread_position_in_grid]]) {
  if (gid >= uniforms.pixel_count) return;
  output[gid] = max(0.0f, mean_a[gid] * input[gid] + mean_b[gid]);
}

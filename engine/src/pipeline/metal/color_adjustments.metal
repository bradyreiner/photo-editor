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

struct DetailUniforms {
  float sharpen_strength;
  float sharpen_mask_strength;
  float luma_nr_strength;
  float color_nr_strength;
  int width;
  int height;
};

struct SpatialToneUniforms {
  float texture_strength;
  float clarity_strength;
  float dehaze_strength;
  int width;
  int height;
};

struct PostColorUniforms {
  float saturation_strength;
  float vibrance_strength;
  float dehaze_strength;
  float grading_hue[3];
  float grading_sat[3];
  float grading_lum[3];
  float grading_amount;
  float grading_balance;
  float mixer_hue[8];
  float mixer_sat[8];
  float mixer_lum[8];
  float selective_cyan[8];
  float selective_magenta[8];
  float selective_yellow[8];
  float selective_black[8];
  float channel_mixer[9];
  float black_and_white_mixer[8];
  int black_and_white_enabled;
  int color_replacement_enabled;
  float color_replacement_source_hue;
  float color_replacement_target_hue;
  float color_replacement_range;
  float color_replacement_softness;
  float color_replacement_strength;
  int color_replacement_preserve_luminance;
  int color_replacement_preserve_saturation;
  int skin_tone_protection_enabled;
  float skin_tone_line_hue;
  float skin_tone_protection_range;
  float skin_tone_protection_strength;
  int color_harmony_enabled;
  int color_harmony_mode;
  float color_harmony_base_hue;
  float color_harmony_strength;
  int use_master_curve;
  int use_luma_curve;
  int use_red_curve;
  int use_green_curve;
  int use_blue_curve;
  int curve_lut_size;
  int color_mixer_model;
  int highlight_chroma_compression_enabled;
  float output_headroom;
  int width;
  int height;
};

struct PreToneUniforms {
  float exposure_gain;
  float global_wb_red;
  float global_wb_green;
  float global_wb_blue;
  float contrast;
  float highlights;
  float shadows;
  float whites;
  float blacks;
  float brightness;
  float filmic_toe;
  float adaptive_contrast;
  float levels_black_point;
  float levels_gamma;
  float levels_white_point;
  float zone_deep_shadows;
  float zone_shadows;
  float zone_midtones;
  float zone_lights;
  float zone_highlights;
  float shoulder_threshold;
  int use_parametric_curve;
  int curve_lut_size;
  int has_linear_mask;
  float linear_exposure;
  float linear_feather;
  float linear_position;
  float linear_rotation_rad;
  float linear_wb_red;
  float linear_wb_green;
  float linear_wb_blue;
  int has_radial_mask;
  float radial_exposure;
  float radial_feather;
  float radial_center_x;
  float radial_center_y;
  float radial_radius_x;
  float radial_radius_y;
  float radial_rotation_rad;
  float radial_wb_red;
  float radial_wb_green;
  float radial_wb_blue;
  int has_local_mask;
  float local_exposure;
  float local_wb_red;
  float local_wb_green;
  float local_wb_blue;
  int width;
  int height;
};

struct GamutFitResult {
  float lightness;
  float chroma;
  float4 pixel;
};

static float wrap_radians(float value) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  value = fmod(value, kTwoPi);
  if (value < 0.0f) {
    value += kTwoPi;
  }
  return value;
}

static float shortest_angular_delta(float from, float to) {
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kTwoPi = 6.28318530717958647692f;
  float delta = fmod(to - from + kPi, kTwoPi);
  if (delta < 0.0f) {
    delta += kTwoPi;
  }
  return delta - kPi;
}

static float wrap_unit(float value) {
  value -= floor(value);
  if (value < 0.0f) {
    value += 1.0f;
  }
  return value;
}

static float rgb_hue_for_selection(float3 rgb) {
  rgb = max(rgb, float3(0.0f));
  const float maximum = max(rgb.r, max(rgb.g, rgb.b));
  const float minimum = min(rgb.r, min(rgb.g, rgb.b));
  const float delta = maximum - minimum;
  if (delta <= 1.0e-9f) {
    return -1.0f;
  }

  float hue = 0.0f;
  if (maximum == rgb.r) {
    hue = (rgb.g - rgb.b) / delta;
    if (hue < 0.0f) {
      hue += 6.0f;
    }
  } else if (maximum == rgb.g) {
    hue = ((rgb.b - rgb.r) / delta) + 2.0f;
  } else {
    hue = ((rgb.r - rgb.g) / delta) + 4.0f;
  }
  return wrap_unit(hue / 6.0f);
}

static void apply_selective_component(
    float amount,
    float target_hue_unit,
    float mixer_weight,
    thread float& chroma,
    thread float& hue) {
  const float control = clamp(amount, -100.0f, 100.0f) / 100.0f;
  if (fabs(control) <= 1.0e-9f || mixer_weight <= 1.0e-9f) {
    return;
  }

  const float direction = target_hue_unit * 6.28318530717958647692f +
      (control < 0.0f ? 3.14159265358979323846f : 0.0f);
  const float strength = fabs(control) * mixer_weight * 0.07f;
  const float next_a = chroma * cos(hue) + cos(direction) * strength;
  const float next_b = chroma * sin(hue) + sin(direction) * strength;
  chroma = length(float2(next_a, next_b));
  hue = wrap_radians(atan2(next_b, next_a));
}

static void apply_selective_color(
    float cyan,
    float magenta,
    float yellow,
    float black,
    float mixer_weight,
    thread float& lightness,
    thread float& chroma,
    thread float& hue) {
  apply_selective_component(cyan, 0.50f, mixer_weight, chroma, hue);
  apply_selective_component(magenta, 0.88f, mixer_weight, chroma, hue);
  apply_selective_component(yellow, 0.15f, mixer_weight, chroma, hue);
  lightness = clamp_unit(lightness - mixer_weight * clamp(black, -100.0f, 100.0f) / 100.0f * 0.18f);
}

static float3 linear_rgb_to_oklab(float3 rgb) {
  rgb = max(rgb, float3(0.0f));
  const float l = 0.4122214708f * rgb.r + 0.5363325363f * rgb.g + 0.0514459929f * rgb.b;
  const float m = 0.2119034982f * rgb.r + 0.6806995451f * rgb.g + 0.1073969566f * rgb.b;
  const float s = 0.0883024619f * rgb.r + 0.2817188376f * rgb.g + 0.6299787005f * rgb.b;
  const float l_root = pow(max(0.0f, l), 1.0f / 3.0f);
  const float m_root = pow(max(0.0f, m), 1.0f / 3.0f);
  const float s_root = pow(max(0.0f, s), 1.0f / 3.0f);
  return float3(
      0.2104542553f * l_root + 0.7936177850f * m_root - 0.0040720468f * s_root,
      1.9779984951f * l_root - 2.4285922050f * m_root + 0.4505937099f * s_root,
      0.0259040371f * l_root + 0.7827717662f * m_root - 0.8086757660f * s_root);
}

static float hue_to_rgb_component(float p, float q, float t) {
  if (t < 0.0f) {
    t += 1.0f;
  }
  if (t > 1.0f) {
    t -= 1.0f;
  }
  if (t < 1.0f / 6.0f) {
    return p + (q - p) * 6.0f * t;
  }
  if (t < 0.5f) {
    return q;
  }
  if (t < 2.0f / 3.0f) {
    return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
  }
  return p;
}

static float3 hsl_to_rgb(float hue, float saturation, float lightness) {
  hue = wrap_unit(hue);
  saturation = clamp_unit(saturation);
  lightness = clamp_unit(lightness);
  if (saturation <= 1.0e-9f) {
    return float3(lightness);
  }

  const float q = lightness < 0.5f
      ? lightness * (1.0f + saturation)
      : lightness + saturation - lightness * saturation;
  const float p = 2.0f * lightness - q;
  return float3(
      clamp_unit(hue_to_rgb_component(p, q, hue + 1.0f / 3.0f)),
      clamp_unit(hue_to_rgb_component(p, q, hue)),
      clamp_unit(hue_to_rgb_component(p, q, hue - 1.0f / 3.0f)));
}

static float oklab_hue_for_rgb_hue(float hue_unit) {
  const float3 target_rgb = hsl_to_rgb(hue_unit, 1.0f, 0.5f);
  const float3 target_oklab = linear_rgb_to_oklab(target_rgb);
  return wrap_radians(atan2(target_oklab.z, target_oklab.y));
}

static float3 oklab_to_linear_rgb(float lightness, float a, float b) {
  const float l_root = lightness + 0.3963377774f * a + 0.2158037573f * b;
  const float m_root = lightness - 0.1055613458f * a - 0.0638541728f * b;
  const float s_root = lightness - 0.0894841775f * a - 1.2914855480f * b;
  const float l = l_root * l_root * l_root;
  const float m = m_root * m_root * m_root;
  const float s = s_root * s_root * s_root;
  return float3(
      +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
      -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
      -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
}

static bool in_gamut(float3 rgb) {
  return all(rgb >= float3(-1.0e-6f)) && all(rgb <= float3(1.0f + 1.0e-6f));
}

static float4 clamp_rgb(float4 pixel) {
  return float4(clamp_unit(pixel.r), clamp_unit(pixel.g), clamp_unit(pixel.b), clamp_unit(pixel.a));
}

static GamutFitResult search_chroma_at_lightness(float candidate_lightness, float candidate_chroma, float hue, float alpha, int iterations) {
  candidate_lightness = clamp_unit(candidate_lightness);
  candidate_chroma = max(0.0f, candidate_chroma);

  float3 candidate_rgb = oklab_to_linear_rgb(
      candidate_lightness,
      candidate_chroma * cos(hue),
      candidate_chroma * sin(hue));
  if (in_gamut(candidate_rgb)) {
    return GamutFitResult{
        candidate_lightness,
        candidate_chroma,
        clamp_rgb(float4(candidate_rgb, alpha)),
    };
  }

  float low = 0.0f;
  float high = candidate_chroma;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    const float mid = (low + high) * 0.5f;
    candidate_rgb = oklab_to_linear_rgb(
        candidate_lightness,
        mid * cos(hue),
        mid * sin(hue));
    if (in_gamut(candidate_rgb)) {
      low = mid;
    } else {
      high = mid;
    }
  }

  candidate_rgb = oklab_to_linear_rgb(
      candidate_lightness,
      low * cos(hue),
      low * sin(hue));
  return GamutFitResult{
      candidate_lightness,
      low,
      clamp_rgb(float4(candidate_rgb, alpha)),
  };
}

static float4 gamut_fit_oklch(float lightness, float chroma, float hue, float alpha) {
  lightness = clamp_unit(lightness);
  chroma = max(0.0f, chroma);
  hue = wrap_radians(hue);

  GamutFitResult best_fit = search_chroma_at_lightness(lightness, chroma, hue, alpha, 16);
  if (best_fit.chroma > max(1.0e-4f, chroma * 0.02f)) {
    return best_fit.pixel;
  }

  constexpr float kTargetLightness = 0.5f;
  for (int iteration = 1; iteration <= 12; ++iteration) {
    const float t = float(iteration) / 12.0f;
    const float candidate_lightness = mix(lightness, kTargetLightness, t);
    const GamutFitResult candidate_fit =
        search_chroma_at_lightness(candidate_lightness, chroma, hue, alpha, 16);
    if (candidate_fit.chroma > best_fit.chroma + 1.0e-6f ||
        (fabs(candidate_fit.chroma - best_fit.chroma) <= 1.0e-6f &&
         fabs(candidate_fit.lightness - lightness) < fabs(best_fit.lightness - lightness))) {
      best_fit = candidate_fit;
    }
  }

  return best_fit.pixel;
}

static float bell_weight(float value, float center, float radius) {
  if (radius <= 0.0f) {
    return 0.0f;
  }

  const float distance = fabs(value - center) / radius;
  if (distance >= 1.0f) {
    return 0.0f;
  }

  const float t = 1.0f - distance;
  return t * t * (3.0f - 2.0f * t);
}

static float gentle_signed_response(float control) {
  const float clamped = clamp(control, -1.0f, 1.0f);
  return copysign(pow(fabs(clamped), 1.18f), clamped);
}

static float interpolate_tone_anchors(float stops, thread const float* input_stops, thread const float* output_stops) {
  if (stops <= input_stops[0]) {
    return stops + (output_stops[0] - input_stops[0]);
  }
  if (stops >= input_stops[6]) {
    return stops + (output_stops[6] - input_stops[6]) * 0.72f;
  }

  for (int index = 0; index < 6; ++index) {
    if (stops < input_stops[index] || stops > input_stops[index + 1]) {
      continue;
    }
    const float span = max(1.0e-6f, input_stops[index + 1] - input_stops[index]);
    const float t = clamp_unit((stops - input_stops[index]) / span);
    const float eased = t * t * (3.0f - 2.0f * t);
    const float left_delta = output_stops[index] - input_stops[index];
    const float right_delta = output_stops[index + 1] - input_stops[index + 1];
    return stops + mix(left_delta, right_delta, eased);
  }

  return stops;
}

static float apply_filmic_toe(float value, float amount) {
  constexpr float kMiddleGray = 0.18f;
  const float control = clamp(amount, -1.0f, 1.0f);
  const float safe_value = max(0.0f, value);
  const float shadow_weight = 1.0f - smoothstep(kMiddleGray * 0.35f, kMiddleGray * 2.0f, safe_value);
  if (shadow_weight <= 1.0e-6f || fabs(control) <= 1.0e-6f) {
    return safe_value;
  }
  if (control > 0.0f) {
    const float lifted = sqrt(safe_value * kMiddleGray);
    return mix(safe_value, lifted, control * shadow_weight * 0.72f);
  }
  return max(0.0f, safe_value * (1.0f - (-control) * 0.58f * shadow_weight));
}

static float apply_adaptive_contrast(float value, float amount) {
  constexpr float kMiddleGray = 0.18f;
  const float control = clamp(amount, -1.0f, 1.0f);
  if (fabs(control) <= 1.0e-6f) {
    return value;
  }
  const float safe_value = max(1.0e-6f, value);
  const float stops = log2(safe_value / kMiddleGray);
  const float midtone_weight = 1.0f - smoothstep(0.85f, 3.35f, fabs(stops));
  if (midtone_weight <= 1.0e-6f) {
    return value;
  }
  const float slope = clamp(1.0f + control * 0.42f * midtone_weight, 0.58f, 1.48f);
  const float contrasted = kMiddleGray * pow(safe_value / kMiddleGray, slope);
  const float detail_weight = clamp(midtone_weight * (0.42f + fabs(control) * 0.58f), 0.0f, 1.0f);
  return mix(safe_value, contrasted, detail_weight);
}

static float apply_levels(float value, constant PreToneUniforms& uniforms) {
  const float black_control = clamp(uniforms.levels_black_point, 0.0f, 1.0f);
  const float white_control = clamp(uniforms.levels_white_point, 0.0f, 1.0f);
  const float gamma_control = clamp(uniforms.levels_gamma, -1.0f, 1.0f);
  if (black_control <= 1.0e-6f && white_control <= 1.0e-6f && fabs(gamma_control) <= 1.0e-6f) {
    return value;
  }
  const float input_black = black_control * 0.95f;
  const float input_white = max(input_black + 0.01f, 1.0f - white_control * 0.95f);
  const float normalized = max(0.0f, (value - input_black) / (input_white - input_black));
  const float gamma = pow(2.0f, -gamma_control * 1.45f);
  return pow(normalized, gamma);
}

static float zone_exposure_delta(float stops, constant PreToneUniforms& uniforms) {
  float delta = 0.0f;
  delta += bell_weight(stops, -3.50f, 1.45f) * clamp(uniforms.zone_deep_shadows, -1.0f, 1.0f) * 0.95f;
  delta += bell_weight(stops, -1.70f, 1.35f) * clamp(uniforms.zone_shadows, -1.0f, 1.0f) * 0.90f;
  delta += bell_weight(stops, 0.00f, 1.50f) * clamp(uniforms.zone_midtones, -1.0f, 1.0f) * 0.82f;
  delta += bell_weight(stops, 1.70f, 1.35f) * clamp(uniforms.zone_lights, -1.0f, 1.0f) * 0.86f;
  delta += bell_weight(stops, 3.30f, 1.55f) * clamp(uniforms.zone_highlights, -1.0f, 1.0f) * 0.90f;
  return delta;
}

static float apply_zone_exposure(
    float adjusted_value,
    float source_value,
    constant PreToneUniforms& uniforms) {
  if (fabs(uniforms.zone_deep_shadows) <= 1.0e-6f &&
      fabs(uniforms.zone_shadows) <= 1.0e-6f &&
      fabs(uniforms.zone_midtones) <= 1.0e-6f &&
      fabs(uniforms.zone_lights) <= 1.0e-6f &&
      fabs(uniforms.zone_highlights) <= 1.0e-6f) {
    return adjusted_value;
  }

  constexpr float kMiddleGray = 0.18f;
  const float safe_value = max(1.0e-8f, source_value);
  const float stops = log2(safe_value / kMiddleGray);
  const float delta = zone_exposure_delta(stops, uniforms) * 1.15f;
  return max(0.0f, adjusted_value * exp2(delta));
}

static float apply_photographic_tone_curve(float value, constant PreToneUniforms& uniforms) {
  constexpr float kMiddleGray = 0.18f;
  if (fabs(uniforms.contrast) <= 1.0e-6f &&
      fabs(uniforms.highlights) <= 1.0e-6f &&
      fabs(uniforms.shadows) <= 1.0e-6f &&
      fabs(uniforms.whites) <= 1.0e-6f &&
      fabs(uniforms.blacks) <= 1.0e-6f &&
      fabs(uniforms.brightness) <= 1.0e-6f &&
      fabs(uniforms.zone_deep_shadows) <= 1.0e-6f &&
      fabs(uniforms.zone_shadows) <= 1.0e-6f &&
      fabs(uniforms.zone_midtones) <= 1.0e-6f &&
      fabs(uniforms.zone_lights) <= 1.0e-6f &&
      fabs(uniforms.zone_highlights) <= 1.0e-6f) {
    return value;
  }
  const float safe_value = max(1.0e-8f, value);
  const float input_stop = log2(safe_value / kMiddleGray);
  const float contrast = gentle_signed_response(clamp(uniforms.contrast, -1.0f, 1.0f));
  const float highlights = gentle_signed_response(clamp(uniforms.highlights, -1.0f, 1.0f));
  const float shadows = gentle_signed_response(clamp(uniforms.shadows, -1.0f, 1.0f));
  const float whites = gentle_signed_response(clamp(uniforms.whites, -1.0f, 1.0f));
  const float blacks = gentle_signed_response(clamp(uniforms.blacks, -1.0f, 1.0f));
  const float brightness = gentle_signed_response(clamp(uniforms.brightness, -1.0f, 1.0f));

  float input_stops[7] = {-8.0f, -4.6f, -2.15f, 0.0f, 1.15f, 2.45f, 5.25f};
  float contrast_weights[7] = {0.05f, 0.20f, 0.62f, 0.0f, 1.0f, 0.04f, 0.02f};
  float output_stops[7];
  const float contrast_scale = contrast >= 0.0f ? 0.44f : 0.58f;
  for (int index = 0; index < 7; ++index) {
    const float local_slope =
        clamp(1.0f + contrast * contrast_scale * contrast_weights[index], 0.56f, 1.42f);
    output_stops[index] = input_stops[index] * local_slope;
  }

  output_stops[0] += blacks * 1.35f;
  output_stops[1] += blacks * 0.68f + shadows * 1.35f;
  output_stops[2] += shadows * 1.10f + blacks * 0.18f;
  output_stops[3] += brightness * 0.90f;
  output_stops[4] += brightness * 0.65f + highlights * 0.18f;
  output_stops[5] += highlights * 0.70f + whites * 0.26f;
  output_stops[6] += whites * 1.02f + highlights * 0.32f;

  const float zone_offset = zone_exposure_delta(input_stop, uniforms) * 0.75f;
  output_stops[1] += zone_offset * 0.55f;
  output_stops[2] += zone_offset * 0.82f;
  output_stops[3] += zone_offset * 0.74f;
  output_stops[4] += zone_offset * 0.68f;
  output_stops[5] += zone_offset * 0.60f;

  for (int index = 1; index < 7; ++index) {
    output_stops[index] = max(output_stops[index], output_stops[index - 1] + 0.18f);
  }

  return kMiddleGray * pow(2.0f, interpolate_tone_anchors(input_stop, input_stops, output_stops));
}

static float apply_global_tone(float value, constant PreToneUniforms& uniforms) {
  const float safe_value = max(0.0f, value);
  float adjusted = apply_photographic_tone_curve(safe_value, uniforms);
  adjusted = apply_zone_exposure(adjusted, safe_value, uniforms);
  adjusted = apply_adaptive_contrast(adjusted, uniforms.adaptive_contrast);
  adjusted = apply_filmic_toe(adjusted, uniforms.filmic_toe);
  return apply_levels(adjusted, uniforms);
}

static float apply_highlight_shoulder(float value, float threshold) {
  if (value <= threshold) {
    return value;
  }
  const float shoulder_scale = max(1.0e-6f, 1.0f - threshold);
  return threshold + shoulder_scale * (1.0f - exp(-(value - threshold) / shoulder_scale));
}

static float circular_weight(float value, float center, float radius) {
  if (radius <= 0.0f) {
    return 0.0f;
  }

  float distance = fabs(value - center);
  distance = min(distance, 1.0f - distance);
  if (distance >= radius) {
    return 0.0f;
  }

  const float t = 1.0f - distance / radius;
  return t * t * (3.0f - 2.0f * t);
}

static float circular_feathered_selection_weight(
    float value,
    float center,
    float radius,
    float softness) {
  if (radius <= 0.0f) {
    return 0.0f;
  }

  float distance = fabs(value - center);
  distance = min(distance, 1.0f - distance);
  if (distance >= radius) {
    return 0.0f;
  }

  const float feather = clamp(softness, 0.0f, 1.0f) * radius;
  const float inner_radius = radius - feather;
  if (distance <= inner_radius || feather <= 1.0e-6f) {
    return 1.0f;
  }
  return 1.0f - smoothstep(inner_radius, radius, distance);
}

static float skin_tone_protection_scale(float normalized_hue, float chroma, constant PostColorUniforms& uniforms) {
  if (uniforms.skin_tone_protection_enabled == 0 || uniforms.skin_tone_protection_strength <= 1.0e-6f) {
    return 1.0f;
  }
  const float range = clamp(uniforms.skin_tone_protection_range / 100.0f, 0.01f, 1.0f) * 0.50f;
  const float weight =
      circular_weight(normalized_hue, wrap_unit(uniforms.skin_tone_line_hue), range) *
      smoothstep(0.02f, 0.12f, chroma) *
      clamp(uniforms.skin_tone_protection_strength / 100.0f, 0.0f, 1.0f);
  return 1.0f - 0.95f * weight;
}

static float default_skin_chroma_protection(float normalized_hue, float chroma) {
  return 1.0f - 0.38f * circular_weight(normalized_hue, 0.08f, 0.15f) *
                      smoothstep(0.018f, 0.13f, chroma);
}

static float tone_chroma_protection(float lightness) {
  const float shadow_protection = smoothstep(0.05f, 0.20f, lightness);
  const float highlight_protection = 1.0f - smoothstep(0.78f, 0.98f, lightness);
  return clamp(shadow_protection * highlight_protection, 0.0f, 1.0f);
}

static float highlight_chroma_compression_scale(float lightness, float chroma) {
  const float highlight_weight = smoothstep(0.78f, 0.98f, lightness);
  const float chroma_weight = smoothstep(0.035f, 0.22f, chroma);
  return clamp(1.0f - 0.42f * highlight_weight * chroma_weight, 0.52f, 1.0f);
}

static int color_harmony_anchor_count(int mode) {
  if (mode == 1 || mode == 2 || mode == 3) {
    return 3;
  }
  if (mode == 4) {
    return 4;
  }
  return 2;
}

static float color_harmony_anchor(float base, int mode, int index) {
  if (mode == 1) {
    constexpr float offsets[4] = {-0.0833333333f, 0.0f, 0.0833333333f, 0.0f};
    return wrap_unit(base + offsets[index]);
  }
  if (mode == 2) {
    constexpr float offsets[4] = {0.0f, 0.3333333333f, 0.6666666667f, 0.0f};
    return wrap_unit(base + offsets[index]);
  }
  if (mode == 3) {
    constexpr float offsets[4] = {0.0f, 0.4166666667f, 0.5833333333f, 0.0f};
    return wrap_unit(base + offsets[index]);
  }
  if (mode == 4) {
    constexpr float offsets[4] = {0.0f, 0.25f, 0.50f, 0.75f};
    return wrap_unit(base + offsets[index]);
  }
  constexpr float offsets[4] = {0.0f, 0.50f, 0.0f, 0.0f};
  return wrap_unit(base + offsets[index]);
}

static float nearest_color_harmony_hue(float normalized_hue, constant PostColorUniforms& uniforms) {
  const float base = wrap_unit(uniforms.color_harmony_base_hue);
  const int count = color_harmony_anchor_count(uniforms.color_harmony_mode);
  float best_hue = color_harmony_anchor(base, uniforms.color_harmony_mode, 0);
  float best_distance = 1.0f;
  for (int i = 0; i < count; ++i) {
    const float anchor = color_harmony_anchor(base, uniforms.color_harmony_mode, i);
    float distance = fabs(normalized_hue - anchor);
    distance = min(distance, 1.0f - distance);
    if (distance < best_distance) {
      best_distance = distance;
      best_hue = anchor;
    }
  }
  return best_hue;
}

static float channel_weight_for_index(float hue, int channel_index) {
  constexpr float centers[8] = {0.0f, 0.09f, 0.15f, 0.32f, 0.50f, 0.64f, 0.78f, 0.88f};
  constexpr float inner_widths[8] = {0.05f, 0.04f, 0.04f, 0.10f, 0.06f, 0.08f, 0.06f, 0.05f};
  constexpr float outer_widths[8] = {0.10f, 0.09f, 0.08f, 0.18f, 0.10f, 0.13f, 0.10f, 0.10f};
  if (channel_index < 0 || channel_index >= 8 || hue < 0.0f) {
    return 0.0f;
  }

  float distance = fabs(hue - centers[channel_index]);
  distance = min(distance, 1.0f - distance);
  if (distance <= inner_widths[channel_index]) {
    return 1.0f;
  }
  if (distance >= outer_widths[channel_index]) {
    return 0.0f;
  }
  return 1.0f - smoothstep(inner_widths[channel_index], outer_widths[channel_index], distance);
}

static float sample_curve_lut(float value, device const float* lut, int lut_size) {
  if (lut_size <= 1) {
    return clamp_unit(value);
  }

  const float scaled = clamp_unit(value) * float(lut_size - 1);
  const int index0 = min(int(floor(scaled)), lut_size - 1);
  const int index1 = min(index0 + 1, lut_size - 1);
  const float t = scaled - float(index0);
  return mix(lut[index0], lut[index1], t);
}

static float4 scale_pixel_to_luminance(float4 pixel, float target_luminance) {
  const float clamped_target = max(0.0f, target_luminance);
  const float current_luminance = luminance(pixel.rgb);
  const float max_input_channel = max(pixel.r, max(pixel.g, pixel.b));
  if (max_input_channel <= 1.0e-9f) {
    return float4(clamped_target, clamped_target, clamped_target, pixel.a);
  }

  const float safe_luminance = max(current_luminance, 1.0e-6f);
  const float scale = clamped_target / safe_luminance;
  return float4(
      max(0.0f, pixel.r * scale),
      max(0.0f, pixel.g * scale),
      max(0.0f, pixel.b * scale),
      pixel.a);
}

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

  pixels[gid] = max(pixel, float4(0.0f));
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

kernel void extract_oklab(
    device const float4* pixels [[buffer(0)]],
    device float* lightness [[buffer(1)]],
    device float* chroma_a [[buffer(2)]],
    device float* chroma_b [[buffer(3)]],
    constant DetailUniforms& uniforms [[buffer(4)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  const float3 oklab = linear_rgb_to_oklab(pixels[gid].rgb);
  lightness[gid] = oklab.x;
  chroma_a[gid] = oklab.y;
  chroma_b[gid] = oklab.z;
}

kernel void extract_luminance(
    device const float4* pixels [[buffer(0)]],
    device float* lightness [[buffer(1)]],
    constant SpatialToneUniforms& uniforms [[buffer(2)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  lightness[gid] = luminance(pixels[gid].rgb);
}

kernel void apply_spatial_tone(
    device float4* pixels [[buffer(0)]],
    device const float* luminance_buffer [[buffer(1)]],
    device const float* texture_blur [[buffer(2)]],
    device const float* clarity_blur [[buffer(3)]],
    device const float* haze_blur [[buffer(4)]],
    constant SpatialToneUniforms& uniforms [[buffer(5)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  const float current_luminance = luminance_buffer[gid];
  const float midtone_weight = bell_weight(current_luminance, 0.5f, 0.42f);
  const float shadow_guard = smoothstep(0.025f, 0.12f, current_luminance);
  const float highlight_headroom = 1.0f - smoothstep(0.74f, 0.98f, current_luminance);
  const float tone_guard = shadow_guard * highlight_headroom;
  const float texture_gain = uniforms.texture_strength >= 0.0f ? 1.30f : 0.48f;
  const float clarity_gain = uniforms.clarity_strength >= 0.0f ? 1.08f : 0.46f;
  const float dehaze_gain = uniforms.dehaze_strength >= 0.0f ? 0.95f : 0.36f;
  float target_luminance = current_luminance;
  if (abs(uniforms.texture_strength) > 1.0e-6f) {
    const float texture_signal = current_luminance - texture_blur[gid];
    const float broad_edge_signal = current_luminance - clarity_blur[gid];
    const float broad_edge_guard = 1.0f - smoothstep(0.08f, 0.36f, abs(broad_edge_signal)) * 0.72f;
    target_luminance +=
        uniforms.texture_strength * texture_signal * (0.46f + midtone_weight * 0.70f) *
        broad_edge_guard * tone_guard * texture_gain;
  }
  if (abs(uniforms.clarity_strength) > 1.0e-6f) {
    const float clarity_signal = current_luminance - clarity_blur[gid];
    const float edge_limit = 1.0f - smoothstep(0.24f, 0.58f, abs(clarity_signal)) * 0.28f;
    target_luminance +=
        uniforms.clarity_strength * clarity_signal * (0.36f + midtone_weight * 0.82f) *
        tone_guard * edge_limit * clarity_gain;
  }
  if (abs(uniforms.dehaze_strength) > 1.0e-6f) {
    const float haze_signal = clamp(current_luminance - haze_blur[gid], -0.18f, 0.18f);
    target_luminance +=
        uniforms.dehaze_strength * haze_signal * (0.34f + midtone_weight * 0.66f) *
        highlight_headroom * dehaze_gain;
    const float haze_shadow_weight = 1.0f - smoothstep(0.14f, 0.46f, current_luminance);
    const float haze_structure_weight = smoothstep(0.008f, 0.065f, abs(haze_signal));
    if (uniforms.dehaze_strength > 0.0f) {
      target_luminance *= max(
          0.0f,
          1.0f - uniforms.dehaze_strength * haze_shadow_weight * haze_structure_weight * 0.16f);
    } else {
      target_luminance += -uniforms.dehaze_strength * haze_shadow_weight * haze_structure_weight * 0.035f;
    }
  }
  pixels[gid] = scale_pixel_to_luminance(pixels[gid], target_luminance);
}

kernel void apply_post_color(
    device float4* pixels [[buffer(0)]],
    device const float* master_curve_lut [[buffer(1)]],
    device const float* luma_curve_lut [[buffer(2)]],
    device const float* red_curve_lut [[buffer(3)]],
    device const float* green_curve_lut [[buffer(4)]],
    device const float* blue_curve_lut [[buffer(5)]],
    constant PostColorUniforms& uniforms [[buffer(6)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  float4 pixel = pixels[gid];
  if (uniforms.use_master_curve != 0) {
    pixel.r = sample_curve_lut(pixel.r, master_curve_lut, uniforms.curve_lut_size);
    pixel.g = sample_curve_lut(pixel.g, master_curve_lut, uniforms.curve_lut_size);
    pixel.b = sample_curve_lut(pixel.b, master_curve_lut, uniforms.curve_lut_size);
  }
  if (uniforms.use_luma_curve != 0) {
    const float source_luminance = luminance(pixel.rgb);
    const float target_luminance = sample_curve_lut(source_luminance, luma_curve_lut, uniforms.curve_lut_size);
    pixel = scale_pixel_to_luminance(pixel, target_luminance);
  }
  if (uniforms.use_red_curve != 0) {
    pixel.r = sample_curve_lut(pixel.r, red_curve_lut, uniforms.curve_lut_size);
  }
  if (uniforms.use_green_curve != 0) {
    pixel.g = sample_curve_lut(pixel.g, green_curve_lut, uniforms.curve_lut_size);
  }
  if (uniforms.use_blue_curve != 0) {
    pixel.b = sample_curve_lut(pixel.b, blue_curve_lut, uniforms.curve_lut_size);
  }
  pixel.rgb = max(
      float3(0.0f),
      float3(
          dot(pixel.rgb, float3(uniforms.channel_mixer[0], uniforms.channel_mixer[1], uniforms.channel_mixer[2])),
          dot(pixel.rgb, float3(uniforms.channel_mixer[3], uniforms.channel_mixer[4], uniforms.channel_mixer[5])),
          dot(pixel.rgb, float3(uniforms.channel_mixer[6], uniforms.channel_mixer[7], uniforms.channel_mixer[8]))));
  if (uniforms.black_and_white_enabled != 0) {
    float gray = luminance(pixel.rgb);
    const float mixer_hue = rgb_hue_for_selection(pixel.rgb);
    if (mixer_hue >= 0.0f) {
      for (int ch = 0; ch < 8; ++ch) {
        const float mixer_weight = channel_weight_for_index(mixer_hue, ch);
        if (mixer_weight > 0.0f) {
          gray *= max(
              0.0f,
              1.0f + mixer_weight * clamp(uniforms.black_and_white_mixer[ch], -100.0f, 100.0f) / 100.0f *
                         0.85f);
        }
      }
    }
    pixel.rgb = float3(max(0.0f, gray));
  }

  const float3 oklab = linear_rgb_to_oklab(pixel.rgb);
  float lightness = clamp_unit(oklab.x);
  float chroma = length(float2(oklab.y, oklab.z));
  float hue = wrap_radians(atan2(oklab.z, oklab.y));

  float explicit_skin_protection = 1.0f;
  float replacement_luminance_reference = -1.0f;
  float replacement_luminance_weight = 0.0f;
  if (chroma > 1.0e-6f) {
    const float normalized_hue = wrap_unit(hue / 6.28318530717958647692f);
    const float low_chroma_weight = 1.0f - smoothstep(0.035f, 0.24f, chroma);
    const float mid_lightness_weight = 1.0f - smoothstep(0.18f, 0.48f, fabs(lightness - 0.56f));
    const float tonal_protection = tone_chroma_protection(lightness);
    explicit_skin_protection = skin_tone_protection_scale(normalized_hue, chroma, uniforms);
    const float skin_protection =
        default_skin_chroma_protection(normalized_hue, chroma) *
        explicit_skin_protection;
    chroma *= max(0.0f, 1.0f + uniforms.vibrance_strength * low_chroma_weight * mid_lightness_weight *
                                tonal_protection * skin_protection * 0.82f);
    chroma *= max(0.0f, 1.0f + max(0.0f, uniforms.dehaze_strength) * 0.10f);
    const float peak_channel = max(pixel.r, max(pixel.g, pixel.b));
    const bool compress_highlight_chroma =
        peak_channel > uniforms.output_headroom + 1.0e-5f ||
        (uniforms.highlight_chroma_compression_enabled != 0 && peak_channel >= 0.98f);
    if (compress_highlight_chroma) {
      chroma *= highlight_chroma_compression_scale(lightness, chroma);
    }

    const float mixer_hue_for_selection = (uniforms.color_mixer_model == 1 || uniforms.color_mixer_model == 2)
        ? rgb_hue_for_selection(pixel.rgb)
        : normalized_hue;
    if (mixer_hue_for_selection >= 0.0f) {
      const float hue_scale = uniforms.color_mixer_model == 2 ? 0.16f : 0.14f;
      const float saturation_scale = uniforms.color_mixer_model == 3 ? 0.72f
          : (uniforms.color_mixer_model == 2 ? 0.92f : 0.80f);
      const float luminance_scale = uniforms.color_mixer_model == 2 ? 0.14f
          : (uniforms.color_mixer_model == 3 ? 0.20f : 0.18f);
      for (int ch = 0; ch < 8; ++ch) {
        const float mixer_weight = channel_weight_for_index(mixer_hue_for_selection, ch);
        if (mixer_weight > 0.0f) {
          const float protected_mixer_weight = mixer_weight * explicit_skin_protection;
          hue = wrap_radians(
              hue + protected_mixer_weight * clamp(uniforms.mixer_hue[ch], -100.0f, 100.0f) / 100.0f *
                        hue_scale * 6.28318530717958647692f);
          chroma *= max(
              0.0f,
              1.0f + protected_mixer_weight * clamp(uniforms.mixer_sat[ch], -100.0f, 100.0f) / 100.0f * saturation_scale);
          lightness = clamp_unit(
              lightness + protected_mixer_weight * clamp(uniforms.mixer_lum[ch], -100.0f, 100.0f) / 100.0f * luminance_scale);
          apply_selective_color(
              uniforms.selective_cyan[ch],
              uniforms.selective_magenta[ch],
              uniforms.selective_yellow[ch],
              uniforms.selective_black[ch],
              protected_mixer_weight,
              lightness,
              chroma,
              hue);
        }
      }
    }
    if (uniforms.color_replacement_enabled != 0 && uniforms.color_replacement_strength > 1.0e-6f) {
      const float selection_hue = rgb_hue_for_selection(pixel.rgb);
      const float range = clamp(uniforms.color_replacement_range / 100.0f, 0.01f, 1.0f) * 0.50f;
      const float softness = clamp(uniforms.color_replacement_softness / 100.0f, 0.0f, 1.0f);
      const float replacement_weight =
          (selection_hue >= 0.0f
               ? circular_feathered_selection_weight(
                     selection_hue,
                     wrap_unit(uniforms.color_replacement_source_hue),
                     range,
                     softness)
               : 0.0f) *
          clamp(uniforms.color_replacement_strength / 100.0f, 0.0f, 1.0f) * explicit_skin_protection;
      if (replacement_weight > 1.0e-6f) {
        if (uniforms.color_replacement_preserve_luminance != 0) {
          replacement_luminance_reference = luminance(gamut_fit_oklch(lightness, chroma, hue, pixel.a).rgb);
          replacement_luminance_weight = replacement_weight;
        }
        const float target_hue = oklab_hue_for_rgb_hue(uniforms.color_replacement_target_hue);
        hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * replacement_weight);
        if (uniforms.color_replacement_preserve_saturation == 0) {
          chroma = max(chroma, mix(chroma, 0.16f, replacement_weight));
        }
      }
    }
    if (uniforms.color_harmony_enabled != 0 && uniforms.color_harmony_strength > 1.0e-6f) {
      const float target_hue =
          nearest_color_harmony_hue(wrap_unit(hue / 6.28318530717958647692f), uniforms) *
          6.28318530717958647692f;
      const float harmony_weight =
          clamp(uniforms.color_harmony_strength / 100.0f, 0.0f, 1.0f) * 0.65f * explicit_skin_protection;
      hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * harmony_weight);
    }
  }

  float grade_a = chroma * cos(hue);
  float grade_b = chroma * sin(hue);
  const float balance_shift = clamp(uniforms.grading_balance, -1.0f, 1.0f) * 0.18f;
  const float grading_amount = clamp(uniforms.grading_amount, 0.0f, 1.0f);
  const float shadow_weight = 1.0f - smoothstep(0.15f - balance_shift, 0.45f - balance_shift, lightness);
  const float highlight_weight = smoothstep(0.55f - balance_shift, 0.85f - balance_shift, lightness);
  const float midtone_weight = 1.0f - shadow_weight - highlight_weight;
  const float weights[3] = {shadow_weight, max(0.0f, midtone_weight), highlight_weight};

  for (int region = 0; region < 3; ++region) {
    const float weight = weights[region];
    if (weight < 1.0e-9f) {
      continue;
    }
    const float sat_norm = clamp(uniforms.grading_sat[region], -100.0f, 100.0f) / 100.0f;
    const float hue_rad = wrap_unit(uniforms.grading_hue[region]) * 6.28318530717958647692f;
    const float lum_norm = clamp(uniforms.grading_lum[region], -100.0f, 100.0f) / 100.0f;
    const float scaled_weight = weight * grading_amount;
    const float intensity = fabs(sat_norm) * 0.18f * scaled_weight * explicit_skin_protection;
    if (intensity > 1.0e-9f) {
      grade_a += intensity * cos(hue_rad);
      grade_b += intensity * sin(hue_rad);
    }
    lightness = clamp_unit(lightness + lum_norm * 0.20f * scaled_weight * explicit_skin_protection);
  }

  chroma = length(float2(grade_a, grade_b));
  hue = wrap_radians(atan2(grade_b, grade_a));
  // Vibrance protects source image color before hue-domain tools. Saturation is
  // final so it controls chroma introduced by color grading too.
  if (fabs(uniforms.saturation_strength) > 1.0e-9f) {
    chroma *= uniforms.saturation_strength >= 0.0f
        ? max(0.0f, 1.0f + uniforms.saturation_strength * 0.85f)
        : max(0.0f, 1.0f + uniforms.saturation_strength);
  }
  float4 output = gamut_fit_oklch(lightness, chroma, hue, pixel.a);
  if (replacement_luminance_reference >= 0.0f && replacement_luminance_weight > 1.0e-6f) {
    const float current_luminance = luminance(output.rgb);
    const float target_luminance = mix(
        current_luminance,
        clamp(replacement_luminance_reference, 0.0f, uniforms.output_headroom),
        clamp(replacement_luminance_weight, 0.0f, 1.0f));
    output = scale_pixel_to_luminance(output, target_luminance);
  }
  const bool grading_adds_chroma =
      fabs(uniforms.grading_sat[0]) > 1.0e-6f ||
      fabs(uniforms.grading_sat[1]) > 1.0e-6f ||
      fabs(uniforms.grading_sat[2]) > 1.0e-6f;
  if (uniforms.saturation_strength <= -0.999999f && !grading_adds_chroma &&
      uniforms.color_replacement_enabled == 0) {
    const float neutral = clamp(luminance(output.rgb), 0.0f, uniforms.output_headroom);
    output.rgb = float3(neutral);
  }
  pixels[gid] = output;
}

kernel void compute_pre_tone_inputs(
    device float4* pixels [[buffer(0)]],
    device float* target_luminance [[buffer(1)]],
    device const float* local_mask_weights [[buffer(2)]],
    constant PreToneUniforms& uniforms [[buffer(3)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  float4 pixel = pixels[gid];
  pixel.r = max(0.0f, pixel.r * uniforms.exposure_gain * uniforms.global_wb_red);
  pixel.g = max(0.0f, pixel.g * uniforms.exposure_gain * uniforms.global_wb_green);
  pixel.b = max(0.0f, pixel.b * uniforms.exposure_gain * uniforms.global_wb_blue);

  const int x = int(gid) % uniforms.width;
  const int y = int(gid) / uniforms.width;
  const float nx = uniforms.width > 1 ? float(x) / float(uniforms.width - 1) : 0.5f;
  const float ny = uniforms.height > 1 ? float(y) / float(uniforms.height - 1) : 0.5f;

  if (uniforms.has_linear_mask != 0) {
    const float w = linear_mask_weight(
        float2(nx, ny), uniforms.linear_position, uniforms.linear_rotation_rad, uniforms.linear_feather);
    if (w > 0.0f) {
      const float gain = pow(2.0f, uniforms.linear_exposure * w);
      pixel.r *= (1.0f + (uniforms.linear_wb_red - 1.0f) * w) * gain;
      pixel.g *= (1.0f + (uniforms.linear_wb_green - 1.0f) * w) * gain;
      pixel.b *= (1.0f + (uniforms.linear_wb_blue - 1.0f) * w) * gain;
    }
  }

  if (uniforms.has_radial_mask != 0) {
    const float w = radial_mask_weight(
        float2(nx, ny),
        float2(uniforms.radial_center_x, uniforms.radial_center_y),
        float2(uniforms.radial_radius_x, uniforms.radial_radius_y),
        uniforms.radial_rotation_rad,
        uniforms.radial_feather);
    if (w > 0.0f) {
      const float gain = pow(2.0f, uniforms.radial_exposure * w);
      pixel.r *= (1.0f + (uniforms.radial_wb_red - 1.0f) * w) * gain;
      pixel.g *= (1.0f + (uniforms.radial_wb_green - 1.0f) * w) * gain;
      pixel.b *= (1.0f + (uniforms.radial_wb_blue - 1.0f) * w) * gain;
    }
  }

  if (uniforms.has_local_mask != 0) {
    const float w = clamp_unit(local_mask_weights[gid]);
    if (w > 0.0f) {
      const float gain = pow(2.0f, uniforms.local_exposure * w);
      pixel.r *= (1.0f + (uniforms.local_wb_red - 1.0f) * w) * gain;
      pixel.g *= (1.0f + (uniforms.local_wb_green - 1.0f) * w) * gain;
      pixel.b *= (1.0f + (uniforms.local_wb_blue - 1.0f) * w) * gain;
    }
  }

  pixel = max(pixel, float4(0.0f));
  pixels[gid] = pixel;
  target_luminance[gid] = apply_global_tone(luminance(pixel.rgb), uniforms);
}

kernel void apply_pre_tone_output(
    device float4* pixels [[buffer(0)]],
    device const float* target_luminance [[buffer(1)]],
    device const float* parametric_curve_lut [[buffer(2)]],
    constant PreToneUniforms& uniforms [[buffer(3)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  float target = target_luminance[gid];
  if (uniforms.shoulder_threshold < 1.0f) {
    target = apply_highlight_shoulder(target, uniforms.shoulder_threshold);
  }

  const float curve_target = uniforms.use_parametric_curve != 0
                                 ? sample_curve_lut(target, parametric_curve_lut, uniforms.curve_lut_size)
                                 : clamp_unit(target);
  pixels[gid] = scale_pixel_to_luminance(pixels[gid], curve_target);
}

kernel void apply_sharpen(
    device float* lightness [[buffer(0)]],
    device const float* sharp_blur [[buffer(1)]],
    device const float* edge_blur [[buffer(2)]],
    constant DetailUniforms& uniforms [[buffer(3)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  const float detail_signal = lightness[gid] - sharp_blur[gid];
  const float edge_strength = fabs(lightness[gid] - edge_blur[gid]);
  const float mask_gate = uniforms.sharpen_mask_strength > 1.0e-6f
                              ? smoothstep(0.0015f, 0.012f + 0.06f * uniforms.sharpen_mask_strength, edge_strength)
                              : 1.0f;
  lightness[gid] += detail_signal * uniforms.sharpen_strength * 1.9f * mask_gate;
}

kernel void apply_luma_nr(
    device float* lightness [[buffer(0)]],
    device const float* nr_blur [[buffer(1)]],
    device const float* nr_edge_blur [[buffer(2)]],
    constant DetailUniforms& uniforms [[buffer(3)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  const float edge = fabs(lightness[gid] - nr_edge_blur[gid]);
  const float edge_preserve = 1.0f - smoothstep(0.005f, 0.04f, edge);
  const float blend = uniforms.luma_nr_strength * 0.85f * edge_preserve;
  lightness[gid] = mix(lightness[gid], nr_blur[gid], blend);
}

kernel void apply_color_nr(
    device float* chroma_a [[buffer(0)]],
    device float* chroma_b [[buffer(1)]],
    device const float* a_blur [[buffer(2)]],
    device const float* b_blur [[buffer(3)]],
    constant DetailUniforms& uniforms [[buffer(4)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  const float blend = uniforms.color_nr_strength * 0.90f;
  chroma_a[gid] = mix(chroma_a[gid], a_blur[gid], blend);
  chroma_b[gid] = mix(chroma_b[gid], b_blur[gid], blend);
}

kernel void compose_detail_pixels(
    device float4* pixels [[buffer(0)]],
    device const float* lightness [[buffer(1)]],
    device const float* chroma_a [[buffer(2)]],
    device const float* chroma_b [[buffer(3)]],
    constant DetailUniforms& uniforms [[buffer(4)]],
    uint gid [[thread_position_in_grid]]) {
  uint total = uint(uniforms.width) * uint(uniforms.height);
  if (gid >= total) {
    return;
  }

  const float L = clamp_unit(lightness[gid]);
  const float a = chroma_a[gid];
  const float b = chroma_b[gid];
  const float c = length(float2(a, b));
  const float h = wrap_radians(atan2(b, a));
  const float alpha = pixels[gid].a;
  pixels[gid] = gamut_fit_oklch(L, c, h, alpha);
}

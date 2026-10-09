#include "color_mask_runtime.h"
#include "photo_editor/pipeline/local_mask_sampling.h"

#include "color_blur_runtime.h"
#include "color_adjustment_scheduler.h"
#include "color_math_runtime.h"
#include "color_spatial_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

constexpr double kMaskPi = std::numbers::pi_v<double>;

void apply_mask_edge_cleanup(
    ColorPixel* pixel,
    const PipelineColorContext& context,
    double mask_weight,
    double decontaminate) {
  if (pixel == nullptr) {
    return;
  }

  const auto cleanup_strength =
      clamp_unit(decontaminate / 100.0) *
      smoothstep(0.02, 0.24, clamp_unit(mask_weight) * (1.0 - clamp_unit(mask_weight)) * 4.0);
  if (cleanup_strength <= 1e-6) {
    return;
  }

  auto oklab = linear_rgb_to_oklab(*pixel, context);
  auto lightness = clamp_oklab_lightness(oklab.lightness, context);
  auto chroma = std::hypot(oklab.a, oklab.b);
  const auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));

  chroma *= std::max(0.0, 1.0 - cleanup_strength * 0.75);
  lightness = clamp_unit(lightness + cleanup_strength * 0.015);
  *pixel = gamut_fit_oklch(lightness, chroma, hue, pixel->alpha, context);
}

[[nodiscard]] double radial_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorAdjustmentState& state) {
  if (!state.radial_mask_enabled) {
    return 0.0;
  }

  const auto radius_x = std::max(0.05, state.radial_mask_radius_x);
  const auto radius_y = std::max(0.05, state.radial_mask_radius_y);
  const auto feather = std::clamp(state.radial_mask_feather, 0.05, 1.0);
  const auto radians = state.radial_mask_rotation * kMaskPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto translated_x = normalized_x - state.radial_mask_center_x;
  const auto translated_y = normalized_y - state.radial_mask_center_y;
  const auto local_x = translated_x * cosine + translated_y * sine;
  const auto local_y = -translated_x * sine + translated_y * cosine;
  const auto distance = std::sqrt((local_x * local_x) / (radius_x * radius_x) +
                                  (local_y * local_y) / (radius_y * radius_y));
  const auto inner_radius = std::max(0.0, 1.0 - feather);
  if (distance <= inner_radius) {
    return 1.0;
  }
  if (distance >= 1.0) {
    return 0.0;
  }
  return 1.0 - smoothstep(inner_radius, 1.0, distance);
}

[[nodiscard]] double linear_mask_weight(
    double normalized_x,
    double normalized_y,
    double position,
    double feather,
    double rotation_degrees) {
  const auto radians = rotation_degrees * kMaskPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto axis_coordinate = ((normalized_x - 0.5) * cosine + (normalized_y - 0.5) * sine) + 0.5;
  return clamp_unit(((position - axis_coordinate) / std::max(0.05, feather)) + 0.5);
}

[[nodiscard]] double apply_selection_expand(double base_weight, const LocalMask& mask) {
  const auto expand = clamp_signed_percent(mask.selection_expand) / 100.0;
  if (std::abs(expand) <= 1e-6) {
    return clamp_unit(base_weight);
  }

  const auto threshold = std::clamp(0.5 - expand * 0.35, 0.08, 0.92);
  const auto ramp = std::clamp(0.06 + std::abs(expand) * 0.18, 0.04, 0.28);
  return smoothstep(threshold - ramp, threshold + ramp, clamp_unit(base_weight));
}

[[nodiscard]] double apply_selection_edge_contrast(double base_weight, const LocalMask& mask) {
  const auto contrast = clamp_unit(mask.selection_edge_contrast / 100.0);
  if (contrast <= 1e-6) {
    return clamp_unit(base_weight);
  }

  const auto ramp = std::lerp(0.24, 0.035, contrast);
  return smoothstep(0.5 - ramp, 0.5 + ramp, clamp_unit(base_weight));
}

[[nodiscard]] double apply_selection_smooth(double base_weight, const LocalMask& mask) {
  const auto smoothing = clamp_unit(mask.selection_smooth / 100.0);
  const auto weight = clamp_unit(base_weight);
  if (smoothing <= 1e-6 || weight <= 1e-6 || weight >= 1.0 - 1e-6) {
    return weight;
  }

  return std::lerp(weight, 0.5, smoothing * 0.35);
}

[[nodiscard]] double apply_selection_fine_edge(double base_weight, const LocalMask& mask) {
  const auto refinement = clamp_unit(mask.selection_fine_edge / 100.0);
  const auto weight = clamp_unit(base_weight);
  if (refinement <= 1e-6 || weight <= 1e-6 || weight >= 1.0 - 1e-6) {
    return weight;
  }

  const auto edge_band = 1.0 - std::abs((weight * 2.0) - 1.0);
  const auto strand_lift = std::sqrt(weight);
  const auto lifted = std::lerp(weight, strand_lift, refinement * 0.55 * edge_band);
  const auto edge_softened = std::lerp(lifted, 0.5, refinement * 0.12 * edge_band);
  return clamp_unit(edge_softened);
}

[[nodiscard]] double apply_selection_refine(
    double base_weight,
    const LocalMask& mask,
    bool spatial_expand_smooth_applied = false) {
  if (spatial_expand_smooth_applied) {
    auto refined = clamp_unit(base_weight);
    refined = apply_selection_fine_edge(refined, mask);
    refined = apply_selection_edge_contrast(refined, mask);
    if (mask.invert || mask.selection_invert) {
      refined = 1.0 - refined;
    }
    refined = std::min(refined, std::clamp(mask.density, 0.0, 1.0));
    return clamp_unit(refined * std::clamp(mask.opacity, 0.0, 1.0));
  } else {
    auto refined = apply_selection_expand(base_weight, mask);
    refined = apply_selection_smooth(refined, mask);
    refined = apply_selection_fine_edge(refined, mask);
    refined = apply_selection_edge_contrast(refined, mask);
    if (mask.invert || mask.selection_invert) {
      refined = 1.0 - refined;
    }
    refined = std::min(refined, std::clamp(mask.density, 0.0, 1.0));
    return clamp_unit(refined * std::clamp(mask.opacity, 0.0, 1.0));
  }
}

[[nodiscard]] double radial_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  if (!mask.enabled) {
    return 0.0;
  }

  const auto radius_x = std::max(0.05, mask.radius_x);
  const auto radius_y = std::max(0.05, mask.radius_y);
  const auto feather = std::clamp(mask.feather, 0.05, 1.0);
  const auto radians = mask.rotation * kMaskPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto translated_x = normalized_x - mask.center_x;
  const auto translated_y = normalized_y - mask.center_y;
  const auto local_x = translated_x * cosine + translated_y * sine;
  const auto local_y = -translated_x * sine + translated_y * cosine;
  const auto distance = std::sqrt((local_x * local_x) / (radius_x * radius_x) +
                                  (local_y * local_y) / (radius_y * radius_y));
  const auto inner_radius = std::max(0.0, 1.0 - feather);
  if (distance <= inner_radius) {
    return 1.0;
  }
  if (distance >= 1.0) {
    return 0.0;
  }
  return 1.0 - smoothstep(inner_radius, 1.0, distance);
}

[[nodiscard]] double rectangle_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  if (!mask.enabled) {
    return 0.0;
  }

  const auto radius_x = std::max(0.01, mask.radius_x);
  const auto radius_y = std::max(0.01, mask.radius_y);
  const auto feather = std::clamp(mask.feather, 0.0, 1.0);
  const auto radians = mask.rotation * kMaskPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto translated_x = normalized_x - mask.center_x;
  const auto translated_y = normalized_y - mask.center_y;
  const auto local_x = translated_x * cosine + translated_y * sine;
  const auto local_y = -translated_x * sine + translated_y * cosine;
  const auto distance = std::max(std::abs(local_x) / radius_x, std::abs(local_y) / radius_y);
  if (feather <= 1e-6) {
    return distance <= 1.0 ? 1.0 : 0.0;
  }
  const auto inner_edge = std::max(0.0, 1.0 - feather);
  if (distance <= inner_edge) {
    return 1.0;
  }
  if (distance >= 1.0) {
    return 0.0;
  }
  return 1.0 - smoothstep(inner_edge, 1.0, distance);
}

[[nodiscard]] double brush_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (!mask.segmentation_mask.empty() && mask.segmentation_width > 0 &&
      mask.segmentation_height > 0 &&
      mask.segmentation_mask.size() == expected_mask_size) {
    const auto max_x = std::max(mask.segmentation_width - 1, 0);
    const auto max_y = std::max(mask.segmentation_height - 1, 0);
    const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
    const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
    const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
    const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
    const auto ix1 = std::min(ix0 + 1, max_x);
    const auto iy1 = std::min(iy0 + 1, max_y);
    const auto dx = mask.segmentation_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
    const auto dy = mask.segmentation_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
    const auto index_for = [&](int x, int y) {
      return static_cast<std::size_t>(y * mask.segmentation_width + x);
    };
    const auto sample = [&](int x, int y) {
      return static_cast<double>(mask.segmentation_mask[index_for(x, y)]) / 255.0;
    };
    const auto v00 = sample(ix0, iy0);
    const auto v10 = sample(ix1, iy0);
    const auto v01 = sample(ix0, iy1);
    const auto v11 = sample(ix1, iy1);
    const auto top = v00 * (1.0 - dx) + v10 * dx;
    const auto bottom = v01 * (1.0 - dx) + v11 * dx;
    return clamp_unit(top * (1.0 - dy) + bottom * dy);
  }

  return 0.0;
}

[[nodiscard]] double distance_to_segment(
    double px,
    double py,
    double ax,
    double ay,
    double bx,
    double by) {
  const auto abx = bx - ax;
  const auto aby = by - ay;
  const auto ab_len_sq = abx * abx + aby * aby;
  if (ab_len_sq <= 1e-9) {
    return std::hypot(px - ax, py - ay);
  }

  const auto t = std::clamp(((px - ax) * abx + (py - ay) * aby) / ab_len_sq, 0.0, 1.0);
  const auto closest_x = ax + abx * t;
  const auto closest_y = ay + aby * t;
  return std::hypot(px - closest_x, py - closest_y);
}

[[nodiscard]] bool polygon_contains_point(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto point_count = mask.polygon_points.size();
  if (point_count < 3) {
    return false;
  }

  auto inside = false;
  for (std::size_t i = 0, j = point_count - 1; i < point_count; j = i++) {
    const auto& pi = mask.polygon_points[i];
    const auto& pj = mask.polygon_points[j];
    // The straddle test excludes horizontal edges, so the signed delta is nonzero.
    const auto intersects =
        ((pi.y > normalized_y) != (pj.y > normalized_y)) &&
        (normalized_x <
         ((pj.x - pi.x) * (normalized_y - pi.y) / (pj.y - pi.y)) + pi.x);
    if (intersects) {
      inside = !inside;
    }
  }
  return inside;
}

[[nodiscard]] double polygon_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto point_count = mask.polygon_points.size();
  if (point_count < 3) {
    return 0.0;
  }

  auto min_distance = std::numeric_limits<double>::max();
  for (std::size_t i = 0; i < point_count; ++i) {
    const auto& start = mask.polygon_points[i];
    const auto& end = mask.polygon_points[(i + 1) % point_count];
    min_distance = std::min(
        min_distance,
        distance_to_segment(normalized_x, normalized_y, start.x, start.y, end.x, end.y));
  }

  const auto feather = std::clamp(mask.feather, 0.0, 0.25);
  if (polygon_contains_point(normalized_x, normalized_y, mask)) {
    return 1.0;
  }
  if (feather <= 1e-6) {
    return 0.0;
  }
  return 1.0 - smoothstep(0.0, feather, min_distance);
}

void hue_saturation_value_for_pixel(
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    double* hue,
    double* saturation,
    double* value) {
  const auto reference_pixel = pixel_to_linear_srgb_reference(pixel, context);
  const auto max_channel = std::max({reference_pixel.red, reference_pixel.green, reference_pixel.blue});
  const auto min_channel = std::min({reference_pixel.red, reference_pixel.green, reference_pixel.blue});
  const auto delta = max_channel - min_channel;

  if (value != nullptr) {
    *value = clamp_unit(max_channel);
  }
  if (saturation != nullptr) {
    *saturation = max_channel <= 1e-6 ? 0.0 : clamp_unit(delta / max_channel);
  }
  if (hue == nullptr) {
    return;
  }

  if (delta <= 1e-6) {
    *hue = 0.0;
    return;
  }

  auto result = 0.0;
  if (max_channel == reference_pixel.red) {
    result = std::fmod((reference_pixel.green - reference_pixel.blue) / delta, 6.0);
  } else if (max_channel == reference_pixel.green) {
    result = ((reference_pixel.blue - reference_pixel.red) / delta) + 2.0;
  } else {
    result = ((reference_pixel.red - reference_pixel.green) / delta) + 4.0;
  }

  result /= 6.0;
  if (result < 0.0) {
    result += 1.0;
  }
  *hue = wrap_unit(result);
}

[[nodiscard]] double luminance_range_mask_weight(
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  auto min_value = std::clamp(mask.luminance_min, 0.0, 1.0);
  auto max_value = std::clamp(mask.luminance_max, 0.0, 1.0);
  if (min_value > max_value) {
    std::swap(min_value, max_value);
  }
  const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
  const auto value = clamp_unit(luminance_for_pixel(pixel, context));
  const auto lower_weight = min_value <= 1.0e-9
                                ? 1.0
                                : smoothstep(std::max(0.0, min_value - feather), min_value, value);
  const auto upper_weight = max_value >= 1.0 - 1.0e-9
                                ? 1.0
                                : 1.0 - smoothstep(
                                      max_value,
                                      std::min(1.0, max_value + feather),
                                      value);
  return clamp_unit(lower_weight * upper_weight);
}

[[nodiscard]] double color_range_mask_weight(
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  double hue = 0.0;
  double saturation = 0.0;
  hue_saturation_value_for_pixel(pixel, context, &hue, &saturation, nullptr);
  auto saturation_min = std::clamp(mask.saturation_min, 0.0, 1.0);
  auto saturation_max = std::clamp(mask.saturation_max, 0.0, 1.0);
  if (saturation_min > saturation_max) {
    std::swap(saturation_min, saturation_max);
  }
  const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
  const auto hue_weight = circular_weight(hue, wrap_unit(mask.hue_center), std::max(0.02, mask.hue_range));
  const auto sat_lower =
      smoothstep(std::max(0.0, saturation_min - feather), saturation_min, saturation);
  const auto sat_upper =
      1.0 - smoothstep(saturation_max, std::min(1.0, saturation_max + feather), saturation);
  return clamp_unit(hue_weight * sat_lower * sat_upper);
}

[[nodiscard]] double depth_range_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  auto min_value = std::clamp(mask.depth_min, 0.0, 1.0);
  auto max_value = std::clamp(mask.depth_max, 0.0, 1.0);
  if (min_value > max_value) {
    std::swap(min_value, max_value);
  }

  auto value = clamp_unit(luminance_for_pixel(pixel, context));
  const auto expected_map_size =
      static_cast<std::size_t>(mask.depth_width) * static_cast<std::size_t>(mask.depth_height);
  if (!mask.depth_map.empty() && mask.depth_width > 0 && mask.depth_height > 0 &&
      mask.depth_map.size() == expected_map_size) {
    const auto max_x = std::max(mask.depth_width - 1, 0);
    const auto max_y = std::max(mask.depth_height - 1, 0);
    const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
    const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
    const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
    const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
    const auto ix1 = std::min(ix0 + 1, max_x);
    const auto iy1 = std::min(iy0 + 1, max_y);
    const auto dx = mask.depth_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
    const auto dy = mask.depth_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
    const auto idx = [&](int x, int y) {
      return static_cast<std::size_t>(y * mask.depth_width + x);
    };
    const auto sample = [&](int x, int y) {
      return static_cast<double>(mask.depth_map[idx(x, y)]) / 255.0;
    };
    const auto top = sample(ix0, iy0) * (1.0 - dx) + sample(ix1, iy0) * dx;
    const auto bottom = sample(ix0, iy1) * (1.0 - dx) + sample(ix1, iy1) * dx;
    value = clamp_unit(top * (1.0 - dy) + bottom * dy);
  }

  const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
  const auto lower_weight = min_value <= 1.0e-9
                                ? 1.0
                                : smoothstep(std::max(0.0, min_value - feather), min_value, value);
  const auto upper_weight = max_value >= 1.0 - 1.0e-9
                                ? 1.0
                                : 1.0 - smoothstep(
                                      max_value,
                                      std::min(1.0, max_value + feather),
                                      value);
  return clamp_unit(lower_weight * upper_weight);
}

[[nodiscard]] double unit_range_weight(double value, double min_value, double max_value, double feather) {
  auto minimum = std::clamp(min_value, 0.0, 1.0);
  auto maximum = std::clamp(max_value, 0.0, 1.0);
  if (minimum > maximum) {
    std::swap(minimum, maximum);
  }
  const auto clamped_value = clamp_unit(value);
  const auto clamped_feather = std::clamp(feather, 0.0, 0.5);
  const auto lower_weight = minimum <= 1.0e-9
                                ? 1.0
                                : smoothstep(std::max(0.0, minimum - clamped_feather), minimum, clamped_value);
  const auto upper_weight = maximum >= 1.0 - 1.0e-9
                                ? 1.0
                                : 1.0 - smoothstep(
                                      maximum,
                                      std::min(1.0, maximum + clamped_feather),
                                      clamped_value);
  return clamp_unit(lower_weight * upper_weight);
}

[[nodiscard]] double sampled_depth_value(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  auto value = clamp_unit(luminance_for_pixel(pixel, context));
  const auto expected_map_size =
      static_cast<std::size_t>(mask.depth_width) * static_cast<std::size_t>(mask.depth_height);
  if (mask.depth_map.empty() || mask.depth_width <= 0 || mask.depth_height <= 0 ||
      mask.depth_map.size() != expected_map_size) {
    return value;
  }

  const auto max_x = std::max(mask.depth_width - 1, 0);
  const auto max_y = std::max(mask.depth_height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
  const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
  const auto ix1 = std::min(ix0 + 1, max_x);
  const auto iy1 = std::min(iy0 + 1, max_y);
  const auto dx = mask.depth_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
  const auto dy = mask.depth_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
  const auto idx = [&](int x, int y) {
    return static_cast<std::size_t>(y * mask.depth_width + x);
  };
  const auto sample = [&](int x, int y) {
    return static_cast<double>(mask.depth_map[idx(x, y)]) / 255.0;
  };
  const auto top = sample(ix0, iy0) * (1.0 - dx) + sample(ix1, iy0) * dx;
  const auto bottom = sample(ix0, iy1) * (1.0 - dx) + sample(ix1, iy1) * dx;
  return clamp_unit(top * (1.0 - dy) + bottom * dy);
}

[[nodiscard]] double apply_range_refinement(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  auto weight = clamp_unit(base_weight);
  if (weight <= 0.0) {
    return 0.0;
  }
  const auto feather = std::clamp(mask.refine_range_feather, 0.0, 0.5);
  if (mask.refine_luminance_min > 1.0e-9 || mask.refine_luminance_max < 1.0 - 1.0e-9) {
    weight *= unit_range_weight(
        luminance_for_pixel(pixel, context),
        mask.refine_luminance_min,
        mask.refine_luminance_max,
        feather);
  }
  if (mask.refine_hue_range < 0.5 - 1.0e-9) {
    double hue = 0.0;
    double saturation = 0.0;
    hue_saturation_value_for_pixel(pixel, context, &hue, &saturation, nullptr);
    weight *= circular_weight(hue, wrap_unit(mask.refine_hue_center), std::max(0.02, mask.refine_hue_range));
  }
  if (mask.refine_saturation_min > 1.0e-9 || mask.refine_saturation_max < 1.0 - 1.0e-9) {
    double hue = 0.0;
    double saturation = 0.0;
    hue_saturation_value_for_pixel(pixel, context, &hue, &saturation, nullptr);
    weight *= unit_range_weight(
        saturation,
        mask.refine_saturation_min,
        mask.refine_saturation_max,
        feather);
  }
  if (mask.refine_depth_min > 1.0e-9 || mask.refine_depth_max < 1.0 - 1.0e-9) {
    weight *= unit_range_weight(
        sampled_depth_value(normalized_x, normalized_y, pixel, context, mask),
        mask.refine_depth_min,
        mask.refine_depth_max,
        feather);
  }
  if (mask.refine_edge_confidence_min > 1.0e-9) {
    const auto edge_confidence = clamp_unit(std::abs(weight - 0.5) * 2.0);
    weight *= smoothstep(
        std::clamp(mask.refine_edge_confidence_min, 0.0, 1.0),
        1.0,
        edge_confidence);
  }
  return clamp_unit(weight);
}

[[nodiscard]] double subject_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (mask.segmentation_mask.empty() || mask.segmentation_width <= 0 || mask.segmentation_height <= 0 ||
      mask.segmentation_mask.size() != expected_mask_size) {
    return 0.0;
  }

  // Map normalized coordinates to mask bitmap with bilinear sampling.
  const auto max_x = std::max(mask.segmentation_width - 1, 0);
  const auto max_y = std::max(mask.segmentation_height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
  const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
  const auto ix1 = std::min(ix0 + 1, max_x);
  const auto iy1 = std::min(iy0 + 1, max_y);
  const auto dx = mask.segmentation_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
  const auto dy = mask.segmentation_height > 1 ? fy - static_cast<double>(iy0) : 0.0;

  const auto idx = [&](int x, int y) {
    return static_cast<std::size_t>(y * mask.segmentation_width + x);
  };

  const auto sample = [&](int x, int y) {
    return static_cast<double>(mask.segmentation_mask[idx(x, y)]) / 255.0;
  };

  // Bilinear interpolation for smooth edges
  const auto v00 = sample(ix0, iy0);
  const auto v10 = sample(ix1, iy0);
  const auto v01 = sample(ix0, iy1);
  const auto v11 = sample(ix1, iy1);
  const auto top = v00 * (1.0 - dx) + v10 * dx;
  const auto bottom = v01 * (1.0 - dx) + v11 * dx;
  const auto value = top * (1.0 - dy) + bottom * dy;

  // Apply feather as a threshold ramp for edge softness control
  const auto feather = std::max(0.01, mask.feather);
  const auto low = std::max(0.0, 0.5 - feather * 0.5);
  const auto high = std::min(1.0, 0.5 + feather * 0.5);
  return smoothstep(low, high, value);
}

[[nodiscard]] bool is_ai_segmentation_mask_kind(const std::string& kind) {
  return kind == "alpha" || kind == "subject" || kind == "sky" || kind == "background" ||
         kind == "people" || kind == "face" || kind == "hair" ||
         kind == "clothing" || kind == "object";
}

[[nodiscard]] bool mask_supports_edge_aware_matte(const LocalMask& mask) {
  return is_ai_segmentation_mask_kind(mask.kind) || mask.kind == "brush";
}

[[nodiscard]] bool raster_morphology_available(const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  return mask_supports_edge_aware_matte(mask) &&
         !mask.segmentation_mask.empty() &&
         mask.segmentation_width > 0 &&
         mask.segmentation_height > 0 &&
         mask.segmentation_mask.size() == expected_mask_size;
}

[[nodiscard]] double raw_segmentation_mask_value(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (mask.segmentation_mask.empty() || mask.segmentation_width <= 0 || mask.segmentation_height <= 0 ||
      mask.segmentation_mask.size() != expected_mask_size) {
    return 0.0;
  }

  const auto max_x = std::max(mask.segmentation_width - 1, 0);
  const auto max_y = std::max(mask.segmentation_height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
  const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
  const auto ix1 = std::min(ix0 + 1, max_x);
  const auto iy1 = std::min(iy0 + 1, max_y);
  const auto dx = mask.segmentation_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
  const auto dy = mask.segmentation_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
  const auto idx = [&](int x, int y) {
    return static_cast<std::size_t>(y * mask.segmentation_width + x);
  };
  const auto sample = [&](int x, int y) {
    return static_cast<double>(mask.segmentation_mask[idx(x, y)]) / 255.0;
  };

  const auto v00 = sample(ix0, iy0);
  const auto v10 = sample(ix1, iy0);
  const auto v01 = sample(ix0, iy1);
  const auto v11 = sample(ix1, iy1);
  const auto top = v00 * (1.0 - dx) + v10 * dx;
  const auto bottom = v01 * (1.0 - dx) + v11 * dx;
  return clamp_unit(top * (1.0 - dy) + bottom * dy);
}

[[nodiscard]] double raster_selection_weight_from_raw_value(double raw_value, const LocalMask& mask) {
  const auto value = clamp_unit(raw_value);
  if (!is_ai_segmentation_mask_kind(mask.kind)) {
    return value;
  }
  const auto feather = std::max(0.01, mask.feather);
  const auto low = std::max(0.0, 0.5 - feather * 0.5);
  const auto high = std::min(1.0, 0.5 + feather * 0.5);
  return smoothstep(low, high, value);
}

[[nodiscard]] double raster_morphology_sample_spacing_x(const LocalMask& mask) {
  return mask.segmentation_width > 1 ? 1.0 / static_cast<double>(mask.segmentation_width - 1) : 1.0;
}

[[nodiscard]] double raster_morphology_sample_spacing_y(const LocalMask& mask) {
  return mask.segmentation_height > 1 ? 1.0 / static_cast<double>(mask.segmentation_height - 1) : 1.0;
}

[[nodiscard]] int raster_morphology_radius(double amount, const LocalMask& mask) {
  const auto raster_extent = std::max(mask.segmentation_width, mask.segmentation_height);
  return std::clamp(
      static_cast<int>(std::ceil(std::abs(amount) * static_cast<double>(std::max(1, raster_extent)))),
      1,
      24);
}

[[nodiscard]] ColorPixel sample_edge_matte_source(
    double normalized_x,
    double normalized_y,
    const EdgeAwareMatteContext& source_context) {
  if (source_context.pixels == nullptr || source_context.width <= 0 || source_context.height <= 0) {
    return {};
  }
  const auto max_x = std::max(source_context.width - 1, 0);
  const auto max_y = std::max(source_context.height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  return sample_bilinear(*source_context.pixels, source_context.width, source_context.height, fx, fy);
}

[[nodiscard]] double edge_matte_color_distance(const ColorPixel& left, const ColorPixel& right) {
  const auto dr = static_cast<double>(left.red) - static_cast<double>(right.red);
  const auto dg = static_cast<double>(left.green) - static_cast<double>(right.green);
  const auto db = static_cast<double>(left.blue) - static_cast<double>(right.blue);
  const auto dl = (dr * 0.2126) + (dg * 0.7152) + (db * 0.0722);
  return std::sqrt((dr * dr * 0.42) + (dg * dg * 0.42) + (db * db * 0.42) + (dl * dl * 0.58));
}

[[nodiscard]] double local_raw_matte_average(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    int radius) {
  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  auto sum = 0.0;
  auto total_weight = 0.0;
  for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(radius) + 1e-6) {
        continue;
      }
      const auto sample_weight = 1.0 / (1.0 + distance);
      const auto sample = raw_segmentation_mask_value(
          normalized_x + static_cast<double>(offset_x) * step_x,
          normalized_y + static_cast<double>(offset_y) * step_y,
          mask);
      sum += sample * sample_weight;
      total_weight += sample_weight;
    }
  }
  return total_weight > 1e-6 ? clamp_unit(sum / total_weight)
                             : raw_segmentation_mask_value(normalized_x, normalized_y, mask);
}

[[nodiscard]] double local_edge_aware_raw_matte_average(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    int radius,
    const EdgeAwareMatteContext* source_context) {
  if (source_context == nullptr || source_context->pixels == nullptr ||
      source_context->width <= 0 || source_context->height <= 0) {
    return local_raw_matte_average(normalized_x, normalized_y, mask, radius);
  }

  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  const auto center_source = sample_edge_matte_source(normalized_x, normalized_y, *source_context);
  auto sum = 0.0;
  auto total_weight = 0.0;
  for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(radius) + 1e-6) {
        continue;
      }
      const auto sample_x = normalized_x + static_cast<double>(offset_x) * step_x;
      const auto sample_y = normalized_y + static_cast<double>(offset_y) * step_y;
      const auto sample_source = sample_edge_matte_source(sample_x, sample_y, *source_context);
      const auto source_distance = edge_matte_color_distance(center_source, sample_source);
      const auto source_gate = std::max(0.03, 1.0 - smoothstep(0.035, 0.22, source_distance));
      const auto sample_weight = source_gate / (1.0 + distance);
      const auto sample = raw_segmentation_mask_value(sample_x, sample_y, mask);
      sum += sample * sample_weight;
      total_weight += sample_weight;
    }
  }
  return total_weight > 1e-6 ? clamp_unit(sum / total_weight)
                             : raw_segmentation_mask_value(normalized_x, normalized_y, mask);
}

[[nodiscard]] double local_raw_matte_neighbor_average(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    int radius) {
  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  auto sum = 0.0;
  auto total_weight = 0.0;
  for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
      if (offset_x == 0 && offset_y == 0) {
        continue;
      }
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(radius) + 1e-6) {
        continue;
      }
      const auto sample_weight = 1.0 / (1.0 + distance);
      const auto sample = raw_segmentation_mask_value(
          normalized_x + static_cast<double>(offset_x) * step_x,
          normalized_y + static_cast<double>(offset_y) * step_y,
          mask);
      sum += sample * sample_weight;
      total_weight += sample_weight;
    }
  }
  return total_weight > 1e-6 ? clamp_unit(sum / total_weight)
                             : raw_segmentation_mask_value(normalized_x, normalized_y, mask);
}

[[nodiscard]] double local_raw_matte_median(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    int radius) {
  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)));
  for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(radius) + 1e-6) {
        continue;
      }
      samples.push_back(raw_segmentation_mask_value(
          normalized_x + static_cast<double>(offset_x) * step_x,
          normalized_y + static_cast<double>(offset_y) * step_y,
          mask));
    }
  }
  if (samples.empty()) {
    return raw_segmentation_mask_value(normalized_x, normalized_y, mask);
  }
  const auto middle = samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2);
  std::nth_element(samples.begin(), middle, samples.end());
  return clamp_unit(*middle);
}

[[nodiscard]] double apply_raster_selection_morphology(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    const EdgeAwareMatteContext* source_context,
    bool* applied) {
  if (applied != nullptr) {
    *applied = false;
  }
  if (!raster_morphology_available(mask)) {
    return clamp_unit(base_weight);
  }

  const auto legacy_expand = clamp_signed_percent(mask.selection_expand) / 100.0;
  const auto grow = clamp_unit(mask.selection_grow / 100.0) + std::max(0.0, legacy_expand);
  const auto shrink = clamp_unit(mask.selection_shrink / 100.0) + std::max(0.0, -legacy_expand);
  const auto feather = clamp_unit(mask.selection_feather / 100.0);
  const auto blur = clamp_unit(mask.selection_blur / 100.0);
  const auto harden = clamp_unit(mask.selection_harden / 100.0);
  const auto despeckle = clamp_unit(mask.selection_despeckle / 100.0);
  const auto fill_holes = clamp_unit(mask.selection_fill_holes / 100.0);
  const auto remove_islands = clamp_unit(mask.selection_remove_islands / 100.0);
  const auto smoothing = clamp_unit(mask.selection_smooth / 100.0);
  if (grow <= 1e-6 && shrink <= 1e-6 && feather <= 1e-6 && blur <= 1e-6 &&
      harden <= 1e-6 && despeckle <= 1e-6 && fill_holes <= 1e-6 &&
      remove_islands <= 1e-6 && smoothing <= 1e-6) {
    return clamp_unit(base_weight);
  }

  auto raw_value = raw_segmentation_mask_value(normalized_x, normalized_y, mask);
  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  if (fill_holes > 1e-6 || remove_islands > 1e-6) {
    const auto neighbor_average = local_raw_matte_neighbor_average(normalized_x, normalized_y, mask, 1);
    if (fill_holes > 1e-6 && raw_value < 0.5 && neighbor_average >= 0.50) {
      raw_value = std::lerp(raw_value, neighbor_average, fill_holes);
    }
    if (remove_islands > 1e-6 && raw_value > 0.5 && neighbor_average <= 0.50) {
      raw_value = std::lerp(raw_value, neighbor_average, remove_islands);
    }
  }

  if (despeckle > 1e-6) {
    raw_value = std::lerp(
        raw_value,
        local_raw_matte_median(normalized_x, normalized_y, mask, raster_morphology_radius(despeckle, mask) / 2 + 1),
        despeckle);
  }

  if (grow > 1e-6) {
    const auto radius = raster_morphology_radius(grow, mask);
    auto extreme = 0.0;
    for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
      for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
        const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
        if (distance > static_cast<double>(radius) + 1e-6) {
          continue;
        }
        const auto sample = raw_segmentation_mask_value(
            normalized_x + static_cast<double>(offset_x) * step_x,
            normalized_y + static_cast<double>(offset_y) * step_y,
            mask);
        extreme = std::max(extreme, sample);
      }
    }
    // The amount has already selected the dilation radius. Blending by the
    // same amount a second time can leave the expanded samples below the
    // selection threshold, changing opacity without moving the boundary.
    raw_value = extreme;
  }

  if (shrink > 1e-6) {
    const auto radius = raster_morphology_radius(shrink, mask);
    auto extreme = 1.0;
    for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
      for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
        const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
        if (distance > static_cast<double>(radius) + 1e-6) {
          continue;
        }
        const auto sample = raw_segmentation_mask_value(
            normalized_x + static_cast<double>(offset_x) * step_x,
            normalized_y + static_cast<double>(offset_y) * step_y,
            mask);
        extreme = std::min(extreme, sample);
      }
    }
    // Keep erosion geometric for the same reason: its amount controls radius,
    // while the neighborhood minimum defines the contracted matte.
    raw_value = extreme;
  }

  if (feather > 1e-6) {
    const auto radius = std::min(raster_morphology_radius(feather, mask), 12);
    raw_value = std::lerp(
        raw_value,
        local_edge_aware_raw_matte_average(normalized_x, normalized_y, mask, radius, source_context),
        feather * 0.75);
  }

  if (blur > 1e-6) {
    const auto radius = raster_morphology_radius(blur, mask);
    raw_value = std::lerp(
        raw_value,
        local_edge_aware_raw_matte_average(normalized_x, normalized_y, mask, radius, source_context),
        blur);
  }

  if (smoothing > 1e-6) {
    const auto radius = std::min(raster_morphology_radius(smoothing, mask), 12);
    auto sum = 0.0;
    auto total_weight = 0.0;
    for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
      for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
        const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
        if (distance > static_cast<double>(radius) + 1e-6) {
          continue;
        }
        const auto sample_weight = 1.0 / (1.0 + distance);
        const auto sample = raw_segmentation_mask_value(
            normalized_x + static_cast<double>(offset_x) * step_x,
            normalized_y + static_cast<double>(offset_y) * step_y,
            mask);
        sum += sample * sample_weight;
        total_weight += sample_weight;
      }
    }
    if (total_weight > 1e-6) {
      raw_value = std::lerp(raw_value, sum / total_weight, smoothing);
    }
  }

  if (harden > 1e-6) {
    const auto ramp = std::lerp(0.45, 0.04, harden);
    raw_value = smoothstep(0.5 - ramp, 0.5 + ramp, raw_value);
  }

  if (applied != nullptr) {
    *applied = true;
  }
  return raster_selection_weight_from_raw_value(raw_value, mask);
}

[[nodiscard]] double apply_edge_aware_matte(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    const EdgeAwareMatteContext* source_context) {
  const auto refinement = clamp_unit(mask.selection_fine_edge / 100.0);
  const auto weight = clamp_unit(base_weight);
  if (refinement <= 1e-6 || weight <= 1e-6 || weight >= 1.0 - 1e-6 ||
      source_context == nullptr || source_context->pixels == nullptr ||
      source_context->width <= 0 || source_context->height <= 0 ||
      !mask_supports_edge_aware_matte(mask)) {
    return weight;
  }

  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (mask.segmentation_mask.empty() || mask.segmentation_width <= 0 || mask.segmentation_height <= 0 ||
      mask.segmentation_mask.size() != expected_mask_size) {
    return weight;
  }

  const auto source_step_x =
      source_context->width > 1 ? 1.0 / static_cast<double>(source_context->width - 1) : 1.0;
  const auto source_step_y =
      source_context->height > 1 ? 1.0 / static_cast<double>(source_context->height - 1) : 1.0;
  const auto matte_step_x =
      mask.segmentation_width > 1 ? 1.0 / static_cast<double>(mask.segmentation_width - 1) : source_step_x;
  const auto matte_step_y =
      mask.segmentation_height > 1 ? 1.0 / static_cast<double>(mask.segmentation_height - 1) : source_step_y;
  const auto step_x = std::max(source_step_x, matte_step_x);
  const auto step_y = std::max(source_step_y, matte_step_y);

  auto foreground = ColorPixel{};
  auto background = ColorPixel{};
  auto foreground_weight = 0.0;
  auto background_weight = 0.0;
  auto minimum_matte = 1.0;
  auto maximum_matte = 0.0;
  constexpr int kRadius = 2;
  for (int offset_y = -kRadius; offset_y <= kRadius; ++offset_y) {
    for (int offset_x = -kRadius; offset_x <= kRadius; ++offset_x) {
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(kRadius) + 1e-6) {
        continue;
      }
      const auto sample_x = normalized_x + static_cast<double>(offset_x) * step_x;
      const auto sample_y = normalized_y + static_cast<double>(offset_y) * step_y;
      const auto matte = raw_segmentation_mask_value(sample_x, sample_y, mask);
      minimum_matte = std::min(minimum_matte, matte);
      maximum_matte = std::max(maximum_matte, matte);

      if (matte > 0.70) {
        const auto influence = (matte - 0.70) / 0.30 / (1.0 + distance);
        const auto sample = sample_edge_matte_source(sample_x, sample_y, *source_context);
        foreground.red += static_cast<float>(static_cast<double>(sample.red) * influence);
        foreground.green += static_cast<float>(static_cast<double>(sample.green) * influence);
        foreground.blue += static_cast<float>(static_cast<double>(sample.blue) * influence);
        foreground_weight += influence;
      } else if (matte < 0.30) {
        const auto influence = (0.30 - matte) / 0.30 / (1.0 + distance);
        const auto sample = sample_edge_matte_source(sample_x, sample_y, *source_context);
        background.red += static_cast<float>(static_cast<double>(sample.red) * influence);
        background.green += static_cast<float>(static_cast<double>(sample.green) * influence);
        background.blue += static_cast<float>(static_cast<double>(sample.blue) * influence);
        background_weight += influence;
      }
    }
  }

  const auto matte_edge = maximum_matte - minimum_matte;
  if (foreground_weight <= 1e-6 || background_weight <= 1e-6 || matte_edge <= 0.08) {
    return weight;
  }

  foreground.red = static_cast<float>(static_cast<double>(foreground.red) / foreground_weight);
  foreground.green = static_cast<float>(static_cast<double>(foreground.green) / foreground_weight);
  foreground.blue = static_cast<float>(static_cast<double>(foreground.blue) / foreground_weight);
  background.red = static_cast<float>(static_cast<double>(background.red) / background_weight);
  background.green = static_cast<float>(static_cast<double>(background.green) / background_weight);
  background.blue = static_cast<float>(static_cast<double>(background.blue) / background_weight);

  const auto current = sample_edge_matte_source(normalized_x, normalized_y, *source_context);
  const auto foreground_distance = edge_matte_color_distance(current, foreground);
  const auto background_distance = edge_matte_color_distance(current, background);
  const auto separation = edge_matte_color_distance(foreground, background);
  if (separation <= 1e-5) {
    return weight;
  }

  const auto affinity = clamp_unit(background_distance / (foreground_distance + background_distance + 1e-6));
  const auto ambiguous_band = 1.0 - std::abs((weight * 2.0) - 1.0);
  const auto separation_gate = smoothstep(0.015, 0.18, separation);
  const auto blend = clamp_unit(refinement * matte_edge * ambiguous_band * separation_gate * 0.92);
  return clamp_unit(std::lerp(weight, affinity, blend));
}

[[nodiscard]] double local_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask,
    const EdgeAwareMatteContext* source_context) {
  if (!mask.enabled) {
    return 0.0;
  }
  const auto refined_weight = [&](double base_weight) {
    auto spatial_expand_smooth_applied = false;
    auto refined_base = apply_raster_selection_morphology(
        base_weight,
        normalized_x,
        normalized_y,
        mask,
        source_context,
        &spatial_expand_smooth_applied);
    return apply_range_refinement(
        apply_selection_refine(
            apply_edge_aware_matte(refined_base, normalized_x, normalized_y, mask, source_context),
            mask,
            spatial_expand_smooth_applied),
        normalized_x,
        normalized_y,
        pixel,
        context,
        mask);
  };

  if (is_ai_segmentation_mask_kind(mask.kind)) {
    return refined_weight(subject_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "radial" || mask.kind == "ellipse") {
    return refined_weight(radial_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "rectangle") {
    return refined_weight(rectangle_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "brush") {
    return refined_weight(brush_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "polygon" || mask.kind == "lasso") {
    return refined_weight(polygon_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "luminance") {
    return refined_weight(luminance_range_mask_weight(pixel, context, mask));
  }

  if (mask.kind == "color") {
    return refined_weight(color_range_mask_weight(pixel, context, mask));
  }

  if (mask.kind == "depth") {
    return refined_weight(depth_range_mask_weight(normalized_x, normalized_y, pixel, context, mask));
  }

  return refined_weight(linear_mask_weight(normalized_x, normalized_y, mask.position, mask.feather, mask.rotation));
}

[[nodiscard]] double nested_local_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask,
    const std::vector<LocalMask>& siblings,
    const EdgeAwareMatteContext* source_context) {
  auto weight = local_mask_weight(normalized_x, normalized_y, pixel, context, mask, source_context);
  if (mask.parent_id.empty() || weight <= 0.0) {
    return weight;
  }

  for (const auto& parent : siblings) {
    if (parent.id != mask.parent_id || parent.id == mask.id) {
      continue;
    }
    weight *= local_mask_weight(normalized_x, normalized_y, pixel, context, parent, source_context);
    break;
  }
  return clamp_unit(weight);
}

void accumulate_composite_weight(
    double raw_weight,
    std::string_view operation,
    std::string_view blend_mode,
    double* composite_weight,
    bool* initialized);

[[nodiscard]] bool local_mask_set_has_adjustments(const LocalMaskSet& mask_set) {
  return std::abs(mask_set.exposure) > 1e-6 ||
         std::abs(mask_set.contrast) > 1e-6 ||
         std::abs(mask_set.highlights) > 1e-6 ||
         std::abs(mask_set.shadows) > 1e-6 ||
         std::abs(mask_set.whites) > 1e-6 ||
         std::abs(mask_set.blacks) > 1e-6 ||
         std::abs(mask_set.temperature) > 1e-6 ||
         std::abs(mask_set.tint) > 1e-6 ||
         std::abs(mask_set.vibrance) > 1e-6 ||
         std::abs(mask_set.saturation) > 1e-6 ||
         std::abs(mask_set.hue_shift) > 1e-6 ||
         std::abs(mask_set.saturation_shift) > 1e-6 ||
         std::abs(mask_set.colorize_hue) > 1e-6 ||
         std::abs(mask_set.colorize_strength) > 1e-6;
}

[[nodiscard]] double apply_mask_set_post_composition_refine(
    double base_weight,
    const LocalMaskSet& mask_set) {
  LocalMask refinement;
  refinement.selection_invert = mask_set.selection_invert;
  refinement.selection_expand = mask_set.selection_expand;
  refinement.selection_smooth = mask_set.selection_smooth;
  refinement.selection_fine_edge = mask_set.selection_fine_edge;
  refinement.selection_edge_contrast = mask_set.selection_edge_contrast;
  refinement.opacity = mask_set.opacity;
  refinement.density = mask_set.density;
  return apply_selection_refine(base_weight, refinement);
}

[[nodiscard]] double local_mask_set_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMaskSet& mask_set,
    const EdgeAwareMatteContext* source_context) {
  if (!mask_set.enabled) {
    return 0.0;
  }

  auto composite_weight = 0.0;
  auto initialized = false;
  for (const auto& source : mask_set.sources) {
    if (!source.enabled) {
      continue;
    }

    const auto source_weight_after_source_refinement =
        nested_local_mask_weight(
            normalized_x, normalized_y, pixel, context, source, mask_set.sources, source_context);
    accumulate_composite_weight(
        source_weight_after_source_refinement,
        source.operation,
        source.blend_mode,
        &composite_weight,
        &initialized);
  }

  return initialized ? apply_mask_set_post_composition_refine(composite_weight, mask_set) : 0.0;
}

// A selection-only mask has no local adjustments and no retouch mode.
// It acts purely as a selection modifier (add/subtract/intersect) for other
// masks in the stack, so it should not prevent the composite rendering path.
[[nodiscard]] bool is_selection_only_mask(const LocalMask& mask) {
  return mask.retouch_mode.empty() &&
         std::abs(mask.exposure) <= 1e-6 &&
         std::abs(mask.contrast) <= 1e-6 &&
         std::abs(mask.highlights) <= 1e-6 &&
         std::abs(mask.shadows) <= 1e-6 &&
         std::abs(mask.whites) <= 1e-6 &&
         std::abs(mask.blacks) <= 1e-6 &&
         std::abs(mask.temperature) <= 1e-6 &&
         std::abs(mask.tint) <= 1e-6 &&
         std::abs(mask.vibrance) <= 1e-6 &&
         std::abs(mask.saturation) <= 1e-6 &&
         std::abs(mask.hue_shift) <= 1e-6 &&
         std::abs(mask.saturation_shift) <= 1e-6 &&
         std::abs(mask.colorize_hue) <= 1e-6 &&
         std::abs(mask.colorize_strength) <= 1e-6 &&
         std::abs(mask.remove_strength) <= 1e-6;
}

[[nodiscard]] bool has_selection_decontaminate(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }
    if (std::clamp(mask.selection_decontaminate, 0.0, 100.0) > 1e-6) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_selection_decontaminate(const LocalMaskSet& mask_set) {
  if (!mask_set.enabled) {
    return false;
  }
  for (const auto& source : mask_set.sources) {
    if (!source.enabled) {
      continue;
    }
    if (std::clamp(source.selection_decontaminate, 0.0, 100.0) > 1e-6) {
      return true;
    }
  }
  return false;
}

void apply_selection_decontaminate_stages(
    ColorPixel* pixel,
    double normalized_x,
    double normalized_y,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks,
    const EdgeAwareMatteContext* source_context) {
  if (pixel == nullptr) {
    return;
  }

  const auto weight_pixel = *pixel;
  for (const auto& mask : masks) {
    const auto decontaminate = std::clamp(mask.selection_decontaminate, 0.0, 100.0);
    if (!mask.enabled || decontaminate <= 1e-6) {
      continue;
    }
    const auto mask_weight = nested_local_mask_weight(
        normalized_x,
        normalized_y,
        weight_pixel,
        context,
        mask,
        masks,
        source_context);
    apply_mask_edge_cleanup(pixel, context, mask_weight, decontaminate);
  }
}

void apply_selection_decontaminate_stages(
    ColorPixel* pixel,
    double normalized_x,
    double normalized_y,
    const PipelineColorContext& context,
    const LocalMaskSet& mask_set,
    const EdgeAwareMatteContext* source_context) {
  if (pixel == nullptr || !mask_set.enabled) {
    return;
  }
  apply_selection_decontaminate_stages(
      pixel,
      normalized_x,
      normalized_y,
      context,
      mask_set.sources,
      source_context);
}

void accumulate_composite_weight(
    double raw_weight,
    std::string_view operation,
    std::string_view blend_mode,
    double* composite_weight,
    bool* initialized) {
  if (composite_weight == nullptr || initialized == nullptr) {
    return;
  }

  if (operation == "subtract") {
    if (!*initialized) {
      return;
    }
    *composite_weight = std::max(0.0, *composite_weight - raw_weight);
    return;
  }

  if (operation == "intersect") {
    if (!*initialized) {
      return;
    }
    *composite_weight *= raw_weight;
    return;
  }

  if (operation == "exclude") {
    if (!*initialized) {
      *composite_weight = raw_weight;
      *initialized = true;
      return;
    }
    *composite_weight = clamp_unit(*composite_weight + raw_weight - (2.0 * *composite_weight * raw_weight));
    return;
  }

  if (operation == "replace") {
    *composite_weight = raw_weight;
    *initialized = true;
    return;
  }

  if (!*initialized) {
    *composite_weight = raw_weight;
    *initialized = true;
    return;
  }

  if (blend_mode == "multiply") {
    *composite_weight *= raw_weight;
  } else if (blend_mode == "overlay") {
    *composite_weight = *composite_weight < 0.5
                            ? 2.0 * *composite_weight * raw_weight
                            : 1.0 - (2.0 * (1.0 - *composite_weight) * (1.0 - raw_weight));
  } else if (blend_mode == "lighten") {
    *composite_weight = std::max(*composite_weight, raw_weight);
  } else if (blend_mode == "darken") {
    *composite_weight = std::min(*composite_weight, raw_weight);
  } else if (blend_mode == "replace") {
    *composite_weight = raw_weight;
  } else {
    *composite_weight = 1.0 - (1.0 - *composite_weight) * (1.0 - raw_weight);
  }
}

[[nodiscard]] bool local_masks_share_adjustments(
    const std::vector<LocalMask>& masks,
    LocalAdjustmentSettings* shared_adjustments) {
  auto found_adjustment = false;
  LocalAdjustmentSettings adjustments;
  auto selection_initialized = false;

  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }

    const auto seeds_selection = mask.operation != "subtract" && mask.operation != "intersect";
    const auto can_contribute_adjustments = selection_initialized || seeds_selection;
    selection_initialized = selection_initialized || seeds_selection;
    if (!can_contribute_adjustments) {
      continue;
    }

    // Skip selection-only masks — they refine the composite selection area
    // without contributing their own adjustments, so they should not cause
    // the renderer to fall back to the independent per-mask path.
    if (is_selection_only_mask(mask)) {
      continue;
    }

    if (!found_adjustment) {
      adjustments.exposure = mask.exposure;
      adjustments.contrast = mask.contrast;
      adjustments.highlights = mask.highlights;
      adjustments.shadows = mask.shadows;
      adjustments.whites = mask.whites;
      adjustments.blacks = mask.blacks;
      adjustments.temperature = mask.temperature;
      adjustments.tint = mask.tint;
      adjustments.vibrance = mask.vibrance;
      adjustments.saturation = mask.saturation;
      adjustments.hue_shift = mask.hue_shift;
      adjustments.saturation_shift = mask.saturation_shift;
      adjustments.colorize_hue = mask.colorize_hue;
      adjustments.colorize_strength = mask.colorize_strength;
      adjustments.remove_strength = mask.remove_strength;
      found_adjustment = true;
      continue;
    }

    if (std::abs(mask.exposure - adjustments.exposure) > 1e-6 ||
        std::abs(mask.contrast - adjustments.contrast) > 1e-6 ||
        std::abs(mask.highlights - adjustments.highlights) > 1e-6 ||
        std::abs(mask.shadows - adjustments.shadows) > 1e-6 ||
        std::abs(mask.whites - adjustments.whites) > 1e-6 ||
        std::abs(mask.blacks - adjustments.blacks) > 1e-6 ||
        std::abs(mask.temperature - adjustments.temperature) > 1e-6 ||
        std::abs(mask.tint - adjustments.tint) > 1e-6 ||
        std::abs(mask.vibrance - adjustments.vibrance) > 1e-6 ||
        std::abs(mask.saturation - adjustments.saturation) > 1e-6 ||
        std::abs(mask.hue_shift - adjustments.hue_shift) > 1e-6 ||
        std::abs(mask.saturation_shift - adjustments.saturation_shift) > 1e-6 ||
        std::abs(mask.colorize_hue - adjustments.colorize_hue) > 1e-6 ||
        std::abs(mask.colorize_strength - adjustments.colorize_strength) > 1e-6 ||
        std::abs(mask.remove_strength - adjustments.remove_strength) > 1e-6) {
      return false;
    }
  }

  if (!found_adjustment) {
    return false;
  }

  if (shared_adjustments != nullptr) {
    *shared_adjustments = adjustments;
  }
  return true;
}

[[nodiscard]] bool local_masks_all_additive(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }
    if (mask.operation != "add" && mask.operation != "union") {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool primary_local_mask_adjustments(
    const std::vector<LocalMask>& masks,
    LocalAdjustmentSettings* adjustments) {
  auto selection_initialized = false;
  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }
    const auto seeds_selection = mask.operation != "subtract" && mask.operation != "intersect";
    const auto can_contribute_adjustments = selection_initialized || seeds_selection;
    selection_initialized = selection_initialized || seeds_selection;
    if (!can_contribute_adjustments) {
      continue;
    }
    // Skip selection-only masks so that the primary adjustments come from the
    // first mask that actually has values (e.g. a subject or radial mask),
    // not from a zero-adjustment refinement brush.
    if (is_selection_only_mask(mask)) {
      continue;
    }
    if (adjustments != nullptr) {
      adjustments->exposure = mask.exposure;
      adjustments->contrast = mask.contrast;
      adjustments->highlights = mask.highlights;
      adjustments->shadows = mask.shadows;
      adjustments->whites = mask.whites;
      adjustments->blacks = mask.blacks;
      adjustments->temperature = mask.temperature;
      adjustments->tint = mask.tint;
      adjustments->vibrance = mask.vibrance;
      adjustments->saturation = mask.saturation;
      adjustments->hue_shift = mask.hue_shift;
      adjustments->saturation_shift = mask.saturation_shift;
      adjustments->colorize_hue = mask.colorize_hue;
      adjustments->colorize_strength = mask.colorize_strength;
      adjustments->remove_strength = mask.remove_strength;
    }
    return true;
  }
  return false;
}

[[nodiscard]] bool has_local_mask_removal(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled || mask.remove_strength <= 1e-6 ||
        !mask.inpaint_patch_pixels.empty()) {
      continue;
    }
    if (mask.retouch_mode == "heal" || mask.retouch_mode == "clone" ||
        mask.retouch_mode == "red-eye" || mask.retouch_mode == "skin-cleanup" ||
        mask.retouch_mode == "frequency-smoothing") {
      continue;
    }
    if (mask.enabled && mask.remove_strength > 1e-6) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_heal(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (mask.enabled && mask.remove_strength > 1e-6 && mask.retouch_mode == "heal" &&
        mask.inpaint_patch_pixels.empty()) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_clone(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled || mask.kind != "brush" || mask.operation != "add") {
      continue;
    }
    if (mask.retouch_mode == "clone" &&
        mask.clone_source_set &&
        !mask.brush_points.empty()) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_red_eye(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (mask.enabled && mask.remove_strength > 1e-6 && mask.retouch_mode == "red-eye") {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_skin_cleanup(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (mask.enabled && mask.remove_strength > 1e-6 && mask.retouch_mode == "skin-cleanup") {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_frequency_smoothing(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (mask.enabled && mask.remove_strength > 1e-6 && mask.retouch_mode == "frequency-smoothing") {
      return true;
    }
  }
  return false;
}

[[nodiscard]] double composite_local_removal_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks,
    std::string_view retouch_mode) {
  auto composite_weight = 0.0;
  auto initialized = false;

  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }

    if (!mask.inpaint_patch_pixels.empty()) {
      continue;
    }

    if (!retouch_mode.empty()) {
      if (mask.retouch_mode != retouch_mode) {
        continue;
      }
    } else {
      if (mask.retouch_mode == "heal" || mask.retouch_mode == "clone" ||
          mask.retouch_mode == "red-eye" || mask.retouch_mode == "skin-cleanup" ||
          mask.retouch_mode == "frequency-smoothing") {
        continue;
      }
    }

    const auto strength = clamp_unit(mask.remove_strength / 100.0);
    if (strength <= 1e-6) {
      continue;
    }

    const auto weighted =
        nested_local_mask_weight(normalized_x, normalized_y, pixel, context, mask, masks) * strength;
    accumulate_composite_weight(
        weighted, mask.operation, mask.blend_mode, &composite_weight, &initialized);
  }

  return initialized ? clamp_unit(composite_weight) : 0.0;
}

[[nodiscard]] HealComposite composite_local_heal(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  auto composite_weight = 0.0;
  auto initialized = false;
  auto texture_sum = 0.0;
  auto color_sum = 0.0;
  auto control_weight_sum = 0.0;

  for (const auto& mask : masks) {
    if (!mask.enabled || !mask.inpaint_patch_pixels.empty() || mask.retouch_mode != "heal") {
      continue;
    }

    const auto strength = clamp_unit(mask.remove_strength / 100.0);
    if (strength <= 1e-6) {
      continue;
    }

    const auto weighted =
        nested_local_mask_weight(normalized_x, normalized_y, pixel, context, mask, masks) * strength;
    if (weighted > 1e-6) {
      texture_sum += weighted * clamp_unit(mask.heal_texture_blend);
      color_sum += weighted * clamp_unit(mask.heal_color_blend);
      control_weight_sum += weighted;
    }
    accumulate_composite_weight(
        weighted, mask.operation, mask.blend_mode, &composite_weight, &initialized);
  }

  if (!initialized) {
    return {};
  }
  return HealComposite{
      .weight = clamp_unit(composite_weight),
      .texture_blend = control_weight_sum > 1e-6 ? texture_sum / control_weight_sum : 0.68,
      .color_blend = control_weight_sum > 1e-6 ? color_sum / control_weight_sum : 0.46,
  };
}

struct RemovalSample final {
  ColorPixel pixel;
  double confidence = 0.0;
  int distance = 0;
  bool valid = false;
};

struct RemovalCandidate final {
  ColorPixel pixel;
  double confidence = 0.0;
  double score = 0.0;
  bool valid = false;
};

[[nodiscard]] std::vector<double> grow_mask_weights(
    const std::vector<double>& mask,
    int width,
    int height,
    int radius) {
  if (radius <= 0 || width <= 0 || height <= 0 ||
      mask.size() != static_cast<std::size_t>(width * height)) {
    return mask;
  }

  auto grown = mask;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto maximum = clamp_unit(mask[static_cast<std::size_t>(y * width + x)]);
      for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
        const auto sample_y = y + offset_y;
        if (sample_y < 0 || sample_y >= height) {
          continue;
        }
        for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
          const auto sample_x = x + offset_x;
          if (sample_x < 0 || sample_x >= width) {
            continue;
          }
          if (offset_x * offset_x + offset_y * offset_y > radius * radius) {
            continue;
          }
          maximum = std::max(
              maximum,
              clamp_unit(mask[static_cast<std::size_t>(sample_y * width + sample_x)]));
        }
      }
      grown[static_cast<std::size_t>(y * width + x)] = maximum;
    }
  }
  return grown;
}

[[nodiscard]] ColorPixel separated_heal_pixel(
    const ColorPixel& source,
    const ColorPixel& source_low,
    const ColorPixel& fill,
    const ColorPixel& fill_low,
    double texture_blend,
    double color_blend) {
  const auto clamped_texture = clamp_unit(texture_blend);
  const auto clamped_color = clamp_unit(color_blend);
  const auto channel = [&](float source_low_channel,
                           float fill_channel,
                           float fill_low_channel) {
    const auto low_frequency =
        std::lerp(static_cast<double>(source_low_channel), static_cast<double>(fill_low_channel), clamped_color);
    const auto detail =
        (static_cast<double>(fill_channel) - static_cast<double>(fill_low_channel)) * clamped_texture;
    return static_cast<float>(std::clamp(low_frequency + detail, 0.0, 1.0));
  };
  return ColorPixel{
      .red = channel(source_low.red, fill.red, fill_low.red),
      .green = channel(source_low.green, fill.green, fill_low.green),
      .blue = channel(source_low.blue, fill.blue, fill_low.blue),
      .alpha = source.alpha,
  };
}

[[nodiscard]] std::vector<ColorPixel> low_frequency_pixels(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    int radius,
    CancellationToken cancellation = {}) {
  const auto pixel_count = pixels.size();
  std::vector<float> red(pixel_count, 0.0F);
  std::vector<float> green(pixel_count, 0.0F);
  std::vector<float> blue(pixel_count, 0.0F);
  for (std::size_t index = 0; index < pixel_count; ++index) {
    if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
      return {};
    }
    red[index] = pixels[index].red;
    green[index] = pixels[index].green;
    blue[index] = pixels[index].blue;
  }
  const auto blurred_red = box_blur(red, width, height, radius, cancellation);
  const auto blurred_green = box_blur(green, width, height, radius, cancellation);
  const auto blurred_blue = box_blur(blue, width, height, radius, cancellation);
  if (cancellation.cancelled() || blurred_red.size() != pixel_count ||
      blurred_green.size() != pixel_count || blurred_blue.size() != pixel_count) {
    return {};
  }
  std::vector<ColorPixel> low(pixel_count);
  for (std::size_t index = 0; index < pixel_count; ++index) {
    low[index] = ColorPixel{
        .red = blurred_red[index],
        .green = blurred_green[index],
        .blue = blurred_blue[index],
        .alpha = pixels[index].alpha,
    };
  }
  return low;
}

[[nodiscard]] double removal_color_distance(const ColorPixel& left, const ColorPixel& right) {
  const auto dr = static_cast<double>(left.red) - static_cast<double>(right.red);
  const auto dg = static_cast<double>(left.green) - static_cast<double>(right.green);
  const auto db = static_cast<double>(left.blue) - static_cast<double>(right.blue);
  return std::sqrt(dr * dr + dg * dg + db * db);
}

[[nodiscard]] RemovalSample nearest_known_removal_sample(
    const std::vector<ColorPixel>& pixels,
    const std::vector<float>& confidence,
    int width,
    int height,
    int start_x,
    int start_y,
    int step_x,
    int step_y,
    int max_distance) {
  RemovalSample sample;
  if (width <= 0 || height <= 0 || start_x < 0 || start_y < 0 ||
      start_x >= width || start_y >= height || max_distance <= 0) {
    return sample;
  }

  for (int distance = 1; distance <= max_distance; ++distance) {
    const auto x = start_x + step_x * distance;
    const auto y = start_y + step_y * distance;
    if (x < 0 || y < 0 || x >= width || y >= height) {
      break;
    }
    const auto index = static_cast<std::size_t>(y * width + x);
    if (confidence[index] <= 1e-4F) {
      continue;
    }
    sample.pixel = pixels[index];
    sample.confidence = static_cast<double>(confidence[index]);
    sample.distance = distance;
    sample.valid = true;
    return sample;
  }

  return sample;
}

[[nodiscard]] bool has_known_neighbor(
    const std::vector<float>& confidence,
    int width,
    int height,
    int x,
    int y) {
  for (int offset_y = -1; offset_y <= 1; ++offset_y) {
    for (int offset_x = -1; offset_x <= 1; ++offset_x) {
      if (offset_x == 0 && offset_y == 0) {
        continue;
      }
      const auto sample_x = x + offset_x;
      const auto sample_y = y + offset_y;
      if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
        continue;
      }
      if (confidence[static_cast<std::size_t>(sample_y * width + sample_x)] > 1e-4F) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] RemovalCandidate directional_removal_candidate(
    const std::vector<ColorPixel>& pixels,
    const std::vector<float>& confidence,
    int width,
    int height,
    int x,
    int y,
    int max_line_radius) {
  RemovalCandidate best_candidate;
  const auto sample_structure_strength =
      [&](int sample_x, int sample_y, int step_x, int step_y) {
        if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
          return 0.0;
        }
        const auto sample_index = static_cast<std::size_t>(sample_y * width + sample_x);
        if (confidence[sample_index] <= 1e-4F) {
          return 0.0;
        }
        const auto perp_x = -step_y;
        const auto perp_y = step_x;
        auto structure = 0.0;
        auto count = 0.0;
        for (const auto sign : {-1, 1}) {
          const auto neighbor_x = sample_x + perp_x * sign;
          const auto neighbor_y = sample_y + perp_y * sign;
          if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= width || neighbor_y >= height) {
            continue;
          }
          const auto neighbor_index = static_cast<std::size_t>(neighbor_y * width + neighbor_x);
          if (confidence[neighbor_index] <= 1e-4F) {
            continue;
          }
          structure += removal_color_distance(pixels[sample_index], pixels[neighbor_index]);
          count += 1.0;
        }
        return count > 0.0 ? structure / count : 0.0;
      };
  constexpr std::array<std::pair<int, int>, 4> kDirections{
      std::pair<int, int>{1, 0},
      std::pair<int, int>{0, 1},
      std::pair<int, int>{1, 1},
      std::pair<int, int>{1, -1},
  };

  for (const auto& [step_x, step_y] : kDirections) {
    const auto forward = nearest_known_removal_sample(
        pixels, confidence, width, height, x, y, step_x, step_y, max_line_radius);
    const auto backward = nearest_known_removal_sample(
        pixels, confidence, width, height, x, y, -step_x, -step_y, max_line_radius);
    if (!forward.valid && !backward.valid) {
      continue;
    }

    RemovalCandidate candidate;
    if (forward.valid && backward.valid) {
      const auto forward_weight = forward.confidence / static_cast<double>(std::max(1, forward.distance));
      const auto backward_weight = backward.confidence / static_cast<double>(std::max(1, backward.distance));
      const auto weight_sum = std::max(1e-6, forward_weight + backward_weight);
      candidate.pixel = ColorPixel{
          .red = static_cast<float>(std::max(
              0.0,
              (static_cast<double>(forward.pixel.red) * forward_weight +
               static_cast<double>(backward.pixel.red) * backward_weight) /
                  weight_sum)),
          .green = static_cast<float>(std::max(
              0.0,
              (static_cast<double>(forward.pixel.green) * forward_weight +
               static_cast<double>(backward.pixel.green) * backward_weight) /
                  weight_sum)),
          .blue = static_cast<float>(std::max(
              0.0,
              (static_cast<double>(forward.pixel.blue) * forward_weight +
               static_cast<double>(backward.pixel.blue) * backward_weight) /
                  weight_sum)),
          .alpha = 1.0F,
      };
      const auto color_match =
          1.0 / (1.0 + removal_color_distance(forward.pixel, backward.pixel) * 6.0);
      const auto structure_strength =
          sample_structure_strength(x + step_x * forward.distance, y + step_y * forward.distance, step_x, step_y) +
          sample_structure_strength(x - step_x * backward.distance, y - step_y * backward.distance, step_x, step_y);
      const auto distance_sum = static_cast<double>(forward.distance + backward.distance);
      candidate.confidence = std::clamp(
          0.45 + 0.25 * std::min(forward.confidence, backward.confidence) + 0.20 * color_match +
              std::min(0.10, structure_strength * 0.10),
          0.0, 1.0);
      candidate.score =
          (forward_weight + backward_weight) * color_match *
          (1.0 + std::min(1.5, structure_strength * 4.0)) /
          std::max(1.0, distance_sum * 0.5);
    } else {
      const auto& sample = forward.valid ? forward : backward;
      candidate.pixel = sample.pixel;
      candidate.confidence =
          std::clamp(0.28 + 0.45 * sample.confidence, 0.0, 0.9);
      candidate.score =
          (sample.confidence / static_cast<double>(std::max(1, sample.distance))) * 0.35;
    }
    candidate.pixel.alpha = pixels[static_cast<std::size_t>(y * width + x)].alpha;
    candidate.valid = true;
    if (!best_candidate.valid || candidate.score > best_candidate.score) {
      best_candidate = candidate;
    }
  }

  RemovalCandidate neighborhood_candidate;
  double weighted_red = 0.0;
  double weighted_green = 0.0;
  double weighted_blue = 0.0;
  double total_weight = 0.0;
  double confidence_sum = 0.0;
  for (int offset_y = -2; offset_y <= 2; ++offset_y) {
    for (int offset_x = -2; offset_x <= 2; ++offset_x) {
      if (offset_x == 0 && offset_y == 0) {
        continue;
      }
      const auto sample_x = x + offset_x;
      const auto sample_y = y + offset_y;
      if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
        continue;
      }
      const auto index = static_cast<std::size_t>(sample_y * width + sample_x);
      const auto sample_confidence = static_cast<double>(confidence[index]);
      if (sample_confidence <= 1e-4) {
        continue;
      }
      const auto distance_sq = static_cast<double>(offset_x * offset_x + offset_y * offset_y);
      const auto weight = sample_confidence / std::max(1.0, distance_sq);
      weighted_red += static_cast<double>(pixels[index].red) * weight;
      weighted_green += static_cast<double>(pixels[index].green) * weight;
      weighted_blue += static_cast<double>(pixels[index].blue) * weight;
      total_weight += weight;
      confidence_sum += sample_confidence;
    }
  }
  if (total_weight > 1e-6) {
    neighborhood_candidate.pixel = ColorPixel{
        .red = static_cast<float>(std::max(0.0, weighted_red / total_weight)),
        .green = static_cast<float>(std::max(0.0, weighted_green / total_weight)),
        .blue = static_cast<float>(std::max(0.0, weighted_blue / total_weight)),
        .alpha = pixels[static_cast<std::size_t>(y * width + x)].alpha,
    };
    neighborhood_candidate.confidence =
        std::clamp(0.20 + confidence_sum / (total_weight * 8.0), 0.0, 0.85);
    neighborhood_candidate.score = total_weight * 0.06;
    neighborhood_candidate.valid = true;
  }

  if (!best_candidate.valid ||
      (neighborhood_candidate.valid && neighborhood_candidate.score > best_candidate.score)) {
    return neighborhood_candidate;
  }
  return best_candidate;
}

void apply_local_removal(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const std::vector<double>& removal_mask,
    double detail_preservation,
    RemovalQuality removal_quality,
    double texture_blend,
    double color_blend,
    CancellationToken cancellation) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() != removal_mask.size()) {
    return;
  }
  if (cancellation.cancelled()) {
    return;
  }

  const auto pixel_count = pixels->size();
  const auto source_exclusion_radius =
      removal_quality == RemovalQuality::Final
          ? (detail_preservation >= 0.45 ? 1 : 2)
          : 0;
  const auto working_mask =
      source_exclusion_radius > 0
          ? grow_mask_weights(removal_mask, width, height, source_exclusion_radius)
          : removal_mask;
  auto filled_pixels = *pixels;
  std::vector<float> confidence(pixel_count, 1.0F);
  std::vector<uint8_t> unresolved(pixel_count, 0);
  auto has_masked_pixels = false;
  for (std::size_t index = 0; index < pixel_count; ++index) {
    if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
      return;
    }
    confidence[index] = static_cast<float>(1.0 - clamp_unit(working_mask[index]));
    has_masked_pixels = has_masked_pixels || working_mask[index] > 1e-4;
    unresolved[index] = working_mask[index] > 1e-4 ? 1U : 0U;
  }
  if (!has_masked_pixels) {
    return;
  }

  const auto max_line_radius =
      std::clamp(std::min(width, height) / 10, 3, 24);
  auto unresolved_count = 0;
  for (const auto flag : unresolved) {
    unresolved_count += flag != 0 ? 1 : 0;
  }

  while (unresolved_count > 0) {
    if (cancellation.cancelled()) {
      return;
    }
    auto next_pixels = filled_pixels;
    auto next_confidence = confidence;
    std::vector<std::size_t> resolved_indices;
    resolved_indices.reserve(static_cast<std::size_t>(unresolved_count));

    for (int y = 0; y < height; ++y) {
      if (cancellation.cancelled()) {
        return;
      }
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        if (unresolved[index] == 0U || !has_known_neighbor(confidence, width, height, x, y)) {
          continue;
        }

        const auto candidate = directional_removal_candidate(
            filled_pixels,
            confidence,
            width,
            height,
            x,
            y,
            max_line_radius);
        if (!candidate.valid) {
          continue;
        }

        const auto mask_weight = clamp_unit(working_mask[index]);
        const auto blend =
            std::clamp(mask_weight * (0.78 + candidate.confidence * 0.22), 0.0, 1.0);
        next_pixels[index].red = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].red), static_cast<double>(candidate.pixel.red), blend));
        next_pixels[index].green = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].green), static_cast<double>(candidate.pixel.green), blend));
        next_pixels[index].blue = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].blue), static_cast<double>(candidate.pixel.blue), blend));
        next_confidence[index] = static_cast<float>(std::max(
            static_cast<double>(next_confidence[index]),
            std::clamp(candidate.confidence, 0.0, 1.0)));
        resolved_indices.push_back(index);
      }
    }

    if (resolved_indices.empty()) {
      break;
    }

    filled_pixels = std::move(next_pixels);
    confidence = std::move(next_confidence);
    for (const auto index : resolved_indices) {
      if (unresolved[index] == 0U) {
        continue;
      }
      unresolved[index] = 0U;
      --unresolved_count;
    }
  }

  if (unresolved_count > 0) {
    constexpr std::array<int, 5> kFillRadii{2, 4, 8, 12, 18};
    for (const auto radius : kFillRadii) {
      if (cancellation.cancelled()) {
        return;
      }
      std::vector<float> weighted_red(pixel_count, 0.0F);
      std::vector<float> weighted_green(pixel_count, 0.0F);
      std::vector<float> weighted_blue(pixel_count, 0.0F);

      for (std::size_t index = 0; index < pixel_count; ++index) {
        if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
          return;
        }
        weighted_red[index] = static_cast<float>(filled_pixels[index].red) * confidence[index];
        weighted_green[index] = static_cast<float>(filled_pixels[index].green) * confidence[index];
        weighted_blue[index] = static_cast<float>(filled_pixels[index].blue) * confidence[index];
      }

      const auto blurred_confidence = box_blur(confidence, width, height, radius, cancellation);
      const auto blurred_red = box_blur(weighted_red, width, height, radius, cancellation);
      const auto blurred_green = box_blur(weighted_green, width, height, radius, cancellation);
      const auto blurred_blue = box_blur(weighted_blue, width, height, radius, cancellation);
      if (cancellation.cancelled()) {
        return;
      }

      for (std::size_t index = 0; index < pixel_count; ++index) {
        if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
          return;
        }
        if (unresolved[index] == 0U) {
          continue;
        }
        const auto local_confidence = static_cast<double>(blurred_confidence[index]);
        if (local_confidence <= 1e-5) {
          continue;
        }

        const auto candidate = ColorPixel{
            .red = static_cast<float>(std::max(0.0, static_cast<double>(blurred_red[index]) / local_confidence)),
            .green = static_cast<float>(std::max(0.0, static_cast<double>(blurred_green[index]) / local_confidence)),
            .blue = static_cast<float>(std::max(0.0, static_cast<double>(blurred_blue[index]) / local_confidence)),
            .alpha = filled_pixels[index].alpha,
        };
        const auto blend =
            std::clamp(0.40 + (1.0 - static_cast<double>(confidence[index])) * 0.60, 0.0, 1.0);
        filled_pixels[index].red = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].red), static_cast<double>(candidate.red), blend));
        filled_pixels[index].green = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].green), static_cast<double>(candidate.green), blend));
        filled_pixels[index].blue = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].blue), static_cast<double>(candidate.blue), blend));
        confidence[index] = static_cast<float>(std::max(
            static_cast<double>(confidence[index]),
            std::clamp(local_confidence, 0.0, 1.0)));
      }
    }
  }

  if (detail_preservation > 1e-6) {
    std::vector<float> filled_red(pixel_count, 0.0F);
    std::vector<float> filled_green(pixel_count, 0.0F);
    std::vector<float> filled_blue(pixel_count, 0.0F);
    for (std::size_t index = 0; index < pixel_count; ++index) {
      if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
        return;
      }
      filled_red[index] = filled_pixels[index].red;
      filled_green[index] = filled_pixels[index].green;
      filled_blue[index] = filled_pixels[index].blue;
    }
    const auto blurred_red = box_blur(filled_red, width, height, 2, cancellation);
    const auto blurred_green = box_blur(filled_green, width, height, 2, cancellation);
    const auto blurred_blue = box_blur(filled_blue, width, height, 2, cancellation);
    if (cancellation.cancelled()) {
      return;
    }
    for (std::size_t index = 0; index < pixel_count; ++index) {
      if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
        return;
      }
      const auto mask_weight = clamp_unit(working_mask[index]);
      if (mask_weight <= 1e-4) {
        continue;
      }
      const auto detail_gain = detail_preservation * mask_weight * 0.30;
      filled_pixels[index].red = static_cast<float>(std::max(
          0.0,
          static_cast<double>(filled_pixels[index].red) +
              static_cast<double>(filled_red[index] - blurred_red[index]) * detail_gain));
      filled_pixels[index].green = static_cast<float>(std::max(
          0.0,
          static_cast<double>(filled_pixels[index].green) +
              static_cast<double>(filled_green[index] - blurred_green[index]) * detail_gain));
      filled_pixels[index].blue = static_cast<float>(std::max(
          0.0,
          static_cast<double>(filled_pixels[index].blue) +
              static_cast<double>(filled_blue[index] - blurred_blue[index]) * detail_gain));
    }
  }

  if (removal_quality == RemovalQuality::Final) {
    auto min_mask_x = width;
    auto min_mask_y = height;
    auto max_mask_x = -1;
    auto max_mask_y = -1;
    auto masked_pixel_count = 0;
    for (int y = 0; y < height; ++y) {
      if (cancellation.cancelled()) {
        return;
      }
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        if (clamp_unit(working_mask[index]) <= 1e-4) {
          continue;
        }
        min_mask_x = std::min(min_mask_x, x);
        min_mask_y = std::min(min_mask_y, y);
        max_mask_x = std::max(max_mask_x, x);
        max_mask_y = std::max(max_mask_y, y);
        ++masked_pixel_count;
      }
    }

    if (masked_pixel_count > 0) {
      const auto search_radius = std::clamp(std::max(width, height) / 16, 12, 64);
      const auto min_search_x = std::max(0, min_mask_x - search_radius);
      const auto min_search_y = std::max(0, min_mask_y - search_radius);
      const auto max_search_x = std::min(width - 1, max_mask_x + search_radius);
      const auto max_search_y = std::min(height - 1, max_mask_y + search_radius);

      std::vector<std::size_t> candidate_indices;
      candidate_indices.reserve(
          static_cast<std::size_t>(std::max(0, (max_search_x - min_search_x + 1) *
                                                   (max_search_y - min_search_y + 1))));
      for (int y = min_search_y; y <= max_search_y; ++y) {
        if (cancellation.cancelled()) {
          return;
        }
        for (int x = min_search_x; x <= max_search_x; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          if (clamp_unit(working_mask[index]) > 1e-4) {
            continue;
          }
          candidate_indices.push_back(index);
        }
      }

      if (!candidate_indices.empty()) {
        const auto candidate_stride =
            std::max(1, static_cast<int>(candidate_indices.size() / 4096));
        const auto patch_radius = 2;
        auto refined_pixels = filled_pixels;
        for (int y = min_mask_y; y <= max_mask_y; ++y) {
          if (cancellation.cancelled()) {
            return;
          }
          for (int x = min_mask_x; x <= max_mask_x; ++x) {
            const auto index = static_cast<std::size_t>(y * width + x);
            const auto mask_weight = clamp_unit(working_mask[index]);
            if (mask_weight <= 1e-4) {
              continue;
            }

            auto best_score = std::numeric_limits<double>::max();
            auto best_candidate = refined_pixels[index];
            auto found_candidate = false;
            for (std::size_t candidate_offset = 0;
                 candidate_offset < candidate_indices.size();
                 candidate_offset += static_cast<std::size_t>(candidate_stride)) {
              if ((candidate_offset & 0x3FFU) == 0U && cancellation.cancelled()) {
                return;
              }
              const auto candidate_index = candidate_indices[candidate_offset];
              const auto candidate_x = static_cast<int>(candidate_index % static_cast<std::size_t>(width));
              const auto candidate_y = static_cast<int>(candidate_index / static_cast<std::size_t>(width));

              auto score = 0.0;
              auto support = 0.0;
              for (int offset_y = -patch_radius; offset_y <= patch_radius; ++offset_y) {
                for (int offset_x = -patch_radius; offset_x <= patch_radius; ++offset_x) {
                  if (offset_x == 0 && offset_y == 0) {
                    continue;
                  }
                  const auto target_x = x + offset_x;
                  const auto target_y = y + offset_y;
                  const auto sample_x = candidate_x + offset_x;
                  const auto sample_y = candidate_y + offset_y;
                  if (target_x < 0 || target_y < 0 || target_x >= width || target_y >= height ||
                      sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
                    continue;
                  }

                  const auto target_index =
                      static_cast<std::size_t>(target_y * width + target_x);
                  const auto sample_index =
                      static_cast<std::size_t>(sample_y * width + sample_x);
                  if (clamp_unit(working_mask[sample_index]) > 1e-4) {
                    continue;
                  }

                  const auto context_weight =
                      1.0 / std::max(1.0, static_cast<double>(offset_x * offset_x + offset_y * offset_y));
                  const auto target_context =
                      clamp_unit(working_mask[target_index]) <= 0.35 ? (*pixels)[target_index]
                                                                     : filled_pixels[target_index];
                  score += removal_color_distance(target_context, filled_pixels[sample_index]) *
                           context_weight;
                  support += context_weight;
                }
              }

              if (support <= 1e-6) {
                continue;
              }
              score /= support;
              if (score < best_score) {
                best_score = score;
                best_candidate = filled_pixels[candidate_index];
                found_candidate = true;
              }
            }

            if (!found_candidate) {
              continue;
            }

            const auto blend =
                std::clamp(0.45 + mask_weight * 0.45, 0.0, 1.0);
            refined_pixels[index].red = static_cast<float>(
                std::lerp(static_cast<double>(refined_pixels[index].red), static_cast<double>(best_candidate.red), blend));
            refined_pixels[index].green = static_cast<float>(
                std::lerp(static_cast<double>(refined_pixels[index].green), static_cast<double>(best_candidate.green), blend));
            refined_pixels[index].blue = static_cast<float>(
                std::lerp(static_cast<double>(refined_pixels[index].blue), static_cast<double>(best_candidate.blue), blend));
          }
        }
        filled_pixels = std::move(refined_pixels);
      }
    }
  }

  const auto use_separated_heal =
      std::abs(clamp_unit(texture_blend) - 1.0) > 1e-6 ||
      std::abs(clamp_unit(color_blend) - 1.0) > 1e-6;
  auto source_low = std::vector<ColorPixel>{};
  auto filled_low = std::vector<ColorPixel>{};
  if (use_separated_heal) {
    source_low = low_frequency_pixels(*pixels, width, height, 2, cancellation);
    filled_low = low_frequency_pixels(filled_pixels, width, height, 2, cancellation);
    if (cancellation.cancelled()) {
      return;
    }
    if (source_low.size() != pixel_count || filled_low.size() != pixel_count) {
      source_low.clear();
      filled_low.clear();
    }
  }

  for (std::size_t index = 0; index < pixel_count; ++index) {
    if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
      return;
    }
    const auto mask_weight = clamp_unit(removal_mask[index]);
    if (mask_weight <= 1e-4) {
      continue;
    }
    const auto target =
        !source_low.empty() && !filled_low.empty()
            ? separated_heal_pixel(
                  (*pixels)[index],
                  source_low[index],
                  filled_pixels[index],
                  filled_low[index],
                  texture_blend,
                  color_blend)
            : filled_pixels[index];
    (*pixels)[index].red = static_cast<float>(
        std::lerp(static_cast<double>((*pixels)[index].red), static_cast<double>(target.red), mask_weight));
    (*pixels)[index].green = static_cast<float>(
        std::lerp(static_cast<double>((*pixels)[index].green), static_cast<double>(target.green), mask_weight));
    (*pixels)[index].blue = static_cast<float>(
        std::lerp(static_cast<double>((*pixels)[index].blue), static_cast<double>(target.blue), mask_weight));
  }
}

void apply_local_clone(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  std::vector<LocalMask> strokes;
  std::vector<const LocalMask*> coverage_masks;
  for (const auto& mask : masks) {
    if (!mask.enabled || mask.retouch_mode!="clone") continue;
    if (mask.clone_strokes.empty()) { strokes.push_back(mask); coverage_masks.push_back(nullptr); continue; }
    auto base=mask;
    base.clone_strokes.clear(); base.brush_points.clear();
    base.segmentation_mask.clear(); base.segmentation_width=0; base.segmentation_height=0;
    for (const auto& stroke : mask.clone_strokes) {
      auto part=base; part.brush_points=stroke.points;
      part.clone_source_x=stroke.source_x; part.clone_source_y=stroke.source_y;
      part.clone_offset_x=stroke.offset_x; part.clone_offset_y=stroke.offset_y;
      part.clone_scale=stroke.scale; part.clone_rotation=stroke.rotation;
      strokes.push_back(std::move(part)); coverage_masks.push_back(&mask);
    }
  }
  for (std::size_t stroke_index=0;stroke_index<strokes.size();++stroke_index) {
    const auto& mask=strokes[stroke_index];
    if (!mask.enabled || mask.kind != "brush" || mask.operation != "add" ||
        mask.retouch_mode != "clone" || !mask.clone_source_set ||
        mask.brush_points.empty()) {
      continue;
    }

    const auto source_pixels = *pixels;
    const auto source_center_x = mask.clone_source_x * static_cast<double>(std::max(width - 1, 0));
    const auto source_center_y = mask.clone_source_y * static_cast<double>(std::max(height - 1, 0));
    const auto offset_x = mask.clone_offset_x * static_cast<double>(std::max(width - 1, 0));
    const auto offset_y = mask.clone_offset_y * static_cast<double>(std::max(height - 1, 0));
    const auto destination_origin_x = source_center_x - offset_x;
    const auto destination_origin_y = source_center_y - offset_y;
    const auto scale = std::clamp(mask.clone_scale, 0.25, 4.0);
    const auto radians = mask.clone_rotation * kMaskPi / 180.0;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    int left=width, top=height, right=0, bottom=0;
    for (const auto& point : mask.brush_points) {
      if (point.erase) continue;
      const double radius=std::max(0.01,point.size);
      left=std::min(left,std::clamp(int(std::floor((point.x-radius)*(width-1))),0,width));
      top=std::min(top,std::clamp(int(std::floor((point.y-radius)*(height-1))),0,height));
      right=std::max(right,std::clamp(int(std::ceil((point.x+radius)*(width-1)))+1,0,width));
      bottom=std::max(bottom,std::clamp(int(std::ceil((point.y+radius)*(height-1)))+1,0,height));
    }
    // Legacy refined mattes can extend beyond the original dabs.
    if (coverage_masks[stroke_index]==nullptr) {left=0;top=0;right=width;bottom=height;}
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y =
            height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        double weight=0;
        if (const auto* coverage=coverage_masks[stroke_index]) {
          // The final canonical matte supplies erasure, auto-mask and refinement.
          // Intersect it with this stroke's dab coverage to retain its own origin.
          for (const auto& point : mask.brush_points) {
            const auto distance=std::hypot(normalized_x-point.x,normalized_y-point.y)/std::max(0.01,point.size);
            const auto dab=clamp_unit(point.flow)*clamp_unit(point.opacity)*
                (1-smoothstep(std::clamp(mask.brush_hardness,0.0,0.98),1.0,distance));
            if(point.erase) weight*=1-dab;
            else weight=mask.brush_buildup?1-(1-weight)*(1-dab):std::max(weight,dab);
          }
          weight=std::min(weight,local_mask_weight(normalized_x,normalized_y,source_pixels[index],context,*coverage));
        } else weight=local_mask_weight(normalized_x,normalized_y,source_pixels[index],context,mask);
        if (weight <= 1e-4) {
          continue;
        }

        const auto dx = static_cast<double>(x) - destination_origin_x;
        const auto dy = static_cast<double>(y) - destination_origin_y;
        const auto source_dx = (dx * cosine + dy * sine) / scale;
        const auto source_dy = (-dx * sine + dy * cosine) / scale;
        if (source_center_x+source_dx<0 || source_center_y+source_dy<0 ||
            source_center_x+source_dx>width-1 || source_center_y+source_dy>height-1) continue;
        const auto candidate = sample_bilinear(
            source_pixels,
            width,
            height,
            source_center_x + source_dx,
            source_center_y + source_dy);
        const auto blend = clamp_unit(weight * clamp_unit(mask.brush_flow));
        (*pixels)[index].red = static_cast<float>(
            std::lerp(static_cast<double>((*pixels)[index].red), static_cast<double>(candidate.red), blend));
        (*pixels)[index].green = static_cast<float>(
            std::lerp(static_cast<double>((*pixels)[index].green), static_cast<double>(candidate.green), blend));
        (*pixels)[index].blue = static_cast<float>(
            std::lerp(static_cast<double>((*pixels)[index].blue), static_cast<double>(candidate.blue), blend));
      }
    }
  }
}

void apply_local_red_eye(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      auto& pixel = (*pixels)[index];
      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      const auto weight = composite_local_removal_weight(
          normalized_x, normalized_y, pixel, context, masks, "red-eye");
      if (weight <= 1e-4) {
        continue;
      }

      const auto green_blue_max = std::max(pixel.green, pixel.blue);
      const auto excess_red = std::max(0.0F, pixel.red - green_blue_max * 1.05F);
      if (excess_red <= 1e-5F) {
        continue;
      }
      const auto neutral_red = std::max(0.0F, green_blue_max * 0.92F);
      const auto blend = clamp_unit(weight);
      pixel.red = static_cast<float>(
          std::lerp(static_cast<double>(pixel.red), static_cast<double>(neutral_red), blend));
      const auto lift = static_cast<double>(excess_red) * 0.12 * blend;
      pixel.green = static_cast<float>(std::min(1.0, static_cast<double>(pixel.green) + lift));
      pixel.blue = static_cast<float>(std::min(1.0, static_cast<double>(pixel.blue) + lift));
    }
  }
}

void apply_local_skin_cleanup(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  const auto source_pixels = *pixels;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      const auto weight = composite_local_removal_weight(
          normalized_x, normalized_y, source_pixels[index], context, masks, "skin-cleanup");
      if (weight <= 1e-4) {
        continue;
      }

      auto red_sum = 0.0;
      auto green_sum = 0.0;
      auto blue_sum = 0.0;
      auto total_weight = 0.0;
      for (int dy = -2; dy <= 2; ++dy) {
        const auto sample_y = std::clamp(y + dy, 0, height - 1);
        for (int dx = -2; dx <= 2; ++dx) {
          const auto sample_x = std::clamp(x + dx, 0, width - 1);
          const auto distance = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
          const auto sample_weight = 1.0 / (1.0 + distance);
          const auto& sample = source_pixels[static_cast<std::size_t>(sample_y * width + sample_x)];
          red_sum += static_cast<double>(sample.red) * sample_weight;
          green_sum += static_cast<double>(sample.green) * sample_weight;
          blue_sum += static_cast<double>(sample.blue) * sample_weight;
          total_weight += sample_weight;
        }
      }
      if (total_weight <= 1e-6) {
        continue;
      }

      auto& pixel = (*pixels)[index];
      const auto blend = clamp_unit(weight * 0.65);
      pixel.red = static_cast<float>(
          std::lerp(static_cast<double>(pixel.red), red_sum / total_weight, blend));
      pixel.green = static_cast<float>(
          std::lerp(static_cast<double>(pixel.green), green_sum / total_weight, blend));
      pixel.blue = static_cast<float>(
          std::lerp(static_cast<double>(pixel.blue), blue_sum / total_weight, blend));
    }
  }
}

void apply_local_frequency_smoothing(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  const auto source_pixels = *pixels;
  const auto sample_average = [&](int center_x, int center_y, int radius) {
    auto red_sum = 0.0;
    auto green_sum = 0.0;
    auto blue_sum = 0.0;
    auto total_weight = 0.0;
    for (int dy = -radius; dy <= radius; ++dy) {
      const auto y = std::clamp(center_y + dy, 0, height - 1);
      for (int dx = -radius; dx <= radius; ++dx) {
        const auto x = std::clamp(center_x + dx, 0, width - 1);
        const auto distance = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
        if (distance > static_cast<double>(radius) + 0.25) {
          continue;
        }
        const auto weight = 1.0 / (1.0 + distance);
        const auto& sample = source_pixels[static_cast<std::size_t>(y * width + x)];
        red_sum += static_cast<double>(sample.red) * weight;
        green_sum += static_cast<double>(sample.green) * weight;
        blue_sum += static_cast<double>(sample.blue) * weight;
        total_weight += weight;
      }
    }
    if (total_weight <= 1e-6) {
      return source_pixels[static_cast<std::size_t>(center_y * width + center_x)];
    }
    return ColorPixel{
        .red = static_cast<float>(red_sum / total_weight),
        .green = static_cast<float>(green_sum / total_weight),
        .blue = static_cast<float>(blue_sum / total_weight),
        .alpha = source_pixels[static_cast<std::size_t>(center_y * width + center_x)].alpha,
    };
  };

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      const auto weight = composite_local_removal_weight(
          normalized_x, normalized_y, source_pixels[index], context, masks, "frequency-smoothing");
      if (weight <= 1e-4) {
        continue;
      }

      const auto low_frequency = sample_average(x, y, 4);
      const auto local_base = sample_average(x, y, 1);
      const auto& source = source_pixels[index];
      const auto target = ColorPixel{
          .red = static_cast<float>(std::clamp(
              static_cast<double>(low_frequency.red) +
                  (static_cast<double>(source.red) - static_cast<double>(local_base.red)) * 0.35,
              0.0,
              1.0)),
          .green = static_cast<float>(std::clamp(
              static_cast<double>(low_frequency.green) +
                  (static_cast<double>(source.green) - static_cast<double>(local_base.green)) * 0.35,
              0.0,
              1.0)),
          .blue = static_cast<float>(std::clamp(
              static_cast<double>(low_frequency.blue) +
                  (static_cast<double>(source.blue) - static_cast<double>(local_base.blue)) * 0.35,
              0.0,
              1.0)),
          .alpha = source.alpha,
      };
      const auto blend = clamp_unit(weight * 0.82);
      auto& pixel = (*pixels)[index];
      pixel.red = static_cast<float>(std::lerp(static_cast<double>(pixel.red), static_cast<double>(target.red), blend));
      pixel.green = static_cast<float>(std::lerp(static_cast<double>(pixel.green), static_cast<double>(target.green), blend));
      pixel.blue = static_cast<float>(std::lerp(static_cast<double>(pixel.blue), static_cast<double>(target.blue), blend));
    }
  }
}

[[nodiscard]] bool inpaint_patch_available(const LocalMask& mask) {
  return mask.enabled && mask.remove_strength > 1e-6 &&
         mask.inpaint_patch_width > 0 && mask.inpaint_patch_height > 0 &&
         mask.inpaint_patch_pixels.size() ==
             static_cast<std::size_t>(mask.inpaint_patch_width * mask.inpaint_patch_height);
}

[[nodiscard]] bool has_local_inpaint_patch(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (inpaint_patch_available(mask)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] ColorPixel matched_inpaint_patch_pixel(
    const ColorPixel& patch_pixel,
    const ColorPixel& color_delta,
    double color_match) {
  const auto strength = clamp_unit(color_match);
  return ColorPixel{
      .red = static_cast<float>(std::clamp(
          static_cast<double>(patch_pixel.red) + static_cast<double>(color_delta.red) * strength,
          0.0,
          1.0)),
      .green = static_cast<float>(std::clamp(
          static_cast<double>(patch_pixel.green) + static_cast<double>(color_delta.green) * strength,
          0.0,
          1.0)),
      .blue = static_cast<float>(std::clamp(
          static_cast<double>(patch_pixel.blue) + static_cast<double>(color_delta.blue) * strength,
          0.0,
          1.0)),
      .alpha = patch_pixel.alpha,
  };
}

struct InpaintPatchPlacement final {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
  int source_width = 0;
  int source_height = 0;
};

[[nodiscard]] InpaintPatchPlacement inpaint_patch_placement(
    const LocalMask& mask,
    int width,
    int height) {
  const auto source_width =
      mask.segmentation_width > 0 ? mask.segmentation_width : width;
  const auto source_height =
      mask.segmentation_height > 0 ? mask.segmentation_height : height;
  const auto scaled_x = [&](int source_x) {
    return static_cast<int>(std::floor(
        static_cast<double>(source_x) * width /
        static_cast<double>(std::max(1, source_width))));
  };
  const auto scaled_y = [&](int source_y) {
    return static_cast<int>(std::floor(
        static_cast<double>(source_y) * height /
        static_cast<double>(std::max(1, source_height))));
  };
  const auto scaled_right = static_cast<int>(std::ceil(
      static_cast<double>(mask.inpaint_patch_x + mask.inpaint_patch_width) *
      width / static_cast<double>(std::max(1, source_width))));
  const auto scaled_bottom = static_cast<int>(std::ceil(
      static_cast<double>(mask.inpaint_patch_y + mask.inpaint_patch_height) *
      height / static_cast<double>(std::max(1, source_height))));
  return InpaintPatchPlacement{
      .left = std::clamp(scaled_x(mask.inpaint_patch_x), 0, width),
      .top = std::clamp(scaled_y(mask.inpaint_patch_y), 0, height),
      .right = std::clamp(scaled_right, 0, width),
      .bottom = std::clamp(scaled_bottom, 0, height),
      .source_width = source_width,
      .source_height = source_height,
  };
}

[[nodiscard]] ColorPixel sample_inpaint_patch(
    const std::vector<ColorPixel>& patch_pixels,
    int patch_width,
    int patch_height,
    double patch_x,
    double patch_y) {
  if (patch_pixels.empty() || patch_width <= 0 || patch_height <= 0) {
    return {};
  }
  const auto clamped_x = std::clamp(patch_x, 0.0, static_cast<double>(patch_width - 1));
  const auto clamped_y = std::clamp(patch_y, 0.0, static_cast<double>(patch_height - 1));
  const auto x0 = static_cast<int>(std::floor(clamped_x));
  const auto y0 = static_cast<int>(std::floor(clamped_y));
  const auto x1 = std::min(x0 + 1, patch_width - 1);
  const auto y1 = std::min(y0 + 1, patch_height - 1);
  const auto tx = clamped_x - static_cast<double>(x0);
  const auto ty = clamped_y - static_cast<double>(y0);
  const auto& p00 = patch_pixels[static_cast<std::size_t>(y0 * patch_width + x0)];
  const auto& p10 = patch_pixels[static_cast<std::size_t>(y0 * patch_width + x1)];
  const auto& p01 = patch_pixels[static_cast<std::size_t>(y1 * patch_width + x0)];
  const auto& p11 = patch_pixels[static_cast<std::size_t>(y1 * patch_width + x1)];
  const auto interpolate = [&](float ColorPixel::*channel) {
    const auto top = std::lerp(
        static_cast<double>(p00.*channel),
        static_cast<double>(p10.*channel),
        tx);
    const auto bottom = std::lerp(
        static_cast<double>(p01.*channel),
        static_cast<double>(p11.*channel),
        tx);
    return static_cast<float>(std::lerp(top, bottom, ty));
  };
  return ColorPixel{
      .red = interpolate(&ColorPixel::red),
      .green = interpolate(&ColorPixel::green),
      .blue = interpolate(&ColorPixel::blue),
      .alpha = interpolate(&ColorPixel::alpha),
  };
}

[[nodiscard]] std::pair<double, double> inpaint_patch_sample_position(
    const LocalMask& mask,
    const InpaintPatchPlacement& placement,
    int target_x,
    int target_y,
    int width,
    int height) {
  const auto source_x =
      (static_cast<double>(target_x) + 0.5) * placement.source_width /
          static_cast<double>(std::max(1, width)) -
      0.5;
  const auto source_y =
      (static_cast<double>(target_y) + 0.5) * placement.source_height /
          static_cast<double>(std::max(1, height)) -
      0.5;
  return {
      source_x - static_cast<double>(mask.inpaint_patch_x),
      source_y - static_cast<double>(mask.inpaint_patch_y),
  };
}

[[nodiscard]] ColorPixel inpaint_patch_seam_color_delta(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  ColorPixel delta{};
  auto weight_sum = 0.0;
  const auto placement = inpaint_patch_placement(mask, width, height);
  const auto add_sample = [&](int target_x, int target_y, double sample_weight) {
    if (target_x < placement.left || target_y < placement.top ||
        target_x >= placement.right || target_y >= placement.bottom ||
        target_x < 0 || target_y < 0 || target_x >= width || target_y >= height) {
      return;
    }

    const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
    const auto& source = pixels[target_index];
    const auto [patch_x, patch_y] = inpaint_patch_sample_position(
        mask, placement, target_x, target_y, width, height);
    const auto patch = sample_inpaint_patch(
        mask.inpaint_patch_pixels,
        mask.inpaint_patch_width,
        mask.inpaint_patch_height,
        patch_x,
        patch_y);
    delta.red += static_cast<float>((static_cast<double>(source.red) - patch.red) * sample_weight);
    delta.green += static_cast<float>((static_cast<double>(source.green) - patch.green) * sample_weight);
    delta.blue += static_cast<float>((static_cast<double>(source.blue) - patch.blue) * sample_weight);
    weight_sum += sample_weight;
  };

  for (int target_y = placement.top; target_y < placement.bottom; ++target_y) {
    for (int target_x = placement.left; target_x < placement.right; ++target_x) {
      const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
      const auto normalized_x =
          width > 1 ? static_cast<double>(target_x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(target_y) / static_cast<double>(height - 1) : 0.5;
      const auto matte = local_mask_weight(normalized_x, normalized_y, pixels[target_index], context, mask);
      if (matte > 0.02 && matte < 0.98) {
        add_sample(target_x, target_y, 1.0 - std::abs(matte - 0.5) * 1.4);
      }
    }
  }

  if (weight_sum <= 1e-6) {
    for (int target_x = placement.left; target_x < placement.right; ++target_x) {
      add_sample(target_x, placement.top, 1.0);
      add_sample(target_x, placement.bottom - 1, 1.0);
    }
    for (int target_y = placement.top + 1; target_y + 1 < placement.bottom;
         ++target_y) {
      add_sample(placement.left, target_y, 1.0);
      add_sample(placement.right - 1, target_y, 1.0);
    }
  }

  if (weight_sum <= 1e-6) {
    return delta;
  }

  delta.red = static_cast<float>(std::clamp(static_cast<double>(delta.red) / weight_sum, -0.35, 0.35));
  delta.green = static_cast<float>(std::clamp(static_cast<double>(delta.green) / weight_sum, -0.35, 0.35));
  delta.blue = static_cast<float>(std::clamp(static_cast<double>(delta.blue) / weight_sum, -0.35, 0.35));
  delta.alpha = 1.0F;
  return delta;
}

// Compute the fixed source matte once, then its bounded Euclidean distance to
// the outside with two separable passes. The old per-pixel neighborhood search
// repeatedly evaluated the matte and observed pixels modified earlier in the loop.
[[nodiscard]] std::vector<double> inpaint_patch_seam_alphas(
    const std::vector<ColorPixel>& pixels, int width, int height,
    const PipelineColorContext& context, const LocalMask& mask,
    int left, int top, int right, int bottom) {
  const int pw=right-left,ph=bottom-top;
  if(pw<=0 || ph<=0) return {};
  std::vector<double> alpha(static_cast<std::size_t>(pw)*static_cast<std::size_t>(ph),1.0);
  const auto scale=std::min(double(width)/std::max(1,mask.segmentation_width>0?mask.segmentation_width:width),
      double(height)/std::max(1,mask.segmentation_height>0?mask.segmentation_height:height));
  const auto feather=std::clamp(mask.inpaint_seam_feather,0.0,24.0)*scale;
  if(feather<=1e-6) return alpha;
  const int radius=static_cast<int>(std::ceil(feather));
  const int x0=std::max(0,left-radius),y0=std::max(0,top-radius);
  const int w=std::min(width,right+radius)-x0,h=std::min(height,bottom+radius)-y0;
  const double limit=(feather+1)*(feather+1);
  std::vector<double> distance(static_cast<std::size_t>(w)*static_cast<std::size_t>(h),limit),vertical=distance;
  for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
    const int sx=x+x0,sy=y+y0;
    const auto weight=local_mask_weight(width>1?double(sx)/(width-1):0.5,
        height>1?double(sy)/(height-1):0.5,pixels[static_cast<std::size_t>(sy*width+sx)],context,mask);
    if(weight<=0.05) distance[static_cast<std::size_t>(y*w+x)]=0;
  }
  // Exact squared-distance transform: the lower envelope of parabolas makes
  // each pass linear in the number of pixels, independent of feather radius.
  const auto extent = std::max(w, h);
  std::vector<int> sites(static_cast<std::size_t>(extent));
  std::vector<double> boundaries(static_cast<std::size_t>(extent + 1));
  std::vector<double> input(static_cast<std::size_t>(extent));
  std::vector<double> output(static_cast<std::size_t>(extent));
  const auto transform_line = [&](int count) {
    int last = 0;
    sites[0] = 0;
    boundaries[0] = -std::numeric_limits<double>::infinity();
    boundaries[1] = std::numeric_limits<double>::infinity();
    for (int q = 1; q < count; ++q) {
      double intersection = 0;
      do {
        const int previous = sites[static_cast<std::size_t>(last)];
        intersection = ((input[static_cast<std::size_t>(q)] + double(q)*q) -
                        (input[static_cast<std::size_t>(previous)] + double(previous)*previous)) /
                       (2.0*(q - previous));
        if (intersection > boundaries[static_cast<std::size_t>(last)]) break;
        --last;
      } while (last >= 0);
      ++last;
      sites[static_cast<std::size_t>(last)] = q;
      boundaries[static_cast<std::size_t>(last)] = intersection;
      boundaries[static_cast<std::size_t>(last + 1)] = std::numeric_limits<double>::infinity();
    }
    last = 0;
    for (int q = 0; q < count; ++q) {
      while (boundaries[static_cast<std::size_t>(last + 1)] < q) ++last;
      const int nearest = sites[static_cast<std::size_t>(last)];
      output[static_cast<std::size_t>(q)] = double(q - nearest)*(q - nearest) +
                                         input[static_cast<std::size_t>(nearest)];
    }
  };
  for (int x = 0; x < w; ++x) {
    for (int y = 0; y < h; ++y) input[static_cast<std::size_t>(y)] = distance[static_cast<std::size_t>(y*w+x)];
    transform_line(h);
    for (int y = 0; y < h; ++y) vertical[static_cast<std::size_t>(y*w+x)] = output[static_cast<std::size_t>(y)];
  }
  for (int y = top; y < bottom; ++y) {
    for (int x = 0; x < w; ++x) input[static_cast<std::size_t>(x)] = vertical[static_cast<std::size_t>((y-y0)*w+x)];
    transform_line(w);
    for (int x = left; x < right; ++x) {
      alpha[static_cast<std::size_t>((y-top)*pw+x-left)] =
          smoothstep(0.0, feather, std::sqrt(output[static_cast<std::size_t>(x-x0)]));
    }
  }
  return alpha;
}

void apply_local_inpaint_patches(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  for (const auto& mask : masks) {
    if (!inpaint_patch_available(mask)) {
      continue;
    }

    const auto separated_heal =
        mask.retouch_mode == "heal" &&
        (std::abs(clamp_unit(mask.heal_texture_blend) - 1.0) > 1e-6 ||
         std::abs(clamp_unit(mask.heal_color_blend) - 1.0) > 1e-6);
    auto source_low = std::vector<ColorPixel>{};
    auto patch_low = std::vector<ColorPixel>{};
    if (separated_heal) {
      source_low = low_frequency_pixels(*pixels, width, height, 2);
      patch_low = low_frequency_pixels(
          mask.inpaint_patch_pixels,
          mask.inpaint_patch_width,
          mask.inpaint_patch_height,
          2);
      if (source_low.size() != pixels->size() ||
          patch_low.size() != mask.inpaint_patch_pixels.size()) {
        source_low.clear();
        patch_low.clear();
      }
    }
    const auto seam_color_delta =
        inpaint_patch_seam_color_delta(*pixels, width, height, context, mask);
    const auto placement = inpaint_patch_placement(mask, width, height);
    const auto seam_alpha=inpaint_patch_seam_alphas(*pixels,width,height,context,mask,
        placement.left,placement.top,placement.right,placement.bottom);

    for (int target_y = placement.top; target_y < placement.bottom; ++target_y) {
      for (int target_x = placement.left; target_x < placement.right; ++target_x) {
        const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(target_x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y =
            height > 1 ? static_cast<double>(target_y) / static_cast<double>(height - 1) : 0.5;
        const auto weight =
            local_mask_weight(normalized_x, normalized_y, (*pixels)[target_index], context, mask) *
            clamp_unit(mask.remove_strength / 100.0) *
            clamp_unit(mask.inpaint_blend_amount) *
            seam_alpha[static_cast<std::size_t>((target_y-placement.top)*(placement.right-placement.left)+target_x-placement.left)];
        if (weight <= 1e-4) {
          continue;
        }

        const auto [patch_x, patch_y] = inpaint_patch_sample_position(
            mask, placement, target_x, target_y, width, height);
        const auto patch_pixel = matched_inpaint_patch_pixel(
            sample_inpaint_patch(
                mask.inpaint_patch_pixels,
                mask.inpaint_patch_width,
                mask.inpaint_patch_height,
                patch_x,
                patch_y),
            seam_color_delta,
            mask.inpaint_color_match);
        auto& target_pixel = (*pixels)[target_index];
        const auto target =
            !source_low.empty() && !patch_low.empty()
                ? separated_heal_pixel(
                      target_pixel,
                      source_low[target_index],
                      patch_pixel,
                      sample_inpaint_patch(
                          patch_low,
                          mask.inpaint_patch_width,
                          mask.inpaint_patch_height,
                          patch_x,
                          patch_y),
                      mask.heal_texture_blend,
                      mask.heal_color_blend)
                : patch_pixel;
        target_pixel.red = static_cast<float>(
            std::lerp(static_cast<double>(target_pixel.red), static_cast<double>(target.red), weight));
        target_pixel.green = static_cast<float>(
            std::lerp(static_cast<double>(target_pixel.green), static_cast<double>(target.green), weight));
        target_pixel.blue = static_cast<float>(
            std::lerp(static_cast<double>(target_pixel.blue), static_cast<double>(target.blue), weight));
      }
    }
  }
}

[[nodiscard]] double composite_local_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks,
    const EdgeAwareMatteContext* source_context) {
  auto composite_weight = 0.0;
  auto initialized = false;

  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }

    const auto raw_weight =
        nested_local_mask_weight(normalized_x, normalized_y, pixel, context, mask, masks, source_context);
    accumulate_composite_weight(
        raw_weight, mask.operation, mask.blend_mode, &composite_weight, &initialized);
  }

  return initialized ? clamp_unit(composite_weight) : 0.0;
}

std::vector<std::uint8_t> rasterize_local_mask_set_alpha_impl(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels,
    std::span<const ColorPixel> edge_pixels,
    const ColorPipelineConfig& color_pipeline) {
  const auto safe_width = std::max(width, 0);
  const auto safe_height = std::max(height, 0);
  if (safe_width <= 0 || safe_height <= 0) {
    return {};
  }

  const auto pixel_count =
      static_cast<std::size_t>(safe_width) * static_cast<std::size_t>(safe_height);
  std::vector<ColorPixel> source_copy;
  const auto has_source_pixels = source_pixels.size() == pixel_count;
  const auto has_edge_pixels = edge_pixels.size() == pixel_count;
  if (has_edge_pixels) {
    source_copy.assign(edge_pixels.begin(), edge_pixels.end());
  }
  const EdgeAwareMatteContext source_context{
      .pixels = has_edge_pixels ? &source_copy : nullptr,
      .width = has_edge_pixels ? safe_width : 0,
      .height = has_edge_pixels ? safe_height : 0,
  };
  const auto* source_context_ptr = has_edge_pixels ? &source_context : nullptr;
  ColorAdjustmentState context_state;
  context_state.color_pipeline = color_pipeline;
  const auto context = make_pipeline_color_context(context_state);
  const auto neutral_pixel = ColorPixel{0.5F, 0.5F, 0.5F, 1.0F};

  std::vector<std::uint8_t> alpha(pixel_count, 0);
  run_color_adjustment_parallel_for(safe_height, [&](int y) {
    const auto row_offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(safe_width);
    for (int x = 0; x < safe_width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      const auto& pixel = has_source_pixels ? source_pixels[index] : neutral_pixel;
      const auto normalized_x =
          safe_width > 1 ? static_cast<double>(x) / static_cast<double>(safe_width - 1) : 0.5;
      const auto normalized_y =
          safe_height > 1 ? static_cast<double>(y) / static_cast<double>(safe_height - 1) : 0.5;
      alpha[index] = static_cast<std::uint8_t>(
          std::llround(clamp_unit(local_mask_set_weight(
                         normalized_x,
                         normalized_y,
                         pixel,
                         context,
                         mask_set,
                         source_context_ptr)) *
                     255.0));
    }
  });
  return alpha;
}

std::vector<std::uint8_t> rasterize_local_mask_set_alpha(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels) {
  return rasterize_local_mask_set_alpha_impl(
      mask_set,
      width,
      height,
      source_pixels,
      source_pixels,
      default_color_pipeline_config());
}

std::vector<std::uint8_t> rasterize_local_mask_set_alpha(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels,
    const ColorPipelineConfig& color_pipeline) {
  return rasterize_local_mask_set_alpha_impl(
      mask_set,
      width,
      height,
      source_pixels,
      source_pixels,
      color_pipeline);
}

std::vector<std::uint8_t> rasterize_local_mask_set_alpha_with_context(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> range_pixels,
    std::span<const ColorPixel> edge_pixels,
    const ColorPipelineConfig& color_pipeline) {
  if (width <= 0 || height <= 0 ||
      range_pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
      edge_pixels.size() != range_pixels.size()) return {};
  return rasterize_local_mask_set_alpha_impl(
      mask_set, width, height, range_pixels, edge_pixels, color_pipeline);
}


}  // namespace photo_editor::pipeline

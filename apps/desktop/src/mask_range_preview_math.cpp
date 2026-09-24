#include "mask_range_preview_math.h"

#include "edit_recipe_mask_schema.h"
#include "photo_editor/pipeline/white_balance.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace photo_editor::desktop {
namespace {

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

double clamp_signed_percent(double value) {
  return std::clamp(value, -100.0, 100.0);
}

double smoothstep(double edge0, double edge1, double value) {
  if (edge1 <= edge0) {
    return value >= edge1 ? 1.0 : 0.0;
  }
  const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

}  // namespace

double wrap_unit(double value) {
  value -= std::floor(value);
  if (value < 0.0) {
    value += 1.0;
  }
  return value;
}

double circular_weight(double value, double center, double radius) {
  if (radius <= 0.0) {
    return 0.0;
  }

  auto distance = std::abs(value - center);
  distance = std::min(distance, 1.0 - distance);
  if (distance >= radius) {
    return 0.0;
  }

  const auto t = 1.0 - distance / radius;
  return t * t * (3.0 - 2.0 * t);
}

double circular_feathered_weight(double value, double center, double radius, double softness) {
  if (radius <= 0.0) {
    return 0.0;
  }

  auto distance = std::abs(wrap_unit(value) - wrap_unit(center));
  distance = std::min(distance, 1.0 - distance);
  if (distance >= radius) {
    return 0.0;
  }

  const auto clamped_softness = clamp_unit(softness);
  const auto inner_radius = radius * (1.0 - clamped_softness);
  if (distance <= inner_radius) {
    return 1.0;
  }
  return 1.0 - smoothstep(inner_radius, radius, distance);
}

double preview_luminance_for_pixel(const photo_editor::pipeline::ColorPixel& pixel) {
  return (pixel.red * 0.2126) + (pixel.green * 0.7152) + (pixel.blue * 0.0722);
}

void hue_saturation_value_for_preview_pixel(
    const photo_editor::pipeline::ColorPixel& pixel,
    double* hue,
    double* saturation,
    double* value) {
  const auto max_channel = std::max({pixel.red, pixel.green, pixel.blue});
  const auto min_channel = std::min({pixel.red, pixel.green, pixel.blue});
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
  if (max_channel == pixel.red) {
    result = std::fmod((pixel.green - pixel.blue) / delta, 6.0);
  } else if (max_channel == pixel.green) {
    result = ((pixel.blue - pixel.red) / delta) + 2.0;
  } else {
    result = ((pixel.red - pixel.green) / delta) + 4.0;
  }

  result /= 6.0;
  if (result < 0.0) {
    result += 1.0;
  }
  *hue = wrap_unit(result);
}

double skin_candidate_weight(double hue, double saturation, double value) {
  const auto warm_hue =
      std::max(circular_feathered_weight(hue, 0.055, 0.13, 0.92),
               circular_feathered_weight(hue, 0.105, 0.12, 0.92));
  const auto saturation_gate =
      smoothstep(0.08, 0.18, saturation) * (1.0 - smoothstep(0.72, 0.92, saturation));
  const auto value_gate =
      smoothstep(0.045, 0.16, value) * (1.0 - smoothstep(0.88, 1.08, value));
  return clamp_unit(warm_hue * saturation_gate * value_gate);
}

double circular_distance_unit(double left, double right) {
  auto distance = std::abs(wrap_unit(left) - wrap_unit(right));
  return std::min(distance, 1.0 - distance);
}

void update_color_range_constraints_from_samples(QVariantMap* mask) {
  if (mask == nullptr) {
    return;
  }

  const auto samples = normalized_color_range_samples(mask->value(QStringLiteral("colorSamples")).toList());
  mask->insert(QStringLiteral("colorSamples"), samples);
  if (samples.isEmpty()) {
    return;
  }

  auto sin_sum = 0.0;
  auto cos_sum = 0.0;
  auto weight_sum = 0.0;
  auto saturation_min = 1.0;
  auto saturation_max = 0.0;
  for (const auto& sample_value : samples) {
    const auto sample = sample_value.toMap();
    const auto hue = wrap_unit(sample.value(QStringLiteral("hue")).toDouble());
    const auto saturation = clamp_unit(sample.value(QStringLiteral("saturation")).toDouble());
    const auto weight = std::max(0.08, saturation);
    const auto radians = hue * 2.0 * M_PI;
    sin_sum += std::sin(radians) * weight;
    cos_sum += std::cos(radians) * weight;
    weight_sum += weight;
    saturation_min = std::min(saturation_min, saturation);
    saturation_max = std::max(saturation_max, saturation);
  }

  if (weight_sum <= 1e-9) {
    return;
  }

  auto center = std::atan2(sin_sum, cos_sum) / (2.0 * M_PI);
  center = wrap_unit(center);
  auto hue_range = 0.04;
  for (const auto& sample_value : samples) {
    const auto sample = sample_value.toMap();
    hue_range = std::max(
        hue_range,
        circular_distance_unit(sample.value(QStringLiteral("hue")).toDouble(), center) + 0.04);
  }

  mask->insert(QStringLiteral("hueCenter"), center);
  mask->insert(QStringLiteral("hueRange"), qBound(0.02, hue_range, 0.5));
  mask->insert(QStringLiteral("saturationMin"), qBound(0.0, saturation_min - 0.08, 1.0));
  mask->insert(QStringLiteral("saturationMax"), qBound(0.0, saturation_max + 0.08, 1.0));
}

void update_luminance_range_constraints_from_samples(QVariantMap* mask) {
  if (mask == nullptr) {
    return;
  }

  const auto samples =
      normalized_luminance_range_samples(mask->value(QStringLiteral("luminanceSamples")).toList());
  mask->insert(QStringLiteral("luminanceSamples"), samples);
  if (samples.isEmpty()) {
    return;
  }

  auto min_value = 1.0;
  auto max_value = 0.0;
  for (const auto& sample_value : samples) {
    const auto sample = sample_value.toMap();
    min_value = std::min(min_value, sample.value(QStringLiteral("luminanceMin")).toDouble());
    max_value = std::max(max_value, sample.value(QStringLiteral("luminanceMax")).toDouble());
  }

  mask->insert(QStringLiteral("luminanceMin"), qBound(0.0, min_value - 0.03, 1.0));
  mask->insert(QStringLiteral("luminanceMax"), qBound(0.0, max_value + 0.03, 1.0));
}

bool mask_has_valid_depth_payload(const QVariantMap& mask) {
  return valid_segmentation_payload(
      mask.value(QStringLiteral("depthData")).toByteArray(),
      mask.value(QStringLiteral("depthWidth")).toInt(),
      mask.value(QStringLiteral("depthHeight")).toInt());
}

void update_depth_range_constraints_from_samples(QVariantMap* mask) {
  if (mask == nullptr) {
    return;
  }

  const auto samples =
      normalized_depth_range_samples(mask->value(QStringLiteral("depthSamples")).toList());
  mask->insert(QStringLiteral("depthSamples"), samples);
  if (samples.isEmpty()) {
    return;
  }

  auto min_value = 1.0;
  auto max_value = 0.0;
  for (const auto& sample_value : samples) {
    const auto sample = sample_value.toMap();
    min_value = std::min(min_value, sample.value(QStringLiteral("depthMin")).toDouble());
    max_value = std::max(max_value, sample.value(QStringLiteral("depthMax")).toDouble());
  }

  mask->insert(QStringLiteral("depthMin"), qBound(0.0, min_value - 0.03, 1.0));
  mask->insert(QStringLiteral("depthMax"), qBound(0.0, max_value + 0.03, 1.0));
}

QVariantMap luminance_sample_preview_map(double start_x, double start_y, double end_x, double end_y) {
  const auto clamped_start_x = qBound(0.0, start_x, 1.0);
  const auto clamped_start_y = qBound(0.0, start_y, 1.0);
  const auto clamped_end_x = qBound(0.0, end_x, 1.0);
  const auto clamped_end_y = qBound(0.0, end_y, 1.0);
  const auto min_x = std::min(clamped_start_x, clamped_end_x);
  const auto min_y = std::min(clamped_start_y, clamped_end_y);
  const auto max_x = std::max(clamped_start_x, clamped_end_x);
  const auto max_y = std::max(clamped_start_y, clamped_end_y);
  QVariantMap preview;
  preview.insert(QStringLiteral("active"), true);
  preview.insert(QStringLiteral("startX"), clamped_start_x);
  preview.insert(QStringLiteral("startY"), clamped_start_y);
  preview.insert(QStringLiteral("endX"), clamped_end_x);
  preview.insert(QStringLiteral("endY"), clamped_end_y);
  preview.insert(QStringLiteral("x"), min_x);
  preview.insert(QStringLiteral("y"), min_y);
  preview.insert(QStringLiteral("width"), max_x - min_x);
  preview.insert(QStringLiteral("height"), max_y - min_y);
  return preview;
}

std::array<double, 3> preview_white_balance_multipliers(double temperature, double tint) {
  return photo_editor::pipeline::preview_white_balance_multipliers(temperature, tint);
}

double apply_selection_expand_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto expand = clamp_signed_percent(mask.selection_expand) / 100.0;
  if (std::abs(expand) <= 1e-6) {
    return clamp_unit(base_weight);
  }

  const auto threshold = std::clamp(0.5 - expand * 0.35, 0.08, 0.92);
  const auto ramp = std::clamp(0.06 + std::abs(expand) * 0.18, 0.04, 0.28);
  return smoothstep(threshold - ramp, threshold + ramp, clamp_unit(base_weight));
}

double apply_selection_edge_contrast_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto contrast =
      clamp_unit(mask.selection_edge_contrast / 100.0);
  if (contrast <= 1e-6) {
    return clamp_unit(base_weight);
  }

  const auto ramp = std::lerp(0.24, 0.035, contrast);
  return smoothstep(0.5 - ramp, 0.5 + ramp, clamp_unit(base_weight));
}

double apply_selection_smooth_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto smoothing = clamp_unit(mask.selection_smooth / 100.0);
  const auto weight = clamp_unit(base_weight);
  if (smoothing <= 1e-6 || weight <= 1e-6 || weight >= 1.0 - 1e-6) {
    return weight;
  }

  return std::lerp(weight, 0.5, smoothing * 0.35);
}

double apply_selection_fine_edge_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
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

double apply_selection_refine_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask,
    bool spatial_expand_smooth_applied) {
  auto refined = clamp_unit(base_weight);
  if (!spatial_expand_smooth_applied) {
    refined = apply_selection_expand_to_preview_weight(refined, mask);
    refined = apply_selection_smooth_to_preview_weight(refined, mask);
  }
  refined = apply_selection_fine_edge_to_preview_weight(refined, mask);
  refined = apply_selection_edge_contrast_to_preview_weight(refined, mask);
  if (mask.invert || mask.selection_invert) {
    refined = 1.0 - refined;
  }
  refined = std::min(refined, std::clamp(mask.density, 0.0, 1.0));
  return clamp_unit(refined * std::clamp(mask.opacity, 0.0, 1.0));
}

double depth_refine_value_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask) {
  auto value = clamp_unit(preview_luminance_for_pixel(pixel));
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

double unit_range_refine_weight(double value, double min_value, double max_value, double feather) {
  auto lower = std::clamp(min_value, 0.0, 1.0);
  auto upper = std::clamp(max_value, 0.0, 1.0);
  if (lower > upper) {
    std::swap(lower, upper);
  }
  const auto edge = std::clamp(feather, 0.0, 0.5);
  const auto low_weight = lower <= 1.0e-9
                              ? 1.0
                              : smoothstep(std::max(0.0, lower - edge), lower, clamp_unit(value));
  const auto high_weight = upper >= 1.0 - 1.0e-9
                               ? 1.0
                               : 1.0 - smoothstep(upper, std::min(1.0, upper + edge), clamp_unit(value));
  return clamp_unit(low_weight * high_weight);
}

double apply_range_refinement_to_preview_weight(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask) {
  auto weight = clamp_unit(base_weight);
  if (weight <= 1.0e-9) {
    return 0.0;
  }

  const auto feather = std::clamp(mask.refine_range_feather, 0.0, 0.5);
  weight *= unit_range_refine_weight(
      preview_luminance_for_pixel(pixel),
      mask.refine_luminance_min,
      mask.refine_luminance_max,
      feather);

  double hue = 0.0;
  double saturation = 0.0;
  hue_saturation_value_for_preview_pixel(pixel, &hue, &saturation, nullptr);
  weight *= circular_weight(hue, wrap_unit(mask.refine_hue_center), std::max(0.02, mask.refine_hue_range));
  weight *= unit_range_refine_weight(
      saturation,
      mask.refine_saturation_min,
      mask.refine_saturation_max,
      feather);
  weight *= unit_range_refine_weight(
      depth_refine_value_for_preview(normalized_x, normalized_y, pixel, mask),
      mask.refine_depth_min,
      mask.refine_depth_max,
      feather);

  const auto edge_min = std::clamp(mask.refine_edge_confidence_min, 0.0, 1.0);
  if (edge_min > 1.0e-9) {
    const auto edge_confidence = clamp_unit(std::abs(clamp_unit(base_weight) - 0.5) * 2.0);
    weight *= smoothstep(edge_min, 1.0, edge_confidence);
  }
  return clamp_unit(weight);
}

double apply_full_refine_to_preview_weight(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask,
    bool spatial_expand_smooth_applied) {
  return apply_range_refinement_to_preview_weight(
      apply_selection_refine_to_preview_weight(base_weight, mask, spatial_expand_smooth_applied),
      normalized_x,
      normalized_y,
      pixel,
      mask);
}

}  // namespace photo_editor::desktop

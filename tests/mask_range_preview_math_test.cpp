#include "mask_range_preview_math.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool close(double actual, double expected, double tolerance = 1e-9) {
  return std::abs(actual - expected) <= tolerance;
}

QVariantMap sample(std::initializer_list<std::pair<QString, QVariant>> fields) {
  QVariantMap value;
  for (const auto& [key, field] : fields) {
    value.insert(key, field);
  }
  return value;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;
  using photo_editor::pipeline::ColorPixel;
  using photo_editor::pipeline::LocalMask;

  if (!close(wrap_unit(-0.1), 0.9) || !close(wrap_unit(1.2), 0.2) ||
      !close(circular_distance_unit(0.98, 0.02), 0.04) ||
      !close(circular_weight(0.0, 0.0, 0.2), 1.0) ||
      !close(circular_weight(0.2, 0.0, 0.2), 0.0) ||
      !close(circular_feathered_weight(0.0, 0.0, 0.2, 0.5), 1.0)) {
    return fail("circular mask-range math must wrap and feather exact unit-domain distances");
  }

  const ColorPixel red{.red = 1.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};
  double hue = -1.0;
  double saturation = -1.0;
  double value = -1.0;
  hue_saturation_value_for_preview_pixel(red, &hue, &saturation, &value);
  if (!close(hue, 0.0) || !close(saturation, 1.0) || !close(value, 1.0) ||
      !close(preview_luminance_for_pixel(red), 0.2126, 1e-6) ||
      skin_candidate_weight(0.08, 0.4, 0.5) <=
          skin_candidate_weight(0.55, 0.4, 0.5)) {
    return fail("preview color analysis must preserve HSV/luminance and prefer plausible warm skin");
  }

  QVariantMap color_mask;
  color_mask.insert(
      QStringLiteral("colorSamples"),
      QVariantList{
          sample({{QStringLiteral("hue"), 0.98}, {QStringLiteral("saturation"), 0.4}}),
          sample({{QStringLiteral("hue"), 0.02}, {QStringLiteral("saturation"), 0.6}})});
  update_color_range_constraints_from_samples(&color_mask);
  const auto hue_center = color_mask.value(QStringLiteral("hueCenter")).toDouble();
  if (std::min(hue_center, 1.0 - hue_center) > 0.03 ||
      color_mask.value(QStringLiteral("hueRange")).toDouble() < 0.05 ||
      !close(color_mask.value(QStringLiteral("saturationMin")).toDouble(), 0.32) ||
      !close(color_mask.value(QStringLiteral("saturationMax")).toDouble(), 0.68)) {
    return fail("color samples must form a circular hue span and padded saturation bounds");
  }

  QVariantMap luminance_mask;
  luminance_mask.insert(
      QStringLiteral("luminanceSamples"),
      QVariantList{
          sample({{QStringLiteral("luminanceMin"), 0.2},
                  {QStringLiteral("luminanceMax"), 0.4}}),
          sample({{QStringLiteral("luminanceMin"), 0.6},
                  {QStringLiteral("luminanceMax"), 0.8}})});
  update_luminance_range_constraints_from_samples(&luminance_mask);
  if (!close(luminance_mask.value(QStringLiteral("luminanceMin")).toDouble(), 0.17) ||
      !close(luminance_mask.value(QStringLiteral("luminanceMax")).toDouble(), 0.83)) {
    return fail("luminance sample aggregation must publish exact padded extrema");
  }

  QVariantMap depth_mask;
  depth_mask.insert(
      QStringLiteral("depthSamples"),
      QVariantList{sample({{QStringLiteral("depthMin"), 0.25},
                           {QStringLiteral("depthMax"), 0.75}})});
  depth_mask.insert(QStringLiteral("depthData"), QByteArray(4, char(1)));
  depth_mask.insert(QStringLiteral("depthWidth"), 2);
  depth_mask.insert(QStringLiteral("depthHeight"), 2);
  update_depth_range_constraints_from_samples(&depth_mask);
  if (!mask_has_valid_depth_payload(depth_mask) ||
      !close(depth_mask.value(QStringLiteral("depthMin")).toDouble(), 0.22) ||
      !close(depth_mask.value(QStringLiteral("depthMax")).toDouble(), 0.78)) {
    return fail("depth samples and payload admission must share exact validated bounds");
  }

  const auto preview = luminance_sample_preview_map(0.8, -1.0, 0.2, 2.0);
  if (!preview.value(QStringLiteral("active")).toBool() ||
      !close(preview.value(QStringLiteral("x")).toDouble(), 0.2) ||
      !close(preview.value(QStringLiteral("y")).toDouble(), 0.0) ||
      !close(preview.value(QStringLiteral("width")).toDouble(), 0.6) ||
      !close(preview.value(QStringLiteral("height")).toDouble(), 1.0)) {
    return fail("luminance drag preview must clamp endpoints and normalize reversed rectangles");
  }

  const auto neutral = preview_white_balance_multipliers(0.0, 0.0);
  const auto warm = preview_white_balance_multipliers(100.0, 0.0);
  if (!close(neutral[0], 1.0, 1e-6) || !close(neutral[1], 1.0, 1e-6) ||
      !close(neutral[2], 1.0, 1e-6) || warm[0] <= warm[2]) {
    return fail("preview white balance must be neutral at zero and warm in the positive direction");
  }

  LocalMask selection;
  if (!close(apply_selection_refine_to_preview_weight(0.4, selection), 0.4)) {
    return fail("identity selection refinement must preserve the input weight");
  }
  selection.selection_invert = true;
  if (!close(apply_selection_refine_to_preview_weight(0.2, selection), 0.8)) {
    return fail("selection inversion must occur before density and opacity");
  }
  selection.selection_invert = false;
  selection.density = 0.6;
  selection.opacity = 0.5;
  if (!close(apply_selection_refine_to_preview_weight(0.8, selection), 0.3)) {
    return fail("selection refinement must apply density cap before opacity");
  }

  LocalMask depth;
  depth.depth_width = 2;
  depth.depth_height = 2;
  depth.depth_map = {0, 255, 255, 0};
  const ColorPixel gray{.red = 0.5F, .green = 0.5F, .blue = 0.5F, .alpha = 1.0F};
  if (!close(depth_refine_value_for_preview(0.5, 0.5, gray, depth), 0.5, 1e-6) ||
      !close(unit_range_refine_weight(0.5, 0.2, 0.8, 0.1), 1.0) ||
      !close(unit_range_refine_weight(0.0, 0.2, 0.8, 0.1), 0.0)) {
    return fail("depth interpolation and unit-range feathering must be exact and bounded");
  }

  LocalMask range;
  if (!close(apply_full_refine_to_preview_weight(0.7, 0.5, 0.5, gray, range), 0.7)) {
    return fail("identity full refinement must preserve a valid base matte");
  }
  range.refine_luminance_min = 0.7;
  range.refine_luminance_max = 1.0;
  if (apply_range_refinement_to_preview_weight(0.7, 0.5, 0.5, gray, range) > 1e-9) {
    return fail("range refinement must reject pixels outside the admitted luminance band");
  }

  return EXIT_SUCCESS;
}

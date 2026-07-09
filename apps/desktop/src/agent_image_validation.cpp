#include "agent_image_validation.h"

#include <QColor>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "edit_recipe_state.h"

namespace photo_editor::desktop {
namespace {

double validation_threshold(
    const QVariantMap& style,
    const QString& key,
    double fallback) {
  const auto validation = style.value(QStringLiteral("validation")).toMap();
  auto ok = false;
  const auto value = validation.value(key, fallback).toDouble(&ok);
  return ok ? value : fallback;
}

bool validation_flag(
    const QVariantMap& style,
    const QString& key,
    bool fallback = false) {
  const auto validation = style.value(QStringLiteral("validation")).toMap();
  const auto value = validation.value(key);
  return value.isValid() ? value.toBool() : fallback;
}

double clamped_score(double value) {
  return std::clamp(value, 0.0, 1.0);
}

bool warnings_contain(const QStringList& warnings, const QString& needle) {
  for (const auto& warning : warnings) {
    if (warning.contains(needle, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

template <std::size_t Size>
QVariantList percent_bins(const std::array<int, Size>& bins, double total) {
  QVariantList values;
  values.reserve(static_cast<int>(Size));
  for (const auto count : bins) {
    values.append(total <= 0.0 ? 0.0 : (static_cast<double>(count) * 100.0 / total));
  }
  return values;
}

template <std::size_t Size>
int peak_bin_index(const std::array<int, Size>& bins) {
  auto peak_index = 0;
  for (auto index = 1; index < static_cast<int>(Size); ++index) {
    if (bins[static_cast<std::size_t>(index)] > bins[static_cast<std::size_t>(peak_index)]) {
      peak_index = index;
    }
  }
  return peak_index;
}

QVariantMap quality_score_report_for_validation(const QVariantMap& report) {
  const auto errors = report.value(QStringLiteral("errors")).toStringList();
  const auto warnings = report.value(QStringLiteral("warnings")).toStringList();
  const auto hard_failure = !errors.isEmpty();
  const auto highlight_clip = report.value(QStringLiteral("highlightClipPercent")).toDouble();
  const auto shadow_clip = report.value(QStringLiteral("shadowClipPercent")).toDouble();
  const auto highlight_limit = report.value(QStringLiteral("highlightClipLimitPercent"), 1.0).toDouble();
  const auto shadow_limit = report.value(QStringLiteral("shadowClipLimitPercent"), 2.0).toDouble();
  const auto saturation_extreme = report.value(QStringLiteral("saturationExtremePercent")).toDouble();
  const auto channel_imbalance = report.value(QStringLiteral("channelImbalance")).toDouble();
  const auto red_blue_cast = report.value(QStringLiteral("redBlueCast")).toDouble();
  const auto green_cast = report.value(QStringLiteral("greenCast")).toDouble();

  const auto tone_penalty =
      std::max(0.0, highlight_clip - highlight_limit) / 12.0 +
      std::max(0.0, shadow_clip - shadow_limit) / 12.0 +
      (warnings_contain(warnings, QStringLiteral("compressed histogram")) ? 0.28 : 0.0);
  const auto color_penalty =
      std::max(0.0, saturation_extreme - 8.0) / 22.0 +
      std::max(0.0, channel_imbalance - 0.42) / 0.45 +
      std::max(0.0, red_blue_cast - 0.55) / 0.60 +
      std::max(0.0, green_cast - 0.45) / 0.50;
  const auto skin_required = report.value(QStringLiteral("skinToneCheckRequired")).toBool();
  const auto skin_plausible =
      !skin_required || report.value(QStringLiteral("skinTonePlausible"), true).toBool();
  const auto recipe_report = report.value(QStringLiteral("recipeValidation")).toMap();
  const auto crop_valid = recipe_report.isEmpty()
                              ? report.value(QStringLiteral("cropValid"), true).toBool()
                              : recipe_report.value(QStringLiteral("cropValid"), true).toBool();
  const auto missing_retouch =
      report.value(QStringLiteral("missingRetouchProvenanceCount"),
                   recipe_report.value(QStringLiteral("missingRetouchProvenanceCount")))
          .toInt();
  auto structure_penalty = 0.0;
  if (!crop_valid) {
    structure_penalty += 0.35;
  }
  if (missing_retouch > 0) {
    structure_penalty += std::min(0.65, 0.35 * static_cast<double>(missing_retouch));
  }
  if (warnings_contain(warnings, QStringLiteral("mask coverage risk"))) {
    structure_penalty += 0.18;
  }
  if (warnings_contain(warnings, QStringLiteral("mask leakage risk"))) {
    structure_penalty += 0.28;
  }
  if (warnings_contain(warnings, QStringLiteral("crop/rotation risk"))) {
    structure_penalty += 0.18;
  }

  const auto technical_penalty =
      hard_failure ? 1.0 : std::min(0.65, static_cast<double>(warnings.size()) * 0.055);
  const auto technical_safety_score = hard_failure ? 0.0 : clamped_score(1.0 - technical_penalty);
  const auto tone_score = hard_failure ? 0.0 : clamped_score(1.0 - tone_penalty);
  const auto color_score = hard_failure ? 0.0 : clamped_score(1.0 - color_penalty);
  const auto skin_score = hard_failure ? 0.0 : (skin_plausible ? 1.0 : 0.35);
  const auto structure_score = hard_failure ? 0.0 : clamped_score(1.0 - structure_penalty);
  const auto overall_score =
      clamped_score((technical_safety_score * 0.20) +
                    (tone_score * 0.24) +
                    (color_score * 0.24) +
                    (skin_score * 0.12) +
                    (structure_score * 0.20));

  const auto needs_review =
      hard_failure || overall_score < 0.78 || !warnings.isEmpty() ||
      (skin_required && !skin_plausible) || missing_retouch > 0;

  return QVariantMap{
      {QStringLiteral("version"), QStringLiteral("agent-edit-quality-score-v1")},
      {QStringLiteral("overallScore"), overall_score},
      {QStringLiteral("overallScorePercent"),
       static_cast<int>(std::round(overall_score * 100.0))},
      {QStringLiteral("technicalSafetyScore"), technical_safety_score},
      {QStringLiteral("toneScore"), tone_score},
      {QStringLiteral("colorScore"), color_score},
      {QStringLiteral("skinScore"), skin_score},
      {QStringLiteral("structureScore"), structure_score},
      {QStringLiteral("warningCount"), warnings.size()},
      {QStringLiteral("hardValidationFailure"), hard_failure},
      {QStringLiteral("needsUserReview"), needs_review},
      {QStringLiteral("method"), QStringLiteral("deterministic-render-recipe-guardrails")},
  };
}

void attach_quality_score(QVariantMap* report) {
  if (report == nullptr) {
    return;
  }
  const auto score_report = quality_score_report_for_validation(*report);
  report->insert(QStringLiteral("qualityScore"), score_report.value(QStringLiteral("overallScore")));
  report->insert(
      QStringLiteral("qualityScorePercent"),
      score_report.value(QStringLiteral("overallScorePercent")));
  report->insert(QStringLiteral("qualityScoreReport"), score_report);
  report->insert(
      QStringLiteral("needsUserReview"),
      score_report.value(QStringLiteral("needsUserReview")));
}

bool skin_tone_candidate(const QColor& color) {
  const auto hue = color.hsvHueF();
  const auto saturation = color.hsvSaturationF();
  const auto value = color.valueF();
  if (hue < 0.0 || saturation < 0.10 || saturation > 0.96 || value < 0.16 || value > 0.98) {
    return false;
  }
  const auto hue_degrees = hue * 360.0;
  const auto warm_hue = hue_degrees <= 58.0 || hue_degrees >= 345.0;
  if (!warm_hue) {
    return false;
  }

  const auto red = color.redF();
  const auto green = color.greenF();
  const auto blue = color.blueF();
  return red >= blue && green >= blue * 0.72 && (red - blue) >= 0.04;
}

double normalized_skin_hue(double hue_degrees) {
  return hue_degrees > 300.0 ? hue_degrees - 360.0 : hue_degrees;
}

double polygon_area_percent(const QVariantList& points) {
  if (points.size() < 3) {
    return 0.0;
  }
  auto area = 0.0;
  for (int index = 0; index < points.size(); ++index) {
    const auto current = points.at(index).toMap();
    const auto next = points.at((index + 1) % points.size()).toMap();
    area += current.value(QStringLiteral("x")).toDouble() *
            next.value(QStringLiteral("y")).toDouble();
    area -= next.value(QStringLiteral("x")).toDouble() *
            current.value(QStringLiteral("y")).toDouble();
  }
  return std::clamp(std::abs(area) * 50.0, 0.0, 100.0);
}

double estimated_mask_coverage_percent(const QVariantMap& mask) {
  const auto kind = mask.value(QStringLiteral("kind")).toString().trimmed().toLower();
  if (kind == QStringLiteral("linear")) {
    return 50.0;
  }
  if (kind == QStringLiteral("radial") || kind == QStringLiteral("ellipse")) {
    const auto radius_x = std::clamp(mask.value(QStringLiteral("radiusX"), 0.25).toDouble(), 0.0, 1.0);
    const auto radius_y = std::clamp(mask.value(QStringLiteral("radiusY"), 0.25).toDouble(), 0.0, 1.0);
    return std::clamp(std::numbers::pi * radius_x * radius_y * 100.0, 0.0, 100.0);
  }
  if (kind == QStringLiteral("rectangle")) {
    const auto radius_x = std::clamp(mask.value(QStringLiteral("radiusX"), 0.30).toDouble(), 0.0, 1.0);
    const auto radius_y = std::clamp(mask.value(QStringLiteral("radiusY"), 0.20).toDouble(), 0.0, 1.0);
    return std::clamp(4.0 * radius_x * radius_y * 100.0, 0.0, 100.0);
  }
  if (kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
    return polygon_area_percent(mask.value(QStringLiteral("polygonPoints")).toList());
  }
  if (kind == QStringLiteral("brush")) {
    auto coverage = 0.0;
    for (const auto& point_value : mask.value(QStringLiteral("brushPoints")).toList()) {
      const auto point = point_value.toMap();
      const auto size = std::clamp(point.value(QStringLiteral("size"),
                                               mask.value(QStringLiteral("brushSize"), 0.08))
                                       .toDouble(),
                                   0.0,
                                   1.0);
      coverage += std::numbers::pi * size * size * 25.0;
    }
    return std::clamp(coverage, 0.0, 100.0);
  }
  if (kind == QStringLiteral("luminance") || kind == QStringLiteral("color") ||
      kind == QStringLiteral("depth")) {
    return std::clamp(
        mask.value(QStringLiteral("rangeFeather"), 0.05).toDouble() * 100.0,
        1.0,
        100.0);
  }
  return -1.0;
}

QVariantMap validate_agent_edit_recipe_state(const EditRecipeState& recipe, const QVariantMap& style) {
  QVariantMap report;
  QStringList warnings;
  auto crop_valid = true;
  auto mask_known_count = 0;
  auto mask_unknown_count = 0;
  auto retouch_count = 0;
  auto missing_retouch_provenance_count = 0;
  auto largest_mask_coverage = 0.0;
  auto smallest_mask_coverage = 100.0;

  const auto has_crop = recipe.crop_rect_width > 1e-6 || recipe.crop_rect_height > 1e-6 ||
                        recipe.crop_rect_x > 1e-6 || recipe.crop_rect_y > 1e-6;
  if (has_crop) {
    crop_valid = recipe.crop_rect_x >= 0.0 && recipe.crop_rect_y >= 0.0 &&
                 recipe.crop_rect_width >= 0.05 && recipe.crop_rect_height >= 0.05 &&
                 recipe.crop_rect_x + recipe.crop_rect_width <= 1.0 + 1e-6 &&
                 recipe.crop_rect_y + recipe.crop_rect_height <= 1.0 + 1e-6;
    if (!crop_valid) {
      warnings.append(QStringLiteral("crop rectangle is outside normalized image bounds or too small"));
    }
  }

  const auto max_rotation =
      validation_threshold(style, QStringLiteral("max_rotation_degrees"), 3.0);
  if (std::abs(recipe.transform_rotation) > max_rotation) {
    warnings.append(
        QStringLiteral("crop/rotation risk: transform rotation %1 degrees exceeds %2")
            .arg(recipe.transform_rotation, 0, 'f', 2)
            .arg(max_rotation, 0, 'f', 2));
  }
  if (recipe.upright_mode == QStringLiteral("Guided") &&
      std::abs(recipe.upright_vertical_guide_angle) < 1e-6 &&
      std::abs(recipe.upright_horizontal_guide_angle) < 1e-6) {
    warnings.append(QStringLiteral("guided upright mode has no guide angles"));
  }

  const auto normalized_masks = normalize_mask_stack(recipe.mask_stack);
  for (const auto& mask_value : normalized_masks) {
    const auto mask = mask_value.toMap();
    const auto role = mask.value(QStringLiteral("toolRole")).toString().trimmed().toLower();
    const auto mode = mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
    if (role == QStringLiteral("retouch") || !mode.isEmpty()) {
      ++retouch_count;
      const auto has_brush_points = !mask.value(QStringLiteral("brushPoints")).toList().isEmpty();
      const auto has_clone_source = mask.value(QStringLiteral("cloneSourceSet")).toBool();
      const auto has_patch_source = mask.value(QStringLiteral("patchSourceSet")).toBool();
      const auto has_inpaint_patch =
          !mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() &&
          mask.value(QStringLiteral("inpaintPatchWidth")).toInt() > 0 &&
          mask.value(QStringLiteral("inpaintPatchHeight")).toInt() > 0;
      const auto has_model_name =
          !mask.value(QStringLiteral("inpaintModelName")).toString().trimmed().isEmpty();
      auto provenance_ok = false;
      if (mode == QStringLiteral("clone")) {
        provenance_ok = has_brush_points && has_clone_source;
      } else if (mode == QStringLiteral("patch")) {
        provenance_ok = has_brush_points && has_patch_source;
      } else if (mode == QStringLiteral("heal")) {
        provenance_ok = has_brush_points;
      } else if (mode == QStringLiteral("remove")) {
        provenance_ok = has_inpaint_patch && has_model_name;
      }
      if (!provenance_ok) {
        ++missing_retouch_provenance_count;
        warnings.append(
            QStringLiteral("retouch provenance risk: %1 retouch lacks required source or inpaint artifact metadata")
                .arg(mode.isEmpty() ? QStringLiteral("unknown") : mode));
      }
      continue;
    }

    const auto coverage = estimated_mask_coverage_percent(mask);
    if (coverage < 0.0) {
      ++mask_unknown_count;
      continue;
    }
    ++mask_known_count;
    largest_mask_coverage = std::max(largest_mask_coverage, coverage);
    smallest_mask_coverage = std::min(smallest_mask_coverage, coverage);
    if (coverage < 0.1) {
      warnings.append(
          QStringLiteral("mask coverage risk: %1 mask covers less than 0.1%")
              .arg(mask.value(QStringLiteral("kind")).toString()));
    }
    if (coverage > 95.0 && mask.value(QStringLiteral("operation")).toString() != QStringLiteral("replace")) {
      warnings.append(
          QStringLiteral("mask leakage risk: %1 mask covers %2%")
              .arg(mask.value(QStringLiteral("kind")).toString())
              .arg(coverage, 0, 'f', 2));
    }
  }
  if (mask_known_count == 0) {
    smallest_mask_coverage = 0.0;
  }

  report.insert(QStringLiteral("cropValid"), crop_valid);
  report.insert(QStringLiteral("cropActive"), has_crop);
  report.insert(QStringLiteral("maskKnownCoverageCount"), mask_known_count);
  report.insert(QStringLiteral("maskUnknownCoverageCount"), mask_unknown_count);
  report.insert(QStringLiteral("largestMaskCoveragePercent"), largest_mask_coverage);
  report.insert(QStringLiteral("smallestMaskCoveragePercent"), smallest_mask_coverage);
  report.insert(QStringLiteral("retouchCount"), retouch_count);
  report.insert(QStringLiteral("missingRetouchProvenanceCount"), missing_retouch_provenance_count);
  report.insert(QStringLiteral("warnings"), warnings);
  return report;
}

}  // namespace

QVariantMap validate_agent_edit_image(const QImage& image, const QVariantMap& style) {
  QVariantMap report;
  QStringList warnings;
  QStringList errors;
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    errors.append(QStringLiteral("image is empty"));
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("errors"), errors);
    report.insert(QStringLiteral("warnings"), warnings);
    return report;
  }

  const auto sample = image.convertToFormat(QImage::Format_ARGB32);
  const auto total_pixels = static_cast<double>(sample.width()) * static_cast<double>(sample.height());
  auto highlight_clip_count = 0.0;
  auto shadow_clip_count = 0.0;
  auto saturation_extreme_count = 0.0;
  auto red_sum = 0.0;
  auto green_sum = 0.0;
  auto blue_sum = 0.0;
  auto luma_sum = 0.0;
  auto luma_square_sum = 0.0;
  auto luma_min = 255.0;
  auto luma_max = 0.0;
  auto saturation_sum = 0.0;
  auto chroma_x_sum = 0.0;
  auto chroma_y_sum = 0.0;
  auto chroma_radius_sum = 0.0;
  auto chroma_radius_square_sum = 0.0;
  auto shadow_luma_count = 0.0;
  auto midtone_luma_count = 0.0;
  auto highlight_luma_count = 0.0;
  std::array<int, 16> luma_histogram{};
  std::array<int, 12> hue_histogram{};
  auto skin_candidate_count = 0.0;
  auto skin_hue_sum = 0.0;
  auto skin_saturation_sum = 0.0;
  auto skin_luma_sum = 0.0;
  auto skin_red_blue_delta_sum = 0.0;

  for (int y = 0; y < sample.height(); ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(sample.constScanLine(y));
    for (int x = 0; x < sample.width(); ++x) {
      const QColor color(line[x]);
      const auto red = color.red();
      const auto green = color.green();
      const auto blue = color.blue();
      red_sum += red;
      green_sum += green;
      blue_sum += blue;
      const auto luma = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
      luma_sum += luma;
      luma_square_sum += luma * luma;
      luma_min = std::min(luma_min, luma);
      luma_max = std::max(luma_max, luma);
      const auto luma_bin = std::clamp(
          static_cast<int>(std::floor(luma / 16.0)),
          0,
          static_cast<int>(luma_histogram.size()) - 1);
      ++luma_histogram[static_cast<std::size_t>(luma_bin)];
      if (luma < 32.0) {
        shadow_luma_count += 1.0;
      } else if (luma > 224.0) {
        highlight_luma_count += 1.0;
      } else {
        midtone_luma_count += 1.0;
      }
      if (red >= 250 || green >= 250 || blue >= 250) {
        highlight_clip_count += 1.0;
      }
      if (red <= 5 && green <= 5 && blue <= 5) {
        shadow_clip_count += 1.0;
      }
      const auto maximum = std::max({red, green, blue});
      const auto minimum = std::min({red, green, blue});
      const auto saturation = color.hsvSaturationF();
      saturation_sum += saturation;
      const auto chroma_x = (static_cast<double>(red) - luma) / 255.0;
      const auto chroma_y = (static_cast<double>(blue) - luma) / 255.0;
      const auto chroma_radius = std::hypot(chroma_x, chroma_y);
      chroma_x_sum += chroma_x;
      chroma_y_sum += chroma_y;
      chroma_radius_sum += chroma_radius;
      chroma_radius_square_sum += chroma_radius * chroma_radius;
      const auto hue = color.hsvHueF();
      if (hue >= 0.0 && saturation >= 0.08 && maximum > 16) {
        const auto hue_bin = std::clamp(
            static_cast<int>(std::floor(hue * static_cast<double>(hue_histogram.size()))),
            0,
            static_cast<int>(hue_histogram.size()) - 1);
        ++hue_histogram[static_cast<std::size_t>(hue_bin)];
      }
      if (maximum > 32 && (static_cast<double>(maximum - minimum) / static_cast<double>(maximum)) >= 0.95) {
        saturation_extreme_count += 1.0;
      }
      if (skin_tone_candidate(color)) {
        skin_candidate_count += 1.0;
        skin_hue_sum += normalized_skin_hue(color.hsvHueF() * 360.0);
        skin_saturation_sum += color.hsvSaturationF();
        skin_luma_sum += luma;
        skin_red_blue_delta_sum += luma <= 0.0 ? 0.0 : static_cast<double>(red - blue) / luma;
      }
    }
  }

  const auto highlight_clip_percent = highlight_clip_count * 100.0 / total_pixels;
  const auto shadow_clip_percent = shadow_clip_count * 100.0 / total_pixels;
  const auto saturation_extreme_percent = saturation_extreme_count * 100.0 / total_pixels;
  const auto average_red = red_sum / total_pixels;
  const auto average_green = green_sum / total_pixels;
  const auto average_blue = blue_sum / total_pixels;
  const auto average_luma = luma_sum / total_pixels;
  const auto luma_variance =
      std::max(0.0, (luma_square_sum / total_pixels) - (average_luma * average_luma));
  const auto luma_stddev = std::sqrt(luma_variance);
  const auto luma_range = luma_max - luma_min;
  const auto channel_min = std::min({average_red, average_green, average_blue});
  const auto channel_max = std::max({average_red, average_green, average_blue});
  const auto channel_imbalance =
      channel_max <= 0.0 ? 0.0 : (channel_max - channel_min) / channel_max;
  const auto red_blue_delta =
      average_luma <= 0.0 ? 0.0 : std::abs(average_red - average_blue) / average_luma;
  const auto green_delta =
      average_luma <= 0.0 ? 0.0
                          : std::abs(average_green - ((average_red + average_blue) / 2.0)) /
                                average_luma;
  const auto skin_candidate_percent = skin_candidate_count * 100.0 / total_pixels;
  const auto average_skin_hue =
      skin_candidate_count <= 0.0 ? 0.0 : skin_hue_sum / skin_candidate_count;
  const auto average_skin_saturation =
      skin_candidate_count <= 0.0 ? 0.0 : skin_saturation_sum / skin_candidate_count;
  const auto average_skin_luma =
      skin_candidate_count <= 0.0 ? 0.0 : skin_luma_sum / skin_candidate_count;
  const auto average_skin_red_blue_delta =
      skin_candidate_count <= 0.0 ? 0.0 : skin_red_blue_delta_sum / skin_candidate_count;
  const auto average_saturation = saturation_sum / total_pixels;
  const auto average_chroma_x = chroma_x_sum / total_pixels;
  const auto average_chroma_y = chroma_y_sum / total_pixels;
  const auto average_chroma_radius = chroma_radius_sum / total_pixels;
  const auto chroma_radius_variance =
      std::max(0.0, (chroma_radius_square_sum / total_pixels) -
                        (average_chroma_radius * average_chroma_radius));
  const auto chroma_radius_stddev = std::sqrt(chroma_radius_variance);
  const auto shadow_luma_percent = shadow_luma_count * 100.0 / total_pixels;
  const auto midtone_luma_percent = midtone_luma_count * 100.0 / total_pixels;
  const auto highlight_luma_percent = highlight_luma_count * 100.0 / total_pixels;
  const auto luma_peak_bin = peak_bin_index(luma_histogram);
  const auto hue_peak_bin = peak_bin_index(hue_histogram);

  const auto highlight_limit =
      validation_threshold(style, QStringLiteral("highlight_clip_limit_percent"), 1.0);
  const auto shadow_limit =
      validation_threshold(style, QStringLiteral("shadow_clip_limit_percent"), 2.0);
  if (highlight_clip_percent > highlight_limit) {
    warnings.append(
        QStringLiteral("highlight clipping %1% exceeds %2%")
            .arg(highlight_clip_percent, 0, 'f', 2)
            .arg(highlight_limit, 0, 'f', 2));
  }
  if (shadow_clip_percent > shadow_limit) {
    warnings.append(
        QStringLiteral("shadow clipping %1% exceeds %2%")
            .arg(shadow_clip_percent, 0, 'f', 2)
            .arg(shadow_limit, 0, 'f', 2));
  }
  if (saturation_extreme_percent > 8.0) {
    warnings.append(
        QStringLiteral("extreme saturation covers %1% of pixels")
            .arg(saturation_extreme_percent, 0, 'f', 2));
  }
  if (channel_imbalance > 0.42 && average_luma > 20.0) {
    warnings.append(
        QStringLiteral("large average channel imbalance %1")
            .arg(channel_imbalance, 0, 'f', 2));
  }
  if (average_luma > 16.0 && luma_range < 18.0 && luma_stddev < 5.0) {
    warnings.append(
        QStringLiteral("compressed histogram shape: luma range %1 and stddev %2")
            .arg(luma_range, 0, 'f', 2)
            .arg(luma_stddev, 0, 'f', 2));
  }
  if (average_luma > 20.0 && (red_blue_delta > 0.55 || green_delta > 0.45)) {
    warnings.append(
        QStringLiteral("white balance plausibility risk: channel cast red-blue %1 green %2")
            .arg(red_blue_delta, 0, 'f', 2)
            .arg(green_delta, 0, 'f', 2));
  }
  auto skin_tone_plausible = true;
  const auto require_skin_tone_check =
      validation_flag(style, QStringLiteral("require_skin_tone_check"));
  if (require_skin_tone_check && skin_candidate_percent >= 0.5) {
    skin_tone_plausible = average_skin_saturation <= 0.72 &&
                          average_skin_hue >= 2.0 &&
                          average_skin_hue <= 48.0 &&
                          average_skin_red_blue_delta <= 1.15 &&
                          average_skin_luma >= 28.0 &&
                          average_skin_luma <= 235.0;
    if (!skin_tone_plausible) {
      warnings.append(
          QStringLiteral("skin tone plausibility risk: hue %1 saturation %2 red-blue %3")
              .arg(average_skin_hue, 0, 'f', 2)
              .arg(average_skin_saturation, 0, 'f', 2)
              .arg(average_skin_red_blue_delta, 0, 'f', 2));
    }
  }

  report.insert(QStringLiteral("ok"), errors.isEmpty());
  report.insert(QStringLiteral("errors"), errors);
  report.insert(QStringLiteral("warnings"), warnings);
  report.insert(QStringLiteral("width"), sample.width());
  report.insert(QStringLiteral("height"), sample.height());
  report.insert(QStringLiteral("highlightClipPercent"), highlight_clip_percent);
  report.insert(QStringLiteral("highlightClipLimitPercent"), highlight_limit);
  report.insert(QStringLiteral("shadowClipPercent"), shadow_clip_percent);
  report.insert(QStringLiteral("shadowClipLimitPercent"), shadow_limit);
  report.insert(QStringLiteral("saturationExtremePercent"), saturation_extreme_percent);
  report.insert(QStringLiteral("averageRed"), average_red);
  report.insert(QStringLiteral("averageGreen"), average_green);
  report.insert(QStringLiteral("averageBlue"), average_blue);
  report.insert(QStringLiteral("averageLuma"), average_luma);
  report.insert(QStringLiteral("lumaRange"), luma_range);
  report.insert(QStringLiteral("lumaStddev"), luma_stddev);
  report.insert(QStringLiteral("channelImbalance"), channel_imbalance);
  report.insert(QStringLiteral("redBlueCast"), red_blue_delta);
  report.insert(QStringLiteral("greenCast"), green_delta);
  report.insert(QStringLiteral("skinToneCheckRequired"), require_skin_tone_check);
  report.insert(QStringLiteral("skinToneCandidatePercent"), skin_candidate_percent);
  report.insert(QStringLiteral("averageSkinHue"), average_skin_hue);
  report.insert(QStringLiteral("averageSkinSaturation"), average_skin_saturation);
  report.insert(QStringLiteral("averageSkinLuma"), average_skin_luma);
  report.insert(QStringLiteral("averageSkinRedBlueDelta"), average_skin_red_blue_delta);
  report.insert(QStringLiteral("skinTonePlausible"), skin_tone_plausible);
  report.insert(
      QStringLiteral("imageStats"),
      QVariantMap{
          {QStringLiteral("version"), QStringLiteral("agent-image-stats-v1")},
          {QStringLiteral("sampleWidth"), sample.width()},
          {QStringLiteral("sampleHeight"), sample.height()},
          {QStringLiteral("sampleCount"), static_cast<int>(total_pixels)},
          {QStringLiteral("histogram"),
           QVariantMap{
               {QStringLiteral("kind"), QStringLiteral("luma-16-bin-percent")},
               {QStringLiteral("binCount"), static_cast<int>(luma_histogram.size())},
               {QStringLiteral("binsPercent"), percent_bins(luma_histogram, total_pixels)},
               {QStringLiteral("peakBin"), luma_peak_bin},
               {QStringLiteral("peakBinApproxLuma"),
                (static_cast<double>(luma_peak_bin) + 0.5) *
                    (255.0 / static_cast<double>(luma_histogram.size()))},
               {QStringLiteral("shadowPercent"), shadow_luma_percent},
               {QStringLiteral("midtonePercent"), midtone_luma_percent},
               {QStringLiteral("highlightPercent"), highlight_luma_percent},
               {QStringLiteral("averageLuma"), average_luma},
               {QStringLiteral("lumaMin"), luma_min},
               {QStringLiteral("lumaMax"), luma_max},
               {QStringLiteral("lumaRange"), luma_range},
               {QStringLiteral("lumaStddev"), luma_stddev},
               {QStringLiteral("compressed"),
                average_luma > 16.0 && luma_range < 18.0 && luma_stddev < 5.0},
           }},
          {QStringLiteral("rgb"),
           QVariantMap{
               {QStringLiteral("averageRed"), average_red},
               {QStringLiteral("averageGreen"), average_green},
               {QStringLiteral("averageBlue"), average_blue},
               {QStringLiteral("channelImbalance"), channel_imbalance},
               {QStringLiteral("redBlueCast"), red_blue_delta},
               {QStringLiteral("greenCast"), green_delta},
           }},
          {QStringLiteral("vectorscope"),
           QVariantMap{
               {QStringLiteral("kind"), QStringLiteral("hue-12-bin-plus-chroma-summary")},
               {QStringLiteral("hueBinCount"), static_cast<int>(hue_histogram.size())},
               {QStringLiteral("hueBinsPercent"), percent_bins(hue_histogram, total_pixels)},
               {QStringLiteral("dominantHueBin"), hue_peak_bin},
               {QStringLiteral("dominantHueDegreesApprox"),
                (static_cast<double>(hue_peak_bin) + 0.5) *
                    (360.0 / static_cast<double>(hue_histogram.size()))},
               {QStringLiteral("averageSaturation"), average_saturation},
               {QStringLiteral("saturationExtremePercent"), saturation_extreme_percent},
               {QStringLiteral("chromaCenterX"), average_chroma_x},
               {QStringLiteral("chromaCenterY"), average_chroma_y},
               {QStringLiteral("chromaAverageRadius"), average_chroma_radius},
               {QStringLiteral("chromaRadiusStddev"), chroma_radius_stddev},
           }},
          {QStringLiteral("skin"),
           QVariantMap{
               {QStringLiteral("checkRequired"), require_skin_tone_check},
               {QStringLiteral("candidatePercent"), skin_candidate_percent},
               {QStringLiteral("averageHue"), average_skin_hue},
               {QStringLiteral("averageSaturation"), average_skin_saturation},
               {QStringLiteral("averageLuma"), average_skin_luma},
               {QStringLiteral("averageRedBlueDelta"), average_skin_red_blue_delta},
               {QStringLiteral("plausible"), skin_tone_plausible},
           }},
      });
  attach_quality_score(&report);
  return report;
}

QVariantMap validate_agent_edit_image(
    const QImage& image,
    const EditRecipeState& recipe,
    const QVariantMap& style) {
  auto report = validate_agent_edit_image(image, style);
  auto warnings = report.value(QStringLiteral("warnings")).toStringList();
  const auto recipe_report = validate_agent_edit_recipe_state(recipe, style);
  warnings.append(recipe_report.value(QStringLiteral("warnings")).toStringList());
  report.insert(QStringLiteral("warnings"), warnings);
  report.insert(QStringLiteral("recipeValidation"), recipe_report);
  report.insert(QStringLiteral("cropValid"), recipe_report.value(QStringLiteral("cropValid")));
  report.insert(QStringLiteral("maskKnownCoverageCount"),
                recipe_report.value(QStringLiteral("maskKnownCoverageCount")));
  report.insert(QStringLiteral("largestMaskCoveragePercent"),
                recipe_report.value(QStringLiteral("largestMaskCoveragePercent")));
  report.insert(QStringLiteral("retouchCount"), recipe_report.value(QStringLiteral("retouchCount")));
  report.insert(
      QStringLiteral("missingRetouchProvenanceCount"),
      recipe_report.value(QStringLiteral("missingRetouchProvenanceCount")));
  attach_quality_score(&report);
  return report;
}

}  // namespace photo_editor::desktop

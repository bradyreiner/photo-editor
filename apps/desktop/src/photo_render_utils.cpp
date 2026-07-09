#include "photo_render_utils.h"

#include "app_bootstrap.h"
#include "decoded_image_cache.h"
#include "mask_alpha_provider.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "raw_image_decoder.h"
#include "working_image.h"

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QJsonDocument>
#include <QPainter>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>

namespace photo_editor::desktop {

namespace {

QImage normalize_preview_color_space(QImage image);
QColorSpace preview_target_color_space() {
  return preview_color_space();
}

bool gui_application_available() {
  return qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr;
}

QColorSpace output_color_space_for_name(const QString& name) {
  return preview_color_space_for_name(normalized_output_color_space_name(name));
}

bool has_extended_range_pixels(const std::vector<photo_editor::pipeline::ColorPixel>& pixels) {
  constexpr auto kTolerance = 1.0e-6F;
  return std::any_of(pixels.cbegin(), pixels.cend(), [](const auto& pixel) {
    return pixel.red > 1.0F + kTolerance || pixel.green > 1.0F + kTolerance ||
           pixel.blue > 1.0F + kTolerance || pixel.red < -kTolerance ||
           pixel.green < -kTolerance || pixel.blue < -kTolerance;
  });
}

bool render_state_requires_cpu_pipeline(const RenderEditState& state) {
  return std::abs(state.texture) > 1e-6 || std::abs(state.clarity) > 1e-6 ||
         std::abs(state.dehaze) > 1e-6 || std::abs(state.hdr_local_tone_mapping) > 1e-6 ||
         std::abs(state.sharpening) > 1e-6 ||
        std::abs(state.sharpen_masking) > 1e-6 || std::abs(state.luminance_nr) > 1e-6 ||
        std::abs(state.color_nr) > 1e-6 || std::abs(state.lens_distortion) > 1e-6 ||
         std::abs(state.lens_vignette) > 1e-6 || std::abs(state.chromatic_aberration) > 1e-6 ||
         std::abs(state.perspective_vertical) > 1e-6 || std::abs(state.perspective_horizontal) > 1e-6 ||
         !state.layers.isEmpty() ||
         std::abs(state.transform_rotation) > 1e-6 || std::abs(state.transform_scale - 1.0) > 1e-6 ||
         std::abs(state.transform_skew_x) > 1e-6 || std::abs(state.transform_skew_y) > 1e-6 ||
         std::abs(state.liquify_strength) > 1e-6;
}

bool render_state_requires_exact_global_tone(const RenderEditState& state) {
  return std::abs(state.exposure) > 1e-6 || std::abs(state.contrast) > 1e-6 ||
         std::abs(state.highlights) > 1e-6 || std::abs(state.shadows) > 1e-6 ||
         std::abs(state.whites) > 1e-6 || std::abs(state.blacks) > 1e-6 ||
         std::abs(state.brightness) > 1e-6 || std::abs(state.filmic_shoulder) > 1e-6 ||
         std::abs(state.filmic_toe) > 1e-6 || std::abs(state.adaptive_contrast) > 1e-6 ||
         std::abs(state.levels_black_point) > 1e-6 || std::abs(state.levels_gamma) > 1e-6 ||
         std::abs(state.levels_white_point) > 1e-6 ||
         std::abs(state.zone_deep_shadows) > 1e-6 || std::abs(state.zone_shadows) > 1e-6 ||
         std::abs(state.zone_midtones) > 1e-6 || std::abs(state.zone_lights) > 1e-6 ||
         std::abs(state.zone_highlights) > 1e-6 || std::abs(state.curve_shadows) > 1e-6 ||
         std::abs(state.curve_darks) > 1e-6 || std::abs(state.curve_lights) > 1e-6 ||
         std::abs(state.curve_highlights) > 1e-6 || !state.master_curve_points.isEmpty() ||
         !state.luma_curve_points.isEmpty();
}

bool render_state_requires_exact_post_color(const RenderEditState& state) {
  const auto has_mixer_channel_adjustments = std::any_of(
      state.mixer_channels.cbegin(),
      state.mixer_channels.cend(),
      [](const auto& channel) {
        return std::abs(channel.hue) > 1e-6 || std::abs(channel.saturation) > 1e-6 ||
               std::abs(channel.luminance) > 1e-6 ||
               std::abs(channel.selective_cyan) > 1e-6 ||
               std::abs(channel.selective_magenta) > 1e-6 ||
               std::abs(channel.selective_yellow) > 1e-6 ||
               std::abs(channel.selective_black) > 1e-6;
      });
  const auto& rgb_mixer = state.rgb_channel_mixer;
  const auto has_rgb_mixer_adjustments =
      std::abs(rgb_mixer.red_from_red - 100.0) > 1e-6 ||
      std::abs(rgb_mixer.red_from_green) > 1e-6 ||
      std::abs(rgb_mixer.red_from_blue) > 1e-6 ||
      std::abs(rgb_mixer.green_from_red) > 1e-6 ||
      std::abs(rgb_mixer.green_from_green - 100.0) > 1e-6 ||
      std::abs(rgb_mixer.green_from_blue) > 1e-6 ||
      std::abs(rgb_mixer.blue_from_red) > 1e-6 ||
      std::abs(rgb_mixer.blue_from_green) > 1e-6 ||
      std::abs(rgb_mixer.blue_from_blue - 100.0) > 1e-6;
  const auto has_black_and_white_mixer_adjustments = std::any_of(
      state.black_and_white_mixer.cbegin(),
      state.black_and_white_mixer.cend(),
      [](double value) { return std::abs(value) > 1e-6; });
  return std::abs(state.saturation) > 1e-6 || std::abs(state.vibrance) > 1e-6 ||
         std::abs(state.hue_adjustment) > 1e-6 ||
         std::abs(state.saturation_adjustment) > 1e-6 ||
         std::abs(state.luminance_adjustment) > 1e-6 ||
         has_mixer_channel_adjustments || has_rgb_mixer_adjustments ||
         state.black_and_white_enabled || has_black_and_white_mixer_adjustments ||
         (state.color_replacement_enabled &&
          std::abs(state.color_replacement_strength) > 1e-6) ||
         (state.skin_tone_protection_enabled &&
          std::abs(state.skin_tone_protection_strength) > 1e-6) ||
         (state.color_harmony_enabled && std::abs(state.color_harmony_strength) > 1e-6) ||
         std::abs(state.grading_shadows_saturation) > 1e-6 ||
          std::abs(state.grading_midtones_saturation) > 1e-6 ||
          std::abs(state.grading_highlights_saturation) > 1e-6 ||
          std::abs(state.grading_shadows_luminance) > 1e-6 ||
          std::abs(state.grading_midtones_luminance) > 1e-6 ||
          std::abs(state.grading_highlights_luminance) > 1e-6 ||
          std::abs(state.grading_amount - 100.0) > 1e-6 ||
          std::abs(state.grading_balance) > 1e-6;
}

QString gpu_fallback_reason(
    const RenderEditState& state,
    const photo_editor::pipeline::ColorAdjustmentState& pipeline_state,
    const std::vector<photo_editor::pipeline::ColorPixel>& pixels,
    bool metal_available) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  if (!metal_available) {
    return QStringLiteral("metal_unavailable");
  }
  if (!pipeline_state.local_mask_sets.empty()) {
    return QStringLiteral("local_mask_sets");
  }
  if (!pipeline_state.retouch_masks.empty()) {
    return QStringLiteral("retouch_masks");
  }
  if (!pipeline_state.local_masks.empty() ||
      state.linear_mask_enabled ||
      state.radial_mask_enabled ||
      std::abs(state.linear_mask_exposure) > 1e-6 ||
      std::abs(state.linear_mask_temperature) > 1e-6 ||
      std::abs(state.radial_mask_exposure) > 1e-6 ||
      std::abs(state.radial_mask_temperature) > 1e-6) {
    return QStringLiteral("legacy_local_masks");
  }
  if (has_extended_range_pixels(pixels)) {
    return QStringLiteral("extended_range_pixels");
  }
  if (render_state_requires_exact_global_tone(state)) {
    return QStringLiteral("exact_global_tone");
  }
  if (render_state_requires_exact_post_color(state)) {
    return QStringLiteral("exact_post_color");
  }
  if (render_state_requires_cpu_pipeline(state)) {
    return QStringLiteral("cpu_required_stages");
  }
  return {};
#else
  Q_UNUSED(state);
  Q_UNUSED(pipeline_state);
  Q_UNUSED(pixels);
  Q_UNUSED(metal_available);
  return QStringLiteral("metal_not_compiled");
#endif
}

void apply_output_color_space_transform(QImage* image, const QString& output_color_space_name) {
  if (image == nullptr || image->isNull()) {
    return;
  }

  const auto target_color_space = output_color_space_for_name(output_color_space_name);
  if (!target_color_space.isValid()) {
    return;
  }

  *image = finalize_output_image(std::move(*image), target_color_space);
}

QImage normalize_preview_color_space(QImage image) {
  if (image.isNull()) {
    return image;
  }

  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_target_color_space());
  }
  return image;
}

RawDecodeSettings raw_decode_settings_for_state(const RenderEditState& state) {
  return RawDecodeSettings{
      .input_transform_enabled = state.raw_input_transform_enabled,
      .input_color_profile = state.input_color_profile,
      .highlight_reconstruction_mode = state.highlight_reconstruction_mode,
      .chromatic_adaptation_method = state.chromatic_adaptation_method,
      .demosaic_quality = state.raw_demosaic_quality,
      .exposure = state.raw_exposure,
      .boost_amount = state.raw_boost,
      .boost_shadow_amount = state.raw_shadow_boost,
      .detail_amount = state.raw_detail,
      .chroma_noise_reduction_amount = state.raw_chroma_noise_reduction,
      .moire_reduction_amount = state.raw_moire_reduction,
      .hot_dead_pixel_cleanup_amount = state.raw_hot_dead_pixel_cleanup,
      .false_color_suppression_amount = state.raw_false_color_suppression,
      .highlight_recovery_enabled = state.raw_highlight_recovery,
      .gamut_mapping_enabled = state.raw_gamut_mapping,
      .lens_correction_enabled = state.raw_lens_correction,
      .white_balance_override_enabled = state.raw_white_balance_override,
      .white_balance_temperature = state.raw_white_balance_temperature,
      .white_balance_tint = state.raw_white_balance_tint,
  };
}

QString raw_decode_settings_key(const RenderEditState& state) {
  return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17|%18|%19")
      .arg(state.raw_input_transform_enabled ? 1 : 0)
      .arg(normalized_input_color_profile_name(state.input_color_profile))
      .arg(normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode))
      .arg(normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method))
      .arg(normalized_raw_demosaic_quality_name(state.raw_demosaic_quality))
      .arg(state.raw_exposure, 0, 'f', 6)
      .arg(state.raw_boost, 0, 'f', 6)
      .arg(state.raw_shadow_boost, 0, 'f', 6)
      .arg(state.raw_detail, 0, 'f', 6)
      .arg(state.raw_chroma_noise_reduction, 0, 'f', 6)
      .arg(state.raw_moire_reduction, 0, 'f', 6)
      .arg(state.raw_hot_dead_pixel_cleanup, 0, 'f', 6)
      .arg(state.raw_false_color_suppression, 0, 'f', 6)
      .arg(state.raw_highlight_recovery ? 1 : 0)
      .arg(state.raw_gamut_mapping ? 1 : 0)
      .arg(state.raw_lens_correction ? 1 : 0)
      .arg(state.raw_white_balance_override ? 1 : 0)
      .arg(state.raw_white_balance_temperature, 0, 'f', 3)
      .arg(state.raw_white_balance_tint, 0, 'f', 3);
}

double clamp_unit(double value) {
  return qBound(0.0, value, 1.0);
}

double smoothstep(double edge0, double edge1, double x) {
  if (edge0 >= edge1) {
    return x < edge0 ? 0.0 : 1.0;
  }
  const auto t = clamp_unit((x - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

double map_double_value(const QVariantMap& map, const QString& key, double fallback) {
  return map.contains(key) ? map.value(key).toDouble() : fallback;
}

bool map_bool_value(const QVariantMap& map, const QString& key, bool fallback) {
  return map.contains(key) ? map.value(key).toBool() : fallback;
}

QString variant_sha1(const QVariant& value) {
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex());
}

QStringList normalized_string_list_value(
    const QVariantMap& map,
    const QString& list_key,
    const QString& single_key) {
  QStringList values;
  const auto append_value = [&](const QString& raw_value) {
    const auto value = raw_value.simplified();
    if (!value.isEmpty() && !values.contains(value)) {
      values.push_back(value);
    }
  };

  const auto list_value = map.value(list_key);
  if (list_value.canConvert<QStringList>()) {
    for (const auto& value : list_value.toStringList()) {
      append_value(value);
    }
  } else {
    for (const auto& value : list_value.toList()) {
      append_value(value.toString());
    }
  }
  append_value(map.value(single_key).toString());
  return values;
}

QStringList selection_mask_set_ids_for_layer(const QVariantMap& layer) {
  return normalized_string_list_value(
      layer,
      QStringLiteral("selectionMaskSetIds"),
      QStringLiteral("selectionMaskSetId"));
}

QStringList selection_mask_set_names_for_layer(const QVariantMap& layer) {
  return normalized_string_list_value(
      layer,
      QStringLiteral("selectionMaskSetNames"),
      QStringLiteral("selectionMaskSetName"));
}

enum class MaskRasterPolicy {
  UseStoredRaster,
  RebuildEditableAtOutputResolution,
};

double brush_point_weight_for_mask(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::LocalMask& mask,
    const photo_editor::pipeline::BrushPoint& point) {
  const auto brush_size = std::max(0.01, point.size);
  const auto dx = normalized_x - point.x;
  const auto dy = normalized_y - point.y;
  const auto distance = std::sqrt(dx * dx + dy * dy) / brush_size;
  const auto inner_scale = std::clamp(mask.brush_hardness, 0.0, 0.98);
  return clamp_unit(point.flow) * clamp_unit(point.opacity) *
         (1.0 - smoothstep(inner_scale, 1.0, distance));
}

void rebuild_brush_segmentation_at_output_resolution(
    photo_editor::pipeline::LocalMask* mask,
    int width,
    int height) {
  if (mask == nullptr || width <= 0 || height <= 0 || mask->brush_points.empty()) {
    return;
  }

  std::vector<unsigned char> rebuilt(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
  for (const auto& point : mask->brush_points) {
    const auto radius_x =
        std::max(1, static_cast<int>(std::ceil(std::max(0.01, point.size) * static_cast<double>(std::max(1, width - 1)))));
    const auto radius_y =
        std::max(1, static_cast<int>(std::ceil(std::max(0.01, point.size) * static_cast<double>(std::max(1, height - 1)))));
    const auto center_x = clamp_unit(point.x) * static_cast<double>(std::max(1, width - 1));
    const auto center_y = clamp_unit(point.y) * static_cast<double>(std::max(1, height - 1));
    const auto min_x = std::max(0, static_cast<int>(std::floor(center_x)) - radius_x);
    const auto max_x = std::min(width - 1, static_cast<int>(std::ceil(center_x)) + radius_x);
    const auto min_y = std::max(0, static_cast<int>(std::floor(center_y)) - radius_y);
    const auto max_y = std::min(height - 1, static_cast<int>(std::ceil(center_y)) + radius_y);
    for (int y = min_y; y <= max_y; ++y) {
      const auto normalized_y = height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      for (int x = min_x; x <= max_x; ++x) {
        const auto normalized_x = width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto point_weight = brush_point_weight_for_mask(normalized_x, normalized_y, *mask, point);
        if (point_weight <= 1e-6) {
          continue;
        }
        auto weight = static_cast<double>(rebuilt[static_cast<std::size_t>(y * width + x)]) / 255.0;
        if (point.erase) {
          weight *= (1.0 - point_weight);
        } else {
          weight = mask->brush_buildup ? 1.0 - (1.0 - weight) * (1.0 - point_weight)
                                       : std::max(weight, point_weight);
        }
        rebuilt[static_cast<std::size_t>(y * width + x)] =
            static_cast<unsigned char>(std::lround(clamp_unit(weight) * 255.0));
      }
    }
  }

  mask->segmentation_mask = std::move(rebuilt);
  mask->segmentation_width = width;
  mask->segmentation_height = height;
}

QString pipeline_mask_tool_role(const QVariantMap& mask) {
  if (mask.contains(QStringLiteral("toolRole"))) {
    return normalized_mask_tool_role(mask.value(QStringLiteral("toolRole")).toString());
  }

  const auto retouch_mode =
      mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
  if (!retouch_mode.isEmpty()) {
    return QStringLiteral("retouch");
  }

  const auto has_selection_adjustments =
      std::abs(map_double_value(mask, QStringLiteral("exposure"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("contrast"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("highlights"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("shadows"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("whites"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("blacks"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("temperature"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("tint"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("vibrance"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("saturation"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("hueShift"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("saturationShift"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("colorizeHue"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("colorizeStrength"), 0.0)) > 1e-6;
  if (!has_selection_adjustments &&
      std::abs(map_double_value(mask, QStringLiteral("removeStrength"), 0.0)) > 1e-6) {
    return QStringLiteral("retouch");
  }

  return QStringLiteral("selection");
}

std::vector<photo_editor::pipeline::CurvePoint> pipeline_curve_points(const QVariantList& points) {
  std::vector<photo_editor::pipeline::CurvePoint> curve_points;
  curve_points.reserve(static_cast<std::size_t>(points.size()));
  for (const auto& value : points) {
    const auto point = value.toMap();
    curve_points.push_back(photo_editor::pipeline::CurvePoint{
        .x = map_double_value(point, QStringLiteral("x"), 0.0),
        .y = map_double_value(point, QStringLiteral("y"), 0.0),
    });
  }
  return curve_points;
}

void populate_inpaint_patch(
    photo_editor::pipeline::LocalMask* local_mask,
    const QVariantMap& mask) {
  if (local_mask == nullptr) {
    return;
  }

  const auto patch_data = mask.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto patch_x = mask.value(QStringLiteral("inpaintPatchX")).toInt();
  const auto patch_y = mask.value(QStringLiteral("inpaintPatchY")).toInt();
  const auto patch_width = mask.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height = mask.value(QStringLiteral("inpaintPatchHeight")).toInt();
  if (patch_data.isEmpty() || patch_width <= 0 || patch_height <= 0) {
    return;
  }

  auto patch_image = QImage::fromData(patch_data, "PNG");
  if (patch_image.isNull() || patch_image.width() != patch_width ||
      patch_image.height() != patch_height) {
    return;
  }
  if (!patch_image.colorSpace().isValid()) {
    auto patch_color_space = output_color_space_for_name(
        mask.value(QStringLiteral("inpaintPatchColorSpace"), QStringLiteral("sRGB")).toString());
    if (!patch_color_space.isValid()) {
      patch_color_space = preview_target_color_space();
    }
    patch_image.setColorSpace(patch_color_space);
  }

  const auto working_patch = working_image_from_qimage(patch_image);
  if (working_patch.isNull()) {
    return;
  }

  local_mask->inpaint_patch_pixels = working_patch.pixels;
  local_mask->inpaint_patch_x = patch_x;
  local_mask->inpaint_patch_y = patch_y;
  local_mask->inpaint_patch_width = working_patch.width;
  local_mask->inpaint_patch_height = working_patch.height;
  local_mask->inpaint_model_name =
      mask.value(QStringLiteral("inpaintModelName")).toString().toStdString();
  local_mask->inpaint_blend_amount =
      qBound(0.0, mask.value(QStringLiteral("inpaintBlendAmount"), 1.0).toDouble(), 1.0);
  local_mask->inpaint_seam_feather =
      qBound(0.0, mask.value(QStringLiteral("inpaintSeamFeather"), 4.0).toDouble(), 24.0);
  local_mask->inpaint_color_match =
      qBound(0.0, mask.value(QStringLiteral("inpaintColorMatch"), 0.75).toDouble(), 1.0);
}

photo_editor::pipeline::LocalMask pipeline_local_mask(
    const QVariantMap& mask,
    MaskRasterPolicy raster_policy,
    const QSize& output_size) {
  photo_editor::pipeline::LocalMask local_mask;
  local_mask.id = mask.value(QStringLiteral("id")).toString().toStdString();
  local_mask.parent_id = mask.value(QStringLiteral("parentId")).toString().toStdString();
  local_mask.output_id = mask.value(QStringLiteral("outputId")).toString().toStdString();
  local_mask.name = mask.value(QStringLiteral("name")).toString().toStdString();
  local_mask.kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString()).toStdString();
  local_mask.operation =
      normalized_mask_operation(mask.value(QStringLiteral("operation")).toString()).toStdString();
  local_mask.blend_mode =
      normalized_mask_blend_mode(mask.value(QStringLiteral("blendMode")).toString()).toStdString();
  local_mask.retouch_mode = mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower().toStdString();
  local_mask.retouch_order = std::max(0, mask.value(QStringLiteral("retouchOrder")).toInt());
  local_mask.enabled = mask.contains(QStringLiteral("enabled"))
                           ? mask.value(QStringLiteral("enabled")).toBool()
                           : true;
  local_mask.reusable_output = map_bool_value(mask, QStringLiteral("reusableOutput"), false);
  local_mask.invert = map_bool_value(
      mask,
      QStringLiteral("invert"),
      map_bool_value(mask, QStringLiteral("selectionInvert"), false));
  local_mask.opacity = std::clamp(map_double_value(mask, QStringLiteral("opacity"), 1.0), 0.0, 1.0);
  local_mask.density = std::clamp(map_double_value(mask, QStringLiteral("density"), 1.0), 0.0, 1.0);
  local_mask.exposure = map_double_value(mask, QStringLiteral("exposure"), 0.0);
  local_mask.contrast = map_double_value(mask, QStringLiteral("contrast"), 0.0);
  local_mask.highlights = map_double_value(mask, QStringLiteral("highlights"), 0.0);
  local_mask.shadows = map_double_value(mask, QStringLiteral("shadows"), 0.0);
  local_mask.whites = map_double_value(mask, QStringLiteral("whites"), 0.0);
  local_mask.blacks = map_double_value(mask, QStringLiteral("blacks"), 0.0);
  local_mask.temperature = map_double_value(mask, QStringLiteral("temperature"), 0.0);
  local_mask.tint = map_double_value(mask, QStringLiteral("tint"), 0.0);
  local_mask.vibrance = map_double_value(mask, QStringLiteral("vibrance"), 0.0);
  local_mask.saturation = map_double_value(mask, QStringLiteral("saturation"), 0.0);
  local_mask.hue_shift = map_double_value(mask, QStringLiteral("hueShift"), 0.0);
  local_mask.saturation_shift = map_double_value(mask, QStringLiteral("saturationShift"), 0.0);
  local_mask.colorize_hue = map_double_value(mask, QStringLiteral("colorizeHue"), 0.0);
  local_mask.colorize_strength = map_double_value(mask, QStringLiteral("colorizeStrength"), 0.0);
  local_mask.remove_strength = map_double_value(mask, QStringLiteral("removeStrength"), 0.0);
  local_mask.heal_texture_blend =
      std::clamp(map_double_value(mask, QStringLiteral("healTextureBlend"), 0.68), 0.0, 1.0);
  local_mask.heal_color_blend =
      std::clamp(map_double_value(mask, QStringLiteral("healColorBlend"), 0.46), 0.0, 1.0);
  local_mask.selection_invert = map_bool_value(mask, QStringLiteral("selectionInvert"), local_mask.invert);
  local_mask.selection_expand = map_double_value(mask, QStringLiteral("selectionExpand"), 0.0);
  local_mask.selection_smooth = map_double_value(mask, QStringLiteral("selectionSmooth"), 0.0);
  local_mask.selection_fine_edge = map_double_value(mask, QStringLiteral("selectionFineEdge"), 0.0);
  local_mask.selection_edge_contrast =
      map_double_value(mask, QStringLiteral("selectionEdgeContrast"), 0.0);
  local_mask.selection_decontaminate =
      map_double_value(mask, QStringLiteral("selectionDecontaminate"), 0.0);
  local_mask.selection_grow = map_double_value(mask, QStringLiteral("selectionGrow"), 0.0);
  local_mask.selection_shrink = map_double_value(mask, QStringLiteral("selectionShrink"), 0.0);
  local_mask.selection_feather = map_double_value(mask, QStringLiteral("selectionFeather"), 0.0);
  local_mask.selection_blur = map_double_value(mask, QStringLiteral("selectionBlur"), 0.0);
  local_mask.selection_harden = map_double_value(mask, QStringLiteral("selectionHarden"), 0.0);
  local_mask.selection_despeckle = map_double_value(mask, QStringLiteral("selectionDespeckle"), 0.0);
  local_mask.selection_fill_holes = map_double_value(mask, QStringLiteral("selectionFillHoles"), 0.0);
  local_mask.selection_remove_islands =
      map_double_value(mask, QStringLiteral("selectionRemoveIslands"), 0.0);
  local_mask.refine_luminance_min = map_double_value(mask, QStringLiteral("refineLuminanceMin"), 0.0);
  local_mask.refine_luminance_max = map_double_value(mask, QStringLiteral("refineLuminanceMax"), 1.0);
  local_mask.refine_hue_center = map_double_value(mask, QStringLiteral("refineHueCenter"), 0.0);
  local_mask.refine_hue_range = map_double_value(mask, QStringLiteral("refineHueRange"), 0.5);
  local_mask.refine_saturation_min = map_double_value(mask, QStringLiteral("refineSaturationMin"), 0.0);
  local_mask.refine_saturation_max = map_double_value(mask, QStringLiteral("refineSaturationMax"), 1.0);
  local_mask.refine_depth_min = map_double_value(mask, QStringLiteral("refineDepthMin"), 0.0);
  local_mask.refine_depth_max = map_double_value(mask, QStringLiteral("refineDepthMax"), 1.0);
  local_mask.refine_edge_confidence_min =
      map_double_value(mask, QStringLiteral("refineEdgeConfidenceMin"), 0.0);
  local_mask.refine_range_feather = map_double_value(mask, QStringLiteral("refineRangeFeather"), 0.05);
  local_mask.feather = map_double_value(
      mask,
      QStringLiteral("feather"),
      local_mask.kind == "radial" ? 0.25
      : local_mask.kind == "ellipse" || local_mask.kind == "rectangle" ? 0.08
      : 0.22);
  local_mask.rotation = map_double_value(mask, QStringLiteral("rotation"), 0.0);

  if (local_mask.kind == "radial" || local_mask.kind == "ellipse" ||
      local_mask.kind == "rectangle") {
    local_mask.center_x = map_double_value(mask, QStringLiteral("centerX"), 0.5);
    local_mask.center_y = map_double_value(mask, QStringLiteral("centerY"), 0.5);
    local_mask.radius_x = map_double_value(
        mask, QStringLiteral("radiusX"), local_mask.kind == "rectangle" ? 0.30 : 0.25);
    local_mask.radius_y = map_double_value(
        mask, QStringLiteral("radiusY"), local_mask.kind == "rectangle" ? 0.20 : 0.25);
  } else if (local_mask.kind == "linear") {
    local_mask.position = map_double_value(mask, QStringLiteral("position"), 0.5);
  } else if (local_mask.kind == "brush") {
    local_mask.feather = map_double_value(mask, QStringLiteral("feather"), 0.75);
    local_mask.brush_size = map_double_value(mask, QStringLiteral("brushSize"), 0.08);
    local_mask.brush_flow = map_double_value(mask, QStringLiteral("brushFlow"), 1.0);
    local_mask.brush_hardness = map_double_value(mask, QStringLiteral("brushHardness"), 0.25);
    local_mask.brush_buildup = map_bool_value(mask, QStringLiteral("brushBuildup"), true);
    local_mask.clone_source_set = map_bool_value(mask, QStringLiteral("cloneSourceSet"), false);
    local_mask.clone_source_x = map_double_value(mask, QStringLiteral("cloneSourceX"), 0.5);
    local_mask.clone_source_y = map_double_value(mask, QStringLiteral("cloneSourceY"), 0.5);
    local_mask.clone_offset_x = map_double_value(mask, QStringLiteral("cloneOffsetX"), 0.0);
    local_mask.clone_offset_y = map_double_value(mask, QStringLiteral("cloneOffsetY"), 0.0);
    local_mask.clone_scale = map_double_value(mask, QStringLiteral("cloneScale"), 1.0);
    local_mask.clone_rotation = map_double_value(mask, QStringLiteral("cloneRotation"), 0.0);
    local_mask.clone_aligned = map_bool_value(mask, QStringLiteral("cloneAligned"), true);
    const auto points = mask.value(QStringLiteral("brushPoints")).toList();
    local_mask.brush_points.reserve(static_cast<std::size_t>(points.size()));
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      local_mask.brush_points.push_back(photo_editor::pipeline::BrushPoint{
          .x = map_double_value(point, QStringLiteral("x"), 0.5),
          .y = map_double_value(point, QStringLiteral("y"), 0.5),
          .size = map_double_value(point, QStringLiteral("size"), local_mask.brush_size),
          .flow = map_double_value(point, QStringLiteral("flow"), local_mask.brush_flow),
          .opacity = map_double_value(point, QStringLiteral("opacity"), 1.0),
          .auto_mask_anchor_x = map_double_value(
              point,
              QStringLiteral("autoMaskAnchorX"),
              map_double_value(point, QStringLiteral("x"), 0.5)),
          .auto_mask_anchor_y = map_double_value(
              point,
              QStringLiteral("autoMaskAnchorY"),
              map_double_value(point, QStringLiteral("y"), 0.5)),
          .pressure = map_double_value(point, QStringLiteral("pressure"), 1.0),
          .tilt_x = map_double_value(point, QStringLiteral("tiltX"), 0.0),
          .tilt_y = map_double_value(point, QStringLiteral("tiltY"), 0.0),
          .erase = map_bool_value(point, QStringLiteral("erase"), false),
      });
    }
    const auto seg_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto seg_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto seg_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (raster_policy == MaskRasterPolicy::RebuildEditableAtOutputResolution &&
        !local_mask.brush_points.empty() && output_size.isValid()) {
      rebuild_brush_segmentation_at_output_resolution(&local_mask, output_size.width(), output_size.height());
    } else if (valid_segmentation_payload(seg_data, seg_width, seg_height)) {
      local_mask.segmentation_mask.resize(static_cast<std::size_t>(seg_data.size()));
      std::memcpy(local_mask.segmentation_mask.data(),
                  seg_data.constData(),
                  static_cast<std::size_t>(seg_data.size()));
      local_mask.segmentation_width = seg_width;
      local_mask.segmentation_height = seg_height;
    }
  } else if (local_mask.kind == "polygon" || local_mask.kind == "lasso") {
    local_mask.feather = map_double_value(mask, QStringLiteral("feather"), 0.06);
    const auto points = mask.value(QStringLiteral("polygonPoints")).toList();
    local_mask.polygon_points.reserve(static_cast<std::size_t>(points.size()));
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      local_mask.polygon_points.push_back(photo_editor::pipeline::CurvePoint{
          .x = map_double_value(point, QStringLiteral("x"), 0.5),
          .y = map_double_value(point, QStringLiteral("y"), 0.5),
      });
    }
  } else if (local_mask.kind == "luminance") {
    local_mask.luminance_min = map_double_value(mask, QStringLiteral("luminanceMin"), 0.2);
    local_mask.luminance_max = map_double_value(mask, QStringLiteral("luminanceMax"), 0.8);
    local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.1);
  } else if (local_mask.kind == "color") {
    local_mask.hue_center = map_double_value(mask, QStringLiteral("hueCenter"), 0.08);
    local_mask.hue_range = map_double_value(mask, QStringLiteral("hueRange"), 0.12);
    local_mask.saturation_min = map_double_value(mask, QStringLiteral("saturationMin"), 0.1);
    local_mask.saturation_max = map_double_value(mask, QStringLiteral("saturationMax"), 1.0);
    local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.08);
  } else if (local_mask.kind == "depth") {
    local_mask.depth_min = map_double_value(mask, QStringLiteral("depthMin"), 0.2);
    local_mask.depth_max = map_double_value(mask, QStringLiteral("depthMax"), 0.8);
    local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.1);
    const auto depth_data = mask.value(QStringLiteral("depthData")).toByteArray();
    const auto depth_width = mask.value(QStringLiteral("depthWidth")).toInt();
    const auto depth_height = mask.value(QStringLiteral("depthHeight")).toInt();
    if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
      local_mask.depth_map.resize(static_cast<std::size_t>(depth_data.size()));
      std::memcpy(local_mask.depth_map.data(),
                  depth_data.constData(),
                  static_cast<std::size_t>(depth_data.size()));
      local_mask.depth_width = depth_width;
      local_mask.depth_height = depth_height;
    }
  } else if (is_raster_mask_kind(QString::fromStdString(local_mask.kind))) {
    const auto seg_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto seg_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto seg_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(seg_data, seg_width, seg_height)) {
      local_mask.segmentation_mask.resize(static_cast<std::size_t>(seg_data.size()));
      std::memcpy(local_mask.segmentation_mask.data(),
                  seg_data.constData(),
                  static_cast<std::size_t>(seg_data.size()));
      local_mask.segmentation_width = seg_width;
      local_mask.segmentation_height = seg_height;
    }
  }

  populate_inpaint_patch(&local_mask, mask);
  return local_mask;
}

std::vector<photo_editor::pipeline::LocalMaskSet> pipeline_local_mask_sets(
    const QVariantList& masks,
    MaskRasterPolicy raster_policy,
    const QSize& output_size) {
  std::vector<photo_editor::pipeline::LocalMaskSet> mask_sets;
  QHash<QString, int> indices_by_id;
  auto selection_solo_active = false;
  auto solo_ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), solo_ordinal);
    ++solo_ordinal;
    if (mask_definition_affects_render(mask, solo_ordinal - 1) &&
        pipeline_mask_tool_role(mask) == QStringLiteral("selection") &&
        mask.value(QStringLiteral("solo")).toBool()) {
      selection_solo_active = true;
      break;
    }
  }
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("selection") ||
        (selection_solo_active && !mask.value(QStringLiteral("solo")).toBool())) {
      continue;
    }

    auto source = pipeline_local_mask(mask, raster_policy, output_size);
    source.exposure = 0.0;
    source.contrast = 0.0;
    source.highlights = 0.0;
    source.shadows = 0.0;
    source.whites = 0.0;
    source.blacks = 0.0;
    source.temperature = 0.0;
    source.tint = 0.0;
    source.vibrance = 0.0;
    source.saturation = 0.0;
    source.hue_shift = 0.0;
    source.saturation_shift = 0.0;
    source.colorize_hue = 0.0;
    source.colorize_strength = 0.0;
    source.remove_strength = 0.0;
    source.retouch_mode.clear();

    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
    auto set_index = indices_by_id.value(set_id, -1);
    if (set_index < 0) {
      photo_editor::pipeline::LocalMaskSet set;
      set.id = set_id.toStdString();
      set.name = mask.value(QStringLiteral("maskSetName")).toString().toStdString();
      set.enabled = map_bool_value(mask, QStringLiteral("enabled"), true);
      set.exposure = map_double_value(mask, QStringLiteral("exposure"), 0.0);
      set.contrast = map_double_value(mask, QStringLiteral("contrast"), 0.0);
      set.highlights = map_double_value(mask, QStringLiteral("highlights"), 0.0);
      set.shadows = map_double_value(mask, QStringLiteral("shadows"), 0.0);
      set.whites = map_double_value(mask, QStringLiteral("whites"), 0.0);
      set.blacks = map_double_value(mask, QStringLiteral("blacks"), 0.0);
      set.temperature = map_double_value(mask, QStringLiteral("temperature"), 0.0);
      set.tint = map_double_value(mask, QStringLiteral("tint"), 0.0);
      set.vibrance = map_double_value(mask, QStringLiteral("vibrance"), 0.0);
      set.saturation = map_double_value(mask, QStringLiteral("saturation"), 0.0);
      set.hue_shift = map_double_value(mask, QStringLiteral("hueShift"), 0.0);
      set.saturation_shift = map_double_value(mask, QStringLiteral("saturationShift"), 0.0);
      set.colorize_hue = map_double_value(mask, QStringLiteral("colorizeHue"), 0.0);
      set.colorize_strength = map_double_value(mask, QStringLiteral("colorizeStrength"), 0.0);
      mask_sets.push_back(std::move(set));
      set_index = static_cast<int>(mask_sets.size()) - 1;
      indices_by_id.insert(set_id, set_index);
    }

    mask_sets[static_cast<std::size_t>(set_index)].sources.push_back(std::move(source));
  }

  return mask_sets;
}

std::vector<photo_editor::pipeline::LocalMask> pipeline_retouch_masks(
    const QVariantList& masks,
    MaskRasterPolicy raster_policy,
    const QSize& output_size) {
  std::vector<photo_editor::pipeline::LocalMask> retouch_masks;
  retouch_masks.reserve(static_cast<std::size_t>(masks.size()));
  auto retouch_solo_active = false;
  auto solo_ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), solo_ordinal);
    ++solo_ordinal;
    if (mask_definition_affects_render(mask, solo_ordinal - 1) &&
        pipeline_mask_tool_role(mask) == QStringLiteral("retouch") &&
        mask.value(QStringLiteral("solo")).toBool()) {
      retouch_solo_active = true;
      break;
    }
  }
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("retouch") ||
        (retouch_solo_active && !mask.value(QStringLiteral("solo")).toBool())) {
      continue;
    }
    retouch_masks.push_back(pipeline_local_mask(mask, raster_policy, output_size));
  }
  std::stable_sort(
      retouch_masks.begin(),
      retouch_masks.end(),
      [](const auto& left, const auto& right) {
        return left.retouch_order < right.retouch_order;
      });
  return retouch_masks;
}

double layer_adjustment_value(const QVariantMap& layer, const QString& key) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(key)) {
    return adjustments.value(key).toDouble();
  }
  return layer.value(key).toDouble();
}

QVariantList layer_adjustment_curve_points(const QVariantMap& layer, const QString& key) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(key)) {
    return adjustments.value(key).toList();
  }
  return layer.value(key).toList();
}

QVariantMap layer_adjustment_object(const QVariantMap& layer, const QString& key) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  const auto value = adjustments.contains(key) ? adjustments.value(key) : layer.value(key);
  return value.toMap();
}

double layer_object_double(const QVariantMap& object, const QString& key, double fallback = 0.0) {
  return object.contains(key) ? object.value(key).toDouble() : fallback;
}

bool layer_object_bool(const QVariantMap& object, const QString& key, bool fallback = false) {
  return object.contains(key) ? object.value(key).toBool() : fallback;
}

QString layer_object_string(const QVariantMap& object, const QString& key, const QString& fallback) {
  return object.contains(key) ? object.value(key).toString() : fallback;
}

std::array<double, 3> layer_lut_domain_array(
    const QVariant& value,
    const std::array<double, 3>& fallback) {
  auto domain = fallback;
  const auto list = value.toList();
  if (list.size() >= 3) {
    for (int index = 0; index < 3; ++index) {
      domain[static_cast<std::size_t>(index)] = list.at(index).toDouble();
    }
  }
  return domain;
}

std::vector<photo_editor::pipeline::ColorPixel> layer_lut_table(const QVariant& value) {
  std::vector<photo_editor::pipeline::ColorPixel> table;
  const auto list = value.toList();
  table.reserve(static_cast<std::size_t>(list.size()));
  for (const auto& entry : list) {
    const auto object = entry.toMap();
    if (object.isEmpty()) {
      const auto row = entry.toList();
      if (row.size() >= 3) {
        table.push_back(photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(qBound(0.0, row.at(0).toDouble(), 1.0)),
            .green = static_cast<float>(qBound(0.0, row.at(1).toDouble(), 1.0)),
            .blue = static_cast<float>(qBound(0.0, row.at(2).toDouble(), 1.0)),
            .alpha = static_cast<float>(qBound(0.0, row.value(3, 1.0).toDouble(), 1.0)),
        });
      }
      continue;
    }
    table.push_back(photo_editor::pipeline::ColorPixel{
        .red = static_cast<float>(qBound(0.0, object.value(QStringLiteral("red")).toDouble(), 1.0)),
        .green = static_cast<float>(qBound(0.0, object.value(QStringLiteral("green")).toDouble(), 1.0)),
        .blue = static_cast<float>(qBound(0.0, object.value(QStringLiteral("blue")).toDouble(), 1.0)),
        .alpha = static_cast<float>(qBound(0.0, object.value(QStringLiteral("alpha"), 1.0).toDouble(), 1.0)),
    });
  }
  return table;
}

photo_editor::pipeline::LutAdjustment layer_lut_adjustment_from_object(const QVariantMap& object) {
  return photo_editor::pipeline::LutAdjustment{
      .enabled = layer_object_bool(object, QStringLiteral("enabled"), true),
      .strength = qBound(0.0, layer_object_double(object, QStringLiteral("strength"), 100.0), 100.0),
      .name = object.value(QStringLiteral("name")).toString().toStdString(),
      .table = layer_lut_table(object.value(QStringLiteral("table"))),
      .size = qBound(0, object.value(QStringLiteral("size")).toInt(), 256),
      .domain_min = layer_lut_domain_array(
          object.value(QStringLiteral("domainMin")),
          std::array<double, 3>{0.0, 0.0, 0.0}),
      .domain_max = layer_lut_domain_array(
          object.value(QStringLiteral("domainMax")),
          std::array<double, 3>{1.0, 1.0, 1.0}),
      .input_color_space = object.value(QStringLiteral("inputColorSpace"), QStringLiteral("sRGB"))
                               .toString()
                               .toStdString(),
      .output_color_space = object.value(QStringLiteral("outputColorSpace"), QStringLiteral("sRGB"))
                                .toString()
                                .toStdString(),
  };
}

std::vector<photo_editor::pipeline::LutAdjustment> layer_lut_stack(const QVariantMap& layer) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  const auto lut_object = adjustments.contains(QStringLiteral("lut"))
                              ? adjustments.value(QStringLiteral("lut")).toMap()
                              : layer.value(QStringLiteral("lut")).toMap();
  const auto stack_value = adjustments.contains(QStringLiteral("lutStack"))
                               ? adjustments.value(QStringLiteral("lutStack"))
                               : layer.value(QStringLiteral("lutStack"));
  std::vector<photo_editor::pipeline::LutAdjustment> stack;
  for (const auto& value : stack_value.toList()) {
    const auto object = value.toMap();
    if (!object.isEmpty()) {
      stack.push_back(layer_lut_adjustment_from_object(object));
    }
  }
  if (!lut_object.isEmpty()) {
    stack.insert(stack.begin(), layer_lut_adjustment_from_object(lut_object));
  }
  return stack;
}

QString layer_nested_adjustment_string(
    const QVariantMap& layer,
    const QString& object_key,
    const QString& key,
    const QString& flat_key,
    const QString& fallback) {
  const auto object = layer_adjustment_object(layer, object_key);
  if (object.contains(key)) {
    return object.value(key).toString();
  }
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(flat_key)) {
    return adjustments.value(flat_key).toString();
  }
  return layer.value(flat_key, fallback).toString();
}

double layer_nested_adjustment_value(
    const QVariantMap& layer,
    const QString& object_key,
    const QString& key,
    const QString& flat_key,
    double fallback = 0.0) {
  const auto object = layer_adjustment_object(layer, object_key);
  if (object.contains(key)) {
    return object.value(key).toDouble();
  }
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(flat_key)) {
    return adjustments.value(flat_key).toDouble();
  }
  return layer.value(flat_key, fallback).toDouble();
}

std::array<photo_editor::pipeline::ColorMixerChannelAdjustment,
           photo_editor::pipeline::kColorMixerChannelCount>
layer_color_mixer_channels(const QVariantMap& layer) {
  std::array<photo_editor::pipeline::ColorMixerChannelAdjustment,
             photo_editor::pipeline::kColorMixerChannelCount> channels{};
  const auto color_mixer = layer_adjustment_object(layer, QStringLiteral("colorMixer"));
  const auto mixer_channels = color_mixer.value(QStringLiteral("channels")).toMap();
  for (std::size_t index = 0; index < channels.size(); ++index) {
    const auto name = QString::fromUtf8(
        photo_editor::pipeline::kColorMixerChannelNames[index]);
    const auto channel = mixer_channels.value(name).toMap();
    if (channel.isEmpty()) {
      continue;
    }
    channels[index].hue = channel.value(QStringLiteral("hue")).toDouble();
    channels[index].saturation = channel.value(QStringLiteral("saturation")).toDouble();
    channels[index].luminance = channel.value(QStringLiteral("luminance")).toDouble();
    channels[index].selective_cyan = channel.value(QStringLiteral("selectiveCyan")).toDouble();
    channels[index].selective_magenta = channel.value(QStringLiteral("selectiveMagenta")).toDouble();
    channels[index].selective_yellow = channel.value(QStringLiteral("selectiveYellow")).toDouble();
    channels[index].selective_black = channel.value(QStringLiteral("selectiveBlack")).toDouble();
  }
  return channels;
}

photo_editor::pipeline::RgbChannelMixerAdjustment layer_rgb_channel_mixer(const QVariantMap& layer) {
  photo_editor::pipeline::RgbChannelMixerAdjustment mixer;
  const auto color_mixer = layer_adjustment_object(layer, QStringLiteral("colorMixer"));
  const auto object = color_mixer.value(QStringLiteral("rgbChannelMixer")).toMap();
  if (object.isEmpty()) {
    return mixer;
  }
  mixer.red_from_red = layer_object_double(object, QStringLiteral("redFromRed"), 100.0);
  mixer.red_from_green = object.value(QStringLiteral("redFromGreen")).toDouble();
  mixer.red_from_blue = object.value(QStringLiteral("redFromBlue")).toDouble();
  mixer.green_from_red = object.value(QStringLiteral("greenFromRed")).toDouble();
  mixer.green_from_green = layer_object_double(object, QStringLiteral("greenFromGreen"), 100.0);
  mixer.green_from_blue = object.value(QStringLiteral("greenFromBlue")).toDouble();
  mixer.blue_from_red = object.value(QStringLiteral("blueFromRed")).toDouble();
  mixer.blue_from_green = object.value(QStringLiteral("blueFromGreen")).toDouble();
  mixer.blue_from_blue = layer_object_double(object, QStringLiteral("blueFromBlue"), 100.0);
  return mixer;
}

std::array<double, photo_editor::pipeline::kColorMixerChannelCount> layer_black_and_white_mixer(
    const QVariantMap& layer) {
  std::array<double, photo_editor::pipeline::kColorMixerChannelCount> mixer{};
  const auto color_mixer = layer_adjustment_object(layer, QStringLiteral("colorMixer"));
  const auto black_and_white = color_mixer.value(QStringLiteral("blackAndWhite")).toMap();
  const auto channels = black_and_white.value(QStringLiteral("channels")).toMap();
  for (std::size_t index = 0; index < mixer.size(); ++index) {
    const auto name = QString::fromUtf8(
        photo_editor::pipeline::kColorMixerChannelNames[index]);
    mixer[index] = channels.value(name).toDouble();
  }
  return mixer;
}

QVariantMap layer_color_mixer_child(const QVariantMap& layer, const QString& key) {
  return layer_adjustment_object(layer, QStringLiteral("colorMixer")).value(key).toMap();
}

QVariantMap layer_color_grading_child(const QVariantMap& layer, const QString& key) {
  return layer_adjustment_object(layer, QStringLiteral("colorGrading")).value(key).toMap();
}

photo_editor::pipeline::ColorPixel layer_fill_color(const QVariantMap& layer) {
  auto color = QColor(layer.value(QStringLiteral("fillColor"), QStringLiteral("#808080")).toString());
  if (!color.isValid()) {
    color = QColor(QStringLiteral("#808080"));
  }
  return photo_editor::pipeline::ColorPixel{
      .red = static_cast<float>(color.redF()),
      .green = static_cast<float>(color.greenF()),
      .blue = static_cast<float>(color.blueF()),
      .alpha = static_cast<float>(qBound(0.0, layer.value(QStringLiteral("fillAlpha"), 1.0).toDouble(), 1.0)),
  };
}

Qt::Alignment text_alignment_flag(const QVariantMap& layer) {
  const auto alignment = layer.value(QStringLiteral("textAlignment")).toString().trimmed().toLower();
  if (alignment == QStringLiteral("center")) {
    return Qt::AlignHCenter;
  }
  if (alignment == QStringLiteral("right")) {
    return Qt::AlignRight;
  }
  return Qt::AlignLeft;
}

QImage rasterized_text_layer_image(const QVariantMap& layer) {
  const auto text = layer.value(QStringLiteral("text"), QStringLiteral("Text")).toString();
  const auto width = qBound(16, layer.value(QStringLiteral("pixelWidth"), 512).toInt(), 8192);
  const auto height = qBound(16, layer.value(QStringLiteral("pixelHeight"), 128).toInt(), 8192);
  auto color = QColor(layer.value(QStringLiteral("textColor"), QStringLiteral("#ffffff")).toString());
  if (!color.isValid()) {
    color = QColor(QStringLiteral("#ffffff"));
  }

  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  if (!gui_application_available()) {
    return image;
  }
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  QFont font(layer.value(QStringLiteral("fontFamily"), QStringLiteral("Helvetica")).toString());
  font.setPixelSize(qBound(8, layer.value(QStringLiteral("fontSize"), 48).toInt(), 512));
  font.setBold(layer.value(QStringLiteral("fontWeight")).toString().trimmed().toLower() ==
               QStringLiteral("bold"));
  painter.setFont(font);
  painter.setPen(color);
  const auto text_flags = static_cast<int>(text_alignment_flag(layer)) |
                          static_cast<int>(Qt::AlignVCenter) |
                          static_cast<int>(Qt::TextWordWrap);
  painter.drawText(
      image.rect().adjusted(8, 4, -8, -4),
      text_flags,
      text);
  return image;
}

void populate_photo_layer_mask(photo_editor::pipeline::PhotoLayer* target, const QVariantMap& layer) {
  if (target == nullptr) {
    return;
  }
  const auto mask_data = layer.value(QStringLiteral("maskData")).toByteArray();
  const auto mask_width = layer.value(QStringLiteral("maskWidth")).toInt();
  const auto mask_height = layer.value(QStringLiteral("maskHeight")).toInt();
  const auto expected_size = mask_width * mask_height;
  if (mask_data.isEmpty() || mask_width <= 0 || mask_height <= 0 ||
      mask_data.size() != expected_size) {
    return;
  }
  target->mask_data.assign(mask_data.cbegin(), mask_data.cend());
  target->mask_width = mask_width;
  target->mask_height = mask_height;
  target->mask_inverted = layer.value(QStringLiteral("maskInverted")).toBool();
  target->mask_opacity =
      std::clamp(layer.value(QStringLiteral("maskOpacity"), 1.0).toDouble(), 0.0, 1.0);
  target->mask_density =
      std::clamp(layer.value(QStringLiteral("maskDensity"), 1.0).toDouble(), 0.0, 1.0);
}

QSize selection_layer_mask_raster_size(
    const QVariantList& masks,
    const QVariantMap& layer,
    const QSize& output_size) {
  if (output_size.isValid() && output_size.width() > 0 && output_size.height() > 0) {
    return output_size;
  }

  const auto layer_ids = selection_mask_set_ids_for_layer(layer);
  const auto layer_names = selection_mask_set_names_for_layer(layer);
  for (const auto& value : masks) {
    const auto mask = value.toMap();
    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString().simplified();
    const auto set_name = mask.value(QStringLiteral("maskSetName")).toString().simplified();
    const auto matches = (!set_id.isEmpty() && layer_ids.contains(set_id)) ||
                         (!set_name.isEmpty() && layer_names.contains(set_name));
    if (!matches) {
      continue;
    }
    const auto segmentation_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto segmentation_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto segmentation_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
      return QSize(segmentation_width, segmentation_height);
    }
  }
  return QSize(1024, 1024);
}

bool selection_layer_mask_set_is_identity(const photo_editor::pipeline::LocalMaskSet& mask_set) {
  return mask_set.enabled &&
         !mask_set.selection_invert &&
         std::abs(mask_set.selection_expand) <= 1e-6 &&
         std::abs(mask_set.selection_smooth) <= 1e-6 &&
         std::abs(mask_set.selection_fine_edge) <= 1e-6 &&
         std::abs(mask_set.selection_edge_contrast) <= 1e-6 &&
         std::abs(mask_set.opacity - 1.0) <= 1e-6 &&
         std::abs(mask_set.density - 1.0) <= 1e-6;
}

bool selection_layer_source_is_stored_raster_passthrough(
    const photo_editor::pipeline::LocalMask& source) {
  const auto expected_size =
      static_cast<std::size_t>(source.segmentation_width) *
      static_cast<std::size_t>(source.segmentation_height);
  const auto source_kind = QString::fromStdString(source.kind);
  return source.enabled &&
         source.operation == "add" &&
         !source.invert &&
         std::abs(source.opacity - 1.0) <= 1e-6 &&
         std::abs(source.density - 1.0) <= 1e-6 &&
         (source.kind == "brush" || is_ai_segmentation_mask_kind(source_kind)) &&
         source.segmentation_width > 0 &&
         source.segmentation_height > 0 &&
         source.segmentation_mask.size() == expected_size;
}

bool populate_photo_layer_selection_mask_from_stored_source(
    photo_editor::pipeline::PhotoLayer* target,
    const QVariantMap& layer,
    const photo_editor::pipeline::LocalMaskSet& mask_set,
    const QSize& raster_size) {
  if (target == nullptr || mask_set.sources.size() != 1 ||
      !selection_layer_mask_set_is_identity(mask_set)) {
    return false;
  }

  const auto& source = mask_set.sources.front();
  if (!selection_layer_source_is_stored_raster_passthrough(source)) {
    return false;
  }

  const QByteArray source_data(
      reinterpret_cast<const char*>(source.segmentation_mask.data()),
      static_cast<qsizetype>(source.segmentation_mask.size()));

  QVariantMap cache_payload;
  cache_payload.insert(
      QStringLiteral("version"),
      QStringLiteral("render-utils-linked-layer-mask-alpha-provider-cache-v1"));
  cache_payload.insert(QStringLiteral("layerId"), layer.value(QStringLiteral("id")).toString());
  cache_payload.insert(QStringLiteral("maskSetId"), QString::fromStdString(mask_set.id));
  cache_payload.insert(QStringLiteral("maskSetName"), QString::fromStdString(mask_set.name));
  cache_payload.insert(QStringLiteral("sourceKind"), QString::fromStdString(source.kind));
  cache_payload.insert(QStringLiteral("sourceOperation"), QString::fromStdString(source.operation));
  cache_payload.insert(QStringLiteral("sourceWidth"), source.segmentation_width);
  cache_payload.insert(QStringLiteral("sourceHeight"), source.segmentation_height);
  cache_payload.insert(QStringLiteral("targetWidth"), raster_size.width());
  cache_payload.insert(QStringLiteral("targetHeight"), raster_size.height());
  cache_payload.insert(
      QStringLiteral("sourceAlphaSha256"),
      QString::fromLatin1(QCryptographicHash::hash(source_data, QCryptographicHash::Sha256).toHex()));
  const auto cache_key = QStringLiteral("sha1:%1").arg(variant_sha1(cache_payload));
  const auto alpha_result = evaluate_mask_alpha_cached(
      cache_key,
      raster_size,
      1,
      [&](int, int, double normalized_x, double normalized_y) {
        const auto source_x = source.segmentation_width > 1
                                  ? normalized_x * static_cast<double>(source.segmentation_width - 1)
                                  : 0.0;
        const auto source_y = source.segmentation_height > 1
                                  ? normalized_y * static_cast<double>(source.segmentation_height - 1)
                                  : 0.0;
        const auto x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, source.segmentation_width - 1);
        const auto y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, source.segmentation_height - 1);
        const auto x1 = std::min(source.segmentation_width - 1, x0 + 1);
        const auto y1 = std::min(source.segmentation_height - 1, y0 + 1);
        const auto fx = source_x - static_cast<double>(x0);
        const auto fy = source_y - static_cast<double>(y0);
        const auto sample = [&](int sample_x, int sample_y) {
          return static_cast<double>(
              static_cast<unsigned char>(source_data.at(sample_y * source.segmentation_width + sample_x))) /
                 255.0;
        };
        const auto top = std::lerp(sample(x0, y0), sample(x1, y0), fx);
        const auto bottom = std::lerp(sample(x0, y1), sample(x1, y1), fx);
        return std::lerp(top, bottom, fy);
      });
  if (!alpha_result.valid ||
      !valid_segmentation_payload(alpha_result.alpha_mask, raster_size.width(), raster_size.height())) {
    return false;
  }

  target->mask_data.assign(alpha_result.alpha_mask.cbegin(), alpha_result.alpha_mask.cend());
  target->mask_width = raster_size.width();
  target->mask_height = raster_size.height();
  target->mask_inverted = layer.value(QStringLiteral("maskInverted")).toBool();
  return true;
}

void populate_photo_layer_selection_mask(
    photo_editor::pipeline::PhotoLayer* target,
    const QVariantMap& layer,
    const QVariantList& masks,
    MaskRasterPolicy raster_policy,
    const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline) {
  if (target == nullptr || !layer.value(QStringLiteral("selectionScoped")).toBool()) {
    return;
  }

  const auto mask_set_ids = selection_mask_set_ids_for_layer(layer);
  const auto mask_set_names = selection_mask_set_names_for_layer(layer);
  if (mask_set_ids.isEmpty() && mask_set_names.isEmpty()) {
    return;
  }

  const auto raster_size = selection_layer_mask_raster_size(masks, layer, output_size);
  if (!raster_size.isValid() || raster_size.width() <= 0 || raster_size.height() <= 0) {
    return;
  }

  const auto mask_sets = pipeline_local_mask_sets(masks, raster_policy, raster_size);
  const auto ordered_count = std::max(mask_set_ids.size(), mask_set_names.size());
  for (auto index = 0; index < ordered_count; ++index) {
    const auto target_id = index < mask_set_ids.size() ? mask_set_ids.at(index) : QString();
    const auto target_name = index < mask_set_names.size() ? mask_set_names.at(index) : QString();
    for (const auto& set : mask_sets) {
      const auto set_id = QString::fromStdString(set.id).simplified();
      const auto set_name = QString::fromStdString(set.name).simplified();
      const auto matches =
          (!target_id.isEmpty() && set_id == target_id) ||
          (target_id.isEmpty() && !target_name.isEmpty() && set_name == target_name);
      if (matches &&
          populate_photo_layer_selection_mask_from_stored_source(target, layer, set, raster_size)) {
        return;
      }
      if (!matches) {
        continue;
      }
      const auto alpha = photo_editor::pipeline::rasterize_local_mask_set_alpha(
          set,
          raster_size.width(),
          raster_size.height(),
          source_pixels,
          color_pipeline);
      if (alpha.size() !=
          static_cast<std::size_t>(raster_size.width()) *
              static_cast<std::size_t>(raster_size.height())) {
        continue;
      }
      target->mask_data = alpha;
      target->mask_width = raster_size.width();
      target->mask_height = raster_size.height();
      target->mask_inverted = layer.value(QStringLiteral("maskInverted")).toBool();
      return;
    }
  }
}

void populate_photo_layer_pixels(photo_editor::pipeline::PhotoLayer* target, const QVariantMap& layer) {
  if (target == nullptr) {
    return;
  }
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  const auto pixel_data = layer.value(QStringLiteral("pixelData")).toByteArray();
  const auto pixel_width = layer.value(QStringLiteral("pixelWidth")).toInt();
  const auto pixel_height = layer.value(QStringLiteral("pixelHeight")).toInt();
  QImage image;
  if (!pixel_data.isEmpty() && pixel_width > 0 && pixel_height > 0) {
    image = QImage::fromData(pixel_data, "PNG");
    if (image.isNull() || image.width() != pixel_width || image.height() != pixel_height) {
      return;
    }
  } else if (kind == QStringLiteral("text")) {
    image = rasterized_text_layer_image(layer);
  } else if (kind == QStringLiteral("source")) {
    const auto source_path = layer.value(QStringLiteral("sourcePath")).toString();
    if (source_path.isEmpty()) {
      return;
    }
    image = QImage(source_path);
    if (image.isNull()) {
      return;
    }
    const auto target_width = layer.value(QStringLiteral("pixelWidth")).toInt();
    const auto target_height = layer.value(QStringLiteral("pixelHeight")).toInt();
    if (target_width > 0 && target_height > 0 &&
        (image.width() != target_width || image.height() != target_height)) {
      image = image.scaled(target_width, target_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
  } else {
    return;
  }
  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_target_color_space());
  }
  const auto working = working_image_from_qimage(image);
  if (working.isNull()) {
    return;
  }
  target->pixels = working.pixels;
  target->pixel_width = working.width;
  target->pixel_height = working.height;
  target->x = layer.value(QStringLiteral("x")).toInt();
  target->y = layer.value(QStringLiteral("y")).toInt();
  target->transform_scale = qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
  target->transform_rotation = qBound(-3600.0, layer.value(QStringLiteral("rotation")).toDouble(), 3600.0);
}

std::vector<photo_editor::pipeline::PhotoLayer> pipeline_photo_layers(
    const QVariantList& layers,
    const QVariantList& masks,
    MaskRasterPolicy raster_policy,
    const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline) {
  std::vector<photo_editor::pipeline::PhotoLayer> photo_layers;
  photo_layers.reserve(static_cast<std::size_t>(layers.size()));
  auto solo_active = false;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (kind != QStringLiteral("group") && layer.value(QStringLiteral("solo")).toBool()) {
      solo_active = true;
      break;
    }
  }
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (solo_active && kind != QStringLiteral("group") && !layer.value(QStringLiteral("solo")).toBool()) {
      continue;
    }
    auto photo_layer = photo_editor::pipeline::PhotoLayer{
        .id = layer.value(QStringLiteral("id")).toString().toStdString(),
        .name = layer.value(QStringLiteral("name")).toString().toStdString(),
        .kind = (kind.isEmpty() ? QStringLiteral("adjustment") : kind).toStdString(),
        .blend_mode =
            normalized_mask_blend_mode(layer.value(QStringLiteral("blendMode")).toString()).toStdString(),
        .enabled = map_bool_value(layer, QStringLiteral("enabled"), true),
        .opacity = qBound(0.0, layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 1.0),
        .clipping_mask = layer.value(QStringLiteral("clippingMask")).toBool(),
        .exposure = layer_adjustment_value(layer, QStringLiteral("exposure")),
        .contrast = layer_adjustment_value(layer, QStringLiteral("contrast")),
        .highlights = layer_adjustment_value(layer, QStringLiteral("highlights")),
        .shadows = layer_adjustment_value(layer, QStringLiteral("shadows")),
        .whites = layer_adjustment_value(layer, QStringLiteral("whites")),
        .blacks = layer_adjustment_value(layer, QStringLiteral("blacks")),
        .brightness = layer_adjustment_value(layer, QStringLiteral("brightness")),
        .filmic_shoulder = layer_adjustment_value(layer, QStringLiteral("filmicShoulder")),
        .filmic_toe = layer_adjustment_value(layer, QStringLiteral("filmicToe")),
        .adaptive_contrast = layer_adjustment_value(layer, QStringLiteral("adaptiveContrast")),
        .levels_black_point = layer_adjustment_value(layer, QStringLiteral("levelsBlackPoint")),
        .levels_gamma = layer_adjustment_value(layer, QStringLiteral("levelsGamma")),
        .levels_white_point = layer_adjustment_value(layer, QStringLiteral("levelsWhitePoint")),
        .zone_deep_shadows = layer_adjustment_value(layer, QStringLiteral("zoneDeepShadows")),
        .zone_shadows = layer_adjustment_value(layer, QStringLiteral("zoneShadows")),
        .zone_midtones = layer_adjustment_value(layer, QStringLiteral("zoneMidtones")),
        .zone_lights = layer_adjustment_value(layer, QStringLiteral("zoneLights")),
        .zone_highlights = layer_adjustment_value(layer, QStringLiteral("zoneHighlights")),
        .temperature = layer_adjustment_value(layer, QStringLiteral("temperature")),
        .tint = layer_adjustment_value(layer, QStringLiteral("tint")),
        .texture = layer_adjustment_value(layer, QStringLiteral("texture")),
        .clarity = layer_adjustment_value(layer, QStringLiteral("clarity")),
        .dehaze = layer_adjustment_value(layer, QStringLiteral("dehaze")),
        .sharpening = layer_adjustment_value(layer, QStringLiteral("sharpening")),
        .sharpen_radius = layer.contains(QStringLiteral("sharpenRadius")) ||
                                  layer.value(QStringLiteral("adjustments")).toMap().contains(
                                      QStringLiteral("sharpenRadius"))
                              ? layer_adjustment_value(layer, QStringLiteral("sharpenRadius"))
                              : 1.0,
        .sharpen_detail = layer_adjustment_value(layer, QStringLiteral("sharpenDetail")),
        .sharpen_masking = layer_adjustment_value(layer, QStringLiteral("sharpenMasking")),
        .luminance_nr = layer_adjustment_value(layer, QStringLiteral("luminanceNr")),
        .color_nr = layer_adjustment_value(layer, QStringLiteral("colorNr")),
        .lens_distortion = layer_adjustment_value(layer, QStringLiteral("lensDistortion")),
        .lens_vignette = layer_adjustment_value(layer, QStringLiteral("lensVignette")),
        .chromatic_aberration =
            layer_adjustment_value(layer, QStringLiteral("chromaticAberration")),
        .perspective_vertical = layer_adjustment_value(layer, QStringLiteral("perspectiveVertical")),
        .perspective_horizontal =
            layer_adjustment_value(layer, QStringLiteral("perspectiveHorizontal")),
        .global_transform_rotation =
            layer_adjustment_value(layer, QStringLiteral("transformRotation")),
        .global_transform_scale = layer.contains(QStringLiteral("transformScale")) ||
                                          layer.value(QStringLiteral("adjustments")).toMap().contains(
                                              QStringLiteral("transformScale"))
                                      ? layer_adjustment_value(layer, QStringLiteral("transformScale"))
                                      : 1.0,
        .transform_skew_x = layer_adjustment_value(layer, QStringLiteral("transformSkewX")),
        .transform_skew_y = layer_adjustment_value(layer, QStringLiteral("transformSkewY")),
        .liquify_center_x = layer.contains(QStringLiteral("liquifyCenterX")) ||
                                    layer.value(QStringLiteral("adjustments")).toMap().contains(
                                        QStringLiteral("liquifyCenterX"))
                                ? layer_adjustment_value(layer, QStringLiteral("liquifyCenterX"))
                                : 0.5,
        .liquify_center_y = layer.contains(QStringLiteral("liquifyCenterY")) ||
                                    layer.value(QStringLiteral("adjustments")).toMap().contains(
                                        QStringLiteral("liquifyCenterY"))
                                ? layer_adjustment_value(layer, QStringLiteral("liquifyCenterY"))
                                : 0.5,
        .liquify_radius = layer.contains(QStringLiteral("liquifyRadius")) ||
                                  layer.value(QStringLiteral("adjustments")).toMap().contains(
                                      QStringLiteral("liquifyRadius"))
                              ? layer_adjustment_value(layer, QStringLiteral("liquifyRadius"))
                              : 0.25,
        .liquify_strength = layer_adjustment_value(layer, QStringLiteral("liquifyStrength")),
        .content_aware_crop_fill =
            layer.value(QStringLiteral("adjustments")).toMap().contains(
                QStringLiteral("contentAwareCropFill"))
                ? layer.value(QStringLiteral("adjustments")).toMap()
                      .value(QStringLiteral("contentAwareCropFill"))
                      .toBool()
                : layer.value(QStringLiteral("contentAwareCropFill")).toBool(),
        .vibrance = layer_adjustment_value(layer, QStringLiteral("vibrance")),
        .saturation = layer_adjustment_value(layer, QStringLiteral("saturation")),
        .hue_shift = layer_adjustment_value(layer, QStringLiteral("hueShift")),
        .saturation_shift = layer_adjustment_value(layer, QStringLiteral("saturationShift")),
        .colorize_hue = layer_adjustment_value(layer, QStringLiteral("colorizeHue")),
        .colorize_strength = layer_adjustment_value(layer, QStringLiteral("colorizeStrength")),
        .curve_shadows = layer_adjustment_value(layer, QStringLiteral("curveShadows")),
        .curve_darks = layer_adjustment_value(layer, QStringLiteral("curveDarks")),
        .curve_lights = layer_adjustment_value(layer, QStringLiteral("curveLights")),
        .curve_highlights = layer_adjustment_value(layer, QStringLiteral("curveHighlights")),
        .master_curve_points =
            pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("masterCurvePoints"))),
        .luma_curve_points =
            pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("lumaCurvePoints"))),
        .red_curve_points =
            pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("redCurvePoints"))),
        .green_curve_points =
            pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("greenCurvePoints"))),
        .blue_curve_points =
            pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("blueCurvePoints"))),
        .color_mixer_model = layer_nested_adjustment_string(
            layer,
            QStringLiteral("colorMixer"),
            QStringLiteral("model"),
            QStringLiteral("colorMixerModel"),
            QStringLiteral("OKLab")).toStdString(),
        .color_mixer_channel = layer_nested_adjustment_string(
            layer,
            QStringLiteral("colorMixer"),
            QStringLiteral("channel"),
            QStringLiteral("hslChannel"),
            QStringLiteral("Reds")).toStdString(),
        .hue_adjustment = layer_nested_adjustment_value(
            layer, QStringLiteral("colorMixer"), QStringLiteral("hue"), QStringLiteral("hueAdjustment")),
        .saturation_adjustment = layer_nested_adjustment_value(
            layer, QStringLiteral("colorMixer"), QStringLiteral("saturation"), QStringLiteral("saturationAdjustment")),
        .luminance_adjustment = layer_nested_adjustment_value(
            layer, QStringLiteral("colorMixer"), QStringLiteral("luminance"), QStringLiteral("luminanceAdjustment")),
        .color_mixer_channels = layer_color_mixer_channels(layer),
        .rgb_channel_mixer = layer_rgb_channel_mixer(layer),
        .black_and_white_enabled = layer_color_mixer_child(
            layer, QStringLiteral("blackAndWhite")).value(QStringLiteral("enabled")).toBool(),
        .black_and_white_mixer = layer_black_and_white_mixer(layer),
        .color_replacement_enabled = layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")).value(QStringLiteral("enabled")).toBool(),
        .color_replacement_source_hue = layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")).value(QStringLiteral("sourceHue")).toDouble(),
        .color_replacement_target_hue = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("targetHue"), 0.33),
        .color_replacement_range = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("range"), 35.0),
        .color_replacement_softness = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("softness"), 100.0),
        .color_replacement_strength = layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")).value(QStringLiteral("strength")).toDouble(),
        .color_replacement_preserve_luminance = layer_object_bool(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("preserveLuminance"), true),
        .color_replacement_preserve_saturation = layer_object_bool(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("preserveSaturation"), true),
        .skin_tone_protection_enabled = layer_color_mixer_child(
            layer, QStringLiteral("skinToneProtection")).value(QStringLiteral("enabled")).toBool(),
        .skin_tone_line_hue = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("skinToneProtection")), QStringLiteral("lineHue"), 0.08),
        .skin_tone_protection_range = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("skinToneProtection")), QStringLiteral("range"), 30.0),
        .skin_tone_protection_strength = layer_color_mixer_child(
            layer, QStringLiteral("skinToneProtection")).value(QStringLiteral("strength")).toDouble(),
        .color_harmony_enabled = layer_color_mixer_child(
            layer, QStringLiteral("colorHarmony")).value(QStringLiteral("enabled")).toBool(),
        .color_harmony_mode = layer_object_string(layer_color_mixer_child(
            layer, QStringLiteral("colorHarmony")), QStringLiteral("mode"), QStringLiteral("Complementary")).toStdString(),
        .color_harmony_base_hue = layer_color_mixer_child(
            layer, QStringLiteral("colorHarmony")).value(QStringLiteral("baseHue")).toDouble(),
        .color_harmony_strength = layer_color_mixer_child(
            layer, QStringLiteral("colorHarmony")).value(QStringLiteral("strength")).toDouble(),
        .grading_shadows_hue = layer_color_grading_child(
            layer, QStringLiteral("shadows")).value(QStringLiteral("hue")).toDouble(),
        .grading_shadows_saturation = layer_color_grading_child(
            layer, QStringLiteral("shadows")).value(QStringLiteral("saturation")).toDouble(),
        .grading_shadows_luminance = layer_color_grading_child(
            layer, QStringLiteral("shadows")).value(QStringLiteral("luminance")).toDouble(),
        .grading_midtones_hue = layer_color_grading_child(
            layer, QStringLiteral("midtones")).value(QStringLiteral("hue")).toDouble(),
        .grading_midtones_saturation = layer_color_grading_child(
            layer, QStringLiteral("midtones")).value(QStringLiteral("saturation")).toDouble(),
        .grading_midtones_luminance = layer_color_grading_child(
            layer, QStringLiteral("midtones")).value(QStringLiteral("luminance")).toDouble(),
        .grading_highlights_hue = layer_color_grading_child(
            layer, QStringLiteral("highlights")).value(QStringLiteral("hue")).toDouble(),
        .grading_highlights_saturation = layer_color_grading_child(
            layer, QStringLiteral("highlights")).value(QStringLiteral("saturation")).toDouble(),
        .grading_highlights_luminance = layer_color_grading_child(
            layer, QStringLiteral("highlights")).value(QStringLiteral("luminance")).toDouble(),
        .grading_amount = layer_object_double(layer_adjustment_object(
            layer, QStringLiteral("colorGrading")), QStringLiteral("amount"), 100.0),
        .grading_balance = layer_adjustment_object(
            layer, QStringLiteral("colorGrading")).value(QStringLiteral("balance")).toDouble(),
        .lut_stack = layer_lut_stack(layer),
        .fill_color = layer_fill_color(layer),
    };
    populate_photo_layer_mask(&photo_layer, layer);
    if (photo_layer.mask_data.empty()) {
      populate_photo_layer_selection_mask(
          &photo_layer,
          layer,
          masks,
          raster_policy,
          output_size,
          source_pixels,
          color_pipeline);
    }
    populate_photo_layer_pixels(&photo_layer, layer);
    photo_layers.push_back(std::move(photo_layer));
  }
  return photo_layers;
}

photo_editor::pipeline::ColorPipelineConfig color_pipeline_config_for_state(const RenderEditState& state) {
  const auto preview_output_name =
      state.preview_color_space.trimmed().isEmpty() ? state.output_color_space : state.preview_color_space;
  return photo_editor::pipeline::ColorPipelineConfig{
      .input_profile =
          photo_editor::pipeline::input_color_profile_from_name(state.input_color_profile.toStdString()),
      .working_profile =
          photo_editor::pipeline::working_color_profile_from_name(state.working_color_profile.toStdString()),
      .preview_output_profile =
          photo_editor::pipeline::output_color_profile_from_name(preview_output_name.toStdString()),
      .export_output_profile =
          photo_editor::pipeline::output_color_profile_from_name(state.output_color_space.toStdString()),
      .camera_profile = {},
      .input_transform_recipe = {},
      .render_intent =
          photo_editor::pipeline::render_intent_from_name(state.render_intent.toStdString()),
      .output_gamut_mapping_enabled = state.output_gamut_mapping_enabled,
      .output_gamut_mapping_policy =
          photo_editor::pipeline::gamut_mapping_policy_from_name(
              state.output_gamut_mapping_policy.toStdString()),
      .output_headroom = state.output_headroom,
      .black_point_compensation_enabled = state.black_point_compensation_enabled,
      .source_black_point = state.source_black_point,
      .output_black_point = state.output_black_point,
  };
}

photo_editor::pipeline::ColorPipelineConfig color_pipeline_config_for_state(
    const RenderEditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata) {
  auto config = color_pipeline_config_for_state(state);
  if (metadata != nullptr && metadata->valid) {
    config.camera_profile = photo_editor::pipeline::resolve_camera_profile(
        *metadata,
        config.input_profile);
    config.input_transform_recipe = photo_editor::pipeline::resolve_input_transform_recipe(
        config.input_profile,
        config.camera_profile,
        *metadata);
  }
  return config;
}

photo_editor::pipeline::ColorAdjustmentState to_pipeline_state(
    const RenderEditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    MaskRasterPolicy raster_policy = MaskRasterPolicy::UseStoredRaster,
    const QSize& output_size = {},
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels = {}) {
  const auto raw_input_transform_enabled =
      metadata != nullptr && metadata->valid && metadata->is_raw && state.raw_input_transform_enabled;
  const auto color_pipeline_config = color_pipeline_config_for_state(state, metadata);
  return photo_editor::pipeline::ColorAdjustmentState{
      .input_transform =
          photo_editor::pipeline::InputTransformSettings{
              .input_profile_name = normalized_input_color_profile_name(state.input_color_profile).toStdString(),
              .exposure = state.exposure,
              .temperature = state.temperature,
              .tint = state.tint,
              .raw_input_transform_enabled = raw_input_transform_enabled,
              .highlight_recovery_enabled = raw_input_transform_enabled && state.raw_highlight_recovery,
              .highlight_reconstruction_mode =
                  normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode).toStdString(),
              .chromatic_adaptation_method =
                  normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method).toStdString(),
              .gamut_mapping_enabled = raw_input_transform_enabled && state.raw_gamut_mapping,
              .lens_correction_enabled = raw_input_transform_enabled && state.raw_lens_correction,
              .raw_white_balance_override_enabled =
                  raw_input_transform_enabled && state.raw_white_balance_override,
              .camera_matching_look =
                  raw_input_transform_enabled
                      ? normalized_camera_matching_look_name(state.raw_camera_matching_look).toStdString()
                      : std::string("Standard"),
              .raw_white_balance_temperature = state.raw_white_balance_temperature,
              .raw_white_balance_tint = state.raw_white_balance_tint,
              .raw_black_level = raw_input_transform_enabled ? state.raw_black_level : 0.0,
              .raw_white_level = raw_input_transform_enabled ? state.raw_white_level : 1.0,
              .calibration_hue = raw_input_transform_enabled ? state.raw_calibration_hue : 0.0,
              .calibration_saturation =
                  raw_input_transform_enabled ? state.raw_calibration_saturation : 0.0,
              .red_primary_hue = raw_input_transform_enabled ? state.raw_red_primary_hue : 0.0,
              .red_primary_saturation =
                  raw_input_transform_enabled ? state.raw_red_primary_saturation : 0.0,
              .green_primary_hue = raw_input_transform_enabled ? state.raw_green_primary_hue : 0.0,
              .green_primary_saturation =
                  raw_input_transform_enabled ? state.raw_green_primary_saturation : 0.0,
              .blue_primary_hue = raw_input_transform_enabled ? state.raw_blue_primary_hue : 0.0,
              .blue_primary_saturation =
                  raw_input_transform_enabled ? state.raw_blue_primary_saturation : 0.0,
          },
      .color_pipeline = color_pipeline_config,
      .exposure = state.exposure,
      .contrast = state.contrast,
      .highlights = state.highlights,
      .shadows = state.shadows,
      .whites = state.whites,
      .blacks = state.blacks,
      .brightness = state.brightness,
      .hdr_local_tone_mapping = state.hdr_local_tone_mapping,
      .filmic_shoulder = state.filmic_shoulder,
      .filmic_toe = state.filmic_toe,
      .adaptive_contrast = state.adaptive_contrast,
      .levels_black_point = state.levels_black_point,
      .levels_gamma = state.levels_gamma,
      .levels_white_point = state.levels_white_point,
      .zone_deep_shadows = state.zone_deep_shadows,
      .zone_shadows = state.zone_shadows,
      .zone_midtones = state.zone_midtones,
      .zone_lights = state.zone_lights,
      .zone_highlights = state.zone_highlights,
      .temperature = state.temperature,
      .tint = state.tint,
      .texture = state.texture,
      .clarity = state.clarity,
      .dehaze = state.dehaze,
      .sharpening = state.sharpening,
      .sharpen_radius = state.sharpen_radius,
      .sharpen_detail = state.sharpen_detail,
      .sharpen_masking = state.sharpen_masking,
      .luminance_nr = state.luminance_nr,
      .color_nr = state.color_nr,
      .lens_distortion = state.lens_distortion,
      .lens_vignette = state.lens_vignette,
      .chromatic_aberration = state.chromatic_aberration,
      .perspective_vertical = state.perspective_vertical,
      .perspective_horizontal = state.perspective_horizontal,
      .transform_rotation = state.transform_rotation,
      .transform_scale = state.transform_scale,
      .transform_skew_x = state.transform_skew_x,
      .transform_skew_y = state.transform_skew_y,
      .liquify_center_x = state.liquify_center_x,
      .liquify_center_y = state.liquify_center_y,
      .liquify_radius = state.liquify_radius,
      .liquify_strength = state.liquify_strength,
      .content_aware_crop_fill = state.content_aware_crop_fill,
      .vibrance = state.vibrance,
      .saturation = state.saturation,
      .curve_shadows = state.curve_shadows,
      .curve_darks = state.curve_darks,
      .curve_lights = state.curve_lights,
      .curve_highlights = state.curve_highlights,
      .master_curve_points = pipeline_curve_points(state.master_curve_points),
      .luma_curve_points = pipeline_curve_points(state.luma_curve_points),
      .red_curve_points = pipeline_curve_points(state.red_curve_points),
      .green_curve_points = pipeline_curve_points(state.green_curve_points),
      .blue_curve_points = pipeline_curve_points(state.blue_curve_points),
      .color_mixer_model = state.color_mixer_model.toStdString(),
      .color_mixer_channel = state.hsl_channel.toStdString(),
      .hue_adjustment = state.hue_adjustment,
      .saturation_adjustment = state.saturation_adjustment,
      .luminance_adjustment = state.luminance_adjustment,
      .color_mixer_channels = [&]() {
        std::array<photo_editor::pipeline::ColorMixerChannelAdjustment,
                   photo_editor::pipeline::kColorMixerChannelCount> channels{};
        for (std::size_t i = 0; i < channels.size(); ++i) {
          channels[i].hue = state.mixer_channels[i].hue;
          channels[i].saturation = state.mixer_channels[i].saturation;
          channels[i].luminance = state.mixer_channels[i].luminance;
          channels[i].selective_cyan = state.mixer_channels[i].selective_cyan;
          channels[i].selective_magenta = state.mixer_channels[i].selective_magenta;
          channels[i].selective_yellow = state.mixer_channels[i].selective_yellow;
          channels[i].selective_black = state.mixer_channels[i].selective_black;
        }
        return channels;
      }(),
      .rgb_channel_mixer = {
          .red_from_red = state.rgb_channel_mixer.red_from_red,
          .red_from_green = state.rgb_channel_mixer.red_from_green,
          .red_from_blue = state.rgb_channel_mixer.red_from_blue,
          .green_from_red = state.rgb_channel_mixer.green_from_red,
          .green_from_green = state.rgb_channel_mixer.green_from_green,
          .green_from_blue = state.rgb_channel_mixer.green_from_blue,
          .blue_from_red = state.rgb_channel_mixer.blue_from_red,
          .blue_from_green = state.rgb_channel_mixer.blue_from_green,
          .blue_from_blue = state.rgb_channel_mixer.blue_from_blue,
      },
      .black_and_white_enabled = state.black_and_white_enabled,
      .black_and_white_mixer = state.black_and_white_mixer,
      .color_replacement_enabled = state.color_replacement_enabled,
      .color_replacement_source_hue = state.color_replacement_source_hue,
      .color_replacement_target_hue = state.color_replacement_target_hue,
      .color_replacement_range = state.color_replacement_range,
      .color_replacement_softness = state.color_replacement_softness,
      .color_replacement_strength = state.color_replacement_strength,
      .color_replacement_preserve_luminance = state.color_replacement_preserve_luminance,
      .color_replacement_preserve_saturation = state.color_replacement_preserve_saturation,
      .skin_tone_protection_enabled = state.skin_tone_protection_enabled,
      .skin_tone_line_hue = state.skin_tone_line_hue,
      .skin_tone_protection_range = state.skin_tone_protection_range,
      .skin_tone_protection_strength = state.skin_tone_protection_strength,
      .color_harmony_enabled = state.color_harmony_enabled,
      .color_harmony_mode = state.color_harmony_mode.toStdString(),
      .color_harmony_base_hue = state.color_harmony_base_hue,
      .color_harmony_strength = state.color_harmony_strength,
      .grading_shadows_hue = state.grading_shadows_hue,
      .grading_shadows_saturation = state.grading_shadows_saturation,
      .grading_shadows_luminance = state.grading_shadows_luminance,
      .grading_midtones_hue = state.grading_midtones_hue,
      .grading_midtones_saturation = state.grading_midtones_saturation,
      .grading_midtones_luminance = state.grading_midtones_luminance,
      .grading_highlights_hue = state.grading_highlights_hue,
      .grading_highlights_saturation = state.grading_highlights_saturation,
      .grading_highlights_luminance = state.grading_highlights_luminance,
      .grading_amount = state.grading_amount,
      .grading_balance = state.grading_balance,
      .linear_mask_enabled = state.linear_mask_enabled,
      .linear_mask_exposure = state.linear_mask_exposure,
      .linear_mask_temperature = state.linear_mask_temperature,
      .linear_mask_feather = state.linear_mask_feather,
      .linear_mask_position = state.linear_mask_position,
      .linear_mask_rotation = state.linear_mask_rotation,
      .radial_mask_enabled = state.radial_mask_enabled,
      .radial_mask_exposure = state.radial_mask_exposure,
      .radial_mask_temperature = state.radial_mask_temperature,
      .radial_mask_feather = state.radial_mask_feather,
      .radial_mask_center_x = state.radial_mask_center_x,
      .radial_mask_center_y = state.radial_mask_center_y,
      .radial_mask_radius_x = state.radial_mask_radius_x,
      .radial_mask_radius_y = state.radial_mask_radius_y,
      .radial_mask_rotation = state.radial_mask_rotation,
      .local_mask_sets = pipeline_local_mask_sets(state.mask_stack, raster_policy, output_size),
      .retouch_masks = pipeline_retouch_masks(state.mask_stack, raster_policy, output_size),
      .photo_layers =
          pipeline_photo_layers(
              state.layers,
              state.mask_stack,
              raster_policy,
              output_size,
              source_pixels,
              color_pipeline_config),
  };
}

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
photo_editor::pipeline::metal::MetalPipelineBackend& metal_backend() {
  static photo_editor::pipeline::metal::MetalPipelineBackend backend;
  return backend;
}
#endif

}  // namespace

QString render_backend_unavailable_reason(RenderBackendPreference backend_preference) {
  switch (backend_preference) {
    case RenderBackendPreference::Auto:
    case RenderBackendPreference::CpuOnly:
      return {};
    case RenderBackendPreference::GpuOnly:
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
      return metal_backend().available()
                 ? QString{}
                 : QString::fromStdString(metal_backend().unavailable_reason());
#else
      return QStringLiteral("metal_not_compiled");
#endif
  }
  return QStringLiteral("unknown_backend_preference");
}

bool preview_supported(const QString& file_path) {
  const QFileInfo file_info(file_path);
  const auto suffix = file_info.suffix().toLower().toUtf8();
  return QImageReader::supportedImageFormats().contains(suffix) || raw_image_supported(file_path) ||
         native_image_supported(file_path);
}

bool render_backend_available(RenderBackendPreference backend_preference) {
  switch (backend_preference) {
    case RenderBackendPreference::Auto:
    case RenderBackendPreference::CpuOnly:
      return true;
    case RenderBackendPreference::GpuOnly:
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
      return metal_backend().available();
#else
      return false;
#endif
  }
  return false;
}

GpuPipelineSupport evaluate_gpu_pipeline_support(
    const QImage& image,
    const RenderEditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderMaskOptions mask_options,
    bool assume_metal_available) {
  if (image.isNull()) {
    return GpuPipelineSupport{
        .supported = false,
        .metal_aware_cpu_engine = false,
        .fallback_reason = QStringLiteral("empty_source"),
    };
  }

  const auto working = working_image_from_qimage(image, state.working_color_profile);
  if (working.isNull()) {
    return GpuPipelineSupport{
        .supported = false,
        .metal_aware_cpu_engine = false,
        .fallback_reason = QStringLiteral("linearization_failed"),
    };
  }

  const auto raster_policy = mask_options.rebuild_editable_masks_at_output_resolution
                                 ? MaskRasterPolicy::RebuildEditableAtOutputResolution
                                 : MaskRasterPolicy::UseStoredRaster;
  const auto pipeline_state =
      to_pipeline_state(
          state,
          metadata,
          raster_policy,
          QSize(working.width, working.height),
          working.pixels);
  const auto reason = gpu_fallback_reason(
      state,
      pipeline_state,
      working.pixels,
      assume_metal_available);
  return GpuPipelineSupport{
      .supported = reason.isEmpty(),
      .metal_aware_cpu_engine = reason == QStringLiteral("local_mask_sets"),
      .fallback_reason = reason,
  };
}

bool decode_source_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    const RenderEditState& state,
    photo_editor::pipeline::DecodedImageMetadata* metadata) {
  if (image == nullptr || !preview_supported(file_path)) {
    return false;
  }
  if (metadata != nullptr) {
    *metadata = {};
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }

  const auto native_supported = native_image_supported(file_path);
  const auto qt_supported =
      !native_supported &&
      QImageReader::supportedImageFormats().contains(source_info.suffix().toLower().toUtf8());
  const auto should_cache = max_edge >= 0;
  auto cache_key = should_cache ? decoded_image_cache_key(source_info, max_edge) : QString{};
  if (should_cache && !qt_supported) {
    cache_key += QStringLiteral("|raw|") + raw_decode_settings_key(state);
  }
  if (should_cache) {
    if (find_decoded_image_cache_entry(cache_key, image)) {
      if (metadata != nullptr && !qt_supported) {
        (void)read_raw_decoded_metadata(file_path, metadata, raw_decode_settings_for_state(state));
      }
      return !image->isNull();
    }
  }

  if (native_supported) {
    auto decoded = QImage{};
    if (!decode_native_linear_image(file_path, &decoded, max_edge)) {
      return false;
    }

    *image = normalize_preview_color_space(std::move(decoded));
    if (should_cache && !image->isNull()) {
      insert_decoded_image_cache_entry(cache_key, *image);
    }
    return !image->isNull();
  }

  if (!qt_supported) {
    auto decoded = QImage{};
    if (!decode_raw_linear_image(
            file_path,
            &decoded,
            max_edge,
            raw_decode_settings_for_state(state),
            metadata)) {
      return false;
    }

    *image = normalize_preview_color_space(std::move(decoded));
    if (should_cache && !image->isNull()) {
      insert_decoded_image_cache_entry(cache_key, *image);
    }
    return !image->isNull();
  }

  QImageReader reader(file_path);
  reader.setAutoTransform(true);
  if (!reader.canRead()) {
    return false;
  }

  if (max_edge > 0) {
    const auto source_size = reader.size();
    if (source_size.isValid()) {
      reader.setScaledSize(source_size.scaled(max_edge, max_edge, Qt::KeepAspectRatio));
    }
  }

  *image = normalize_preview_color_space(reader.read());
  if (should_cache && !image->isNull()) {
    insert_decoded_image_cache_entry(cache_key, *image);
  }

  return !image->isNull();
}

QSize source_image_size(const QString& file_path) {
  if (!preview_supported(file_path)) {
    return {};
  }

  if (native_image_supported(file_path)) {
    return native_image_size(file_path);
  }

  QImageReader reader(file_path);
  if (reader.canRead()) {
    return reader.size();
  }

  return raw_image_size(file_path);
}

double crop_aspect_ratio(const QString& preset) {
  auto normalized = preset.trimmed().toLower();
  normalized.replace(QLatin1Char(':'), QLatin1Char('x'));
  normalized.remove(QLatin1Char(' '));
  if (normalized == QStringLiteral("square") || normalized == QStringLiteral("1x1")) {
    return 1.0;
  }
  if (normalized == QStringLiteral("a3") || normalized == QStringLiteral("a4") ||
      normalized == QStringLiteral("a5")) {
    return 210.0 / 297.0;
  }
  if (normalized == QStringLiteral("a3landscape") ||
      normalized == QStringLiteral("a4landscape") ||
      normalized == QStringLiteral("a5landscape")) {
    return 297.0 / 210.0;
  }
  const auto parts = normalized.split(QLatin1Char('x'), Qt::SkipEmptyParts);
  if (parts.size() == 2) {
    bool width_ok = false;
    bool height_ok = false;
    auto width_label = parts.at(0);
    auto height_label = parts.at(1);
    for (const auto& unit : {QStringLiteral("inches"), QStringLiteral("inch"), QStringLiteral("in"),
                             QStringLiteral("cm"), QStringLiteral("mm")}) {
      width_label.remove(unit);
      height_label.remove(unit);
    }
    const auto width = width_label.toDouble(&width_ok);
    const auto height = height_label.toDouble(&height_ok);
    if (width_ok && height_ok && width > 0.0 && height > 0.0) {
      return width / height;
    }
  }
  return 0.0;
}

bool has_explicit_crop_rect(const RenderEditState& state) {
  return state.crop_rect_width > 0.0 && state.crop_rect_height > 0.0;
}

QRectF normalized_crop_rect_for_state(const QSize& size, const RenderEditState& state) {
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
    return QRectF(0.0, 0.0, 1.0, 1.0);
  }

  if (has_explicit_crop_rect(state)) {
    const auto width = clamp_unit(state.crop_rect_width);
    const auto height = clamp_unit(state.crop_rect_height);
    const auto x = clamp_unit(state.crop_rect_x);
    const auto y = clamp_unit(state.crop_rect_y);
    return QRectF(
        std::min(x, std::max(0.0, 1.0 - width)),
        std::min(y, std::max(0.0, 1.0 - height)),
        width,
        height);
  }

  auto crop_width = size.width();
  auto crop_height = size.height();
  const auto aspect_ratio = crop_aspect_ratio(state.crop_preset);
  if (aspect_ratio > 0.0) {
    const auto current_ratio = static_cast<double>(size.width()) / static_cast<double>(size.height());
    if (current_ratio > aspect_ratio) {
      crop_width = static_cast<int>(qRound(static_cast<double>(size.height()) * aspect_ratio));
    } else {
      crop_height = static_cast<int>(qRound(static_cast<double>(size.width()) / aspect_ratio));
    }
  }

  crop_width = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_width) / state.crop_zoom)));
  crop_height = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_height) / state.crop_zoom)));
  const auto x = qMax(0, (size.width() - crop_width) / 2);
  const auto y = qMax(0, (size.height() - crop_height) / 2);
  return QRectF(
      static_cast<double>(x) / static_cast<double>(size.width()),
      static_cast<double>(y) / static_cast<double>(size.height()),
      static_cast<double>(crop_width) / static_cast<double>(size.width()),
      static_cast<double>(crop_height) / static_cast<double>(size.height()));
}

QRect crop_rect_for_image(const QSize& size, const RenderEditState& state) {
  if (!size.isValid()) {
    return {};
  }

  const auto normalized = normalized_crop_rect_for_state(size, state);
  const auto x = qBound(
      0, static_cast<int>(qRound(normalized.x() * static_cast<double>(size.width()))), size.width() - 1);
  const auto y = qBound(
      0, static_cast<int>(qRound(normalized.y() * static_cast<double>(size.height()))), size.height() - 1);
  const auto width = qBound(
      1, static_cast<int>(qRound(normalized.width() * static_cast<double>(size.width()))), size.width() - x);
  const auto height = qBound(
      1, static_cast<int>(qRound(normalized.height() * static_cast<double>(size.height()))), size.height() - y);
  return {x, y, width, height};
}

QImage apply_crop(const QImage& image, const RenderEditState& state) {
  return image.copy(crop_rect_for_image(image.size(), state));
}

void apply_color_adjustments(QImage* image, const RenderEditState& state) {
  if (image == nullptr) {
    return;
  }
  *image = render_output_image(
      *image,
      state,
      output_color_space_for_name(state.output_color_space),
      QImage::Format_ARGB32,
      nullptr,
      nullptr,
      RenderMaskOptions{.rebuild_editable_masks_at_output_resolution = true});
}

QImage render_output_image(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry,
    RenderMaskOptions mask_options,
    photo_editor::pipeline::RemovalQuality removal_quality) {
  return render_output_image_with_backend(
      image,
      state,
      target_color_space,
      output_format,
      RenderBackendPreference::Auto,
      metadata,
      telemetry,
      mask_options,
      removal_quality);
}

QImage render_output_image_with_backend(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    RenderBackendPreference backend_preference,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry,
    RenderMaskOptions mask_options,
    photo_editor::pipeline::RemovalQuality removal_quality) {
  if (telemetry != nullptr) {
    *telemetry = {};
  }
  if (image.isNull()) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = QStringLiteral("empty_source");
    }
    return {};
  }

  auto working = working_image_from_qimage(image, state.working_color_profile);
  if (working.isNull()) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = QStringLiteral("linearization_failed");
    }
    return {};
  }

  auto& pixels = working.pixels;
  const auto width = working.width;
  const auto height = working.height;

  const auto raster_policy = mask_options.rebuild_editable_masks_at_output_resolution
                                 ? MaskRasterPolicy::RebuildEditableAtOutputResolution
                                 : MaskRasterPolicy::UseStoredRaster;
  const auto pipeline_state =
      to_pipeline_state(state, metadata, raster_policy, QSize(width, height), pixels);
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = metal_backend();
  const auto fallback_reason = gpu_fallback_reason(state, pipeline_state, pixels, metal.available());
  const auto gpu_supported = fallback_reason.isEmpty();
  const auto can_use_metal_aware_engine =
      backend_preference != RenderBackendPreference::CpuOnly &&
      metal.available() &&
      fallback_reason == QStringLiteral("local_mask_sets");
  if (backend_preference == RenderBackendPreference::GpuOnly &&
      !gpu_supported &&
      !can_use_metal_aware_engine) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = fallback_reason;
    }
    return {};
  }
  if (backend_preference != RenderBackendPreference::CpuOnly && gpu_supported) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("metal");
      telemetry->fallback_reason.clear();
    }
    const auto lut = photo_editor::pipeline::build_color_lut(pipeline_state);
    metal.apply_masks(&pixels, width, height, pipeline_state);
    metal.apply_color_lut(&pixels, width, height, lut);
  } else {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("cpu");
      telemetry->fallback_reason =
          backend_preference == RenderBackendPreference::CpuOnly
              ? QStringLiteral("cpu_requested")
              : fallback_reason;
    }
    photo_editor::pipeline::ColorAdjustmentTelemetry engine_telemetry;
    photo_editor::pipeline::apply_color_adjustments(
        &pixels,
        width,
        height,
        pipeline_state,
        removal_quality,
        photo_editor::pipeline::CancellationToken{},
        &engine_telemetry);
    if (telemetry != nullptr &&
        telemetry->backend == QStringLiteral("cpu") &&
        engine_telemetry.gpu_command_buffer_count > 0 &&
        engine_telemetry.gpu_kernel_dispatch_count > 0) {
      telemetry->backend = QStringLiteral("metal");
      telemetry->fallback_reason.clear();
    }
  }
#else
  if (backend_preference == RenderBackendPreference::GpuOnly) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = QStringLiteral("metal_not_compiled");
    }
    return {};
  }
  if (telemetry != nullptr) {
    telemetry->backend = QStringLiteral("cpu");
    telemetry->fallback_reason =
        backend_preference == RenderBackendPreference::CpuOnly
            ? QStringLiteral("cpu_requested")
            : QStringLiteral("metal_not_compiled");
  }
  photo_editor::pipeline::apply_color_adjustments(
      &pixels, width, height, pipeline_state, removal_quality);
#endif

  auto output_image = qimage_from_working_image(
      working,
      target_color_space.isValid() ? target_color_space : preview_target_color_space(),
      output_format,
      state.working_color_profile);
  if (output_image.isNull()) {
    return {};
  }
  if (target_color_space.isValid()) {
    return output_image;
  }
  apply_output_color_space_transform(&output_image, state.output_color_space);
  return output_image;
}

}  // namespace photo_editor::desktop

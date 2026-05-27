#include "photo_render_utils.h"

#include "app_bootstrap.h"
#include "decoded_image_cache.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "raw_image_decoder.h"
#include "working_image.h"

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <QColor>
#include <QColorSpace>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QPainter>

#include <algorithm>
#include <cstring>

namespace photo_editor::desktop {

namespace {

QImage normalize_preview_color_space(QImage image);
QColorSpace preview_target_color_space() {
  return preview_color_space();
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
  if (!pipeline_state.local_masks.empty()) {
    return QStringLiteral("legacy_local_masks");
  }
  if (has_extended_range_pixels(pixels)) {
    return QStringLiteral("extended_range_pixels");
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

double map_double_value(const QVariantMap& map, const QString& key, double fallback) {
  return map.contains(key) ? map.value(key).toDouble() : fallback;
}

bool map_bool_value(const QVariantMap& map, const QString& key, bool fallback) {
  return map.contains(key) ? map.value(key).toBool() : fallback;
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
    patch_image.setColorSpace(preview_target_color_space());
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
}

photo_editor::pipeline::LocalMask pipeline_local_mask(const QVariantMap& mask) {
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
  local_mask.selection_invert = map_bool_value(mask, QStringLiteral("selectionInvert"), local_mask.invert);
  local_mask.selection_expand = map_double_value(mask, QStringLiteral("selectionExpand"), 0.0);
  local_mask.selection_smooth = map_double_value(mask, QStringLiteral("selectionSmooth"), 0.0);
  local_mask.selection_fine_edge = map_double_value(mask, QStringLiteral("selectionFineEdge"), 0.0);
  local_mask.selection_edge_contrast =
      map_double_value(mask, QStringLiteral("selectionEdgeContrast"), 0.0);
  local_mask.selection_decontaminate =
      map_double_value(mask, QStringLiteral("selectionDecontaminate"), 0.0);
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
      mask, QStringLiteral("feather"), local_mask.kind == "radial" ? 0.25 : 0.22);
  local_mask.rotation = map_double_value(mask, QStringLiteral("rotation"), 0.0);

  if (local_mask.kind == "radial") {
    local_mask.center_x = map_double_value(mask, QStringLiteral("centerX"), 0.5);
    local_mask.center_y = map_double_value(mask, QStringLiteral("centerY"), 0.5);
    local_mask.radius_x = map_double_value(mask, QStringLiteral("radiusX"), 0.25);
    local_mask.radius_y = map_double_value(mask, QStringLiteral("radiusY"), 0.25);
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
    const auto points = mask.value(QStringLiteral("brushPoints")).toList();
    local_mask.brush_points.reserve(static_cast<std::size_t>(points.size()));
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      local_mask.brush_points.push_back(photo_editor::pipeline::BrushPoint{
          .x = map_double_value(point, QStringLiteral("x"), 0.5),
          .y = map_double_value(point, QStringLiteral("y"), 0.5),
          .size = map_double_value(point, QStringLiteral("size"), local_mask.brush_size),
          .flow = map_double_value(point, QStringLiteral("flow"), local_mask.brush_flow),
          .pressure = map_double_value(point, QStringLiteral("pressure"), 1.0),
          .tilt_x = map_double_value(point, QStringLiteral("tiltX"), 0.0),
          .tilt_y = map_double_value(point, QStringLiteral("tiltY"), 0.0),
          .erase = map_bool_value(point, QStringLiteral("erase"), false),
      });
    }
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

std::vector<photo_editor::pipeline::LocalMaskSet> pipeline_local_mask_sets(const QVariantList& masks) {
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

    auto source = pipeline_local_mask(mask);
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

std::vector<photo_editor::pipeline::LocalMask> pipeline_retouch_masks(const QVariantList& masks) {
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
    retouch_masks.push_back(pipeline_local_mask(mask));
  }
  return retouch_masks;
}

double layer_adjustment_value(const QVariantMap& layer, const QString& key) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(key)) {
    return adjustments.value(key).toDouble();
  }
  return layer.value(key).toDouble();
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
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  QFont font(layer.value(QStringLiteral("fontFamily"), QStringLiteral("Helvetica")).toString());
  font.setPixelSize(qBound(8, layer.value(QStringLiteral("fontSize"), 48).toInt(), 512));
  font.setBold(layer.value(QStringLiteral("fontWeight")).toString().trimmed().toLower() ==
               QStringLiteral("bold"));
  painter.setFont(font);
  painter.setPen(color);
  painter.drawText(image.rect().adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, text);
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

std::vector<photo_editor::pipeline::PhotoLayer> pipeline_photo_layers(const QVariantList& layers) {
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
        .temperature = layer_adjustment_value(layer, QStringLiteral("temperature")),
        .tint = layer_adjustment_value(layer, QStringLiteral("tint")),
        .vibrance = layer_adjustment_value(layer, QStringLiteral("vibrance")),
        .saturation = layer_adjustment_value(layer, QStringLiteral("saturation")),
        .hue_shift = layer_adjustment_value(layer, QStringLiteral("hueShift")),
        .saturation_shift = layer_adjustment_value(layer, QStringLiteral("saturationShift")),
        .colorize_hue = layer_adjustment_value(layer, QStringLiteral("colorizeHue")),
        .colorize_strength = layer_adjustment_value(layer, QStringLiteral("colorizeStrength")),
        .fill_color = layer_fill_color(layer),
    };
    populate_photo_layer_mask(&photo_layer, layer);
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
      .output_headroom = state.output_headroom,
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
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr) {
  return photo_editor::pipeline::ColorAdjustmentState{
      .input_transform =
          photo_editor::pipeline::InputTransformSettings{
              .input_profile_name = normalized_input_color_profile_name(state.input_color_profile).toStdString(),
              .exposure = state.exposure,
              .temperature = state.temperature,
              .tint = state.tint,
              .raw_input_transform_enabled = state.raw_input_transform_enabled,
              .highlight_recovery_enabled = state.raw_highlight_recovery,
              .highlight_reconstruction_mode =
                  normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode).toStdString(),
              .chromatic_adaptation_method =
                  normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method).toStdString(),
              .gamut_mapping_enabled = state.raw_gamut_mapping,
              .lens_correction_enabled = state.raw_lens_correction,
              .raw_white_balance_override_enabled = state.raw_white_balance_override,
              .camera_matching_look =
                  normalized_camera_matching_look_name(state.raw_camera_matching_look).toStdString(),
              .raw_white_balance_temperature = state.raw_white_balance_temperature,
              .raw_white_balance_tint = state.raw_white_balance_tint,
              .raw_black_level = state.raw_black_level,
              .raw_white_level = state.raw_white_level,
              .calibration_hue = state.raw_calibration_hue,
              .calibration_saturation = state.raw_calibration_saturation,
              .red_primary_hue = state.raw_red_primary_hue,
              .red_primary_saturation = state.raw_red_primary_saturation,
              .green_primary_hue = state.raw_green_primary_hue,
              .green_primary_saturation = state.raw_green_primary_saturation,
              .blue_primary_hue = state.raw_blue_primary_hue,
              .blue_primary_saturation = state.raw_blue_primary_saturation,
          },
      .color_pipeline = color_pipeline_config_for_state(state, metadata),
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
      .color_replacement_strength = state.color_replacement_strength,
      .skin_tone_protection_enabled = state.skin_tone_protection_enabled,
      .skin_tone_line_hue = state.skin_tone_line_hue,
      .skin_tone_protection_range = state.skin_tone_protection_range,
      .skin_tone_protection_strength = state.skin_tone_protection_strength,
      .color_harmony_enabled = state.color_harmony_enabled,
      .color_harmony_mode = state.color_harmony_mode.toStdString(),
      .color_harmony_base_hue = state.color_harmony_base_hue,
      .color_harmony_strength = state.color_harmony_strength,
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
      .local_mask_sets = pipeline_local_mask_sets(state.mask_stack),
      .retouch_masks = pipeline_retouch_masks(state.mask_stack),
      .photo_layers = pipeline_photo_layers(state.layers),
  };
}

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
photo_editor::pipeline::metal::MetalPipelineBackend& metal_backend() {
  static photo_editor::pipeline::metal::MetalPipelineBackend backend;
  return backend;
}
#endif

}  // namespace

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
  const auto should_cache = max_edge > 0;
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
  *image = render_output_image(*image, state, output_color_space_for_name(state.output_color_space));
}

QImage render_output_image(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry) {
  return render_output_image_with_backend(
      image,
      state,
      target_color_space,
      output_format,
      RenderBackendPreference::Auto,
      metadata,
      telemetry);
}

QImage render_output_image_with_backend(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    RenderBackendPreference backend_preference,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry) {
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

  const auto pipeline_state = to_pipeline_state(state, metadata);
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = metal_backend();
  const auto fallback_reason = gpu_fallback_reason(state, pipeline_state, pixels, metal.available());
  const auto gpu_supported = fallback_reason.isEmpty();
  if (backend_preference == RenderBackendPreference::GpuOnly && !gpu_supported) {
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
    photo_editor::pipeline::apply_color_adjustments(
        &pixels, width, height, pipeline_state, photo_editor::pipeline::RemovalQuality::Final);
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
      &pixels, width, height, pipeline_state, photo_editor::pipeline::RemovalQuality::Final);
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

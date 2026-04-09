#include "photo_render_utils.h"

#include "app_bootstrap.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "raw_image_decoder.h"
#include "working_image.h"

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <QColorSpace>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QTimeZone>

#include <cstring>

namespace photo_editor::desktop {

namespace {

struct DecodedImageCache final {
  QMutex mutex;
  QHash<QString, QImage> images;
  QList<QString> access_order;
  qint64 total_bytes = 0;
};

constexpr qint64 kMaxCacheMemoryBytes = 512 * 1024 * 1024;

QImage normalize_preview_color_space(QImage image);
QColorSpace preview_target_color_space() {
  return preview_color_space();
}

QColorSpace output_color_space_for_name(const QString& name) {
  const auto normalized = normalized_output_color_space_name(name);
  if (normalized == QStringLiteral("Display P3")) {
    return QColorSpace(QColorSpace::DisplayP3);
  }
  return preview_target_color_space();
}

bool has_extended_range_pixels(const std::vector<photo_editor::pipeline::ColorPixel>& pixels) {
  constexpr auto kTolerance = 1.0e-6F;
  return std::any_of(pixels.cbegin(), pixels.cend(), [](const auto& pixel) {
    return pixel.red > 1.0F + kTolerance || pixel.green > 1.0F + kTolerance ||
           pixel.blue > 1.0F + kTolerance || pixel.red < -kTolerance ||
           pixel.green < -kTolerance || pixel.blue < -kTolerance;
  });
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

DecodedImageCache& decoded_image_cache() {
  static DecodedImageCache cache;
  return cache;
}

QString decoded_image_cache_key(const QFileInfo& file_info, int max_edge) {
  return QStringLiteral("%1|%2|%3|%4")
      .arg(file_info.absoluteFilePath())
      .arg(max_edge)
      .arg(file_info.size())
      .arg(file_info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch());
}

RawDecodeSettings raw_decode_settings_for_state(const RenderEditState& state) {
  return RawDecodeSettings{
      .exposure = state.raw_exposure,
      .boost_amount = state.raw_boost,
      .boost_shadow_amount = state.raw_shadow_boost,
      .detail_amount = state.raw_detail,
      .highlight_recovery_enabled = state.raw_highlight_recovery,
      .gamut_mapping_enabled = state.raw_gamut_mapping,
      .lens_correction_enabled = state.raw_lens_correction,
      .white_balance_override_enabled = state.raw_white_balance_override,
      .white_balance_temperature = state.raw_white_balance_temperature,
      .white_balance_tint = state.raw_white_balance_tint,
  };
}

QString raw_decode_settings_key(const RenderEditState& state) {
  return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
      .arg(state.raw_exposure, 0, 'f', 6)
      .arg(state.raw_boost, 0, 'f', 6)
      .arg(state.raw_shadow_boost, 0, 'f', 6)
      .arg(state.raw_detail, 0, 'f', 6)
      .arg(state.raw_highlight_recovery ? 1 : 0)
      .arg(state.raw_gamut_mapping ? 1 : 0)
      .arg(state.raw_lens_correction ? 1 : 0)
      .arg(state.raw_white_balance_override ? 1 : 0)
      .arg(state.raw_white_balance_temperature, 0, 'f', 3)
      .arg(state.raw_white_balance_tint, 0, 'f', 3);
}

void touch_cache_key(DecodedImageCache* cache, const QString& key) {
  cache->access_order.removeOne(key);
  cache->access_order.prepend(key);
}

void insert_cached_image(DecodedImageCache* cache, const QString& key, const QImage& image) {
  if (cache->images.contains(key)) {
    cache->total_bytes -= cache->images.value(key).sizeInBytes();
    cache->images.remove(key);
    cache->access_order.removeOne(key);
  }

  const auto image_bytes = image.sizeInBytes();
  while (cache->total_bytes + image_bytes > kMaxCacheMemoryBytes && !cache->access_order.isEmpty()) {
    const auto& evict_key = cache->access_order.last();
    cache->total_bytes -= cache->images.value(evict_key).sizeInBytes();
    cache->images.remove(evict_key);
    cache->access_order.removeLast();
  }

  cache->images.insert(key, image);
  cache->access_order.prepend(key);
  cache->total_bytes += image_bytes;
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

photo_editor::pipeline::LocalMask pipeline_local_mask(const QVariantMap& mask) {
  photo_editor::pipeline::LocalMask local_mask;
  local_mask.id = mask.value(QStringLiteral("id")).toString().toStdString();
  local_mask.name = mask.value(QStringLiteral("name")).toString().toStdString();
  local_mask.kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString()).toStdString();
  local_mask.operation =
      normalized_mask_operation(mask.value(QStringLiteral("operation")).toString()).toStdString();
  local_mask.retouch_mode = mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower().toStdString();
  local_mask.enabled = mask.contains(QStringLiteral("enabled"))
                           ? mask.value(QStringLiteral("enabled")).toBool()
                           : true;
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
  local_mask.selection_invert = map_bool_value(mask, QStringLiteral("selectionInvert"), false);
  local_mask.selection_expand = map_double_value(mask, QStringLiteral("selectionExpand"), 0.0);
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
  } else if (local_mask.kind == "polygon") {
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
  } else if (local_mask.kind == "subject" || local_mask.kind == "object") {
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

  return local_mask;
}

std::vector<photo_editor::pipeline::LocalMaskSet> pipeline_local_mask_sets(const QVariantList& masks) {
  std::vector<photo_editor::pipeline::LocalMaskSet> mask_sets;
  QHash<QString, int> indices_by_id;
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("selection")) {
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
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("retouch")) {
      continue;
    }
    retouch_masks.push_back(pipeline_local_mask(mask));
  }
  return retouch_masks;
}

photo_editor::pipeline::ColorAdjustmentState to_pipeline_state(const RenderEditState& state) {
  return photo_editor::pipeline::ColorAdjustmentState{
      .exposure = state.exposure,
      .contrast = state.contrast,
      .highlights = state.highlights,
      .shadows = state.shadows,
      .whites = state.whites,
      .blacks = state.blacks,
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
      .vibrance = state.vibrance,
      .saturation = state.saturation,
      .curve_shadows = state.curve_shadows,
      .curve_darks = state.curve_darks,
      .curve_lights = state.curve_lights,
      .curve_highlights = state.curve_highlights,
      .master_curve_points = pipeline_curve_points(state.master_curve_points),
      .red_curve_points = pipeline_curve_points(state.red_curve_points),
      .green_curve_points = pipeline_curve_points(state.green_curve_points),
      .blue_curve_points = pipeline_curve_points(state.blue_curve_points),
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
        }
        return channels;
      }(),
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

bool decode_source_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    const RenderEditState& state) {
  if (image == nullptr || !preview_supported(file_path)) {
    return false;
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
    auto& cache = decoded_image_cache();
    QMutexLocker locker(&cache.mutex);
    const auto cached = cache.images.constFind(cache_key);
    if (cached != cache.images.cend()) {
      *image = cached.value();
      touch_cache_key(&cache, cache_key);
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
      auto& cache = decoded_image_cache();
      QMutexLocker locker(&cache.mutex);
      insert_cached_image(&cache, cache_key, *image);
    }
    return !image->isNull();
  }

  if (!qt_supported) {
    auto decoded = QImage{};
    if (!decode_raw_linear_image(file_path, &decoded, max_edge, raw_decode_settings_for_state(state))) {
      return false;
    }

    *image = normalize_preview_color_space(std::move(decoded));
    if (should_cache && !image->isNull()) {
      auto& cache = decoded_image_cache();
      QMutexLocker locker(&cache.mutex);
      insert_cached_image(&cache, cache_key, *image);
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
    auto& cache = decoded_image_cache();
    QMutexLocker locker(&cache.mutex);
    insert_cached_image(&cache, cache_key, *image);
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
  if (preset == QStringLiteral("Square")) {
    return 1.0;
  }
  if (preset == QStringLiteral("4x5")) {
    return 4.0 / 5.0;
  }
  if (preset == QStringLiteral("3x2")) {
    return 3.0 / 2.0;
  }
  if (preset == QStringLiteral("16x9")) {
    return 16.0 / 9.0;
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
  if (image == nullptr || image->isNull()) {
    return;
  }

  auto working = working_image_from_qimage(*image);
  if (working.isNull()) {
    return;
  }

  auto& pixels = working.pixels;
  const auto width = working.width;
  const auto height = working.height;

  const auto pipeline_state = to_pipeline_state(state);
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = metal_backend();
  const auto requires_cpu_pipeline =
      std::abs(state.texture) > 1e-6 || std::abs(state.clarity) > 1e-6 || std::abs(state.dehaze) > 1e-6 ||
      std::abs(state.sharpening) > 1e-6 || std::abs(state.sharpen_masking) > 1e-6 ||
      std::abs(state.luminance_nr) > 1e-6 || std::abs(state.color_nr) > 1e-6 ||
      std::abs(state.lens_distortion) > 1e-6 || std::abs(state.lens_vignette) > 1e-6 ||
      std::abs(state.chromatic_aberration) > 1e-6;
  if (metal.available() &&
      pipeline_state.local_mask_sets.empty() &&
      pipeline_state.retouch_masks.empty() &&
      pipeline_state.local_masks.empty() &&
      !has_extended_range_pixels(pixels) &&
      !requires_cpu_pipeline) {
    const auto lut = photo_editor::pipeline::build_color_lut(pipeline_state);
    metal.apply_masks(&pixels, width, height, pipeline_state);
    metal.apply_color_lut(&pixels, width, height, lut);
  } else {
    photo_editor::pipeline::apply_color_adjustments(
        &pixels, width, height, pipeline_state, photo_editor::pipeline::RemovalQuality::Final);
  }
#else
  photo_editor::pipeline::apply_color_adjustments(
      &pixels, width, height, pipeline_state, photo_editor::pipeline::RemovalQuality::Final);
#endif

  *image = qimage_from_working_image(working, preview_target_color_space());
  apply_output_color_space_transform(image, state.output_color_space);
}

}  // namespace photo_editor::desktop

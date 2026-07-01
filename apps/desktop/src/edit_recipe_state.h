#pragma once

#include <algorithm>
#include <array>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <limits>
#include <vector>

namespace photo_editor::desktop {

inline constexpr int kCurrentEditRecipeVersion = 4;

struct EditRecipeState final {
  QString input_color_profile = QStringLiteral("Embedded");
  QString working_color_profile = QStringLiteral("Linear ProPhoto RGB");
  QString preview_color_space = QStringLiteral("sRGB");
  QString monitor_color_profile;
  bool soft_proof_enabled = false;
  QString soft_proof_profile = QStringLiteral("sRGB");
  QString render_intent = QStringLiteral("Perceptual");
  bool raw_input_transform_enabled = true;
  QString highlight_reconstruction_mode = QStringLiteral("Enabled");
  QString chromatic_adaptation_method = QStringLiteral("Bradford");
  QString raw_demosaic_quality = QStringLiteral("Balanced");
  double raw_exposure = 0.0;
  double raw_boost = 0.0;
  double raw_shadow_boost = 1.0;
  double raw_detail = 0.0;
  double raw_chroma_noise_reduction = 0.0;
  double raw_moire_reduction = 0.0;
  double raw_hot_dead_pixel_cleanup = 0.0;
  double raw_false_color_suppression = 0.0;
  bool raw_highlight_recovery = true;
  bool raw_gamut_mapping = true;
  bool raw_lens_correction = true;
  bool raw_white_balance_override = false;
  QString raw_camera_matching_look = QStringLiteral("Standard");
  double raw_white_balance_temperature = 5200.0;
  double raw_white_balance_tint = 0.0;
  double raw_black_level = 0.0;
  double raw_white_level = 1.0;
  double raw_calibration_hue = 0.0;
  double raw_calibration_saturation = 0.0;
  double raw_red_primary_hue = 0.0;
  double raw_red_primary_saturation = 0.0;
  double raw_green_primary_hue = 0.0;
  double raw_green_primary_saturation = 0.0;
  double raw_blue_primary_hue = 0.0;
  double raw_blue_primary_saturation = 0.0;
  double exposure = 0.0;
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double brightness = 0.0;
  double hdr_local_tone_mapping = 0.0;
  double filmic_shoulder = 0.0;
  double filmic_toe = 0.0;
  double adaptive_contrast = 0.0;
  double levels_black_point = 0.0;
  double levels_gamma = 0.0;
  double levels_white_point = 0.0;
  double zone_deep_shadows = 0.0;
  double zone_shadows = 0.0;
  double zone_midtones = 0.0;
  double zone_lights = 0.0;
  double zone_highlights = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  double texture = 0.0;
  double clarity = 0.0;
  double dehaze = 0.0;
  double sharpening = 0.0;
  double sharpen_radius = 1.0;
  double sharpen_detail = 0.0;
  double sharpen_masking = 0.0;
  double luminance_nr = 0.0;
  double color_nr = 0.0;
  double lens_distortion = 0.0;
  double lens_vignette = 0.0;
  double chromatic_aberration = 0.0;
  double perspective_vertical = 0.0;
  double perspective_horizontal = 0.0;
  double transform_rotation = 0.0;
  double transform_scale = 1.0;
  double transform_skew_x = 0.0;
  double transform_skew_y = 0.0;
  double liquify_center_x = 0.5;
  double liquify_center_y = 0.5;
  double liquify_radius = 0.25;
  double liquify_strength = 0.0;
  bool content_aware_crop_fill = false;
  double vibrance = 0.0;
  double saturation = 0.0;
  double curve_shadows = 0.0;
  double curve_darks = 0.0;
  double curve_lights = 0.0;
  double curve_highlights = 0.0;
  QVariantList master_curve_points;
  QVariantList luma_curve_points;
  QVariantList red_curve_points;
  QVariantList green_curve_points;
  QVariantList blue_curve_points;
  QString color_mixer_model = QStringLiteral("OKLab");
  QString hsl_channel = QStringLiteral("Reds");
  double hue_adjustment = 0.0;
  double saturation_adjustment = 0.0;
  double luminance_adjustment = 0.0;
  // Per-channel mixer adjustments: [Reds, Oranges, Yellows, Greens, Aquas, Blues, Purples, Magentas]
  struct MixerChannel {
    double hue = 0.0;
    double saturation = 0.0;
    double luminance = 0.0;
    double selective_cyan = 0.0;
    double selective_magenta = 0.0;
    double selective_yellow = 0.0;
    double selective_black = 0.0;
  };
  std::array<MixerChannel, 8> mixer_channels{};
  struct RgbChannelMixer {
    double red_from_red = 100.0;
    double red_from_green = 0.0;
    double red_from_blue = 0.0;
    double green_from_red = 0.0;
    double green_from_green = 100.0;
    double green_from_blue = 0.0;
    double blue_from_red = 0.0;
    double blue_from_green = 0.0;
    double blue_from_blue = 100.0;
  };
  RgbChannelMixer rgb_channel_mixer;
  bool black_and_white_enabled = false;
  std::array<double, 8> black_and_white_mixer{};
  bool color_replacement_enabled = false;
  double color_replacement_source_hue = 0.0;
  double color_replacement_target_hue = 0.33;
  double color_replacement_range = 35.0;
  double color_replacement_softness = 100.0;
  double color_replacement_strength = 0.0;
  bool color_replacement_preserve_luminance = true;
  bool color_replacement_preserve_saturation = true;
  bool skin_tone_protection_enabled = false;
  double skin_tone_line_hue = 0.08;
  double skin_tone_protection_range = 30.0;
  double skin_tone_protection_strength = 0.0;
  bool color_harmony_enabled = false;
  QString color_harmony_mode = QStringLiteral("Complementary");
  double color_harmony_base_hue = 0.0;
  double color_harmony_strength = 0.0;
  double grading_shadows_hue = 0.0;
  double grading_shadows_saturation = 0.0;
  double grading_shadows_luminance = 0.0;
  double grading_midtones_hue = 0.0;
  double grading_midtones_saturation = 0.0;
  double grading_midtones_luminance = 0.0;
  double grading_highlights_hue = 0.0;
  double grading_highlights_saturation = 0.0;
  double grading_highlights_luminance = 0.0;
  double grading_amount = 100.0;
  double grading_balance = 0.0;
  double crop_rect_x = 0.0;
  double crop_rect_y = 0.0;
  double crop_rect_width = 0.0;
  double crop_rect_height = 0.0;
  double crop_zoom = 1.0;
  QString crop_preset = QStringLiteral("Original");
  QString upright_mode = QStringLiteral("Off");
  double upright_vertical_guide_angle = 0.0;
  double upright_horizontal_guide_angle = 0.0;
  QString output_color_space = QStringLiteral("sRGB");
  double output_headroom = 1.0;
  bool linear_mask_enabled = false;
  double linear_mask_exposure = 0.0;
  double linear_mask_temperature = 0.0;
  double linear_mask_feather = 0.22;
  double linear_mask_position = 0.5;
  double linear_mask_rotation = 0.0;
  bool radial_mask_enabled = false;
  double radial_mask_exposure = 0.0;
  double radial_mask_temperature = 0.0;
  double radial_mask_feather = 0.25;
  double radial_mask_center_x = 0.5;
  double radial_mask_center_y = 0.5;
  double radial_mask_radius_x = 0.25;
  double radial_mask_radius_y = 0.25;
  double radial_mask_rotation = 0.0;
  QVariantList mask_stack;
  int active_mask_index = -1;
  QVariantList preset_applications;
  QVariantList layers;
  QString active_history_branch = QStringLiteral("main");
  QVariantList recipe_snapshots;

  [[nodiscard]] bool operator==(const EditRecipeState& other) const {
    auto feq = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    return feq(raw_exposure, other.raw_exposure) &&
           input_color_profile == other.input_color_profile &&
           working_color_profile == other.working_color_profile &&
           preview_color_space == other.preview_color_space &&
           monitor_color_profile == other.monitor_color_profile &&
           soft_proof_enabled == other.soft_proof_enabled &&
           soft_proof_profile == other.soft_proof_profile &&
           render_intent == other.render_intent &&
           raw_input_transform_enabled == other.raw_input_transform_enabled &&
           highlight_reconstruction_mode == other.highlight_reconstruction_mode &&
           chromatic_adaptation_method == other.chromatic_adaptation_method &&
           raw_demosaic_quality == other.raw_demosaic_quality &&
           feq(raw_boost, other.raw_boost) &&
           feq(raw_shadow_boost, other.raw_shadow_boost) &&
           feq(raw_detail, other.raw_detail) &&
           feq(raw_chroma_noise_reduction, other.raw_chroma_noise_reduction) &&
           feq(raw_moire_reduction, other.raw_moire_reduction) &&
           feq(raw_hot_dead_pixel_cleanup, other.raw_hot_dead_pixel_cleanup) &&
           feq(raw_false_color_suppression, other.raw_false_color_suppression) &&
           raw_highlight_recovery == other.raw_highlight_recovery &&
           raw_gamut_mapping == other.raw_gamut_mapping &&
            raw_lens_correction == other.raw_lens_correction &&
              raw_white_balance_override == other.raw_white_balance_override &&
              raw_camera_matching_look == other.raw_camera_matching_look &&
              feq(raw_white_balance_temperature, other.raw_white_balance_temperature) &&
             feq(raw_white_balance_tint, other.raw_white_balance_tint) &&
             feq(raw_black_level, other.raw_black_level) &&
             feq(raw_white_level, other.raw_white_level) &&
             feq(raw_calibration_hue, other.raw_calibration_hue) &&
             feq(raw_calibration_saturation, other.raw_calibration_saturation) &&
             feq(raw_red_primary_hue, other.raw_red_primary_hue) &&
             feq(raw_red_primary_saturation, other.raw_red_primary_saturation) &&
             feq(raw_green_primary_hue, other.raw_green_primary_hue) &&
             feq(raw_green_primary_saturation, other.raw_green_primary_saturation) &&
             feq(raw_blue_primary_hue, other.raw_blue_primary_hue) &&
             feq(raw_blue_primary_saturation, other.raw_blue_primary_saturation) &&
             feq(exposure, other.exposure) && feq(contrast, other.contrast) &&
           feq(highlights, other.highlights) && feq(shadows, other.shadows) &&
           feq(whites, other.whites) && feq(blacks, other.blacks) &&
           feq(brightness, other.brightness) &&
            feq(hdr_local_tone_mapping, other.hdr_local_tone_mapping) &&
            feq(filmic_shoulder, other.filmic_shoulder) &&
            feq(filmic_toe, other.filmic_toe) &&
            feq(adaptive_contrast, other.adaptive_contrast) &&
            feq(levels_black_point, other.levels_black_point) &&
            feq(levels_gamma, other.levels_gamma) &&
            feq(levels_white_point, other.levels_white_point) &&
            feq(zone_deep_shadows, other.zone_deep_shadows) &&
            feq(zone_shadows, other.zone_shadows) &&
            feq(zone_midtones, other.zone_midtones) &&
            feq(zone_lights, other.zone_lights) &&
            feq(zone_highlights, other.zone_highlights) &&
            feq(temperature, other.temperature) && feq(tint, other.tint) &&
           feq(texture, other.texture) && feq(clarity, other.clarity) &&
           feq(dehaze, other.dehaze) &&
           feq(sharpening, other.sharpening) &&
           feq(sharpen_radius, other.sharpen_radius) &&
           feq(sharpen_detail, other.sharpen_detail) &&
           feq(sharpen_masking, other.sharpen_masking) &&
           feq(luminance_nr, other.luminance_nr) && feq(color_nr, other.color_nr) &&
           feq(lens_distortion, other.lens_distortion) &&
           feq(lens_vignette, other.lens_vignette) &&
           feq(chromatic_aberration, other.chromatic_aberration) &&
           feq(perspective_vertical, other.perspective_vertical) &&
           feq(perspective_horizontal, other.perspective_horizontal) &&
            feq(transform_rotation, other.transform_rotation) &&
            feq(transform_scale, other.transform_scale) &&
            feq(transform_skew_x, other.transform_skew_x) &&
            feq(transform_skew_y, other.transform_skew_y) &&
            feq(liquify_center_x, other.liquify_center_x) &&
            feq(liquify_center_y, other.liquify_center_y) &&
            feq(liquify_radius, other.liquify_radius) &&
            feq(liquify_strength, other.liquify_strength) &&
            content_aware_crop_fill == other.content_aware_crop_fill &&
           feq(vibrance, other.vibrance) &&
           feq(saturation, other.saturation) && feq(curve_shadows, other.curve_shadows) &&
           feq(curve_darks, other.curve_darks) && feq(curve_lights, other.curve_lights) &&
            feq(curve_highlights, other.curve_highlights) &&
            master_curve_points == other.master_curve_points &&
            luma_curve_points == other.luma_curve_points &&
            red_curve_points == other.red_curve_points &&
           green_curve_points == other.green_curve_points &&
           blue_curve_points == other.blue_curve_points &&
           color_mixer_model == other.color_mixer_model &&
           hsl_channel == other.hsl_channel &&
           feq(hue_adjustment, other.hue_adjustment) &&
           feq(saturation_adjustment, other.saturation_adjustment) &&
           feq(luminance_adjustment, other.luminance_adjustment) &&
           [&]() {
             for (std::size_t i = 0; i < mixer_channels.size(); ++i) {
                if (!feq(mixer_channels[i].hue, other.mixer_channels[i].hue) ||
                    !feq(mixer_channels[i].saturation, other.mixer_channels[i].saturation) ||
                    !feq(mixer_channels[i].luminance, other.mixer_channels[i].luminance) ||
                    !feq(mixer_channels[i].selective_cyan, other.mixer_channels[i].selective_cyan) ||
                    !feq(mixer_channels[i].selective_magenta, other.mixer_channels[i].selective_magenta) ||
                    !feq(mixer_channels[i].selective_yellow, other.mixer_channels[i].selective_yellow) ||
                    !feq(mixer_channels[i].selective_black, other.mixer_channels[i].selective_black)) {
                 return false;
               }
             }
             return true;
            }() &&
            feq(rgb_channel_mixer.red_from_red, other.rgb_channel_mixer.red_from_red) &&
            feq(rgb_channel_mixer.red_from_green, other.rgb_channel_mixer.red_from_green) &&
            feq(rgb_channel_mixer.red_from_blue, other.rgb_channel_mixer.red_from_blue) &&
            feq(rgb_channel_mixer.green_from_red, other.rgb_channel_mixer.green_from_red) &&
            feq(rgb_channel_mixer.green_from_green, other.rgb_channel_mixer.green_from_green) &&
            feq(rgb_channel_mixer.green_from_blue, other.rgb_channel_mixer.green_from_blue) &&
            feq(rgb_channel_mixer.blue_from_red, other.rgb_channel_mixer.blue_from_red) &&
            feq(rgb_channel_mixer.blue_from_green, other.rgb_channel_mixer.blue_from_green) &&
            feq(rgb_channel_mixer.blue_from_blue, other.rgb_channel_mixer.blue_from_blue) &&
            black_and_white_enabled == other.black_and_white_enabled &&
            [&]() {
              for (std::size_t i = 0; i < black_and_white_mixer.size(); ++i) {
                if (!feq(black_and_white_mixer[i], other.black_and_white_mixer[i])) {
                  return false;
                }
              }
              return true;
            }() &&
            color_replacement_enabled == other.color_replacement_enabled &&
            feq(color_replacement_source_hue, other.color_replacement_source_hue) &&
            feq(color_replacement_target_hue, other.color_replacement_target_hue) &&
            feq(color_replacement_range, other.color_replacement_range) &&
            feq(color_replacement_softness, other.color_replacement_softness) &&
            feq(color_replacement_strength, other.color_replacement_strength) &&
            color_replacement_preserve_luminance == other.color_replacement_preserve_luminance &&
            color_replacement_preserve_saturation == other.color_replacement_preserve_saturation &&
            skin_tone_protection_enabled == other.skin_tone_protection_enabled &&
            feq(skin_tone_line_hue, other.skin_tone_line_hue) &&
            feq(skin_tone_protection_range, other.skin_tone_protection_range) &&
            feq(skin_tone_protection_strength, other.skin_tone_protection_strength) &&
            color_harmony_enabled == other.color_harmony_enabled &&
            color_harmony_mode == other.color_harmony_mode &&
            feq(color_harmony_base_hue, other.color_harmony_base_hue) &&
            feq(color_harmony_strength, other.color_harmony_strength) &&
            feq(grading_shadows_hue, other.grading_shadows_hue) &&
           feq(grading_shadows_saturation, other.grading_shadows_saturation) &&
           feq(grading_shadows_luminance, other.grading_shadows_luminance) &&
           feq(grading_midtones_hue, other.grading_midtones_hue) &&
           feq(grading_midtones_saturation, other.grading_midtones_saturation) &&
           feq(grading_midtones_luminance, other.grading_midtones_luminance) &&
            feq(grading_highlights_hue, other.grading_highlights_hue) &&
           feq(grading_highlights_saturation, other.grading_highlights_saturation) &&
           feq(grading_highlights_luminance, other.grading_highlights_luminance) &&
           feq(grading_amount, other.grading_amount) &&
           feq(grading_balance, other.grading_balance) &&
           feq(crop_rect_x, other.crop_rect_x) && feq(crop_rect_y, other.crop_rect_y) &&
           feq(crop_rect_width, other.crop_rect_width) && feq(crop_rect_height, other.crop_rect_height) &&
           feq(crop_zoom, other.crop_zoom) && crop_preset == other.crop_preset &&
           upright_mode == other.upright_mode &&
           feq(upright_vertical_guide_angle, other.upright_vertical_guide_angle) &&
           feq(upright_horizontal_guide_angle, other.upright_horizontal_guide_angle) &&
           output_color_space == other.output_color_space && feq(output_headroom, other.output_headroom) &&
           linear_mask_enabled == other.linear_mask_enabled &&
           feq(linear_mask_exposure, other.linear_mask_exposure) &&
           feq(linear_mask_temperature, other.linear_mask_temperature) &&
           feq(linear_mask_feather, other.linear_mask_feather) &&
           feq(linear_mask_position, other.linear_mask_position) &&
           feq(linear_mask_rotation, other.linear_mask_rotation) &&
           radial_mask_enabled == other.radial_mask_enabled &&
           feq(radial_mask_exposure, other.radial_mask_exposure) &&
           feq(radial_mask_temperature, other.radial_mask_temperature) &&
           feq(radial_mask_feather, other.radial_mask_feather) &&
           feq(radial_mask_center_x, other.radial_mask_center_x) &&
           feq(radial_mask_center_y, other.radial_mask_center_y) &&
            feq(radial_mask_radius_x, other.radial_mask_radius_x) &&
            feq(radial_mask_radius_y, other.radial_mask_radius_y) &&
            feq(radial_mask_rotation, other.radial_mask_rotation) &&
             mask_stack == other.mask_stack &&
             active_mask_index == other.active_mask_index &&
             preset_applications == other.preset_applications &&
            layers == other.layers &&
            active_history_branch == other.active_history_branch &&
            recipe_snapshots == other.recipe_snapshots;
  }

  [[nodiscard]] bool operator!=(const EditRecipeState& other) const { return !(*this == other); }
};

namespace detail {

inline QJsonObject object_value(const QJsonObject& object, const QString& key) {
  const auto value = object.value(key);
  return value.isObject() ? value.toObject() : QJsonObject{};
}

inline double read_double(
    const QJsonObject& primary,
    const QString& primary_key,
    const QJsonObject& legacy,
    const QString& legacy_key,
    double default_value = 0.0) {
  if (primary.contains(primary_key)) {
    return primary.value(primary_key).toDouble(default_value);
  }
  return legacy.value(legacy_key).toDouble(default_value);
}

inline QString read_string(
    const QJsonObject& primary,
    const QString& primary_key,
    const QJsonObject& legacy,
    const QString& legacy_key,
    const QString& default_value = QString()) {
  if (primary.contains(primary_key)) {
    return primary.value(primary_key).toString(default_value);
  }
  return legacy.value(legacy_key).toString(default_value);
}

inline bool read_bool(
    const QJsonObject& primary,
    const QString& primary_key,
    const QJsonObject& legacy,
    const QString& legacy_key,
    bool default_value = false) {
  if (primary.contains(primary_key)) {
    return primary.value(primary_key).toBool(default_value);
  }
  return legacy.value(legacy_key).toBool(default_value);
}

inline QByteArray read_byte_array(const QJsonObject& object, const QString& key) {
  return QByteArray::fromBase64(object.value(key).toString().toLatin1());
}

inline QVariantMap normalize_photo_layer_payloads(QVariantMap layer) {
  constexpr std::array<const char*, 2> byte_payload_keys{"maskData", "pixelData"};
  for (const auto* raw_key : byte_payload_keys) {
    const auto key = QString::fromLatin1(raw_key);
    const auto value = layer.value(key);
    if (!value.isValid()) {
      continue;
    }
    if (value.metaType().id() == QMetaType::QByteArray) {
      continue;
    }
    layer.insert(key, QByteArray::fromBase64(value.toString().toLatin1()));
  }
  return layer;
}

inline QVariantMap encode_photo_layer_payloads(QVariantMap layer) {
  constexpr std::array<const char*, 2> byte_payload_keys{"maskData", "pixelData"};
  for (const auto* raw_key : byte_payload_keys) {
    const auto key = QString::fromLatin1(raw_key);
    const auto value = layer.value(key);
    if (!value.isValid()) {
      continue;
    }
    const auto data = value.metaType().id() == QMetaType::QByteArray
                          ? value.toByteArray()
                          : QByteArray::fromBase64(value.toString().toLatin1());
    layer.insert(key, QString::fromLatin1(data.toBase64()));
  }
  return layer;
}

inline QVariantList parse_photo_layers(const QJsonArray& layers) {
  QVariantList parsed_layers;
  parsed_layers.reserve(layers.size());
  for (const auto& layer_value : layers) {
    if (!layer_value.isObject()) {
      parsed_layers.append(layer_value.toVariant());
      continue;
    }
    parsed_layers.append(normalize_photo_layer_payloads(layer_value.toObject().toVariantMap()));
  }
  return parsed_layers;
}

inline QVariantList serialize_photo_layers(const QVariantList& layers) {
  QVariantList serialized_layers;
  serialized_layers.reserve(layers.size());
  for (const auto& layer_value : layers) {
    if (!layer_value.canConvert<QVariantMap>()) {
      serialized_layers.append(layer_value);
      continue;
    }
    serialized_layers.append(encode_photo_layer_payloads(layer_value.toMap()));
  }
  return serialized_layers;
}

}  // namespace detail

struct EditRecipeValidationResult final {
  bool valid = false;
  QStringList errors;
};

struct EditRecipeMigrationResult final {
  bool migrated = false;
  int source_version = 0;
  int target_version = kCurrentEditRecipeVersion;
  QString recipe_json;
  QStringList errors;
};

struct EditRecipeDiffResult final {
  bool valid = false;
  QStringList differences;
  QStringList errors;
};

struct EditRecipeSidecarResult final {
  bool ok = false;
  QString recipe_json;
  QString sidecar_path;
  QStringList errors;
};

inline constexpr qint64 kMaxMaskPayloadBytes = 64LL * 1024LL * 1024LL;
inline constexpr int kMaxMaskPayloadEdge = 16384;

inline bool valid_segmentation_payload(const QByteArray& data, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (width > kMaxMaskPayloadEdge || height > kMaxMaskPayloadEdge) {
    return false;
  }

  const auto expected_size = static_cast<qint64>(width) * static_cast<qint64>(height);
  return expected_size > 0 &&
         expected_size <= kMaxMaskPayloadBytes &&
         data.size() == expected_size;
}

inline int map_int_value_or(const QVariantMap& map, const QString& key, int fallback) {
  return map.contains(key) ? map.value(key).toInt() : fallback;
}

inline void copy_inpaint_patch_payload(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr) {
    return;
  }

  const auto patch_data = source.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto patch_width = source.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height = source.value(QStringLiteral("inpaintPatchHeight")).toInt();
  if (patch_data.isEmpty() || patch_width <= 0 || patch_height <= 0) {
    return;
  }

  target->insert(QStringLiteral("inpaintPatchData"), patch_data);
  target->insert(QStringLiteral("inpaintPatchX"), source.value(QStringLiteral("inpaintPatchX")).toInt());
  target->insert(QStringLiteral("inpaintPatchY"), source.value(QStringLiteral("inpaintPatchY")).toInt());
  target->insert(QStringLiteral("inpaintPatchWidth"), patch_width);
  target->insert(QStringLiteral("inpaintPatchHeight"), patch_height);
  target->insert(
      QStringLiteral("inpaintPatchColorSpace"),
      source.value(QStringLiteral("inpaintPatchColorSpace"), QStringLiteral("sRGB")).toString());
  target->insert(
      QStringLiteral("inpaintPatchSchemaVersion"),
      std::max(1, map_int_value_or(source, QStringLiteral("inpaintPatchSchemaVersion"), 1)));
  target->insert(
      QStringLiteral("inpaintModelName"),
      source.value(QStringLiteral("inpaintModelName")).toString());
  const auto source_mask_data = source.value(QStringLiteral("inpaintSourceMaskData")).toByteArray();
  const auto source_mask_width = source.value(QStringLiteral("inpaintSourceMaskWidth")).toInt();
  const auto source_mask_height = source.value(QStringLiteral("inpaintSourceMaskHeight")).toInt();
  if (valid_segmentation_payload(source_mask_data, source_mask_width, source_mask_height)) {
    target->insert(QStringLiteral("inpaintSourceMaskData"), source_mask_data);
    target->insert(QStringLiteral("inpaintSourceMaskWidth"), source_mask_width);
    target->insert(QStringLiteral("inpaintSourceMaskHeight"), source_mask_height);
    const auto source_bounds_width =
        source.value(QStringLiteral("inpaintSourceBoundsWidth")).toInt();
    const auto source_bounds_height =
        source.value(QStringLiteral("inpaintSourceBoundsHeight")).toInt();
    if (source_bounds_width > 0 && source_bounds_height > 0) {
      target->insert(
          QStringLiteral("inpaintSourceBoundsX"),
          source.value(QStringLiteral("inpaintSourceBoundsX")).toInt());
      target->insert(
          QStringLiteral("inpaintSourceBoundsY"),
          source.value(QStringLiteral("inpaintSourceBoundsY")).toInt());
      target->insert(QStringLiteral("inpaintSourceBoundsWidth"), source_bounds_width);
      target->insert(QStringLiteral("inpaintSourceBoundsHeight"), source_bounds_height);
    }
  }
  const auto selected_variation_index =
      source.contains(QStringLiteral("inpaintSelectedVariationIndex"))
          ? source.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt()
          : source.value(QStringLiteral("inpaintCandidateIndex")).toInt();
  target->insert(QStringLiteral("inpaintCandidateIndex"), selected_variation_index);
  target->insert(QStringLiteral("inpaintSelectedVariationIndex"), selected_variation_index);
  target->insert(
      QStringLiteral("inpaintSelectedVariationSeed"),
      std::max(
          1,
          source.contains(QStringLiteral("inpaintSelectedVariationSeed"))
              ? source.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt()
              : source.value(QStringLiteral("inpaintSeed")).toInt()));
  target->insert(
      QStringLiteral("inpaintSelectedVariationModelName"),
      source.value(
                QStringLiteral("inpaintSelectedVariationModelName"),
                source.value(QStringLiteral("inpaintModelName")))
          .toString());
  const auto source_candidates = source.value(QStringLiteral("inpaintCandidates")).toList();
  if (!source_candidates.isEmpty()) {
    QVariantList candidates;
    for (const auto& candidate_value : source_candidates) {
      const auto candidate = candidate_value.toMap();
      const auto candidate_data = candidate.value(QStringLiteral("patchData")).toByteArray();
      const auto candidate_width = candidate.value(QStringLiteral("patchWidth")).toInt();
      const auto candidate_height = candidate.value(QStringLiteral("patchHeight")).toInt();
      if (candidate_data.isEmpty() || candidate_width <= 0 || candidate_height <= 0) {
        continue;
      }
      QVariantMap normalized_candidate;
      normalized_candidate.insert(QStringLiteral("patchData"), candidate_data);
      normalized_candidate.insert(QStringLiteral("patchX"), candidate.value(QStringLiteral("patchX")).toInt());
      normalized_candidate.insert(QStringLiteral("patchY"), candidate.value(QStringLiteral("patchY")).toInt());
      normalized_candidate.insert(QStringLiteral("patchWidth"), candidate_width);
      normalized_candidate.insert(QStringLiteral("patchHeight"), candidate_height);
      normalized_candidate.insert(
          QStringLiteral("patchColorSpace"),
          candidate.value(QStringLiteral("patchColorSpace"), QStringLiteral("sRGB")).toString());
      normalized_candidate.insert(QStringLiteral("modelName"), candidate.value(QStringLiteral("modelName")).toString());
      normalized_candidate.insert(QStringLiteral("index"), candidate.value(QStringLiteral("index")).toInt());
      normalized_candidate.insert(
          QStringLiteral("seed"),
          std::max(1, candidate.value(QStringLiteral("seed")).toInt()));
      candidates.append(normalized_candidate);
    }
    if (!candidates.isEmpty()) {
      target->insert(QStringLiteral("inpaintCandidates"), candidates);
      target->insert(QStringLiteral("inpaintCandidateCount"), candidates.size());
    }
  }
  target->insert(
      QStringLiteral("inpaintPatchPayloadBytes"),
      std::max(0, source.value(QStringLiteral("inpaintPatchPayloadBytes")).toInt()));
  target->insert(
      QStringLiteral("inpaintCandidatePayloadBytes"),
      std::max(0, source.value(QStringLiteral("inpaintCandidatePayloadBytes")).toInt()));
  target->insert(
      QStringLiteral("inpaintCandidateEvictedCount"),
      std::max(0, source.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt()));
  const auto prompt = source.value(QStringLiteral("inpaintPrompt")).toString();
  if (!prompt.isEmpty()) {
    target->insert(QStringLiteral("inpaintPrompt"), prompt);
  }
  const auto quality_mode = source.value(QStringLiteral("inpaintQualityMode")).toString().trimmed().toLower();
  target->insert(
      QStringLiteral("inpaintQualityMode"),
      quality_mode == QStringLiteral("final") ? QStringLiteral("final") : QStringLiteral("preview"));
  target->insert(
      QStringLiteral("inpaintFinalRequired"),
      source.contains(QStringLiteral("inpaintFinalRequired"))
          ? source.value(QStringLiteral("inpaintFinalRequired")).toBool()
          : quality_mode != QStringLiteral("final"));
  target->insert(
      QStringLiteral("inpaintSeed"),
      std::max(1, source.value(QStringLiteral("inpaintSeed")).toInt()));
  target->insert(
      QStringLiteral("inpaintBlendAmount"),
      qBound(0.0, source.value(QStringLiteral("inpaintBlendAmount"), 1.0).toDouble(), 1.0));
  target->insert(
      QStringLiteral("inpaintSeamFeather"),
      qBound(0.0, source.value(QStringLiteral("inpaintSeamFeather"), 4.0).toDouble(), 24.0));
  target->insert(
      QStringLiteral("inpaintColorMatch"),
      qBound(0.0, source.value(QStringLiteral("inpaintColorMatch"), 0.75).toDouble(), 1.0));
  target->insert(
      QStringLiteral("inpaintFallback"),
      source.contains(QStringLiteral("inpaintFallback"))
          ? source.value(QStringLiteral("inpaintFallback")).toBool()
          : false);
  const auto fallback_reason = source.value(QStringLiteral("inpaintFallbackReason")).toString();
  if (!fallback_reason.isEmpty()) {
    target->insert(QStringLiteral("inpaintFallbackReason"), fallback_reason);
  }
  const auto region_kind = source.value(QStringLiteral("inpaintRegionKind")).toString();
  if (!region_kind.isEmpty()) {
    target->insert(QStringLiteral("inpaintRegionKind"), region_kind);
    target->insert(
        QStringLiteral("inpaintEdgeContinuation"),
        source.value(QStringLiteral("inpaintEdgeContinuation")).toBool());
    target->insert(
        QStringLiteral("inpaintTextureSynthesis"),
        source.value(QStringLiteral("inpaintTextureSynthesis")).toBool());
    target->insert(
        QStringLiteral("inpaintTextureStrength"),
        qBound(0.0, source.value(QStringLiteral("inpaintTextureStrength")).toDouble(), 1.0));
    target->insert(
        QStringLiteral("inpaintRegionCoverage"),
        qBound(0.0, source.value(QStringLiteral("inpaintRegionCoverage")).toDouble(), 1.0));
    target->insert(QStringLiteral("inpaintRegionX"), source.value(QStringLiteral("inpaintRegionX")).toInt());
    target->insert(QStringLiteral("inpaintRegionY"), source.value(QStringLiteral("inpaintRegionY")).toInt());
    target->insert(QStringLiteral("inpaintRegionWidth"), source.value(QStringLiteral("inpaintRegionWidth")).toInt());
    target->insert(QStringLiteral("inpaintRegionHeight"), source.value(QStringLiteral("inpaintRegionHeight")).toInt());
  }
}

inline void copy_generated_confidence_payload(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr) {
    return;
  }

  const auto confidence_data = source.value(QStringLiteral("confidenceData")).toByteArray();
  const auto confidence_width = source.value(QStringLiteral("confidenceWidth")).toInt();
  const auto confidence_height = source.value(QStringLiteral("confidenceHeight")).toInt();
  if (!valid_segmentation_payload(confidence_data, confidence_width, confidence_height)) {
    return;
  }
  target->insert(QStringLiteral("confidenceData"), confidence_data);
  target->insert(QStringLiteral("confidenceWidth"), confidence_width);
  target->insert(QStringLiteral("confidenceHeight"), confidence_height);
  target->insert(
      QStringLiteral("confidenceMapSchemaVersion"),
      std::max(1, map_int_value_or(source, QStringLiteral("confidenceMapSchemaVersion"), 1)));
}

inline void copy_generated_mask_metadata(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr) {
    return;
  }

  const auto metadata = source.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (!metadata.isEmpty()) {
    target->insert(QStringLiteral("generatedMaskMetadata"), metadata);
  }
  if (source.contains(QStringLiteral("generatedMaskStale"))) {
    target->insert(
        QStringLiteral("generatedMaskStale"),
        source.value(QStringLiteral("generatedMaskStale")).toBool());
  }
  for (const auto& key : {
           QStringLiteral("generatedAlphaSourceContract"),
           QStringLiteral("generatedAlphaModelName"),
           QStringLiteral("generatedAlphaModelVersion"),
           QStringLiteral("generatedAlphaTargetType"),
           QStringLiteral("generatedAlphaPromptType"),
           QStringLiteral("generatedAlphaSourceImageVersion"),
           QStringLiteral("generatedAlphaWidth"),
           QStringLiteral("generatedAlphaHeight"),
           QStringLiteral("generatedAlphaDownscaleFactor"),
           QStringLiteral("generatedAlphaConfidenceAvailable"),
           QStringLiteral("generatedAlphaConfidenceWidth"),
           QStringLiteral("generatedAlphaConfidenceHeight"),
           QStringLiteral("generatedAlphaRefinementStateSignature"),
           QStringLiteral("generatedAlphaSourceSignature"),
       }) {
    if (source.contains(key)) {
      target->insert(key, source.value(key));
    }
  }
  copy_generated_confidence_payload(target, source);
}

inline void read_inpaint_patch_payload(QVariantMap* target, const QJsonObject& geometry) {
  if (target == nullptr) {
    return;
  }

  const auto patch_width = geometry.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height = geometry.value(QStringLiteral("inpaintPatchHeight")).toInt();
  const auto patch_data =
      detail::read_byte_array(geometry, QStringLiteral("inpaintPatchData"));
  if (patch_data.isEmpty() || patch_width <= 0 || patch_height <= 0) {
    return;
  }

  target->insert(QStringLiteral("inpaintPatchData"), patch_data);
  target->insert(QStringLiteral("inpaintPatchX"), geometry.value(QStringLiteral("inpaintPatchX")).toInt());
  target->insert(QStringLiteral("inpaintPatchY"), geometry.value(QStringLiteral("inpaintPatchY")).toInt());
  target->insert(QStringLiteral("inpaintPatchWidth"), patch_width);
  target->insert(QStringLiteral("inpaintPatchHeight"), patch_height);
  target->insert(
      QStringLiteral("inpaintPatchColorSpace"),
      geometry.value(QStringLiteral("inpaintPatchColorSpace")).toString(QStringLiteral("sRGB")));
  target->insert(
      QStringLiteral("inpaintPatchSchemaVersion"),
      std::max(1, geometry.value(QStringLiteral("inpaintPatchSchemaVersion")).toInt(1)));
  target->insert(
      QStringLiteral("inpaintModelName"),
      geometry.value(QStringLiteral("inpaintModelName")).toString());
  const auto source_mask_data =
      detail::read_byte_array(geometry, QStringLiteral("inpaintSourceMaskData"));
  const auto source_mask_width = geometry.value(QStringLiteral("inpaintSourceMaskWidth")).toInt();
  const auto source_mask_height = geometry.value(QStringLiteral("inpaintSourceMaskHeight")).toInt();
  if (valid_segmentation_payload(source_mask_data, source_mask_width, source_mask_height)) {
    target->insert(QStringLiteral("inpaintSourceMaskData"), source_mask_data);
    target->insert(QStringLiteral("inpaintSourceMaskWidth"), source_mask_width);
    target->insert(QStringLiteral("inpaintSourceMaskHeight"), source_mask_height);
    const auto source_bounds_width =
        geometry.value(QStringLiteral("inpaintSourceBoundsWidth")).toInt();
    const auto source_bounds_height =
        geometry.value(QStringLiteral("inpaintSourceBoundsHeight")).toInt();
    if (source_bounds_width > 0 && source_bounds_height > 0) {
      target->insert(
          QStringLiteral("inpaintSourceBoundsX"),
          geometry.value(QStringLiteral("inpaintSourceBoundsX")).toInt());
      target->insert(
          QStringLiteral("inpaintSourceBoundsY"),
          geometry.value(QStringLiteral("inpaintSourceBoundsY")).toInt());
      target->insert(QStringLiteral("inpaintSourceBoundsWidth"), source_bounds_width);
      target->insert(QStringLiteral("inpaintSourceBoundsHeight"), source_bounds_height);
    }
  }
  const auto selected_variation_index =
      geometry.contains(QStringLiteral("inpaintSelectedVariationIndex"))
          ? geometry.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt()
          : geometry.value(QStringLiteral("inpaintCandidateIndex")).toInt();
  target->insert(QStringLiteral("inpaintCandidateIndex"), selected_variation_index);
  target->insert(QStringLiteral("inpaintSelectedVariationIndex"), selected_variation_index);
  target->insert(
      QStringLiteral("inpaintSelectedVariationSeed"),
      std::max(
          1,
          geometry.contains(QStringLiteral("inpaintSelectedVariationSeed"))
              ? geometry.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt()
              : geometry.value(QStringLiteral("inpaintSeed")).toInt()));
  target->insert(
      QStringLiteral("inpaintSelectedVariationModelName"),
      geometry.contains(QStringLiteral("inpaintSelectedVariationModelName"))
          ? geometry.value(QStringLiteral("inpaintSelectedVariationModelName")).toString()
          : geometry.value(QStringLiteral("inpaintModelName")).toString());
  const auto source_candidates = geometry.value(QStringLiteral("inpaintCandidates")).toArray();
  if (!source_candidates.isEmpty()) {
    QVariantList candidates;
    for (const auto& candidate_value : source_candidates) {
      const auto candidate_object = candidate_value.toObject();
      const auto candidate_data = detail::read_byte_array(candidate_object, QStringLiteral("patchData"));
      const auto candidate_width = candidate_object.value(QStringLiteral("patchWidth")).toInt();
      const auto candidate_height = candidate_object.value(QStringLiteral("patchHeight")).toInt();
      if (candidate_data.isEmpty() || candidate_width <= 0 || candidate_height <= 0) {
        continue;
      }
      QVariantMap candidate;
      candidate.insert(QStringLiteral("patchData"), candidate_data);
      candidate.insert(QStringLiteral("patchX"), candidate_object.value(QStringLiteral("patchX")).toInt());
      candidate.insert(QStringLiteral("patchY"), candidate_object.value(QStringLiteral("patchY")).toInt());
      candidate.insert(QStringLiteral("patchWidth"), candidate_width);
      candidate.insert(QStringLiteral("patchHeight"), candidate_height);
      candidate.insert(
          QStringLiteral("patchColorSpace"),
          candidate_object.value(QStringLiteral("patchColorSpace")).toString(QStringLiteral("sRGB")));
      candidate.insert(QStringLiteral("modelName"), candidate_object.value(QStringLiteral("modelName")).toString());
      candidate.insert(QStringLiteral("index"), candidate_object.value(QStringLiteral("index")).toInt());
      candidate.insert(
          QStringLiteral("seed"),
          std::max(1, candidate_object.value(QStringLiteral("seed")).toInt()));
      candidates.append(candidate);
    }
    if (!candidates.isEmpty()) {
      target->insert(QStringLiteral("inpaintCandidates"), candidates);
      target->insert(QStringLiteral("inpaintCandidateCount"), candidates.size());
    }
  }
  target->insert(
      QStringLiteral("inpaintPatchPayloadBytes"),
      std::max(0, geometry.value(QStringLiteral("inpaintPatchPayloadBytes")).toInt()));
  target->insert(
      QStringLiteral("inpaintCandidatePayloadBytes"),
      std::max(0, geometry.value(QStringLiteral("inpaintCandidatePayloadBytes")).toInt()));
  target->insert(
      QStringLiteral("inpaintCandidateEvictedCount"),
      std::max(0, geometry.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt()));
  const auto prompt = geometry.value(QStringLiteral("inpaintPrompt")).toString();
  if (!prompt.isEmpty()) {
    target->insert(QStringLiteral("inpaintPrompt"), prompt);
  }
  const auto quality_mode = geometry.value(QStringLiteral("inpaintQualityMode")).toString().trimmed().toLower();
  target->insert(
      QStringLiteral("inpaintQualityMode"),
      quality_mode == QStringLiteral("final") ? QStringLiteral("final") : QStringLiteral("preview"));
  target->insert(
      QStringLiteral("inpaintFinalRequired"),
      geometry.contains(QStringLiteral("inpaintFinalRequired"))
          ? geometry.value(QStringLiteral("inpaintFinalRequired")).toBool()
          : quality_mode != QStringLiteral("final"));
  target->insert(
      QStringLiteral("inpaintSeed"),
      std::max(1, geometry.value(QStringLiteral("inpaintSeed")).toInt()));
  target->insert(
      QStringLiteral("inpaintBlendAmount"),
      qBound(0.0, geometry.value(QStringLiteral("inpaintBlendAmount")).toDouble(1.0), 1.0));
  target->insert(
      QStringLiteral("inpaintSeamFeather"),
      qBound(0.0, geometry.value(QStringLiteral("inpaintSeamFeather")).toDouble(4.0), 24.0));
  target->insert(
      QStringLiteral("inpaintColorMatch"),
      qBound(0.0, geometry.value(QStringLiteral("inpaintColorMatch")).toDouble(0.75), 1.0));
  target->insert(
      QStringLiteral("inpaintFallback"),
      geometry.value(QStringLiteral("inpaintFallback")).toBool(false));
  const auto fallback_reason = geometry.value(QStringLiteral("inpaintFallbackReason")).toString();
  if (!fallback_reason.isEmpty()) {
    target->insert(QStringLiteral("inpaintFallbackReason"), fallback_reason);
  }
  const auto region_kind = geometry.value(QStringLiteral("inpaintRegionKind")).toString();
  if (!region_kind.isEmpty()) {
    target->insert(QStringLiteral("inpaintRegionKind"), region_kind);
    target->insert(
        QStringLiteral("inpaintEdgeContinuation"),
        geometry.value(QStringLiteral("inpaintEdgeContinuation")).toBool(false));
    target->insert(
        QStringLiteral("inpaintTextureSynthesis"),
        geometry.value(QStringLiteral("inpaintTextureSynthesis")).toBool(false));
    target->insert(
        QStringLiteral("inpaintTextureStrength"),
        qBound(0.0, geometry.value(QStringLiteral("inpaintTextureStrength")).toDouble(), 1.0));
    target->insert(
        QStringLiteral("inpaintRegionCoverage"),
        qBound(0.0, geometry.value(QStringLiteral("inpaintRegionCoverage")).toDouble(), 1.0));
    target->insert(QStringLiteral("inpaintRegionX"), geometry.value(QStringLiteral("inpaintRegionX")).toInt());
    target->insert(QStringLiteral("inpaintRegionY"), geometry.value(QStringLiteral("inpaintRegionY")).toInt());
    target->insert(QStringLiteral("inpaintRegionWidth"), geometry.value(QStringLiteral("inpaintRegionWidth")).toInt());
    target->insert(QStringLiteral("inpaintRegionHeight"), geometry.value(QStringLiteral("inpaintRegionHeight")).toInt());
  }
}

inline void read_generated_mask_metadata(QVariantMap* target, const QJsonObject& geometry) {
  if (target == nullptr) {
    return;
  }
  const auto metadata = geometry.value(QStringLiteral("generatedMaskMetadata")).toObject();
  if (!metadata.isEmpty()) {
    target->insert(QStringLiteral("generatedMaskMetadata"), metadata.toVariantMap());
  }
  if (geometry.contains(QStringLiteral("generatedMaskStale"))) {
    target->insert(
        QStringLiteral("generatedMaskStale"),
        geometry.value(QStringLiteral("generatedMaskStale")).toBool(false));
  }
  for (const auto& key : {
           QStringLiteral("generatedAlphaSourceContract"),
           QStringLiteral("generatedAlphaModelName"),
           QStringLiteral("generatedAlphaModelVersion"),
           QStringLiteral("generatedAlphaTargetType"),
           QStringLiteral("generatedAlphaPromptType"),
           QStringLiteral("generatedAlphaSourceImageVersion"),
           QStringLiteral("generatedAlphaRefinementStateSignature"),
           QStringLiteral("generatedAlphaSourceSignature"),
       }) {
    const auto value = geometry.value(key).toString();
    if (!value.isEmpty()) {
      target->insert(key, value);
    }
  }
  for (const auto& key : {
           QStringLiteral("generatedAlphaWidth"),
           QStringLiteral("generatedAlphaHeight"),
           QStringLiteral("generatedAlphaConfidenceWidth"),
           QStringLiteral("generatedAlphaConfidenceHeight"),
       }) {
    if (geometry.contains(key)) {
      target->insert(key, std::max(0, geometry.value(key).toInt()));
    }
  }
  if (geometry.contains(QStringLiteral("generatedAlphaDownscaleFactor"))) {
    target->insert(
        QStringLiteral("generatedAlphaDownscaleFactor"),
        std::max(0.0, geometry.value(QStringLiteral("generatedAlphaDownscaleFactor")).toDouble()));
  }
  if (geometry.contains(QStringLiteral("generatedAlphaConfidenceAvailable"))) {
    target->insert(
        QStringLiteral("generatedAlphaConfidenceAvailable"),
        geometry.value(QStringLiteral("generatedAlphaConfidenceAvailable")).toBool(false));
  }
  const auto confidence_data = detail::read_byte_array(geometry, QStringLiteral("confidenceData"));
  const auto confidence_width = geometry.value(QStringLiteral("confidenceWidth")).toInt();
  const auto confidence_height = geometry.value(QStringLiteral("confidenceHeight")).toInt();
  if (valid_segmentation_payload(confidence_data, confidence_width, confidence_height)) {
    target->insert(QStringLiteral("confidenceData"), confidence_data);
    target->insert(QStringLiteral("confidenceWidth"), confidence_width);
    target->insert(QStringLiteral("confidenceHeight"), confidence_height);
    target->insert(
        QStringLiteral("confidenceMapSchemaVersion"),
        std::max(1, geometry.value(QStringLiteral("confidenceMapSchemaVersion")).toInt(1)));
  }
}

inline void write_inpaint_patch_payload(QJsonObject* geometry, const QVariantMap& mask_definition) {
  if (geometry == nullptr) {
    return;
  }

  const auto patch_data = mask_definition.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto patch_width = mask_definition.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height = mask_definition.value(QStringLiteral("inpaintPatchHeight")).toInt();
  if (patch_data.isEmpty() || patch_width <= 0 || patch_height <= 0) {
    return;
  }

  geometry->insert(
      QStringLiteral("inpaintPatchData"),
      QString::fromLatin1(patch_data.toBase64()));
  geometry->insert(QStringLiteral("inpaintPatchX"), mask_definition.value(QStringLiteral("inpaintPatchX")).toInt());
  geometry->insert(QStringLiteral("inpaintPatchY"), mask_definition.value(QStringLiteral("inpaintPatchY")).toInt());
  geometry->insert(QStringLiteral("inpaintPatchWidth"), patch_width);
  geometry->insert(QStringLiteral("inpaintPatchHeight"), patch_height);
  geometry->insert(
      QStringLiteral("inpaintPatchColorSpace"),
      mask_definition.value(QStringLiteral("inpaintPatchColorSpace"), QStringLiteral("sRGB")).toString());
  geometry->insert(
      QStringLiteral("inpaintPatchSchemaVersion"),
      std::max(1, map_int_value_or(mask_definition, QStringLiteral("inpaintPatchSchemaVersion"), 1)));
  geometry->insert(
      QStringLiteral("inpaintModelName"),
      mask_definition.value(QStringLiteral("inpaintModelName")).toString());
  const auto source_mask_data =
      mask_definition.value(QStringLiteral("inpaintSourceMaskData")).toByteArray();
  const auto source_mask_width = mask_definition.value(QStringLiteral("inpaintSourceMaskWidth")).toInt();
  const auto source_mask_height = mask_definition.value(QStringLiteral("inpaintSourceMaskHeight")).toInt();
  if (valid_segmentation_payload(source_mask_data, source_mask_width, source_mask_height)) {
    geometry->insert(
        QStringLiteral("inpaintSourceMaskData"),
        QString::fromLatin1(source_mask_data.toBase64()));
    geometry->insert(QStringLiteral("inpaintSourceMaskWidth"), source_mask_width);
    geometry->insert(QStringLiteral("inpaintSourceMaskHeight"), source_mask_height);
    const auto source_bounds_width =
        mask_definition.value(QStringLiteral("inpaintSourceBoundsWidth")).toInt();
    const auto source_bounds_height =
        mask_definition.value(QStringLiteral("inpaintSourceBoundsHeight")).toInt();
    if (source_bounds_width > 0 && source_bounds_height > 0) {
      geometry->insert(
          QStringLiteral("inpaintSourceBoundsX"),
          mask_definition.value(QStringLiteral("inpaintSourceBoundsX")).toInt());
      geometry->insert(
          QStringLiteral("inpaintSourceBoundsY"),
          mask_definition.value(QStringLiteral("inpaintSourceBoundsY")).toInt());
      geometry->insert(QStringLiteral("inpaintSourceBoundsWidth"), source_bounds_width);
      geometry->insert(QStringLiteral("inpaintSourceBoundsHeight"), source_bounds_height);
    }
  }
  const auto selected_variation_index =
      mask_definition.contains(QStringLiteral("inpaintSelectedVariationIndex"))
          ? mask_definition.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt()
          : mask_definition.value(QStringLiteral("inpaintCandidateIndex")).toInt();
  geometry->insert(QStringLiteral("inpaintCandidateIndex"), selected_variation_index);
  geometry->insert(QStringLiteral("inpaintSelectedVariationIndex"), selected_variation_index);
  geometry->insert(
      QStringLiteral("inpaintSelectedVariationSeed"),
      std::max(
          1,
          mask_definition.contains(QStringLiteral("inpaintSelectedVariationSeed"))
              ? mask_definition.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt()
              : mask_definition.value(QStringLiteral("inpaintSeed")).toInt()));
  geometry->insert(
      QStringLiteral("inpaintSelectedVariationModelName"),
      mask_definition
          .value(
              QStringLiteral("inpaintSelectedVariationModelName"),
              mask_definition.value(QStringLiteral("inpaintModelName")))
          .toString());
  const auto source_candidates = mask_definition.value(QStringLiteral("inpaintCandidates")).toList();
  if (!source_candidates.isEmpty()) {
    QJsonArray candidates;
    for (const auto& candidate_value : source_candidates) {
      const auto candidate = candidate_value.toMap();
      const auto candidate_data = candidate.value(QStringLiteral("patchData")).toByteArray();
      const auto candidate_width = candidate.value(QStringLiteral("patchWidth")).toInt();
      const auto candidate_height = candidate.value(QStringLiteral("patchHeight")).toInt();
      if (candidate_data.isEmpty() || candidate_width <= 0 || candidate_height <= 0) {
        continue;
      }
      QJsonObject candidate_object;
      candidate_object.insert(QStringLiteral("patchData"), QString::fromLatin1(candidate_data.toBase64()));
      candidate_object.insert(QStringLiteral("patchX"), candidate.value(QStringLiteral("patchX")).toInt());
      candidate_object.insert(QStringLiteral("patchY"), candidate.value(QStringLiteral("patchY")).toInt());
      candidate_object.insert(QStringLiteral("patchWidth"), candidate_width);
      candidate_object.insert(QStringLiteral("patchHeight"), candidate_height);
      candidate_object.insert(
          QStringLiteral("patchColorSpace"),
          candidate.value(QStringLiteral("patchColorSpace"), QStringLiteral("sRGB")).toString());
      candidate_object.insert(QStringLiteral("modelName"), candidate.value(QStringLiteral("modelName")).toString());
      candidate_object.insert(QStringLiteral("index"), candidate.value(QStringLiteral("index")).toInt());
      candidate_object.insert(
          QStringLiteral("seed"),
          std::max(1, candidate.value(QStringLiteral("seed")).toInt()));
      candidates.append(candidate_object);
    }
    if (!candidates.isEmpty()) {
      geometry->insert(QStringLiteral("inpaintCandidates"), candidates);
      geometry->insert(QStringLiteral("inpaintCandidateCount"), candidates.size());
    }
  }
  geometry->insert(
      QStringLiteral("inpaintPatchPayloadBytes"),
      std::max(0, mask_definition.value(QStringLiteral("inpaintPatchPayloadBytes")).toInt()));
  geometry->insert(
      QStringLiteral("inpaintCandidatePayloadBytes"),
      std::max(0, mask_definition.value(QStringLiteral("inpaintCandidatePayloadBytes")).toInt()));
  geometry->insert(
      QStringLiteral("inpaintCandidateEvictedCount"),
      std::max(0, mask_definition.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt()));
  const auto prompt = mask_definition.value(QStringLiteral("inpaintPrompt")).toString();
  if (!prompt.isEmpty()) {
    geometry->insert(QStringLiteral("inpaintPrompt"), prompt);
  }
  const auto quality_mode =
      mask_definition.value(QStringLiteral("inpaintQualityMode")).toString().trimmed().toLower();
  geometry->insert(
      QStringLiteral("inpaintQualityMode"),
      quality_mode == QStringLiteral("final") ? QStringLiteral("final") : QStringLiteral("preview"));
  geometry->insert(
      QStringLiteral("inpaintFinalRequired"),
      mask_definition.contains(QStringLiteral("inpaintFinalRequired"))
          ? mask_definition.value(QStringLiteral("inpaintFinalRequired")).toBool()
          : quality_mode != QStringLiteral("final"));
  geometry->insert(
      QStringLiteral("inpaintSeed"),
      std::max(1, mask_definition.value(QStringLiteral("inpaintSeed")).toInt()));
  geometry->insert(
      QStringLiteral("inpaintBlendAmount"),
      qBound(0.0, mask_definition.value(QStringLiteral("inpaintBlendAmount"), 1.0).toDouble(), 1.0));
  geometry->insert(
      QStringLiteral("inpaintSeamFeather"),
      qBound(0.0, mask_definition.value(QStringLiteral("inpaintSeamFeather"), 4.0).toDouble(), 24.0));
  geometry->insert(
      QStringLiteral("inpaintColorMatch"),
      qBound(0.0, mask_definition.value(QStringLiteral("inpaintColorMatch"), 0.75).toDouble(), 1.0));
  geometry->insert(
      QStringLiteral("inpaintFallback"),
      mask_definition.value(QStringLiteral("inpaintFallback")).toBool());
  const auto fallback_reason =
      mask_definition.value(QStringLiteral("inpaintFallbackReason")).toString();
  if (!fallback_reason.isEmpty()) {
    geometry->insert(QStringLiteral("inpaintFallbackReason"), fallback_reason);
  }
  const auto region_kind = mask_definition.value(QStringLiteral("inpaintRegionKind")).toString();
  if (!region_kind.isEmpty()) {
    geometry->insert(QStringLiteral("inpaintRegionKind"), region_kind);
    geometry->insert(
        QStringLiteral("inpaintEdgeContinuation"),
        mask_definition.value(QStringLiteral("inpaintEdgeContinuation")).toBool());
    geometry->insert(
        QStringLiteral("inpaintTextureSynthesis"),
        mask_definition.value(QStringLiteral("inpaintTextureSynthesis")).toBool());
    geometry->insert(
        QStringLiteral("inpaintTextureStrength"),
        qBound(0.0, mask_definition.value(QStringLiteral("inpaintTextureStrength")).toDouble(), 1.0));
    geometry->insert(
        QStringLiteral("inpaintRegionCoverage"),
        qBound(0.0, mask_definition.value(QStringLiteral("inpaintRegionCoverage")).toDouble(), 1.0));
    geometry->insert(
        QStringLiteral("inpaintRegionX"),
        mask_definition.value(QStringLiteral("inpaintRegionX")).toInt());
    geometry->insert(
        QStringLiteral("inpaintRegionY"),
        mask_definition.value(QStringLiteral("inpaintRegionY")).toInt());
    geometry->insert(
        QStringLiteral("inpaintRegionWidth"),
        mask_definition.value(QStringLiteral("inpaintRegionWidth")).toInt());
    geometry->insert(
        QStringLiteral("inpaintRegionHeight"),
        mask_definition.value(QStringLiteral("inpaintRegionHeight")).toInt());
  }
}

inline void write_generated_mask_metadata(QJsonObject* geometry, const QVariantMap& mask_definition) {
  if (geometry == nullptr) {
    return;
  }
  const auto metadata = mask_definition.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (!metadata.isEmpty()) {
    geometry->insert(QStringLiteral("generatedMaskMetadata"), QJsonObject::fromVariantMap(metadata));
  }
  if (mask_definition.contains(QStringLiteral("generatedMaskStale"))) {
    geometry->insert(
        QStringLiteral("generatedMaskStale"),
        mask_definition.value(QStringLiteral("generatedMaskStale")).toBool());
  }
  for (const auto& key : {
           QStringLiteral("generatedAlphaSourceContract"),
           QStringLiteral("generatedAlphaModelName"),
           QStringLiteral("generatedAlphaModelVersion"),
           QStringLiteral("generatedAlphaTargetType"),
           QStringLiteral("generatedAlphaPromptType"),
           QStringLiteral("generatedAlphaSourceImageVersion"),
           QStringLiteral("generatedAlphaWidth"),
           QStringLiteral("generatedAlphaHeight"),
           QStringLiteral("generatedAlphaDownscaleFactor"),
           QStringLiteral("generatedAlphaConfidenceAvailable"),
           QStringLiteral("generatedAlphaConfidenceWidth"),
           QStringLiteral("generatedAlphaConfidenceHeight"),
           QStringLiteral("generatedAlphaRefinementStateSignature"),
           QStringLiteral("generatedAlphaSourceSignature"),
       }) {
    if (mask_definition.contains(key)) {
      geometry->insert(key, QJsonValue::fromVariant(mask_definition.value(key)));
    }
  }
  const auto confidence_data = mask_definition.value(QStringLiteral("confidenceData")).toByteArray();
  const auto confidence_width = mask_definition.value(QStringLiteral("confidenceWidth")).toInt();
  const auto confidence_height = mask_definition.value(QStringLiteral("confidenceHeight")).toInt();
  if (valid_segmentation_payload(confidence_data, confidence_width, confidence_height)) {
    geometry->insert(
        QStringLiteral("confidenceData"),
        QString::fromLatin1(confidence_data.toBase64()));
    geometry->insert(QStringLiteral("confidenceWidth"), confidence_width);
    geometry->insert(QStringLiteral("confidenceHeight"), confidence_height);
    geometry->insert(
        QStringLiteral("confidenceMapSchemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("confidenceMapSchemaVersion"), 1)));
  }
}

inline QString normalized_mask_kind(const QString& kind) {
  const auto normalized = kind.trimmed().toLower();
  const auto canonical = normalized == QStringLiteral("person") ? QStringLiteral("people") : normalized;
  if (canonical == QStringLiteral("linear") || canonical == QStringLiteral("radial") ||
      canonical == QStringLiteral("ellipse") || canonical == QStringLiteral("rectangle") ||
      canonical == QStringLiteral("brush") || canonical == QStringLiteral("polygon") ||
      canonical == QStringLiteral("lasso") || canonical == QStringLiteral("luminance") ||
      canonical == QStringLiteral("color") || canonical == QStringLiteral("depth") ||
      canonical == QStringLiteral("alpha") ||
      canonical == QStringLiteral("subject") || canonical == QStringLiteral("sky") ||
      canonical == QStringLiteral("background") || canonical == QStringLiteral("people") ||
      canonical == QStringLiteral("face") || canonical == QStringLiteral("hair") ||
      canonical == QStringLiteral("clothing") || canonical == QStringLiteral("object")) {
    return canonical;
  }
  return QStringLiteral("linear");
}

inline bool is_center_radius_mask_kind(const QString& kind) {
  const auto normalized = normalized_mask_kind(kind);
  return normalized == QStringLiteral("radial") || normalized == QStringLiteral("ellipse") ||
         normalized == QStringLiteral("rectangle");
}

inline bool is_ai_segmentation_mask_kind(const QString& kind) {
  const auto normalized = normalized_mask_kind(kind);
  return normalized == QStringLiteral("subject") || normalized == QStringLiteral("sky") ||
         normalized == QStringLiteral("background") || normalized == QStringLiteral("people") ||
         normalized == QStringLiteral("face") || normalized == QStringLiteral("hair") ||
         normalized == QStringLiteral("clothing") || normalized == QStringLiteral("object");
}

inline bool is_raster_mask_kind(const QString& kind) {
  const auto normalized = normalized_mask_kind(kind);
  return normalized == QStringLiteral("alpha") || is_ai_segmentation_mask_kind(normalized);
}

inline QString ai_segmentation_mask_label(const QString& kind) {
  const auto normalized = normalized_mask_kind(kind);
  if (normalized == QStringLiteral("sky")) {
    return QStringLiteral("Sky");
  }
  if (normalized == QStringLiteral("background")) {
    return QStringLiteral("Background");
  }
  if (normalized == QStringLiteral("people")) {
    return QStringLiteral("People");
  }
  if (normalized == QStringLiteral("face")) {
    return QStringLiteral("Face");
  }
  if (normalized == QStringLiteral("hair")) {
    return QStringLiteral("Hair");
  }
  if (normalized == QStringLiteral("clothing")) {
    return QStringLiteral("Clothing");
  }
  if (normalized == QStringLiteral("object")) {
    return QStringLiteral("Object");
  }
  if (normalized == QStringLiteral("alpha")) {
    return QStringLiteral("Alpha");
  }
  return QStringLiteral("Subject");
}

inline QString normalized_mask_operation(const QString& operation) {
  const auto normalized = operation.trimmed().toLower();
  if (normalized == QStringLiteral("add") || normalized == QStringLiteral("subtract") ||
      normalized == QStringLiteral("intersect") || normalized == QStringLiteral("exclude") ||
      normalized == QStringLiteral("replace") || normalized == QStringLiteral("union")) {
    return normalized;
  }
  return QStringLiteral("add");
}

inline QString normalized_brush_stroke_operation(const QString& operation) {
  const auto normalized = operation.trimmed().toLower();
  if (normalized == QStringLiteral("erase")) {
    return normalized;
  }
  return normalized_mask_operation(normalized);
}

inline QString normalized_mask_blend_mode(const QString& blend_mode) {
  const auto normalized = blend_mode.trimmed().toLower();
  if (normalized == QStringLiteral("normal") || normalized == QStringLiteral("multiply") ||
      normalized == QStringLiteral("screen") || normalized == QStringLiteral("overlay") ||
      normalized == QStringLiteral("soft light") || normalized == QStringLiteral("soft-light") ||
      normalized == QStringLiteral("hard light") || normalized == QStringLiteral("hard-light") ||
      normalized == QStringLiteral("color") || normalized == QStringLiteral("luminosity") ||
      normalized == QStringLiteral("hue") || normalized == QStringLiteral("saturation") ||
      normalized == QStringLiteral("difference") || normalized == QStringLiteral("linear light") ||
      normalized == QStringLiteral("linear-light") ||
      normalized == QStringLiteral("lighten") || normalized == QStringLiteral("darken") ||
      normalized == QStringLiteral("replace")) {
    return normalized;
  }
  return QStringLiteral("normal");
}

inline QString normalized_mask_tool_role(const QString& tool_role) {
  const auto normalized = tool_role.trimmed().toLower();
  if (normalized == QStringLiteral("retouch")) {
    return normalized;
  }
  return QStringLiteral("selection");
}

inline bool is_selection_mask_tool_role(const QString& tool_role) {
  return normalized_mask_tool_role(tool_role) == QStringLiteral("selection");
}

inline QString default_mask_set_name(const QString& kind, int ordinal) {
  const auto normalized_kind = normalized_mask_kind(kind);
  QString label = QStringLiteral("Linear");
  if (normalized_kind == QStringLiteral("radial")) {
    label = QStringLiteral("Radial");
  } else if (normalized_kind == QStringLiteral("ellipse")) {
    label = QStringLiteral("Ellipse");
  } else if (normalized_kind == QStringLiteral("rectangle")) {
    label = QStringLiteral("Rectangle");
  } else if (normalized_kind == QStringLiteral("brush")) {
    label = QStringLiteral("Brush");
  } else if (normalized_kind == QStringLiteral("polygon")) {
    label = QStringLiteral("Polygon");
  } else if (normalized_kind == QStringLiteral("lasso")) {
    label = QStringLiteral("Lasso");
  } else if (normalized_kind == QStringLiteral("luminance")) {
    label = QStringLiteral("Luminance");
  } else if (normalized_kind == QStringLiteral("color")) {
    label = QStringLiteral("Color");
  } else if (normalized_kind == QStringLiteral("depth")) {
    label = QStringLiteral("Depth");
  } else if (normalized_kind == QStringLiteral("alpha")) {
    label = QStringLiteral("Alpha");
  } else if (is_ai_segmentation_mask_kind(normalized_kind)) {
    label = ai_segmentation_mask_label(normalized_kind);
  }
  return QStringLiteral("%1 Mask %2").arg(label).arg(ordinal);
}

struct MatteCleanupDefaults final {
  const char* profile = "neutral";
  double smooth = 0.0;
  double fine_edge = 0.0;
  double edge_contrast = 0.0;
  double decontaminate = 0.0;
  double feather = 0.0;
  double harden = 0.0;
  double despeckle = 0.0;
  double fill_holes = 0.0;
  double remove_islands = 0.0;
  double generated_hole_threshold = 0.004;
  double generated_island_threshold = 0.0015;
};

inline MatteCleanupDefaults matte_cleanup_defaults_for_kind(const QString& kind) {
  const auto normalized_kind = normalized_mask_kind(kind);
  if (normalized_kind == QStringLiteral("hair")) {
    return MatteCleanupDefaults{
        .profile = "hair-soft-detail",
        .smooth = 6.0,
        .fine_edge = 65.0,
        .decontaminate = 8.0,
        .feather = 10.0,
        .despeckle = 12.0,
        .fill_holes = 5.0,
        .remove_islands = 8.0,
        .generated_hole_threshold = 0.0025,
        .generated_island_threshold = 0.0010,
    };
  }
  if (normalized_kind == QStringLiteral("sky")) {
    return MatteCleanupDefaults{
        .profile = "sky-large-region",
        .smooth = 4.0,
        .edge_contrast = 10.0,
        .feather = 4.0,
        .despeckle = 8.0,
        .fill_holes = 14.0,
        .remove_islands = 18.0,
        .generated_hole_threshold = 0.0100,
        .generated_island_threshold = 0.0040,
    };
  }
  if (normalized_kind == QStringLiteral("object") ||
      normalized_kind == QStringLiteral("subject") ||
      normalized_kind == QStringLiteral("people") ||
      normalized_kind == QStringLiteral("background")) {
    return MatteCleanupDefaults{
        .profile = normalized_kind == QStringLiteral("object") ? "object-hard-edge"
                                                               : "subject-object",
        .smooth = 3.0,
        .edge_contrast = normalized_kind == QStringLiteral("object") ? 8.0 : 4.0,
        .feather = 4.0,
        .despeckle = 6.0,
        .fill_holes = 8.0,
        .remove_islands = 10.0,
        .generated_hole_threshold = 0.0060,
        .generated_island_threshold = 0.0025,
    };
  }
  if (normalized_kind == QStringLiteral("brush")) {
    return MatteCleanupDefaults{
        .profile = "brush-painted",
        .despeckle = 3.0,
        .fill_holes = 2.0,
        .remove_islands = 2.0,
    };
  }
  if (normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso")) {
    return MatteCleanupDefaults{
        .profile = "manual-shape",
        .smooth = 2.0,
        .edge_contrast = 6.0,
        .harden = 4.0,
    };
  }
  if (normalized_kind == QStringLiteral("alpha")) {
    return MatteCleanupDefaults{
        .profile = "alpha-import",
        .smooth = 3.0,
        .edge_contrast = 8.0,
        .feather = 4.0,
        .despeckle = 10.0,
        .fill_holes = 10.0,
        .remove_islands = 12.0,
        .generated_hole_threshold = 0.0060,
        .generated_island_threshold = 0.0030,
    };
  }
  return {};
}

inline QVariantMap default_mask_definition(const QString& kind, int ordinal = 1) {
  static int unique_suffix = 0;
  ++unique_suffix;
  const auto normalized_kind = normalized_mask_kind(kind);
  const auto matte_cleanup_defaults = matte_cleanup_defaults_for_kind(normalized_kind);
  QVariantMap mask;
  const auto mask_id =
      QStringLiteral("%1-mask-%2-%3").arg(normalized_kind).arg(ordinal).arg(unique_suffix);
  mask.insert(QStringLiteral("id"), mask_id);
  mask.insert(QStringLiteral("schemaVersion"), 1);
  mask.insert(QStringLiteral("sourceSchemaVersion"), 1);
  mask.insert(QStringLiteral("parentId"), QString());
  mask.insert(QStringLiteral("outputId"), QString());
  mask.insert(QStringLiteral("reusableOutput"), false);
  QString label = QStringLiteral("Linear");
  if (normalized_kind == QStringLiteral("radial")) {
    label = QStringLiteral("Radial");
  } else if (normalized_kind == QStringLiteral("ellipse")) {
    label = QStringLiteral("Ellipse");
  } else if (normalized_kind == QStringLiteral("rectangle")) {
    label = QStringLiteral("Rectangle");
  } else if (normalized_kind == QStringLiteral("brush")) {
    label = QStringLiteral("Brush");
  } else if (normalized_kind == QStringLiteral("polygon")) {
    label = QStringLiteral("Polygon");
  } else if (normalized_kind == QStringLiteral("lasso")) {
    label = QStringLiteral("Lasso");
  } else if (normalized_kind == QStringLiteral("luminance")) {
    label = QStringLiteral("Luminance");
  } else if (normalized_kind == QStringLiteral("color")) {
    label = QStringLiteral("Color");
  } else if (normalized_kind == QStringLiteral("depth")) {
    label = QStringLiteral("Depth");
  } else if (normalized_kind == QStringLiteral("alpha")) {
    label = QStringLiteral("Alpha");
  } else if (is_ai_segmentation_mask_kind(normalized_kind)) {
    label = ai_segmentation_mask_label(normalized_kind);
  }
  mask.insert(QStringLiteral("name"), QStringLiteral("%1 %2").arg(label).arg(ordinal));
  mask.insert(QStringLiteral("kind"), normalized_kind);
  mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  mask.insert(QStringLiteral("maskSetId"), QStringLiteral("%1-set-%2-%3").arg(normalized_kind).arg(ordinal).arg(unique_suffix));
  mask.insert(QStringLiteral("maskSetName"), default_mask_set_name(normalized_kind, ordinal));
  mask.insert(QStringLiteral("maskSetSelectionInvert"), false);
  mask.insert(QStringLiteral("maskSetSelectionExpand"), 0.0);
  mask.insert(QStringLiteral("maskSetSelectionSmooth"), 0.0);
  mask.insert(QStringLiteral("maskSetSelectionFineEdge"), 0.0);
  mask.insert(QStringLiteral("maskSetSelectionEdgeContrast"), 0.0);
  mask.insert(QStringLiteral("maskSetDensity"), 1.0);
  mask.insert(QStringLiteral("maskSetOpacity"), 1.0);
  mask.insert(QStringLiteral("maskSetPreviewBypassed"), false);
  mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask.insert(QStringLiteral("enabled"), true);
  mask.insert(QStringLiteral("solo"), false);
  mask.insert(QStringLiteral("locked"), false);
  mask.insert(QStringLiteral("committed"), true);
  mask.insert(QStringLiteral("opacity"), 1.0);
  mask.insert(QStringLiteral("density"), 1.0);
  mask.insert(QStringLiteral("invert"), false);
  mask.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
  mask.insert(QStringLiteral("exposure"), 0.0);
  mask.insert(QStringLiteral("contrast"), 0.0);
  mask.insert(QStringLiteral("highlights"), 0.0);
  mask.insert(QStringLiteral("shadows"), 0.0);
  mask.insert(QStringLiteral("whites"), 0.0);
  mask.insert(QStringLiteral("blacks"), 0.0);
  mask.insert(QStringLiteral("temperature"), 0.0);
  mask.insert(QStringLiteral("tint"), 0.0);
  mask.insert(QStringLiteral("vibrance"), 0.0);
  mask.insert(QStringLiteral("saturation"), 0.0);
  mask.insert(QStringLiteral("hueShift"), 0.0);
  mask.insert(QStringLiteral("saturationShift"), 0.0);
  mask.insert(QStringLiteral("colorizeHue"), 0.0);
  mask.insert(QStringLiteral("colorizeStrength"), 0.0);
  mask.insert(QStringLiteral("removeStrength"), 0.0);
  mask.insert(QStringLiteral("healTextureBlend"), 0.68);
  mask.insert(QStringLiteral("healColorBlend"), 0.46);
  mask.insert(QStringLiteral("retouchMode"), QString());
  mask.insert(QStringLiteral("retouchOrder"), 0);
  mask.insert(QStringLiteral("selectionInvert"), false);
  mask.insert(QStringLiteral("selectionExpand"), 0.0);
  mask.insert(QStringLiteral("selectionSmooth"), matte_cleanup_defaults.smooth);
  mask.insert(QStringLiteral("selectionFineEdge"), matte_cleanup_defaults.fine_edge);
  mask.insert(QStringLiteral("selectionEdgeContrast"), matte_cleanup_defaults.edge_contrast);
  mask.insert(QStringLiteral("selectionDecontaminate"), matte_cleanup_defaults.decontaminate);
  mask.insert(QStringLiteral("selectionGrow"), 0.0);
  mask.insert(QStringLiteral("selectionShrink"), 0.0);
  mask.insert(QStringLiteral("selectionFeather"), matte_cleanup_defaults.feather);
  mask.insert(QStringLiteral("selectionBlur"), 0.0);
  mask.insert(QStringLiteral("selectionHarden"), matte_cleanup_defaults.harden);
  mask.insert(QStringLiteral("selectionDespeckle"), matte_cleanup_defaults.despeckle);
  mask.insert(QStringLiteral("selectionFillHoles"), matte_cleanup_defaults.fill_holes);
  mask.insert(QStringLiteral("selectionRemoveIslands"), matte_cleanup_defaults.remove_islands);
  mask.insert(QStringLiteral("matteCleanupSchemaVersion"), 1);
  mask.insert(QStringLiteral("matteCleanupProfile"), QString::fromLatin1(matte_cleanup_defaults.profile));
  mask.insert(QStringLiteral("matteCleanupDefaultsApplied"), true);
  mask.insert(QStringLiteral("refineLuminanceMin"), 0.0);
  mask.insert(QStringLiteral("refineLuminanceMax"), 1.0);
  mask.insert(QStringLiteral("refineHueCenter"), 0.0);
  mask.insert(QStringLiteral("refineHueRange"), 0.5);
  mask.insert(QStringLiteral("refineSaturationMin"), 0.0);
  mask.insert(QStringLiteral("refineSaturationMax"), 1.0);
  mask.insert(QStringLiteral("refineDepthMin"), 0.0);
  mask.insert(QStringLiteral("refineDepthMax"), 1.0);
  mask.insert(QStringLiteral("refineEdgeConfidenceMin"), 0.0);
  mask.insert(QStringLiteral("refineRangeFeather"), 0.05);
  mask.insert(QStringLiteral("generatedMaskHoleFillThreshold"), matte_cleanup_defaults.generated_hole_threshold);
  mask.insert(QStringLiteral("generatedMaskIslandRemovalThreshold"), matte_cleanup_defaults.generated_island_threshold);
  mask.insert(QStringLiteral("generatedMaskHolesFilled"), 0);
  mask.insert(QStringLiteral("generatedMaskIslandsRemoved"), 0);
  mask.insert(QStringLiteral("generatedMaskPixelsFilled"), 0);
  mask.insert(QStringLiteral("generatedMaskPixelsRemoved"), 0);
  mask.insert(
      QStringLiteral("feather"),
      normalized_kind == QStringLiteral("radial") ? 0.25
      : normalized_kind == QStringLiteral("ellipse") || normalized_kind == QStringLiteral("rectangle") ? 0.08
      : normalized_kind == QStringLiteral("brush") ? 0.75
      : normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso") ? 0.06
      : is_raster_mask_kind(normalized_kind) ? 0.5
      : 0.22);
  mask.insert(QStringLiteral("rotation"), 0.0);
  if (is_center_radius_mask_kind(normalized_kind)) {
    mask.insert(QStringLiteral("centerX"), 0.5);
    mask.insert(QStringLiteral("centerY"), 0.5);
    mask.insert(QStringLiteral("radiusX"), normalized_kind == QStringLiteral("rectangle") ? 0.30 : 0.25);
    mask.insert(QStringLiteral("radiusY"), normalized_kind == QStringLiteral("rectangle") ? 0.20 : 0.25);
  } else if (normalized_kind == QStringLiteral("linear")) {
    mask.insert(QStringLiteral("position"), 0.5);
  } else if (normalized_kind == QStringLiteral("brush")) {
    mask.insert(QStringLiteral("retouchMode"), QString());
    mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Soft Round"));
    mask.insert(QStringLiteral("brushSize"), 0.08);
    mask.insert(QStringLiteral("brushFlow"), 1.0);
    mask.insert(QStringLiteral("brushHardness"), 0.25);
    mask.insert(QStringLiteral("brushSpacing"), 0.75);
    mask.insert(QStringLiteral("brushSmoothing"), 0.0);
    mask.insert(QStringLiteral("brushPressureSize"), 0.65);
    mask.insert(QStringLiteral("brushPressureFlow"), 1.0);
    mask.insert(QStringLiteral("brushPressureOpacity"), 0.0);
    mask.insert(QStringLiteral("brushBuildup"), true);
    mask.insert(QStringLiteral("brushEraseMode"), false);
    mask.insert(QStringLiteral("brushAutoMask"), false);
    mask.insert(QStringLiteral("brushAutoMaskTolerance"), 0.18);
    mask.insert(QStringLiteral("brushAutoMaskContinuousSampling"), false);
    mask.insert(QStringLiteral("brushProtectEdges"), false);
    mask.insert(QStringLiteral("brushRefineMode"), false);
    mask.insert(QStringLiteral("refineBrushRecalculate"), false);
    mask.insert(QStringLiteral("brushRefineTargetId"), QString());
    mask.insert(QStringLiteral("brushRefineTargetName"), QString());
    mask.insert(QStringLiteral("brushRefineTargetKind"), QString());
    mask.insert(QStringLiteral("cloneSourceSet"), false);
    mask.insert(QStringLiteral("cloneSourceX"), 0.5);
    mask.insert(QStringLiteral("cloneSourceY"), 0.5);
    mask.insert(QStringLiteral("cloneOffsetX"), 0.0);
    mask.insert(QStringLiteral("cloneOffsetY"), 0.0);
    mask.insert(QStringLiteral("cloneScale"), 1.0);
    mask.insert(QStringLiteral("cloneRotation"), 0.0);
    mask.insert(QStringLiteral("cloneAligned"), true);
    mask.insert(QStringLiteral("cloneSources"), QVariantList{});
    mask.insert(QStringLiteral("cloneSourceIndex"), -1);
    mask.insert(QStringLiteral("patchSourceSet"), false);
    mask.insert(QStringLiteral("patchSourceX"), 0.5);
    mask.insert(QStringLiteral("patchSourceY"), 0.5);
    mask.insert(QStringLiteral("patchSourceWidth"), 0.18);
    mask.insert(QStringLiteral("patchSourceHeight"), 0.18);
    mask.insert(QStringLiteral("brushPoints"), QVariantList{});
    mask.insert(QStringLiteral("brushStrokes"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso")) {
    mask.insert(QStringLiteral("polygonPoints"), QVariantList{});
    if (normalized_kind == QStringLiteral("lasso")) {
      mask.insert(QStringLiteral("lassoRawPoints"), QVariantList{});
      mask.insert(QStringLiteral("lassoRawPointCount"), 0);
      mask.insert(QStringLiteral("lassoSimplifiedPointCount"), 0);
      mask.insert(QStringLiteral("lassoClosed"), false);
      mask.insert(QStringLiteral("lassoSimplified"), false);
      mask.insert(QStringLiteral("lassoSmoothed"), false);
      mask.insert(QStringLiteral("lassoFreehandPath"), true);
    }
  } else if (normalized_kind == QStringLiteral("luminance")) {
    mask.insert(QStringLiteral("luminanceMin"), 0.2);
    mask.insert(QStringLiteral("luminanceMax"), 0.8);
    mask.insert(QStringLiteral("rangeFeather"), 0.1);
    mask.insert(QStringLiteral("luminanceSamples"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("color")) {
    mask.insert(QStringLiteral("hueCenter"), 0.08);
    mask.insert(QStringLiteral("hueRange"), 0.12);
    mask.insert(QStringLiteral("saturationMin"), 0.1);
    mask.insert(QStringLiteral("saturationMax"), 1.0);
    mask.insert(QStringLiteral("rangeFeather"), 0.08);
    mask.insert(QStringLiteral("colorSamples"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("depth")) {
    mask.insert(QStringLiteral("depthMin"), 0.2);
    mask.insert(QStringLiteral("depthMax"), 0.8);
    mask.insert(QStringLiteral("rangeFeather"), 0.1);
    mask.insert(QStringLiteral("depthSamples"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("alpha")) {
    mask.insert(QStringLiteral("alphaSourcePath"), QString());
    mask.insert(QStringLiteral("alphaImportInvert"), false);
    mask.insert(QStringLiteral("alphaImportThreshold"), 0.0);
    mask.insert(QStringLiteral("alphaImportResampleMode"), QStringLiteral("Original"));
  }
  return mask;
}

inline QVariantList normalized_color_range_samples(const QVariantList& sample_values) {
  QVariantList samples;
  const auto start_index =
      sample_values.size() > qsizetype{5} ? sample_values.size() - qsizetype{5} : qsizetype{0};
  for (auto index = start_index; index < sample_values.size(); ++index) {
    const auto sample = sample_values.at(index).toMap();
    if (sample.isEmpty()) {
      continue;
    }
    QVariantMap normalized_sample;
    normalized_sample.insert(
        QStringLiteral("x"), qBound(0.0, sample.value(QStringLiteral("x")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("y"), qBound(0.0, sample.value(QStringLiteral("y")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("hue"), qBound(0.0, sample.value(QStringLiteral("hue")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("saturation"),
        qBound(0.0, sample.value(QStringLiteral("saturation")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("luminance"),
        qBound(0.0, sample.value(QStringLiteral("luminance")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("red"), qBound(0, sample.value(QStringLiteral("red")).toInt(), 255));
    normalized_sample.insert(
        QStringLiteral("green"), qBound(0, sample.value(QStringLiteral("green")).toInt(), 255));
    normalized_sample.insert(
        QStringLiteral("blue"), qBound(0, sample.value(QStringLiteral("blue")).toInt(), 255));
    normalized_sample.insert(
        QStringLiteral("hex"), sample.value(QStringLiteral("hex")).toString().left(16));
    samples.append(normalized_sample);
  }
  return samples;
}

inline QVariantList normalized_luminance_range_samples(const QVariantList& sample_values) {
  QVariantList samples;
  const auto start_index =
      sample_values.size() > qsizetype{5} ? sample_values.size() - qsizetype{5} : qsizetype{0};
  for (auto index = start_index; index < sample_values.size(); ++index) {
    const auto sample = sample_values.at(index).toMap();
    if (sample.isEmpty()) {
      continue;
    }
    QVariantMap normalized_sample;
    normalized_sample.insert(
        QStringLiteral("x"), qBound(0.0, sample.value(QStringLiteral("x")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("y"), qBound(0.0, sample.value(QStringLiteral("y")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("width"),
        qBound(0.0, sample.value(QStringLiteral("width")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("height"),
        qBound(0.0, sample.value(QStringLiteral("height")).toDouble(), 1.0));
    auto min_value = qBound(
        0.0,
        sample.contains(QStringLiteral("luminanceMin"))
            ? sample.value(QStringLiteral("luminanceMin")).toDouble()
            : 0.2,
        1.0);
    auto max_value = qBound(
        0.0,
        sample.contains(QStringLiteral("luminanceMax"))
            ? sample.value(QStringLiteral("luminanceMax")).toDouble()
            : 0.8,
        1.0);
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    normalized_sample.insert(QStringLiteral("luminanceMin"), min_value);
    normalized_sample.insert(QStringLiteral("luminanceMax"), max_value);
    normalized_sample.insert(
        QStringLiteral("luminanceMean"),
        qBound(
            0.0,
            sample.contains(QStringLiteral("luminanceMean"))
                ? sample.value(QStringLiteral("luminanceMean")).toDouble()
                : (min_value + max_value) * 0.5,
            1.0));
    normalized_sample.insert(
        QStringLiteral("pixelCount"), std::max(0, sample.value(QStringLiteral("pixelCount")).toInt()));
    samples.append(normalized_sample);
  }
  return samples;
}

inline QVariantList normalized_depth_range_samples(const QVariantList& sample_values) {
  QVariantList samples;
  const auto start_index =
      sample_values.size() > qsizetype{5} ? sample_values.size() - qsizetype{5} : qsizetype{0};
  for (auto index = start_index; index < sample_values.size(); ++index) {
    const auto sample = sample_values.at(index).toMap();
    if (sample.isEmpty()) {
      continue;
    }
    QVariantMap normalized_sample;
    normalized_sample.insert(
        QStringLiteral("x"), qBound(0.0, sample.value(QStringLiteral("x")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("y"), qBound(0.0, sample.value(QStringLiteral("y")).toDouble(), 1.0));
    auto min_value = qBound(
        0.0,
        sample.contains(QStringLiteral("depthMin")) ? sample.value(QStringLiteral("depthMin")).toDouble() : 0.2,
        1.0);
    auto max_value = qBound(
        0.0,
        sample.contains(QStringLiteral("depthMax")) ? sample.value(QStringLiteral("depthMax")).toDouble() : 0.8,
        1.0);
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    normalized_sample.insert(QStringLiteral("depthMin"), min_value);
    normalized_sample.insert(QStringLiteral("depthMax"), max_value);
    normalized_sample.insert(
        QStringLiteral("depthMean"),
        qBound(
            0.0,
            sample.contains(QStringLiteral("depthMean"))
                ? sample.value(QStringLiteral("depthMean")).toDouble()
                : (min_value + max_value) * 0.5,
            1.0));
    normalized_sample.insert(
        QStringLiteral("pixelCount"), std::max(0, sample.value(QStringLiteral("pixelCount")).toInt()));
    samples.append(normalized_sample);
  }
  return samples;
}

inline QVariantMap normalized_mask_definition(const QVariantMap& candidate, int ordinal = 1) {
  const auto normalized_kind = normalized_mask_kind(candidate.value(QStringLiteral("kind")).toString());
  QVariantMap mask = default_mask_definition(normalized_kind, ordinal);
  const auto id = candidate.value(QStringLiteral("id")).toString().simplified();
  const auto name = candidate.value(QStringLiteral("name")).toString().simplified();
  mask.insert(
      QStringLiteral("schemaVersion"),
      std::max(1, map_int_value_or(candidate, QStringLiteral("schemaVersion"), 1)));
  mask.insert(
      QStringLiteral("sourceSchemaVersion"),
      std::max(1, map_int_value_or(candidate, QStringLiteral("sourceSchemaVersion"), 1)));
  mask.insert(QStringLiteral("id"), id.isEmpty() ? mask.value(QStringLiteral("id")).toString() : id);
  mask.insert(QStringLiteral("parentId"), candidate.value(QStringLiteral("parentId")).toString().simplified());
  const auto reusable_output = candidate.contains(QStringLiteral("reusableOutput"))
                                   ? candidate.value(QStringLiteral("reusableOutput")).toBool()
                                   : false;
  const auto output_id = candidate.value(QStringLiteral("outputId")).toString().simplified();
  mask.insert(
      QStringLiteral("outputId"),
      output_id.isEmpty() && reusable_output ? mask.value(QStringLiteral("id")).toString()
                                             : output_id);
  mask.insert(
      QStringLiteral("reusableOutput"),
      reusable_output);
  mask.insert(QStringLiteral("name"), name.isEmpty() ? mask.value(QStringLiteral("name")).toString() : name);
  mask.insert(QStringLiteral("kind"), normalized_kind);
  const auto inferred_tool_role = [&]() {
    if (candidate.contains(QStringLiteral("toolRole"))) {
      return normalized_mask_tool_role(candidate.value(QStringLiteral("toolRole")).toString());
    }
    const auto retouch_mode =
        candidate.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
    if (!retouch_mode.isEmpty()) {
      return QStringLiteral("retouch");
    }
    const auto has_selection_adjustments =
        std::abs(candidate.value(QStringLiteral("exposure")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("contrast")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("highlights")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("shadows")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("whites")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("blacks")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("temperature")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("tint")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("vibrance")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("saturation")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("texture")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("clarity")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("dehaze")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("sharpening")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("sharpenRadius"), 1.0).toDouble() - 1.0) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("sharpenDetail")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("sharpenMasking")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("luminanceNr")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("colorNr")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("hueShift")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("saturationShift")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("colorizeHue")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("colorizeStrength")).toDouble()) > 1e-6;
    if (!has_selection_adjustments &&
        std::abs(candidate.value(QStringLiteral("removeStrength")).toDouble()) > 1e-6) {
      return QStringLiteral("retouch");
    }
    return QStringLiteral("selection");
  }();
  mask.insert(QStringLiteral("toolRole"), inferred_tool_role);
  if (is_selection_mask_tool_role(inferred_tool_role)) {
    const auto fallback_set_id = QStringLiteral("%1-set").arg(mask.value(QStringLiteral("id")).toString());
    const auto set_id = candidate.value(QStringLiteral("maskSetId")).toString().simplified();
    const auto set_name = candidate.value(QStringLiteral("maskSetName")).toString().simplified();
    mask.insert(
        QStringLiteral("maskSetId"),
        set_id.isEmpty() ? fallback_set_id : set_id);
    mask.insert(
        QStringLiteral("maskSetName"),
        set_name.isEmpty() ? mask.value(QStringLiteral("name")).toString()
                           : set_name);
  } else {
    mask.insert(QStringLiteral("maskSetId"), QString());
    mask.insert(QStringLiteral("maskSetName"), QString());
  }
  mask.insert(
      QStringLiteral("operation"),
      normalized_mask_operation(candidate.value(QStringLiteral("operation")).toString()));
  mask.insert(
      QStringLiteral("maskSetSelectionInvert"),
      candidate.contains(QStringLiteral("maskSetSelectionInvert"))
          ? candidate.value(QStringLiteral("maskSetSelectionInvert")).toBool()
          : false);
  mask.insert(
      QStringLiteral("maskSetSelectionExpand"),
      qBound(
          -100.0,
          candidate.contains(QStringLiteral("maskSetSelectionExpand"))
              ? candidate.value(QStringLiteral("maskSetSelectionExpand")).toDouble()
              : 0.0,
          100.0));
  mask.insert(
      QStringLiteral("maskSetSelectionSmooth"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetSelectionSmooth"))
              ? candidate.value(QStringLiteral("maskSetSelectionSmooth")).toDouble()
              : 0.0,
          100.0));
  mask.insert(
      QStringLiteral("maskSetSelectionFineEdge"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetSelectionFineEdge"))
              ? candidate.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble()
              : 0.0,
          100.0));
  mask.insert(
      QStringLiteral("maskSetSelectionEdgeContrast"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetSelectionEdgeContrast"))
              ? candidate.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble()
              : 0.0,
          100.0));
  mask.insert(
      QStringLiteral("maskSetDensity"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetDensity"))
              ? candidate.value(QStringLiteral("maskSetDensity")).toDouble()
              : 1.0,
          1.0));
  mask.insert(
      QStringLiteral("maskSetOpacity"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetOpacity"))
              ? candidate.value(QStringLiteral("maskSetOpacity")).toDouble()
              : 1.0,
          1.0));
  mask.insert(
      QStringLiteral("maskSetPreviewBypassed"),
      candidate.contains(QStringLiteral("maskSetPreviewBypassed"))
          ? candidate.value(QStringLiteral("maskSetPreviewBypassed")).toBool()
          : false);
  mask.insert(
      QStringLiteral("enabled"),
      candidate.contains(QStringLiteral("enabled"))
          ? candidate.value(QStringLiteral("enabled")).toBool()
          : true);
  mask.insert(
      QStringLiteral("solo"),
      candidate.contains(QStringLiteral("solo"))
          ? candidate.value(QStringLiteral("solo")).toBool()
          : false);
  mask.insert(
      QStringLiteral("locked"),
      candidate.contains(QStringLiteral("locked"))
          ? candidate.value(QStringLiteral("locked")).toBool()
          : false);
  mask.insert(
      QStringLiteral("committed"),
      candidate.contains(QStringLiteral("committed"))
          ? candidate.value(QStringLiteral("committed")).toBool()
          : true);
  mask.insert(
      QStringLiteral("opacity"),
      qBound(0.0, candidate.contains(QStringLiteral("opacity"))
                       ? candidate.value(QStringLiteral("opacity")).toDouble()
                       : 1.0,
             1.0));
  mask.insert(
      QStringLiteral("density"),
      qBound(0.0, candidate.contains(QStringLiteral("density"))
                       ? candidate.value(QStringLiteral("density")).toDouble()
                       : 1.0,
             1.0));
  const auto mask_inverted = candidate.contains(QStringLiteral("invert"))
                                 ? candidate.value(QStringLiteral("invert")).toBool()
                                 : candidate.value(QStringLiteral("selectionInvert")).toBool();
  mask.insert(QStringLiteral("invert"), mask_inverted);
  mask.insert(
      QStringLiteral("blendMode"),
      normalized_mask_blend_mode(candidate.value(QStringLiteral("blendMode")).toString()));
  mask.insert(QStringLiteral("exposure"), candidate.value(QStringLiteral("exposure")).toDouble());
  mask.insert(QStringLiteral("contrast"), candidate.value(QStringLiteral("contrast")).toDouble());
  mask.insert(QStringLiteral("highlights"), candidate.value(QStringLiteral("highlights")).toDouble());
  mask.insert(QStringLiteral("shadows"), candidate.value(QStringLiteral("shadows")).toDouble());
  mask.insert(QStringLiteral("whites"), candidate.value(QStringLiteral("whites")).toDouble());
  mask.insert(QStringLiteral("blacks"), candidate.value(QStringLiteral("blacks")).toDouble());
  mask.insert(QStringLiteral("temperature"), candidate.value(QStringLiteral("temperature")).toDouble());
  mask.insert(QStringLiteral("tint"), candidate.value(QStringLiteral("tint")).toDouble());
  mask.insert(QStringLiteral("vibrance"), candidate.value(QStringLiteral("vibrance")).toDouble());
  mask.insert(QStringLiteral("saturation"), candidate.value(QStringLiteral("saturation")).toDouble());
  mask.insert(QStringLiteral("texture"), candidate.value(QStringLiteral("texture")).toDouble());
  mask.insert(QStringLiteral("clarity"), candidate.value(QStringLiteral("clarity")).toDouble());
  mask.insert(QStringLiteral("dehaze"), candidate.value(QStringLiteral("dehaze")).toDouble());
  mask.insert(QStringLiteral("sharpening"), candidate.value(QStringLiteral("sharpening")).toDouble());
  mask.insert(
      QStringLiteral("sharpenRadius"),
      candidate.contains(QStringLiteral("sharpenRadius"))
          ? candidate.value(QStringLiteral("sharpenRadius")).toDouble()
          : 1.0);
  mask.insert(QStringLiteral("sharpenDetail"), candidate.value(QStringLiteral("sharpenDetail")).toDouble());
  mask.insert(QStringLiteral("sharpenMasking"), candidate.value(QStringLiteral("sharpenMasking")).toDouble());
  mask.insert(QStringLiteral("luminanceNr"), candidate.value(QStringLiteral("luminanceNr")).toDouble());
  mask.insert(QStringLiteral("colorNr"), candidate.value(QStringLiteral("colorNr")).toDouble());
  mask.insert(QStringLiteral("hueShift"), candidate.value(QStringLiteral("hueShift")).toDouble());
  mask.insert(QStringLiteral("saturationShift"), candidate.value(QStringLiteral("saturationShift")).toDouble());
  mask.insert(
      QStringLiteral("retouchMode"),
      candidate.contains(QStringLiteral("retouchMode"))
          ? candidate.value(QStringLiteral("retouchMode")).toString().trimmed().toLower()
          : QString());
  mask.insert(
      QStringLiteral("retouchOrder"),
      normalized_mask_tool_role(mask.value(QStringLiteral("toolRole")).toString()) == QStringLiteral("retouch")
          ? std::max(1, map_int_value_or(candidate, QStringLiteral("retouchOrder"), ordinal))
          : 0);
  mask.insert(
      QStringLiteral("colorizeHue"),
      candidate.contains(QStringLiteral("colorizeHue"))
          ? candidate.value(QStringLiteral("colorizeHue")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("colorizeStrength"),
      candidate.contains(QStringLiteral("colorizeStrength"))
          ? candidate.value(QStringLiteral("colorizeStrength")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("removeStrength"),
      candidate.contains(QStringLiteral("removeStrength"))
          ? candidate.value(QStringLiteral("removeStrength")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("healTextureBlend"),
      qBound(
          0.0,
          candidate.value(QStringLiteral("healTextureBlend"), mask.value(QStringLiteral("healTextureBlend"))).toDouble(),
          1.0));
  mask.insert(
      QStringLiteral("healColorBlend"),
      qBound(
          0.0,
          candidate.value(QStringLiteral("healColorBlend"), mask.value(QStringLiteral("healColorBlend"))).toDouble(),
          1.0));
  mask.insert(
      QStringLiteral("selectionInvert"),
      candidate.contains(QStringLiteral("selectionInvert"))
          ? candidate.value(QStringLiteral("selectionInvert")).toBool()
          : mask.value(QStringLiteral("invert")).toBool());
  mask.insert(
      QStringLiteral("selectionExpand"),
      candidate.contains(QStringLiteral("selectionExpand"))
          ? candidate.value(QStringLiteral("selectionExpand")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("selectionSmooth"),
      candidate.contains(QStringLiteral("selectionSmooth"))
          ? candidate.value(QStringLiteral("selectionSmooth")).toDouble()
          : matte_cleanup_defaults_for_kind(normalized_kind).smooth);
  mask.insert(
      QStringLiteral("selectionFineEdge"),
      candidate.contains(QStringLiteral("selectionFineEdge"))
          ? candidate.value(QStringLiteral("selectionFineEdge")).toDouble()
          : matte_cleanup_defaults_for_kind(normalized_kind).fine_edge);
  mask.insert(
      QStringLiteral("selectionEdgeContrast"),
      candidate.contains(QStringLiteral("selectionEdgeContrast"))
          ? candidate.value(QStringLiteral("selectionEdgeContrast")).toDouble()
          : matte_cleanup_defaults_for_kind(normalized_kind).edge_contrast);
  mask.insert(
      QStringLiteral("selectionDecontaminate"),
      candidate.contains(QStringLiteral("selectionDecontaminate"))
          ? candidate.value(QStringLiteral("selectionDecontaminate")).toDouble()
          : matte_cleanup_defaults_for_kind(normalized_kind).decontaminate);
  const auto matte_cleanup_defaults = matte_cleanup_defaults_for_kind(normalized_kind);
  const auto refine_percent_value = [&](const QString& key) {
    return qBound(0.0, candidate.contains(key) ? candidate.value(key).toDouble() : mask.value(key).toDouble(), 100.0);
  };
  mask.insert(QStringLiteral("selectionGrow"), refine_percent_value(QStringLiteral("selectionGrow")));
  mask.insert(QStringLiteral("selectionShrink"), refine_percent_value(QStringLiteral("selectionShrink")));
  mask.insert(
      QStringLiteral("selectionFeather"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionFeather"))
                 ? candidate.value(QStringLiteral("selectionFeather")).toDouble()
                 : matte_cleanup_defaults.feather,
             100.0));
  mask.insert(QStringLiteral("selectionBlur"), refine_percent_value(QStringLiteral("selectionBlur")));
  mask.insert(
      QStringLiteral("selectionHarden"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionHarden"))
                 ? candidate.value(QStringLiteral("selectionHarden")).toDouble()
                 : matte_cleanup_defaults.harden,
             100.0));
  mask.insert(
      QStringLiteral("selectionDespeckle"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionDespeckle"))
                 ? candidate.value(QStringLiteral("selectionDespeckle")).toDouble()
                 : matte_cleanup_defaults.despeckle,
             100.0));
  mask.insert(
      QStringLiteral("selectionFillHoles"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionFillHoles"))
                 ? candidate.value(QStringLiteral("selectionFillHoles")).toDouble()
                 : matte_cleanup_defaults.fill_holes,
             100.0));
  mask.insert(
      QStringLiteral("selectionRemoveIslands"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionRemoveIslands"))
                 ? candidate.value(QStringLiteral("selectionRemoveIslands")).toDouble()
                 : matte_cleanup_defaults.remove_islands,
             100.0));
  mask.insert(
      QStringLiteral("matteCleanupSchemaVersion"),
      std::max(1, candidate.value(QStringLiteral("matteCleanupSchemaVersion"), 1).toInt()));
  mask.insert(
      QStringLiteral("matteCleanupProfile"),
      candidate.value(
                   QStringLiteral("matteCleanupProfile"),
                   QString::fromLatin1(matte_cleanup_defaults.profile))
          .toString()
          .simplified());
  mask.insert(
      QStringLiteral("matteCleanupDefaultsApplied"),
      candidate.contains(QStringLiteral("matteCleanupDefaultsApplied"))
          ? candidate.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool()
          : true);
  const auto refine_value = [&](const QString& key, double fallback) {
    return candidate.contains(key) ? candidate.value(key).toDouble() : fallback;
  };
  mask.insert(QStringLiteral("refineLuminanceMin"), qBound(0.0, refine_value(QStringLiteral("refineLuminanceMin"), 0.0), 1.0));
  mask.insert(QStringLiteral("refineLuminanceMax"), qBound(0.0, refine_value(QStringLiteral("refineLuminanceMax"), 1.0), 1.0));
  mask.insert(QStringLiteral("refineHueCenter"), refine_value(QStringLiteral("refineHueCenter"), 0.0));
  mask.insert(QStringLiteral("refineHueRange"), qBound(0.02, refine_value(QStringLiteral("refineHueRange"), 0.5), 0.5));
  mask.insert(QStringLiteral("refineSaturationMin"), qBound(0.0, refine_value(QStringLiteral("refineSaturationMin"), 0.0), 1.0));
  mask.insert(QStringLiteral("refineSaturationMax"), qBound(0.0, refine_value(QStringLiteral("refineSaturationMax"), 1.0), 1.0));
  mask.insert(QStringLiteral("refineDepthMin"), qBound(0.0, refine_value(QStringLiteral("refineDepthMin"), 0.0), 1.0));
  mask.insert(QStringLiteral("refineDepthMax"), qBound(0.0, refine_value(QStringLiteral("refineDepthMax"), 1.0), 1.0));
  mask.insert(QStringLiteral("refineEdgeConfidenceMin"), qBound(0.0, refine_value(QStringLiteral("refineEdgeConfidenceMin"), 0.0), 1.0));
  mask.insert(QStringLiteral("refineRangeFeather"), qBound(0.0, refine_value(QStringLiteral("refineRangeFeather"), 0.05), 0.5));
  mask.insert(
      QStringLiteral("generatedMaskHoleFillThreshold"),
      qBound(
          0.0,
          refine_value(
              QStringLiteral("generatedMaskHoleFillThreshold"),
              matte_cleanup_defaults.generated_hole_threshold),
          0.05));
  mask.insert(
      QStringLiteral("generatedMaskIslandRemovalThreshold"),
      qBound(
          0.0,
          refine_value(
              QStringLiteral("generatedMaskIslandRemovalThreshold"),
              matte_cleanup_defaults.generated_island_threshold),
          0.05));
  mask.insert(
      QStringLiteral("generatedMaskHolesFilled"),
      std::max(0, candidate.value(QStringLiteral("generatedMaskHolesFilled")).toInt()));
  mask.insert(
      QStringLiteral("generatedMaskIslandsRemoved"),
      std::max(0, candidate.value(QStringLiteral("generatedMaskIslandsRemoved")).toInt()));
  mask.insert(
      QStringLiteral("generatedMaskPixelsFilled"),
      std::max(0, candidate.value(QStringLiteral("generatedMaskPixelsFilled")).toInt()));
  mask.insert(
      QStringLiteral("generatedMaskPixelsRemoved"),
      std::max(0, candidate.value(QStringLiteral("generatedMaskPixelsRemoved")).toInt()));
  mask.insert(
      QStringLiteral("feather"),
      candidate.contains(QStringLiteral("feather"))
          ? candidate.value(QStringLiteral("feather")).toDouble()
          : (normalized_kind == QStringLiteral("radial") ? 0.25
             : normalized_kind == QStringLiteral("ellipse") || normalized_kind == QStringLiteral("rectangle") ? 0.08
             : normalized_kind == QStringLiteral("brush") ? 0.75
             : normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso") ? 0.06
             : is_raster_mask_kind(normalized_kind) ? 0.5
             : 0.22));
  mask.insert(QStringLiteral("rotation"), candidate.value(QStringLiteral("rotation")).toDouble());
  if (is_center_radius_mask_kind(normalized_kind)) {
    mask.insert(
        QStringLiteral("centerX"),
        candidate.contains(QStringLiteral("centerX"))
            ? candidate.value(QStringLiteral("centerX")).toDouble()
            : 0.5);
    mask.insert(
        QStringLiteral("centerY"),
        candidate.contains(QStringLiteral("centerY"))
            ? candidate.value(QStringLiteral("centerY")).toDouble()
            : 0.5);
    mask.insert(
        QStringLiteral("radiusX"),
        candidate.contains(QStringLiteral("radiusX"))
            ? candidate.value(QStringLiteral("radiusX")).toDouble()
            : (normalized_kind == QStringLiteral("rectangle") ? 0.30 : 0.25));
    mask.insert(
        QStringLiteral("radiusY"),
        candidate.contains(QStringLiteral("radiusY"))
            ? candidate.value(QStringLiteral("radiusY")).toDouble()
            : (normalized_kind == QStringLiteral("rectangle") ? 0.20 : 0.25));
  } else if (normalized_kind == QStringLiteral("linear")) {
    mask.insert(
        QStringLiteral("position"),
        candidate.contains(QStringLiteral("position"))
            ? candidate.value(QStringLiteral("position")).toDouble()
            : 0.5);
  } else if (normalized_kind == QStringLiteral("brush")) {
    const auto brush_preset = candidate.value(QStringLiteral("brushPreset")).toString().simplified();
    mask.insert(
        QStringLiteral("brushPreset"),
        brush_preset.isEmpty() ? QStringLiteral("Soft Round") : brush_preset);
    mask.insert(
        QStringLiteral("brushSize"),
        candidate.contains(QStringLiteral("brushSize"))
            ? candidate.value(QStringLiteral("brushSize")).toDouble()
            : 0.08);
    mask.insert(
        QStringLiteral("brushFlow"),
        candidate.contains(QStringLiteral("brushFlow"))
            ? candidate.value(QStringLiteral("brushFlow")).toDouble()
            : 1.0);
    mask.insert(
        QStringLiteral("brushHardness"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushHardness"))
                   ? candidate.value(QStringLiteral("brushHardness")).toDouble()
                   : 0.25,
               1.0));
    mask.insert(
        QStringLiteral("brushSpacing"),
        qBound(0.05,
               candidate.contains(QStringLiteral("brushSpacing"))
                   ? candidate.value(QStringLiteral("brushSpacing")).toDouble()
                   : 0.75,
               2.0));
    mask.insert(
        QStringLiteral("brushSmoothing"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushSmoothing"))
                   ? candidate.value(QStringLiteral("brushSmoothing")).toDouble()
                   : 0.0,
               1.0));
    mask.insert(
        QStringLiteral("brushPressureSize"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushPressureSize"))
                   ? candidate.value(QStringLiteral("brushPressureSize")).toDouble()
                   : 0.65,
               1.0));
    mask.insert(
        QStringLiteral("brushPressureFlow"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushPressureFlow"))
                   ? candidate.value(QStringLiteral("brushPressureFlow")).toDouble()
                   : 1.0,
               1.0));
    mask.insert(
        QStringLiteral("brushPressureOpacity"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushPressureOpacity"))
                   ? candidate.value(QStringLiteral("brushPressureOpacity")).toDouble()
                   : 0.0,
               1.0));
    mask.insert(
        QStringLiteral("brushBuildup"),
        candidate.contains(QStringLiteral("brushBuildup"))
            ? candidate.value(QStringLiteral("brushBuildup")).toBool()
            : true);
    mask.insert(
        QStringLiteral("brushEraseMode"),
        candidate.contains(QStringLiteral("brushEraseMode"))
            ? candidate.value(QStringLiteral("brushEraseMode")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushAutoMask"),
        candidate.contains(QStringLiteral("brushAutoMask"))
            ? candidate.value(QStringLiteral("brushAutoMask")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushAutoMaskTolerance"),
        qBound(0.02,
               candidate.contains(QStringLiteral("brushAutoMaskTolerance"))
                   ? candidate.value(QStringLiteral("brushAutoMaskTolerance")).toDouble()
                   : 0.18,
               0.75));
    mask.insert(
        QStringLiteral("brushAutoMaskContinuousSampling"),
        candidate.contains(QStringLiteral("brushAutoMaskContinuousSampling"))
            ? candidate.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushProtectEdges"),
        candidate.contains(QStringLiteral("brushProtectEdges"))
            ? candidate.value(QStringLiteral("brushProtectEdges")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushRefineMode"),
        candidate.contains(QStringLiteral("brushRefineMode"))
            ? candidate.value(QStringLiteral("brushRefineMode")).toBool()
            : false);
    mask.insert(
        QStringLiteral("refineBrushRecalculate"),
        candidate.contains(QStringLiteral("refineBrushRecalculate"))
            ? candidate.value(QStringLiteral("refineBrushRecalculate")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushRefineTargetId"),
        candidate.value(QStringLiteral("brushRefineTargetId")).toString().simplified());
    mask.insert(
        QStringLiteral("brushRefineTargetName"),
        candidate.value(QStringLiteral("brushRefineTargetName")).toString().simplified());
    mask.insert(
        QStringLiteral("brushRefineTargetKind"),
        normalized_mask_kind(candidate.value(QStringLiteral("brushRefineTargetKind")).toString()));
    mask.insert(
        QStringLiteral("cloneSourceSet"),
        candidate.contains(QStringLiteral("cloneSourceSet"))
            ? candidate.value(QStringLiteral("cloneSourceSet")).toBool()
            : false);
    mask.insert(
        QStringLiteral("cloneSourceX"),
        candidate.contains(QStringLiteral("cloneSourceX"))
            ? candidate.value(QStringLiteral("cloneSourceX")).toDouble()
            : 0.5);
    mask.insert(
        QStringLiteral("cloneSourceY"),
        candidate.contains(QStringLiteral("cloneSourceY"))
            ? candidate.value(QStringLiteral("cloneSourceY")).toDouble()
            : 0.5);
    mask.insert(
        QStringLiteral("cloneOffsetX"),
        candidate.contains(QStringLiteral("cloneOffsetX"))
            ? candidate.value(QStringLiteral("cloneOffsetX")).toDouble()
            : 0.0);
    mask.insert(
        QStringLiteral("cloneOffsetY"),
        candidate.contains(QStringLiteral("cloneOffsetY"))
            ? candidate.value(QStringLiteral("cloneOffsetY")).toDouble()
            : 0.0);
    mask.insert(
        QStringLiteral("cloneScale"),
        candidate.contains(QStringLiteral("cloneScale"))
            ? candidate.value(QStringLiteral("cloneScale")).toDouble()
            : 1.0);
    mask.insert(
        QStringLiteral("cloneRotation"),
        candidate.contains(QStringLiteral("cloneRotation"))
            ? candidate.value(QStringLiteral("cloneRotation")).toDouble()
            : 0.0);
    mask.insert(
        QStringLiteral("cloneAligned"),
        candidate.contains(QStringLiteral("cloneAligned"))
            ? candidate.value(QStringLiteral("cloneAligned")).toBool()
            : true);
    QVariantList clone_sources;
    for (const auto& source_value : candidate.value(QStringLiteral("cloneSources")).toList()) {
      const auto source_map = source_value.toMap();
      QVariantMap clone_source;
      clone_source.insert(
          QStringLiteral("name"),
          source_map.value(QStringLiteral("name")).toString().simplified());
      clone_source.insert(
          QStringLiteral("x"),
          std::clamp(source_map.value(QStringLiteral("x"), 0.5).toDouble(), 0.0, 1.0));
      clone_source.insert(
          QStringLiteral("y"),
          std::clamp(source_map.value(QStringLiteral("y"), 0.5).toDouble(), 0.0, 1.0));
      clone_source.insert(
          QStringLiteral("scale"),
          std::clamp(source_map.value(QStringLiteral("scale"), 1.0).toDouble(), 0.25, 4.0));
      clone_source.insert(
          QStringLiteral("rotation"),
          std::clamp(source_map.value(QStringLiteral("rotation")).toDouble(), -180.0, 180.0));
      clone_source.insert(
          QStringLiteral("aligned"),
          source_map.contains(QStringLiteral("aligned"))
              ? source_map.value(QStringLiteral("aligned")).toBool()
              : true);
      clone_sources.append(clone_source);
    }
    mask.insert(QStringLiteral("cloneSources"), clone_sources);
    const auto clone_source_index = candidate.value(QStringLiteral("cloneSourceIndex"), -1).toInt();
    mask.insert(
        QStringLiteral("cloneSourceIndex"),
        clone_source_index >= 0 && clone_source_index < clone_sources.size() ? clone_source_index : -1);
    const auto patch_source_width =
        std::clamp(candidate.value(QStringLiteral("patchSourceWidth"), 0.18).toDouble(), 0.02, 1.0);
    const auto patch_source_height =
        std::clamp(candidate.value(QStringLiteral("patchSourceHeight"), 0.18).toDouble(), 0.02, 1.0);
    mask.insert(
        QStringLiteral("patchSourceSet"),
        candidate.value(QStringLiteral("patchSourceSet"), false).toBool());
    mask.insert(
        QStringLiteral("patchSourceX"),
        std::clamp(candidate.value(QStringLiteral("patchSourceX"), 0.5).toDouble(), 0.0, 1.0 - patch_source_width));
    mask.insert(
        QStringLiteral("patchSourceY"),
        std::clamp(candidate.value(QStringLiteral("patchSourceY"), 0.5).toDouble(), 0.0, 1.0 - patch_source_height));
    mask.insert(QStringLiteral("patchSourceWidth"), patch_source_width);
    mask.insert(QStringLiteral("patchSourceHeight"), patch_source_height);
    QVariantList normalized_points;
    const auto normalize_brush_point = [&](const QVariant& point_value) {
      const auto point = point_value.toMap();
      QVariantMap normalized_point;
      normalized_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
      normalized_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
      normalized_point.insert(
          QStringLiteral("size"),
          point.contains(QStringLiteral("size"))
              ? point.value(QStringLiteral("size")).toDouble()
              : mask.value(QStringLiteral("brushSize")).toDouble());
      normalized_point.insert(
          QStringLiteral("flow"),
          point.contains(QStringLiteral("flow"))
              ? point.value(QStringLiteral("flow")).toDouble()
              : mask.value(QStringLiteral("brushFlow")).toDouble());
      normalized_point.insert(
          QStringLiteral("opacity"),
          qBound(0.0,
                 point.contains(QStringLiteral("opacity"))
                     ? point.value(QStringLiteral("opacity")).toDouble()
                      : 1.0,
                  1.0));
      normalized_point.insert(
          QStringLiteral("autoMaskAnchorX"),
          qBound(0.0,
                 point.contains(QStringLiteral("autoMaskAnchorX"))
                     ? point.value(QStringLiteral("autoMaskAnchorX")).toDouble()
                     : point.value(QStringLiteral("x")).toDouble(),
                 1.0));
      normalized_point.insert(
          QStringLiteral("autoMaskAnchorY"),
          qBound(0.0,
                 point.contains(QStringLiteral("autoMaskAnchorY"))
                     ? point.value(QStringLiteral("autoMaskAnchorY")).toDouble()
                     : point.value(QStringLiteral("y")).toDouble(),
                 1.0));
      normalized_point.insert(
          QStringLiteral("pressure"),
          qBound(0.0,
                 point.contains(QStringLiteral("pressure"))
                     ? point.value(QStringLiteral("pressure")).toDouble()
                     : 1.0,
                 1.0));
      normalized_point.insert(
          QStringLiteral("tiltX"),
          qBound(-1.0,
                 point.contains(QStringLiteral("tiltX")) ? point.value(QStringLiteral("tiltX")).toDouble() : 0.0,
                 1.0));
      normalized_point.insert(
          QStringLiteral("tiltY"),
          qBound(-1.0,
                 point.contains(QStringLiteral("tiltY")) ? point.value(QStringLiteral("tiltY")).toDouble() : 0.0,
                 1.0));
      normalized_point.insert(
          QStringLiteral("erase"),
          point.contains(QStringLiteral("erase")) ? point.value(QStringLiteral("erase")).toBool() : false);
      return normalized_point;
    };
    const auto points = candidate.value(QStringLiteral("brushPoints")).toList();
    for (const auto& point_value : points) {
      const auto normalized_point = normalize_brush_point(point_value);
      normalized_points.append(normalized_point);
    }
    mask.insert(QStringLiteral("brushPoints"), normalized_points);
    QVariantList normalized_strokes;
    const auto normalize_stroke = [&](const QVariant& stroke_value, int fallback_order) {
      const auto stroke_map = stroke_value.toMap();
      QVariantMap stroke;
      const auto id = stroke_map.value(QStringLiteral("id")).toString().simplified();
      const auto order = std::max(
          1,
          stroke_map.contains(QStringLiteral("createdAtOrder"))
              ? stroke_map.value(QStringLiteral("createdAtOrder")).toInt()
              : fallback_order);
      QVariantList samples;
      const auto sample_values = stroke_map.value(QStringLiteral("samples")).toList();
      for (const auto& sample_value : sample_values) {
        samples.append(normalize_brush_point(sample_value));
      }
      if (samples.isEmpty()) {
        return QVariantMap{};
      }
      QVariantMap settings = stroke_map.value(QStringLiteral("settings")).toMap();
      if (settings.isEmpty()) {
        settings.insert(QStringLiteral("preset"), mask.value(QStringLiteral("brushPreset")).toString());
        settings.insert(QStringLiteral("size"), mask.value(QStringLiteral("brushSize")).toDouble());
        settings.insert(QStringLiteral("flow"), mask.value(QStringLiteral("brushFlow")).toDouble());
        settings.insert(QStringLiteral("hardness"), mask.value(QStringLiteral("brushHardness")).toDouble());
        settings.insert(QStringLiteral("spacing"), mask.value(QStringLiteral("brushSpacing")).toDouble());
        settings.insert(QStringLiteral("smoothing"), mask.value(QStringLiteral("brushSmoothing")).toDouble());
        settings.insert(QStringLiteral("pressureSize"), mask.value(QStringLiteral("brushPressureSize")).toDouble());
        settings.insert(QStringLiteral("pressureFlow"), mask.value(QStringLiteral("brushPressureFlow")).toDouble());
        settings.insert(
            QStringLiteral("pressureOpacity"),
            mask.value(QStringLiteral("brushPressureOpacity")).toDouble());
        settings.insert(QStringLiteral("buildup"), mask.value(QStringLiteral("brushBuildup")).toBool());
        settings.insert(QStringLiteral("eraseMode"), mask.value(QStringLiteral("brushEraseMode")).toBool());
        settings.insert(QStringLiteral("autoMask"), mask.value(QStringLiteral("brushAutoMask")).toBool());
        settings.insert(
            QStringLiteral("autoMaskTolerance"),
            mask.value(QStringLiteral("brushAutoMaskTolerance")).toDouble());
        settings.insert(
            QStringLiteral("autoMaskContinuousSampling"),
            mask.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool());
        settings.insert(
            QStringLiteral("protectEdges"),
            mask.value(QStringLiteral("brushProtectEdges")).toBool());
        settings.insert(QStringLiteral("feather"), mask.value(QStringLiteral("feather")).toDouble());
      }
      if (!settings.contains(QStringLiteral("pressureSize"))) {
        settings.insert(QStringLiteral("pressureSize"), mask.value(QStringLiteral("brushPressureSize")).toDouble());
      }
      if (!settings.contains(QStringLiteral("pressureFlow"))) {
        settings.insert(QStringLiteral("pressureFlow"), mask.value(QStringLiteral("brushPressureFlow")).toDouble());
      }
      if (!settings.contains(QStringLiteral("pressureOpacity"))) {
        settings.insert(
            QStringLiteral("pressureOpacity"),
            mask.value(QStringLiteral("brushPressureOpacity")).toDouble());
      }
      if (!settings.contains(QStringLiteral("autoMaskContinuousSampling"))) {
        settings.insert(
            QStringLiteral("autoMaskContinuousSampling"),
            mask.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool());
      }
      if (!settings.contains(QStringLiteral("protectEdges"))) {
        settings.insert(
            QStringLiteral("protectEdges"),
            mask.value(QStringLiteral("brushProtectEdges")).toBool());
      }
      stroke.insert(QStringLiteral("id"), id.isEmpty() ? QStringLiteral("stroke-%1").arg(order) : id);
      stroke.insert(QStringLiteral("createdAtOrder"), order);
      stroke.insert(
          QStringLiteral("operation"),
          normalized_brush_stroke_operation(
              stroke_map.value(QStringLiteral("operation"), mask.value(QStringLiteral("operation"))).toString()));
      stroke.insert(QStringLiteral("sampleCount"), samples.size());
      stroke.insert(QStringLiteral("settings"), settings);
      stroke.insert(QStringLiteral("samples"), samples);
      return stroke;
    };
    const auto stroke_values = candidate.value(QStringLiteral("brushStrokes")).toList();
    for (auto stroke_index = 0; stroke_index < stroke_values.size(); ++stroke_index) {
      const auto normalized_stroke = normalize_stroke(stroke_values.at(stroke_index), stroke_index + 1);
      if (!normalized_stroke.isEmpty()) {
        normalized_strokes.append(normalized_stroke);
      }
    }
    if (normalized_strokes.isEmpty() && !normalized_points.isEmpty()) {
      QVariantMap legacy_stroke;
      legacy_stroke.insert(QStringLiteral("createdAtOrder"), 1);
      legacy_stroke.insert(QStringLiteral("operation"), mask.value(QStringLiteral("operation")).toString());
      legacy_stroke.insert(QStringLiteral("samples"), normalized_points);
      normalized_strokes.append(normalize_stroke(legacy_stroke, 1));
    }
    mask.insert(QStringLiteral("brushStrokes"), normalized_strokes);
    const auto segmentation_data =
        candidate.value(QStringLiteral("segmentationData")).toByteArray();
    const auto segmentation_width =
        candidate.value(QStringLiteral("segmentationWidth")).toInt();
    const auto segmentation_height =
        candidate.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(
            segmentation_data,
            segmentation_width,
            segmentation_height)) {
      mask.insert(QStringLiteral("segmentationData"), segmentation_data);
      mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
      mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      mask.insert(
          QStringLiteral("rasterSchemaVersion"),
          std::max(1, map_int_value_or(candidate, QStringLiteral("rasterSchemaVersion"), 1)));
    }
    if (candidate.contains(QStringLiteral("brushAlphaSourceContract"))) {
      mask.insert(
          QStringLiteral("brushAlphaSourceContract"),
          candidate.value(QStringLiteral("brushAlphaSourceContract")).toString());
      mask.insert(
          QStringLiteral("brushAlphaSourceOfTruth"),
          candidate.value(QStringLiteral("brushAlphaSourceOfTruth")).toString());
      mask.insert(
          QStringLiteral("brushAlphaStrokeCount"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaStrokeCount")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaPointCount"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaPointCount")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaRasterWidth"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaRasterWidth")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaRasterHeight"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaRasterHeight")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaTileCacheKind"),
          candidate.value(QStringLiteral("brushAlphaTileCacheKind")).toString());
      mask.insert(
          QStringLiteral("brushAlphaTileCacheEnabled"),
          candidate.value(QStringLiteral("brushAlphaTileCacheEnabled")).toBool());
      mask.insert(
          QStringLiteral("brushAlphaTileCacheTileSize"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaTileCacheTileSize")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaTileCacheDirtyTileCount"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaTileCacheDirtyTileCount")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaSourceSignature"),
          candidate.value(QStringLiteral("brushAlphaSourceSignature")).toString());
    }
  } else if (normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso")) {
    QVariantList normalized_points;
    const auto points = candidate.value(QStringLiteral("polygonPoints")).toList();
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      QVariantMap normalized_point;
      normalized_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
      normalized_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
      normalized_points.append(normalized_point);
    }
    mask.insert(QStringLiteral("polygonPoints"), normalized_points);
    if (normalized_kind == QStringLiteral("lasso")) {
      QVariantList normalized_raw_points;
      const auto raw_points = candidate.value(QStringLiteral("lassoRawPoints")).toList();
      for (const auto& point_value : raw_points) {
        const auto point = point_value.toMap();
        QVariantMap normalized_point;
        normalized_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        normalized_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        normalized_raw_points.append(normalized_point);
      }
      mask.insert(QStringLiteral("lassoRawPoints"), normalized_raw_points);
      mask.insert(
          QStringLiteral("lassoRawPointCount"),
          std::max(
              0,
              candidate.contains(QStringLiteral("lassoRawPointCount"))
                  ? candidate.value(QStringLiteral("lassoRawPointCount")).toInt()
                  : static_cast<int>(normalized_raw_points.size())));
      mask.insert(
          QStringLiteral("lassoSimplifiedPointCount"),
          std::max(
              0,
              candidate.contains(QStringLiteral("lassoSimplifiedPointCount"))
                  ? candidate.value(QStringLiteral("lassoSimplifiedPointCount")).toInt()
                  : static_cast<int>(normalized_points.size())));
      mask.insert(
          QStringLiteral("lassoClosed"),
          candidate.contains(QStringLiteral("lassoClosed"))
              ? candidate.value(QStringLiteral("lassoClosed")).toBool()
              : false);
      mask.insert(
          QStringLiteral("lassoSimplified"),
          candidate.contains(QStringLiteral("lassoSimplified"))
              ? candidate.value(QStringLiteral("lassoSimplified")).toBool()
              : false);
      mask.insert(
          QStringLiteral("lassoSmoothed"),
          candidate.contains(QStringLiteral("lassoSmoothed"))
              ? candidate.value(QStringLiteral("lassoSmoothed")).toBool()
              : false);
      mask.insert(
          QStringLiteral("lassoFreehandPath"),
          candidate.contains(QStringLiteral("lassoFreehandPath"))
              ? candidate.value(QStringLiteral("lassoFreehandPath")).toBool()
              : true);
    }
    if (candidate.contains(QStringLiteral("shapeAlphaSourceContract"))) {
      mask.insert(
          QStringLiteral("shapeAlphaSourceContract"),
          candidate.value(QStringLiteral("shapeAlphaSourceContract")).toString());
      mask.insert(
          QStringLiteral("shapeAlphaSourceOfTruth"),
          candidate.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString());
      mask.insert(
          QStringLiteral("shapeAlphaPointCount"),
          std::max(0, candidate.value(QStringLiteral("shapeAlphaPointCount")).toInt()));
      mask.insert(
          QStringLiteral("shapeAlphaRawPointCount"),
          std::max(0, candidate.value(QStringLiteral("shapeAlphaRawPointCount")).toInt()));
      mask.insert(
          QStringLiteral("shapeAlphaClosed"),
          candidate.value(QStringLiteral("shapeAlphaClosed")).toBool());
      mask.insert(
          QStringLiteral("shapeAlphaEditable"),
          candidate.value(QStringLiteral("shapeAlphaEditable")).toBool());
      mask.insert(
          QStringLiteral("shapeAlphaTileCacheKind"),
          candidate.value(QStringLiteral("shapeAlphaTileCacheKind")).toString());
      mask.insert(
          QStringLiteral("shapeAlphaTileCacheEnabled"),
          candidate.value(QStringLiteral("shapeAlphaTileCacheEnabled")).toBool());
      mask.insert(
          QStringLiteral("shapeAlphaTileCacheTileSize"),
          std::max(0, candidate.value(QStringLiteral("shapeAlphaTileCacheTileSize")).toInt()));
      mask.insert(
          QStringLiteral("shapeAlphaSourceSignature"),
          candidate.value(QStringLiteral("shapeAlphaSourceSignature")).toString());
    }
  } else if (normalized_kind == QStringLiteral("luminance")) {
    auto min_value = candidate.contains(QStringLiteral("luminanceMin"))
                         ? candidate.value(QStringLiteral("luminanceMin")).toDouble()
                         : 0.2;
    auto max_value = candidate.contains(QStringLiteral("luminanceMax"))
                         ? candidate.value(QStringLiteral("luminanceMax")).toDouble()
                         : 0.8;
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    mask.insert(QStringLiteral("luminanceMin"), min_value);
    mask.insert(QStringLiteral("luminanceMax"), max_value);
    mask.insert(
        QStringLiteral("rangeFeather"),
        candidate.contains(QStringLiteral("rangeFeather"))
            ? candidate.value(QStringLiteral("rangeFeather")).toDouble()
            : 0.1);
    mask.insert(
        QStringLiteral("luminanceSamples"),
        normalized_luminance_range_samples(candidate.value(QStringLiteral("luminanceSamples")).toList()));
  } else if (normalized_kind == QStringLiteral("color")) {
    auto min_value = candidate.contains(QStringLiteral("saturationMin"))
                         ? candidate.value(QStringLiteral("saturationMin")).toDouble()
                         : 0.1;
    auto max_value = candidate.contains(QStringLiteral("saturationMax"))
                         ? candidate.value(QStringLiteral("saturationMax")).toDouble()
                         : 1.0;
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    mask.insert(
        QStringLiteral("hueCenter"),
        candidate.contains(QStringLiteral("hueCenter"))
            ? candidate.value(QStringLiteral("hueCenter")).toDouble()
            : 0.08);
    mask.insert(
        QStringLiteral("hueRange"),
        candidate.contains(QStringLiteral("hueRange"))
            ? candidate.value(QStringLiteral("hueRange")).toDouble()
            : 0.12);
    mask.insert(QStringLiteral("saturationMin"), min_value);
    mask.insert(QStringLiteral("saturationMax"), max_value);
    mask.insert(
        QStringLiteral("rangeFeather"),
        candidate.contains(QStringLiteral("rangeFeather"))
            ? candidate.value(QStringLiteral("rangeFeather")).toDouble()
            : 0.08);
    mask.insert(
        QStringLiteral("colorSamples"),
        normalized_color_range_samples(candidate.value(QStringLiteral("colorSamples")).toList()));
  } else if (normalized_kind == QStringLiteral("depth")) {
    auto min_value = candidate.contains(QStringLiteral("depthMin"))
                         ? candidate.value(QStringLiteral("depthMin")).toDouble()
                         : 0.2;
    auto max_value = candidate.contains(QStringLiteral("depthMax"))
                         ? candidate.value(QStringLiteral("depthMax")).toDouble()
                         : 0.8;
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    mask.insert(QStringLiteral("depthMin"), min_value);
    mask.insert(QStringLiteral("depthMax"), max_value);
    mask.insert(
        QStringLiteral("rangeFeather"),
        candidate.contains(QStringLiteral("rangeFeather"))
            ? candidate.value(QStringLiteral("rangeFeather")).toDouble()
            : 0.1);
    mask.insert(
        QStringLiteral("depthSamples"),
        normalized_depth_range_samples(candidate.value(QStringLiteral("depthSamples")).toList()));
    const auto depth_data = candidate.value(QStringLiteral("depthData")).toByteArray();
    const auto depth_width = candidate.value(QStringLiteral("depthWidth")).toInt();
    const auto depth_height = candidate.value(QStringLiteral("depthHeight")).toInt();
    if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
      mask.insert(QStringLiteral("depthData"), depth_data);
      mask.insert(QStringLiteral("depthWidth"), depth_width);
      mask.insert(QStringLiteral("depthHeight"), depth_height);
    }
  } else if (is_raster_mask_kind(normalized_kind) || normalized_kind == QStringLiteral("brush")) {
    if (normalized_kind == QStringLiteral("alpha")) {
      mask.insert(
          QStringLiteral("alphaSourcePath"),
          candidate.value(QStringLiteral("alphaSourcePath")).toString());
      mask.insert(
          QStringLiteral("alphaImportInvert"),
          candidate.contains(QStringLiteral("alphaImportInvert"))
              ? candidate.value(QStringLiteral("alphaImportInvert")).toBool()
              : false);
      mask.insert(
          QStringLiteral("alphaImportThreshold"),
          qBound(0.0, candidate.value(QStringLiteral("alphaImportThreshold")).toDouble(), 1.0));
      const auto resample_mode =
          candidate.value(QStringLiteral("alphaImportResampleMode")).toString().simplified();
      mask.insert(
          QStringLiteral("alphaImportResampleMode"),
          resample_mode == QStringLiteral("Match Source") ? resample_mode : QStringLiteral("Original"));
    }
    const auto segmentation_data = candidate.value(QStringLiteral("segmentationData")).toByteArray();
    const auto segmentation_width = candidate.value(QStringLiteral("segmentationWidth")).toInt();
    const auto segmentation_height = candidate.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
      mask.insert(QStringLiteral("segmentationData"), segmentation_data);
      mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
      mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      mask.insert(
          QStringLiteral("rasterSchemaVersion"),
          std::max(1, map_int_value_or(candidate, QStringLiteral("rasterSchemaVersion"), 1)));
      const auto cleanup_source_data =
          candidate.value(QStringLiteral("generatedMaskCleanupSourceData")).toByteArray();
      const auto cleanup_source_width =
          candidate.value(QStringLiteral("generatedMaskCleanupSourceWidth")).toInt();
      const auto cleanup_source_height =
          candidate.value(QStringLiteral("generatedMaskCleanupSourceHeight")).toInt();
      if (valid_segmentation_payload(cleanup_source_data, cleanup_source_width, cleanup_source_height) &&
          cleanup_source_width == segmentation_width &&
          cleanup_source_height == segmentation_height) {
        mask.insert(QStringLiteral("generatedMaskCleanupSourceData"), cleanup_source_data);
        mask.insert(QStringLiteral("generatedMaskCleanupSourceWidth"), cleanup_source_width);
        mask.insert(QStringLiteral("generatedMaskCleanupSourceHeight"), cleanup_source_height);
      }
    }
  }
  copy_inpaint_patch_payload(&mask, candidate);
  copy_generated_mask_metadata(&mask, candidate);
  return mask;
}

inline bool mask_definition_affects_render(const QVariantMap& candidate, int ordinal = 1) {
  const auto mask = normalized_mask_definition(candidate, ordinal);
  return !(mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch") &&
           !mask.value(QStringLiteral("committed")).toBool());
}

inline void copy_selection_adjustments(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr) {
    return;
  }
  target->insert(QStringLiteral("exposure"), source.value(QStringLiteral("exposure")).toDouble());
  target->insert(QStringLiteral("contrast"), source.value(QStringLiteral("contrast")).toDouble());
  target->insert(QStringLiteral("highlights"), source.value(QStringLiteral("highlights")).toDouble());
  target->insert(QStringLiteral("shadows"), source.value(QStringLiteral("shadows")).toDouble());
  target->insert(QStringLiteral("whites"), source.value(QStringLiteral("whites")).toDouble());
  target->insert(QStringLiteral("blacks"), source.value(QStringLiteral("blacks")).toDouble());
  target->insert(QStringLiteral("temperature"), source.value(QStringLiteral("temperature")).toDouble());
  target->insert(QStringLiteral("tint"), source.value(QStringLiteral("tint")).toDouble());
  target->insert(QStringLiteral("vibrance"), source.value(QStringLiteral("vibrance")).toDouble());
  target->insert(QStringLiteral("saturation"), source.value(QStringLiteral("saturation")).toDouble());
  target->insert(QStringLiteral("texture"), source.value(QStringLiteral("texture")).toDouble());
  target->insert(QStringLiteral("clarity"), source.value(QStringLiteral("clarity")).toDouble());
  target->insert(QStringLiteral("dehaze"), source.value(QStringLiteral("dehaze")).toDouble());
  target->insert(QStringLiteral("sharpening"), source.value(QStringLiteral("sharpening")).toDouble());
  target->insert(QStringLiteral("sharpenRadius"), source.value(QStringLiteral("sharpenRadius"), 1.0).toDouble());
  target->insert(QStringLiteral("sharpenDetail"), source.value(QStringLiteral("sharpenDetail")).toDouble());
  target->insert(QStringLiteral("sharpenMasking"), source.value(QStringLiteral("sharpenMasking")).toDouble());
  target->insert(QStringLiteral("luminanceNr"), source.value(QStringLiteral("luminanceNr")).toDouble());
  target->insert(QStringLiteral("colorNr"), source.value(QStringLiteral("colorNr")).toDouble());
  target->insert(QStringLiteral("hueShift"), source.value(QStringLiteral("hueShift")).toDouble());
  target->insert(
      QStringLiteral("saturationShift"),
      source.value(QStringLiteral("saturationShift")).toDouble());
  target->insert(QStringLiteral("colorizeHue"), source.value(QStringLiteral("colorizeHue")).toDouble());
  target->insert(
      QStringLiteral("colorizeStrength"),
      source.value(QStringLiteral("colorizeStrength")).toDouble());
  target->insert(
      QStringLiteral("selectionEdgeContrast"),
      source.value(QStringLiteral("selectionEdgeContrast")).toDouble());
  target->insert(
      QStringLiteral("selectionSmooth"),
      source.value(QStringLiteral("selectionSmooth")).toDouble());
  target->insert(
      QStringLiteral("selectionFineEdge"),
      source.value(QStringLiteral("selectionFineEdge")).toDouble());
  target->insert(
      QStringLiteral("selectionDecontaminate"),
      source.value(QStringLiteral("selectionDecontaminate")).toDouble());
  target->insert(QStringLiteral("selectionGrow"), source.value(QStringLiteral("selectionGrow")).toDouble());
  target->insert(QStringLiteral("selectionShrink"), source.value(QStringLiteral("selectionShrink")).toDouble());
  target->insert(QStringLiteral("selectionFeather"), source.value(QStringLiteral("selectionFeather")).toDouble());
  target->insert(QStringLiteral("selectionBlur"), source.value(QStringLiteral("selectionBlur")).toDouble());
  target->insert(QStringLiteral("selectionHarden"), source.value(QStringLiteral("selectionHarden")).toDouble());
  target->insert(QStringLiteral("selectionDespeckle"), source.value(QStringLiteral("selectionDespeckle")).toDouble());
  target->insert(QStringLiteral("selectionFillHoles"), source.value(QStringLiteral("selectionFillHoles")).toDouble());
  target->insert(
      QStringLiteral("selectionRemoveIslands"),
      source.value(QStringLiteral("selectionRemoveIslands")).toDouble());
  target->insert(
      QStringLiteral("maskSetSelectionInvert"),
      source.value(QStringLiteral("maskSetSelectionInvert")).toBool());
  target->insert(
      QStringLiteral("maskSetSelectionExpand"),
      source.value(QStringLiteral("maskSetSelectionExpand")).toDouble());
  target->insert(
      QStringLiteral("maskSetSelectionSmooth"),
      source.value(QStringLiteral("maskSetSelectionSmooth")).toDouble());
  target->insert(
      QStringLiteral("maskSetSelectionFineEdge"),
      source.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble());
  target->insert(
      QStringLiteral("maskSetSelectionEdgeContrast"),
      source.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble());
  target->insert(QStringLiteral("maskSetDensity"), source.value(QStringLiteral("maskSetDensity"), 1.0).toDouble());
  target->insert(QStringLiteral("maskSetOpacity"), source.value(QStringLiteral("maskSetOpacity"), 1.0).toDouble());
}

inline QStringList mask_set_adjustment_keys() {
  return {
      QStringLiteral("exposure"),
      QStringLiteral("contrast"),
      QStringLiteral("highlights"),
      QStringLiteral("shadows"),
      QStringLiteral("whites"),
      QStringLiteral("blacks"),
      QStringLiteral("temperature"),
      QStringLiteral("tint"),
      QStringLiteral("vibrance"),
      QStringLiteral("saturation"),
      QStringLiteral("texture"),
      QStringLiteral("clarity"),
      QStringLiteral("dehaze"),
      QStringLiteral("sharpening"),
      QStringLiteral("sharpenRadius"),
      QStringLiteral("sharpenDetail"),
      QStringLiteral("sharpenMasking"),
      QStringLiteral("luminanceNr"),
      QStringLiteral("colorNr"),
      QStringLiteral("hueShift"),
      QStringLiteral("saturationShift"),
      QStringLiteral("colorizeHue"),
      QStringLiteral("colorizeStrength"),
      QStringLiteral("selectionEdgeContrast"),
      QStringLiteral("selectionSmooth"),
      QStringLiteral("selectionFineEdge"),
      QStringLiteral("selectionDecontaminate"),
      QStringLiteral("selectionGrow"),
      QStringLiteral("selectionShrink"),
      QStringLiteral("selectionFeather"),
      QStringLiteral("selectionBlur"),
      QStringLiteral("selectionHarden"),
      QStringLiteral("selectionDespeckle"),
      QStringLiteral("selectionFillHoles"),
      QStringLiteral("selectionRemoveIslands"),
      QStringLiteral("maskSetSelectionInvert"),
      QStringLiteral("maskSetSelectionExpand"),
      QStringLiteral("maskSetSelectionSmooth"),
      QStringLiteral("maskSetSelectionFineEdge"),
      QStringLiteral("maskSetSelectionEdgeContrast"),
      QStringLiteral("maskSetDensity"),
      QStringLiteral("maskSetOpacity"),
  };
}

inline bool mask_component_matte_key(const QString& key) {
  return key == QStringLiteral("segmentationData") ||
         key == QStringLiteral("segmentationWidth") ||
         key == QStringLiteral("segmentationHeight") ||
         key == QStringLiteral("rasterSchemaVersion") ||
         key == QStringLiteral("depthData") ||
         key == QStringLiteral("depthWidth") ||
         key == QStringLiteral("depthHeight") ||
         key == QStringLiteral("generatedMaskMetadata") ||
         key == QStringLiteral("generatedMaskConfidenceData") ||
         key == QStringLiteral("generatedMaskConfidenceWidth") ||
         key == QStringLiteral("generatedMaskConfidenceHeight") ||
         key == QStringLiteral("generatedMaskQualityReport") ||
         key == QStringLiteral("generatedMaskStale") ||
         key == QStringLiteral("generatedAlphaSourceContract") ||
         key == QStringLiteral("generatedAlphaModelName") ||
         key == QStringLiteral("generatedAlphaModelVersion") ||
         key == QStringLiteral("generatedAlphaTargetType") ||
         key == QStringLiteral("generatedAlphaPromptType") ||
         key == QStringLiteral("generatedAlphaSourceImageVersion") ||
         key == QStringLiteral("generatedAlphaWidth") ||
         key == QStringLiteral("generatedAlphaHeight") ||
         key == QStringLiteral("generatedAlphaDownscaleFactor") ||
         key == QStringLiteral("generatedAlphaConfidenceAvailable") ||
         key == QStringLiteral("generatedAlphaConfidenceWidth") ||
         key == QStringLiteral("generatedAlphaConfidenceHeight") ||
         key == QStringLiteral("generatedAlphaRefinementStateSignature") ||
         key == QStringLiteral("generatedAlphaSourceSignature");
}

inline bool mask_component_constraint_key(const QString& key) {
  return key == QStringLiteral("refineLuminanceMin") ||
         key == QStringLiteral("refineLuminanceMax") ||
         key == QStringLiteral("refineHueCenter") ||
         key == QStringLiteral("refineHueRange") ||
         key == QStringLiteral("refineSaturationMin") ||
         key == QStringLiteral("refineSaturationMax") ||
         key == QStringLiteral("refineDepthMin") ||
         key == QStringLiteral("refineDepthMax") ||
         key == QStringLiteral("refineEdgeConfidenceMin") ||
         key == QStringLiteral("refineRangeFeather") ||
         key == QStringLiteral("generatedMaskHoleFillThreshold") ||
         key == QStringLiteral("generatedMaskIslandRemovalThreshold") ||
         key == QStringLiteral("generatedMaskHolesFilled") ||
         key == QStringLiteral("generatedMaskIslandsRemoved") ||
         key == QStringLiteral("generatedMaskPixelsFilled") ||
         key == QStringLiteral("generatedMaskPixelsRemoved") ||
         key == QStringLiteral("matteCleanupSchemaVersion") ||
         key == QStringLiteral("matteCleanupProfile") ||
         key == QStringLiteral("matteCleanupDefaultsApplied") ||
         key == QStringLiteral("luminanceMin") ||
         key == QStringLiteral("luminanceMax") ||
         key == QStringLiteral("hueCenter") ||
         key == QStringLiteral("hueRange") ||
         key == QStringLiteral("saturationMin") ||
         key == QStringLiteral("saturationMax") ||
         key == QStringLiteral("depthMin") ||
         key == QStringLiteral("depthMax") ||
         key == QStringLiteral("rangeFeather") ||
         key == QStringLiteral("luminanceSamples") ||
         key == QStringLiteral("colorSamples") ||
         key == QStringLiteral("depthSamples");
}

inline bool mask_component_metadata_key(const QString& key) {
  return key == QStringLiteral("id") ||
         key == QStringLiteral("schemaVersion") ||
         key == QStringLiteral("sourceSchemaVersion") ||
         key == QStringLiteral("parentId") ||
         key == QStringLiteral("outputId") ||
         key == QStringLiteral("reusableOutput") ||
         key == QStringLiteral("name") ||
         key == QStringLiteral("kind") ||
         key == QStringLiteral("toolRole") ||
         key == QStringLiteral("operation") ||
         key == QStringLiteral("enabled") ||
         key == QStringLiteral("solo") ||
         key == QStringLiteral("locked") ||
         key == QStringLiteral("committed") ||
         key == QStringLiteral("retouchOrder") ||
         key == QStringLiteral("opacity") ||
         key == QStringLiteral("density") ||
         key == QStringLiteral("invert") ||
         key == QStringLiteral("blendMode") ||
         key == QStringLiteral("maskSetId") ||
         key == QStringLiteral("maskSetName");
}

struct MaskSourceModel final {
  int flat_index = -1;
  QVariantMap metadata;
  QVariantMap adjustments;
  QVariantMap geometry;
  QVariantMap constraints;
  QVariantMap matte;

  [[nodiscard]] QVariantMap toVariantMap() const {
    QVariantMap model;
    model.insert(QStringLiteral("modelSchemaVersion"), 1);
    model.insert(QStringLiteral("flatIndex"), flat_index);
    model.insert(QStringLiteral("metadata"), metadata);
    model.insert(QStringLiteral("adjustments"), adjustments);
    model.insert(QStringLiteral("geometry"), geometry);
    model.insert(QStringLiteral("constraints"), constraints);
    model.insert(QStringLiteral("matte"), matte);
    return model;
  }

  [[nodiscard]] QVariantMap toLegacyVariantMap() const {
    QVariantMap mask;
    const auto append_bucket = [&mask](const QVariantMap& bucket) {
      for (auto iterator = bucket.cbegin(); iterator != bucket.cend(); ++iterator) {
        mask.insert(iterator.key(), iterator.value());
      }
    };
    append_bucket(metadata);
    append_bucket(adjustments);
    append_bucket(geometry);
    append_bucket(constraints);
    append_bucket(matte);
    return mask;
  }

  [[nodiscard]] QJsonObject toJsonObject() const {
    QJsonObject object;
    object.insert(QStringLiteral("modelSchemaVersion"), 1);
    object.insert(QStringLiteral("flatIndex"), flat_index);
    object.insert(QStringLiteral("metadata"), jsonBucket(metadata));
    object.insert(QStringLiteral("adjustments"), jsonBucket(adjustments));
    object.insert(QStringLiteral("geometry"), jsonBucket(geometry));
    object.insert(QStringLiteral("constraints"), jsonBucket(constraints));
    object.insert(QStringLiteral("matte"), jsonBucket(matte));
    return object;
  }

  [[nodiscard]] static MaskSourceModel fromDefinition(
      const QVariantMap& mask_definition,
      int flat_index) {
    MaskSourceModel model;
    model.flat_index = flat_index;
    copy_selection_adjustments(&model.adjustments, mask_definition);
    const auto adjustment_keys = mask_set_adjustment_keys();
    for (auto iterator = mask_definition.cbegin(); iterator != mask_definition.cend(); ++iterator) {
      if (adjustment_keys.contains(iterator.key())) {
        continue;
      }
      if (mask_component_matte_key(iterator.key())) {
        model.matte.insert(iterator.key(), iterator.value());
      } else if (mask_component_constraint_key(iterator.key())) {
        model.constraints.insert(iterator.key(), iterator.value());
      } else if (mask_component_metadata_key(iterator.key())) {
        model.metadata.insert(iterator.key(), iterator.value());
      } else {
        model.geometry.insert(iterator.key(), iterator.value());
      }
    }
    return model;
  }

  [[nodiscard]] static MaskSourceModel fromVariantMap(
      const QVariantMap& candidate,
      int ordinal = 1) {
    return fromDefinition(normalized_mask_definition(candidate, ordinal), ordinal - 1);
  }

 private:
  [[nodiscard]] static QJsonObject jsonBucket(const QVariantMap& bucket) {
    QJsonObject object;
    for (auto iterator = bucket.cbegin(); iterator != bucket.cend(); ++iterator) {
      if (iterator.value().metaType().id() == QMetaType::QByteArray) {
        object.insert(iterator.key(), QString::fromLatin1(iterator.value().toByteArray().toBase64()));
      } else {
        object.insert(iterator.key(), QJsonValue::fromVariant(iterator.value()));
      }
    }
    return object;
  }
};

struct MaskSourceComponentModel final {
  int flat_index = -1;
  QVariantMap metadata;
  QVariantMap geometry;
  QVariantMap constraints;
  QVariantMap matte;
  QVariantMap adjustment_overrides;

  [[nodiscard]] QVariantMap toVariantMap() const {
    QVariantMap component;
    component.insert(QStringLiteral("flatIndex"), flat_index);
    component.insert(QStringLiteral("metadata"), metadata);
    component.insert(QStringLiteral("geometry"), geometry);
    component.insert(QStringLiteral("constraints"), constraints);
    component.insert(QStringLiteral("matte"), matte);
    component.insert(QStringLiteral("adjustmentOverrides"), adjustment_overrides);
    return component;
  }
};

struct MaskSetModel final {
  QString id;
  QString name;
  QVariantMap adjustments;
  std::vector<MaskSourceComponentModel> components;

  [[nodiscard]] QVariantMap toVariantMap() const {
    QVariantList component_values;
    component_values.reserve(static_cast<int>(components.size()));
    for (const auto& component : components) {
      component_values.append(component.toVariantMap());
    }
    QVariantMap model;
    model.insert(QStringLiteral("schemaVersion"), 2);
    model.insert(QStringLiteral("id"), id);
    model.insert(QStringLiteral("name"), name);
    model.insert(QStringLiteral("adjustments"), adjustments);
    model.insert(QStringLiteral("components"), component_values);
    return model;
  }
};

inline QVariantMap mask_set_adjustments_from_definition(const QVariantMap& mask_definition) {
  QVariantMap adjustments;
  copy_selection_adjustments(&adjustments, mask_definition);
  return adjustments;
}

inline bool mask_adjustment_values_equal(const QVariant& left, const QVariant& right) {
  if (left.userType() == QMetaType::Bool || right.userType() == QMetaType::Bool) {
    return left.toBool() == right.toBool();
  }
  if (left.canConvert<double>() && right.canConvert<double>()) {
    return std::abs(left.toDouble() - right.toDouble()) < 1e-9;
  }
  return left.toString() == right.toString();
}

inline QVariantMap mask_source_adjustment_overrides(
    const QVariantMap& mask_definition,
    const QVariantMap& set_adjustments) {
  QVariantMap overrides;
  const auto adjustment_keys = mask_set_adjustment_keys();
  for (const auto& key : adjustment_keys) {
    if (!mask_definition.contains(key) || !set_adjustments.contains(key)) {
      continue;
    }
    if (!mask_adjustment_values_equal(mask_definition.value(key), set_adjustments.value(key))) {
      overrides.insert(key, mask_definition.value(key));
    }
  }
  return overrides;
}

inline MaskSourceComponentModel mask_source_component_from_definition(
    const QVariantMap& mask_definition,
    int flat_index,
    const QVariantMap& set_adjustments) {
  MaskSourceComponentModel component;
  const auto source_model = MaskSourceModel::fromDefinition(mask_definition, flat_index);
  component.flat_index = source_model.flat_index;
  component.metadata = source_model.metadata;
  component.geometry = source_model.geometry;
  component.constraints = source_model.constraints;
  component.matte = source_model.matte;
  component.adjustment_overrides =
      mask_source_adjustment_overrides(mask_definition, set_adjustments);
  return component;
}

inline std::vector<MaskSourceModel> typed_mask_source_models_from_stack(const QVariantList& stack) {
  std::vector<MaskSourceModel> models;
  models.reserve(static_cast<std::size_t>(stack.size()));
  auto ordinal = 1;
  for (const auto& value : stack) {
    models.push_back(MaskSourceModel::fromVariantMap(value.toMap(), ordinal));
    ++ordinal;
  }
  return models;
}

inline QVariantList typed_mask_source_model_variants_from_stack(const QVariantList& stack) {
  QVariantList variants;
  const auto models = typed_mask_source_models_from_stack(stack);
  variants.reserve(static_cast<int>(models.size()));
  for (const auto& model : models) {
    variants.append(model.toVariantMap());
  }
  return variants;
}

inline std::vector<MaskSetModel> typed_mask_set_models_from_stack(const QVariantList& stack) {
  std::vector<MaskSetModel> models;
  QHash<QString, std::size_t> indices_by_id;
  auto ordinal = 1;
  for (const auto& value : stack) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    const auto flat_index = ordinal - 1;
    ++ordinal;
    if (mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("selection")) {
      continue;
    }
    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
    auto set_index_iterator = indices_by_id.constFind(set_id);
    if (set_index_iterator == indices_by_id.constEnd()) {
      MaskSetModel model;
      model.id = set_id;
      model.name = mask.value(QStringLiteral("maskSetName")).toString();
      model.adjustments = mask_set_adjustments_from_definition(mask);
      models.push_back(model);
      const auto new_index = models.size() - 1;
      indices_by_id.insert(set_id, new_index);
      set_index_iterator = indices_by_id.constFind(set_id);
    }
    models[*set_index_iterator].components.push_back(
        mask_source_component_from_definition(
            mask,
            flat_index,
            models[*set_index_iterator].adjustments));
  }
  return models;
}

inline QVariantList typed_mask_set_model_variants_from_stack(const QVariantList& stack) {
  QVariantList variants;
  const auto models = typed_mask_set_models_from_stack(stack);
  variants.reserve(static_cast<int>(models.size()));
  for (const auto& model : models) {
    variants.append(model.toVariantMap());
  }
  return variants;
}

inline QVariantList normalize_mask_stack(const QVariantList& stack) {
  QVariantList normalized;
  normalized.reserve(stack.size());
  auto ordinal = 1;
  auto retouch_order = 1;
  for (const auto& value : stack) {
    auto mask = normalized_mask_definition(value.toMap(), ordinal);
    if (mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")) {
      mask.insert(QStringLiteral("retouchOrder"), retouch_order);
      ++retouch_order;
    } else {
      mask.insert(QStringLiteral("retouchOrder"), 0);
    }
    normalized.append(mask);
    ++ordinal;
  }
  return normalized;
}

inline bool has_structured_linear_mask(const EditRecipeState& state) {
  return state.linear_mask_enabled || std::abs(state.linear_mask_exposure) > 1e-6 ||
         std::abs(state.linear_mask_temperature) > 1e-6 ||
         std::abs(state.linear_mask_feather - 0.22) > 1e-6 ||
         std::abs(state.linear_mask_position - 0.5) > 1e-6 ||
         std::abs(state.linear_mask_rotation) > 1e-6;
}

inline bool has_structured_radial_mask(const EditRecipeState& state) {
  return state.radial_mask_enabled || std::abs(state.radial_mask_exposure) > 1e-6 ||
         std::abs(state.radial_mask_temperature) > 1e-6 ||
         std::abs(state.radial_mask_feather - 0.25) > 1e-6 ||
         std::abs(state.radial_mask_center_x - 0.5) > 1e-6 ||
         std::abs(state.radial_mask_center_y - 0.5) > 1e-6 ||
         std::abs(state.radial_mask_radius_x - 0.25) > 1e-6 ||
         std::abs(state.radial_mask_radius_y - 0.25) > 1e-6 ||
         std::abs(state.radial_mask_rotation) > 1e-6;
}

inline QStringList all_adjustment_groups() {
  return {
      QStringLiteral("raw"),
      QStringLiteral("tone"),
      QStringLiteral("color"),
      QStringLiteral("crop"),
      QStringLiteral("masks"),
      QStringLiteral("presets"),
      QStringLiteral("layers"),
      QStringLiteral("history"),
      QStringLiteral("detail"),
  };
}

inline QString normalize_adjustment_group(const QString& group) {
  const auto normalized = group.trimmed().toLower();
  if (normalized == QStringLiteral("all")) {
    return normalized;
  }

  for (const auto& candidate : all_adjustment_groups()) {
    if (candidate == normalized) {
      return candidate;
    }
  }

  return {};
}

inline QStringList normalize_adjustment_groups(const QStringList& groups) {
  QStringList normalized_groups;
  for (const auto& group : groups) {
    const auto normalized = normalize_adjustment_group(group);
    if (normalized.isEmpty()) {
      continue;
    }
    if (normalized == QStringLiteral("all")) {
      return all_adjustment_groups();
    }
    if (!normalized_groups.contains(normalized)) {
      normalized_groups.push_back(normalized);
    }
  }

  if (normalized_groups.isEmpty()) {
    return all_adjustment_groups();
  }

  return normalized_groups;
}

inline QString normalized_output_color_space_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Display P3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("display-p3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("p3"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Display P3");
  }
  if (trimmed.compare(QStringLiteral("Adobe RGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("adobe-rgb"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Adobe RGB");
  }
  if (trimmed.compare(QStringLiteral("ProPhoto RGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("prophoto-rgb"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("prophoto"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("ProPhoto RGB");
  }
  if (trimmed.compare(QStringLiteral("Rec.709"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec709"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec-709"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Rec.709");
  }
  if (trimmed.compare(QStringLiteral("Rec.2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec-2020"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Rec.2020");
  }
  return QStringLiteral("sRGB");
}

inline QString normalized_input_color_profile_name(const QString& value) {
  const auto trimmed = value.trimmed();
  const auto lower = trimmed.toLower();
  auto profile_label = [](const QString& input) {
    const auto separator = input.indexOf(QLatin1Char(':'));
    if (separator >= 0) {
      return input.mid(separator + 1).trimmed();
    }
    return QFileInfo(input).fileName().trimmed();
  };
  if (lower.startsWith(QStringLiteral("camera dcp:")) ||
      lower.startsWith(QStringLiteral("dcp:")) ||
      lower.endsWith(QStringLiteral(".dcp"))) {
    const auto label = profile_label(trimmed);
    return label.isEmpty() ? QStringLiteral("Camera DCP") : QStringLiteral("Camera DCP: %1").arg(label);
  }
  if (lower.startsWith(QStringLiteral("camera icc:")) ||
      lower.startsWith(QStringLiteral("icc:")) ||
      lower.startsWith(QStringLiteral("camera icm:")) ||
      lower.startsWith(QStringLiteral("icm:")) ||
      lower.endsWith(QStringLiteral(".icc")) ||
      lower.endsWith(QStringLiteral(".icm"))) {
    const auto label = profile_label(trimmed);
    return label.isEmpty() ? QStringLiteral("Camera ICC") : QStringLiteral("Camera ICC: %1").arg(label);
  }
  if (trimmed.compare(QStringLiteral("Camera Native"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("camera-native"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Camera Native");
  }
  if (trimmed.compare(QStringLiteral("RAW Decoder Default"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("raw-decoder-default"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("RAW Decoder Default");
  }
  if (trimmed.compare(QStringLiteral("sRGB"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("sRGB");
  }
  if (trimmed.compare(QStringLiteral("Display P3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("display-p3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("p3"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Display P3");
  }
  if (trimmed.compare(QStringLiteral("Adobe RGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("adobe-rgb"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Adobe RGB");
  }
  if (trimmed.compare(QStringLiteral("ProPhoto RGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("prophoto-rgb"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("prophoto"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("ProPhoto RGB");
  }
  if (trimmed.compare(QStringLiteral("Rec.709"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec709"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec-709"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Rec.709");
  }
  if (trimmed.compare(QStringLiteral("Rec.2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec-2020"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Rec.2020");
  }
  return QStringLiteral("Embedded");
}

inline QString normalized_working_color_profile_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Linear sRGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("linear-srgb"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("srgb-linear"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Linear sRGB");
  }
  if (trimmed.compare(QStringLiteral("Linear Display P3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("linear-display-p3"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("display-p3-linear"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Linear Display P3");
  }
  if (trimmed.compare(QStringLiteral("Linear ProPhoto RGB"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("linear-prophoto-rgb"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("prophoto-linear"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Linear ProPhoto RGB");
  }
  if (trimmed.compare(QStringLiteral("Linear Rec.2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("linear-rec.2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("linear-rec2020"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("rec2020-linear"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Linear Rec.2020");
  }
  return QStringLiteral("Linear ProPhoto RGB");
}

inline QString normalized_monitor_color_profile_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("System Default"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("system-default"), Qt::CaseInsensitive) == 0) {
    return {};
  }
  return normalized_output_color_space_name(trimmed);
}

inline QString normalized_render_intent_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Relative Colorimetric"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("relative-colorimetric"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Relative Colorimetric");
  }
  return QStringLiteral("Perceptual");
}

inline QString normalized_camera_matching_look_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Neutral"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Neutral");
  }
  if (trimmed.compare(QStringLiteral("Vivid"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Vivid");
  }
  if (trimmed.compare(QStringLiteral("Portrait"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Portrait");
  }
  if (trimmed.compare(QStringLiteral("Landscape"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Landscape");
  }
  if (trimmed.compare(QStringLiteral("Monochrome"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("Black & White"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Monochrome");
  }
  return QStringLiteral("Standard");
}

inline QString normalized_upright_mode_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Vertical"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Vertical");
  }
  if (trimmed.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Full");
  }
  if (trimmed.compare(QStringLiteral("Guided"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Guided");
  }
  return QStringLiteral("Off");
}

inline QString normalized_highlight_reconstruction_mode_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Disabled"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Disabled");
  }
  if (trimmed.compare(QStringLiteral("Camera Default"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("camera-default"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Camera Default");
  }
  return QStringLiteral("Enabled");
}

inline QString normalized_chromatic_adaptation_method_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("CAT02"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("CAT02");
  }
  return QStringLiteral("Bradford");
}

inline QString normalized_raw_demosaic_quality_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Preview"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("Draft"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Preview");
  }
  if (trimmed.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("Best"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Full");
  }
  return QStringLiteral("Balanced");
}

inline EditRecipeState merge_edit_recipe_state_by_groups(
    const EditRecipeState& base,
    const EditRecipeState& source,
    const QStringList& groups) {
  const auto normalized_groups = normalize_adjustment_groups(groups);
  EditRecipeState merged = base;

  if (normalized_groups.contains(QStringLiteral("raw"))) {
    merged.input_color_profile = source.input_color_profile;
    merged.working_color_profile = source.working_color_profile;
    merged.render_intent = source.render_intent;
    merged.raw_input_transform_enabled = source.raw_input_transform_enabled;
    merged.highlight_reconstruction_mode = source.highlight_reconstruction_mode;
    merged.chromatic_adaptation_method = source.chromatic_adaptation_method;
    merged.raw_demosaic_quality = source.raw_demosaic_quality;
    merged.raw_exposure = source.raw_exposure;
    merged.raw_boost = source.raw_boost;
    merged.raw_shadow_boost = source.raw_shadow_boost;
    merged.raw_detail = source.raw_detail;
    merged.raw_chroma_noise_reduction = source.raw_chroma_noise_reduction;
    merged.raw_moire_reduction = source.raw_moire_reduction;
    merged.raw_hot_dead_pixel_cleanup = source.raw_hot_dead_pixel_cleanup;
    merged.raw_false_color_suppression = source.raw_false_color_suppression;
    merged.raw_highlight_recovery = source.raw_highlight_recovery;
    merged.raw_gamut_mapping = source.raw_gamut_mapping;
    merged.raw_lens_correction = source.raw_lens_correction;
    merged.raw_white_balance_override = source.raw_white_balance_override;
    merged.raw_camera_matching_look = source.raw_camera_matching_look;
    merged.raw_white_balance_temperature = source.raw_white_balance_temperature;
    merged.raw_white_balance_tint = source.raw_white_balance_tint;
    merged.raw_black_level = source.raw_black_level;
    merged.raw_white_level = source.raw_white_level;
    merged.raw_calibration_hue = source.raw_calibration_hue;
    merged.raw_calibration_saturation = source.raw_calibration_saturation;
    merged.raw_red_primary_hue = source.raw_red_primary_hue;
    merged.raw_red_primary_saturation = source.raw_red_primary_saturation;
    merged.raw_green_primary_hue = source.raw_green_primary_hue;
    merged.raw_green_primary_saturation = source.raw_green_primary_saturation;
    merged.raw_blue_primary_hue = source.raw_blue_primary_hue;
    merged.raw_blue_primary_saturation = source.raw_blue_primary_saturation;
  }

  if (normalized_groups.contains(QStringLiteral("tone"))) {
    merged.exposure = source.exposure;
    merged.contrast = source.contrast;
    merged.highlights = source.highlights;
    merged.shadows = source.shadows;
    merged.whites = source.whites;
    merged.blacks = source.blacks;
    merged.brightness = source.brightness;
    merged.hdr_local_tone_mapping = source.hdr_local_tone_mapping;
    merged.filmic_shoulder = source.filmic_shoulder;
    merged.filmic_toe = source.filmic_toe;
    merged.adaptive_contrast = source.adaptive_contrast;
    merged.levels_black_point = source.levels_black_point;
    merged.levels_gamma = source.levels_gamma;
    merged.levels_white_point = source.levels_white_point;
    merged.zone_deep_shadows = source.zone_deep_shadows;
    merged.zone_shadows = source.zone_shadows;
    merged.zone_midtones = source.zone_midtones;
    merged.zone_lights = source.zone_lights;
    merged.zone_highlights = source.zone_highlights;
    merged.curve_shadows = source.curve_shadows;
    merged.curve_darks = source.curve_darks;
    merged.curve_lights = source.curve_lights;
    merged.curve_highlights = source.curve_highlights;
    merged.master_curve_points = source.master_curve_points;
    merged.luma_curve_points = source.luma_curve_points;
    merged.red_curve_points = source.red_curve_points;
    merged.green_curve_points = source.green_curve_points;
    merged.blue_curve_points = source.blue_curve_points;
  }

  if (normalized_groups.contains(QStringLiteral("color"))) {
    merged.preview_color_space = source.preview_color_space;
    merged.monitor_color_profile = source.monitor_color_profile;
    merged.soft_proof_enabled = source.soft_proof_enabled;
    merged.soft_proof_profile = source.soft_proof_profile;
    merged.temperature = source.temperature;
    merged.tint = source.tint;
    merged.output_color_space = source.output_color_space;
    merged.output_headroom = source.output_headroom;
    merged.vibrance = source.vibrance;
    merged.saturation = source.saturation;
    merged.color_mixer_model = source.color_mixer_model;
    merged.hsl_channel = source.hsl_channel;
    merged.hue_adjustment = source.hue_adjustment;
    merged.saturation_adjustment = source.saturation_adjustment;
    merged.luminance_adjustment = source.luminance_adjustment;
    merged.mixer_channels = source.mixer_channels;
    merged.rgb_channel_mixer = source.rgb_channel_mixer;
    merged.black_and_white_enabled = source.black_and_white_enabled;
    merged.black_and_white_mixer = source.black_and_white_mixer;
    merged.color_replacement_enabled = source.color_replacement_enabled;
    merged.color_replacement_source_hue = source.color_replacement_source_hue;
    merged.color_replacement_target_hue = source.color_replacement_target_hue;
    merged.color_replacement_range = source.color_replacement_range;
    merged.color_replacement_softness = source.color_replacement_softness;
    merged.color_replacement_strength = source.color_replacement_strength;
    merged.color_replacement_preserve_luminance = source.color_replacement_preserve_luminance;
    merged.color_replacement_preserve_saturation = source.color_replacement_preserve_saturation;
    merged.skin_tone_protection_enabled = source.skin_tone_protection_enabled;
    merged.skin_tone_line_hue = source.skin_tone_line_hue;
    merged.skin_tone_protection_range = source.skin_tone_protection_range;
    merged.skin_tone_protection_strength = source.skin_tone_protection_strength;
    merged.color_harmony_enabled = source.color_harmony_enabled;
    merged.color_harmony_mode = source.color_harmony_mode;
    merged.color_harmony_base_hue = source.color_harmony_base_hue;
    merged.color_harmony_strength = source.color_harmony_strength;
    merged.grading_shadows_hue = source.grading_shadows_hue;
    merged.grading_shadows_saturation = source.grading_shadows_saturation;
    merged.grading_shadows_luminance = source.grading_shadows_luminance;
    merged.grading_midtones_hue = source.grading_midtones_hue;
    merged.grading_midtones_saturation = source.grading_midtones_saturation;
    merged.grading_midtones_luminance = source.grading_midtones_luminance;
    merged.grading_highlights_hue = source.grading_highlights_hue;
    merged.grading_highlights_saturation = source.grading_highlights_saturation;
    merged.grading_highlights_luminance = source.grading_highlights_luminance;
    merged.grading_amount = source.grading_amount;
    merged.grading_balance = source.grading_balance;
  }

  if (normalized_groups.contains(QStringLiteral("crop"))) {
    merged.crop_rect_x = source.crop_rect_x;
    merged.crop_rect_y = source.crop_rect_y;
    merged.crop_rect_width = source.crop_rect_width;
    merged.crop_rect_height = source.crop_rect_height;
    merged.crop_zoom = source.crop_zoom;
    merged.crop_preset = source.crop_preset;
    merged.upright_mode = source.upright_mode;
    merged.upright_vertical_guide_angle = source.upright_vertical_guide_angle;
    merged.upright_horizontal_guide_angle = source.upright_horizontal_guide_angle;
    merged.perspective_vertical = source.perspective_vertical;
    merged.perspective_horizontal = source.perspective_horizontal;
    merged.transform_rotation = source.transform_rotation;
    merged.transform_scale = source.transform_scale;
    merged.transform_skew_x = source.transform_skew_x;
    merged.transform_skew_y = source.transform_skew_y;
    merged.liquify_center_x = source.liquify_center_x;
    merged.liquify_center_y = source.liquify_center_y;
    merged.liquify_radius = source.liquify_radius;
    merged.liquify_strength = source.liquify_strength;
    merged.content_aware_crop_fill = source.content_aware_crop_fill;
  }

  if (normalized_groups.contains(QStringLiteral("masks"))) {
    merged.linear_mask_enabled = source.linear_mask_enabled;
    merged.linear_mask_exposure = source.linear_mask_exposure;
    merged.linear_mask_temperature = source.linear_mask_temperature;
    merged.linear_mask_feather = source.linear_mask_feather;
    merged.linear_mask_position = source.linear_mask_position;
    merged.linear_mask_rotation = source.linear_mask_rotation;
    merged.radial_mask_enabled = source.radial_mask_enabled;
    merged.radial_mask_exposure = source.radial_mask_exposure;
    merged.radial_mask_temperature = source.radial_mask_temperature;
    merged.radial_mask_feather = source.radial_mask_feather;
    merged.radial_mask_center_x = source.radial_mask_center_x;
    merged.radial_mask_center_y = source.radial_mask_center_y;
    merged.radial_mask_radius_x = source.radial_mask_radius_x;
    merged.radial_mask_radius_y = source.radial_mask_radius_y;
    merged.radial_mask_rotation = source.radial_mask_rotation;
    merged.mask_stack = source.mask_stack;
  }

  if (normalized_groups.contains(QStringLiteral("presets"))) {
    merged.preset_applications = source.preset_applications;
  }

  if (normalized_groups.contains(QStringLiteral("layers"))) {
    merged.layers = source.layers;
  }

  if (normalized_groups.contains(QStringLiteral("history"))) {
    merged.active_history_branch = source.active_history_branch;
    merged.recipe_snapshots = source.recipe_snapshots;
  }

  if (normalized_groups.contains(QStringLiteral("detail"))) {
    merged.texture = source.texture;
    merged.clarity = source.clarity;
    merged.dehaze = source.dehaze;
    merged.sharpening = source.sharpening;
    merged.sharpen_radius = source.sharpen_radius;
    merged.sharpen_detail = source.sharpen_detail;
    merged.sharpen_masking = source.sharpen_masking;
    merged.luminance_nr = source.luminance_nr;
    merged.color_nr = source.color_nr;
    merged.lens_distortion = source.lens_distortion;
    merged.lens_vignette = source.lens_vignette;
    merged.chromatic_aberration = source.chromatic_aberration;
  }

  return merged;
}

inline QVariantList parse_curve_points(const QJsonArray& points) {
  QVariantList parsed_points;
  parsed_points.reserve(points.size());
  for (const auto& value : points) {
    if (!value.isObject()) {
      continue;
    }

    const auto point = value.toObject();
    QVariantMap map;
    map.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
    map.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
    parsed_points.append(map);
  }
  return parsed_points;
}

inline QJsonArray serialize_curve_points(const QVariantList& points) {
  QJsonArray serialized_points;
  for (const auto& value : points) {
    const auto point = value.toMap();
    if (point.isEmpty()) {
      continue;
    }

    QJsonObject object;
    object.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
    object.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
    serialized_points.append(object);
  }
  return serialized_points;
}

inline EditRecipeState parse_edit_recipe_json(const QString& recipe_json) {
  if (recipe_json.trimmed().isEmpty()) {
    return {};
  }

  const auto document = QJsonDocument::fromJson(recipe_json.toUtf8());
  if (!document.isObject()) {
    return {};
  }

  const auto object = document.object();
  const auto global_adjustments = detail::object_value(object, QStringLiteral("globalAdjustments"));
  const auto curves = detail::object_value(object, QStringLiteral("curves"));
  const auto parametric_curve = detail::object_value(curves, QStringLiteral("parametric"));
  const auto color_mixer = detail::object_value(object, QStringLiteral("colorMixer"));
  const auto crop_transform = detail::object_value(object, QStringLiteral("cropTransform"));
  const auto optics = detail::object_value(object, QStringLiteral("optics"));
  const auto raw_adjustments = detail::object_value(object, QStringLiteral("rawAdjustments"));
  const auto output_transform = detail::object_value(object, QStringLiteral("outputTransform"));
  const auto color_pipeline = detail::object_value(object, QStringLiteral("colorPipeline"));

  EditRecipeState state;
  state.input_color_profile = normalized_input_color_profile_name(
      color_pipeline.contains(QStringLiteral("inputProfile"))
          ? color_pipeline.value(QStringLiteral("inputProfile")).toString()
          : raw_adjustments.value(QStringLiteral("inputProfile")).toString());
  state.working_color_profile = normalized_working_color_profile_name(
      color_pipeline.contains(QStringLiteral("workingProfile"))
          ? color_pipeline.value(QStringLiteral("workingProfile")).toString()
          : output_transform.value(QStringLiteral("workingColorSpace")).toString());
  state.render_intent = normalized_render_intent_name(
      output_transform.contains(QStringLiteral("renderIntent"))
          ? output_transform.value(QStringLiteral("renderIntent")).toString()
          : color_pipeline.value(QStringLiteral("renderIntent")).toString());
  state.raw_input_transform_enabled =
      raw_adjustments.contains(QStringLiteral("inputTransformEnabled"))
          ? raw_adjustments.value(QStringLiteral("inputTransformEnabled")).toBool()
          : true;
  state.highlight_reconstruction_mode = normalized_highlight_reconstruction_mode_name(
      raw_adjustments.contains(QStringLiteral("highlightReconstructionMode"))
          ? raw_adjustments.value(QStringLiteral("highlightReconstructionMode")).toString()
          : (raw_adjustments.contains(QStringLiteral("highlightRecovery")) &&
                     !raw_adjustments.value(QStringLiteral("highlightRecovery")).toBool()
                 ? QStringLiteral("Disabled")
                 : QStringLiteral("Enabled")));
  state.chromatic_adaptation_method = normalized_chromatic_adaptation_method_name(
      raw_adjustments.value(QStringLiteral("chromaticAdaptationMethod")).toString());
  state.raw_demosaic_quality = normalized_raw_demosaic_quality_name(
      raw_adjustments.value(QStringLiteral("demosaicQuality")).toString());
  state.raw_exposure = raw_adjustments.value(QStringLiteral("exposure")).toDouble();
  state.raw_boost = raw_adjustments.value(QStringLiteral("boost")).toDouble();
  state.raw_shadow_boost = raw_adjustments.value(QStringLiteral("shadowBoost")).toDouble(1.0);
  state.raw_detail = raw_adjustments.value(QStringLiteral("detail")).toDouble();
  state.raw_chroma_noise_reduction = std::clamp(
      raw_adjustments.value(QStringLiteral("chromaNoiseReduction")).toDouble(),
      0.0,
      1.0);
  state.raw_moire_reduction = std::clamp(
      raw_adjustments.value(QStringLiteral("moireReduction")).toDouble(),
      0.0,
      1.0);
  state.raw_hot_dead_pixel_cleanup = std::clamp(
      raw_adjustments.value(QStringLiteral("hotDeadPixelCleanup")).toDouble(),
      0.0,
      1.0);
  state.raw_false_color_suppression = std::clamp(
      raw_adjustments.value(QStringLiteral("falseColorSuppression")).toDouble(),
      0.0,
      1.0);
  state.raw_highlight_recovery =
      raw_adjustments.contains(QStringLiteral("highlightRecovery"))
          ? raw_adjustments.value(QStringLiteral("highlightRecovery")).toBool()
          : true;
  state.raw_gamut_mapping =
      raw_adjustments.contains(QStringLiteral("gamutMapping"))
          ? raw_adjustments.value(QStringLiteral("gamutMapping")).toBool()
          : true;
  state.raw_lens_correction =
      raw_adjustments.contains(QStringLiteral("lensCorrection"))
          ? raw_adjustments.value(QStringLiteral("lensCorrection")).toBool()
          : true;
  state.raw_white_balance_override =
      raw_adjustments.value(QStringLiteral("whiteBalanceOverride")).toBool();
  state.raw_camera_matching_look = normalized_camera_matching_look_name(
      raw_adjustments.value(QStringLiteral("cameraMatchingLook")).toString());
  state.raw_white_balance_temperature =
      raw_adjustments.value(QStringLiteral("whiteBalanceTemperature")).toDouble(5200.0);
  state.raw_white_balance_tint =
      raw_adjustments.value(QStringLiteral("whiteBalanceTint")).toDouble();
  state.raw_black_level = std::clamp(
      raw_adjustments.value(QStringLiteral("blackLevel")).toDouble(0.0),
      0.0,
      0.95);
  state.raw_white_level = std::clamp(
      raw_adjustments.value(QStringLiteral("whiteLevel")).toDouble(1.0),
      state.raw_black_level + 0.01,
      4.0);
  state.raw_calibration_hue = std::clamp(
      raw_adjustments.value(QStringLiteral("calibrationHue")).toDouble(),
      -100.0,
      100.0);
  state.raw_calibration_saturation = std::clamp(
      raw_adjustments.value(QStringLiteral("calibrationSaturation")).toDouble(),
      -100.0,
      100.0);
  state.raw_red_primary_hue = std::clamp(
      raw_adjustments.value(QStringLiteral("redPrimaryHue")).toDouble(),
      -100.0,
      100.0);
  state.raw_red_primary_saturation = std::clamp(
      raw_adjustments.value(QStringLiteral("redPrimarySaturation")).toDouble(),
      -100.0,
      100.0);
  state.raw_green_primary_hue = std::clamp(
      raw_adjustments.value(QStringLiteral("greenPrimaryHue")).toDouble(),
      -100.0,
      100.0);
  state.raw_green_primary_saturation = std::clamp(
      raw_adjustments.value(QStringLiteral("greenPrimarySaturation")).toDouble(),
      -100.0,
      100.0);
  state.raw_blue_primary_hue = std::clamp(
      raw_adjustments.value(QStringLiteral("bluePrimaryHue")).toDouble(),
      -100.0,
      100.0);
  state.raw_blue_primary_saturation = std::clamp(
      raw_adjustments.value(QStringLiteral("bluePrimarySaturation")).toDouble(),
      -100.0,
      100.0);
  state.exposure = detail::read_double(
      global_adjustments, QStringLiteral("exposure"), object, QStringLiteral("exposure"));
  state.contrast = detail::read_double(
      global_adjustments, QStringLiteral("contrast"), object, QStringLiteral("contrast"));
  state.highlights = detail::read_double(
      global_adjustments, QStringLiteral("highlights"), object, QStringLiteral("highlights"));
  state.shadows = detail::read_double(
      global_adjustments, QStringLiteral("shadows"), object, QStringLiteral("shadows"));
  state.whites = detail::read_double(
      global_adjustments, QStringLiteral("whites"), object, QStringLiteral("whites"));
  state.blacks = detail::read_double(
      global_adjustments, QStringLiteral("blacks"), object, QStringLiteral("blacks"));
  state.brightness = detail::read_double(
      global_adjustments, QStringLiteral("brightness"), object, QStringLiteral("brightness"));
  state.hdr_local_tone_mapping = detail::read_double(
      global_adjustments, QStringLiteral("hdrLocalTone"), object, QStringLiteral("hdrLocalTone"));
  state.filmic_shoulder = detail::read_double(
      global_adjustments, QStringLiteral("filmicShoulder"), object, QStringLiteral("filmicShoulder"));
  state.filmic_toe = detail::read_double(
      global_adjustments, QStringLiteral("filmicToe"), object, QStringLiteral("filmicToe"));
  state.adaptive_contrast = detail::read_double(
      global_adjustments, QStringLiteral("adaptiveContrast"), object, QStringLiteral("adaptiveContrast"));
  state.levels_black_point = detail::read_double(
      global_adjustments, QStringLiteral("levelsBlackPoint"), object, QStringLiteral("levelsBlackPoint"));
  state.levels_gamma = detail::read_double(
      global_adjustments, QStringLiteral("levelsGamma"), object, QStringLiteral("levelsGamma"));
  state.levels_white_point = detail::read_double(
      global_adjustments, QStringLiteral("levelsWhitePoint"), object, QStringLiteral("levelsWhitePoint"));
  state.zone_deep_shadows = detail::read_double(
      global_adjustments, QStringLiteral("zoneDeepShadows"), object, QStringLiteral("zoneDeepShadows"));
  state.zone_shadows = detail::read_double(
      global_adjustments, QStringLiteral("zoneShadows"), object, QStringLiteral("zoneShadows"));
  state.zone_midtones = detail::read_double(
      global_adjustments, QStringLiteral("zoneMidtones"), object, QStringLiteral("zoneMidtones"));
  state.zone_lights = detail::read_double(
      global_adjustments, QStringLiteral("zoneLights"), object, QStringLiteral("zoneLights"));
  state.zone_highlights = detail::read_double(
      global_adjustments, QStringLiteral("zoneHighlights"), object, QStringLiteral("zoneHighlights"));
  state.temperature = detail::read_double(
      global_adjustments, QStringLiteral("temperature"), object, QStringLiteral("temperature"));
  state.tint = detail::read_double(
      global_adjustments, QStringLiteral("tint"), object, QStringLiteral("tint"));
  state.texture = detail::read_double(
      global_adjustments, QStringLiteral("texture"), object, QStringLiteral("texture"));
  state.clarity = detail::read_double(
      global_adjustments, QStringLiteral("clarity"), object, QStringLiteral("clarity"));
  state.dehaze = detail::read_double(
      global_adjustments, QStringLiteral("dehaze"), object, QStringLiteral("dehaze"));
  state.sharpening = detail::read_double(
      global_adjustments, QStringLiteral("sharpening"), object, QStringLiteral("sharpening"));
  state.sharpen_radius = detail::read_double(
      global_adjustments, QStringLiteral("sharpenRadius"), object, QStringLiteral("sharpenRadius"), 1.0);
  state.sharpen_detail = detail::read_double(
      global_adjustments, QStringLiteral("sharpenDetail"), object, QStringLiteral("sharpenDetail"));
  state.sharpen_masking = detail::read_double(
      global_adjustments, QStringLiteral("sharpenMasking"), object, QStringLiteral("sharpenMasking"));
  state.luminance_nr = detail::read_double(
      global_adjustments, QStringLiteral("luminanceNr"), object, QStringLiteral("luminanceNr"));
  state.color_nr = detail::read_double(
      global_adjustments, QStringLiteral("colorNr"), object, QStringLiteral("colorNr"));
  state.lens_distortion = detail::read_double(
      optics, QStringLiteral("lensDistortion"), global_adjustments, QStringLiteral("lensDistortion"));
  state.lens_vignette = detail::read_double(
      optics, QStringLiteral("lensVignette"), global_adjustments, QStringLiteral("lensVignette"));
  state.chromatic_aberration = detail::read_double(
      optics, QStringLiteral("chromaticAberration"), global_adjustments, QStringLiteral("chromaticAberration"));
  state.perspective_vertical = detail::read_double(
      crop_transform,
      QStringLiteral("perspectiveVertical"),
      global_adjustments,
      QStringLiteral("perspectiveVertical"));
  state.perspective_horizontal = detail::read_double(
      crop_transform,
      QStringLiteral("perspectiveHorizontal"),
      global_adjustments,
      QStringLiteral("perspectiveHorizontal"));
  state.transform_rotation = detail::read_double(
      crop_transform, QStringLiteral("rotation"), global_adjustments, QStringLiteral("rotation"));
  state.transform_scale = std::clamp(
      detail::read_double(crop_transform, QStringLiteral("scale"), global_adjustments, QStringLiteral("scale"), 1.0),
      0.25,
      4.0);
  state.transform_skew_x = detail::read_double(
      crop_transform, QStringLiteral("skewX"), global_adjustments, QStringLiteral("skewX"));
  state.transform_skew_y = detail::read_double(
      crop_transform, QStringLiteral("skewY"), global_adjustments, QStringLiteral("skewY"));
  state.liquify_center_x = std::clamp(
      detail::read_double(
          crop_transform, QStringLiteral("liquifyCenterX"), global_adjustments, QStringLiteral("liquifyCenterX"), 0.5),
      0.0,
      1.0);
  state.liquify_center_y = std::clamp(
      detail::read_double(
          crop_transform, QStringLiteral("liquifyCenterY"), global_adjustments, QStringLiteral("liquifyCenterY"), 0.5),
      0.0,
      1.0);
  state.liquify_radius = std::clamp(
      detail::read_double(
          crop_transform, QStringLiteral("liquifyRadius"), global_adjustments, QStringLiteral("liquifyRadius"), 0.25),
      0.02,
      1.0);
  state.liquify_strength = detail::read_double(
      crop_transform, QStringLiteral("liquifyStrength"), global_adjustments, QStringLiteral("liquifyStrength"));
  state.content_aware_crop_fill =
      crop_transform.contains(QStringLiteral("contentAwareFill"))
          ? crop_transform.value(QStringLiteral("contentAwareFill")).toBool()
          : object.value(QStringLiteral("contentAwareCropFill")).toBool(false);
  state.vibrance = detail::read_double(
      global_adjustments, QStringLiteral("vibrance"), object, QStringLiteral("vibrance"));
  state.saturation = detail::read_double(
      global_adjustments, QStringLiteral("saturation"), object, QStringLiteral("saturation"));

  state.curve_shadows = detail::read_double(
      parametric_curve, QStringLiteral("shadows"), object, QStringLiteral("curveShadows"));
  state.curve_darks = detail::read_double(
      parametric_curve, QStringLiteral("darks"), object, QStringLiteral("curveDarks"));
  state.curve_lights = detail::read_double(
      parametric_curve, QStringLiteral("lights"), object, QStringLiteral("curveLights"));
  state.curve_highlights = detail::read_double(
      parametric_curve, QStringLiteral("highlights"), object, QStringLiteral("curveHighlights"));

  for (const auto& channel_value : curves.value(QStringLiteral("channels")).toArray()) {
    if (!channel_value.isObject()) {
      continue;
    }

    const auto channel_object = channel_value.toObject();
    const auto channel_name = channel_object.value(QStringLiteral("channel")).toString().toLower();
    const auto channel_points =
        parse_curve_points(channel_object.value(QStringLiteral("points")).toArray());
    if (channel_name == QStringLiteral("master")) {
      state.master_curve_points = channel_points;
    } else if (channel_name == QStringLiteral("luma")) {
      state.luma_curve_points = channel_points;
    } else if (channel_name == QStringLiteral("red")) {
      state.red_curve_points = channel_points;
    } else if (channel_name == QStringLiteral("green")) {
      state.green_curve_points = channel_points;
    } else if (channel_name == QStringLiteral("blue")) {
      state.blue_curve_points = channel_points;
    }
  }

  state.hsl_channel = detail::read_string(
      color_mixer, QStringLiteral("channel"), object, QStringLiteral("hslChannel"), QStringLiteral("Reds"));
  state.color_mixer_model = detail::read_string(
      color_mixer, QStringLiteral("model"), object, QStringLiteral("colorMixerModel"), QStringLiteral("OKLab"));
  state.hue_adjustment = detail::read_double(
      color_mixer, QStringLiteral("hue"), object, QStringLiteral("hueAdjustment"));
  state.saturation_adjustment = detail::read_double(
      color_mixer, QStringLiteral("saturation"), object, QStringLiteral("saturationAdjustment"));
  state.luminance_adjustment = detail::read_double(
      color_mixer, QStringLiteral("luminance"), object, QStringLiteral("luminanceAdjustment"));

  // Deserialize per-channel mixer adjustments.
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  const auto mixer_channels = color_mixer.value(QStringLiteral("channels")).toObject();
  for (int i = 0; i < 8; ++i) {
    const auto ch = mixer_channels.value(kMixerChannelNames[i]).toObject();
    if (!ch.isEmpty()) {
      state.mixer_channels[static_cast<std::size_t>(i)].hue = ch.value(QStringLiteral("hue")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].saturation = ch.value(QStringLiteral("saturation")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].luminance = ch.value(QStringLiteral("luminance")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].selective_cyan = ch.value(QStringLiteral("selectiveCyan")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].selective_magenta = ch.value(QStringLiteral("selectiveMagenta")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].selective_yellow = ch.value(QStringLiteral("selectiveYellow")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].selective_black = ch.value(QStringLiteral("selectiveBlack")).toDouble();
    }
  }
  // Backward compat: if no per-channel data, populate the selected channel from legacy fields.
  if (mixer_channels.isEmpty()) {
    for (int i = 0; i < 8; ++i) {
      if (kMixerChannelNames[i] == state.hsl_channel) {
        state.mixer_channels[static_cast<std::size_t>(i)].hue = state.hue_adjustment;
        state.mixer_channels[static_cast<std::size_t>(i)].saturation = state.saturation_adjustment;
        state.mixer_channels[static_cast<std::size_t>(i)].luminance = state.luminance_adjustment;
        break;
      }
    }
  }

  const auto rgb_channel_mixer = color_mixer.value(QStringLiteral("rgbChannelMixer")).toObject();
  if (!rgb_channel_mixer.isEmpty()) {
    state.rgb_channel_mixer.red_from_red = rgb_channel_mixer.value(QStringLiteral("redFromRed")).toDouble(100.0);
    state.rgb_channel_mixer.red_from_green = rgb_channel_mixer.value(QStringLiteral("redFromGreen")).toDouble();
    state.rgb_channel_mixer.red_from_blue = rgb_channel_mixer.value(QStringLiteral("redFromBlue")).toDouble();
    state.rgb_channel_mixer.green_from_red = rgb_channel_mixer.value(QStringLiteral("greenFromRed")).toDouble();
    state.rgb_channel_mixer.green_from_green =
        rgb_channel_mixer.value(QStringLiteral("greenFromGreen")).toDouble(100.0);
    state.rgb_channel_mixer.green_from_blue = rgb_channel_mixer.value(QStringLiteral("greenFromBlue")).toDouble();
    state.rgb_channel_mixer.blue_from_red = rgb_channel_mixer.value(QStringLiteral("blueFromRed")).toDouble();
    state.rgb_channel_mixer.blue_from_green = rgb_channel_mixer.value(QStringLiteral("blueFromGreen")).toDouble();
    state.rgb_channel_mixer.blue_from_blue = rgb_channel_mixer.value(QStringLiteral("blueFromBlue")).toDouble(100.0);
  }

  const auto black_and_white = color_mixer.value(QStringLiteral("blackAndWhite")).toObject();
  state.black_and_white_enabled =
      black_and_white.value(QStringLiteral("enabled")).toBool(state.black_and_white_enabled);
  const auto black_and_white_channels = black_and_white.value(QStringLiteral("channels")).toObject();
  for (int i = 0; i < 8; ++i) {
    state.black_and_white_mixer[static_cast<std::size_t>(i)] =
        black_and_white_channels.value(kMixerChannelNames[i]).toDouble();
  }
  const auto color_replacement = color_mixer.value(QStringLiteral("colorReplacement")).toObject();
  if (!color_replacement.isEmpty()) {
    state.color_replacement_enabled = color_replacement.value(QStringLiteral("enabled")).toBool();
    state.color_replacement_source_hue = color_replacement.value(QStringLiteral("sourceHue")).toDouble();
    state.color_replacement_target_hue = color_replacement.value(QStringLiteral("targetHue")).toDouble(0.33);
    state.color_replacement_range = color_replacement.value(QStringLiteral("range")).toDouble(35.0);
    state.color_replacement_softness = color_replacement.value(QStringLiteral("softness")).toDouble(100.0);
    state.color_replacement_strength = color_replacement.value(QStringLiteral("strength")).toDouble();
    state.color_replacement_preserve_luminance =
        color_replacement.value(QStringLiteral("preserveLuminance")).toBool(true);
    state.color_replacement_preserve_saturation =
        color_replacement.value(QStringLiteral("preserveSaturation")).toBool(true);
  }
  const auto skin_tone = color_mixer.value(QStringLiteral("skinToneProtection")).toObject();
  if (!skin_tone.isEmpty()) {
    state.skin_tone_protection_enabled = skin_tone.value(QStringLiteral("enabled")).toBool();
    state.skin_tone_line_hue = skin_tone.value(QStringLiteral("lineHue")).toDouble(0.08);
    state.skin_tone_protection_range = skin_tone.value(QStringLiteral("range")).toDouble(30.0);
    state.skin_tone_protection_strength = skin_tone.value(QStringLiteral("strength")).toDouble();
  }
  const auto color_harmony = color_mixer.value(QStringLiteral("colorHarmony")).toObject();
  if (!color_harmony.isEmpty()) {
    state.color_harmony_enabled = color_harmony.value(QStringLiteral("enabled")).toBool();
    state.color_harmony_mode =
        color_harmony.value(QStringLiteral("mode")).toString(QStringLiteral("Complementary"));
    state.color_harmony_base_hue = color_harmony.value(QStringLiteral("baseHue")).toDouble();
    state.color_harmony_strength = color_harmony.value(QStringLiteral("strength")).toDouble();
  }

  const auto color_grading = detail::object_value(object, QStringLiteral("colorGrading"));
  const auto grading_shadows = detail::object_value(color_grading, QStringLiteral("shadows"));
  const auto grading_midtones = detail::object_value(color_grading, QStringLiteral("midtones"));
  const auto grading_highlights = detail::object_value(color_grading, QStringLiteral("highlights"));
  state.grading_shadows_hue = grading_shadows.value(QStringLiteral("hue")).toDouble();
  state.grading_shadows_saturation = grading_shadows.value(QStringLiteral("saturation")).toDouble();
  state.grading_shadows_luminance = grading_shadows.value(QStringLiteral("luminance")).toDouble();
  state.grading_midtones_hue = grading_midtones.value(QStringLiteral("hue")).toDouble();
  state.grading_midtones_saturation = grading_midtones.value(QStringLiteral("saturation")).toDouble();
  state.grading_midtones_luminance = grading_midtones.value(QStringLiteral("luminance")).toDouble();
  state.grading_highlights_hue = grading_highlights.value(QStringLiteral("hue")).toDouble();
  state.grading_highlights_saturation = grading_highlights.value(QStringLiteral("saturation")).toDouble();
  state.grading_highlights_luminance = grading_highlights.value(QStringLiteral("luminance")).toDouble();
  state.grading_amount = color_grading.value(QStringLiteral("amount")).toDouble(100.0);
  state.grading_balance = color_grading.value(QStringLiteral("balance")).toDouble();

  state.crop_zoom = detail::read_double(
      crop_transform, QStringLiteral("zoom"), object, QStringLiteral("cropZoom"), 1.0);
  state.crop_preset = detail::read_string(
      crop_transform, QStringLiteral("preset"), object, QStringLiteral("cropPreset"), QStringLiteral("Original"));
  state.upright_mode = normalized_upright_mode_name(detail::read_string(
      crop_transform, QStringLiteral("uprightMode"), object, QStringLiteral("uprightMode"), QStringLiteral("Off")));
  state.upright_vertical_guide_angle = detail::read_double(
      crop_transform,
      QStringLiteral("uprightVerticalGuideAngle"),
      object,
      QStringLiteral("uprightVerticalGuideAngle"));
  state.upright_horizontal_guide_angle = detail::read_double(
      crop_transform,
      QStringLiteral("uprightHorizontalGuideAngle"),
      object,
      QStringLiteral("uprightHorizontalGuideAngle"));
  state.crop_rect_x = crop_transform.value(QStringLiteral("x")).toDouble();
  state.crop_rect_y = crop_transform.value(QStringLiteral("y")).toDouble();
  state.crop_rect_width = crop_transform.value(QStringLiteral("width")).toDouble();
  state.crop_rect_height = crop_transform.value(QStringLiteral("height")).toDouble();
  state.output_color_space = normalized_output_color_space_name(
      output_transform.contains(QStringLiteral("colorSpace"))
          ? output_transform.value(QStringLiteral("colorSpace")).toString()
          : object.value(QStringLiteral("outputColorSpace")).toString());
  state.preview_color_space = normalized_output_color_space_name(
      output_transform.contains(QStringLiteral("previewColorSpace"))
          ? output_transform.value(QStringLiteral("previewColorSpace")).toString()
          : state.output_color_space);
  state.monitor_color_profile = normalized_monitor_color_profile_name(
      output_transform.value(QStringLiteral("monitorProfile")).toString());
  state.soft_proof_enabled = output_transform.value(QStringLiteral("softProofEnabled")).toBool(false);
  state.soft_proof_profile = normalized_output_color_space_name(
      output_transform.value(QStringLiteral("softProofProfile")).toString(QStringLiteral("sRGB")));
  state.output_headroom = qBound(
      1.0,
      output_transform.value(QStringLiteral("headroom")).toDouble(1.0),
      16.0);
  state.preset_applications =
      object.value(QStringLiteral("presetApplications")).toArray().toVariantList();
  state.layers = detail::parse_photo_layers(
      object.contains(QStringLiteral("photoLayers"))
          ? object.value(QStringLiteral("photoLayers")).toArray()
          : object.value(QStringLiteral("layers")).toArray());
  const auto history = detail::object_value(object, QStringLiteral("history"));
  state.active_history_branch = history.value(QStringLiteral("activeBranchId")).toString(QStringLiteral("main"));
  state.recipe_snapshots = history.value(QStringLiteral("snapshots")).toArray().toVariantList();

  QVariantList parsed_mask_stack;
  auto linear_mask_ordinal = 1;
  auto radial_mask_ordinal = 1;
  const auto append_parsed_mask = [&](const QJsonObject& mask_object,
                                      const QJsonObject& adjustments_override,
                                      const QString& default_tool_role,
                                      const QString& mask_set_id,
                                      const QString& mask_set_name) {
    const auto adjustments =
        adjustments_override.isEmpty()
            ? detail::object_value(mask_object, QStringLiteral("adjustments"))
            : adjustments_override;
    const auto geometry = detail::object_value(mask_object, QStringLiteral("geometry"));
    const auto constraints = detail::object_value(mask_object, QStringLiteral("constraints"));
    const auto selection = detail::object_value(mask_object, QStringLiteral("selection"));
    const auto kind = normalized_mask_kind(mask_object.value(QStringLiteral("kind")).toString());
    const auto matte_cleanup_defaults = matte_cleanup_defaults_for_kind(kind);

    QVariantMap parsed_mask;
    parsed_mask.insert(QStringLiteral("id"), mask_object.value(QStringLiteral("id")).toString());
    parsed_mask.insert(
        QStringLiteral("schemaVersion"),
        std::max(1, mask_object.value(QStringLiteral("schemaVersion")).toInt(1)));
    parsed_mask.insert(
        QStringLiteral("sourceSchemaVersion"),
        std::max(1, mask_object.value(QStringLiteral("sourceSchemaVersion")).toInt(1)));
    parsed_mask.insert(QStringLiteral("parentId"), mask_object.value(QStringLiteral("parentId")).toString());
    parsed_mask.insert(QStringLiteral("outputId"), mask_object.value(QStringLiteral("outputId")).toString());
    parsed_mask.insert(
        QStringLiteral("reusableOutput"),
        mask_object.value(QStringLiteral("reusableOutput")).toBool(false));
    parsed_mask.insert(QStringLiteral("name"), mask_object.value(QStringLiteral("name")).toString());
    parsed_mask.insert(QStringLiteral("kind"), kind);
    parsed_mask.insert(
        QStringLiteral("toolRole"),
        normalized_mask_tool_role(
            mask_object.contains(QStringLiteral("toolRole"))
                ? mask_object.value(QStringLiteral("toolRole")).toString()
                : default_tool_role));
    parsed_mask.insert(
        QStringLiteral("retouchOrder"),
        parsed_mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")
            ? std::max(1, mask_object.value(QStringLiteral("retouchOrder")).toInt(
                              static_cast<int>(parsed_mask_stack.size()) + 1))
            : 0);
    parsed_mask.insert(
        QStringLiteral("operation"),
        normalized_mask_operation(mask_object.value(QStringLiteral("operation")).toString()));
    parsed_mask.insert(
        QStringLiteral("enabled"),
        mask_object.contains(QStringLiteral("enabled"))
            ? mask_object.value(QStringLiteral("enabled")).toBool()
            : true);
    parsed_mask.insert(
        QStringLiteral("solo"),
        mask_object.contains(QStringLiteral("solo"))
            ? mask_object.value(QStringLiteral("solo")).toBool()
            : false);
    parsed_mask.insert(
        QStringLiteral("locked"),
        mask_object.contains(QStringLiteral("locked"))
            ? mask_object.value(QStringLiteral("locked")).toBool()
            : false);
    parsed_mask.insert(
        QStringLiteral("committed"),
        mask_object.contains(QStringLiteral("committed"))
            ? mask_object.value(QStringLiteral("committed")).toBool()
            : true);
    parsed_mask.insert(
        QStringLiteral("opacity"),
        qBound(0.0, mask_object.contains(QStringLiteral("opacity"))
                         ? mask_object.value(QStringLiteral("opacity")).toDouble()
                         : 1.0,
               1.0));
    parsed_mask.insert(
        QStringLiteral("density"),
        qBound(0.0, mask_object.contains(QStringLiteral("density"))
                         ? mask_object.value(QStringLiteral("density")).toDouble()
                         : 1.0,
               1.0));
    parsed_mask.insert(
        QStringLiteral("blendMode"),
        normalized_mask_blend_mode(mask_object.value(QStringLiteral("blendMode")).toString()));
    parsed_mask.insert(QStringLiteral("exposure"), adjustments.value(QStringLiteral("exposure")).toDouble());
    parsed_mask.insert(QStringLiteral("contrast"), adjustments.value(QStringLiteral("contrast")).toDouble());
    parsed_mask.insert(QStringLiteral("highlights"), adjustments.value(QStringLiteral("highlights")).toDouble());
    parsed_mask.insert(QStringLiteral("shadows"), adjustments.value(QStringLiteral("shadows")).toDouble());
    parsed_mask.insert(QStringLiteral("whites"), adjustments.value(QStringLiteral("whites")).toDouble());
    parsed_mask.insert(QStringLiteral("blacks"), adjustments.value(QStringLiteral("blacks")).toDouble());
    parsed_mask.insert(
        QStringLiteral("temperature"),
        adjustments.value(QStringLiteral("temperature")).toDouble());
    parsed_mask.insert(QStringLiteral("tint"), adjustments.value(QStringLiteral("tint")).toDouble());
    parsed_mask.insert(QStringLiteral("vibrance"), adjustments.value(QStringLiteral("vibrance")).toDouble());
    parsed_mask.insert(QStringLiteral("saturation"), adjustments.value(QStringLiteral("saturation")).toDouble());
    parsed_mask.insert(QStringLiteral("hueShift"), adjustments.value(QStringLiteral("hueShift")).toDouble());
    parsed_mask.insert(
        QStringLiteral("saturationShift"),
        adjustments.value(QStringLiteral("saturationShift")).toDouble());
    parsed_mask.insert(
        QStringLiteral("colorizeHue"),
        adjustments.value(QStringLiteral("colorizeHue")).toDouble());
    parsed_mask.insert(
        QStringLiteral("colorizeStrength"),
        adjustments.value(QStringLiteral("colorizeStrength")).toDouble());
    parsed_mask.insert(
        QStringLiteral("removeStrength"),
        adjustments.value(QStringLiteral("removeStrength")).toDouble());
    parsed_mask.insert(
        QStringLiteral("selectionInvert"),
        selection.contains(QStringLiteral("invert"))
            ? selection.value(QStringLiteral("invert")).toBool()
            : (mask_object.contains(QStringLiteral("invert"))
                   ? mask_object.value(QStringLiteral("invert")).toBool()
                   : adjustments.value(QStringLiteral("selectionInvert")).toBool()));
    parsed_mask.insert(
        QStringLiteral("invert"),
        parsed_mask.value(QStringLiteral("selectionInvert")).toBool());
    parsed_mask.insert(
        QStringLiteral("selectionExpand"),
        selection.contains(QStringLiteral("expand"))
            ? selection.value(QStringLiteral("expand")).toDouble()
            : adjustments.value(QStringLiteral("selectionExpand")).toDouble());
    parsed_mask.insert(
        QStringLiteral("selectionSmooth"),
        selection.contains(QStringLiteral("smooth"))
            ? selection.value(QStringLiteral("smooth")).toDouble()
            : adjustments.value(QStringLiteral("selectionSmooth")).toDouble(matte_cleanup_defaults.smooth));
    parsed_mask.insert(
        QStringLiteral("selectionFineEdge"),
        selection.contains(QStringLiteral("fineEdge"))
            ? selection.value(QStringLiteral("fineEdge")).toDouble()
            : adjustments.value(QStringLiteral("selectionFineEdge")).toDouble(
                  matte_cleanup_defaults.fine_edge));
    parsed_mask.insert(
        QStringLiteral("selectionEdgeContrast"),
        adjustments.value(QStringLiteral("selectionEdgeContrast")).toDouble(matte_cleanup_defaults.edge_contrast));
    parsed_mask.insert(
        QStringLiteral("selectionDecontaminate"),
        adjustments.value(QStringLiteral("selectionDecontaminate")).toDouble(matte_cleanup_defaults.decontaminate));
    const auto parse_refine_percent = [&](const QString& adjustment_key, const QString& selection_key) {
      return selection.contains(selection_key) ? selection.value(selection_key).toDouble()
                                               : adjustments.value(adjustment_key).toDouble(
                                                     adjustment_key == QStringLiteral("selectionFeather") ? matte_cleanup_defaults.feather
                                                     : adjustment_key == QStringLiteral("selectionHarden") ? matte_cleanup_defaults.harden
                                                     : adjustment_key == QStringLiteral("selectionDespeckle") ? matte_cleanup_defaults.despeckle
                                                     : adjustment_key == QStringLiteral("selectionFillHoles") ? matte_cleanup_defaults.fill_holes
                                                     : adjustment_key == QStringLiteral("selectionRemoveIslands") ? matte_cleanup_defaults.remove_islands
                                                     : 0.0);
    };
    parsed_mask.insert(
        QStringLiteral("selectionGrow"),
        parse_refine_percent(QStringLiteral("selectionGrow"), QStringLiteral("grow")));
    parsed_mask.insert(
        QStringLiteral("selectionShrink"),
        parse_refine_percent(QStringLiteral("selectionShrink"), QStringLiteral("shrink")));
    parsed_mask.insert(
        QStringLiteral("selectionFeather"),
        parse_refine_percent(QStringLiteral("selectionFeather"), QStringLiteral("feather")));
    parsed_mask.insert(
        QStringLiteral("selectionBlur"),
        parse_refine_percent(QStringLiteral("selectionBlur"), QStringLiteral("blur")));
    parsed_mask.insert(
        QStringLiteral("selectionHarden"),
        parse_refine_percent(QStringLiteral("selectionHarden"), QStringLiteral("harden")));
    parsed_mask.insert(
        QStringLiteral("selectionDespeckle"),
        parse_refine_percent(QStringLiteral("selectionDespeckle"), QStringLiteral("despeckle")));
    parsed_mask.insert(
        QStringLiteral("selectionFillHoles"),
        parse_refine_percent(QStringLiteral("selectionFillHoles"), QStringLiteral("fillHoles")));
    parsed_mask.insert(
        QStringLiteral("selectionRemoveIslands"),
        parse_refine_percent(QStringLiteral("selectionRemoveIslands"), QStringLiteral("removeIslands")));
    parsed_mask.insert(
        QStringLiteral("refineLuminanceMin"),
        constraints.value(QStringLiteral("refineLuminanceMin")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineLuminanceMax"),
        constraints.value(QStringLiteral("refineLuminanceMax")).toDouble(1.0));
    parsed_mask.insert(
        QStringLiteral("refineHueCenter"),
        constraints.value(QStringLiteral("refineHueCenter")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineHueRange"),
        constraints.value(QStringLiteral("refineHueRange")).toDouble(0.5));
    parsed_mask.insert(
        QStringLiteral("refineSaturationMin"),
        constraints.value(QStringLiteral("refineSaturationMin")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineSaturationMax"),
        constraints.value(QStringLiteral("refineSaturationMax")).toDouble(1.0));
    parsed_mask.insert(
        QStringLiteral("refineDepthMin"),
        constraints.value(QStringLiteral("refineDepthMin")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineDepthMax"),
        constraints.value(QStringLiteral("refineDepthMax")).toDouble(1.0));
    parsed_mask.insert(
        QStringLiteral("refineEdgeConfidenceMin"),
        constraints.value(QStringLiteral("refineEdgeConfidenceMin")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineRangeFeather"),
        constraints.value(QStringLiteral("refineRangeFeather")).toDouble(0.05));
    parsed_mask.insert(
        QStringLiteral("generatedMaskHoleFillThreshold"),
        constraints.value(QStringLiteral("generatedMaskHoleFillThreshold"))
            .toDouble(matte_cleanup_defaults.generated_hole_threshold));
    parsed_mask.insert(
        QStringLiteral("generatedMaskIslandRemovalThreshold"),
        constraints.value(QStringLiteral("generatedMaskIslandRemovalThreshold"))
            .toDouble(matte_cleanup_defaults.generated_island_threshold));
    parsed_mask.insert(
        QStringLiteral("matteCleanupSchemaVersion"),
        std::max(
            1,
            constraints.contains(QStringLiteral("matteCleanupSchemaVersion"))
                ? constraints.value(QStringLiteral("matteCleanupSchemaVersion")).toInt(1)
                : geometry.value(QStringLiteral("matteCleanupSchemaVersion")).toInt(1)));
    parsed_mask.insert(
        QStringLiteral("matteCleanupProfile"),
        (constraints.contains(QStringLiteral("matteCleanupProfile"))
             ? constraints.value(QStringLiteral("matteCleanupProfile")).toString()
             : geometry.value(QStringLiteral("matteCleanupProfile"))
                   .toString(QString::fromLatin1(matte_cleanup_defaults.profile)))
            .simplified());
    parsed_mask.insert(
        QStringLiteral("matteCleanupDefaultsApplied"),
        constraints.contains(QStringLiteral("matteCleanupDefaultsApplied"))
            ? constraints.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool(true)
            : geometry.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool(true));
    if (parsed_mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("selection")) {
      parsed_mask.insert(QStringLiteral("maskSetId"), mask_set_id);
      parsed_mask.insert(QStringLiteral("maskSetName"), mask_set_name);
    }
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionInvert"),
        adjustments.contains(QStringLiteral("maskSetSelectionInvert"))
            ? adjustments.value(QStringLiteral("maskSetSelectionInvert")).toBool()
            : mask_object.value(QStringLiteral("maskSetSelectionInvert")).toBool(false));
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionExpand"),
        adjustments.contains(QStringLiteral("maskSetSelectionExpand"))
            ? adjustments.value(QStringLiteral("maskSetSelectionExpand")).toDouble()
            : mask_object.value(QStringLiteral("maskSetSelectionExpand")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionSmooth"),
        adjustments.contains(QStringLiteral("maskSetSelectionSmooth"))
            ? adjustments.value(QStringLiteral("maskSetSelectionSmooth")).toDouble()
            : mask_object.value(QStringLiteral("maskSetSelectionSmooth")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionFineEdge"),
        adjustments.contains(QStringLiteral("maskSetSelectionFineEdge"))
            ? adjustments.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble()
            : mask_object.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionEdgeContrast"),
        adjustments.contains(QStringLiteral("maskSetSelectionEdgeContrast"))
            ? adjustments.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble()
            : mask_object.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("maskSetDensity"),
        adjustments.contains(QStringLiteral("maskSetDensity"))
            ? adjustments.value(QStringLiteral("maskSetDensity")).toDouble(1.0)
            : mask_object.value(QStringLiteral("maskSetDensity")).toDouble(1.0));
    parsed_mask.insert(
        QStringLiteral("maskSetOpacity"),
        adjustments.contains(QStringLiteral("maskSetOpacity"))
            ? adjustments.value(QStringLiteral("maskSetOpacity")).toDouble(1.0)
            : mask_object.value(QStringLiteral("maskSetOpacity")).toDouble(1.0));

    if (kind == QStringLiteral("linear")) {
      state.linear_mask_enabled =
          mask_object.contains(QStringLiteral("enabled"))
              ? mask_object.value(QStringLiteral("enabled")).toBool()
              : true;
      state.linear_mask_exposure = adjustments.value(QStringLiteral("exposure")).toDouble();
      state.linear_mask_temperature = adjustments.value(QStringLiteral("temperature")).toDouble();
      state.linear_mask_feather = geometry.value(QStringLiteral("feather")).toDouble(0.22);
      state.linear_mask_position = geometry.value(QStringLiteral("position")).toDouble(0.5);
      state.linear_mask_rotation = geometry.value(QStringLiteral("rotation")).toDouble();
      parsed_mask.insert(QStringLiteral("feather"), state.linear_mask_feather);
      parsed_mask.insert(QStringLiteral("position"), state.linear_mask_position);
      parsed_mask.insert(QStringLiteral("rotation"), state.linear_mask_rotation);
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, linear_mask_ordinal));
      ++linear_mask_ordinal;
      return;
    }

    if (is_center_radius_mask_kind(kind)) {
      const auto default_feather = kind == QStringLiteral("radial") ? 0.25 : 0.08;
      const auto default_radius_x = kind == QStringLiteral("rectangle") ? 0.30 : 0.25;
      const auto default_radius_y = kind == QStringLiteral("rectangle") ? 0.20 : 0.25;
      const auto feather = geometry.value(QStringLiteral("feather")).toDouble(default_feather);
      const auto center_x = geometry.value(QStringLiteral("centerX")).toDouble(0.5);
      const auto center_y = geometry.value(QStringLiteral("centerY")).toDouble(0.5);
      const auto radius_x = geometry.value(QStringLiteral("radiusX")).toDouble(default_radius_x);
      const auto radius_y = geometry.value(QStringLiteral("radiusY")).toDouble(default_radius_y);
      const auto rotation = geometry.value(QStringLiteral("rotation")).toDouble();
      if (kind == QStringLiteral("radial")) {
        state.radial_mask_enabled =
            mask_object.contains(QStringLiteral("enabled"))
                ? mask_object.value(QStringLiteral("enabled")).toBool()
                : true;
        state.radial_mask_exposure = adjustments.value(QStringLiteral("exposure")).toDouble();
        state.radial_mask_temperature = adjustments.value(QStringLiteral("temperature")).toDouble();
        state.radial_mask_feather = feather;
        state.radial_mask_center_x = center_x;
        state.radial_mask_center_y = center_y;
        state.radial_mask_radius_x = radius_x;
        state.radial_mask_radius_y = radius_y;
        state.radial_mask_rotation = rotation;
      }
      parsed_mask.insert(QStringLiteral("feather"), feather);
      parsed_mask.insert(QStringLiteral("centerX"), center_x);
      parsed_mask.insert(QStringLiteral("centerY"), center_y);
      parsed_mask.insert(QStringLiteral("radiusX"), radius_x);
      parsed_mask.insert(QStringLiteral("radiusY"), radius_y);
      parsed_mask.insert(QStringLiteral("rotation"), rotation);
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, radial_mask_ordinal));
      ++radial_mask_ordinal;
      return;
    }

    if (kind == QStringLiteral("brush")) {
      parsed_mask.insert(
          QStringLiteral("retouchMode"),
          geometry.value(QStringLiteral("retouchMode")).toString());
      const auto brush_preset = geometry.value(QStringLiteral("brushPreset")).toString().simplified();
      parsed_mask.insert(
          QStringLiteral("brushPreset"),
          brush_preset.isEmpty() ? QStringLiteral("Soft Round") : brush_preset);
      parsed_mask.insert(
          QStringLiteral("feather"),
          geometry.value(QStringLiteral("feather")).toDouble(0.75));
      parsed_mask.insert(
          QStringLiteral("brushSize"),
          geometry.value(QStringLiteral("brushSize")).toDouble(0.08));
      parsed_mask.insert(
          QStringLiteral("brushFlow"),
          geometry.value(QStringLiteral("brushFlow")).toDouble(1.0));
      parsed_mask.insert(
          QStringLiteral("brushHardness"),
          geometry.value(QStringLiteral("brushHardness")).toDouble(0.25));
      parsed_mask.insert(
          QStringLiteral("brushSpacing"),
          geometry.value(QStringLiteral("brushSpacing")).toDouble(0.75));
      parsed_mask.insert(
          QStringLiteral("brushSmoothing"),
          geometry.value(QStringLiteral("brushSmoothing")).toDouble(0.0));
      parsed_mask.insert(
          QStringLiteral("brushPressureSize"),
          qBound(0.0, geometry.value(QStringLiteral("brushPressureSize")).toDouble(0.65), 1.0));
      parsed_mask.insert(
          QStringLiteral("brushPressureFlow"),
          qBound(0.0, geometry.value(QStringLiteral("brushPressureFlow")).toDouble(1.0), 1.0));
      parsed_mask.insert(
          QStringLiteral("brushPressureOpacity"),
          qBound(0.0, geometry.value(QStringLiteral("brushPressureOpacity")).toDouble(0.0), 1.0));
      parsed_mask.insert(
          QStringLiteral("brushBuildup"),
          geometry.contains(QStringLiteral("brushBuildup"))
              ? geometry.value(QStringLiteral("brushBuildup")).toBool()
              : true);
      parsed_mask.insert(
          QStringLiteral("brushEraseMode"),
          geometry.contains(QStringLiteral("brushEraseMode"))
              ? geometry.value(QStringLiteral("brushEraseMode")).toBool()
              : false);
      parsed_mask.insert(
          QStringLiteral("brushAutoMask"),
          geometry.contains(QStringLiteral("brushAutoMask"))
              ? geometry.value(QStringLiteral("brushAutoMask")).toBool()
              : false);
      parsed_mask.insert(
          QStringLiteral("brushAutoMaskTolerance"),
          geometry.value(QStringLiteral("brushAutoMaskTolerance")).toDouble(0.18));
      parsed_mask.insert(
          QStringLiteral("brushAutoMaskContinuousSampling"),
          geometry.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("brushProtectEdges"),
          geometry.value(QStringLiteral("brushProtectEdges")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("healTextureBlend"),
          qBound(0.0, geometry.value(QStringLiteral("healTextureBlend")).toDouble(0.68), 1.0));
      parsed_mask.insert(
          QStringLiteral("healColorBlend"),
          qBound(0.0, geometry.value(QStringLiteral("healColorBlend")).toDouble(0.46), 1.0));
      parsed_mask.insert(
          QStringLiteral("brushRefineMode"),
          geometry.value(QStringLiteral("brushRefineMode")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("refineBrushRecalculate"),
          geometry.value(QStringLiteral("refineBrushRecalculate")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("brushRefineTargetId"),
          geometry.value(QStringLiteral("brushRefineTargetId")).toString());
      parsed_mask.insert(
          QStringLiteral("brushRefineTargetName"),
          geometry.value(QStringLiteral("brushRefineTargetName")).toString());
      parsed_mask.insert(
          QStringLiteral("brushRefineTargetKind"),
          geometry.value(QStringLiteral("brushRefineTargetKind")).toString());
      parsed_mask.insert(
          QStringLiteral("cloneSourceSet"),
          geometry.value(QStringLiteral("cloneSourceSet")).toBool());
      parsed_mask.insert(
          QStringLiteral("cloneSourceX"),
          geometry.value(QStringLiteral("cloneSourceX")).toDouble(0.5));
      parsed_mask.insert(
          QStringLiteral("cloneSourceY"),
          geometry.value(QStringLiteral("cloneSourceY")).toDouble(0.5));
      parsed_mask.insert(
          QStringLiteral("cloneOffsetX"),
          geometry.value(QStringLiteral("cloneOffsetX")).toDouble());
      parsed_mask.insert(
          QStringLiteral("cloneOffsetY"),
          geometry.value(QStringLiteral("cloneOffsetY")).toDouble());
      parsed_mask.insert(
          QStringLiteral("cloneScale"),
          geometry.value(QStringLiteral("cloneScale")).toDouble(1.0));
      parsed_mask.insert(
          QStringLiteral("cloneRotation"),
          geometry.value(QStringLiteral("cloneRotation")).toDouble());
      parsed_mask.insert(
          QStringLiteral("cloneAligned"),
          geometry.contains(QStringLiteral("cloneAligned"))
              ? geometry.value(QStringLiteral("cloneAligned")).toBool(true)
              : true);
      QVariantList clone_sources;
      const auto clone_source_array = geometry.value(QStringLiteral("cloneSources")).toArray();
      for (const auto& source_value : clone_source_array) {
        if (!source_value.isObject()) {
          continue;
        }
        const auto source_object = source_value.toObject();
        QVariantMap clone_source;
        clone_source.insert(QStringLiteral("name"), source_object.value(QStringLiteral("name")).toString());
        clone_source.insert(QStringLiteral("x"), source_object.value(QStringLiteral("x")).toDouble(0.5));
        clone_source.insert(QStringLiteral("y"), source_object.value(QStringLiteral("y")).toDouble(0.5));
        clone_source.insert(QStringLiteral("scale"), source_object.value(QStringLiteral("scale")).toDouble(1.0));
        clone_source.insert(QStringLiteral("rotation"), source_object.value(QStringLiteral("rotation")).toDouble());
        clone_source.insert(
            QStringLiteral("aligned"),
            source_object.contains(QStringLiteral("aligned"))
                ? source_object.value(QStringLiteral("aligned")).toBool(true)
                : true);
        clone_sources.append(clone_source);
      }
      parsed_mask.insert(QStringLiteral("cloneSources"), clone_sources);
      const auto clone_source_index =
          geometry.value(QStringLiteral("cloneSourceIndex")).toInt(-1);
      parsed_mask.insert(
          QStringLiteral("cloneSourceIndex"),
          clone_source_index >= 0 && clone_source_index < clone_sources.size()
              ? clone_source_index
              : -1);
      const auto patch_source_width =
          std::clamp(geometry.value(QStringLiteral("patchSourceWidth")).toDouble(0.18), 0.02, 1.0);
      const auto patch_source_height =
          std::clamp(geometry.value(QStringLiteral("patchSourceHeight")).toDouble(0.18), 0.02, 1.0);
      parsed_mask.insert(
          QStringLiteral("patchSourceSet"),
          geometry.value(QStringLiteral("patchSourceSet")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("patchSourceX"),
          std::clamp(geometry.value(QStringLiteral("patchSourceX")).toDouble(0.5), 0.0, 1.0 - patch_source_width));
      parsed_mask.insert(
          QStringLiteral("patchSourceY"),
          std::clamp(geometry.value(QStringLiteral("patchSourceY")).toDouble(0.5), 0.0, 1.0 - patch_source_height));
      parsed_mask.insert(QStringLiteral("patchSourceWidth"), patch_source_width);
      parsed_mask.insert(QStringLiteral("patchSourceHeight"), patch_source_height);
      QVariantList brush_points;
      const auto strokes = geometry.value(QStringLiteral("brushPoints")).toArray();
      for (const auto& stroke_value : strokes) {
        if (!stroke_value.isObject()) {
          continue;
        }
        const auto stroke = stroke_value.toObject();
        QVariantMap point;
        point.insert(QStringLiteral("x"), stroke.value(QStringLiteral("x")).toDouble());
        point.insert(QStringLiteral("y"), stroke.value(QStringLiteral("y")).toDouble());
        point.insert(QStringLiteral("size"), stroke.value(QStringLiteral("size")).toDouble(0.08));
        point.insert(QStringLiteral("flow"), stroke.value(QStringLiteral("flow")).toDouble(1.0));
        point.insert(QStringLiteral("opacity"), stroke.value(QStringLiteral("opacity")).toDouble(1.0));
        point.insert(
            QStringLiteral("autoMaskAnchorX"),
            stroke.value(QStringLiteral("autoMaskAnchorX")).toDouble(
                stroke.value(QStringLiteral("x")).toDouble()));
        point.insert(
            QStringLiteral("autoMaskAnchorY"),
            stroke.value(QStringLiteral("autoMaskAnchorY")).toDouble(
                stroke.value(QStringLiteral("y")).toDouble()));
        point.insert(QStringLiteral("pressure"), stroke.value(QStringLiteral("pressure")).toDouble(1.0));
        point.insert(QStringLiteral("tiltX"), stroke.value(QStringLiteral("tiltX")).toDouble());
        point.insert(QStringLiteral("tiltY"), stroke.value(QStringLiteral("tiltY")).toDouble());
        point.insert(QStringLiteral("erase"), stroke.value(QStringLiteral("erase")).toBool(false));
        brush_points.append(point);
      }
      parsed_mask.insert(QStringLiteral("brushPoints"), brush_points);
      QVariantList brush_strokes;
      const auto stroke_array = geometry.value(QStringLiteral("brushStrokes")).toArray();
      for (const auto& stroke_value : stroke_array) {
        if (!stroke_value.isObject()) {
          continue;
        }
        const auto stroke_object = stroke_value.toObject();
        QVariantList samples;
        for (const auto& sample_value : stroke_object.value(QStringLiteral("samples")).toArray()) {
          if (!sample_value.isObject()) {
            continue;
          }
          const auto sample = sample_value.toObject();
          QVariantMap point;
          point.insert(QStringLiteral("x"), sample.value(QStringLiteral("x")).toDouble());
          point.insert(QStringLiteral("y"), sample.value(QStringLiteral("y")).toDouble());
          point.insert(QStringLiteral("size"), sample.value(QStringLiteral("size")).toDouble(0.08));
          point.insert(QStringLiteral("flow"), sample.value(QStringLiteral("flow")).toDouble(1.0));
          point.insert(QStringLiteral("opacity"), sample.value(QStringLiteral("opacity")).toDouble(1.0));
          point.insert(
              QStringLiteral("autoMaskAnchorX"),
              sample.value(QStringLiteral("autoMaskAnchorX")).toDouble(
                  sample.value(QStringLiteral("x")).toDouble()));
          point.insert(
              QStringLiteral("autoMaskAnchorY"),
              sample.value(QStringLiteral("autoMaskAnchorY")).toDouble(
                  sample.value(QStringLiteral("y")).toDouble()));
          point.insert(QStringLiteral("pressure"), sample.value(QStringLiteral("pressure")).toDouble(1.0));
          point.insert(QStringLiteral("tiltX"), sample.value(QStringLiteral("tiltX")).toDouble());
          point.insert(QStringLiteral("tiltY"), sample.value(QStringLiteral("tiltY")).toDouble());
          point.insert(QStringLiteral("erase"), sample.value(QStringLiteral("erase")).toBool(false));
          samples.append(point);
        }
        QVariantMap stroke;
        stroke.insert(QStringLiteral("id"), stroke_object.value(QStringLiteral("id")).toString());
        stroke.insert(
            QStringLiteral("createdAtOrder"),
            stroke_object.value(QStringLiteral("createdAtOrder"))
                .toInt(static_cast<int>(brush_strokes.size()) + 1));
        stroke.insert(QStringLiteral("operation"), stroke_object.value(QStringLiteral("operation")).toString());
        stroke.insert(QStringLiteral("settings"), stroke_object.value(QStringLiteral("settings")).toObject().toVariantMap());
        stroke.insert(QStringLiteral("samples"), samples);
        brush_strokes.append(stroke);
      }
      parsed_mask.insert(QStringLiteral("brushStrokes"), brush_strokes);
      const auto segmentation_data =
          detail::read_byte_array(geometry, QStringLiteral("segmentationData"));
      const auto segmentation_width = geometry.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = geometry.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        parsed_mask.insert(QStringLiteral("segmentationData"), segmentation_data);
        parsed_mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        parsed_mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        parsed_mask.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, geometry.value(QStringLiteral("rasterSchemaVersion")).toInt(1)));
      }
      read_inpaint_patch_payload(&parsed_mask, geometry);
      read_generated_mask_metadata(&parsed_mask, geometry);
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
      parsed_mask.insert(
          QStringLiteral("feather"),
          geometry.value(QStringLiteral("feather")).toDouble(0.06));
      QVariantList polygon_points;
      const auto points = geometry.value(QStringLiteral("polygonPoints")).toArray();
      for (const auto& point_value : points) {
        if (!point_value.isObject()) {
          continue;
        }
        const auto point_object = point_value.toObject();
        QVariantMap point;
        point.insert(QStringLiteral("x"), point_object.value(QStringLiteral("x")).toDouble());
        point.insert(QStringLiteral("y"), point_object.value(QStringLiteral("y")).toDouble());
        polygon_points.append(point);
      }
      parsed_mask.insert(QStringLiteral("polygonPoints"), polygon_points);
      if (kind == QStringLiteral("lasso")) {
        parsed_mask.insert(QStringLiteral("lassoClosed"), geometry.value(QStringLiteral("lassoClosed")).toBool(false));
        parsed_mask.insert(
            QStringLiteral("lassoSimplified"),
            geometry.value(QStringLiteral("lassoSimplified")).toBool(false));
        parsed_mask.insert(QStringLiteral("lassoSmoothed"), geometry.value(QStringLiteral("lassoSmoothed")).toBool(false));
        parsed_mask.insert(
            QStringLiteral("lassoFreehandPath"),
            geometry.value(QStringLiteral("lassoFreehandPath")).toBool(true));
        parsed_mask.insert(
            QStringLiteral("lassoRawPointCount"),
            std::max(0, geometry.value(QStringLiteral("lassoRawPointCount")).toInt(0)));
        parsed_mask.insert(
            QStringLiteral("lassoSimplifiedPointCount"),
            std::max(
                0,
                geometry.contains(QStringLiteral("lassoSimplifiedPointCount"))
                    ? geometry.value(QStringLiteral("lassoSimplifiedPointCount")).toInt()
                    : static_cast<int>(polygon_points.size())));
        QVariantList lasso_raw_points;
        const auto raw_points = geometry.value(QStringLiteral("lassoRawPoints")).toArray();
        for (const auto& point_value : raw_points) {
          if (!point_value.isObject()) {
            continue;
          }
          const auto point_object = point_value.toObject();
          QVariantMap point;
          point.insert(QStringLiteral("x"), point_object.value(QStringLiteral("x")).toDouble());
          point.insert(QStringLiteral("y"), point_object.value(QStringLiteral("y")).toDouble());
          lasso_raw_points.append(point);
        }
        parsed_mask.insert(QStringLiteral("lassoRawPoints"), lasso_raw_points);
      }
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("depth")) {
      parsed_mask.insert(
          QStringLiteral("depthMin"),
          constraints.value(QStringLiteral("depthMin")).toDouble(0.2));
      parsed_mask.insert(
          QStringLiteral("depthMax"),
          constraints.value(QStringLiteral("depthMax")).toDouble(0.8));
      parsed_mask.insert(
          QStringLiteral("rangeFeather"),
          constraints.value(QStringLiteral("rangeFeather")).toDouble(0.1));
      parsed_mask.insert(
          QStringLiteral("depthSamples"),
          constraints.value(QStringLiteral("depthSamples")).toArray().toVariantList());
      const auto depth_data = detail::read_byte_array(geometry, QStringLiteral("depthData"));
      const auto depth_width = geometry.value(QStringLiteral("depthWidth")).toInt();
      const auto depth_height = geometry.value(QStringLiteral("depthHeight")).toInt();
      if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
        parsed_mask.insert(QStringLiteral("depthData"), depth_data);
        parsed_mask.insert(QStringLiteral("depthWidth"), depth_width);
        parsed_mask.insert(QStringLiteral("depthHeight"), depth_height);
      }
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("luminance")) {
      parsed_mask.insert(
          QStringLiteral("luminanceMin"),
          constraints.value(QStringLiteral("luminanceMin")).toDouble(0.2));
      parsed_mask.insert(
          QStringLiteral("luminanceMax"),
          constraints.value(QStringLiteral("luminanceMax")).toDouble(0.8));
      parsed_mask.insert(
          QStringLiteral("rangeFeather"),
          constraints.value(QStringLiteral("rangeFeather")).toDouble(0.1));
      parsed_mask.insert(
          QStringLiteral("luminanceSamples"),
          constraints.value(QStringLiteral("luminanceSamples")).toArray().toVariantList());
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("color")) {
      parsed_mask.insert(
          QStringLiteral("hueCenter"),
          constraints.value(QStringLiteral("hueCenter")).toDouble(0.08));
      parsed_mask.insert(
          QStringLiteral("hueRange"),
          constraints.value(QStringLiteral("hueRange")).toDouble(0.12));
      parsed_mask.insert(
          QStringLiteral("saturationMin"),
          constraints.value(QStringLiteral("saturationMin")).toDouble(0.1));
      parsed_mask.insert(
          QStringLiteral("saturationMax"),
          constraints.value(QStringLiteral("saturationMax")).toDouble(1.0));
      parsed_mask.insert(
          QStringLiteral("rangeFeather"),
          constraints.value(QStringLiteral("rangeFeather")).toDouble(0.08));
      parsed_mask.insert(
          QStringLiteral("colorSamples"),
          constraints.value(QStringLiteral("colorSamples")).toArray().toVariantList());
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (is_raster_mask_kind(kind) || kind == QStringLiteral("brush")) {
      parsed_mask.insert(
          QStringLiteral("retouchMode"),
          geometry.value(QStringLiteral("retouchMode")).toString());
      parsed_mask.insert(
          QStringLiteral("feather"),
          geometry.value(QStringLiteral("feather")).toDouble(0.5));
      if (kind == QStringLiteral("alpha")) {
        parsed_mask.insert(
            QStringLiteral("alphaSourcePath"),
            geometry.value(QStringLiteral("alphaSourcePath")).toString());
        parsed_mask.insert(
            QStringLiteral("alphaImportInvert"),
            geometry.value(QStringLiteral("alphaImportInvert")).toBool(false));
        parsed_mask.insert(
            QStringLiteral("alphaImportThreshold"),
            geometry.value(QStringLiteral("alphaImportThreshold")).toDouble(0.0));
        parsed_mask.insert(
            QStringLiteral("alphaImportResampleMode"),
            geometry.value(QStringLiteral("alphaImportResampleMode")).toString(QStringLiteral("Original")));
      }
      const auto segmentation_data =
          detail::read_byte_array(geometry, QStringLiteral("segmentationData"));
      const auto segmentation_width = geometry.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = geometry.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        parsed_mask.insert(QStringLiteral("segmentationData"), segmentation_data);
        parsed_mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        parsed_mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        parsed_mask.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, geometry.value(QStringLiteral("rasterSchemaVersion")).toInt(1)));
      }
      read_inpaint_patch_payload(&parsed_mask, geometry);
      read_generated_mask_metadata(&parsed_mask, geometry);
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
    }
  };

  const auto mask_sets = object.value(QStringLiteral("maskSets")).toArray();
  for (const auto& set_value : mask_sets) {
    if (!set_value.isObject()) {
      continue;
    }

    const auto mask_set = set_value.toObject();
    const auto set_adjustments = detail::object_value(mask_set, QStringLiteral("adjustments"));
    const auto set_id = mask_set.value(QStringLiteral("id")).toString();
    const auto set_name =
        mask_set.value(QStringLiteral("name")).toString(
            default_mask_set_name(QStringLiteral("linear"), static_cast<int>(parsed_mask_stack.size()) + 1));
    const auto sources = mask_set.value(QStringLiteral("sources")).toArray();
    for (const auto& source_value : sources) {
      if (!source_value.isObject()) {
        continue;
      }
      const auto source_object = source_value.toObject();
      auto source_adjustments = set_adjustments;
      const auto adjustment_overrides =
          detail::object_value(source_object, QStringLiteral("adjustmentOverrides"));
      for (auto iterator = adjustment_overrides.constBegin();
           iterator != adjustment_overrides.constEnd();
           ++iterator) {
        source_adjustments.insert(iterator.key(), iterator.value());
      }
      append_parsed_mask(
          source_object,
          source_adjustments,
          QStringLiteral("selection"),
          set_id,
          set_name);
    }
  }

  const auto retouch_stack = object.value(QStringLiteral("retouchStack")).toArray();
  for (const auto& value : retouch_stack) {
    if (!value.isObject()) {
      continue;
    }
    append_parsed_mask(
        value.toObject(),
        QJsonObject{},
        QStringLiteral("retouch"),
        QString(),
        QString());
  }

  const auto legacy_mask_stack = object.value(QStringLiteral("maskStack")).toArray();
  if (parsed_mask_stack.isEmpty()) {
    for (const auto& value : legacy_mask_stack) {
      if (!value.isObject()) {
        continue;
      }
      append_parsed_mask(
          value.toObject(),
          QJsonObject{},
          QStringLiteral("selection"),
          QString(),
          QString());
    }
  }

  if (parsed_mask_stack.isEmpty()) {
    state.linear_mask_enabled =
        object.value(QStringLiteral("linearMaskEnabled")).toBool();
    state.linear_mask_exposure =
        object.value(QStringLiteral("linearMaskExposure")).toDouble();
    state.linear_mask_temperature =
        object.value(QStringLiteral("linearMaskTemperature")).toDouble();
    state.linear_mask_feather =
        object.value(QStringLiteral("linearMaskFeather")).toDouble(0.22);
    state.linear_mask_position =
        object.value(QStringLiteral("linearMaskPosition")).toDouble(0.5);
    state.linear_mask_rotation =
        object.value(QStringLiteral("linearMaskRotation")).toDouble();

    if (has_structured_linear_mask(state)) {
      QVariantMap linear_mask = default_mask_definition(QStringLiteral("linear"), 1);
      linear_mask.insert(QStringLiteral("enabled"), state.linear_mask_enabled);
      linear_mask.insert(QStringLiteral("exposure"), state.linear_mask_exposure);
      linear_mask.insert(QStringLiteral("temperature"), state.linear_mask_temperature);
      linear_mask.insert(QStringLiteral("feather"), state.linear_mask_feather);
      linear_mask.insert(QStringLiteral("position"), state.linear_mask_position);
      linear_mask.insert(QStringLiteral("rotation"), state.linear_mask_rotation);
      parsed_mask_stack.append(linear_mask);
    }

    state.radial_mask_enabled = object.value(QStringLiteral("radialMaskEnabled")).toBool();
    state.radial_mask_exposure = object.value(QStringLiteral("radialMaskExposure")).toDouble();
    state.radial_mask_temperature = object.value(QStringLiteral("radialMaskTemperature")).toDouble();
    state.radial_mask_feather = object.value(QStringLiteral("radialMaskFeather")).toDouble(0.25);
    state.radial_mask_center_x = object.value(QStringLiteral("radialMaskCenterX")).toDouble(0.5);
    state.radial_mask_center_y = object.value(QStringLiteral("radialMaskCenterY")).toDouble(0.5);
    state.radial_mask_radius_x = object.value(QStringLiteral("radialMaskRadiusX")).toDouble(0.25);
    state.radial_mask_radius_y = object.value(QStringLiteral("radialMaskRadiusY")).toDouble(0.25);
    state.radial_mask_rotation = object.value(QStringLiteral("radialMaskRotation")).toDouble();

    if (has_structured_radial_mask(state)) {
      QVariantMap radial_mask = default_mask_definition(QStringLiteral("radial"), 1);
      radial_mask.insert(QStringLiteral("enabled"), state.radial_mask_enabled);
      radial_mask.insert(QStringLiteral("exposure"), state.radial_mask_exposure);
      radial_mask.insert(QStringLiteral("temperature"), state.radial_mask_temperature);
      radial_mask.insert(QStringLiteral("feather"), state.radial_mask_feather);
      radial_mask.insert(QStringLiteral("centerX"), state.radial_mask_center_x);
      radial_mask.insert(QStringLiteral("centerY"), state.radial_mask_center_y);
      radial_mask.insert(QStringLiteral("radiusX"), state.radial_mask_radius_x);
      radial_mask.insert(QStringLiteral("radiusY"), state.radial_mask_radius_y);
      radial_mask.insert(QStringLiteral("rotation"), state.radial_mask_rotation);
      parsed_mask_stack.append(radial_mask);
    }
  }

  state.mask_stack = normalize_mask_stack(parsed_mask_stack);

  return state;
}

inline QJsonObject build_edit_recipe_object(const EditRecipeState& state) {
  QJsonObject raw_adjustments;
  raw_adjustments.insert(QStringLiteral("inputTransformEnabled"), state.raw_input_transform_enabled);
  raw_adjustments.insert(
      QStringLiteral("highlightReconstructionMode"),
      normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode));
  raw_adjustments.insert(
      QStringLiteral("chromaticAdaptationMethod"),
      normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method));
  raw_adjustments.insert(
      QStringLiteral("demosaicQuality"),
      normalized_raw_demosaic_quality_name(state.raw_demosaic_quality));
  raw_adjustments.insert(
      QStringLiteral("inputProfile"),
      normalized_input_color_profile_name(state.input_color_profile));
  raw_adjustments.insert(QStringLiteral("exposure"), state.raw_exposure);
  raw_adjustments.insert(QStringLiteral("boost"), state.raw_boost);
  raw_adjustments.insert(QStringLiteral("shadowBoost"), state.raw_shadow_boost);
  raw_adjustments.insert(QStringLiteral("detail"), state.raw_detail);
  raw_adjustments.insert(QStringLiteral("chromaNoiseReduction"), state.raw_chroma_noise_reduction);
  raw_adjustments.insert(QStringLiteral("moireReduction"), state.raw_moire_reduction);
  raw_adjustments.insert(QStringLiteral("hotDeadPixelCleanup"), state.raw_hot_dead_pixel_cleanup);
  raw_adjustments.insert(QStringLiteral("falseColorSuppression"), state.raw_false_color_suppression);
  raw_adjustments.insert(QStringLiteral("highlightRecovery"), state.raw_highlight_recovery);
  raw_adjustments.insert(QStringLiteral("gamutMapping"), state.raw_gamut_mapping);
  raw_adjustments.insert(QStringLiteral("lensCorrection"), state.raw_lens_correction);
  raw_adjustments.insert(QStringLiteral("whiteBalanceOverride"), state.raw_white_balance_override);
  raw_adjustments.insert(
      QStringLiteral("cameraMatchingLook"),
      normalized_camera_matching_look_name(state.raw_camera_matching_look));
  raw_adjustments.insert(
      QStringLiteral("whiteBalanceTemperature"), state.raw_white_balance_temperature);
  raw_adjustments.insert(QStringLiteral("whiteBalanceTint"), state.raw_white_balance_tint);
  raw_adjustments.insert(QStringLiteral("blackLevel"), state.raw_black_level);
  raw_adjustments.insert(QStringLiteral("whiteLevel"), state.raw_white_level);
  raw_adjustments.insert(QStringLiteral("calibrationHue"), state.raw_calibration_hue);
  raw_adjustments.insert(QStringLiteral("calibrationSaturation"), state.raw_calibration_saturation);
  raw_adjustments.insert(QStringLiteral("redPrimaryHue"), state.raw_red_primary_hue);
  raw_adjustments.insert(QStringLiteral("redPrimarySaturation"), state.raw_red_primary_saturation);
  raw_adjustments.insert(QStringLiteral("greenPrimaryHue"), state.raw_green_primary_hue);
  raw_adjustments.insert(QStringLiteral("greenPrimarySaturation"), state.raw_green_primary_saturation);
  raw_adjustments.insert(QStringLiteral("bluePrimaryHue"), state.raw_blue_primary_hue);
  raw_adjustments.insert(QStringLiteral("bluePrimarySaturation"), state.raw_blue_primary_saturation);

  QJsonObject global_adjustments;
  global_adjustments.insert(QStringLiteral("exposure"), state.exposure);
  global_adjustments.insert(QStringLiteral("contrast"), state.contrast);
  global_adjustments.insert(QStringLiteral("highlights"), state.highlights);
  global_adjustments.insert(QStringLiteral("shadows"), state.shadows);
  global_adjustments.insert(QStringLiteral("whites"), state.whites);
  global_adjustments.insert(QStringLiteral("blacks"), state.blacks);
  global_adjustments.insert(QStringLiteral("brightness"), state.brightness);
  global_adjustments.insert(QStringLiteral("hdrLocalTone"), state.hdr_local_tone_mapping);
  global_adjustments.insert(QStringLiteral("filmicShoulder"), state.filmic_shoulder);
  global_adjustments.insert(QStringLiteral("filmicToe"), state.filmic_toe);
  global_adjustments.insert(QStringLiteral("adaptiveContrast"), state.adaptive_contrast);
  global_adjustments.insert(QStringLiteral("levelsBlackPoint"), state.levels_black_point);
  global_adjustments.insert(QStringLiteral("levelsGamma"), state.levels_gamma);
  global_adjustments.insert(QStringLiteral("levelsWhitePoint"), state.levels_white_point);
  global_adjustments.insert(QStringLiteral("zoneDeepShadows"), state.zone_deep_shadows);
  global_adjustments.insert(QStringLiteral("zoneShadows"), state.zone_shadows);
  global_adjustments.insert(QStringLiteral("zoneMidtones"), state.zone_midtones);
  global_adjustments.insert(QStringLiteral("zoneLights"), state.zone_lights);
  global_adjustments.insert(QStringLiteral("zoneHighlights"), state.zone_highlights);
  global_adjustments.insert(QStringLiteral("temperature"), state.temperature);
  global_adjustments.insert(QStringLiteral("tint"), state.tint);
  global_adjustments.insert(QStringLiteral("texture"), state.texture);
  global_adjustments.insert(QStringLiteral("clarity"), state.clarity);
  global_adjustments.insert(QStringLiteral("dehaze"), state.dehaze);
  global_adjustments.insert(QStringLiteral("sharpening"), state.sharpening);
  global_adjustments.insert(QStringLiteral("sharpenRadius"), state.sharpen_radius);
  global_adjustments.insert(QStringLiteral("sharpenDetail"), state.sharpen_detail);
  global_adjustments.insert(QStringLiteral("sharpenMasking"), state.sharpen_masking);
  global_adjustments.insert(QStringLiteral("luminanceNr"), state.luminance_nr);
  global_adjustments.insert(QStringLiteral("colorNr"), state.color_nr);
  global_adjustments.insert(QStringLiteral("vibrance"), state.vibrance);
  global_adjustments.insert(QStringLiteral("saturation"), state.saturation);

  QJsonObject optics;
  optics.insert(QStringLiteral("lensDistortion"), state.lens_distortion);
  optics.insert(QStringLiteral("lensVignette"), state.lens_vignette);
  optics.insert(QStringLiteral("chromaticAberration"), state.chromatic_aberration);

  QJsonObject parametric_curve;
  parametric_curve.insert(QStringLiteral("shadows"), state.curve_shadows);
  parametric_curve.insert(QStringLiteral("darks"), state.curve_darks);
  parametric_curve.insert(QStringLiteral("lights"), state.curve_lights);
  parametric_curve.insert(QStringLiteral("highlights"), state.curve_highlights);

  QJsonObject curves;
  curves.insert(QStringLiteral("mode"), QStringLiteral("parametric"));
  curves.insert(QStringLiteral("parametric"), parametric_curve);
  QJsonArray channels;
  const auto append_channel = [&channels](const QString& channel_name, const QVariantList& points) {
    if (points.isEmpty()) {
      return;
    }

    QJsonObject channel;
    channel.insert(QStringLiteral("channel"), channel_name.toLower());
    channel.insert(QStringLiteral("points"), serialize_curve_points(points));
    channels.append(channel);
  };
  append_channel(QStringLiteral("Master"), state.master_curve_points);
  append_channel(QStringLiteral("Luma"), state.luma_curve_points);
  append_channel(QStringLiteral("Red"), state.red_curve_points);
  append_channel(QStringLiteral("Green"), state.green_curve_points);
  append_channel(QStringLiteral("Blue"), state.blue_curve_points);
  curves.insert(QStringLiteral("channels"), channels);

  QJsonObject color_mixer;
  color_mixer.insert(QStringLiteral("model"), state.color_mixer_model);
  color_mixer.insert(QStringLiteral("channel"), state.hsl_channel);
  color_mixer.insert(QStringLiteral("hue"), state.hue_adjustment);
  color_mixer.insert(QStringLiteral("saturation"), state.saturation_adjustment);
  color_mixer.insert(QStringLiteral("luminance"), state.luminance_adjustment);
  {
    static const QStringList kMixerChannelNames = {
        QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
        QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
        QStringLiteral("Purples"), QStringLiteral("Magentas"),
    };
    QJsonObject mixer_channels_obj;
    for (int i = 0; i < 8; ++i) {
      const auto& ch = state.mixer_channels[static_cast<std::size_t>(i)];
      if (std::abs(ch.hue) > 1e-6 || std::abs(ch.saturation) > 1e-6 || std::abs(ch.luminance) > 1e-6 ||
          std::abs(ch.selective_cyan) > 1e-6 || std::abs(ch.selective_magenta) > 1e-6 ||
          std::abs(ch.selective_yellow) > 1e-6 || std::abs(ch.selective_black) > 1e-6) {
        QJsonObject ch_obj;
        ch_obj.insert(QStringLiteral("hue"), ch.hue);
        ch_obj.insert(QStringLiteral("saturation"), ch.saturation);
        ch_obj.insert(QStringLiteral("luminance"), ch.luminance);
        ch_obj.insert(QStringLiteral("selectiveCyan"), ch.selective_cyan);
        ch_obj.insert(QStringLiteral("selectiveMagenta"), ch.selective_magenta);
        ch_obj.insert(QStringLiteral("selectiveYellow"), ch.selective_yellow);
        ch_obj.insert(QStringLiteral("selectiveBlack"), ch.selective_black);
        mixer_channels_obj.insert(kMixerChannelNames[i], ch_obj);
      }
    }
    if (!mixer_channels_obj.isEmpty()) {
      color_mixer.insert(QStringLiteral("channels"), mixer_channels_obj);
    }
  }
  if (std::abs(state.rgb_channel_mixer.red_from_red - 100.0) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.red_from_green) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.red_from_blue) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.green_from_red) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.green_from_green - 100.0) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.green_from_blue) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.blue_from_red) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.blue_from_green) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.blue_from_blue - 100.0) > 1e-6) {
    QJsonObject rgb_channel_mixer;
    rgb_channel_mixer.insert(QStringLiteral("redFromRed"), state.rgb_channel_mixer.red_from_red);
    rgb_channel_mixer.insert(QStringLiteral("redFromGreen"), state.rgb_channel_mixer.red_from_green);
    rgb_channel_mixer.insert(QStringLiteral("redFromBlue"), state.rgb_channel_mixer.red_from_blue);
    rgb_channel_mixer.insert(QStringLiteral("greenFromRed"), state.rgb_channel_mixer.green_from_red);
    rgb_channel_mixer.insert(QStringLiteral("greenFromGreen"), state.rgb_channel_mixer.green_from_green);
    rgb_channel_mixer.insert(QStringLiteral("greenFromBlue"), state.rgb_channel_mixer.green_from_blue);
    rgb_channel_mixer.insert(QStringLiteral("blueFromRed"), state.rgb_channel_mixer.blue_from_red);
    rgb_channel_mixer.insert(QStringLiteral("blueFromGreen"), state.rgb_channel_mixer.blue_from_green);
    rgb_channel_mixer.insert(QStringLiteral("blueFromBlue"), state.rgb_channel_mixer.blue_from_blue);
    color_mixer.insert(QStringLiteral("rgbChannelMixer"), rgb_channel_mixer);
  }
  if (state.black_and_white_enabled ||
      std::any_of(state.black_and_white_mixer.begin(), state.black_and_white_mixer.end(), [](double value) {
        return std::abs(value) > 1e-6;
      })) {
    static const QStringList kMixerChannelNames = {
        QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
        QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
        QStringLiteral("Purples"), QStringLiteral("Magentas"),
    };
    QJsonObject channels;
    for (int i = 0; i < 8; ++i) {
      channels.insert(kMixerChannelNames[i], state.black_and_white_mixer[static_cast<std::size_t>(i)]);
    }
    QJsonObject black_and_white;
    black_and_white.insert(QStringLiteral("enabled"), state.black_and_white_enabled);
    black_and_white.insert(QStringLiteral("channels"), channels);
    color_mixer.insert(QStringLiteral("blackAndWhite"), black_and_white);
  }
  if (state.color_replacement_enabled ||
      std::abs(state.color_replacement_strength) > 1e-6 ||
      std::abs(state.color_replacement_source_hue) > 1e-6 ||
      std::abs(state.color_replacement_target_hue - 0.33) > 1e-6 ||
      std::abs(state.color_replacement_range - 35.0) > 1e-6 ||
      std::abs(state.color_replacement_softness - 100.0) > 1e-6 ||
      !state.color_replacement_preserve_luminance ||
      !state.color_replacement_preserve_saturation) {
    QJsonObject color_replacement;
    color_replacement.insert(QStringLiteral("enabled"), state.color_replacement_enabled);
    color_replacement.insert(QStringLiteral("sourceHue"), state.color_replacement_source_hue);
    color_replacement.insert(QStringLiteral("targetHue"), state.color_replacement_target_hue);
    color_replacement.insert(QStringLiteral("range"), state.color_replacement_range);
    color_replacement.insert(QStringLiteral("softness"), state.color_replacement_softness);
    color_replacement.insert(QStringLiteral("strength"), state.color_replacement_strength);
    color_replacement.insert(QStringLiteral("preserveLuminance"), state.color_replacement_preserve_luminance);
    color_replacement.insert(QStringLiteral("preserveSaturation"), state.color_replacement_preserve_saturation);
    color_mixer.insert(QStringLiteral("colorReplacement"), color_replacement);
  }
  if (state.skin_tone_protection_enabled ||
      std::abs(state.skin_tone_line_hue - 0.08) > 1e-6 ||
      std::abs(state.skin_tone_protection_range - 30.0) > 1e-6 ||
      std::abs(state.skin_tone_protection_strength) > 1e-6) {
    QJsonObject skin_tone;
    skin_tone.insert(QStringLiteral("enabled"), state.skin_tone_protection_enabled);
    skin_tone.insert(QStringLiteral("lineHue"), state.skin_tone_line_hue);
    skin_tone.insert(QStringLiteral("range"), state.skin_tone_protection_range);
    skin_tone.insert(QStringLiteral("strength"), state.skin_tone_protection_strength);
    color_mixer.insert(QStringLiteral("skinToneProtection"), skin_tone);
  }
  if (state.color_harmony_enabled ||
      state.color_harmony_mode != QStringLiteral("Complementary") ||
      std::abs(state.color_harmony_base_hue) > 1e-6 ||
      std::abs(state.color_harmony_strength) > 1e-6) {
    QJsonObject color_harmony;
    color_harmony.insert(QStringLiteral("enabled"), state.color_harmony_enabled);
    color_harmony.insert(QStringLiteral("mode"), state.color_harmony_mode);
    color_harmony.insert(QStringLiteral("baseHue"), state.color_harmony_base_hue);
    color_harmony.insert(QStringLiteral("strength"), state.color_harmony_strength);
    color_mixer.insert(QStringLiteral("colorHarmony"), color_harmony);
  }

  QJsonObject grading_shadows;
  grading_shadows.insert(QStringLiteral("hue"), state.grading_shadows_hue);
  grading_shadows.insert(QStringLiteral("saturation"), state.grading_shadows_saturation);
  grading_shadows.insert(QStringLiteral("luminance"), state.grading_shadows_luminance);
  QJsonObject grading_midtones;
  grading_midtones.insert(QStringLiteral("hue"), state.grading_midtones_hue);
  grading_midtones.insert(QStringLiteral("saturation"), state.grading_midtones_saturation);
  grading_midtones.insert(QStringLiteral("luminance"), state.grading_midtones_luminance);
  QJsonObject grading_highlights;
  grading_highlights.insert(QStringLiteral("hue"), state.grading_highlights_hue);
  grading_highlights.insert(QStringLiteral("saturation"), state.grading_highlights_saturation);
  grading_highlights.insert(QStringLiteral("luminance"), state.grading_highlights_luminance);
  QJsonObject color_grading;
  color_grading.insert(QStringLiteral("shadows"), grading_shadows);
  color_grading.insert(QStringLiteral("midtones"), grading_midtones);
  color_grading.insert(QStringLiteral("highlights"), grading_highlights);
  color_grading.insert(QStringLiteral("amount"), state.grading_amount);
  color_grading.insert(QStringLiteral("balance"), state.grading_balance);

  QJsonObject crop_transform;
  crop_transform.insert(QStringLiteral("preset"), state.crop_preset);
  crop_transform.insert(QStringLiteral("zoom"), state.crop_zoom);
  crop_transform.insert(QStringLiteral("uprightMode"), normalized_upright_mode_name(state.upright_mode));
  crop_transform.insert(QStringLiteral("uprightVerticalGuideAngle"), state.upright_vertical_guide_angle);
  crop_transform.insert(QStringLiteral("uprightHorizontalGuideAngle"), state.upright_horizontal_guide_angle);
  crop_transform.insert(QStringLiteral("perspectiveVertical"), state.perspective_vertical);
  crop_transform.insert(QStringLiteral("perspectiveHorizontal"), state.perspective_horizontal);
  crop_transform.insert(QStringLiteral("rotation"), state.transform_rotation);
  crop_transform.insert(QStringLiteral("scale"), state.transform_scale);
  crop_transform.insert(QStringLiteral("skewX"), state.transform_skew_x);
  crop_transform.insert(QStringLiteral("skewY"), state.transform_skew_y);
  crop_transform.insert(QStringLiteral("liquifyCenterX"), state.liquify_center_x);
  crop_transform.insert(QStringLiteral("liquifyCenterY"), state.liquify_center_y);
  crop_transform.insert(QStringLiteral("liquifyRadius"), state.liquify_radius);
  crop_transform.insert(QStringLiteral("liquifyStrength"), state.liquify_strength);
  crop_transform.insert(QStringLiteral("contentAwareFill"), state.content_aware_crop_fill);
  if (state.crop_rect_width > 0.0 && state.crop_rect_height > 0.0) {
    crop_transform.insert(QStringLiteral("x"), state.crop_rect_x);
    crop_transform.insert(QStringLiteral("y"), state.crop_rect_y);
    crop_transform.insert(QStringLiteral("width"), state.crop_rect_width);
    crop_transform.insert(QStringLiteral("height"), state.crop_rect_height);
  }

  QJsonObject output_transform;
  output_transform.insert(
      QStringLiteral("colorSpace"),
      normalized_output_color_space_name(state.output_color_space));
  output_transform.insert(
      QStringLiteral("previewColorSpace"),
      normalized_output_color_space_name(state.preview_color_space));
  const auto monitor_profile = normalized_monitor_color_profile_name(state.monitor_color_profile);
  if (!monitor_profile.isEmpty()) {
    output_transform.insert(QStringLiteral("monitorProfile"), monitor_profile);
  }
  output_transform.insert(QStringLiteral("softProofEnabled"), state.soft_proof_enabled);
  output_transform.insert(
      QStringLiteral("softProofProfile"),
      normalized_output_color_space_name(state.soft_proof_profile));
  output_transform.insert(
      QStringLiteral("workingColorSpace"),
      normalized_working_color_profile_name(state.working_color_profile));
  output_transform.insert(
      QStringLiteral("renderIntent"),
      normalized_render_intent_name(state.render_intent));
  output_transform.insert(QStringLiteral("headroom"), qBound(1.0, state.output_headroom, 16.0));

  QJsonObject color_pipeline;
  color_pipeline.insert(
      QStringLiteral("inputProfile"),
      normalized_input_color_profile_name(state.input_color_profile));
  color_pipeline.insert(
      QStringLiteral("workingProfile"),
      normalized_working_color_profile_name(state.working_color_profile));
  color_pipeline.insert(
      QStringLiteral("renderIntent"),
      normalized_render_intent_name(state.render_intent));

  QJsonArray mask_stack;
  QJsonArray retouch_stack;
  const auto serialize_mask_object = [](const QVariantMap& candidate, int ordinal) {
    const auto mask_definition = normalized_mask_definition(candidate, ordinal);
    const auto kind = normalized_mask_kind(mask_definition.value(QStringLiteral("kind")).toString());

    QJsonObject adjustments;
    adjustments.insert(QStringLiteral("exposure"), mask_definition.value(QStringLiteral("exposure")).toDouble());
    adjustments.insert(QStringLiteral("contrast"), mask_definition.value(QStringLiteral("contrast")).toDouble());
    adjustments.insert(QStringLiteral("highlights"), mask_definition.value(QStringLiteral("highlights")).toDouble());
    adjustments.insert(QStringLiteral("shadows"), mask_definition.value(QStringLiteral("shadows")).toDouble());
    adjustments.insert(QStringLiteral("whites"), mask_definition.value(QStringLiteral("whites")).toDouble());
    adjustments.insert(QStringLiteral("blacks"), mask_definition.value(QStringLiteral("blacks")).toDouble());
    adjustments.insert(
        QStringLiteral("temperature"),
        mask_definition.value(QStringLiteral("temperature")).toDouble());
    adjustments.insert(QStringLiteral("tint"), mask_definition.value(QStringLiteral("tint")).toDouble());
    adjustments.insert(QStringLiteral("vibrance"), mask_definition.value(QStringLiteral("vibrance")).toDouble());
    adjustments.insert(QStringLiteral("saturation"), mask_definition.value(QStringLiteral("saturation")).toDouble());
    adjustments.insert(QStringLiteral("hueShift"), mask_definition.value(QStringLiteral("hueShift")).toDouble());
    adjustments.insert(
        QStringLiteral("saturationShift"),
        mask_definition.value(QStringLiteral("saturationShift")).toDouble());
    adjustments.insert(QStringLiteral("colorizeHue"), mask_definition.value(QStringLiteral("colorizeHue")).toDouble());
    adjustments.insert(
        QStringLiteral("colorizeStrength"),
        mask_definition.value(QStringLiteral("colorizeStrength")).toDouble());
    adjustments.insert(
        QStringLiteral("removeStrength"),
        mask_definition.value(QStringLiteral("removeStrength")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionInvert"),
        mask_definition.value(QStringLiteral("selectionInvert")).toBool());
    adjustments.insert(
        QStringLiteral("selectionExpand"),
        mask_definition.value(QStringLiteral("selectionExpand")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionSmooth"),
        mask_definition.value(QStringLiteral("selectionSmooth")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionFineEdge"),
        mask_definition.value(QStringLiteral("selectionFineEdge")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionEdgeContrast"),
        mask_definition.value(QStringLiteral("selectionEdgeContrast")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionDecontaminate"),
        mask_definition.value(QStringLiteral("selectionDecontaminate")).toDouble());
    adjustments.insert(QStringLiteral("selectionGrow"), mask_definition.value(QStringLiteral("selectionGrow")).toDouble());
    adjustments.insert(QStringLiteral("selectionShrink"), mask_definition.value(QStringLiteral("selectionShrink")).toDouble());
    adjustments.insert(QStringLiteral("selectionFeather"), mask_definition.value(QStringLiteral("selectionFeather")).toDouble());
    adjustments.insert(QStringLiteral("selectionBlur"), mask_definition.value(QStringLiteral("selectionBlur")).toDouble());
    adjustments.insert(QStringLiteral("selectionHarden"), mask_definition.value(QStringLiteral("selectionHarden")).toDouble());
    adjustments.insert(QStringLiteral("selectionDespeckle"), mask_definition.value(QStringLiteral("selectionDespeckle")).toDouble());
    adjustments.insert(QStringLiteral("selectionFillHoles"), mask_definition.value(QStringLiteral("selectionFillHoles")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionRemoveIslands"),
        mask_definition.value(QStringLiteral("selectionRemoveIslands")).toDouble());
    adjustments.insert(
        QStringLiteral("maskSetSelectionInvert"),
        mask_definition.value(QStringLiteral("maskSetSelectionInvert")).toBool());
    adjustments.insert(
        QStringLiteral("maskSetSelectionExpand"),
        mask_definition.value(QStringLiteral("maskSetSelectionExpand")).toDouble());
    adjustments.insert(
        QStringLiteral("maskSetSelectionSmooth"),
        mask_definition.value(QStringLiteral("maskSetSelectionSmooth")).toDouble());
    adjustments.insert(
        QStringLiteral("maskSetSelectionFineEdge"),
        mask_definition.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble());
    adjustments.insert(
        QStringLiteral("maskSetSelectionEdgeContrast"),
        mask_definition.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble());
    adjustments.insert(QStringLiteral("maskSetDensity"), mask_definition.value(QStringLiteral("maskSetDensity"), 1.0).toDouble());
    adjustments.insert(QStringLiteral("maskSetOpacity"), mask_definition.value(QStringLiteral("maskSetOpacity"), 1.0).toDouble());

    QJsonObject geometry;
    geometry.insert(QStringLiteral("feather"), mask_definition.value(QStringLiteral("feather")).toDouble());
    geometry.insert(QStringLiteral("rotation"), mask_definition.value(QStringLiteral("rotation")).toDouble());
    QJsonObject constraints;
    constraints.insert(QStringLiteral("refineLuminanceMin"), mask_definition.value(QStringLiteral("refineLuminanceMin")).toDouble());
    constraints.insert(QStringLiteral("refineLuminanceMax"), mask_definition.value(QStringLiteral("refineLuminanceMax")).toDouble());
    constraints.insert(QStringLiteral("refineHueCenter"), mask_definition.value(QStringLiteral("refineHueCenter")).toDouble());
    constraints.insert(QStringLiteral("refineHueRange"), mask_definition.value(QStringLiteral("refineHueRange")).toDouble());
    constraints.insert(QStringLiteral("refineSaturationMin"), mask_definition.value(QStringLiteral("refineSaturationMin")).toDouble());
    constraints.insert(QStringLiteral("refineSaturationMax"), mask_definition.value(QStringLiteral("refineSaturationMax")).toDouble());
    constraints.insert(QStringLiteral("refineDepthMin"), mask_definition.value(QStringLiteral("refineDepthMin")).toDouble());
    constraints.insert(QStringLiteral("refineDepthMax"), mask_definition.value(QStringLiteral("refineDepthMax")).toDouble());
    constraints.insert(QStringLiteral("refineEdgeConfidenceMin"), mask_definition.value(QStringLiteral("refineEdgeConfidenceMin")).toDouble());
    constraints.insert(QStringLiteral("refineRangeFeather"), mask_definition.value(QStringLiteral("refineRangeFeather")).toDouble());
    constraints.insert(
        QStringLiteral("generatedMaskHoleFillThreshold"),
        mask_definition.contains(QStringLiteral("generatedMaskHoleFillThreshold"))
            ? mask_definition.value(QStringLiteral("generatedMaskHoleFillThreshold")).toDouble()
            : 0.004);
    constraints.insert(
        QStringLiteral("generatedMaskIslandRemovalThreshold"),
        mask_definition.contains(QStringLiteral("generatedMaskIslandRemovalThreshold"))
            ? mask_definition.value(QStringLiteral("generatedMaskIslandRemovalThreshold")).toDouble()
            : 0.0015);
    constraints.insert(
        QStringLiteral("matteCleanupSchemaVersion"),
        std::max(1, mask_definition.value(QStringLiteral("matteCleanupSchemaVersion"), 1).toInt()));
    constraints.insert(
        QStringLiteral("matteCleanupProfile"),
        mask_definition.value(QStringLiteral("matteCleanupProfile")).toString());
    constraints.insert(
        QStringLiteral("matteCleanupDefaultsApplied"),
        mask_definition.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool());
    if (is_center_radius_mask_kind(kind)) {
      geometry.insert(QStringLiteral("centerX"), mask_definition.value(QStringLiteral("centerX")).toDouble());
      geometry.insert(QStringLiteral("centerY"), mask_definition.value(QStringLiteral("centerY")).toDouble());
      geometry.insert(QStringLiteral("radiusX"), mask_definition.value(QStringLiteral("radiusX")).toDouble());
      geometry.insert(QStringLiteral("radiusY"), mask_definition.value(QStringLiteral("radiusY")).toDouble());
    } else if (kind == QStringLiteral("linear")) {
      geometry.insert(QStringLiteral("position"), mask_definition.value(QStringLiteral("position")).toDouble());
    } else if (kind == QStringLiteral("brush")) {
      geometry.insert(QStringLiteral("retouchMode"), mask_definition.value(QStringLiteral("retouchMode")).toString());
      geometry.insert(QStringLiteral("brushPreset"), mask_definition.value(QStringLiteral("brushPreset")).toString());
      geometry.insert(QStringLiteral("brushSize"), mask_definition.value(QStringLiteral("brushSize")).toDouble());
      geometry.insert(QStringLiteral("brushFlow"), mask_definition.value(QStringLiteral("brushFlow")).toDouble());
      geometry.insert(QStringLiteral("brushHardness"), mask_definition.value(QStringLiteral("brushHardness")).toDouble());
      geometry.insert(QStringLiteral("brushSpacing"), mask_definition.value(QStringLiteral("brushSpacing")).toDouble());
      geometry.insert(QStringLiteral("brushSmoothing"), mask_definition.value(QStringLiteral("brushSmoothing")).toDouble());
      geometry.insert(QStringLiteral("brushPressureSize"), mask_definition.value(QStringLiteral("brushPressureSize")).toDouble());
      geometry.insert(QStringLiteral("brushPressureFlow"), mask_definition.value(QStringLiteral("brushPressureFlow")).toDouble());
      geometry.insert(
          QStringLiteral("brushPressureOpacity"),
          mask_definition.value(QStringLiteral("brushPressureOpacity")).toDouble());
      geometry.insert(QStringLiteral("brushBuildup"), mask_definition.value(QStringLiteral("brushBuildup")).toBool());
      geometry.insert(QStringLiteral("brushEraseMode"), mask_definition.value(QStringLiteral("brushEraseMode")).toBool());
      geometry.insert(QStringLiteral("brushAutoMask"), mask_definition.value(QStringLiteral("brushAutoMask")).toBool());
      geometry.insert(QStringLiteral("brushAutoMaskTolerance"), mask_definition.value(QStringLiteral("brushAutoMaskTolerance")).toDouble());
      geometry.insert(
          QStringLiteral("brushAutoMaskContinuousSampling"),
          mask_definition.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool());
      geometry.insert(
          QStringLiteral("brushProtectEdges"),
          mask_definition.value(QStringLiteral("brushProtectEdges")).toBool());
      geometry.insert(
          QStringLiteral("healTextureBlend"),
          qBound(0.0, mask_definition.value(QStringLiteral("healTextureBlend"), 0.68).toDouble(), 1.0));
      geometry.insert(
          QStringLiteral("healColorBlend"),
          qBound(0.0, mask_definition.value(QStringLiteral("healColorBlend"), 0.46).toDouble(), 1.0));
      geometry.insert(QStringLiteral("brushRefineMode"), mask_definition.value(QStringLiteral("brushRefineMode")).toBool());
      geometry.insert(QStringLiteral("refineBrushRecalculate"), mask_definition.value(QStringLiteral("refineBrushRecalculate")).toBool());
      geometry.insert(QStringLiteral("brushRefineTargetId"), mask_definition.value(QStringLiteral("brushRefineTargetId")).toString());
      geometry.insert(QStringLiteral("brushRefineTargetName"), mask_definition.value(QStringLiteral("brushRefineTargetName")).toString());
      geometry.insert(QStringLiteral("brushRefineTargetKind"), mask_definition.value(QStringLiteral("brushRefineTargetKind")).toString());
      geometry.insert(QStringLiteral("cloneSourceSet"), mask_definition.value(QStringLiteral("cloneSourceSet")).toBool());
      geometry.insert(QStringLiteral("cloneSourceX"), mask_definition.value(QStringLiteral("cloneSourceX")).toDouble());
      geometry.insert(QStringLiteral("cloneSourceY"), mask_definition.value(QStringLiteral("cloneSourceY")).toDouble());
      geometry.insert(QStringLiteral("cloneOffsetX"), mask_definition.value(QStringLiteral("cloneOffsetX")).toDouble());
      geometry.insert(QStringLiteral("cloneOffsetY"), mask_definition.value(QStringLiteral("cloneOffsetY")).toDouble());
      geometry.insert(QStringLiteral("cloneScale"), mask_definition.value(QStringLiteral("cloneScale")).toDouble());
      geometry.insert(QStringLiteral("cloneRotation"), mask_definition.value(QStringLiteral("cloneRotation")).toDouble());
      geometry.insert(QStringLiteral("cloneAligned"), mask_definition.value(QStringLiteral("cloneAligned"), true).toBool());
      QJsonArray clone_sources;
      for (const auto& source_value : mask_definition.value(QStringLiteral("cloneSources")).toList()) {
        const auto source_map = source_value.toMap();
        QJsonObject clone_source;
        clone_source.insert(QStringLiteral("name"), source_map.value(QStringLiteral("name")).toString());
        clone_source.insert(QStringLiteral("x"), source_map.value(QStringLiteral("x"), 0.5).toDouble());
        clone_source.insert(QStringLiteral("y"), source_map.value(QStringLiteral("y"), 0.5).toDouble());
        clone_source.insert(QStringLiteral("scale"), source_map.value(QStringLiteral("scale"), 1.0).toDouble());
        clone_source.insert(QStringLiteral("rotation"), source_map.value(QStringLiteral("rotation")).toDouble());
        clone_source.insert(QStringLiteral("aligned"), source_map.value(QStringLiteral("aligned"), true).toBool());
        clone_sources.append(clone_source);
      }
      geometry.insert(QStringLiteral("cloneSources"), clone_sources);
      geometry.insert(
          QStringLiteral("cloneSourceIndex"),
          mask_definition.value(QStringLiteral("cloneSourceIndex"), -1).toInt());
      const auto patch_source_width =
          qBound(0.02, mask_definition.value(QStringLiteral("patchSourceWidth"), 0.18).toDouble(), 1.0);
      const auto patch_source_height =
          qBound(0.02, mask_definition.value(QStringLiteral("patchSourceHeight"), 0.18).toDouble(), 1.0);
      geometry.insert(
          QStringLiteral("patchSourceSet"),
          mask_definition.value(QStringLiteral("patchSourceSet")).toBool());
      geometry.insert(
          QStringLiteral("patchSourceX"),
          qBound(0.0, mask_definition.value(QStringLiteral("patchSourceX"), 0.5).toDouble(), 1.0 - patch_source_width));
      geometry.insert(
          QStringLiteral("patchSourceY"),
          qBound(0.0, mask_definition.value(QStringLiteral("patchSourceY"), 0.5).toDouble(), 1.0 - patch_source_height));
      geometry.insert(QStringLiteral("patchSourceWidth"), patch_source_width);
      geometry.insert(QStringLiteral("patchSourceHeight"), patch_source_height);
      QJsonArray brush_points;
      for (const auto& point_value : mask_definition.value(QStringLiteral("brushPoints")).toList()) {
        const auto point = point_value.toMap();
        QJsonObject brush_point;
        brush_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        brush_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        brush_point.insert(QStringLiteral("size"), point.value(QStringLiteral("size")).toDouble());
        brush_point.insert(QStringLiteral("flow"), point.value(QStringLiteral("flow")).toDouble());
        brush_point.insert(
            QStringLiteral("opacity"),
            point.contains(QStringLiteral("opacity")) ? point.value(QStringLiteral("opacity")).toDouble() : 1.0);
        brush_point.insert(
            QStringLiteral("autoMaskAnchorX"),
            point.contains(QStringLiteral("autoMaskAnchorX"))
                ? point.value(QStringLiteral("autoMaskAnchorX")).toDouble()
                : point.value(QStringLiteral("x")).toDouble());
        brush_point.insert(
            QStringLiteral("autoMaskAnchorY"),
            point.contains(QStringLiteral("autoMaskAnchorY"))
                ? point.value(QStringLiteral("autoMaskAnchorY")).toDouble()
                : point.value(QStringLiteral("y")).toDouble());
        brush_point.insert(
            QStringLiteral("pressure"),
            point.contains(QStringLiteral("pressure")) ? point.value(QStringLiteral("pressure")).toDouble() : 1.0);
        brush_point.insert(
            QStringLiteral("tiltX"),
            point.contains(QStringLiteral("tiltX")) ? point.value(QStringLiteral("tiltX")).toDouble() : 0.0);
        brush_point.insert(
            QStringLiteral("tiltY"),
            point.contains(QStringLiteral("tiltY")) ? point.value(QStringLiteral("tiltY")).toDouble() : 0.0);
        brush_point.insert(
            QStringLiteral("erase"),
            point.contains(QStringLiteral("erase")) ? point.value(QStringLiteral("erase")).toBool() : false);
        brush_points.append(brush_point);
      }
      geometry.insert(QStringLiteral("brushPoints"), brush_points);
      QJsonArray brush_strokes;
      for (const auto& stroke_value : mask_definition.value(QStringLiteral("brushStrokes")).toList()) {
        const auto stroke_map = stroke_value.toMap();
        QJsonObject stroke;
        stroke.insert(QStringLiteral("id"), stroke_map.value(QStringLiteral("id")).toString());
        stroke.insert(
            QStringLiteral("createdAtOrder"),
            stroke_map.value(QStringLiteral("createdAtOrder")).toInt());
        stroke.insert(QStringLiteral("operation"), stroke_map.value(QStringLiteral("operation")).toString());
        stroke.insert(
            QStringLiteral("sampleCount"),
            stroke_map.contains(QStringLiteral("sampleCount"))
                ? stroke_map.value(QStringLiteral("sampleCount")).toInt()
                : stroke_map.value(QStringLiteral("samples")).toList().size());
        stroke.insert(
            QStringLiteral("settings"),
            QJsonObject::fromVariantMap(stroke_map.value(QStringLiteral("settings")).toMap()));
        QJsonArray samples;
        for (const auto& sample_value : stroke_map.value(QStringLiteral("samples")).toList()) {
          const auto sample_map = sample_value.toMap();
          QJsonObject sample;
          sample.insert(QStringLiteral("x"), sample_map.value(QStringLiteral("x")).toDouble());
          sample.insert(QStringLiteral("y"), sample_map.value(QStringLiteral("y")).toDouble());
          sample.insert(QStringLiteral("size"), sample_map.value(QStringLiteral("size")).toDouble());
          sample.insert(QStringLiteral("flow"), sample_map.value(QStringLiteral("flow")).toDouble());
          sample.insert(
              QStringLiteral("opacity"),
              sample_map.contains(QStringLiteral("opacity")) ? sample_map.value(QStringLiteral("opacity")).toDouble() : 1.0);
          sample.insert(
              QStringLiteral("autoMaskAnchorX"),
              sample_map.contains(QStringLiteral("autoMaskAnchorX"))
                  ? sample_map.value(QStringLiteral("autoMaskAnchorX")).toDouble()
                  : sample_map.value(QStringLiteral("x")).toDouble());
          sample.insert(
              QStringLiteral("autoMaskAnchorY"),
              sample_map.contains(QStringLiteral("autoMaskAnchorY"))
                  ? sample_map.value(QStringLiteral("autoMaskAnchorY")).toDouble()
                  : sample_map.value(QStringLiteral("y")).toDouble());
          sample.insert(
              QStringLiteral("pressure"),
              sample_map.contains(QStringLiteral("pressure"))
                  ? sample_map.value(QStringLiteral("pressure")).toDouble()
                  : 1.0);
          sample.insert(QStringLiteral("tiltX"), sample_map.value(QStringLiteral("tiltX")).toDouble());
          sample.insert(QStringLiteral("tiltY"), sample_map.value(QStringLiteral("tiltY")).toDouble());
          sample.insert(
              QStringLiteral("erase"),
              sample_map.contains(QStringLiteral("erase"))
                  ? sample_map.value(QStringLiteral("erase")).toBool()
                  : false);
          samples.append(sample);
        }
        stroke.insert(QStringLiteral("samples"), samples);
        brush_strokes.append(stroke);
      }
      geometry.insert(QStringLiteral("brushStrokes"), brush_strokes);
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        geometry.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, map_int_value_or(mask_definition, QStringLiteral("rasterSchemaVersion"), 1)));
      }
    } else if (kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
      QJsonArray polygon_points;
      for (const auto& point_value : mask_definition.value(QStringLiteral("polygonPoints")).toList()) {
        const auto point = point_value.toMap();
        QJsonObject polygon_point;
        polygon_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        polygon_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        polygon_points.append(polygon_point);
      }
      geometry.insert(QStringLiteral("polygonPoints"), polygon_points);
      if (kind == QStringLiteral("lasso")) {
        geometry.insert(QStringLiteral("lassoClosed"), mask_definition.value(QStringLiteral("lassoClosed")).toBool());
        geometry.insert(
            QStringLiteral("lassoSimplified"),
            mask_definition.value(QStringLiteral("lassoSimplified")).toBool());
        geometry.insert(QStringLiteral("lassoSmoothed"), mask_definition.value(QStringLiteral("lassoSmoothed")).toBool());
        geometry.insert(
            QStringLiteral("lassoFreehandPath"),
            mask_definition.contains(QStringLiteral("lassoFreehandPath"))
                ? mask_definition.value(QStringLiteral("lassoFreehandPath")).toBool()
                : true);
        geometry.insert(
            QStringLiteral("lassoRawPointCount"),
            mask_definition.value(QStringLiteral("lassoRawPointCount")).toInt());
        geometry.insert(
            QStringLiteral("lassoSimplifiedPointCount"),
            mask_definition.value(QStringLiteral("lassoSimplifiedPointCount")).toInt());
        QJsonArray lasso_raw_points;
        for (const auto& point_value : mask_definition.value(QStringLiteral("lassoRawPoints")).toList()) {
          const auto point = point_value.toMap();
          QJsonObject lasso_point;
          lasso_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
          lasso_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
          lasso_raw_points.append(lasso_point);
        }
        geometry.insert(QStringLiteral("lassoRawPoints"), lasso_raw_points);
      }
    } else if (kind == QStringLiteral("depth")) {
      constraints.insert(QStringLiteral("depthMin"), mask_definition.value(QStringLiteral("depthMin")).toDouble());
      constraints.insert(QStringLiteral("depthMax"), mask_definition.value(QStringLiteral("depthMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
      constraints.insert(
          QStringLiteral("depthSamples"),
          QJsonArray::fromVariantList(
              normalized_depth_range_samples(
                  mask_definition.value(QStringLiteral("depthSamples")).toList())));
      const auto depth_data = mask_definition.value(QStringLiteral("depthData")).toByteArray();
      const auto depth_width = mask_definition.value(QStringLiteral("depthWidth")).toInt();
      const auto depth_height = mask_definition.value(QStringLiteral("depthHeight")).toInt();
      if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
        geometry.insert(
            QStringLiteral("depthData"),
            QString::fromLatin1(depth_data.toBase64()));
        geometry.insert(QStringLiteral("depthWidth"), depth_width);
        geometry.insert(QStringLiteral("depthHeight"), depth_height);
      }
    } else if (kind == QStringLiteral("luminance")) {
      constraints.insert(QStringLiteral("luminanceMin"), mask_definition.value(QStringLiteral("luminanceMin")).toDouble());
      constraints.insert(QStringLiteral("luminanceMax"), mask_definition.value(QStringLiteral("luminanceMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
      constraints.insert(
          QStringLiteral("luminanceSamples"),
          QJsonArray::fromVariantList(
              normalized_luminance_range_samples(
                  mask_definition.value(QStringLiteral("luminanceSamples")).toList())));
    } else if (kind == QStringLiteral("color")) {
      constraints.insert(QStringLiteral("hueCenter"), mask_definition.value(QStringLiteral("hueCenter")).toDouble());
      constraints.insert(QStringLiteral("hueRange"), mask_definition.value(QStringLiteral("hueRange")).toDouble());
      constraints.insert(QStringLiteral("saturationMin"), mask_definition.value(QStringLiteral("saturationMin")).toDouble());
      constraints.insert(QStringLiteral("saturationMax"), mask_definition.value(QStringLiteral("saturationMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
      constraints.insert(
          QStringLiteral("colorSamples"),
          QJsonArray::fromVariantList(mask_definition.value(QStringLiteral("colorSamples")).toList()));
    } else if (is_raster_mask_kind(kind)) {
      geometry.insert(QStringLiteral("retouchMode"), mask_definition.value(QStringLiteral("retouchMode")).toString());
      if (kind == QStringLiteral("alpha")) {
        geometry.insert(
            QStringLiteral("alphaSourcePath"),
            mask_definition.value(QStringLiteral("alphaSourcePath")).toString());
        geometry.insert(
            QStringLiteral("alphaImportInvert"),
            mask_definition.contains(QStringLiteral("alphaImportInvert"))
                ? mask_definition.value(QStringLiteral("alphaImportInvert")).toBool()
                : false);
        geometry.insert(
            QStringLiteral("alphaImportThreshold"),
            mask_definition.value(QStringLiteral("alphaImportThreshold")).toDouble());
        geometry.insert(
            QStringLiteral("alphaImportResampleMode"),
            mask_definition.value(QStringLiteral("alphaImportResampleMode")).toString());
      }
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (!segmentation_data.isEmpty() && segmentation_width > 0 && segmentation_height > 0) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        geometry.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, map_int_value_or(mask_definition, QStringLiteral("rasterSchemaVersion"), 1)));
      }
    }
    if (is_ai_segmentation_mask_kind(kind)) {
      write_generated_mask_metadata(&geometry, mask_definition);
    }
    write_inpaint_patch_payload(&geometry, mask_definition);

    QJsonObject mask;
    mask.insert(QStringLiteral("id"), mask_definition.value(QStringLiteral("id")).toString());
    mask.insert(
        QStringLiteral("schemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("schemaVersion"), 1)));
    mask.insert(
        QStringLiteral("sourceSchemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("sourceSchemaVersion"), 1)));
    mask.insert(QStringLiteral("parentId"), mask_definition.value(QStringLiteral("parentId")).toString());
    mask.insert(QStringLiteral("outputId"), mask_definition.value(QStringLiteral("outputId")).toString());
    mask.insert(
        QStringLiteral("reusableOutput"),
        mask_definition.value(QStringLiteral("reusableOutput")).toBool());
    mask.insert(QStringLiteral("name"), mask_definition.value(QStringLiteral("name")).toString());
    mask.insert(QStringLiteral("kind"), kind);
    mask.insert(
        QStringLiteral("toolRole"),
        normalized_mask_tool_role(mask_definition.value(QStringLiteral("toolRole")).toString()));
    mask.insert(
        QStringLiteral("operation"),
        normalized_mask_operation(mask_definition.value(QStringLiteral("operation")).toString()));
    mask.insert(
        QStringLiteral("enabled"),
        mask_definition.contains(QStringLiteral("enabled"))
            ? mask_definition.value(QStringLiteral("enabled")).toBool()
            : true);
    mask.insert(
        QStringLiteral("solo"),
        mask_definition.contains(QStringLiteral("solo"))
            ? mask_definition.value(QStringLiteral("solo")).toBool()
            : false);
    mask.insert(
        QStringLiteral("locked"),
        mask_definition.contains(QStringLiteral("locked"))
            ? mask_definition.value(QStringLiteral("locked")).toBool()
            : false);
    mask.insert(
        QStringLiteral("committed"),
        mask_definition.contains(QStringLiteral("committed"))
            ? mask_definition.value(QStringLiteral("committed")).toBool()
            : true);
    mask.insert(
        QStringLiteral("retouchOrder"),
        mask_definition.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")
            ? std::max(1, map_int_value_or(mask_definition, QStringLiteral("retouchOrder"), ordinal))
            : 0);
    mask.insert(
        QStringLiteral("opacity"),
        qBound(0.0,
               mask_definition.contains(QStringLiteral("opacity"))
                   ? mask_definition.value(QStringLiteral("opacity")).toDouble()
                   : 1.0,
               1.0));
    mask.insert(
        QStringLiteral("density"),
        qBound(0.0,
               mask_definition.contains(QStringLiteral("density"))
                   ? mask_definition.value(QStringLiteral("density")).toDouble()
                   : 1.0,
               1.0));
    mask.insert(
        QStringLiteral("invert"),
        mask_definition.value(QStringLiteral("invert")).toBool());
    mask.insert(
        QStringLiteral("blendMode"),
        normalized_mask_blend_mode(mask_definition.value(QStringLiteral("blendMode")).toString()));
    mask.insert(QStringLiteral("adjustments"), adjustments);
    mask.insert(QStringLiteral("geometry"), geometry);
    if (!constraints.isEmpty()) {
      mask.insert(QStringLiteral("constraints"), constraints);
    }
    return mask;
  };

  QVariantList serialized_masks = normalize_mask_stack(state.mask_stack);
  if (!state.mask_stack.isEmpty()) {
  } else {
    QVariantList synthesized_masks;
    if (has_structured_linear_mask(state)) {
      QVariantMap linear_mask = default_mask_definition(QStringLiteral("linear"), 1);
      linear_mask.insert(QStringLiteral("enabled"), state.linear_mask_enabled);
      linear_mask.insert(QStringLiteral("exposure"), state.linear_mask_exposure);
      linear_mask.insert(QStringLiteral("temperature"), state.linear_mask_temperature);
      linear_mask.insert(QStringLiteral("feather"), state.linear_mask_feather);
      linear_mask.insert(QStringLiteral("position"), state.linear_mask_position);
      linear_mask.insert(QStringLiteral("rotation"), state.linear_mask_rotation);
      synthesized_masks.append(linear_mask);
    }

    if (has_structured_radial_mask(state)) {
      QVariantMap radial_mask = default_mask_definition(
          QStringLiteral("radial"), static_cast<int>(synthesized_masks.size()) + 1);
      radial_mask.insert(QStringLiteral("enabled"), state.radial_mask_enabled);
      radial_mask.insert(QStringLiteral("exposure"), state.radial_mask_exposure);
      radial_mask.insert(QStringLiteral("temperature"), state.radial_mask_temperature);
      radial_mask.insert(QStringLiteral("feather"), state.radial_mask_feather);
      radial_mask.insert(QStringLiteral("centerX"), state.radial_mask_center_x);
      radial_mask.insert(QStringLiteral("centerY"), state.radial_mask_center_y);
      radial_mask.insert(QStringLiteral("radiusX"), state.radial_mask_radius_x);
      radial_mask.insert(QStringLiteral("radiusY"), state.radial_mask_radius_y);
      radial_mask.insert(QStringLiteral("rotation"), state.radial_mask_rotation);
      synthesized_masks.append(radial_mask);
    }
    serialized_masks = synthesized_masks;
  }

  QJsonArray mask_sets;
  QHash<QString, int> mask_set_indices;
  auto ordinal = 1;
  for (const auto& mask_value : serialized_masks) {
    const auto mask_definition = normalized_mask_definition(mask_value.toMap(), ordinal);
    mask_stack.append(serialize_mask_object(mask_definition, ordinal));
    if (mask_definition.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")) {
      retouch_stack.append(serialize_mask_object(mask_definition, ordinal));
      ++ordinal;
      continue;
    }

    const auto set_id = mask_definition.value(QStringLiteral("maskSetId")).toString();
    const auto set_name = mask_definition.value(QStringLiteral("maskSetName")).toString();
    auto set_index = mask_set_indices.value(set_id, -1);
    if (set_index < 0) {
      QJsonObject set_adjustments;
      set_adjustments.insert(
          QStringLiteral("exposure"),
          mask_definition.value(QStringLiteral("exposure")).toDouble());
      set_adjustments.insert(
          QStringLiteral("contrast"),
          mask_definition.value(QStringLiteral("contrast")).toDouble());
      set_adjustments.insert(
          QStringLiteral("highlights"),
          mask_definition.value(QStringLiteral("highlights")).toDouble());
      set_adjustments.insert(
          QStringLiteral("shadows"),
          mask_definition.value(QStringLiteral("shadows")).toDouble());
      set_adjustments.insert(
          QStringLiteral("whites"),
          mask_definition.value(QStringLiteral("whites")).toDouble());
      set_adjustments.insert(
          QStringLiteral("blacks"),
          mask_definition.value(QStringLiteral("blacks")).toDouble());
      set_adjustments.insert(
          QStringLiteral("temperature"),
          mask_definition.value(QStringLiteral("temperature")).toDouble());
      set_adjustments.insert(
          QStringLiteral("tint"),
          mask_definition.value(QStringLiteral("tint")).toDouble());
      set_adjustments.insert(
          QStringLiteral("vibrance"),
          mask_definition.value(QStringLiteral("vibrance")).toDouble());
      set_adjustments.insert(
          QStringLiteral("saturation"),
          mask_definition.value(QStringLiteral("saturation")).toDouble());
      set_adjustments.insert(
          QStringLiteral("hueShift"),
          mask_definition.value(QStringLiteral("hueShift")).toDouble());
      set_adjustments.insert(
          QStringLiteral("saturationShift"),
          mask_definition.value(QStringLiteral("saturationShift")).toDouble());
      set_adjustments.insert(
          QStringLiteral("colorizeHue"),
          mask_definition.value(QStringLiteral("colorizeHue")).toDouble());
      set_adjustments.insert(
          QStringLiteral("colorizeStrength"),
          mask_definition.value(QStringLiteral("colorizeStrength")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionEdgeContrast"),
          mask_definition.value(QStringLiteral("selectionEdgeContrast")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionSmooth"),
          mask_definition.value(QStringLiteral("selectionSmooth")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionFineEdge"),
          mask_definition.value(QStringLiteral("selectionFineEdge")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionDecontaminate"),
          mask_definition.value(QStringLiteral("selectionDecontaminate")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionGrow"), mask_definition.value(QStringLiteral("selectionGrow")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionShrink"), mask_definition.value(QStringLiteral("selectionShrink")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionFeather"), mask_definition.value(QStringLiteral("selectionFeather")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionBlur"), mask_definition.value(QStringLiteral("selectionBlur")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionHarden"), mask_definition.value(QStringLiteral("selectionHarden")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionDespeckle"), mask_definition.value(QStringLiteral("selectionDespeckle")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionFillHoles"), mask_definition.value(QStringLiteral("selectionFillHoles")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionRemoveIslands"),
          mask_definition.value(QStringLiteral("selectionRemoveIslands")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionInvert"),
          mask_definition.value(QStringLiteral("maskSetSelectionInvert")).toBool());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionExpand"),
          mask_definition.value(QStringLiteral("maskSetSelectionExpand")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionSmooth"),
          mask_definition.value(QStringLiteral("maskSetSelectionSmooth")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionFineEdge"),
          mask_definition.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionEdgeContrast"),
          mask_definition.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetDensity"),
          mask_definition.value(QStringLiteral("maskSetDensity"), 1.0).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetOpacity"),
          mask_definition.value(QStringLiteral("maskSetOpacity"), 1.0).toDouble());

      QJsonObject set_object;
      set_object.insert(QStringLiteral("schemaVersion"), 2);
      set_object.insert(QStringLiteral("adjustmentsSchemaVersion"), 1);
      set_object.insert(QStringLiteral("componentSchemaVersion"), 1);
      set_object.insert(QStringLiteral("id"), set_id);
      set_object.insert(QStringLiteral("name"), set_name);
      set_object.insert(QStringLiteral("adjustments"), set_adjustments);
      set_object.insert(QStringLiteral("sources"), QJsonArray{});
      set_index = static_cast<int>(mask_sets.size());
      mask_sets.append(set_object);
      mask_set_indices.insert(set_id, set_index);
    }

    auto set_object = mask_sets.at(set_index).toObject();
    auto sources = set_object.value(QStringLiteral("sources")).toArray();

    QJsonObject geometry;
    geometry.insert(
        QStringLiteral("feather"),
        mask_definition.value(QStringLiteral("feather")).toDouble());
    geometry.insert(
        QStringLiteral("rotation"),
        mask_definition.value(QStringLiteral("rotation")).toDouble());
    QJsonObject constraints;
    constraints.insert(QStringLiteral("refineLuminanceMin"), mask_definition.value(QStringLiteral("refineLuminanceMin")).toDouble());
    constraints.insert(QStringLiteral("refineLuminanceMax"), mask_definition.value(QStringLiteral("refineLuminanceMax")).toDouble());
    constraints.insert(QStringLiteral("refineHueCenter"), mask_definition.value(QStringLiteral("refineHueCenter")).toDouble());
    constraints.insert(QStringLiteral("refineHueRange"), mask_definition.value(QStringLiteral("refineHueRange")).toDouble());
    constraints.insert(QStringLiteral("refineSaturationMin"), mask_definition.value(QStringLiteral("refineSaturationMin")).toDouble());
    constraints.insert(QStringLiteral("refineSaturationMax"), mask_definition.value(QStringLiteral("refineSaturationMax")).toDouble());
    constraints.insert(QStringLiteral("refineDepthMin"), mask_definition.value(QStringLiteral("refineDepthMin")).toDouble());
    constraints.insert(QStringLiteral("refineDepthMax"), mask_definition.value(QStringLiteral("refineDepthMax")).toDouble());
    constraints.insert(QStringLiteral("refineEdgeConfidenceMin"), mask_definition.value(QStringLiteral("refineEdgeConfidenceMin")).toDouble());
    constraints.insert(QStringLiteral("refineRangeFeather"), mask_definition.value(QStringLiteral("refineRangeFeather")).toDouble());
    constraints.insert(
        QStringLiteral("generatedMaskHoleFillThreshold"),
        mask_definition.contains(QStringLiteral("generatedMaskHoleFillThreshold"))
            ? mask_definition.value(QStringLiteral("generatedMaskHoleFillThreshold")).toDouble()
            : 0.004);
    constraints.insert(
        QStringLiteral("generatedMaskIslandRemovalThreshold"),
        mask_definition.contains(QStringLiteral("generatedMaskIslandRemovalThreshold"))
            ? mask_definition.value(QStringLiteral("generatedMaskIslandRemovalThreshold")).toDouble()
            : 0.0015);
    const auto kind = normalized_mask_kind(mask_definition.value(QStringLiteral("kind")).toString());
    if (is_center_radius_mask_kind(kind)) {
      geometry.insert(QStringLiteral("centerX"), mask_definition.value(QStringLiteral("centerX")).toDouble());
      geometry.insert(QStringLiteral("centerY"), mask_definition.value(QStringLiteral("centerY")).toDouble());
      geometry.insert(QStringLiteral("radiusX"), mask_definition.value(QStringLiteral("radiusX")).toDouble());
      geometry.insert(QStringLiteral("radiusY"), mask_definition.value(QStringLiteral("radiusY")).toDouble());
    } else if (kind == QStringLiteral("linear")) {
      geometry.insert(QStringLiteral("position"), mask_definition.value(QStringLiteral("position")).toDouble());
    } else if (kind == QStringLiteral("brush")) {
      geometry.insert(QStringLiteral("brushPreset"), mask_definition.value(QStringLiteral("brushPreset")).toString());
      geometry.insert(QStringLiteral("brushSize"), mask_definition.value(QStringLiteral("brushSize")).toDouble());
      geometry.insert(QStringLiteral("brushFlow"), mask_definition.value(QStringLiteral("brushFlow")).toDouble());
      geometry.insert(QStringLiteral("brushHardness"), mask_definition.value(QStringLiteral("brushHardness")).toDouble());
      geometry.insert(QStringLiteral("brushSpacing"), mask_definition.value(QStringLiteral("brushSpacing")).toDouble());
      geometry.insert(QStringLiteral("brushSmoothing"), mask_definition.value(QStringLiteral("brushSmoothing")).toDouble());
      geometry.insert(QStringLiteral("brushPressureSize"), mask_definition.value(QStringLiteral("brushPressureSize")).toDouble());
      geometry.insert(QStringLiteral("brushPressureFlow"), mask_definition.value(QStringLiteral("brushPressureFlow")).toDouble());
      geometry.insert(
          QStringLiteral("brushPressureOpacity"),
          mask_definition.value(QStringLiteral("brushPressureOpacity")).toDouble());
      geometry.insert(QStringLiteral("brushBuildup"), mask_definition.value(QStringLiteral("brushBuildup")).toBool());
      geometry.insert(QStringLiteral("brushEraseMode"), mask_definition.value(QStringLiteral("brushEraseMode")).toBool());
      geometry.insert(QStringLiteral("brushAutoMask"), mask_definition.value(QStringLiteral("brushAutoMask")).toBool());
      geometry.insert(QStringLiteral("brushAutoMaskTolerance"), mask_definition.value(QStringLiteral("brushAutoMaskTolerance")).toDouble());
      geometry.insert(
          QStringLiteral("brushAutoMaskContinuousSampling"),
          mask_definition.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool());
      geometry.insert(
          QStringLiteral("brushProtectEdges"),
          mask_definition.value(QStringLiteral("brushProtectEdges")).toBool());
      geometry.insert(QStringLiteral("brushRefineMode"), mask_definition.value(QStringLiteral("brushRefineMode")).toBool());
      geometry.insert(QStringLiteral("refineBrushRecalculate"), mask_definition.value(QStringLiteral("refineBrushRecalculate")).toBool());
      geometry.insert(QStringLiteral("brushRefineTargetId"), mask_definition.value(QStringLiteral("brushRefineTargetId")).toString());
      geometry.insert(QStringLiteral("brushRefineTargetName"), mask_definition.value(QStringLiteral("brushRefineTargetName")).toString());
      geometry.insert(QStringLiteral("brushRefineTargetKind"), mask_definition.value(QStringLiteral("brushRefineTargetKind")).toString());
      QJsonArray brush_points;
      for (const auto& point_value : mask_definition.value(QStringLiteral("brushPoints")).toList()) {
        const auto point = point_value.toMap();
        QJsonObject brush_point;
        brush_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        brush_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        brush_point.insert(QStringLiteral("size"), point.value(QStringLiteral("size")).toDouble());
        brush_point.insert(QStringLiteral("flow"), point.value(QStringLiteral("flow")).toDouble());
        brush_point.insert(
            QStringLiteral("opacity"),
            point.contains(QStringLiteral("opacity")) ? point.value(QStringLiteral("opacity")).toDouble() : 1.0);
        brush_point.insert(
            QStringLiteral("autoMaskAnchorX"),
            point.contains(QStringLiteral("autoMaskAnchorX"))
                ? point.value(QStringLiteral("autoMaskAnchorX")).toDouble()
                : point.value(QStringLiteral("x")).toDouble());
        brush_point.insert(
            QStringLiteral("autoMaskAnchorY"),
            point.contains(QStringLiteral("autoMaskAnchorY"))
                ? point.value(QStringLiteral("autoMaskAnchorY")).toDouble()
                : point.value(QStringLiteral("y")).toDouble());
        brush_point.insert(
            QStringLiteral("pressure"),
            point.contains(QStringLiteral("pressure")) ? point.value(QStringLiteral("pressure")).toDouble() : 1.0);
        brush_point.insert(
            QStringLiteral("tiltX"),
            point.contains(QStringLiteral("tiltX")) ? point.value(QStringLiteral("tiltX")).toDouble() : 0.0);
        brush_point.insert(
            QStringLiteral("tiltY"),
            point.contains(QStringLiteral("tiltY")) ? point.value(QStringLiteral("tiltY")).toDouble() : 0.0);
        brush_point.insert(
            QStringLiteral("erase"),
            point.contains(QStringLiteral("erase")) ? point.value(QStringLiteral("erase")).toBool() : false);
        brush_points.append(brush_point);
      }
      geometry.insert(QStringLiteral("brushPoints"), brush_points);
      QJsonArray brush_strokes;
      for (const auto& stroke_value : mask_definition.value(QStringLiteral("brushStrokes")).toList()) {
        const auto stroke_map = stroke_value.toMap();
        QJsonObject stroke;
        stroke.insert(QStringLiteral("id"), stroke_map.value(QStringLiteral("id")).toString());
        stroke.insert(
            QStringLiteral("createdAtOrder"),
            stroke_map.value(QStringLiteral("createdAtOrder")).toInt());
        stroke.insert(QStringLiteral("operation"), stroke_map.value(QStringLiteral("operation")).toString());
        stroke.insert(
            QStringLiteral("sampleCount"),
            stroke_map.contains(QStringLiteral("sampleCount"))
                ? stroke_map.value(QStringLiteral("sampleCount")).toInt()
                : stroke_map.value(QStringLiteral("samples")).toList().size());
        stroke.insert(
            QStringLiteral("settings"),
            QJsonObject::fromVariantMap(stroke_map.value(QStringLiteral("settings")).toMap()));
        QJsonArray samples;
        for (const auto& sample_value : stroke_map.value(QStringLiteral("samples")).toList()) {
          const auto sample_map = sample_value.toMap();
          QJsonObject sample;
          sample.insert(QStringLiteral("x"), sample_map.value(QStringLiteral("x")).toDouble());
          sample.insert(QStringLiteral("y"), sample_map.value(QStringLiteral("y")).toDouble());
          sample.insert(QStringLiteral("size"), sample_map.value(QStringLiteral("size")).toDouble());
          sample.insert(QStringLiteral("flow"), sample_map.value(QStringLiteral("flow")).toDouble());
          sample.insert(
              QStringLiteral("opacity"),
              sample_map.contains(QStringLiteral("opacity")) ? sample_map.value(QStringLiteral("opacity")).toDouble() : 1.0);
          sample.insert(
              QStringLiteral("autoMaskAnchorX"),
              sample_map.contains(QStringLiteral("autoMaskAnchorX"))
                  ? sample_map.value(QStringLiteral("autoMaskAnchorX")).toDouble()
                  : sample_map.value(QStringLiteral("x")).toDouble());
          sample.insert(
              QStringLiteral("autoMaskAnchorY"),
              sample_map.contains(QStringLiteral("autoMaskAnchorY"))
                  ? sample_map.value(QStringLiteral("autoMaskAnchorY")).toDouble()
                  : sample_map.value(QStringLiteral("y")).toDouble());
          sample.insert(
              QStringLiteral("pressure"),
              sample_map.contains(QStringLiteral("pressure"))
                  ? sample_map.value(QStringLiteral("pressure")).toDouble()
                  : 1.0);
          sample.insert(QStringLiteral("tiltX"), sample_map.value(QStringLiteral("tiltX")).toDouble());
          sample.insert(QStringLiteral("tiltY"), sample_map.value(QStringLiteral("tiltY")).toDouble());
          sample.insert(
              QStringLiteral("erase"),
              sample_map.contains(QStringLiteral("erase"))
                  ? sample_map.value(QStringLiteral("erase")).toBool()
                  : false);
          samples.append(sample);
        }
        stroke.insert(QStringLiteral("samples"), samples);
        brush_strokes.append(stroke);
      }
      geometry.insert(QStringLiteral("brushStrokes"), brush_strokes);
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        geometry.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, map_int_value_or(mask_definition, QStringLiteral("rasterSchemaVersion"), 1)));
      }
    } else if (kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
      QJsonArray polygon_points;
      for (const auto& point_value : mask_definition.value(QStringLiteral("polygonPoints")).toList()) {
        const auto point = point_value.toMap();
        QJsonObject polygon_point;
        polygon_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        polygon_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        polygon_points.append(polygon_point);
      }
      geometry.insert(QStringLiteral("polygonPoints"), polygon_points);
      if (kind == QStringLiteral("lasso")) {
        geometry.insert(QStringLiteral("lassoClosed"), mask_definition.value(QStringLiteral("lassoClosed")).toBool());
        geometry.insert(
            QStringLiteral("lassoSimplified"),
            mask_definition.value(QStringLiteral("lassoSimplified")).toBool());
        geometry.insert(QStringLiteral("lassoSmoothed"), mask_definition.value(QStringLiteral("lassoSmoothed")).toBool());
        geometry.insert(
            QStringLiteral("lassoFreehandPath"),
            mask_definition.contains(QStringLiteral("lassoFreehandPath"))
                ? mask_definition.value(QStringLiteral("lassoFreehandPath")).toBool()
                : true);
        geometry.insert(
            QStringLiteral("lassoRawPointCount"),
            mask_definition.value(QStringLiteral("lassoRawPointCount")).toInt());
        geometry.insert(
            QStringLiteral("lassoSimplifiedPointCount"),
            mask_definition.value(QStringLiteral("lassoSimplifiedPointCount")).toInt());
        QJsonArray lasso_raw_points;
        for (const auto& point_value : mask_definition.value(QStringLiteral("lassoRawPoints")).toList()) {
          const auto point = point_value.toMap();
          QJsonObject lasso_point;
          lasso_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
          lasso_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
          lasso_raw_points.append(lasso_point);
        }
        geometry.insert(QStringLiteral("lassoRawPoints"), lasso_raw_points);
      }
    } else if (kind == QStringLiteral("depth")) {
      constraints.insert(QStringLiteral("depthMin"), mask_definition.value(QStringLiteral("depthMin")).toDouble());
      constraints.insert(QStringLiteral("depthMax"), mask_definition.value(QStringLiteral("depthMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
      constraints.insert(
          QStringLiteral("depthSamples"),
          QJsonArray::fromVariantList(
              normalized_depth_range_samples(
                  mask_definition.value(QStringLiteral("depthSamples")).toList())));
      const auto depth_data = mask_definition.value(QStringLiteral("depthData")).toByteArray();
      const auto depth_width = mask_definition.value(QStringLiteral("depthWidth")).toInt();
      const auto depth_height = mask_definition.value(QStringLiteral("depthHeight")).toInt();
      if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
        geometry.insert(
            QStringLiteral("depthData"),
            QString::fromLatin1(depth_data.toBase64()));
        geometry.insert(QStringLiteral("depthWidth"), depth_width);
        geometry.insert(QStringLiteral("depthHeight"), depth_height);
      }
    } else if (kind == QStringLiteral("luminance")) {
      constraints.insert(QStringLiteral("luminanceMin"), mask_definition.value(QStringLiteral("luminanceMin")).toDouble());
      constraints.insert(QStringLiteral("luminanceMax"), mask_definition.value(QStringLiteral("luminanceMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
      constraints.insert(
          QStringLiteral("luminanceSamples"),
          QJsonArray::fromVariantList(
              normalized_luminance_range_samples(
                  mask_definition.value(QStringLiteral("luminanceSamples")).toList())));
    } else if (kind == QStringLiteral("color")) {
      constraints.insert(QStringLiteral("hueCenter"), mask_definition.value(QStringLiteral("hueCenter")).toDouble());
      constraints.insert(QStringLiteral("hueRange"), mask_definition.value(QStringLiteral("hueRange")).toDouble());
      constraints.insert(QStringLiteral("saturationMin"), mask_definition.value(QStringLiteral("saturationMin")).toDouble());
      constraints.insert(QStringLiteral("saturationMax"), mask_definition.value(QStringLiteral("saturationMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
      constraints.insert(
          QStringLiteral("colorSamples"),
          QJsonArray::fromVariantList(mask_definition.value(QStringLiteral("colorSamples")).toList()));
    } else if (is_raster_mask_kind(kind)) {
      if (kind == QStringLiteral("alpha")) {
        geometry.insert(
            QStringLiteral("alphaSourcePath"),
            mask_definition.value(QStringLiteral("alphaSourcePath")).toString());
        geometry.insert(
            QStringLiteral("alphaImportInvert"),
            mask_definition.contains(QStringLiteral("alphaImportInvert"))
                ? mask_definition.value(QStringLiteral("alphaImportInvert")).toBool()
                : false);
        geometry.insert(
            QStringLiteral("alphaImportThreshold"),
            mask_definition.value(QStringLiteral("alphaImportThreshold")).toDouble());
        geometry.insert(
            QStringLiteral("alphaImportResampleMode"),
            mask_definition.value(QStringLiteral("alphaImportResampleMode")).toString());
      }
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        geometry.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, map_int_value_or(mask_definition, QStringLiteral("rasterSchemaVersion"), 1)));
      }
    }
    if (is_ai_segmentation_mask_kind(kind)) {
      write_generated_mask_metadata(&geometry, mask_definition);
    }
    write_inpaint_patch_payload(&geometry, mask_definition);

    QJsonObject selection;
    selection.insert(
        QStringLiteral("invert"),
        mask_definition.value(QStringLiteral("invert")).toBool());
    selection.insert(
        QStringLiteral("expand"),
        mask_definition.value(QStringLiteral("selectionExpand")).toDouble());
    selection.insert(
        QStringLiteral("smooth"),
        mask_definition.value(QStringLiteral("selectionSmooth")).toDouble());
    selection.insert(
        QStringLiteral("fineEdge"),
        mask_definition.value(QStringLiteral("selectionFineEdge")).toDouble());

    QJsonObject source_object;
    source_object.insert(QStringLiteral("componentSchemaVersion"), 1);
    source_object.insert(QStringLiteral("id"), mask_definition.value(QStringLiteral("id")).toString());
    source_object.insert(
        QStringLiteral("schemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("schemaVersion"), 1)));
    source_object.insert(
        QStringLiteral("sourceSchemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("sourceSchemaVersion"), 1)));
    source_object.insert(QStringLiteral("parentId"), mask_definition.value(QStringLiteral("parentId")).toString());
    source_object.insert(QStringLiteral("outputId"), mask_definition.value(QStringLiteral("outputId")).toString());
    source_object.insert(
        QStringLiteral("reusableOutput"),
        mask_definition.value(QStringLiteral("reusableOutput")).toBool());
    source_object.insert(QStringLiteral("name"), mask_definition.value(QStringLiteral("name")).toString());
    source_object.insert(QStringLiteral("kind"), kind);
    source_object.insert(
        QStringLiteral("operation"),
        normalized_mask_operation(mask_definition.value(QStringLiteral("operation")).toString()));
    source_object.insert(
        QStringLiteral("enabled"),
        mask_definition.contains(QStringLiteral("enabled"))
            ? mask_definition.value(QStringLiteral("enabled")).toBool()
            : true);
    source_object.insert(
        QStringLiteral("solo"),
        mask_definition.contains(QStringLiteral("solo"))
            ? mask_definition.value(QStringLiteral("solo")).toBool()
            : false);
    source_object.insert(
        QStringLiteral("locked"),
        mask_definition.contains(QStringLiteral("locked"))
            ? mask_definition.value(QStringLiteral("locked")).toBool()
            : false);
    source_object.insert(
        QStringLiteral("opacity"),
        qBound(0.0,
               mask_definition.contains(QStringLiteral("opacity"))
                   ? mask_definition.value(QStringLiteral("opacity")).toDouble()
                   : 1.0,
               1.0));
    source_object.insert(
        QStringLiteral("density"),
        qBound(0.0,
               mask_definition.contains(QStringLiteral("density"))
                   ? mask_definition.value(QStringLiteral("density")).toDouble()
                   : 1.0,
               1.0));
    source_object.insert(
        QStringLiteral("invert"),
        mask_definition.value(QStringLiteral("invert")).toBool());
    source_object.insert(
        QStringLiteral("blendMode"),
        normalized_mask_blend_mode(mask_definition.value(QStringLiteral("blendMode")).toString()));
    const auto adjustment_overrides = mask_source_adjustment_overrides(
        mask_definition,
        set_object.value(QStringLiteral("adjustments")).toObject().toVariantMap());
    if (!adjustment_overrides.isEmpty()) {
      source_object.insert(
          QStringLiteral("adjustmentOverrides"),
          QJsonObject::fromVariantMap(adjustment_overrides));
    }
    source_object.insert(QStringLiteral("selection"), selection);
    source_object.insert(QStringLiteral("geometry"), geometry);
    if (!constraints.isEmpty()) {
      source_object.insert(QStringLiteral("constraints"), constraints);
    }
    sources.append(source_object);
    set_object.insert(QStringLiteral("sources"), sources);
    mask_sets.replace(set_index, set_object);
    ++ordinal;
  }

  QJsonObject guide_settings;
  guide_settings.insert(QStringLiteral("customGuides"), QJsonArray{});

  QJsonObject history;
  history.insert(
      QStringLiteral("activeBranchId"),
      state.active_history_branch.trimmed().isEmpty()
          ? QStringLiteral("main")
          : state.active_history_branch.trimmed());
  history.insert(QStringLiteral("snapshots"), QJsonArray::fromVariantList(state.recipe_snapshots));

  QJsonObject object;
  object.insert(QStringLiteral("recipeVersion"), kCurrentEditRecipeVersion);
  object.insert(QStringLiteral("rawAdjustments"), raw_adjustments);
  object.insert(QStringLiteral("globalAdjustments"), global_adjustments);
  object.insert(QStringLiteral("colorPipeline"), color_pipeline);
  object.insert(QStringLiteral("curves"), curves);
  object.insert(QStringLiteral("colorMixer"), color_mixer);
  object.insert(QStringLiteral("colorGrading"), color_grading);
  object.insert(QStringLiteral("cropTransform"), crop_transform);
  object.insert(QStringLiteral("outputTransform"), output_transform);
  object.insert(QStringLiteral("optics"), optics);
  object.insert(QStringLiteral("maskStack"), mask_stack);
  object.insert(QStringLiteral("maskSetSchemaVersion"), 2);
  object.insert(QStringLiteral("maskSets"), mask_sets);
  object.insert(QStringLiteral("retouchStack"), retouch_stack);
  object.insert(
      QStringLiteral("presetApplications"),
      QJsonArray::fromVariantList(state.preset_applications));
  const auto serialized_layers = detail::serialize_photo_layers(state.layers);
  object.insert(QStringLiteral("photoLayers"), QJsonArray::fromVariantList(serialized_layers));
  object.insert(QStringLiteral("layers"), QJsonArray::fromVariantList(serialized_layers));
  object.insert(QStringLiteral("history"), history);
  object.insert(QStringLiteral("overlayItems"), QJsonArray{});
  object.insert(QStringLiteral("guideSettings"), guide_settings);
  return object;
}

inline QString serialize_edit_recipe_json(const EditRecipeState& state) {
  return QString::fromUtf8(QJsonDocument(build_edit_recipe_object(state)).toJson(QJsonDocument::Compact));
}

inline QString normalized_history_branch_id(const QString& branch_id) {
  const auto normalized = branch_id.trimmed();
  return normalized.isEmpty() ? QStringLiteral("main") : normalized;
}

inline EditRecipeState append_named_recipe_snapshot(
    const EditRecipeState& state,
    const QString& name,
    const QString& branch_id = QString(),
    const QString& parent_snapshot_id = QString()) {
  EditRecipeState next = state;
  const auto normalized_branch = normalized_history_branch_id(branch_id);
  next.active_history_branch = normalized_branch;

  EditRecipeState snapshot_state = state;
  snapshot_state.active_history_branch = normalized_branch;
  snapshot_state.recipe_snapshots.clear();

  const auto snapshot_number = next.recipe_snapshots.size() + 1;
  const auto snapshot_id =
      QStringLiteral("%1-snapshot-%2").arg(normalized_branch).arg(snapshot_number);
  QJsonObject snapshot;
  snapshot.insert(QStringLiteral("id"), snapshot_id);
  snapshot.insert(
      QStringLiteral("name"),
      name.trimmed().isEmpty() ? QStringLiteral("Snapshot %1").arg(snapshot_number) : name.trimmed());
  snapshot.insert(QStringLiteral("branchId"), normalized_branch);
  snapshot.insert(QStringLiteral("parentSnapshotId"), parent_snapshot_id.trimmed());
  snapshot.insert(QStringLiteral("recipeJson"), serialize_edit_recipe_json(snapshot_state));
  next.recipe_snapshots.append(snapshot.toVariantMap());
  return next;
}

inline EditRecipeValidationResult validate_edit_recipe_json(const QString& recipe_json) {
  EditRecipeValidationResult result;
  const auto trimmed = recipe_json.trimmed();
  if (trimmed.isEmpty()) {
    result.errors.append(QStringLiteral("recipe JSON is empty"));
    return result;
  }

  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(trimmed.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    result.errors.append(
        QStringLiteral("recipe JSON parse error at offset %1: %2")
            .arg(parse_error.offset)
            .arg(parse_error.errorString()));
    return result;
  }
  if (!document.isObject()) {
    result.errors.append(QStringLiteral("recipe root must be a JSON object"));
    return result;
  }

  const auto object = document.object();
  const auto recipe_version_value = object.value(QStringLiteral("recipeVersion"));
  const auto recipe_version_number =
      recipe_version_value.isUndefined() ? 1.0 : recipe_version_value.toDouble(-1.0);
  const auto recipe_version = static_cast<int>(recipe_version_number);
  if (recipe_version_value.isUndefined()) {
    // Older flat recipes did not carry a version and are still valid import payloads.
  } else if (!recipe_version_value.isDouble() || recipe_version_number < 1.0 ||
             std::floor(recipe_version_number) != recipe_version_number) {
    result.errors.append(QStringLiteral("recipeVersion must be a positive integer"));
  } else if (recipe_version > kCurrentEditRecipeVersion) {
    result.errors.append(
        QStringLiteral("recipeVersion %1 is newer than supported version %2")
            .arg(recipe_version)
            .arg(kCurrentEditRecipeVersion));
  }

  if (recipe_version >= kCurrentEditRecipeVersion &&
      recipe_version <= kCurrentEditRecipeVersion) {
    const QStringList required_objects = {
        QStringLiteral("rawAdjustments"),
        QStringLiteral("globalAdjustments"),
        QStringLiteral("colorPipeline"),
        QStringLiteral("curves"),
        QStringLiteral("colorMixer"),
        QStringLiteral("colorGrading"),
        QStringLiteral("cropTransform"),
        QStringLiteral("outputTransform"),
        QStringLiteral("optics"),
        QStringLiteral("history"),
        QStringLiteral("guideSettings"),
    };
    for (const auto& key : required_objects) {
      if (!object.value(key).isObject()) {
        result.errors.append(QStringLiteral("%1 must be an object").arg(key));
      }
    }

    const QStringList required_arrays = {
        QStringLiteral("maskStack"),
        QStringLiteral("maskSets"),
        QStringLiteral("retouchStack"),
        QStringLiteral("presetApplications"),
        QStringLiteral("layers"),
        QStringLiteral("overlayItems"),
    };
    for (const auto& key : required_arrays) {
      if (!object.value(key).isArray()) {
        result.errors.append(QStringLiteral("%1 must be an array").arg(key));
      }
    }
    if (object.contains(QStringLiteral("photoLayers")) &&
        !object.value(QStringLiteral("photoLayers")).isArray()) {
      result.errors.append(QStringLiteral("photoLayers must be an array"));
    }

    const auto curves = object.value(QStringLiteral("curves")).toObject();
    if (!curves.value(QStringLiteral("parametric")).isObject()) {
      result.errors.append(QStringLiteral("curves.parametric must be an object"));
    }
    if (!curves.value(QStringLiteral("channels")).isArray()) {
      result.errors.append(QStringLiteral("curves.channels must be an array"));
    }

    const auto output_transform = object.value(QStringLiteral("outputTransform")).toObject();
    if (!output_transform.value(QStringLiteral("colorSpace")).isString()) {
      result.errors.append(QStringLiteral("outputTransform.colorSpace must be a string"));
    }
    if (!output_transform.value(QStringLiteral("workingColorSpace")).isString()) {
      result.errors.append(QStringLiteral("outputTransform.workingColorSpace must be a string"));
    }
    if (!output_transform.value(QStringLiteral("renderIntent")).isString()) {
      result.errors.append(QStringLiteral("outputTransform.renderIntent must be a string"));
    }

    const auto history = object.value(QStringLiteral("history")).toObject();
    if (!history.value(QStringLiteral("activeBranchId")).isString()) {
      result.errors.append(QStringLiteral("history.activeBranchId must be a string"));
    }
    if (!history.value(QStringLiteral("snapshots")).isArray()) {
      result.errors.append(QStringLiteral("history.snapshots must be an array"));
    }
    const auto snapshots = history.value(QStringLiteral("snapshots")).toArray();
    for (int index = 0; index < snapshots.size(); ++index) {
      if (!snapshots.at(index).isObject()) {
        result.errors.append(QStringLiteral("history.snapshots[%1] must be an object").arg(index));
        continue;
      }
      const auto snapshot = snapshots.at(index).toObject();
      if (!snapshot.value(QStringLiteral("id")).isString()) {
        result.errors.append(QStringLiteral("history.snapshots[%1].id must be a string").arg(index));
      }
      if (!snapshot.value(QStringLiteral("name")).isString()) {
        result.errors.append(QStringLiteral("history.snapshots[%1].name must be a string").arg(index));
      }
      if (!snapshot.value(QStringLiteral("branchId")).isString()) {
        result.errors.append(QStringLiteral("history.snapshots[%1].branchId must be a string").arg(index));
      }
      if (!snapshot.value(QStringLiteral("recipeJson")).isString()) {
        result.errors.append(QStringLiteral("history.snapshots[%1].recipeJson must be a string").arg(index));
      }
    }

    const auto mask_stack = object.value(QStringLiteral("maskStack")).toArray();
    for (int index = 0; index < mask_stack.size(); ++index) {
      if (!mask_stack.at(index).isObject()) {
        result.errors.append(QStringLiteral("maskStack[%1] must be an object").arg(index));
        continue;
      }
      const auto mask = mask_stack.at(index).toObject();
      if (!mask.value(QStringLiteral("kind")).isString()) {
        result.errors.append(QStringLiteral("maskStack[%1].kind must be a string").arg(index));
      }
      if (!mask.value(QStringLiteral("operation")).isString()) {
        result.errors.append(QStringLiteral("maskStack[%1].operation must be a string").arg(index));
      }
      if (!mask.value(QStringLiteral("adjustments")).isObject()) {
        result.errors.append(QStringLiteral("maskStack[%1].adjustments must be an object").arg(index));
      }
      if (!mask.value(QStringLiteral("geometry")).isObject()) {
        result.errors.append(QStringLiteral("maskStack[%1].geometry must be an object").arg(index));
        continue;
      }

      const auto geometry = mask.value(QStringLiteral("geometry")).toObject();
      const auto validate_geometry_payload =
          [&](const QString& data_key,
              const QString& width_key,
              const QString& height_key,
              const QString& label) {
            const auto payload = geometry.value(data_key).toString();
            if (payload.isEmpty()) {
              return;
            }
            if (!valid_segmentation_payload(
                    QByteArray::fromBase64(payload.toLatin1()),
                    geometry.value(width_key).toInt(),
                    geometry.value(height_key).toInt())) {
              result.errors.append(
                  QStringLiteral("maskStack[%1].geometry %2 payload size does not match dimensions or exceeds bounds")
                      .arg(index)
                      .arg(label));
            }
          };
      validate_geometry_payload(
          QStringLiteral("segmentationData"),
          QStringLiteral("segmentationWidth"),
          QStringLiteral("segmentationHeight"),
          QStringLiteral("segmentation"));
      validate_geometry_payload(
          QStringLiteral("confidenceData"),
          QStringLiteral("confidenceWidth"),
          QStringLiteral("confidenceHeight"),
          QStringLiteral("confidence"));
      validate_geometry_payload(
          QStringLiteral("depthData"),
          QStringLiteral("depthWidth"),
          QStringLiteral("depthHeight"),
          QStringLiteral("depth"));
      validate_geometry_payload(
          QStringLiteral("inpaintSourceMaskData"),
          QStringLiteral("inpaintSourceMaskWidth"),
          QStringLiteral("inpaintSourceMaskHeight"),
          QStringLiteral("inpaint source mask"));
    }
  }

  result.valid = result.errors.isEmpty();
  return result;
}

inline EditRecipeMigrationResult migrate_edit_recipe_json(
    const QString& recipe_json,
    int target_version = kCurrentEditRecipeVersion) {
  EditRecipeMigrationResult result;
  result.target_version = target_version;
  if (target_version != kCurrentEditRecipeVersion) {
    result.errors.append(
        QStringLiteral("target recipeVersion %1 is not supported; current version is %2")
            .arg(target_version)
            .arg(kCurrentEditRecipeVersion));
    return result;
  }

  const auto trimmed = recipe_json.trimmed();
  if (trimmed.isEmpty()) {
    result.errors.append(QStringLiteral("recipe JSON is empty"));
    return result;
  }

  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(trimmed.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    result.errors.append(
        QStringLiteral("recipe JSON parse error at offset %1: %2")
            .arg(parse_error.offset)
            .arg(parse_error.errorString()));
    return result;
  }
  if (!document.isObject()) {
    result.errors.append(QStringLiteral("recipe root must be a JSON object"));
    return result;
  }

  const auto object = document.object();
  const auto recipe_version_value = object.value(QStringLiteral("recipeVersion"));
  const auto recipe_version_number =
      recipe_version_value.isUndefined() ? 1.0 : recipe_version_value.toDouble(-1.0);
  const auto source_version = static_cast<int>(recipe_version_number);
  result.source_version = source_version;
  if (!recipe_version_value.isUndefined() &&
      (!recipe_version_value.isDouble() || recipe_version_number < 1.0 ||
       std::floor(recipe_version_number) != recipe_version_number)) {
    result.errors.append(QStringLiteral("recipeVersion must be a positive integer"));
    return result;
  }
  if (source_version > kCurrentEditRecipeVersion) {
    result.errors.append(
        QStringLiteral("recipeVersion %1 is newer than supported version %2")
            .arg(source_version)
            .arg(kCurrentEditRecipeVersion));
    return result;
  }

  if (source_version == target_version) {
    const auto validation = validate_edit_recipe_json(trimmed);
    if (!validation.valid) {
      result.errors = validation.errors;
      return result;
    }
    result.recipe_json = serialize_edit_recipe_json(parse_edit_recipe_json(trimmed));
    return result;
  }

  result.recipe_json = serialize_edit_recipe_json(parse_edit_recipe_json(trimmed));
  const auto validation = validate_edit_recipe_json(result.recipe_json);
  if (!validation.valid) {
    result.errors = validation.errors;
    result.recipe_json.clear();
    return result;
  }
  result.migrated = true;
  return result;
}

inline QString recipe_diff_value_summary(const QJsonValue& value) {
  if (value.isUndefined()) {
    return QStringLiteral("<missing>");
  }
  if (value.isNull()) {
    return QStringLiteral("null");
  }
  if (value.isString()) {
    return QStringLiteral("\"%1\"").arg(value.toString());
  }
  if (value.isBool()) {
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'g', 12);
  }
  if (value.isArray()) {
    return QStringLiteral("[array:%1]").arg(value.toArray().size());
  }
  if (value.isObject()) {
    return QStringLiteral("{object:%1}").arg(value.toObject().size());
  }
  return QStringLiteral("<unknown>");
}

inline void append_recipe_json_diffs(
    QStringList* differences,
    const QString& path,
    const QJsonValue& before,
    const QJsonValue& after) {
  if (before.isObject() && after.isObject()) {
    const auto before_object = before.toObject();
    const auto after_object = after.toObject();
    QStringList keys = before_object.keys();
    for (const auto& key : after_object.keys()) {
      if (!keys.contains(key)) {
        keys.append(key);
      }
    }
    keys.sort();

    for (const auto& key : keys) {
      append_recipe_json_diffs(
          differences,
          path.isEmpty() ? key : QStringLiteral("%1.%2").arg(path, key),
          before_object.value(key),
          after_object.value(key));
    }
    return;
  }

  if (before.isArray() && after.isArray()) {
    const auto before_array = before.toArray();
    const auto after_array = after.toArray();
    const auto max_size = std::max(before_array.size(), after_array.size());
    for (int index = 0; index < max_size; ++index) {
      append_recipe_json_diffs(
          differences,
          QStringLiteral("%1[%2]").arg(path).arg(index),
          index < before_array.size() ? before_array.at(index) : QJsonValue(QJsonValue::Undefined),
          index < after_array.size() ? after_array.at(index) : QJsonValue(QJsonValue::Undefined));
    }
    return;
  }

  if (before != after) {
    if (path == QStringLiteral("id") || path.endsWith(QStringLiteral(".id"))) {
      return;
    }
    differences->append(
        QStringLiteral("%1: %2 -> %3")
            .arg(path.isEmpty() ? QStringLiteral("$") : path)
            .arg(recipe_diff_value_summary(before), recipe_diff_value_summary(after)));
  }
}

inline EditRecipeDiffResult diff_edit_recipe_json(
    const QString& before_recipe_json,
    const QString& after_recipe_json) {
  EditRecipeDiffResult result;
  const auto before_migration = migrate_edit_recipe_json(before_recipe_json);
  if (!before_migration.errors.isEmpty()) {
    for (const auto& error : before_migration.errors) {
      result.errors.append(QStringLiteral("before: %1").arg(error));
    }
  }
  const auto after_migration = migrate_edit_recipe_json(after_recipe_json);
  if (!after_migration.errors.isEmpty()) {
    for (const auto& error : after_migration.errors) {
      result.errors.append(QStringLiteral("after: %1").arg(error));
    }
  }
  if (!result.errors.isEmpty()) {
    return result;
  }

  const auto before_document = QJsonDocument::fromJson(before_migration.recipe_json.toUtf8());
  const auto after_document = QJsonDocument::fromJson(after_migration.recipe_json.toUtf8());
  append_recipe_json_diffs(
      &result.differences,
      QString(),
      before_document.object(),
      after_document.object());
  result.valid = true;
  return result;
}

inline QString edit_recipe_sidecar_path_for_asset(const QString& asset_file_path) {
  const QFileInfo asset_info(asset_file_path);
  if (asset_info.filePath().trimmed().isEmpty()) {
    return {};
  }
  return asset_info.dir().filePath(
      QStringLiteral("%1.photo-editor-recipe.json").arg(asset_info.fileName()));
}

inline EditRecipeSidecarResult export_edit_recipe_sidecar(
    const QString& asset_file_path,
    const QString& recipe_json) {
  EditRecipeSidecarResult result;
  result.sidecar_path = edit_recipe_sidecar_path_for_asset(asset_file_path);
  if (result.sidecar_path.isEmpty()) {
    result.errors.append(QStringLiteral("asset file path is empty"));
    return result;
  }

  const auto migration = migrate_edit_recipe_json(recipe_json);
  if (!migration.errors.isEmpty()) {
    result.errors = migration.errors;
    return result;
  }

  const QFileInfo sidecar_info(result.sidecar_path);
  if (!QDir().mkpath(sidecar_info.dir().absolutePath())) {
    result.errors.append(QStringLiteral("could not create sidecar directory"));
    return result;
  }

  QSaveFile file(result.sidecar_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    result.errors.append(QStringLiteral("could not open recipe sidecar for writing"));
    return result;
  }
  file.write(migration.recipe_json.toUtf8());
  file.write("\n");
  if (!file.commit()) {
    result.errors.append(QStringLiteral("could not atomically commit recipe sidecar"));
    return result;
  }

  result.ok = true;
  result.recipe_json = migration.recipe_json;
  return result;
}

inline EditRecipeSidecarResult import_edit_recipe_sidecar(const QString& asset_file_path) {
  EditRecipeSidecarResult result;
  result.sidecar_path = edit_recipe_sidecar_path_for_asset(asset_file_path);
  if (result.sidecar_path.isEmpty()) {
    result.errors.append(QStringLiteral("asset file path is empty"));
    return result;
  }

  QFile file(result.sidecar_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    result.errors.append(QStringLiteral("could not open recipe sidecar for reading"));
    return result;
  }

  const auto migration = migrate_edit_recipe_json(QString::fromUtf8(file.readAll()));
  if (!migration.errors.isEmpty()) {
    result.errors = migration.errors;
    return result;
  }

  result.ok = true;
  result.recipe_json = migration.recipe_json;
  return result;
}

}  // namespace photo_editor::desktop

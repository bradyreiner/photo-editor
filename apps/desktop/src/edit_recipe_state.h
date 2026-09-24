#pragma once

#include "edit_recipe_content_validation.h"
#include "edit_recipe_profile_validation.h"

#include <algorithm>
#include <array>

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace photo_editor::desktop {

inline constexpr int kCurrentEditRecipeVersion = 12;
// Version 13 is the canonical engine format. Desktop JSON with effect stacks
// uses 14; effect-free JSON keeps its established version/bytes.
inline constexpr int kPhotoEffectsEditRecipeVersion = 14;
inline constexpr int kPhotoTypographyEditRecipeVersion = 15;
inline constexpr int kPhotoPaintEditRecipeVersion = 16;

QString normalized_photographic_rendering_model_name(const QString& value);
QString normalized_renderer_transition_look_name(const QString& value);
QString normalized_camera_profile_rendering_model_name(const QString& value);

QString normalized_highlight_reconstruction_model_name(
    const QString& value);

QString normalized_output_color_space_name(const QString& value);

QString normalized_preview_output_mode_name(const QString& value);

QString normalized_input_color_profile_name(const QString& value);

QString normalized_working_color_profile_name(const QString& value);

QString normalized_monitor_color_profile_name(const QString& value);

QString normalized_render_intent_name(const QString& value);

QString normalized_gamut_mapping_policy_name(const QString& value);

QString normalized_camera_matching_look_name(const QString& value);

QString normalized_upright_mode_name(const QString& value);

QString normalized_highlight_reconstruction_mode_name(
    const QString& value);

QString normalized_chromatic_adaptation_method_name(
    const QString& value);

QString normalized_raw_demosaic_quality_name(const QString& value);

struct IccProfileAssetState final {
  QString asset_id;
  QString payload_sha256;
  QString display_name;
  QString source_uri;
  QString source_kind;
  qint64 byte_size = 0;

  [[nodiscard]] bool configured() const {
    return !asset_id.trimmed().isEmpty() ||
           !payload_sha256.trimmed().isEmpty() ||
           !source_uri.trimmed().isEmpty();
  }

  [[nodiscard]] bool operator==(const IccProfileAssetState& other) const = default;
};

struct DcpProfileAssetState final {
  int schema_version = 1;
  QString asset_id;
  QString payload_sha256;
  QString display_name;
  QString source_uri;
  QString source_kind;
  qint64 byte_size = 0;

  [[nodiscard]] bool configured() const {
    return !asset_id.trimmed().isEmpty() ||
           !payload_sha256.trimmed().isEmpty() ||
           !source_uri.trimmed().isEmpty();
  }

  [[nodiscard]] bool operator==(const DcpProfileAssetState& other) const = default;
};

struct EditRecipeState final {
  QString input_color_profile = QStringLiteral("Embedded");
  QString working_color_profile = QStringLiteral("Linear ProPhoto RGB");
  QString preview_color_space = QStringLiteral("sRGB");
  QString preview_output_mode = QStringLiteral("Monitor");
  DcpProfileAssetState camera_dcp_profile_asset;
  QString camera_profile_rendering_model = QStringLiteral("dng-profile-v2");
  QString photographic_rendering_model = QStringLiteral("aces2-ocio-v1"), renderer_transition_look = QStringLiteral("none");
  IccProfileAssetState input_icc_profile_asset;
  IccProfileAssetState preview_icc_profile_asset;
  IccProfileAssetState proof_icc_profile_asset;
  IccProfileAssetState export_icc_profile_asset;
  QString monitor_color_profile;
  bool soft_proof_enabled = false;
  QString soft_proof_profile = QStringLiteral("sRGB");
  QString proof_render_intent = QStringLiteral("Relative Colorimetric");
  bool proof_simulate_paper_white = false;
  bool proof_simulate_ink_black = false;
  QString render_intent = QStringLiteral("Perceptual");
  bool output_gamut_mapping_enabled = true;
  QString output_gamut_mapping_policy = QStringLiteral("Perceptual");
  bool black_point_compensation_enabled = true;
  double source_black_point = 0.0;
  double output_black_point = 0.0;
  bool raw_input_transform_enabled = true;
  QString highlight_reconstruction_mode = QStringLiteral("Enabled");
  QString highlight_reconstruction_model = QStringLiteral("camera-linear-opposed-v2");
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
  bool raw_gamut_mapping = false;
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
           preview_output_mode == other.preview_output_mode &&
           camera_dcp_profile_asset == other.camera_dcp_profile_asset &&
           camera_profile_rendering_model == other.camera_profile_rendering_model &&
           photographic_rendering_model == other.photographic_rendering_model && renderer_transition_look == other.renderer_transition_look &&
           input_icc_profile_asset == other.input_icc_profile_asset &&
           preview_icc_profile_asset == other.preview_icc_profile_asset &&
           proof_icc_profile_asset == other.proof_icc_profile_asset &&
           export_icc_profile_asset == other.export_icc_profile_asset &&
           monitor_color_profile == other.monitor_color_profile &&
           soft_proof_enabled == other.soft_proof_enabled &&
           soft_proof_profile == other.soft_proof_profile &&
           proof_render_intent == other.proof_render_intent &&
           proof_simulate_paper_white == other.proof_simulate_paper_white &&
           proof_simulate_ink_black == other.proof_simulate_ink_black &&
           render_intent == other.render_intent &&
           output_gamut_mapping_enabled == other.output_gamut_mapping_enabled &&
           output_gamut_mapping_policy == other.output_gamut_mapping_policy &&
           black_point_compensation_enabled == other.black_point_compensation_enabled &&
           feq(source_black_point, other.source_black_point) &&
           feq(output_black_point, other.output_black_point) &&
           raw_input_transform_enabled == other.raw_input_transform_enabled &&
           highlight_reconstruction_mode == other.highlight_reconstruction_mode &&
           highlight_reconstruction_model == other.highlight_reconstruction_model &&
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

QJsonObject object_value(
    const QJsonObject& object,
    const QString& key);

double read_double(
    const QJsonObject& primary,
    const QString& primary_key,
    const QJsonObject& legacy,
    const QString& legacy_key,
    double default_value = 0.0);

QString read_string(
    const QJsonObject& primary,
    const QString& primary_key,
    const QJsonObject& legacy,
    const QString& legacy_key,
    const QString& default_value = QString());

bool read_bool(
    const QJsonObject& primary,
    const QString& primary_key,
    const QJsonObject& legacy,
    const QString& legacy_key,
    bool default_value = false);

QByteArray read_byte_array(
    const QJsonObject& object,
    const QString& key);

QVariantList parse_photo_layers(const QJsonArray& layers);

QVariantList serialize_photo_layers(const QVariantList& layers);
bool photo_layers_have_effect_stacks(const QVariantList& layers);
bool photo_layers_have_typography(const QVariantList& layers);
bool photo_layers_have_paint_tiles(const QVariantList& layers);

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

}  // namespace photo_editor::desktop

#include "edit_recipe_mask_schema.h"

namespace photo_editor::desktop {

QStringList all_adjustment_groups();

QString normalize_adjustment_group(const QString& group);

QStringList normalize_adjustment_groups(const QStringList& groups);

QStringList adjustment_preview_sections();
EditRecipeState edit_recipe_without_preview_section(const EditRecipeState& state, const QString& section);

EditRecipeState merge_edit_recipe_state_by_groups(
    const EditRecipeState& base,
    const EditRecipeState& source,
    const QStringList& groups);

QVariantList parse_curve_points(const QJsonArray& points);

QJsonArray serialize_curve_points(const QVariantList& points);

IccProfileAssetState parse_icc_profile_asset_state(const QJsonValue& value);

QJsonObject serialize_icc_profile_asset_state(
    const IccProfileAssetState& state);

DcpProfileAssetState parse_dcp_profile_asset_state(const QJsonValue& value);

QJsonObject serialize_dcp_profile_asset_state(
    const DcpProfileAssetState& state);

EditRecipeState parse_edit_recipe_json(const QString& recipe_json);
EditRecipeState parse_edit_adjustment_state_object(const QJsonObject& adjustment_state);
QJsonObject build_edit_adjustment_state_object(const EditRecipeState& state);
QJsonObject build_edit_recipe_object(const EditRecipeState& state);
QString serialize_edit_recipe_json(const EditRecipeState& state);

QString normalized_history_branch_id(const QString& branch_id);

EditRecipeState append_named_recipe_snapshot(
    const EditRecipeState& state,
    const QString& name,
    const QString& branch_id = QString(),
    const QString& parent_snapshot_id = QString());

EditRecipeValidationResult validate_edit_recipe_json(
    const QString& recipe_json);

EditRecipeMigrationResult migrate_edit_recipe_json(
    const QString& recipe_json,
    int target_version = 0);  // Automatic: preserve the required document schema.

EditRecipeDiffResult diff_edit_recipe_json(
    const QString& before_recipe_json,
    const QString& after_recipe_json);

EditRecipeDiffResult diff_json_documents(
    const QString& before_json,
    const QString& after_json);

QString edit_recipe_sidecar_path_for_asset(const QString& asset_file_path);

EditRecipeSidecarResult import_edit_recipe_sidecar(
    const QString& asset_file_path);

}  // namespace photo_editor::desktop

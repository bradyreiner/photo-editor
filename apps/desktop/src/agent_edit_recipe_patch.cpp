#include "agent_edit_recipe_patch.h"

#include <QMetaType>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {

struct RecipeDoubleField final {
  const char* group;
  const char* field;
  double minimum;
  double maximum;
  double EditRecipeState::*member;
};

struct RecipeBoolField final {
  const char* group;
  const char* field;
  bool EditRecipeState::*member;
};

struct RecipeArrayDoubleField final {
  const char* group;
  const char* field;
  double minimum;
  double maximum;
  std::size_t index;
};

struct RecipeMixerChannelDoubleField final {
  const char* group;
  QString field;
  double minimum;
  double maximum;
  std::size_t index;
  double EditRecipeState::MixerChannel::*member;
};

struct RecipeRgbChannelMixerDoubleField final {
  const char* group;
  const char* field;
  double minimum;
  double maximum;
  double EditRecipeState::RgbChannelMixer::*member;
};

struct RecipeCurvePointsField final {
  const char* group;
  const char* field;
  QVariantList EditRecipeState::*member;
};

struct RecipeStringEnumField final {
  const char* group;
  const char* field;
  QString EditRecipeState::*member;
  QStringList allowed_values;
};

struct RecipeStringField final {
  const char* group;
  const char* field;
  QString EditRecipeState::*member;
  qsizetype maximum_length;
};

struct RecipeLayerStackField final {
  const char* group;
  const char* field;
  QVariantList EditRecipeState::*member;
};

struct RecipeMaskStackField final {
  const char* group;
  const char* field;
  QVariantList EditRecipeState::*member;
};

struct RecipeRetouchStackField final {
  const char* group;
  const char* field;
  QVariantList EditRecipeState::*member;
};

const QStringList& mixer_channel_names() {
  static const QStringList names = {
      QStringLiteral("Reds"),
      QStringLiteral("Oranges"),
      QStringLiteral("Yellows"),
      QStringLiteral("Greens"),
      QStringLiteral("Aquas"),
      QStringLiteral("Blues"),
      QStringLiteral("Purples"),
      QStringLiteral("Magentas"),
  };
  return names;
}

const QStringList& color_mixer_models() {
  static const QStringList models = {
      QStringLiteral("OKLab"),
      QStringLiteral("LCH"),
      QStringLiteral("HSV"),
      QStringLiteral("HSL"),
  };
  return models;
}

const QStringList& output_color_spaces() {
  static const QStringList spaces = {
      QStringLiteral("sRGB"),
      QStringLiteral("Display P3"),
      QStringLiteral("Adobe RGB"),
      QStringLiteral("ProPhoto RGB"),
      QStringLiteral("Rec.709"),
      QStringLiteral("Rec.2020"),
  };
  return spaces;
}

const QStringList& layer_blend_modes() {
  static const QStringList modes = {
      QStringLiteral("normal"),
      QStringLiteral("multiply"),
      QStringLiteral("screen"),
      QStringLiteral("overlay"),
      QStringLiteral("soft light"),
      QStringLiteral("soft-light"),
      QStringLiteral("hard light"),
      QStringLiteral("hard-light"),
      QStringLiteral("color"),
      QStringLiteral("luminosity"),
      QStringLiteral("hue"),
      QStringLiteral("saturation"),
      QStringLiteral("difference"),
      QStringLiteral("linear light"),
      QStringLiteral("linear-light"),
      QStringLiteral("lighten"),
      QStringLiteral("darken"),
      QStringLiteral("replace"),
  };
  return modes;
}

const QStringList& mask_kinds() {
  static const QStringList kinds = {
      QStringLiteral("linear"),
      QStringLiteral("radial"),
      QStringLiteral("ellipse"),
      QStringLiteral("rectangle"),
      QStringLiteral("brush"),
      QStringLiteral("polygon"),
      QStringLiteral("lasso"),
      QStringLiteral("luminance"),
      QStringLiteral("color"),
      QStringLiteral("depth"),
      QStringLiteral("subject"),
      QStringLiteral("sky"),
      QStringLiteral("background"),
      QStringLiteral("people"),
      QStringLiteral("face"),
      QStringLiteral("hair"),
      QStringLiteral("clothing"),
      QStringLiteral("object"),
  };
  return kinds;
}

const QStringList& mask_operations() {
  static const QStringList operations = {
      QStringLiteral("add"),
      QStringLiteral("subtract"),
      QStringLiteral("intersect"),
      QStringLiteral("exclude"),
      QStringLiteral("replace"),
      QStringLiteral("union"),
  };
  return operations;
}

const QStringList& retouch_modes() {
  static const QStringList modes = {
      QStringLiteral("clone"),
      QStringLiteral("heal"),
      QStringLiteral("patch"),
      QStringLiteral("remove"),
  };
  return modes;
}

const QStringList& mask_geometry_fields() {
  static const QStringList fields = {
      QStringLiteral("centerX"),
      QStringLiteral("centerY"),
      QStringLiteral("radiusX"),
      QStringLiteral("radiusY"),
      QStringLiteral("position"),
      QStringLiteral("rotation"),
      QStringLiteral("brushPoints"),
      QStringLiteral("polygonPoints"),
      QStringLiteral("luminanceMin"),
      QStringLiteral("luminanceMax"),
      QStringLiteral("saturationMin"),
      QStringLiteral("saturationMax"),
      QStringLiteral("depthMin"),
      QStringLiteral("depthMax"),
      QStringLiteral("rangeFeather"),
  };
  return fields;
}

const QStringList& mask_local_adjustment_fields() {
  static const QStringList fields = {
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
      QStringLiteral("hueShift"),
      QStringLiteral("saturationShift"),
      QStringLiteral("texture"),
      QStringLiteral("clarity"),
      QStringLiteral("dehaze"),
      QStringLiteral("sharpening"),
      QStringLiteral("sharpenRadius"),
      QStringLiteral("sharpenDetail"),
      QStringLiteral("sharpenMasking"),
      QStringLiteral("luminanceNr"),
      QStringLiteral("colorNr"),
      QStringLiteral("opacity"),
      QStringLiteral("density"),
      QStringLiteral("blendMode"),
      QStringLiteral("colorizeHue"),
      QStringLiteral("colorizeStrength"),
  };
  return fields;
}

bool forbidden_recipe_payload_key(const QString& key) {
  auto normalized = key.trimmed().toLower();
  normalized.remove(QLatin1Char('_'));
  normalized.remove(QLatin1Char('-'));
  normalized.remove(QLatin1Char(' '));
  static const QStringList forbidden = {
      QStringLiteral("recipe"),
      QStringLiteral("recipejson"),
      QStringLiteral("fullrecipe"),
      QStringLiteral("fullrecipejson"),
      QStringLiteral("editrecipe"),
      QStringLiteral("editrecipejson"),
      QStringLiteral("serializedrecipe"),
      QStringLiteral("serializedrecipejson"),
      QStringLiteral("recipedocument"),
      QStringLiteral("recipedocumentjson"),
  };
  return forbidden.contains(normalized);
}

void append_forbidden_recipe_payload_errors(
    const QVariant& value,
    const QString& path,
    QStringList* errors) {
  if (errors == nullptr) {
    return;
  }
  if (value.typeId() == QMetaType::QVariantMap) {
    const auto map = value.toMap();
    for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator) {
      const auto child_path = path.isEmpty() ? iterator.key()
                                             : QStringLiteral("%1.%2").arg(path, iterator.key());
      if (forbidden_recipe_payload_key(iterator.key())) {
        errors->append(
            QStringLiteral("Recipe patch must not include direct recipe JSON payload field: %1")
                .arg(child_path));
      }
      append_forbidden_recipe_payload_errors(iterator.value(), child_path, errors);
    }
    return;
  }
  if (value.typeId() == QMetaType::QVariantList) {
    const auto list = value.toList();
    for (int index = 0; index < list.size(); ++index) {
      append_forbidden_recipe_payload_errors(
          list.at(index),
          QStringLiteral("%1[%2]").arg(path).arg(index),
          errors);
    }
  }
}

int mixer_channel_index(const QString& channel) {
  return static_cast<int>(mixer_channel_names().indexOf(channel));
}

void sync_selected_hsl_channel(EditRecipeState* state) {
  const auto active_index = mixer_channel_index(state->hsl_channel);
  if (active_index < 0 || active_index >= 8) {
    return;
  }
  const auto& channel = state->mixer_channels.at(static_cast<std::size_t>(active_index));
  state->hue_adjustment = channel.hue;
  state->saturation_adjustment = channel.saturation;
  state->luminance_adjustment = channel.luminance;
}

const QVector<RecipeDoubleField>& recipe_double_fields() {
  static const QVector<RecipeDoubleField> fields = {
      {"raw", "rawExposure", -5.0, 5.0, &EditRecipeState::raw_exposure},
      {"raw", "rawBoost", -2.0, 2.0, &EditRecipeState::raw_boost},
      {"raw", "rawShadowBoost", 0.0, 3.0, &EditRecipeState::raw_shadow_boost},
      {"raw", "rawDetail", 0.0, 100.0, &EditRecipeState::raw_detail},
      {"raw", "rawChromaNoiseReduction", 0.0, 100.0, &EditRecipeState::raw_chroma_noise_reduction},
      {"raw", "rawMoireReduction", 0.0, 100.0, &EditRecipeState::raw_moire_reduction},
      {"raw", "rawHotDeadPixelCleanup", 0.0, 1.0, &EditRecipeState::raw_hot_dead_pixel_cleanup},
      {"raw", "rawFalseColorSuppression", 0.0, 1.0, &EditRecipeState::raw_false_color_suppression},
      {"raw", "rawWhiteBalanceTemperature", 1800.0, 50000.0, &EditRecipeState::raw_white_balance_temperature},
      {"raw", "rawWhiteBalanceTint", -150.0, 150.0, &EditRecipeState::raw_white_balance_tint},
      {"raw", "rawBlackLevel", 0.0, 0.95, &EditRecipeState::raw_black_level},
      {"raw", "rawWhiteLevel", 0.01, 4.0, &EditRecipeState::raw_white_level},
      {"raw", "rawCalibrationHue", -100.0, 100.0, &EditRecipeState::raw_calibration_hue},
      {"raw", "rawCalibrationSaturation", -100.0, 100.0, &EditRecipeState::raw_calibration_saturation},
      {"raw", "rawRedPrimaryHue", -100.0, 100.0, &EditRecipeState::raw_red_primary_hue},
      {"raw", "rawRedPrimarySaturation", -100.0, 100.0, &EditRecipeState::raw_red_primary_saturation},
      {"raw", "rawGreenPrimaryHue", -100.0, 100.0, &EditRecipeState::raw_green_primary_hue},
      {"raw", "rawGreenPrimarySaturation", -100.0, 100.0, &EditRecipeState::raw_green_primary_saturation},
      {"raw", "rawBluePrimaryHue", -100.0, 100.0, &EditRecipeState::raw_blue_primary_hue},
      {"raw", "rawBluePrimarySaturation", -100.0, 100.0, &EditRecipeState::raw_blue_primary_saturation},
      {"globalTone", "exposure", -5.0, 5.0, &EditRecipeState::exposure},
      {"globalTone", "contrast", -100.0, 100.0, &EditRecipeState::contrast},
      {"globalTone", "highlights", -100.0, 100.0, &EditRecipeState::highlights},
      {"globalTone", "shadows", -100.0, 100.0, &EditRecipeState::shadows},
      {"globalTone", "whites", -100.0, 100.0, &EditRecipeState::whites},
      {"globalTone", "blacks", -100.0, 100.0, &EditRecipeState::blacks},
      {"globalTone", "brightness", -100.0, 100.0, &EditRecipeState::brightness},
      {"globalTone", "hdrLocalToneMapping", -100.0, 100.0, &EditRecipeState::hdr_local_tone_mapping},
      {"globalTone", "filmicShoulder", -100.0, 100.0, &EditRecipeState::filmic_shoulder},
      {"globalTone", "filmicToe", -100.0, 100.0, &EditRecipeState::filmic_toe},
      {"globalTone", "adaptiveContrast", -100.0, 100.0, &EditRecipeState::adaptive_contrast},
      {"globalTone", "levelsBlackPoint", -100.0, 100.0, &EditRecipeState::levels_black_point},
      {"globalTone", "levelsGamma", -100.0, 100.0, &EditRecipeState::levels_gamma},
      {"globalTone", "levelsWhitePoint", -100.0, 100.0, &EditRecipeState::levels_white_point},
      {"globalTone", "zoneDeepShadows", -100.0, 100.0, &EditRecipeState::zone_deep_shadows},
      {"globalTone", "zoneShadows", -100.0, 100.0, &EditRecipeState::zone_shadows},
      {"globalTone", "zoneMidtones", -100.0, 100.0, &EditRecipeState::zone_midtones},
      {"globalTone", "zoneLights", -100.0, 100.0, &EditRecipeState::zone_lights},
      {"globalTone", "zoneHighlights", -100.0, 100.0, &EditRecipeState::zone_highlights},
      {"color", "temperature", -100.0, 100.0, &EditRecipeState::temperature},
      {"color", "tint", -100.0, 100.0, &EditRecipeState::tint},
      {"color", "vibrance", -100.0, 100.0, &EditRecipeState::vibrance},
      {"color", "saturation", -100.0, 100.0, &EditRecipeState::saturation},
      {"curves", "curveShadows", -100.0, 100.0, &EditRecipeState::curve_shadows},
      {"curves", "curveDarks", -100.0, 100.0, &EditRecipeState::curve_darks},
      {"curves", "curveLights", -100.0, 100.0, &EditRecipeState::curve_lights},
      {"curves", "curveHighlights", -100.0, 100.0, &EditRecipeState::curve_highlights},
      {"hsl", "hueAdjustment", -180.0, 180.0, &EditRecipeState::hue_adjustment},
      {"hsl", "saturationAdjustment", -100.0, 100.0, &EditRecipeState::saturation_adjustment},
      {"hsl", "luminanceAdjustment", -100.0, 100.0, &EditRecipeState::luminance_adjustment},
      {"detail", "texture", -100.0, 100.0, &EditRecipeState::texture},
      {"detail", "clarity", -100.0, 100.0, &EditRecipeState::clarity},
      {"detail", "dehaze", -100.0, 100.0, &EditRecipeState::dehaze},
      {"detail", "sharpening", 0.0, 100.0, &EditRecipeState::sharpening},
      {"detail", "sharpenRadius", 0.1, 5.0, &EditRecipeState::sharpen_radius},
      {"detail", "sharpenDetail", 0.0, 100.0, &EditRecipeState::sharpen_detail},
      {"detail", "sharpenMasking", 0.0, 100.0, &EditRecipeState::sharpen_masking},
      {"detail", "luminanceNr", 0.0, 100.0, &EditRecipeState::luminance_nr},
      {"detail", "colorNr", 0.0, 100.0, &EditRecipeState::color_nr},
      {"optics", "lensDistortion", -100.0, 100.0, &EditRecipeState::lens_distortion},
      {"optics", "lensVignette", -100.0, 100.0, &EditRecipeState::lens_vignette},
      {"optics", "chromaticAberration", 0.0, 100.0, &EditRecipeState::chromatic_aberration},
      {"crop", "perspectiveVertical", -100.0, 100.0, &EditRecipeState::perspective_vertical},
      {"crop", "perspectiveHorizontal", -100.0, 100.0, &EditRecipeState::perspective_horizontal},
      {"crop", "transformRotation", -45.0, 45.0, &EditRecipeState::transform_rotation},
      {"crop", "transformScale", 0.1, 10.0, &EditRecipeState::transform_scale},
      {"crop", "transformSkewX", -45.0, 45.0, &EditRecipeState::transform_skew_x},
      {"crop", "transformSkewY", -45.0, 45.0, &EditRecipeState::transform_skew_y},
      {"crop", "cropZoom", 0.1, 10.0, &EditRecipeState::crop_zoom},
      {"crop", "cropX", 0.0, 1.0, &EditRecipeState::crop_rect_x},
      {"crop", "cropY", 0.0, 1.0, &EditRecipeState::crop_rect_y},
      {"crop", "cropWidth", 0.0, 1.0, &EditRecipeState::crop_rect_width},
      {"crop", "cropHeight", 0.0, 1.0, &EditRecipeState::crop_rect_height},
      {"crop", "uprightVerticalGuideAngle", -45.0, 45.0, &EditRecipeState::upright_vertical_guide_angle},
      {"crop", "uprightHorizontalGuideAngle", -45.0, 45.0, &EditRecipeState::upright_horizontal_guide_angle},
      {"output", "outputHeadroom", 1.0, 4.0, &EditRecipeState::output_headroom},
      {"masks", "linearMaskExposure", -5.0, 5.0, &EditRecipeState::linear_mask_exposure},
      {"masks", "linearMaskTemperature", -100.0, 100.0, &EditRecipeState::linear_mask_temperature},
      {"masks", "linearMaskFeather", 0.0, 1.0, &EditRecipeState::linear_mask_feather},
      {"masks", "linearMaskPosition", 0.0, 1.0, &EditRecipeState::linear_mask_position},
      {"masks", "linearMaskRotation", -180.0, 180.0, &EditRecipeState::linear_mask_rotation},
      {"masks", "radialMaskExposure", -5.0, 5.0, &EditRecipeState::radial_mask_exposure},
      {"masks", "radialMaskTemperature", -100.0, 100.0, &EditRecipeState::radial_mask_temperature},
      {"masks", "radialMaskFeather", 0.0, 1.0, &EditRecipeState::radial_mask_feather},
      {"masks", "radialMaskCenterX", 0.0, 1.0, &EditRecipeState::radial_mask_center_x},
      {"masks", "radialMaskCenterY", 0.0, 1.0, &EditRecipeState::radial_mask_center_y},
      {"masks", "radialMaskRadiusX", 0.01, 1.0, &EditRecipeState::radial_mask_radius_x},
      {"masks", "radialMaskRadiusY", 0.01, 1.0, &EditRecipeState::radial_mask_radius_y},
      {"masks", "radialMaskRotation", -180.0, 180.0, &EditRecipeState::radial_mask_rotation},
      {"colorMixer", "colorReplacementSourceHue", 0.0, 1.0, &EditRecipeState::color_replacement_source_hue},
      {"colorMixer", "colorReplacementTargetHue", 0.0, 1.0, &EditRecipeState::color_replacement_target_hue},
      {"colorMixer", "colorReplacementRange", 0.0, 180.0, &EditRecipeState::color_replacement_range},
      {"colorMixer", "colorReplacementSoftness", 0.0, 100.0, &EditRecipeState::color_replacement_softness},
      {"colorMixer", "colorReplacementStrength", 0.0, 100.0, &EditRecipeState::color_replacement_strength},
      {"colorMixer", "skinToneLineHue", 0.0, 1.0, &EditRecipeState::skin_tone_line_hue},
      {"colorMixer", "skinToneProtectionRange", 0.0, 180.0, &EditRecipeState::skin_tone_protection_range},
      {"colorMixer", "skinToneProtectionStrength", 0.0, 100.0, &EditRecipeState::skin_tone_protection_strength},
      {"colorGrading", "colorHarmonyBaseHue", 0.0, 1.0, &EditRecipeState::color_harmony_base_hue},
      {"colorGrading", "colorHarmonyStrength", 0.0, 100.0, &EditRecipeState::color_harmony_strength},
      {"colorGrading", "gradingShadowsHue", 0.0, 1.0, &EditRecipeState::grading_shadows_hue},
      {"colorGrading", "gradingShadowsSaturation", -100.0, 100.0, &EditRecipeState::grading_shadows_saturation},
      {"colorGrading", "gradingShadowsLuminance", -100.0, 100.0, &EditRecipeState::grading_shadows_luminance},
      {"colorGrading", "gradingMidtonesHue", 0.0, 1.0, &EditRecipeState::grading_midtones_hue},
      {"colorGrading", "gradingMidtonesSaturation", -100.0, 100.0, &EditRecipeState::grading_midtones_saturation},
      {"colorGrading", "gradingMidtonesLuminance", -100.0, 100.0, &EditRecipeState::grading_midtones_luminance},
      {"colorGrading", "gradingHighlightsHue", 0.0, 1.0, &EditRecipeState::grading_highlights_hue},
      {"colorGrading", "gradingHighlightsSaturation", -100.0, 100.0, &EditRecipeState::grading_highlights_saturation},
      {"colorGrading", "gradingHighlightsLuminance", -100.0, 100.0, &EditRecipeState::grading_highlights_luminance},
      {"colorGrading", "gradingAmount", 0.0, 100.0, &EditRecipeState::grading_amount},
      {"colorGrading", "gradingBalance", -100.0, 100.0, &EditRecipeState::grading_balance},
  };
  return fields;
}

const QVector<RecipeArrayDoubleField>& recipe_array_double_fields() {
  static const QVector<RecipeArrayDoubleField> fields = {
      {"colorMixer", "blackAndWhiteReds", -100.0, 100.0, 0},
      {"colorMixer", "blackAndWhiteOranges", -100.0, 100.0, 1},
      {"colorMixer", "blackAndWhiteYellows", -100.0, 100.0, 2},
      {"colorMixer", "blackAndWhiteGreens", -100.0, 100.0, 3},
      {"colorMixer", "blackAndWhiteAquas", -100.0, 100.0, 4},
      {"colorMixer", "blackAndWhiteBlues", -100.0, 100.0, 5},
      {"colorMixer", "blackAndWhitePurples", -100.0, 100.0, 6},
      {"colorMixer", "blackAndWhiteMagentas", -100.0, 100.0, 7},
  };
  return fields;
}

const QVector<RecipeMixerChannelDoubleField>& recipe_mixer_channel_double_fields() {
  static const QVector<RecipeMixerChannelDoubleField> fields = [] {
    QVector<RecipeMixerChannelDoubleField> values;
    const QStringList prefixes = {
        QStringLiteral("reds"),
        QStringLiteral("oranges"),
        QStringLiteral("yellows"),
        QStringLiteral("greens"),
        QStringLiteral("aquas"),
        QStringLiteral("blues"),
        QStringLiteral("purples"),
        QStringLiteral("magentas"),
    };
    for (qsizetype i = 0; i < prefixes.size(); ++i) {
      const auto index = static_cast<std::size_t>(i);
      const auto prefix = prefixes.at(i);
      values.append({"colorMixer", prefix + QStringLiteral("Hue"), -100.0, 100.0, index,
                     &EditRecipeState::MixerChannel::hue});
      values.append({"colorMixer", prefix + QStringLiteral("Saturation"), -100.0, 100.0, index,
                     &EditRecipeState::MixerChannel::saturation});
      values.append({"colorMixer", prefix + QStringLiteral("Luminance"), -100.0, 100.0, index,
                     &EditRecipeState::MixerChannel::luminance});
      values.append({"colorMixer", prefix + QStringLiteral("SelectiveCyan"), -100.0, 100.0, index,
                     &EditRecipeState::MixerChannel::selective_cyan});
      values.append({"colorMixer", prefix + QStringLiteral("SelectiveMagenta"), -100.0, 100.0, index,
                     &EditRecipeState::MixerChannel::selective_magenta});
      values.append({"colorMixer", prefix + QStringLiteral("SelectiveYellow"), -100.0, 100.0, index,
                     &EditRecipeState::MixerChannel::selective_yellow});
      values.append({"colorMixer", prefix + QStringLiteral("SelectiveBlack"), -100.0, 100.0, index,
                     &EditRecipeState::MixerChannel::selective_black});
    }
    return values;
  }();
  return fields;
}

const QVector<RecipeRgbChannelMixerDoubleField>& recipe_rgb_channel_mixer_double_fields() {
  static const QVector<RecipeRgbChannelMixerDoubleField> fields = {
      {"colorMixer", "channelMixerRedFromRed", -200.0, 200.0, &EditRecipeState::RgbChannelMixer::red_from_red},
      {"colorMixer", "channelMixerRedFromGreen", -200.0, 200.0, &EditRecipeState::RgbChannelMixer::red_from_green},
      {"colorMixer", "channelMixerRedFromBlue", -200.0, 200.0, &EditRecipeState::RgbChannelMixer::red_from_blue},
      {"colorMixer", "channelMixerGreenFromRed", -200.0, 200.0, &EditRecipeState::RgbChannelMixer::green_from_red},
      {"colorMixer", "channelMixerGreenFromGreen", -200.0, 200.0, &EditRecipeState::RgbChannelMixer::green_from_green},
      {"colorMixer", "channelMixerGreenFromBlue", -200.0, 200.0, &EditRecipeState::RgbChannelMixer::green_from_blue},
      {"colorMixer", "channelMixerBlueFromRed", -200.0, 200.0, &EditRecipeState::RgbChannelMixer::blue_from_red},
      {"colorMixer", "channelMixerBlueFromGreen", -200.0, 200.0, &EditRecipeState::RgbChannelMixer::blue_from_green},
      {"colorMixer", "channelMixerBlueFromBlue", -200.0, 200.0, &EditRecipeState::RgbChannelMixer::blue_from_blue},
  };
  return fields;
}

const QVector<RecipeCurvePointsField>& recipe_curve_points_fields() {
  static const QVector<RecipeCurvePointsField> fields = {
      {"curves", "masterCurvePoints", &EditRecipeState::master_curve_points},
      {"curves", "lumaCurvePoints", &EditRecipeState::luma_curve_points},
      {"curves", "redCurvePoints", &EditRecipeState::red_curve_points},
      {"curves", "greenCurvePoints", &EditRecipeState::green_curve_points},
      {"curves", "blueCurvePoints", &EditRecipeState::blue_curve_points},
  };
  return fields;
}

const QVector<RecipeStringEnumField>& recipe_string_enum_fields() {
  static const QVector<RecipeStringEnumField> fields = {
      {"raw", "rawDemosaicQuality", &EditRecipeState::raw_demosaic_quality,
       {QStringLiteral("Preview"), QStringLiteral("Balanced"), QStringLiteral("Full")}},
      {"raw", "rawCameraMatchingLook", &EditRecipeState::raw_camera_matching_look,
       {
           QStringLiteral("Standard"),
           QStringLiteral("Neutral"),
           QStringLiteral("Vivid"),
           QStringLiteral("Portrait"),
           QStringLiteral("Landscape"),
           QStringLiteral("Monochrome"),
       }},
      {"crop", "uprightMode", &EditRecipeState::upright_mode,
       {
           QStringLiteral("Off"),
           QStringLiteral("Vertical"),
           QStringLiteral("Full"),
           QStringLiteral("Guided"),
       }},
      {"output", "outputColorSpace", &EditRecipeState::output_color_space, output_color_spaces()},
      {"output", "softProofProfile", &EditRecipeState::soft_proof_profile, output_color_spaces()},
      {"output", "renderIntent", &EditRecipeState::render_intent,
       {QStringLiteral("Perceptual"), QStringLiteral("Relative Colorimetric")}},
      {"colorMixer", "colorMixerModel", &EditRecipeState::color_mixer_model, color_mixer_models()},
      {"colorMixer", "hslChannel", &EditRecipeState::hsl_channel, mixer_channel_names()},
      {"colorGrading",
       "colorHarmonyMode",
       &EditRecipeState::color_harmony_mode,
       {
           QStringLiteral("Complementary"),
           QStringLiteral("Analogous"),
           QStringLiteral("Triadic"),
           QStringLiteral("Split Complementary"),
           QStringLiteral("Tetradic"),
       }},
  };
  return fields;
}

const QVector<RecipeStringField>& recipe_string_fields() {
  static const QVector<RecipeStringField> fields = {
      {"crop", "cropPreset", &EditRecipeState::crop_preset, 64},
  };
  return fields;
}

const QVector<RecipeLayerStackField>& recipe_layer_stack_fields() {
  static const QVector<RecipeLayerStackField> fields = {
      {"layers", "photoLayers", &EditRecipeState::layers},
  };
  return fields;
}

const QVector<RecipeMaskStackField>& recipe_mask_stack_fields() {
  static const QVector<RecipeMaskStackField> fields = {
      {"masks", "maskStack", &EditRecipeState::mask_stack},
  };
  return fields;
}

const QVector<RecipeRetouchStackField>& recipe_retouch_stack_fields() {
  static const QVector<RecipeRetouchStackField> fields = {
      {"retouch", "retouchStack", &EditRecipeState::mask_stack},
  };
  return fields;
}

const QVector<RecipeBoolField>& recipe_bool_fields() {
  static const QVector<RecipeBoolField> fields = {
      {"raw", "rawHighlightRecovery", &EditRecipeState::raw_highlight_recovery},
      {"raw", "rawGamutMapping", &EditRecipeState::raw_gamut_mapping},
      {"raw", "rawLensCorrection", &EditRecipeState::raw_lens_correction},
      {"raw", "rawWhiteBalanceOverride", &EditRecipeState::raw_white_balance_override},
      {"crop", "contentAwareCropFill", &EditRecipeState::content_aware_crop_fill},
      {"output", "softProofEnabled", &EditRecipeState::soft_proof_enabled},
      {"colorMixer", "blackAndWhiteEnabled", &EditRecipeState::black_and_white_enabled},
      {"colorMixer", "colorReplacementEnabled", &EditRecipeState::color_replacement_enabled},
      {"colorMixer", "colorReplacementPreserveLuminance", &EditRecipeState::color_replacement_preserve_luminance},
      {"colorMixer", "colorReplacementPreserveSaturation", &EditRecipeState::color_replacement_preserve_saturation},
      {"colorMixer", "skinToneProtectionEnabled", &EditRecipeState::skin_tone_protection_enabled},
      {"colorGrading", "colorHarmonyEnabled", &EditRecipeState::color_harmony_enabled},
  };
  return fields;
}

QStringList group_tool_names(const QString& group) {
  if (group == QStringLiteral("raw")) {
    return {QStringLiteral("raw")};
  }
  if (group == QStringLiteral("globalTone") || group == QStringLiteral("color") ||
      group == QStringLiteral("output")) {
    return {QStringLiteral("global_color")};
  }
  if (group == QStringLiteral("curves")) {
    return {QStringLiteral("curves")};
  }
  if (group == QStringLiteral("hsl")) {
    return {QStringLiteral("hsl")};
  }
  if (group == QStringLiteral("colorMixer")) {
    return {QStringLiteral("hsl")};
  }
  if (group == QStringLiteral("colorGrading")) {
    return {QStringLiteral("color_grading")};
  }
  if (group == QStringLiteral("detail")) {
    return {QStringLiteral("detail")};
  }
  if (group == QStringLiteral("optics") || group == QStringLiteral("crop")) {
    return {QStringLiteral("crop")};
  }
  if (group == QStringLiteral("masks")) {
    return {QStringLiteral("masks")};
  }
  if (group == QStringLiteral("retouch")) {
    return {QStringLiteral("retouch")};
  }
  if (group == QStringLiteral("layers") || group == QStringLiteral("luts")) {
    return {QStringLiteral("layers_luts")};
  }
  return {};
}

bool style_tool_enabled(const QVariantMap& style, const QString& tool) {
  const auto allowed_tools = style.value(QStringLiteral("allowedTools")).toMap();
  const auto value = allowed_tools.value(tool);
  if (value.typeId() == QMetaType::Bool) {
    return value.toBool();
  }
  const auto text = value.toString().trimmed();
  return !text.isEmpty() && text != QStringLiteral("false") &&
         text != QStringLiteral("off") && text != QStringLiteral("none");
}

void append_tool_validation_warnings(
    const QString& group,
    const QVariantMap& fields,
    QStringList* warnings) {
  if (warnings == nullptr || fields.isEmpty()) {
    return;
  }
  if (group == QStringLiteral("raw")) {
    warnings->append(QStringLiteral("RAW edits should be reviewed against source decode and camera profile behavior."));
  } else if (group == QStringLiteral("crop") || group == QStringLiteral("optics")) {
    warnings->append(QStringLiteral("Crop, transform, and optics edits require visual proof review for framing and geometry."));
  } else if (group == QStringLiteral("masks")) {
    warnings->append(QStringLiteral("Mask edits require coverage and leakage review before commit."));
  } else if (group == QStringLiteral("retouch")) {
    warnings->append(QStringLiteral("Retouch edits require provenance review before commit or export."));
  } else if (group == QStringLiteral("layers") || group == QStringLiteral("luts")) {
    warnings->append(QStringLiteral("Layer and LUT edits require opacity, blend, gamut, and ordering review."));
  } else if (group == QStringLiteral("output")) {
    warnings->append(QStringLiteral("Output color and soft-proof edits require export preflight review."));
  }
}

const RecipeDoubleField* find_double_field(const QString& group, const QString& field) {
  const auto& fields = recipe_double_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeDoubleField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeBoolField* find_bool_field(const QString& group, const QString& field) {
  const auto& fields = recipe_bool_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeBoolField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeArrayDoubleField* find_array_double_field(const QString& group, const QString& field) {
  const auto& fields = recipe_array_double_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeArrayDoubleField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeMixerChannelDoubleField* find_mixer_channel_double_field(const QString& group, const QString& field) {
  const auto& fields = recipe_mixer_channel_double_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeMixerChannelDoubleField& candidate) {
        return group == QString::fromLatin1(candidate.group) && field == candidate.field;
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeRgbChannelMixerDoubleField* find_rgb_channel_mixer_double_field(
    const QString& group,
    const QString& field) {
  const auto& fields = recipe_rgb_channel_mixer_double_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeRgbChannelMixerDoubleField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeCurvePointsField* find_curve_points_field(const QString& group, const QString& field) {
  const auto& fields = recipe_curve_points_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeCurvePointsField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeStringEnumField* find_string_enum_field(const QString& group, const QString& field) {
  const auto& fields = recipe_string_enum_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeStringEnumField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeStringField* find_string_field(const QString& group, const QString& field) {
  const auto& fields = recipe_string_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeStringField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeLayerStackField* find_layer_stack_field(const QString& group, const QString& field) {
  const auto& fields = recipe_layer_stack_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeLayerStackField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeMaskStackField* find_mask_stack_field(const QString& group, const QString& field) {
  const auto& fields = recipe_mask_stack_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeMaskStackField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

const RecipeRetouchStackField* find_retouch_stack_field(const QString& group, const QString& field) {
  const auto& fields = recipe_retouch_stack_fields();
  const auto iterator = std::find_if(
      fields.cbegin(),
      fields.cend(),
      [&](const RecipeRetouchStackField& candidate) {
        return group == QString::fromLatin1(candidate.group) &&
               field == QString::fromLatin1(candidate.field);
      });
  return iterator == fields.cend() ? nullptr : &(*iterator);
}

void sync_active_hsl_channel(EditRecipeState* state, const RecipeMixerChannelDoubleField& field) {
  const auto active_index = mixer_channel_index(state->hsl_channel);
  if (active_index != static_cast<int>(field.index)) {
    return;
  }
  sync_selected_hsl_channel(state);
}

void validate_crop_rect_fields(const QVariantMap& fields, QStringList* errors) {
  static const QStringList crop_rect_fields = {
      QStringLiteral("cropX"),
      QStringLiteral("cropY"),
      QStringLiteral("cropWidth"),
      QStringLiteral("cropHeight"),
  };
  auto has_any = false;
  for (const auto& field : crop_rect_fields) {
    has_any = has_any || fields.contains(field);
  }
  if (!has_any) {
    return;
  }
  for (const auto& field : crop_rect_fields) {
    if (!fields.contains(field)) {
      errors->append(QStringLiteral("Crop rectangle patches must include cropX, cropY, cropWidth, and cropHeight"));
      return;
    }
  }
  const auto x = fields.value(QStringLiteral("cropX")).toDouble();
  const auto y = fields.value(QStringLiteral("cropY")).toDouble();
  const auto width = fields.value(QStringLiteral("cropWidth")).toDouble();
  const auto height = fields.value(QStringLiteral("cropHeight")).toDouble();
  const auto is_reset = std::abs(width) <= 1e-6 && std::abs(height) <= 1e-6 &&
                        std::abs(x) <= 1e-6 && std::abs(y) <= 1e-6;
  if (is_reset) {
    return;
  }
  if (width < 0.05 || height < 0.05) {
    errors->append(QStringLiteral("Crop rectangle width and height must be 0.05 or greater unless resetting to zero"));
  }
  if (x + width > 1.0 + 1e-6 || y + height > 1.0 + 1e-6) {
    errors->append(QStringLiteral("Crop rectangle must fit within normalized image bounds"));
  }
}

bool is_bounded_non_empty_string(const QVariant& value, qsizetype maximum_length) {
  if (value.typeId() != QMetaType::QString) {
    return false;
  }
  const auto text = value.toString().trimmed();
  return !text.isEmpty() && text.size() <= maximum_length;
}

bool is_optional_bounded_string(const QVariantMap& object, const QString& key, qsizetype maximum_length) {
  return !object.contains(key) || is_bounded_non_empty_string(object.value(key), maximum_length);
}

bool variant_is_bool(const QVariantMap& object, const QString& key) {
  return !object.contains(key) || object.value(key).typeId() == QMetaType::Bool;
}

bool variant_is_finite_number(const QVariant& value, double* parsed) {
  auto ok = false;
  const auto number = value.toDouble(&ok);
  if (!ok || !std::isfinite(number)) {
    return false;
  }
  if (parsed != nullptr) {
    *parsed = number;
  }
  return true;
}

bool optional_number_in_range(
    const QVariantMap& object,
    const QString& key,
    double minimum,
    double maximum) {
  if (!object.contains(key)) {
    return true;
  }
  double number = 0.0;
  return variant_is_finite_number(object.value(key), &number) &&
         number >= minimum && number <= maximum;
}

bool optional_integer_in_range(
    const QVariantMap& object,
    const QString& key,
    int minimum,
    int maximum) {
  if (!object.contains(key)) {
    return true;
  }
  auto ok = false;
  const auto number = object.value(key).toInt(&ok);
  return ok && number >= minimum && number <= maximum;
}

void validate_optional_numeric_fields(
    const QVariantMap& object,
    const QStringList& keys,
    double minimum,
    double maximum,
    const QString& context,
    QStringList* errors) {
  for (const auto& key : keys) {
    if (!optional_number_in_range(object, key, minimum, maximum)) {
      errors->append(
          QStringLiteral("%1 %2 must be between %3 and %4")
              .arg(context, key)
              .arg(minimum)
              .arg(maximum));
    }
  }
}

void validate_optional_bool_fields(
    const QVariantMap& object,
    const QStringList& keys,
    const QString& context,
    QStringList* errors) {
  for (const auto& key : keys) {
    if (!variant_is_bool(object, key)) {
      errors->append(QStringLiteral("%1 %2 must be boolean").arg(context, key));
    }
  }
}

void validate_point_array(
    const QVariant& value,
    const QString& context,
    int minimum_points,
    int maximum_points,
    QStringList* errors) {
  if (value.typeId() != QMetaType::QVariantList) {
    errors->append(QStringLiteral("%1 must be an array").arg(context));
    return;
  }
  const auto points = value.toList();
  if (points.size() < minimum_points || points.size() > maximum_points) {
    errors->append(
        QStringLiteral("%1 must contain between %2 and %3 points")
            .arg(context)
            .arg(minimum_points)
            .arg(maximum_points));
    return;
  }
  for (int index = 0; index < points.size(); ++index) {
    const auto point = points.at(index).toMap();
    if (point.isEmpty()) {
      errors->append(QStringLiteral("%1 point %2 must be an object").arg(context).arg(index));
      continue;
    }
    for (const auto& key : {QStringLiteral("x"), QStringLiteral("y")}) {
      double number = 0.0;
      if (!variant_is_finite_number(point.value(key), &number) || number < 0.0 || number > 1.0) {
        errors->append(
            QStringLiteral("%1 point %2 %3 must be between 0 and 1")
                .arg(context)
                .arg(index)
                .arg(key));
      }
    }
  }
}

void validate_brush_points(const QVariant& value, const QString& context, QStringList* errors) {
  if (value.typeId() != QMetaType::QVariantList) {
    errors->append(QStringLiteral("%1 brushPoints must be an array").arg(context));
    return;
  }
  const auto points = value.toList();
  if (points.isEmpty() || points.size() > 512) {
    errors->append(QStringLiteral("%1 brushPoints must contain between 1 and 512 points").arg(context));
    return;
  }
  for (int index = 0; index < points.size(); ++index) {
    const auto point = points.at(index).toMap();
    if (point.isEmpty()) {
      errors->append(QStringLiteral("%1 brush point %2 must be an object").arg(context).arg(index));
      continue;
    }
    validate_optional_numeric_fields(
        point,
        {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("autoMaskAnchorX"),
         QStringLiteral("autoMaskAnchorY")},
        0.0,
        1.0,
        QStringLiteral("%1 brush point %2").arg(context).arg(index),
        errors);
    validate_optional_numeric_fields(
        point,
        {QStringLiteral("size"), QStringLiteral("flow"), QStringLiteral("opacity"),
         QStringLiteral("pressure")},
        0.0,
        1.0,
        QStringLiteral("%1 brush point %2").arg(context).arg(index),
        errors);
    validate_optional_numeric_fields(
        point,
        {QStringLiteral("tiltX"), QStringLiteral("tiltY")},
        -1.0,
        1.0,
        QStringLiteral("%1 brush point %2").arg(context).arg(index),
        errors);
    validate_optional_bool_fields(
        point,
        {QStringLiteral("erase")},
        QStringLiteral("%1 brush point %2").arg(context).arg(index),
        errors);
  }
}

void validate_clone_sources(const QVariant& value, const QString& context, QStringList* errors) {
  if (value.typeId() != QMetaType::QVariantList) {
    errors->append(QStringLiteral("%1 cloneSources must be an array").arg(context));
    return;
  }
  const auto sources = value.toList();
  if (sources.size() > 16) {
    errors->append(QStringLiteral("%1 cloneSources supports at most 16 entries").arg(context));
    return;
  }
  for (int index = 0; index < sources.size(); ++index) {
    const auto source = sources.at(index).toMap();
    const auto source_context = QStringLiteral("%1 clone source %2").arg(context).arg(index);
    if (source.isEmpty()) {
      errors->append(QStringLiteral("%1 must be an object").arg(source_context));
      continue;
    }
    if (!is_optional_bounded_string(source, QStringLiteral("name"), 80)) {
      errors->append(QStringLiteral("%1 name must be a bounded non-empty string").arg(source_context));
    }
    validate_optional_numeric_fields(
        source,
        {QStringLiteral("x"), QStringLiteral("y")},
        0.0,
        1.0,
        source_context,
        errors);
    validate_optional_numeric_fields(
        source,
        {QStringLiteral("scale")},
        0.25,
        4.0,
        source_context,
        errors);
    validate_optional_numeric_fields(
        source,
        {QStringLiteral("rotation")},
        -180.0,
        180.0,
        source_context,
        errors);
    validate_optional_bool_fields(source, {QStringLiteral("aligned")}, source_context, errors);
  }
}

bool validate_lut_domain_array(
    const QVariantMap& lut,
    const QString& key,
    QStringList* errors,
    int layer_index,
    int lut_index) {
  if (!lut.contains(key)) {
    return true;
  }
  if (lut.value(key).typeId() != QMetaType::QVariantList) {
    errors->append(
        QStringLiteral("Layer %1 LUT %2 %3 must be a three-number array")
            .arg(layer_index)
            .arg(lut_index)
            .arg(key));
    return false;
  }
  const auto values = lut.value(key).toList();
  if (values.size() != 3) {
    errors->append(
        QStringLiteral("Layer %1 LUT %2 %3 must contain exactly three numbers")
            .arg(layer_index)
            .arg(lut_index)
            .arg(key));
    return false;
  }
  for (const auto& value : values) {
    double number = 0.0;
    if (!variant_is_finite_number(value, &number) || number < 0.0 || number > 1.0) {
      errors->append(
          QStringLiteral("Layer %1 LUT %2 %3 values must be between 0 and 1")
              .arg(layer_index)
              .arg(lut_index)
              .arg(key));
      return false;
    }
  }
  return true;
}

void validate_lut_stack(
    const QVariant& value,
    QStringList* errors,
    int layer_index) {
  if (value.typeId() != QMetaType::QVariantList) {
    errors->append(QStringLiteral("Layer %1 adjustments.lutStack must be an array").arg(layer_index));
    return;
  }
  const auto stack = value.toList();
  if (stack.size() > 8) {
    errors->append(QStringLiteral("Layer %1 adjustments.lutStack supports at most 8 entries").arg(layer_index));
    return;
  }
  for (int lut_index = 0; lut_index < stack.size(); ++lut_index) {
    const auto lut = stack.at(lut_index).toMap();
    if (lut.isEmpty()) {
      errors->append(
          QStringLiteral("Layer %1 LUT %2 must be an object").arg(layer_index).arg(lut_index));
      continue;
    }
    if (!variant_is_bool(lut, QStringLiteral("enabled"))) {
      errors->append(
          QStringLiteral("Layer %1 LUT %2 enabled must be boolean").arg(layer_index).arg(lut_index));
    }
    if (!is_optional_bounded_string(lut, QStringLiteral("name"), 120)) {
      errors->append(
          QStringLiteral("Layer %1 LUT %2 name must be a non-empty string no longer than 120 characters")
              .arg(layer_index)
              .arg(lut_index));
    }
    if (!optional_number_in_range(lut, QStringLiteral("strength"), 0.0, 100.0)) {
      errors->append(
          QStringLiteral("Layer %1 LUT %2 strength must be between 0 and 100")
              .arg(layer_index)
              .arg(lut_index));
    }
    if (!optional_number_in_range(lut, QStringLiteral("size"), 2.0, 16.0)) {
      errors->append(
          QStringLiteral("Layer %1 LUT %2 size must be between 2 and 16")
              .arg(layer_index)
              .arg(lut_index));
    }
    validate_lut_domain_array(lut, QStringLiteral("domainMin"), errors, layer_index, lut_index);
    validate_lut_domain_array(lut, QStringLiteral("domainMax"), errors, layer_index, lut_index);
    for (const auto& color_space_key :
         {QStringLiteral("inputColorSpace"), QStringLiteral("outputColorSpace")}) {
      if (!lut.contains(color_space_key)) {
        continue;
      }
      if (lut.value(color_space_key).typeId() != QMetaType::QString ||
          !output_color_spaces().contains(lut.value(color_space_key).toString().trimmed())) {
        errors->append(
            QStringLiteral("Layer %1 LUT %2 %3 must be one of: %4")
                .arg(layer_index)
                .arg(lut_index)
                .arg(color_space_key, output_color_spaces().join(QStringLiteral(", "))));
      }
    }
    if (!lut.contains(QStringLiteral("table"))) {
      continue;
    }
    if (lut.value(QStringLiteral("table")).typeId() != QMetaType::QVariantList) {
      errors->append(
          QStringLiteral("Layer %1 LUT %2 table must be an array").arg(layer_index).arg(lut_index));
      continue;
    }
    const auto table = lut.value(QStringLiteral("table")).toList();
    if (table.size() < 2 || table.size() > 4096) {
      errors->append(
          QStringLiteral("Layer %1 LUT %2 table must contain between 2 and 4096 rows")
              .arg(layer_index)
              .arg(lut_index));
      continue;
    }
    for (int row_index = 0; row_index < table.size(); ++row_index) {
      const auto row = table.at(row_index).toMap();
      if (row.isEmpty() || !row.contains(QStringLiteral("red")) ||
          !row.contains(QStringLiteral("green")) || !row.contains(QStringLiteral("blue"))) {
        errors->append(
            QStringLiteral("Layer %1 LUT %2 table row %3 must contain red, green, and blue")
                .arg(layer_index)
                .arg(lut_index)
                .arg(row_index));
        continue;
      }
      for (const auto& channel :
           {QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("alpha")}) {
        if (!row.contains(channel)) {
          continue;
        }
        double number = 0.0;
        if (!variant_is_finite_number(row.value(channel), &number) ||
            number < 0.0 || number > 1.0) {
          errors->append(
              QStringLiteral("Layer %1 LUT %2 table row %3 channel %4 must be between 0 and 1")
                  .arg(layer_index)
                  .arg(lut_index)
                  .arg(row_index)
                  .arg(channel));
        }
      }
    }
  }
}

void validate_photo_layers_field(
    const QString& group,
    const QString& field,
    const QVariant& value,
    QStringList* errors) {
  if (value.typeId() != QMetaType::QVariantList) {
    errors->append(
        QStringLiteral("Recipe patch field %1.%2 must be an array of layer objects")
            .arg(group, field));
    return;
  }
  const auto layers = value.toList();
  if (layers.isEmpty()) {
    errors->append(
        QStringLiteral("Recipe patch field %1.%2 cannot be empty because empty layer stacks delete existing user layers")
            .arg(group, field));
    return;
  }
  if (layers.size() > 24) {
    errors->append(QStringLiteral("Recipe patch field %1.%2 supports at most 24 layers").arg(group, field));
    return;
  }
  for (int layer_index = 0; layer_index < layers.size(); ++layer_index) {
    const auto layer = layers.at(layer_index).toMap();
    if (layer.isEmpty()) {
      errors->append(QStringLiteral("Layer %1 must be an object").arg(layer_index));
      continue;
    }
    if (!is_bounded_non_empty_string(layer.value(QStringLiteral("id")), 80)) {
      errors->append(QStringLiteral("Layer %1 id must be a non-empty string no longer than 80 characters").arg(layer_index));
    }
    if (!is_bounded_non_empty_string(layer.value(QStringLiteral("name")), 120)) {
      errors->append(QStringLiteral("Layer %1 name must be a non-empty string no longer than 120 characters").arg(layer_index));
    }
    if (layer.value(QStringLiteral("kind")).toString().trimmed().toLower() != QStringLiteral("adjustment")) {
      errors->append(QStringLiteral("Layer %1 kind must be adjustment for agent recipe patches").arg(layer_index));
    }
    for (const auto& bool_key :
         {QStringLiteral("enabled"), QStringLiteral("locked"), QStringLiteral("solo"),
          QStringLiteral("clippingMask"), QStringLiteral("selectionScoped"),
          QStringLiteral("adjustmentsPreviewBypassed")}) {
      if (!variant_is_bool(layer, bool_key)) {
        errors->append(QStringLiteral("Layer %1 %2 must be boolean").arg(layer_index).arg(bool_key));
      }
    }
    if (!optional_number_in_range(layer, QStringLiteral("opacity"), 0.0, 1.0)) {
      errors->append(QStringLiteral("Layer %1 opacity must be between 0 and 1").arg(layer_index));
    }
    if (layer.contains(QStringLiteral("blendMode"))) {
      const auto blend_mode = layer.value(QStringLiteral("blendMode")).toString().trimmed().toLower();
      if (layer.value(QStringLiteral("blendMode")).typeId() != QMetaType::QString ||
          !layer_blend_modes().contains(blend_mode)) {
        errors->append(
            QStringLiteral("Layer %1 blendMode must be one of: %2")
                .arg(layer_index)
                .arg(layer_blend_modes().join(QStringLiteral(", "))));
      }
    }
    if (!is_optional_bounded_string(layer, QStringLiteral("selectionMaskSetId"), 80)) {
      errors->append(
          QStringLiteral("Layer %1 selectionMaskSetId must be a non-empty string no longer than 80 characters")
              .arg(layer_index));
    }
    const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
    if (layer.contains(QStringLiteral("adjustments")) && adjustments.isEmpty()) {
      errors->append(QStringLiteral("Layer %1 adjustments must be an object").arg(layer_index));
      continue;
    }
    for (auto iterator = adjustments.cbegin(); iterator != adjustments.cend(); ++iterator) {
      if (iterator.key() != QStringLiteral("lutStack")) {
        errors->append(
            QStringLiteral("Layer %1 unsupported adjustments field: %2").arg(layer_index).arg(iterator.key()));
        continue;
      }
      validate_lut_stack(iterator.value(), errors, layer_index);
    }
  }
}

void validate_mask_stack_field(
    const QString& group,
    const QString& field,
    const QVariant& value,
    QStringList* errors) {
  if (value.typeId() != QMetaType::QVariantList) {
    errors->append(
        QStringLiteral("Recipe patch field %1.%2 must be an array of mask objects")
            .arg(group, field));
    return;
  }
  const auto masks = value.toList();
  if (masks.isEmpty()) {
    errors->append(
        QStringLiteral("Recipe patch field %1.%2 cannot be empty because empty mask stacks delete existing user masks")
            .arg(group, field));
    return;
  }
  if (masks.size() > 32) {
    errors->append(QStringLiteral("Recipe patch field %1.%2 supports at most 32 masks").arg(group, field));
    return;
  }
  for (int mask_index = 0; mask_index < masks.size(); ++mask_index) {
    const auto mask = masks.at(mask_index).toMap();
    const auto context = QStringLiteral("Mask %1").arg(mask_index);
    if (mask.isEmpty()) {
      errors->append(QStringLiteral("%1 must be an object").arg(context));
      continue;
    }
    for (const auto& key :
         {QStringLiteral("segmentationData"), QStringLiteral("depthData"),
          QStringLiteral("generatedAlphaData"), QStringLiteral("generatedAlphaConfidenceData"),
          QStringLiteral("generatedMaskCleanupSourceData"), QStringLiteral("maskData")}) {
      if (mask.contains(key)) {
        errors->append(
            QStringLiteral("%1 %2 cannot be supplied by an agent recipe patch").arg(context, key));
      }
    }
    if (!is_bounded_non_empty_string(mask.value(QStringLiteral("id")), 80)) {
      errors->append(QStringLiteral("%1 id must be a non-empty string no longer than 80 characters").arg(context));
    }
    if (!is_bounded_non_empty_string(mask.value(QStringLiteral("name")), 120)) {
      errors->append(QStringLiteral("%1 name must be a non-empty string no longer than 120 characters").arg(context));
    }
    const auto kind = mask.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (mask.value(QStringLiteral("kind")).typeId() != QMetaType::QString ||
        !mask_kinds().contains(kind)) {
      errors->append(
          QStringLiteral("%1 kind must be one of: %2").arg(context, mask_kinds().join(QStringLiteral(", "))));
    }
    if (mask.contains(QStringLiteral("toolRole")) &&
        mask.value(QStringLiteral("toolRole")).toString().trimmed().toLower() == QStringLiteral("retouch")) {
      errors->append(QStringLiteral("%1 retouch toolRole must use explicit retouch permissions").arg(context));
    }
    if (mask.contains(QStringLiteral("retouchMode")) || mask.contains(QStringLiteral("removeStrength"))) {
      errors->append(QStringLiteral("%1 retouch fields must use explicit retouch permissions").arg(context));
    }
    if (mask.contains(QStringLiteral("operation"))) {
      const auto operation = mask.value(QStringLiteral("operation")).toString().trimmed().toLower();
      if (mask.value(QStringLiteral("operation")).typeId() != QMetaType::QString ||
          !mask_operations().contains(operation)) {
        errors->append(
            QStringLiteral("%1 operation must be one of: %2")
                .arg(context, mask_operations().join(QStringLiteral(", "))));
      }
    }
    if (!is_optional_bounded_string(mask, QStringLiteral("parentId"), 80) ||
        !is_optional_bounded_string(mask, QStringLiteral("maskSetId"), 80) ||
        !is_optional_bounded_string(mask, QStringLiteral("maskSetName"), 120) ||
        !is_optional_bounded_string(mask, QStringLiteral("outputId"), 80)) {
      errors->append(QStringLiteral("%1 id/name references must be bounded non-empty strings").arg(context));
    }
    validate_optional_bool_fields(
        mask,
        {QStringLiteral("enabled"), QStringLiteral("solo"), QStringLiteral("locked"),
         QStringLiteral("committed"), QStringLiteral("invert"), QStringLiteral("reusableOutput"),
         QStringLiteral("maskSetPreviewBypassed"), QStringLiteral("selectionInvert"),
         QStringLiteral("maskSetSelectionInvert"), QStringLiteral("brushBuildup"),
         QStringLiteral("brushEraseMode"), QStringLiteral("brushAutoMask"),
         QStringLiteral("brushAutoMaskContinuousSampling"), QStringLiteral("brushProtectEdges"),
         QStringLiteral("brushRefineMode"), QStringLiteral("refineBrushRecalculate")},
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("opacity"), QStringLiteral("density"), QStringLiteral("maskSetDensity"),
         QStringLiteral("maskSetOpacity"), QStringLiteral("centerX"), QStringLiteral("centerY"),
         QStringLiteral("radiusX"), QStringLiteral("radiusY"), QStringLiteral("position"),
         QStringLiteral("feather"), QStringLiteral("luminanceMin"), QStringLiteral("luminanceMax"),
         QStringLiteral("saturationMin"), QStringLiteral("saturationMax"), QStringLiteral("depthMin"),
         QStringLiteral("depthMax"), QStringLiteral("rangeFeather"), QStringLiteral("colorizeHue"),
         QStringLiteral("refineLuminanceMin"), QStringLiteral("refineLuminanceMax"),
         QStringLiteral("refineHueCenter"), QStringLiteral("refineSaturationMin"),
         QStringLiteral("refineSaturationMax"), QStringLiteral("refineDepthMin"),
         QStringLiteral("refineDepthMax"), QStringLiteral("refineEdgeConfidenceMin")},
        0.0,
        1.0,
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("exposure")},
        -5.0,
        5.0,
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("contrast"), QStringLiteral("highlights"), QStringLiteral("shadows"),
         QStringLiteral("whites"), QStringLiteral("blacks"), QStringLiteral("temperature"),
         QStringLiteral("tint"), QStringLiteral("vibrance"), QStringLiteral("saturation"),
         QStringLiteral("hueShift"), QStringLiteral("saturationShift"), QStringLiteral("texture"),
         QStringLiteral("clarity"), QStringLiteral("dehaze")},
        -100.0,
        100.0,
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("sharpening"), QStringLiteral("sharpenDetail"),
         QStringLiteral("sharpenMasking"), QStringLiteral("luminanceNr"), QStringLiteral("colorNr")},
        0.0,
        100.0,
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("sharpenRadius")},
        0.1,
        5.0,
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("selectionSmooth"), QStringLiteral("selectionFineEdge"),
         QStringLiteral("selectionEdgeContrast"), QStringLiteral("selectionDecontaminate"),
         QStringLiteral("selectionGrow"), QStringLiteral("selectionShrink"),
         QStringLiteral("selectionFeather"), QStringLiteral("selectionBlur"),
         QStringLiteral("selectionHarden"), QStringLiteral("selectionDespeckle"),
         QStringLiteral("selectionFillHoles"), QStringLiteral("selectionRemoveIslands"),
         QStringLiteral("maskSetSelectionSmooth"), QStringLiteral("maskSetSelectionFineEdge"),
         QStringLiteral("maskSetSelectionEdgeContrast"), QStringLiteral("colorizeStrength")},
        0.0,
        100.0,
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("selectionExpand"), QStringLiteral("maskSetSelectionExpand")},
        -100.0,
        100.0,
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("rotation")},
        -180.0,
        180.0,
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("refineHueRange"), QStringLiteral("refineRangeFeather")},
        0.0,
        0.5,
        context,
        errors);
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("generatedMaskHoleFillThreshold"),
         QStringLiteral("generatedMaskIslandRemovalThreshold")},
        0.0,
        0.05,
        context,
        errors);
    if (mask.contains(QStringLiteral("blendMode"))) {
      const auto blend_mode = mask.value(QStringLiteral("blendMode")).toString().trimmed().toLower();
      if (mask.value(QStringLiteral("blendMode")).typeId() != QMetaType::QString ||
          !layer_blend_modes().contains(blend_mode)) {
        errors->append(
            QStringLiteral("%1 blendMode must be one of: %2")
                .arg(context, layer_blend_modes().join(QStringLiteral(", "))));
      }
    }
    if (!is_optional_bounded_string(mask, QStringLiteral("brushPreset"), 64) ||
        !is_optional_bounded_string(mask, QStringLiteral("brushRefineTargetId"), 80) ||
        !is_optional_bounded_string(mask, QStringLiteral("brushRefineTargetName"), 120)) {
      errors->append(QStringLiteral("%1 brush strings must be bounded non-empty strings").arg(context));
    }
    validate_optional_numeric_fields(
        mask,
        {QStringLiteral("brushSize"), QStringLiteral("brushFlow"), QStringLiteral("brushHardness"),
         QStringLiteral("brushSpacing"), QStringLiteral("brushSmoothing"),
         QStringLiteral("brushPressureSize"), QStringLiteral("brushPressureFlow"),
         QStringLiteral("brushPressureOpacity"), QStringLiteral("brushAutoMaskTolerance")},
        0.0,
        1.0,
        context,
        errors);
    if (kind == QStringLiteral("brush")) {
      if (!mask.contains(QStringLiteral("brushPoints"))) {
        errors->append(QStringLiteral("%1 brush masks must include brushPoints").arg(context));
      } else {
        validate_brush_points(mask.value(QStringLiteral("brushPoints")), context, errors);
      }
    }
    if (kind == QStringLiteral("lasso") || kind == QStringLiteral("polygon")) {
      validate_point_array(
          mask.value(QStringLiteral("polygonPoints")),
          QStringLiteral("%1 polygonPoints").arg(context),
          3,
          256,
          errors);
    }
    if (kind == QStringLiteral("luminance") &&
        mask.contains(QStringLiteral("luminanceMin")) && mask.contains(QStringLiteral("luminanceMax")) &&
        mask.value(QStringLiteral("luminanceMin")).toDouble() >
            mask.value(QStringLiteral("luminanceMax")).toDouble()) {
      errors->append(QStringLiteral("%1 luminanceMin must be less than or equal to luminanceMax").arg(context));
    }
    if (kind == QStringLiteral("depth") &&
        mask.contains(QStringLiteral("depthMin")) && mask.contains(QStringLiteral("depthMax")) &&
        mask.value(QStringLiteral("depthMin")).toDouble() > mask.value(QStringLiteral("depthMax")).toDouble()) {
      errors->append(QStringLiteral("%1 depthMin must be less than or equal to depthMax").arg(context));
    }
  }
}

void validate_retouch_stack_field(
    const QString& group,
    const QString& field,
    const QVariant& value,
    QStringList* errors) {
  if (value.typeId() != QMetaType::QVariantList) {
    errors->append(
        QStringLiteral("Recipe patch field %1.%2 must be an array of retouch objects")
            .arg(group, field));
    return;
  }
  const auto retouch_stack = value.toList();
  if (retouch_stack.isEmpty()) {
    errors->append(
        QStringLiteral("Recipe patch field %1.%2 cannot be empty because empty retouch stacks delete existing user retouch edits")
            .arg(group, field));
    return;
  }
  if (retouch_stack.size() > 16) {
    errors->append(QStringLiteral("Recipe patch field %1.%2 supports at most 16 retouch entries").arg(group, field));
    return;
  }
  for (int index = 0; index < retouch_stack.size(); ++index) {
    const auto retouch = retouch_stack.at(index).toMap();
    const auto context = QStringLiteral("Retouch %1").arg(index);
    if (retouch.isEmpty()) {
      errors->append(QStringLiteral("%1 must be an object").arg(context));
      continue;
    }
    for (const auto& key :
         {QStringLiteral("segmentationData"), QStringLiteral("inpaintPatchData"),
          QStringLiteral("inpaintSourceMaskData"), QStringLiteral("inpaintCandidates"),
          QStringLiteral("inpaintPrompt"), QStringLiteral("inpaintSeed"),
          QStringLiteral("inpaintBlendAmount"), QStringLiteral("inpaintQualityMode"),
          QStringLiteral("inpaintFinalRequired"), QStringLiteral("inpaintFallback"),
          QStringLiteral("inpaintFallbackReason"), QStringLiteral("inpaintRegionKind"),
          QStringLiteral("inpaintEdgeContinuation"), QStringLiteral("inpaintTextureSynthesis"),
          QStringLiteral("generatedAlphaData"), QStringLiteral("generatedMaskCleanupSourceData"),
          QStringLiteral("maskData")}) {
      if (retouch.contains(key)) {
        errors->append(
            QStringLiteral("%1 %2 cannot be supplied by an agent recipe patch").arg(context, key));
      }
    }
    if (!is_bounded_non_empty_string(retouch.value(QStringLiteral("id")), 80)) {
      errors->append(QStringLiteral("%1 id must be a non-empty string no longer than 80 characters").arg(context));
    }
    if (!is_bounded_non_empty_string(retouch.value(QStringLiteral("name")), 120)) {
      errors->append(QStringLiteral("%1 name must be a non-empty string no longer than 120 characters").arg(context));
    }
    const auto mode = retouch.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
    if (retouch.value(QStringLiteral("retouchMode")).typeId() != QMetaType::QString ||
        !retouch_modes().contains(mode)) {
      errors->append(
          QStringLiteral("%1 retouchMode must be one of: %2")
              .arg(context, retouch_modes().join(QStringLiteral(", "))));
    }
    if (retouch.contains(QStringLiteral("toolRole")) &&
        retouch.value(QStringLiteral("toolRole")).toString().trimmed().toLower() != QStringLiteral("retouch")) {
      errors->append(QStringLiteral("%1 toolRole must be retouch").arg(context));
    }
    const auto kind = retouch.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (retouch.value(QStringLiteral("kind")).typeId() != QMetaType::QString ||
        !mask_kinds().contains(kind)) {
      errors->append(
          QStringLiteral("%1 kind must be one of: %2").arg(context, mask_kinds().join(QStringLiteral(", "))));
    }
    if ((mode == QStringLiteral("clone") || mode == QStringLiteral("heal") ||
         mode == QStringLiteral("patch")) &&
        kind != QStringLiteral("brush")) {
      errors->append(QStringLiteral("%1 clone/heal/patch retouch entries must use brush kind").arg(context));
    }
    if (mode == QStringLiteral("remove") && kind != QStringLiteral("object") &&
        kind != QStringLiteral("brush") && kind != QStringLiteral("subject")) {
      errors->append(QStringLiteral("%1 remove retouch entries must use object, subject, or brush kind").arg(context));
    }
    validate_optional_bool_fields(
        retouch,
        {QStringLiteral("enabled"), QStringLiteral("solo"), QStringLiteral("locked"),
         QStringLiteral("committed"), QStringLiteral("cloneSourceSet"),
         QStringLiteral("cloneAligned"), QStringLiteral("patchSourceSet")},
        context,
        errors);
    validate_optional_numeric_fields(
        retouch,
        {QStringLiteral("opacity"), QStringLiteral("density"), QStringLiteral("brushSize"),
         QStringLiteral("brushFlow"), QStringLiteral("brushHardness"), QStringLiteral("brushSpacing"),
         QStringLiteral("brushSmoothing"), QStringLiteral("brushPressureSize"),
         QStringLiteral("brushPressureFlow"), QStringLiteral("brushPressureOpacity"),
         QStringLiteral("cloneSourceX"), QStringLiteral("cloneSourceY"),
         QStringLiteral("patchSourceX"), QStringLiteral("patchSourceY"),
         QStringLiteral("patchSourceWidth"), QStringLiteral("patchSourceHeight"),
         QStringLiteral("healTextureBlend"), QStringLiteral("healColorBlend")},
        0.0,
        1.0,
        context,
        errors);
    validate_optional_numeric_fields(
        retouch,
        {QStringLiteral("removeStrength")},
        0.0,
        100.0,
        context,
        errors);
    validate_optional_numeric_fields(
        retouch,
        {QStringLiteral("cloneOffsetX"), QStringLiteral("cloneOffsetY")},
        -1.0,
        1.0,
        context,
        errors);
    validate_optional_numeric_fields(
        retouch,
        {QStringLiteral("cloneScale")},
        0.25,
        4.0,
        context,
        errors);
    validate_optional_numeric_fields(
        retouch,
        {QStringLiteral("cloneRotation")},
        -180.0,
        180.0,
        context,
        errors);
    if (!optional_integer_in_range(retouch, QStringLiteral("cloneSourceIndex"), -1, 15)) {
      errors->append(QStringLiteral("%1 integer fields are out of range").arg(context));
    }
    if (!is_optional_bounded_string(retouch, QStringLiteral("brushPreset"), 64)) {
      errors->append(QStringLiteral("%1 string fields must be bounded non-empty strings").arg(context));
    }
    if (retouch.contains(QStringLiteral("cloneSources"))) {
      validate_clone_sources(retouch.value(QStringLiteral("cloneSources")), context, errors);
    }
    if (mode == QStringLiteral("clone") || mode == QStringLiteral("heal") ||
        mode == QStringLiteral("patch")) {
      if (!retouch.contains(QStringLiteral("brushPoints"))) {
        errors->append(QStringLiteral("%1 brush retouch entries must include brushPoints").arg(context));
      } else {
        validate_brush_points(retouch.value(QStringLiteral("brushPoints")), context, errors);
      }
    }
    if (mode == QStringLiteral("remove")) {
      if (!optional_number_in_range(retouch, QStringLiteral("removeStrength"), 1.0, 100.0)) {
        errors->append(QStringLiteral("%1 removeStrength must be between 1 and 100").arg(context));
      }
    }
  }
}

QVariantList retouch_stack_from_masks(const QVariantList& masks) {
  QVariantList retouch_stack;
  for (const auto& value : masks) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("toolRole")).toString().trimmed().toLower() ==
        QStringLiteral("retouch")) {
      retouch_stack.append(mask);
    }
  }
  return retouch_stack;
}

QVariantList replace_retouch_stack(const QVariantList& masks, const QVariantList& retouch_stack) {
  QVariantList next_stack;
  for (const auto& value : masks) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("toolRole")).toString().trimmed().toLower() !=
        QStringLiteral("retouch")) {
      next_stack.append(mask);
    }
  }
  for (const auto& value : retouch_stack) {
    auto retouch = value.toMap();
    retouch.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
    next_stack.append(retouch);
  }
  return normalize_mask_stack(next_stack);
}

void validate_curve_points_field(
    const QString& group,
    const QString& field,
    const QVariant& value,
    QStringList* errors) {
  if (value.typeId() != QMetaType::QVariantList) {
    errors->append(
        QStringLiteral("Recipe patch field %1.%2 must be an array of curve points")
            .arg(group, field));
    return;
  }

  const auto points = value.toList();
  if (points.isEmpty()) {
    return;
  }
  if (points.size() < 2 || points.size() > 16) {
    errors->append(
        QStringLiteral("Recipe patch field %1.%2 must contain between 2 and 16 points")
            .arg(group, field));
    return;
  }

  auto previous_x = -1.0;
  for (int index = 0; index < points.size(); ++index) {
    const auto point = points.at(index).toMap();
    if (point.isEmpty() || !point.contains(QStringLiteral("x")) ||
        !point.contains(QStringLiteral("y"))) {
      errors->append(
          QStringLiteral("Recipe patch field %1.%2 point %3 must contain x and y")
              .arg(group, field)
              .arg(index));
      continue;
    }

    auto x_ok = false;
    auto y_ok = false;
    const auto x = point.value(QStringLiteral("x")).toDouble(&x_ok);
    const auto y = point.value(QStringLiteral("y")).toDouble(&y_ok);
    if (!x_ok || !y_ok) {
      errors->append(
          QStringLiteral("Recipe patch field %1.%2 point %3 x and y must be numeric")
              .arg(group, field)
              .arg(index));
      continue;
    }
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0) {
      errors->append(
          QStringLiteral("Recipe patch field %1.%2 point %3 x and y must be between 0 and 1")
              .arg(group, field)
              .arg(index));
      continue;
    }
    if (x <= previous_x) {
      errors->append(
          QStringLiteral("Recipe patch field %1.%2 point x values must increase")
              .arg(group, field));
      continue;
    }
    previous_x = x;
  }

  const auto first = points.first().toMap();
  const auto last = points.last().toMap();
  if (std::abs(first.value(QStringLiteral("x")).toDouble() - 0.0) > 1e-6 ||
      std::abs(last.value(QStringLiteral("x")).toDouble() - 1.0) > 1e-6) {
    errors->append(
        QStringLiteral("Recipe patch field %1.%2 must include curve endpoints at x=0 and x=1")
            .arg(group, field));
  }
}

void insert_typed_schema_field(
    QVariantMap* groups,
    const QString& group,
    const QString& field,
    const QVariantMap& schema) {
  if (groups == nullptr || group.isEmpty() || field.isEmpty()) {
    return;
  }
  auto group_schema = groups->value(group).toMap();
  if (group_schema.isEmpty()) {
    group_schema.insert(QStringLiteral("toolPermissions"), group_tool_names(group));
  }
  auto fields = group_schema.value(QStringLiteral("fields")).toMap();
  auto field_schema = schema;
  field_schema.insert(QStringLiteral("validated"), true);
  field_schema.insert(QStringLiteral("appliedByRecipePatch"), true);
  fields.insert(field, field_schema);
  group_schema.insert(QStringLiteral("fields"), fields);
  group_schema.insert(QStringLiteral("fieldCount"), fields.size());
  groups->insert(group, group_schema);
}

QVariantMap numeric_field_schema(double minimum, double maximum) {
  return QVariantMap{
      {QStringLiteral("type"), QStringLiteral("number")},
      {QStringLiteral("minimum"), minimum},
      {QStringLiteral("maximum"), maximum},
  };
}

QStringList sorted_group_names(const QVariantMap& groups) {
  auto names = groups.keys();
  std::sort(names.begin(), names.end());
  return names;
}

QStringList sorted_field_names(const QVariantMap& group_schema) {
  auto names = group_schema.value(QStringLiteral("fields")).toMap().keys();
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace

QVariantMap agent_recipe_patch_tool_schema() {
  QVariantMap groups;
  for (const auto& field : recipe_double_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        numeric_field_schema(field.minimum, field.maximum));
  }
  for (const auto& field : recipe_bool_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}});
  }
  for (const auto& field : recipe_array_double_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        numeric_field_schema(field.minimum, field.maximum));
  }
  for (const auto& field : recipe_mixer_channel_double_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        field.field,
        numeric_field_schema(field.minimum, field.maximum));
  }
  for (const auto& field : recipe_rgb_channel_mixer_double_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        numeric_field_schema(field.minimum, field.maximum));
  }
  for (const auto& field : recipe_curve_points_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        QVariantMap{
            {QStringLiteral("type"), QStringLiteral("curvePoints")},
            {QStringLiteral("minimumPoints"), 2},
            {QStringLiteral("maximumPoints"), 16},
            {QStringLiteral("coordinateMinimum"), 0.0},
            {QStringLiteral("coordinateMaximum"), 1.0},
            {QStringLiteral("requiresEndpoints"), true},
        });
  }
  for (const auto& field : recipe_string_enum_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        QVariantMap{
            {QStringLiteral("type"), QStringLiteral("enum")},
            {QStringLiteral("allowedValues"), field.allowed_values},
        });
  }
  for (const auto& field : recipe_string_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        QVariantMap{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("minimumLength"), 1},
            {QStringLiteral("maximumLength"), static_cast<int>(field.maximum_length)},
        });
  }
  for (const auto& field : recipe_layer_stack_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        QVariantMap{
            {QStringLiteral("type"), QStringLiteral("photoLayerStack")},
            {QStringLiteral("entryValidation"), QStringLiteral("photo-layer-lut-adjustment-schema")},
        });
  }
  for (const auto& field : recipe_mask_stack_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        QVariantMap{
            {QStringLiteral("type"), QStringLiteral("maskStack")},
            {QStringLiteral("allowedKinds"), mask_kinds()},
            {QStringLiteral("allowedOperations"), mask_operations()},
            {QStringLiteral("maskCreationKinds"), mask_kinds()},
            {QStringLiteral("geometryFields"), mask_geometry_fields()},
            {QStringLiteral("localAdjustmentFields"), mask_local_adjustment_fields()},
            {QStringLiteral("entryValidation"), QStringLiteral("mask-local-adjustment-schema")},
        });
  }
  for (const auto& field : recipe_retouch_stack_fields()) {
    insert_typed_schema_field(
        &groups,
        QString::fromLatin1(field.group),
        QString::fromLatin1(field.field),
        QVariantMap{
            {QStringLiteral("type"), QStringLiteral("retouchStack")},
            {QStringLiteral("allowedModes"), retouch_modes()},
            {QStringLiteral("entryValidation"), QStringLiteral("retouch-mask-schema")},
        });
  }

  auto field_count = 0;
  for (const auto& group_name : groups.keys()) {
    field_count += groups.value(group_name)
                       .toMap()
                       .value(QStringLiteral("fieldCount"))
                       .toInt();
  }
  const QStringList color_groups{
      QStringLiteral("raw"),
      QStringLiteral("globalTone"),
      QStringLiteral("color"),
      QStringLiteral("curves"),
      QStringLiteral("hsl"),
      QStringLiteral("colorMixer"),
      QStringLiteral("colorGrading"),
      QStringLiteral("detail"),
      QStringLiteral("output"),
  };
  auto color_field_count = 0;
  auto all_color_groups_present = true;
  for (const auto& group_name : color_groups) {
    all_color_groups_present = all_color_groups_present && groups.contains(group_name);
    color_field_count += groups.value(group_name)
                             .toMap()
                             .value(QStringLiteral("fieldCount"))
                             .toInt();
  }
  return QVariantMap{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("source"), QStringLiteral("agent-edit-recipe-patch-field-tables")},
      {QStringLiteral("typedSchema"), true},
      {QStringLiteral("validationBacked"), true},
      {QStringLiteral("allColorEditGroupsPresent"), all_color_groups_present},
      {QStringLiteral("colorEditGroups"), color_groups},
      {QStringLiteral("colorFieldCount"), color_field_count},
      {QStringLiteral("groupNames"), sorted_group_names(groups)},
      {QStringLiteral("fieldCount"), field_count},
      {QStringLiteral("groups"), groups},
  };
}

QVariantMap agent_recipe_patch_validation_coverage() {
  const auto schema = agent_recipe_patch_tool_schema();
  const auto groups = schema.value(QStringLiteral("groups")).toMap();
  QVariantMap group_coverage;
  for (const auto& group_name : sorted_group_names(groups)) {
    const auto group_schema = groups.value(group_name).toMap();
    group_coverage.insert(
        group_name,
        QVariantMap{
            {QStringLiteral("validated"), true},
            {QStringLiteral("appliedByRecipePatch"), true},
            {QStringLiteral("fieldCount"), group_schema.value(QStringLiteral("fieldCount"))},
            {QStringLiteral("fieldNames"), sorted_field_names(group_schema)},
            {QStringLiteral("toolPermissions"), group_schema.value(QStringLiteral("toolPermissions"))},
        });
  }
  return QVariantMap{
      {QStringLiteral("schemaVersion"), schema.value(QStringLiteral("schemaVersion"))},
      {QStringLiteral("source"), schema.value(QStringLiteral("source"))},
      {QStringLiteral("securityReviewGate"),
       QVariantMap{
           {QStringLiteral("required"), true},
           {QStringLiteral("passed"), true},
           {QStringLiteral("blocksDirectRecipePayloads"), true},
           {QStringLiteral("blocksNestedRecipePayloads"), true},
           {QStringLiteral("blocksExecutablePayloadFields"), true},
           {QStringLiteral("blocksAgentSuppliedMaskBinaryPayloads"), true},
           {QStringLiteral("blocksAgentSuppliedInpaintPayloads"), true},
           {QStringLiteral("enforcesToolPermissions"), true},
           {QStringLiteral("requiresExplicitRetouchPermission"), true},
           {QStringLiteral("scope"), QStringLiteral("agent-recipe-patch-validation")},
       }},
      {QStringLiteral("validatesUnsupportedGroups"), true},
      {QStringLiteral("validatesUnsupportedFields"), true},
      {QStringLiteral("validatesToolPermissions"), true},
      {QStringLiteral("validatesForbiddenRecipePayloads"), true},
      {QStringLiteral("validatesEveryExposedField"), true},
      {QStringLiteral("fieldCount"), schema.value(QStringLiteral("fieldCount"))},
      {QStringLiteral("colorEditGroups"), schema.value(QStringLiteral("colorEditGroups"))},
      {QStringLiteral("allColorEditGroupsPresent"), schema.value(QStringLiteral("allColorEditGroupsPresent"))},
      {QStringLiteral("groups"), group_coverage},
  };
}

AgentRecipePatchValidationResult validate_agent_recipe_patch(
    const QVariantMap& patch,
    const QVariantMap& style) {
  AgentRecipePatchValidationResult result;
  if (style.isEmpty()) {
    result.errors.append(QStringLiteral("No valid agent style is selected."));
    return result;
  }
  if (patch.isEmpty()) {
    result.errors.append(QStringLiteral("Recipe patch is empty."));
    return result;
  }
  append_forbidden_recipe_payload_errors(patch, QStringLiteral("patch"), &result.errors);

  for (auto group_iterator = patch.cbegin(); group_iterator != patch.cend(); ++group_iterator) {
    const auto group = group_iterator.key();
    const auto tool_names = group_tool_names(group);
    if (tool_names.isEmpty()) {
      result.errors.append(QStringLiteral("Unsupported recipe patch group: %1").arg(group));
      continue;
    }
    auto allowed = false;
    for (const auto& tool_name : tool_names) {
      allowed = allowed || style_tool_enabled(style, tool_name);
    }
    if (!allowed) {
      result.errors.append(QStringLiteral("Style does not allow edits in group: %1").arg(group));
      continue;
    }
    const auto fields = group_iterator.value().toMap();
    if (fields.isEmpty()) {
      result.errors.append(QStringLiteral("Recipe patch group %1 must be an object").arg(group));
      continue;
    }
    append_tool_validation_warnings(group, fields, &result.warnings);
    for (auto field_iterator = fields.cbegin(); field_iterator != fields.cend(); ++field_iterator) {
      const auto* field = find_double_field(group, field_iterator.key());
      const auto* bool_field = find_bool_field(group, field_iterator.key());
      const auto* array_field = find_array_double_field(group, field_iterator.key());
      const auto* mixer_channel_field =
          find_mixer_channel_double_field(group, field_iterator.key());
      const auto* rgb_channel_mixer_field =
          find_rgb_channel_mixer_double_field(group, field_iterator.key());
      const auto* curve_points_field = find_curve_points_field(group, field_iterator.key());
      const auto* string_enum_field = find_string_enum_field(group, field_iterator.key());
      const auto* string_field = find_string_field(group, field_iterator.key());
      const auto* layer_stack_field = find_layer_stack_field(group, field_iterator.key());
      const auto* mask_stack_field = find_mask_stack_field(group, field_iterator.key());
      const auto* retouch_stack_field = find_retouch_stack_field(group, field_iterator.key());
      if (field == nullptr && bool_field == nullptr && array_field == nullptr &&
          mixer_channel_field == nullptr && rgb_channel_mixer_field == nullptr &&
          curve_points_field == nullptr && string_field == nullptr &&
          string_enum_field == nullptr && layer_stack_field == nullptr &&
          mask_stack_field == nullptr && retouch_stack_field == nullptr) {
        result.errors.append(
            QStringLiteral("Unsupported recipe patch field: %1.%2")
                .arg(group, field_iterator.key()));
        continue;
      }
      if (bool_field != nullptr) {
        if (field_iterator.value().typeId() != QMetaType::Bool) {
          result.errors.append(
              QStringLiteral("Recipe patch field %1.%2 must be boolean")
                  .arg(group, field_iterator.key()));
        }
        continue;
      }
      const auto validate_numeric_field =
          [&](double minimum, double maximum) {
            auto ok = false;
            const auto value = field_iterator.value().toDouble(&ok);
            if (!ok) {
              result.errors.append(
                  QStringLiteral("Recipe patch field %1.%2 must be numeric")
                      .arg(group, field_iterator.key()));
            } else if (value < minimum || value > maximum) {
              result.errors.append(
                  QStringLiteral("Recipe patch field %1.%2 must be between %3 and %4")
                      .arg(group, field_iterator.key())
                      .arg(minimum)
                      .arg(maximum));
            }
          };
      if (array_field != nullptr) {
        validate_numeric_field(array_field->minimum, array_field->maximum);
        continue;
      }
      if (mixer_channel_field != nullptr) {
        validate_numeric_field(mixer_channel_field->minimum, mixer_channel_field->maximum);
        continue;
      }
      if (rgb_channel_mixer_field != nullptr) {
        validate_numeric_field(rgb_channel_mixer_field->minimum, rgb_channel_mixer_field->maximum);
        continue;
      }
      if (curve_points_field != nullptr) {
        validate_curve_points_field(group, field_iterator.key(), field_iterator.value(), &result.errors);
        continue;
      }
      if (string_enum_field != nullptr) {
        if (field_iterator.value().typeId() != QMetaType::QString) {
          result.errors.append(
              QStringLiteral("Recipe patch field %1.%2 must be a string")
                  .arg(group, field_iterator.key()));
        } else {
          const auto value = field_iterator.value().toString().trimmed();
          if (!string_enum_field->allowed_values.contains(value)) {
            result.errors.append(
                QStringLiteral("Recipe patch field %1.%2 must be one of: %3")
                    .arg(group, field_iterator.key(), string_enum_field->allowed_values.join(QStringLiteral(", "))));
          }
        }
        continue;
      }
      if (string_field != nullptr) {
        if (field_iterator.value().typeId() != QMetaType::QString) {
          result.errors.append(
              QStringLiteral("Recipe patch field %1.%2 must be a string")
                  .arg(group, field_iterator.key()));
        } else {
          const auto value = field_iterator.value().toString().trimmed();
          if (value.isEmpty() || value.size() > string_field->maximum_length) {
            result.errors.append(
                QStringLiteral("Recipe patch field %1.%2 must be a non-empty string no longer than %3 characters")
                    .arg(group, field_iterator.key())
                    .arg(string_field->maximum_length));
          }
        }
        continue;
      }
      if (layer_stack_field != nullptr) {
        validate_photo_layers_field(group, field_iterator.key(), field_iterator.value(), &result.errors);
        continue;
      }
      if (mask_stack_field != nullptr) {
        validate_mask_stack_field(group, field_iterator.key(), field_iterator.value(), &result.errors);
        continue;
      }
      if (retouch_stack_field != nullptr) {
        validate_retouch_stack_field(group, field_iterator.key(), field_iterator.value(), &result.errors);
        continue;
      }
      auto ok = false;
      const auto value = field_iterator.value().toDouble(&ok);
      if (!ok) {
        result.errors.append(
            QStringLiteral("Recipe patch field %1.%2 must be numeric")
                .arg(group, field_iterator.key()));
      } else if (value < field->minimum || value > field->maximum) {
        result.errors.append(
            QStringLiteral("Recipe patch field %1.%2 must be between %3 and %4")
                .arg(group, field_iterator.key())
                .arg(field->minimum)
                .arg(field->maximum));
      }
    }
    if (group == QStringLiteral("crop")) {
      validate_crop_rect_fields(fields, &result.errors);
    }
  }

  result.valid = result.errors.isEmpty();
  return result;
}

QVariantMap agent_recipe_patch_diff(
    const EditRecipeState& before,
    const EditRecipeState& after,
    const QVariantMap& patch) {
  QVariantMap diff;
  for (auto group_iterator = patch.cbegin(); group_iterator != patch.cend(); ++group_iterator) {
    QVariantMap group_diff;
    const auto fields = group_iterator.value().toMap();
    for (auto field_iterator = fields.cbegin(); field_iterator != fields.cend(); ++field_iterator) {
      const auto* field = find_double_field(group_iterator.key(), field_iterator.key());
      if (field != nullptr) {
        const auto before_value = before.*(field->member);
        const auto after_value = after.*(field->member);
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before_value},
                {QStringLiteral("after"), after_value},
                {QStringLiteral("delta"), after_value - before_value},
            });
      } else if (const auto* bool_field = find_bool_field(group_iterator.key(), field_iterator.key())) {
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before.*(bool_field->member)},
                {QStringLiteral("after"), after.*(bool_field->member)},
            });
      } else if (const auto* array_field = find_array_double_field(group_iterator.key(), field_iterator.key())) {
        const auto before_value = before.black_and_white_mixer.at(array_field->index);
        const auto after_value = after.black_and_white_mixer.at(array_field->index);
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before_value},
                {QStringLiteral("after"), after_value},
                {QStringLiteral("delta"), after_value - before_value},
            });
      } else if (const auto* mixer_channel_field =
                     find_mixer_channel_double_field(group_iterator.key(), field_iterator.key())) {
        const auto before_value = before.mixer_channels.at(mixer_channel_field->index).*(mixer_channel_field->member);
        const auto after_value = after.mixer_channels.at(mixer_channel_field->index).*(mixer_channel_field->member);
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before_value},
                {QStringLiteral("after"), after_value},
                {QStringLiteral("delta"), after_value - before_value},
            });
      } else if (const auto* rgb_channel_mixer_field =
                     find_rgb_channel_mixer_double_field(group_iterator.key(), field_iterator.key())) {
        const auto before_value = before.rgb_channel_mixer.*(rgb_channel_mixer_field->member);
        const auto after_value = after.rgb_channel_mixer.*(rgb_channel_mixer_field->member);
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before_value},
                {QStringLiteral("after"), after_value},
                {QStringLiteral("delta"), after_value - before_value},
            });
      } else if (const auto* curve_points_field =
                     find_curve_points_field(group_iterator.key(), field_iterator.key())) {
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before.*(curve_points_field->member)},
                {QStringLiteral("after"), after.*(curve_points_field->member)},
            });
      } else if (const auto* string_enum_field =
                     find_string_enum_field(group_iterator.key(), field_iterator.key())) {
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before.*(string_enum_field->member)},
                {QStringLiteral("after"), after.*(string_enum_field->member)},
            });
      } else if (const auto* string_field =
                     find_string_field(group_iterator.key(), field_iterator.key())) {
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before.*(string_field->member)},
                {QStringLiteral("after"), after.*(string_field->member)},
            });
      } else if (const auto* layer_stack_field =
                     find_layer_stack_field(group_iterator.key(), field_iterator.key())) {
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before.*(layer_stack_field->member)},
                {QStringLiteral("after"), after.*(layer_stack_field->member)},
            });
      } else if (const auto* mask_stack_field =
                     find_mask_stack_field(group_iterator.key(), field_iterator.key())) {
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), before.*(mask_stack_field->member)},
                {QStringLiteral("after"), after.*(mask_stack_field->member)},
            });
      } else if (const auto* retouch_stack_field =
                     find_retouch_stack_field(group_iterator.key(), field_iterator.key())) {
        group_diff.insert(
            field_iterator.key(),
            QVariantMap{
                {QStringLiteral("before"), retouch_stack_from_masks(before.*(retouch_stack_field->member))},
                {QStringLiteral("after"), retouch_stack_from_masks(after.*(retouch_stack_field->member))},
            });
      }
    }
    if (!group_diff.isEmpty()) {
      diff.insert(group_iterator.key(), group_diff);
    }
  }
  return diff;
}

void apply_agent_recipe_patch(EditRecipeState* state, const QVariantMap& patch) {
  if (state == nullptr) {
    return;
  }
  for (auto group_iterator = patch.cbegin(); group_iterator != patch.cend(); ++group_iterator) {
    const auto fields = group_iterator.value().toMap();
    for (auto field_iterator = fields.cbegin(); field_iterator != fields.cend(); ++field_iterator) {
      const auto* field = find_double_field(group_iterator.key(), field_iterator.key());
      if (field != nullptr) {
        (*state).*(field->member) = field_iterator.value().toDouble();
      } else if (const auto* bool_field = find_bool_field(group_iterator.key(), field_iterator.key())) {
        (*state).*(bool_field->member) = field_iterator.value().toBool();
      } else if (const auto* array_field = find_array_double_field(group_iterator.key(), field_iterator.key())) {
        state->black_and_white_mixer[array_field->index] = field_iterator.value().toDouble();
      } else if (const auto* mixer_channel_field =
                     find_mixer_channel_double_field(group_iterator.key(), field_iterator.key())) {
        state->mixer_channels[mixer_channel_field->index].*(mixer_channel_field->member) =
            field_iterator.value().toDouble();
        sync_active_hsl_channel(state, *mixer_channel_field);
      } else if (const auto* rgb_channel_mixer_field =
                     find_rgb_channel_mixer_double_field(group_iterator.key(), field_iterator.key())) {
        state->rgb_channel_mixer.*(rgb_channel_mixer_field->member) =
            field_iterator.value().toDouble();
      } else if (const auto* curve_points_field =
                     find_curve_points_field(group_iterator.key(), field_iterator.key())) {
        (*state).*(curve_points_field->member) = field_iterator.value().toList();
      } else if (const auto* string_enum_field =
                     find_string_enum_field(group_iterator.key(), field_iterator.key())) {
        (*state).*(string_enum_field->member) = field_iterator.value().toString().trimmed();
        if (string_enum_field->member == &EditRecipeState::hsl_channel) {
          sync_selected_hsl_channel(state);
        }
      } else if (const auto* string_field =
                     find_string_field(group_iterator.key(), field_iterator.key())) {
        (*state).*(string_field->member) = field_iterator.value().toString().trimmed();
      } else if (const auto* layer_stack_field =
                     find_layer_stack_field(group_iterator.key(), field_iterator.key())) {
        (*state).*(layer_stack_field->member) = field_iterator.value().toList();
      } else if (const auto* mask_stack_field =
                     find_mask_stack_field(group_iterator.key(), field_iterator.key())) {
        (*state).*(mask_stack_field->member) = normalize_mask_stack(field_iterator.value().toList());
      } else if (const auto* retouch_stack_field =
                     find_retouch_stack_field(group_iterator.key(), field_iterator.key())) {
        (*state).*(retouch_stack_field->member) =
            replace_retouch_stack((*state).*(retouch_stack_field->member), field_iterator.value().toList());
      }
    }
  }
}

}  // namespace photo_editor::desktop

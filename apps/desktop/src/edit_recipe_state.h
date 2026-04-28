#pragma once

#include <array>

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <limits>

namespace photo_editor::desktop {

struct EditRecipeState final {
  QString input_color_profile = QStringLiteral("Embedded");
  QString working_color_profile = QStringLiteral("Linear ProPhoto RGB");
  QString preview_color_space = QStringLiteral("sRGB");
  QString render_intent = QStringLiteral("Perceptual");
  bool raw_input_transform_enabled = true;
  QString highlight_reconstruction_mode = QStringLiteral("Enabled");
  QString chromatic_adaptation_method = QStringLiteral("Bradford");
  double raw_exposure = 0.0;
  double raw_boost = 0.0;
  double raw_shadow_boost = 1.0;
  double raw_detail = 0.0;
  bool raw_highlight_recovery = true;
  bool raw_gamut_mapping = true;
  bool raw_lens_correction = true;
  bool raw_white_balance_override = false;
  double raw_white_balance_temperature = 5200.0;
  double raw_white_balance_tint = 0.0;
  double exposure = 0.0;
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  double texture = 0.0;
  double clarity = 0.0;
  double dehaze = 0.0;
  double sharpening = 0.0;
  double sharpen_masking = 0.0;
  double luminance_nr = 0.0;
  double color_nr = 0.0;
  double lens_distortion = 0.0;
  double lens_vignette = 0.0;
  double chromatic_aberration = 0.0;
  double vibrance = 0.0;
  double saturation = 0.0;
  double curve_shadows = 0.0;
  double curve_darks = 0.0;
  double curve_lights = 0.0;
  double curve_highlights = 0.0;
  QVariantList master_curve_points;
  QVariantList red_curve_points;
  QVariantList green_curve_points;
  QVariantList blue_curve_points;
  QString hsl_channel = QStringLiteral("Reds");
  double hue_adjustment = 0.0;
  double saturation_adjustment = 0.0;
  double luminance_adjustment = 0.0;
  // Per-channel mixer adjustments: [Reds, Oranges, Yellows, Greens, Aquas, Blues, Purples, Magentas]
  struct MixerChannel {
    double hue = 0.0;
    double saturation = 0.0;
    double luminance = 0.0;
  };
  std::array<MixerChannel, 8> mixer_channels{};
  double grading_shadows_hue = 0.0;
  double grading_shadows_saturation = 0.0;
  double grading_shadows_luminance = 0.0;
  double grading_midtones_hue = 0.0;
  double grading_midtones_saturation = 0.0;
  double grading_midtones_luminance = 0.0;
  double grading_highlights_hue = 0.0;
  double grading_highlights_saturation = 0.0;
  double grading_highlights_luminance = 0.0;
  double crop_rect_x = 0.0;
  double crop_rect_y = 0.0;
  double crop_rect_width = 0.0;
  double crop_rect_height = 0.0;
  double crop_zoom = 1.0;
  QString crop_preset = QStringLiteral("Original");
  QString output_color_space = QStringLiteral("sRGB");
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

  [[nodiscard]] bool operator==(const EditRecipeState& other) const {
    auto feq = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    return feq(raw_exposure, other.raw_exposure) &&
           input_color_profile == other.input_color_profile &&
           working_color_profile == other.working_color_profile &&
           preview_color_space == other.preview_color_space &&
           render_intent == other.render_intent &&
           raw_input_transform_enabled == other.raw_input_transform_enabled &&
           highlight_reconstruction_mode == other.highlight_reconstruction_mode &&
           chromatic_adaptation_method == other.chromatic_adaptation_method &&
           feq(raw_boost, other.raw_boost) &&
           feq(raw_shadow_boost, other.raw_shadow_boost) &&
           feq(raw_detail, other.raw_detail) &&
           raw_highlight_recovery == other.raw_highlight_recovery &&
           raw_gamut_mapping == other.raw_gamut_mapping &&
           raw_lens_correction == other.raw_lens_correction &&
           raw_white_balance_override == other.raw_white_balance_override &&
           feq(raw_white_balance_temperature, other.raw_white_balance_temperature) &&
           feq(raw_white_balance_tint, other.raw_white_balance_tint) &&
           feq(exposure, other.exposure) && feq(contrast, other.contrast) &&
           feq(highlights, other.highlights) && feq(shadows, other.shadows) &&
           feq(whites, other.whites) && feq(blacks, other.blacks) &&
           feq(temperature, other.temperature) && feq(tint, other.tint) &&
           feq(texture, other.texture) && feq(clarity, other.clarity) &&
           feq(dehaze, other.dehaze) &&
           feq(sharpening, other.sharpening) && feq(sharpen_masking, other.sharpen_masking) &&
           feq(luminance_nr, other.luminance_nr) && feq(color_nr, other.color_nr) &&
           feq(lens_distortion, other.lens_distortion) &&
           feq(lens_vignette, other.lens_vignette) &&
           feq(chromatic_aberration, other.chromatic_aberration) &&
           feq(vibrance, other.vibrance) &&
           feq(saturation, other.saturation) && feq(curve_shadows, other.curve_shadows) &&
           feq(curve_darks, other.curve_darks) && feq(curve_lights, other.curve_lights) &&
           feq(curve_highlights, other.curve_highlights) &&
           master_curve_points == other.master_curve_points &&
           red_curve_points == other.red_curve_points &&
           green_curve_points == other.green_curve_points &&
           blue_curve_points == other.blue_curve_points &&
           hsl_channel == other.hsl_channel &&
           feq(hue_adjustment, other.hue_adjustment) &&
           feq(saturation_adjustment, other.saturation_adjustment) &&
           feq(luminance_adjustment, other.luminance_adjustment) &&
           [&]() {
             for (std::size_t i = 0; i < mixer_channels.size(); ++i) {
               if (!feq(mixer_channels[i].hue, other.mixer_channels[i].hue) ||
                   !feq(mixer_channels[i].saturation, other.mixer_channels[i].saturation) ||
                   !feq(mixer_channels[i].luminance, other.mixer_channels[i].luminance)) {
                 return false;
               }
             }
             return true;
           }() &&
           feq(grading_shadows_hue, other.grading_shadows_hue) &&
           feq(grading_shadows_saturation, other.grading_shadows_saturation) &&
           feq(grading_shadows_luminance, other.grading_shadows_luminance) &&
           feq(grading_midtones_hue, other.grading_midtones_hue) &&
           feq(grading_midtones_saturation, other.grading_midtones_saturation) &&
           feq(grading_midtones_luminance, other.grading_midtones_luminance) &&
           feq(grading_highlights_hue, other.grading_highlights_hue) &&
           feq(grading_highlights_saturation, other.grading_highlights_saturation) &&
           feq(grading_highlights_luminance, other.grading_highlights_luminance) &&
           feq(crop_rect_x, other.crop_rect_x) && feq(crop_rect_y, other.crop_rect_y) &&
           feq(crop_rect_width, other.crop_rect_width) && feq(crop_rect_height, other.crop_rect_height) &&
           feq(crop_zoom, other.crop_zoom) && crop_preset == other.crop_preset &&
           output_color_space == other.output_color_space &&
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
           mask_stack == other.mask_stack;
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

}  // namespace detail

inline bool valid_segmentation_payload(const QByteArray& data, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }

  const auto expected_size = static_cast<qint64>(width) * static_cast<qint64>(height);
  return expected_size > 0 &&
         expected_size <= static_cast<qint64>(std::numeric_limits<int>::max()) &&
         data.size() == expected_size;
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
      QStringLiteral("inpaintModelName"),
      source.value(QStringLiteral("inpaintModelName")).toString());
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
      QStringLiteral("inpaintModelName"),
      geometry.value(QStringLiteral("inpaintModelName")).toString());
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
      QStringLiteral("inpaintModelName"),
      mask_definition.value(QStringLiteral("inpaintModelName")).toString());
}

inline QString normalized_mask_kind(const QString& kind) {
  const auto normalized = kind.trimmed().toLower();
  if (normalized == QStringLiteral("linear") || normalized == QStringLiteral("radial") ||
      normalized == QStringLiteral("brush") || normalized == QStringLiteral("polygon") || normalized == QStringLiteral("luminance") ||
      normalized == QStringLiteral("color") || normalized == QStringLiteral("subject") ||
      normalized == QStringLiteral("object")) {
    return normalized;
  }
  return QStringLiteral("linear");
}

inline QString normalized_mask_operation(const QString& operation) {
  const auto normalized = operation.trimmed().toLower();
  if (normalized == QStringLiteral("add") || normalized == QStringLiteral("subtract") ||
      normalized == QStringLiteral("intersect")) {
    return normalized;
  }
  return QStringLiteral("add");
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
  } else if (normalized_kind == QStringLiteral("brush")) {
    label = QStringLiteral("Brush");
  } else if (normalized_kind == QStringLiteral("polygon")) {
    label = QStringLiteral("Polygon");
  } else if (normalized_kind == QStringLiteral("luminance")) {
    label = QStringLiteral("Luminance");
  } else if (normalized_kind == QStringLiteral("color")) {
    label = QStringLiteral("Color");
  } else if (normalized_kind == QStringLiteral("subject")) {
    label = QStringLiteral("Subject");
  } else if (normalized_kind == QStringLiteral("object")) {
    label = QStringLiteral("Object");
  }
  return QStringLiteral("%1 Mask %2").arg(label).arg(ordinal);
}

inline QVariantMap default_mask_definition(const QString& kind, int ordinal = 1) {
  static int unique_suffix = 0;
  ++unique_suffix;
  const auto normalized_kind = normalized_mask_kind(kind);
  QVariantMap mask;
  mask.insert(QStringLiteral("id"), QStringLiteral("%1-mask-%2-%3").arg(normalized_kind).arg(ordinal).arg(unique_suffix));
  QString label = QStringLiteral("Linear");
  if (normalized_kind == QStringLiteral("radial")) {
    label = QStringLiteral("Radial");
  } else if (normalized_kind == QStringLiteral("brush")) {
    label = QStringLiteral("Brush");
  } else if (normalized_kind == QStringLiteral("polygon")) {
    label = QStringLiteral("Polygon");
  } else if (normalized_kind == QStringLiteral("luminance")) {
    label = QStringLiteral("Luminance");
  } else if (normalized_kind == QStringLiteral("color")) {
    label = QStringLiteral("Color");
  } else if (normalized_kind == QStringLiteral("subject")) {
    label = QStringLiteral("Subject");
  } else if (normalized_kind == QStringLiteral("object")) {
    label = QStringLiteral("Object");
  }
  mask.insert(QStringLiteral("name"), QStringLiteral("%1 %2").arg(label).arg(ordinal));
  mask.insert(QStringLiteral("kind"), normalized_kind);
  mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  mask.insert(QStringLiteral("maskSetId"), QStringLiteral("%1-set-%2-%3").arg(normalized_kind).arg(ordinal).arg(unique_suffix));
  mask.insert(QStringLiteral("maskSetName"), default_mask_set_name(normalized_kind, ordinal));
  mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask.insert(QStringLiteral("enabled"), true);
  mask.insert(QStringLiteral("committed"), true);
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
  mask.insert(QStringLiteral("retouchMode"), QString());
  mask.insert(QStringLiteral("selectionInvert"), false);
  mask.insert(QStringLiteral("selectionExpand"), 0.0);
  mask.insert(QStringLiteral("selectionEdgeContrast"), 0.0);
  mask.insert(QStringLiteral("selectionDecontaminate"), 0.0);
  mask.insert(
      QStringLiteral("feather"),
      normalized_kind == QStringLiteral("radial") ? 0.25
      : normalized_kind == QStringLiteral("brush") ? 0.75
      : normalized_kind == QStringLiteral("polygon") ? 0.06
      : normalized_kind == QStringLiteral("subject") || normalized_kind == QStringLiteral("object") ? 0.5
      : 0.22);
  mask.insert(QStringLiteral("rotation"), 0.0);
  if (normalized_kind == QStringLiteral("radial")) {
    mask.insert(QStringLiteral("centerX"), 0.5);
    mask.insert(QStringLiteral("centerY"), 0.5);
    mask.insert(QStringLiteral("radiusX"), 0.25);
    mask.insert(QStringLiteral("radiusY"), 0.25);
  } else if (normalized_kind == QStringLiteral("linear")) {
    mask.insert(QStringLiteral("position"), 0.5);
  } else if (normalized_kind == QStringLiteral("brush")) {
    mask.insert(QStringLiteral("retouchMode"), QString());
    mask.insert(QStringLiteral("brushSize"), 0.08);
    mask.insert(QStringLiteral("brushFlow"), 1.0);
    mask.insert(QStringLiteral("cloneSourceSet"), false);
    mask.insert(QStringLiteral("cloneSourceX"), 0.5);
    mask.insert(QStringLiteral("cloneSourceY"), 0.5);
    mask.insert(QStringLiteral("cloneOffsetX"), 0.0);
    mask.insert(QStringLiteral("cloneOffsetY"), 0.0);
    mask.insert(QStringLiteral("cloneScale"), 1.0);
    mask.insert(QStringLiteral("cloneRotation"), 0.0);
    mask.insert(QStringLiteral("brushPoints"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("polygon")) {
    mask.insert(QStringLiteral("polygonPoints"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("luminance")) {
    mask.insert(QStringLiteral("luminanceMin"), 0.2);
    mask.insert(QStringLiteral("luminanceMax"), 0.8);
    mask.insert(QStringLiteral("rangeFeather"), 0.1);
  } else if (normalized_kind == QStringLiteral("color")) {
    mask.insert(QStringLiteral("hueCenter"), 0.08);
    mask.insert(QStringLiteral("hueRange"), 0.12);
    mask.insert(QStringLiteral("saturationMin"), 0.1);
    mask.insert(QStringLiteral("saturationMax"), 1.0);
    mask.insert(QStringLiteral("rangeFeather"), 0.08);
  }
  return mask;
}

inline QVariantMap normalized_mask_definition(const QVariantMap& candidate, int ordinal = 1) {
  const auto normalized_kind = normalized_mask_kind(candidate.value(QStringLiteral("kind")).toString());
  QVariantMap mask = default_mask_definition(normalized_kind, ordinal);
  const auto id = candidate.value(QStringLiteral("id")).toString().simplified();
  const auto name = candidate.value(QStringLiteral("name")).toString().simplified();
  mask.insert(QStringLiteral("id"), id.isEmpty() ? mask.value(QStringLiteral("id")).toString() : id);
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
      QStringLiteral("enabled"),
      candidate.contains(QStringLiteral("enabled"))
          ? candidate.value(QStringLiteral("enabled")).toBool()
          : true);
  mask.insert(
      QStringLiteral("committed"),
      candidate.contains(QStringLiteral("committed"))
          ? candidate.value(QStringLiteral("committed")).toBool()
          : true);
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
  mask.insert(QStringLiteral("hueShift"), candidate.value(QStringLiteral("hueShift")).toDouble());
  mask.insert(QStringLiteral("saturationShift"), candidate.value(QStringLiteral("saturationShift")).toDouble());
  mask.insert(
      QStringLiteral("retouchMode"),
      candidate.contains(QStringLiteral("retouchMode"))
          ? candidate.value(QStringLiteral("retouchMode")).toString().trimmed().toLower()
          : QString());
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
      QStringLiteral("selectionInvert"),
      candidate.contains(QStringLiteral("selectionInvert"))
          ? candidate.value(QStringLiteral("selectionInvert")).toBool()
          : false);
  mask.insert(
      QStringLiteral("selectionExpand"),
      candidate.contains(QStringLiteral("selectionExpand"))
          ? candidate.value(QStringLiteral("selectionExpand")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("selectionEdgeContrast"),
      candidate.contains(QStringLiteral("selectionEdgeContrast"))
          ? candidate.value(QStringLiteral("selectionEdgeContrast")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("selectionDecontaminate"),
      candidate.contains(QStringLiteral("selectionDecontaminate"))
          ? candidate.value(QStringLiteral("selectionDecontaminate")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("feather"),
      candidate.contains(QStringLiteral("feather"))
          ? candidate.value(QStringLiteral("feather")).toDouble()
          : (normalized_kind == QStringLiteral("radial") ? 0.25
             : normalized_kind == QStringLiteral("brush") ? 0.75
             : normalized_kind == QStringLiteral("polygon") ? 0.06
             : normalized_kind == QStringLiteral("subject") || normalized_kind == QStringLiteral("object") ? 0.5
             : 0.22));
  mask.insert(QStringLiteral("rotation"), candidate.value(QStringLiteral("rotation")).toDouble());
  if (normalized_kind == QStringLiteral("radial")) {
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
            : 0.25);
    mask.insert(
        QStringLiteral("radiusY"),
        candidate.contains(QStringLiteral("radiusY"))
            ? candidate.value(QStringLiteral("radiusY")).toDouble()
            : 0.25);
  } else if (normalized_kind == QStringLiteral("linear")) {
    mask.insert(
        QStringLiteral("position"),
        candidate.contains(QStringLiteral("position"))
            ? candidate.value(QStringLiteral("position")).toDouble()
            : 0.5);
  } else if (normalized_kind == QStringLiteral("brush")) {
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
    QVariantList normalized_points;
    const auto points = candidate.value(QStringLiteral("brushPoints")).toList();
    for (const auto& point_value : points) {
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
      normalized_points.append(normalized_point);
    }
    mask.insert(QStringLiteral("brushPoints"), normalized_points);
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
    }
  } else if (normalized_kind == QStringLiteral("polygon")) {
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
  } else if (normalized_kind == QStringLiteral("subject") || normalized_kind == QStringLiteral("object") ||
             normalized_kind == QStringLiteral("brush")) {
    const auto segmentation_data = candidate.value(QStringLiteral("segmentationData")).toByteArray();
    const auto segmentation_width = candidate.value(QStringLiteral("segmentationWidth")).toInt();
    const auto segmentation_height = candidate.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
      mask.insert(QStringLiteral("segmentationData"), segmentation_data);
      mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
      mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
    }
  }
  copy_inpaint_patch_payload(&mask, candidate);
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
      QStringLiteral("selectionDecontaminate"),
      source.value(QStringLiteral("selectionDecontaminate")).toDouble());
}

inline QVariantList normalize_mask_stack(const QVariantList& stack) {
  QVariantList normalized;
  normalized.reserve(stack.size());
  auto ordinal = 1;
  for (const auto& value : stack) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
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

inline QString normalized_render_intent_name(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("Relative Colorimetric"), Qt::CaseInsensitive) == 0 ||
      trimmed.compare(QStringLiteral("relative-colorimetric"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Relative Colorimetric");
  }
  return QStringLiteral("Perceptual");
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
    merged.raw_exposure = source.raw_exposure;
    merged.raw_boost = source.raw_boost;
    merged.raw_shadow_boost = source.raw_shadow_boost;
    merged.raw_detail = source.raw_detail;
    merged.raw_highlight_recovery = source.raw_highlight_recovery;
    merged.raw_gamut_mapping = source.raw_gamut_mapping;
    merged.raw_lens_correction = source.raw_lens_correction;
    merged.raw_white_balance_override = source.raw_white_balance_override;
    merged.raw_white_balance_temperature = source.raw_white_balance_temperature;
    merged.raw_white_balance_tint = source.raw_white_balance_tint;
  }

  if (normalized_groups.contains(QStringLiteral("tone"))) {
    merged.exposure = source.exposure;
    merged.contrast = source.contrast;
    merged.highlights = source.highlights;
    merged.shadows = source.shadows;
    merged.whites = source.whites;
    merged.blacks = source.blacks;
    merged.curve_shadows = source.curve_shadows;
    merged.curve_darks = source.curve_darks;
    merged.curve_lights = source.curve_lights;
    merged.curve_highlights = source.curve_highlights;
    merged.master_curve_points = source.master_curve_points;
    merged.red_curve_points = source.red_curve_points;
    merged.green_curve_points = source.green_curve_points;
    merged.blue_curve_points = source.blue_curve_points;
  }

  if (normalized_groups.contains(QStringLiteral("color"))) {
    merged.preview_color_space = source.preview_color_space;
    merged.temperature = source.temperature;
    merged.tint = source.tint;
    merged.output_color_space = source.output_color_space;
    merged.vibrance = source.vibrance;
    merged.saturation = source.saturation;
    merged.hsl_channel = source.hsl_channel;
    merged.hue_adjustment = source.hue_adjustment;
    merged.saturation_adjustment = source.saturation_adjustment;
    merged.luminance_adjustment = source.luminance_adjustment;
    merged.mixer_channels = source.mixer_channels;
    merged.grading_shadows_hue = source.grading_shadows_hue;
    merged.grading_shadows_saturation = source.grading_shadows_saturation;
    merged.grading_shadows_luminance = source.grading_shadows_luminance;
    merged.grading_midtones_hue = source.grading_midtones_hue;
    merged.grading_midtones_saturation = source.grading_midtones_saturation;
    merged.grading_midtones_luminance = source.grading_midtones_luminance;
    merged.grading_highlights_hue = source.grading_highlights_hue;
    merged.grading_highlights_saturation = source.grading_highlights_saturation;
    merged.grading_highlights_luminance = source.grading_highlights_luminance;
  }

  if (normalized_groups.contains(QStringLiteral("crop"))) {
    merged.crop_rect_x = source.crop_rect_x;
    merged.crop_rect_y = source.crop_rect_y;
    merged.crop_rect_width = source.crop_rect_width;
    merged.crop_rect_height = source.crop_rect_height;
    merged.crop_zoom = source.crop_zoom;
    merged.crop_preset = source.crop_preset;
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

  if (normalized_groups.contains(QStringLiteral("detail"))) {
    merged.texture = source.texture;
    merged.clarity = source.clarity;
    merged.dehaze = source.dehaze;
    merged.sharpening = source.sharpening;
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
  state.raw_exposure = raw_adjustments.value(QStringLiteral("exposure")).toDouble();
  state.raw_boost = raw_adjustments.value(QStringLiteral("boost")).toDouble();
  state.raw_shadow_boost = raw_adjustments.value(QStringLiteral("shadowBoost")).toDouble(1.0);
  state.raw_detail = raw_adjustments.value(QStringLiteral("detail")).toDouble();
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
  state.raw_white_balance_temperature =
      raw_adjustments.value(QStringLiteral("whiteBalanceTemperature")).toDouble(5200.0);
  state.raw_white_balance_tint =
      raw_adjustments.value(QStringLiteral("whiteBalanceTint")).toDouble();
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

  state.crop_zoom = detail::read_double(
      crop_transform, QStringLiteral("zoom"), object, QStringLiteral("cropZoom"), 1.0);
  state.crop_preset = detail::read_string(
      crop_transform, QStringLiteral("preset"), object, QStringLiteral("cropPreset"), QStringLiteral("Original"));
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

    QVariantMap parsed_mask;
    parsed_mask.insert(QStringLiteral("id"), mask_object.value(QStringLiteral("id")).toString());
    parsed_mask.insert(QStringLiteral("name"), mask_object.value(QStringLiteral("name")).toString());
    parsed_mask.insert(QStringLiteral("kind"), kind);
    parsed_mask.insert(
        QStringLiteral("toolRole"),
        normalized_mask_tool_role(
            mask_object.contains(QStringLiteral("toolRole"))
                ? mask_object.value(QStringLiteral("toolRole")).toString()
                : default_tool_role));
    parsed_mask.insert(
        QStringLiteral("operation"),
        normalized_mask_operation(mask_object.value(QStringLiteral("operation")).toString()));
    parsed_mask.insert(
        QStringLiteral("enabled"),
        mask_object.contains(QStringLiteral("enabled"))
            ? mask_object.value(QStringLiteral("enabled")).toBool()
            : true);
    parsed_mask.insert(
        QStringLiteral("committed"),
        mask_object.contains(QStringLiteral("committed"))
            ? mask_object.value(QStringLiteral("committed")).toBool()
            : true);
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
            : adjustments.value(QStringLiteral("selectionInvert")).toBool());
    parsed_mask.insert(
        QStringLiteral("selectionExpand"),
        selection.contains(QStringLiteral("expand"))
            ? selection.value(QStringLiteral("expand")).toDouble()
            : adjustments.value(QStringLiteral("selectionExpand")).toDouble());
    parsed_mask.insert(
        QStringLiteral("selectionEdgeContrast"),
        adjustments.value(QStringLiteral("selectionEdgeContrast")).toDouble());
    parsed_mask.insert(
        QStringLiteral("selectionDecontaminate"),
        adjustments.value(QStringLiteral("selectionDecontaminate")).toDouble());
    if (parsed_mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("selection")) {
      parsed_mask.insert(QStringLiteral("maskSetId"), mask_set_id);
      parsed_mask.insert(QStringLiteral("maskSetName"), mask_set_name);
    }

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

    if (kind == QStringLiteral("radial")) {
      state.radial_mask_enabled =
          mask_object.contains(QStringLiteral("enabled"))
              ? mask_object.value(QStringLiteral("enabled")).toBool()
              : true;
      state.radial_mask_exposure = adjustments.value(QStringLiteral("exposure")).toDouble();
      state.radial_mask_temperature = adjustments.value(QStringLiteral("temperature")).toDouble();
      state.radial_mask_feather = geometry.value(QStringLiteral("feather")).toDouble(0.25);
      state.radial_mask_center_x = geometry.value(QStringLiteral("centerX")).toDouble(0.5);
      state.radial_mask_center_y = geometry.value(QStringLiteral("centerY")).toDouble(0.5);
      state.radial_mask_radius_x = geometry.value(QStringLiteral("radiusX")).toDouble(0.25);
      state.radial_mask_radius_y = geometry.value(QStringLiteral("radiusY")).toDouble(0.25);
      state.radial_mask_rotation = geometry.value(QStringLiteral("rotation")).toDouble();
      parsed_mask.insert(QStringLiteral("feather"), state.radial_mask_feather);
      parsed_mask.insert(QStringLiteral("centerX"), state.radial_mask_center_x);
      parsed_mask.insert(QStringLiteral("centerY"), state.radial_mask_center_y);
      parsed_mask.insert(QStringLiteral("radiusX"), state.radial_mask_radius_x);
      parsed_mask.insert(QStringLiteral("radiusY"), state.radial_mask_radius_y);
      parsed_mask.insert(QStringLiteral("rotation"), state.radial_mask_rotation);
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, radial_mask_ordinal));
      ++radial_mask_ordinal;
      return;
    }

    if (kind == QStringLiteral("brush")) {
      parsed_mask.insert(
          QStringLiteral("retouchMode"),
          geometry.value(QStringLiteral("retouchMode")).toString());
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
        brush_points.append(point);
      }
      parsed_mask.insert(QStringLiteral("brushPoints"), brush_points);
      const auto segmentation_data =
          detail::read_byte_array(geometry, QStringLiteral("segmentationData"));
      const auto segmentation_width = geometry.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = geometry.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        parsed_mask.insert(QStringLiteral("segmentationData"), segmentation_data);
        parsed_mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        parsed_mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      }
      read_inpaint_patch_payload(&parsed_mask, geometry);
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("polygon")) {
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
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("subject") || kind == QStringLiteral("object") ||
        kind == QStringLiteral("brush")) {
      parsed_mask.insert(
          QStringLiteral("retouchMode"),
          geometry.value(QStringLiteral("retouchMode")).toString());
      parsed_mask.insert(
          QStringLiteral("feather"),
          geometry.value(QStringLiteral("feather")).toDouble(0.5));
      const auto segmentation_data =
          detail::read_byte_array(geometry, QStringLiteral("segmentationData"));
      const auto segmentation_width = geometry.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = geometry.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        parsed_mask.insert(QStringLiteral("segmentationData"), segmentation_data);
        parsed_mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        parsed_mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      }
      read_inpaint_patch_payload(&parsed_mask, geometry);
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
      append_parsed_mask(
          source_value.toObject(),
          set_adjustments,
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
      QStringLiteral("inputProfile"),
      normalized_input_color_profile_name(state.input_color_profile));
  raw_adjustments.insert(QStringLiteral("exposure"), state.raw_exposure);
  raw_adjustments.insert(QStringLiteral("boost"), state.raw_boost);
  raw_adjustments.insert(QStringLiteral("shadowBoost"), state.raw_shadow_boost);
  raw_adjustments.insert(QStringLiteral("detail"), state.raw_detail);
  raw_adjustments.insert(QStringLiteral("highlightRecovery"), state.raw_highlight_recovery);
  raw_adjustments.insert(QStringLiteral("gamutMapping"), state.raw_gamut_mapping);
  raw_adjustments.insert(QStringLiteral("lensCorrection"), state.raw_lens_correction);
  raw_adjustments.insert(QStringLiteral("whiteBalanceOverride"), state.raw_white_balance_override);
  raw_adjustments.insert(
      QStringLiteral("whiteBalanceTemperature"), state.raw_white_balance_temperature);
  raw_adjustments.insert(QStringLiteral("whiteBalanceTint"), state.raw_white_balance_tint);

  QJsonObject global_adjustments;
  global_adjustments.insert(QStringLiteral("exposure"), state.exposure);
  global_adjustments.insert(QStringLiteral("contrast"), state.contrast);
  global_adjustments.insert(QStringLiteral("highlights"), state.highlights);
  global_adjustments.insert(QStringLiteral("shadows"), state.shadows);
  global_adjustments.insert(QStringLiteral("whites"), state.whites);
  global_adjustments.insert(QStringLiteral("blacks"), state.blacks);
  global_adjustments.insert(QStringLiteral("temperature"), state.temperature);
  global_adjustments.insert(QStringLiteral("tint"), state.tint);
  global_adjustments.insert(QStringLiteral("texture"), state.texture);
  global_adjustments.insert(QStringLiteral("clarity"), state.clarity);
  global_adjustments.insert(QStringLiteral("dehaze"), state.dehaze);
  global_adjustments.insert(QStringLiteral("sharpening"), state.sharpening);
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
  append_channel(QStringLiteral("Red"), state.red_curve_points);
  append_channel(QStringLiteral("Green"), state.green_curve_points);
  append_channel(QStringLiteral("Blue"), state.blue_curve_points);
  curves.insert(QStringLiteral("channels"), channels);

  QJsonObject color_mixer;
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
      if (std::abs(ch.hue) > 1e-6 || std::abs(ch.saturation) > 1e-6 || std::abs(ch.luminance) > 1e-6) {
        QJsonObject ch_obj;
        ch_obj.insert(QStringLiteral("hue"), ch.hue);
        ch_obj.insert(QStringLiteral("saturation"), ch.saturation);
        ch_obj.insert(QStringLiteral("luminance"), ch.luminance);
        mixer_channels_obj.insert(kMixerChannelNames[i], ch_obj);
      }
    }
    if (!mixer_channels_obj.isEmpty()) {
      color_mixer.insert(QStringLiteral("channels"), mixer_channels_obj);
    }
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

  QJsonObject crop_transform;
  crop_transform.insert(QStringLiteral("preset"), state.crop_preset);
  crop_transform.insert(QStringLiteral("zoom"), state.crop_zoom);
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
  output_transform.insert(
      QStringLiteral("workingColorSpace"),
      normalized_working_color_profile_name(state.working_color_profile));
  output_transform.insert(
      QStringLiteral("renderIntent"),
      normalized_render_intent_name(state.render_intent));

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
        QStringLiteral("selectionEdgeContrast"),
        mask_definition.value(QStringLiteral("selectionEdgeContrast")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionDecontaminate"),
        mask_definition.value(QStringLiteral("selectionDecontaminate")).toDouble());

    QJsonObject geometry;
    geometry.insert(QStringLiteral("feather"), mask_definition.value(QStringLiteral("feather")).toDouble());
    geometry.insert(QStringLiteral("rotation"), mask_definition.value(QStringLiteral("rotation")).toDouble());
    QJsonObject constraints;
    if (kind == QStringLiteral("radial")) {
      geometry.insert(QStringLiteral("centerX"), mask_definition.value(QStringLiteral("centerX")).toDouble());
      geometry.insert(QStringLiteral("centerY"), mask_definition.value(QStringLiteral("centerY")).toDouble());
      geometry.insert(QStringLiteral("radiusX"), mask_definition.value(QStringLiteral("radiusX")).toDouble());
      geometry.insert(QStringLiteral("radiusY"), mask_definition.value(QStringLiteral("radiusY")).toDouble());
    } else if (kind == QStringLiteral("linear")) {
      geometry.insert(QStringLiteral("position"), mask_definition.value(QStringLiteral("position")).toDouble());
    } else if (kind == QStringLiteral("brush")) {
      geometry.insert(QStringLiteral("retouchMode"), mask_definition.value(QStringLiteral("retouchMode")).toString());
      geometry.insert(QStringLiteral("brushSize"), mask_definition.value(QStringLiteral("brushSize")).toDouble());
      geometry.insert(QStringLiteral("brushFlow"), mask_definition.value(QStringLiteral("brushFlow")).toDouble());
      geometry.insert(QStringLiteral("cloneSourceSet"), mask_definition.value(QStringLiteral("cloneSourceSet")).toBool());
      geometry.insert(QStringLiteral("cloneSourceX"), mask_definition.value(QStringLiteral("cloneSourceX")).toDouble());
      geometry.insert(QStringLiteral("cloneSourceY"), mask_definition.value(QStringLiteral("cloneSourceY")).toDouble());
      geometry.insert(QStringLiteral("cloneOffsetX"), mask_definition.value(QStringLiteral("cloneOffsetX")).toDouble());
      geometry.insert(QStringLiteral("cloneOffsetY"), mask_definition.value(QStringLiteral("cloneOffsetY")).toDouble());
      geometry.insert(QStringLiteral("cloneScale"), mask_definition.value(QStringLiteral("cloneScale")).toDouble());
      geometry.insert(QStringLiteral("cloneRotation"), mask_definition.value(QStringLiteral("cloneRotation")).toDouble());
      QJsonArray brush_points;
      for (const auto& point_value : mask_definition.value(QStringLiteral("brushPoints")).toList()) {
        const auto point = point_value.toMap();
        QJsonObject brush_point;
        brush_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        brush_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        brush_point.insert(QStringLiteral("size"), point.value(QStringLiteral("size")).toDouble());
        brush_point.insert(QStringLiteral("flow"), point.value(QStringLiteral("flow")).toDouble());
        brush_points.append(brush_point);
      }
      geometry.insert(QStringLiteral("brushPoints"), brush_points);
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      }
    } else if (kind == QStringLiteral("polygon")) {
      QJsonArray polygon_points;
      for (const auto& point_value : mask_definition.value(QStringLiteral("polygonPoints")).toList()) {
        const auto point = point_value.toMap();
        QJsonObject polygon_point;
        polygon_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        polygon_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        polygon_points.append(polygon_point);
      }
      geometry.insert(QStringLiteral("polygonPoints"), polygon_points);
    } else if (kind == QStringLiteral("luminance")) {
      constraints.insert(QStringLiteral("luminanceMin"), mask_definition.value(QStringLiteral("luminanceMin")).toDouble());
      constraints.insert(QStringLiteral("luminanceMax"), mask_definition.value(QStringLiteral("luminanceMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
    } else if (kind == QStringLiteral("color")) {
      constraints.insert(QStringLiteral("hueCenter"), mask_definition.value(QStringLiteral("hueCenter")).toDouble());
      constraints.insert(QStringLiteral("hueRange"), mask_definition.value(QStringLiteral("hueRange")).toDouble());
      constraints.insert(QStringLiteral("saturationMin"), mask_definition.value(QStringLiteral("saturationMin")).toDouble());
      constraints.insert(QStringLiteral("saturationMax"), mask_definition.value(QStringLiteral("saturationMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
    } else if (kind == QStringLiteral("subject") || kind == QStringLiteral("object")) {
      geometry.insert(QStringLiteral("retouchMode"), mask_definition.value(QStringLiteral("retouchMode")).toString());
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (!segmentation_data.isEmpty() && segmentation_width > 0 && segmentation_height > 0) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      }
    }
    write_inpaint_patch_payload(&geometry, mask_definition);

    QJsonObject mask;
    mask.insert(QStringLiteral("id"), mask_definition.value(QStringLiteral("id")).toString());
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
        QStringLiteral("committed"),
        mask_definition.contains(QStringLiteral("committed"))
            ? mask_definition.value(QStringLiteral("committed")).toBool()
            : true);
    mask.insert(QStringLiteral("adjustments"), adjustments);
    mask.insert(QStringLiteral("geometry"), geometry);
    if (!constraints.isEmpty()) {
      mask.insert(QStringLiteral("constraints"), constraints);
    }
    return mask;
  };

  QVariantList serialized_masks = state.mask_stack;
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
          QStringLiteral("selectionDecontaminate"),
          mask_definition.value(QStringLiteral("selectionDecontaminate")).toDouble());

      QJsonObject set_object;
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
    const auto kind = normalized_mask_kind(mask_definition.value(QStringLiteral("kind")).toString());
    if (kind == QStringLiteral("radial")) {
      geometry.insert(QStringLiteral("centerX"), mask_definition.value(QStringLiteral("centerX")).toDouble());
      geometry.insert(QStringLiteral("centerY"), mask_definition.value(QStringLiteral("centerY")).toDouble());
      geometry.insert(QStringLiteral("radiusX"), mask_definition.value(QStringLiteral("radiusX")).toDouble());
      geometry.insert(QStringLiteral("radiusY"), mask_definition.value(QStringLiteral("radiusY")).toDouble());
    } else if (kind == QStringLiteral("linear")) {
      geometry.insert(QStringLiteral("position"), mask_definition.value(QStringLiteral("position")).toDouble());
    } else if (kind == QStringLiteral("brush")) {
      geometry.insert(QStringLiteral("brushSize"), mask_definition.value(QStringLiteral("brushSize")).toDouble());
      geometry.insert(QStringLiteral("brushFlow"), mask_definition.value(QStringLiteral("brushFlow")).toDouble());
      QJsonArray brush_points;
      for (const auto& point_value : mask_definition.value(QStringLiteral("brushPoints")).toList()) {
        const auto point = point_value.toMap();
        QJsonObject brush_point;
        brush_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        brush_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        brush_point.insert(QStringLiteral("size"), point.value(QStringLiteral("size")).toDouble());
        brush_point.insert(QStringLiteral("flow"), point.value(QStringLiteral("flow")).toDouble());
        brush_points.append(brush_point);
      }
      geometry.insert(QStringLiteral("brushPoints"), brush_points);
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      }
    } else if (kind == QStringLiteral("polygon")) {
      QJsonArray polygon_points;
      for (const auto& point_value : mask_definition.value(QStringLiteral("polygonPoints")).toList()) {
        const auto point = point_value.toMap();
        QJsonObject polygon_point;
        polygon_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        polygon_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        polygon_points.append(polygon_point);
      }
      geometry.insert(QStringLiteral("polygonPoints"), polygon_points);
    } else if (kind == QStringLiteral("luminance")) {
      constraints.insert(QStringLiteral("luminanceMin"), mask_definition.value(QStringLiteral("luminanceMin")).toDouble());
      constraints.insert(QStringLiteral("luminanceMax"), mask_definition.value(QStringLiteral("luminanceMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
    } else if (kind == QStringLiteral("color")) {
      constraints.insert(QStringLiteral("hueCenter"), mask_definition.value(QStringLiteral("hueCenter")).toDouble());
      constraints.insert(QStringLiteral("hueRange"), mask_definition.value(QStringLiteral("hueRange")).toDouble());
      constraints.insert(QStringLiteral("saturationMin"), mask_definition.value(QStringLiteral("saturationMin")).toDouble());
      constraints.insert(QStringLiteral("saturationMax"), mask_definition.value(QStringLiteral("saturationMax")).toDouble());
      constraints.insert(QStringLiteral("rangeFeather"), mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
    } else if (kind == QStringLiteral("subject") || kind == QStringLiteral("object")) {
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      }
    }
    write_inpaint_patch_payload(&geometry, mask_definition);

    QJsonObject selection;
    selection.insert(
        QStringLiteral("invert"),
        mask_definition.value(QStringLiteral("selectionInvert")).toBool());
    selection.insert(
        QStringLiteral("expand"),
        mask_definition.value(QStringLiteral("selectionExpand")).toDouble());

    QJsonObject source_object;
    source_object.insert(QStringLiteral("id"), mask_definition.value(QStringLiteral("id")).toString());
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

  QJsonObject object;
  object.insert(QStringLiteral("recipeVersion"), 4);
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
  object.insert(QStringLiteral("maskSets"), mask_sets);
  object.insert(QStringLiteral("retouchStack"), retouch_stack);
  object.insert(QStringLiteral("overlayItems"), QJsonArray{});
  object.insert(QStringLiteral("guideSettings"), guide_settings);
  return object;
}

inline QString serialize_edit_recipe_json(const EditRecipeState& state) {
  return QString::fromUtf8(QJsonDocument(build_edit_recipe_object(state)).toJson(QJsonDocument::Compact));
}

}  // namespace photo_editor::desktop

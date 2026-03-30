#pragma once

#include <array>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>

namespace photo_editor::desktop {

struct EditRecipeState final {
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
    return feq(exposure, other.exposure) && feq(contrast, other.contrast) &&
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

inline QVariantMap default_mask_definition(const QString& kind, int ordinal = 1) {
  const auto normalized_kind = normalized_mask_kind(kind);
  QVariantMap mask;
  mask.insert(QStringLiteral("id"), QStringLiteral("%1-mask-%2").arg(normalized_kind).arg(ordinal));
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
  mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask.insert(QStringLiteral("enabled"), true);
  mask.insert(QStringLiteral("exposure"), 0.0);
  mask.insert(QStringLiteral("temperature"), 0.0);
  mask.insert(QStringLiteral("hueShift"), 0.0);
  mask.insert(QStringLiteral("saturationShift"), 0.0);
  mask.insert(QStringLiteral("colorizeHue"), 0.0);
  mask.insert(QStringLiteral("colorizeStrength"), 0.0);
  mask.insert(QStringLiteral("removeStrength"), 0.0);
  mask.insert(QStringLiteral("selectionInvert"), false);
  mask.insert(QStringLiteral("selectionExpand"), 0.0);
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
  mask.insert(
      QStringLiteral("operation"),
      normalized_mask_operation(candidate.value(QStringLiteral("operation")).toString()));
  mask.insert(
      QStringLiteral("enabled"),
      candidate.contains(QStringLiteral("enabled"))
          ? candidate.value(QStringLiteral("enabled")).toBool()
          : true);
  mask.insert(QStringLiteral("exposure"), candidate.value(QStringLiteral("exposure")).toDouble());
  mask.insert(QStringLiteral("temperature"), candidate.value(QStringLiteral("temperature")).toDouble());
  mask.insert(QStringLiteral("hueShift"), candidate.value(QStringLiteral("hueShift")).toDouble());
  mask.insert(QStringLiteral("saturationShift"), candidate.value(QStringLiteral("saturationShift")).toDouble());
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
        QStringLiteral("retouchMode"),
        candidate.contains(QStringLiteral("retouchMode"))
            ? candidate.value(QStringLiteral("retouchMode")).toString().trimmed().toLower()
            : QString());
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
  } else if (normalized_kind == QStringLiteral("subject") || normalized_kind == QStringLiteral("object")) {
    const auto segmentation_data = candidate.value(QStringLiteral("segmentationData")).toByteArray();
    const auto segmentation_width = candidate.value(QStringLiteral("segmentationWidth")).toInt();
    const auto segmentation_height = candidate.value(QStringLiteral("segmentationHeight")).toInt();
    if (!segmentation_data.isEmpty() && segmentation_width > 0 && segmentation_height > 0) {
      mask.insert(QStringLiteral("segmentationData"), segmentation_data);
      mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
      mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
    }
  }
  return mask;
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

inline EditRecipeState merge_edit_recipe_state_by_groups(
    const EditRecipeState& base,
    const EditRecipeState& source,
    const QStringList& groups) {
  const auto normalized_groups = normalize_adjustment_groups(groups);
  EditRecipeState merged = base;

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
    merged.temperature = source.temperature;
    merged.tint = source.tint;
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

  EditRecipeState state;
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

  const auto mask_stack = object.value(QStringLiteral("maskStack")).toArray();
  QVariantList parsed_mask_stack;
  auto linear_mask_ordinal = 1;
  auto radial_mask_ordinal = 1;
  for (const auto& value : mask_stack) {
    if (!value.isObject()) {
      continue;
    }

    const auto mask_object = value.toObject();
    const auto adjustments = detail::object_value(mask_object, QStringLiteral("adjustments"));
    const auto geometry = detail::object_value(mask_object, QStringLiteral("geometry"));
    const auto constraints = detail::object_value(mask_object, QStringLiteral("constraints"));
    const auto kind = normalized_mask_kind(mask_object.value(QStringLiteral("kind")).toString());
    QVariantMap parsed_mask;
    parsed_mask.insert(QStringLiteral("id"), mask_object.value(QStringLiteral("id")).toString());
    parsed_mask.insert(QStringLiteral("name"), mask_object.value(QStringLiteral("name")).toString());
    parsed_mask.insert(QStringLiteral("kind"), kind);
    parsed_mask.insert(
        QStringLiteral("operation"),
        normalized_mask_operation(mask_object.value(QStringLiteral("operation")).toString()));
    parsed_mask.insert(QStringLiteral("enabled"), mask_object.value(QStringLiteral("enabled")).toBool(true));
    parsed_mask.insert(QStringLiteral("exposure"), adjustments.value(QStringLiteral("exposure")).toDouble());
    parsed_mask.insert(QStringLiteral("temperature"), adjustments.value(QStringLiteral("temperature")).toDouble());
    parsed_mask.insert(QStringLiteral("hueShift"), adjustments.value(QStringLiteral("hueShift")).toDouble());
    parsed_mask.insert(QStringLiteral("saturationShift"), adjustments.value(QStringLiteral("saturationShift")).toDouble());
    parsed_mask.insert(QStringLiteral("colorizeHue"), adjustments.value(QStringLiteral("colorizeHue")).toDouble());
    parsed_mask.insert(QStringLiteral("colorizeStrength"), adjustments.value(QStringLiteral("colorizeStrength")).toDouble());
    parsed_mask.insert(QStringLiteral("removeStrength"), adjustments.value(QStringLiteral("removeStrength")).toDouble());
    parsed_mask.insert(QStringLiteral("selectionInvert"), adjustments.value(QStringLiteral("selectionInvert")).toBool(false));
    parsed_mask.insert(QStringLiteral("selectionExpand"), adjustments.value(QStringLiteral("selectionExpand")).toDouble());
    if (kind == QStringLiteral("linear")) {
      state.linear_mask_enabled = mask_object.value(QStringLiteral("enabled")).toBool(true);
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
      continue;
    }

    if (kind == QStringLiteral("radial")) {
      state.radial_mask_enabled = mask_object.value(QStringLiteral("enabled")).toBool(true);
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
      continue;
    }

    if (kind == QStringLiteral("brush")) {
      parsed_mask.insert(QStringLiteral("retouchMode"), geometry.value(QStringLiteral("retouchMode")).toString());
      parsed_mask.insert(QStringLiteral("feather"), geometry.value(QStringLiteral("feather")).toDouble(0.75));
      parsed_mask.insert(QStringLiteral("brushSize"), geometry.value(QStringLiteral("brushSize")).toDouble(0.08));
      parsed_mask.insert(QStringLiteral("brushFlow"), geometry.value(QStringLiteral("brushFlow")).toDouble(1.0));
      parsed_mask.insert(QStringLiteral("cloneSourceSet"), geometry.value(QStringLiteral("cloneSourceSet")).toBool(false));
      parsed_mask.insert(QStringLiteral("cloneSourceX"), geometry.value(QStringLiteral("cloneSourceX")).toDouble(0.5));
      parsed_mask.insert(QStringLiteral("cloneSourceY"), geometry.value(QStringLiteral("cloneSourceY")).toDouble(0.5));
      parsed_mask.insert(QStringLiteral("cloneOffsetX"), geometry.value(QStringLiteral("cloneOffsetX")).toDouble());
      parsed_mask.insert(QStringLiteral("cloneOffsetY"), geometry.value(QStringLiteral("cloneOffsetY")).toDouble());
      parsed_mask.insert(QStringLiteral("cloneScale"), geometry.value(QStringLiteral("cloneScale")).toDouble(1.0));
      parsed_mask.insert(QStringLiteral("cloneRotation"), geometry.value(QStringLiteral("cloneRotation")).toDouble());
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
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      continue;
    }

    if (kind == QStringLiteral("polygon")) {
      parsed_mask.insert(QStringLiteral("feather"), geometry.value(QStringLiteral("feather")).toDouble(0.06));
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
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      continue;
    }

    if (kind == QStringLiteral("luminance")) {
      parsed_mask.insert(QStringLiteral("luminanceMin"), constraints.value(QStringLiteral("luminanceMin")).toDouble(0.2));
      parsed_mask.insert(QStringLiteral("luminanceMax"), constraints.value(QStringLiteral("luminanceMax")).toDouble(0.8));
      parsed_mask.insert(QStringLiteral("rangeFeather"), constraints.value(QStringLiteral("rangeFeather")).toDouble(0.1));
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      continue;
    }

    if (kind == QStringLiteral("color")) {
      parsed_mask.insert(QStringLiteral("hueCenter"), constraints.value(QStringLiteral("hueCenter")).toDouble(0.08));
      parsed_mask.insert(QStringLiteral("hueRange"), constraints.value(QStringLiteral("hueRange")).toDouble(0.12));
      parsed_mask.insert(QStringLiteral("saturationMin"), constraints.value(QStringLiteral("saturationMin")).toDouble(0.1));
      parsed_mask.insert(QStringLiteral("saturationMax"), constraints.value(QStringLiteral("saturationMax")).toDouble(1.0));
      parsed_mask.insert(QStringLiteral("rangeFeather"), constraints.value(QStringLiteral("rangeFeather")).toDouble(0.08));
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      continue;
    }

    if (kind == QStringLiteral("subject") || kind == QStringLiteral("object")) {
      parsed_mask.insert(QStringLiteral("feather"), geometry.value(QStringLiteral("feather")).toDouble(0.5));
      const auto segmentation_data = detail::read_byte_array(geometry, QStringLiteral("segmentationData"));
      const auto segmentation_width = geometry.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = geometry.value(QStringLiteral("segmentationHeight")).toInt();
      if (!segmentation_data.isEmpty() && segmentation_width > 0 && segmentation_height > 0) {
        parsed_mask.insert(QStringLiteral("segmentationData"), segmentation_data);
        parsed_mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        parsed_mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      }
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
    }
  }

  if (parsed_mask_stack.isEmpty()) {
    state.linear_mask_enabled =
        object.value(QStringLiteral("linearMaskEnabled")).toBool(false);
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

    state.radial_mask_enabled = object.value(QStringLiteral("radialMaskEnabled")).toBool(false);
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

  QJsonArray mask_stack;
  const auto append_mask = [&mask_stack](const QVariantMap& candidate, int ordinal) {
    const auto mask_definition = normalized_mask_definition(candidate, ordinal);
    const auto kind = normalized_mask_kind(mask_definition.value(QStringLiteral("kind")).toString());

    QJsonObject adjustments;
    adjustments.insert(QStringLiteral("exposure"), mask_definition.value(QStringLiteral("exposure")).toDouble());
    adjustments.insert(
        QStringLiteral("temperature"),
        mask_definition.value(QStringLiteral("temperature")).toDouble());
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
      if (!segmentation_data.isEmpty() && segmentation_width > 0 && segmentation_height > 0) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      }
    }

    QJsonObject mask;
    mask.insert(QStringLiteral("id"), mask_definition.value(QStringLiteral("id")).toString());
    mask.insert(QStringLiteral("name"), mask_definition.value(QStringLiteral("name")).toString());
    mask.insert(QStringLiteral("kind"), kind);
    mask.insert(
        QStringLiteral("operation"),
        normalized_mask_operation(mask_definition.value(QStringLiteral("operation")).toString()));
    mask.insert(
        QStringLiteral("enabled"),
        mask_definition.contains(QStringLiteral("enabled"))
            ? mask_definition.value(QStringLiteral("enabled")).toBool()
            : true);
    mask.insert(QStringLiteral("adjustments"), adjustments);
    mask.insert(QStringLiteral("geometry"), geometry);
    if (!constraints.isEmpty()) {
      mask.insert(QStringLiteral("constraints"), constraints);
    }
    mask_stack.append(mask);
  };

  if (!state.mask_stack.isEmpty()) {
    auto ordinal = 1;
    for (const auto& mask_value : state.mask_stack) {
      append_mask(mask_value.toMap(), ordinal);
      ++ordinal;
    }
  } else {
    if (has_structured_linear_mask(state)) {
      QVariantMap linear_mask = default_mask_definition(QStringLiteral("linear"), 1);
      linear_mask.insert(QStringLiteral("enabled"), state.linear_mask_enabled);
      linear_mask.insert(QStringLiteral("exposure"), state.linear_mask_exposure);
      linear_mask.insert(QStringLiteral("temperature"), state.linear_mask_temperature);
      linear_mask.insert(QStringLiteral("feather"), state.linear_mask_feather);
      linear_mask.insert(QStringLiteral("position"), state.linear_mask_position);
      linear_mask.insert(QStringLiteral("rotation"), state.linear_mask_rotation);
      append_mask(linear_mask, 1);
    }

    if (has_structured_radial_mask(state)) {
      QVariantMap radial_mask = default_mask_definition(
          QStringLiteral("radial"), static_cast<int>(mask_stack.size()) + 1);
      radial_mask.insert(QStringLiteral("enabled"), state.radial_mask_enabled);
      radial_mask.insert(QStringLiteral("exposure"), state.radial_mask_exposure);
      radial_mask.insert(QStringLiteral("temperature"), state.radial_mask_temperature);
      radial_mask.insert(QStringLiteral("feather"), state.radial_mask_feather);
      radial_mask.insert(QStringLiteral("centerX"), state.radial_mask_center_x);
      radial_mask.insert(QStringLiteral("centerY"), state.radial_mask_center_y);
      radial_mask.insert(QStringLiteral("radiusX"), state.radial_mask_radius_x);
      radial_mask.insert(QStringLiteral("radiusY"), state.radial_mask_radius_y);
      radial_mask.insert(QStringLiteral("rotation"), state.radial_mask_rotation);
      append_mask(radial_mask, static_cast<int>(mask_stack.size()) + 1);
    }
  }

  QJsonObject guide_settings;
  guide_settings.insert(QStringLiteral("customGuides"), QJsonArray{});

  QJsonObject object;
  object.insert(QStringLiteral("recipeVersion"), 3);
  object.insert(QStringLiteral("globalAdjustments"), global_adjustments);
  object.insert(QStringLiteral("curves"), curves);
  object.insert(QStringLiteral("colorMixer"), color_mixer);
  object.insert(QStringLiteral("colorGrading"), color_grading);
  object.insert(QStringLiteral("cropTransform"), crop_transform);
  object.insert(QStringLiteral("optics"), optics);
  object.insert(QStringLiteral("maskStack"), mask_stack);
  object.insert(QStringLiteral("overlayItems"), QJsonArray{});
  object.insert(QStringLiteral("guideSettings"), guide_settings);
  return object;
}

inline QString serialize_edit_recipe_json(const EditRecipeState& state) {
  return QString::fromUtf8(QJsonDocument(build_edit_recipe_object(state)).toJson(QJsonDocument::Compact));
}

}  // namespace photo_editor::desktop

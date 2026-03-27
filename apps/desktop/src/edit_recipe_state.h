#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
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

}  // namespace detail

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

  state.crop_zoom = detail::read_double(
      crop_transform, QStringLiteral("zoom"), object, QStringLiteral("cropZoom"), 1.0);
  state.crop_preset = detail::read_string(
      crop_transform, QStringLiteral("preset"), object, QStringLiteral("cropPreset"), QStringLiteral("Original"));
  state.crop_rect_x = crop_transform.value(QStringLiteral("x")).toDouble();
  state.crop_rect_y = crop_transform.value(QStringLiteral("y")).toDouble();
  state.crop_rect_width = crop_transform.value(QStringLiteral("width")).toDouble();
  state.crop_rect_height = crop_transform.value(QStringLiteral("height")).toDouble();

  bool parsed_linear_mask = false;
  const auto mask_stack = object.value(QStringLiteral("maskStack")).toArray();
  for (const auto& value : mask_stack) {
    if (!value.isObject()) {
      continue;
    }

    const auto mask_object = value.toObject();
    const auto adjustments = detail::object_value(mask_object, QStringLiteral("adjustments"));
    const auto geometry = detail::object_value(mask_object, QStringLiteral("geometry"));
    const auto kind = mask_object.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("linear")) {
      state.linear_mask_enabled = mask_object.value(QStringLiteral("enabled")).toBool(true);
      state.linear_mask_exposure = adjustments.value(QStringLiteral("exposure")).toDouble();
      state.linear_mask_temperature = adjustments.value(QStringLiteral("temperature")).toDouble();
      state.linear_mask_feather = geometry.value(QStringLiteral("feather")).toDouble(0.22);
      state.linear_mask_position = geometry.value(QStringLiteral("position")).toDouble(0.5);
      state.linear_mask_rotation = geometry.value(QStringLiteral("rotation")).toDouble();
      parsed_linear_mask = true;
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
    }
  }

  if (!parsed_linear_mask) {
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
  }

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
  global_adjustments.insert(QStringLiteral("vibrance"), state.vibrance);
  global_adjustments.insert(QStringLiteral("saturation"), state.saturation);

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
  if (has_structured_linear_mask(state)) {
    QJsonObject adjustments;
    adjustments.insert(QStringLiteral("exposure"), state.linear_mask_exposure);
    adjustments.insert(QStringLiteral("temperature"), state.linear_mask_temperature);

    QJsonObject geometry;
    geometry.insert(QStringLiteral("feather"), state.linear_mask_feather);
    geometry.insert(QStringLiteral("position"), state.linear_mask_position);
    geometry.insert(QStringLiteral("rotation"), state.linear_mask_rotation);

    QJsonObject mask;
    mask.insert(QStringLiteral("id"), QStringLiteral("linear-mask-1"));
    mask.insert(QStringLiteral("kind"), QStringLiteral("linear"));
    mask.insert(QStringLiteral("enabled"), state.linear_mask_enabled);
    mask.insert(QStringLiteral("adjustments"), adjustments);
    mask.insert(QStringLiteral("geometry"), geometry);
    mask_stack.append(mask);
  }

  if (has_structured_radial_mask(state)) {
    QJsonObject adjustments;
    adjustments.insert(QStringLiteral("exposure"), state.radial_mask_exposure);
    adjustments.insert(QStringLiteral("temperature"), state.radial_mask_temperature);

    QJsonObject geometry;
    geometry.insert(QStringLiteral("feather"), state.radial_mask_feather);
    geometry.insert(QStringLiteral("centerX"), state.radial_mask_center_x);
    geometry.insert(QStringLiteral("centerY"), state.radial_mask_center_y);
    geometry.insert(QStringLiteral("radiusX"), state.radial_mask_radius_x);
    geometry.insert(QStringLiteral("radiusY"), state.radial_mask_radius_y);
    geometry.insert(QStringLiteral("rotation"), state.radial_mask_rotation);

    QJsonObject mask;
    mask.insert(QStringLiteral("id"), QStringLiteral("radial-mask-1"));
    mask.insert(QStringLiteral("kind"), QStringLiteral("radial"));
    mask.insert(QStringLiteral("enabled"), state.radial_mask_enabled);
    mask.insert(QStringLiteral("adjustments"), adjustments);
    mask.insert(QStringLiteral("geometry"), geometry);
    mask_stack.append(mask);
  }

  QJsonObject guide_settings;
  guide_settings.insert(QStringLiteral("customGuides"), QJsonArray{});

  QJsonObject object;
  object.insert(QStringLiteral("recipeVersion"), 3);
  object.insert(QStringLiteral("globalAdjustments"), global_adjustments);
  object.insert(QStringLiteral("curves"), curves);
  object.insert(QStringLiteral("colorMixer"), color_mixer);
  object.insert(QStringLiteral("colorGrading"), QJsonObject{});
  object.insert(QStringLiteral("cropTransform"), crop_transform);
  object.insert(QStringLiteral("maskStack"), mask_stack);
  object.insert(QStringLiteral("overlayItems"), QJsonArray{});
  object.insert(QStringLiteral("guideSettings"), guide_settings);
  return object;
}

inline QString serialize_edit_recipe_json(const EditRecipeState& state) {
  return QString::fromUtf8(QJsonDocument(build_edit_recipe_object(state)).toJson(QJsonDocument::Compact));
}

}  // namespace photo_editor::desktop

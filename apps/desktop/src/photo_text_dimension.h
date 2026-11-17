#pragma once

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {

// Editable, document-space dimensional text settings. Version 1 is a separate
// layer envelope so existing textTypography v1-v10 artwork keeps its meaning.
struct PhotoTextDimensionSettings final {
  enum class BevelProfile { Rounded, Chamfer };
  enum class Projection { Perspective, Orthographic };

  int version = 1;
  bool enabled = false;
  // Depth and focal length use document pixels.
  double depth = 32.0;
  double bevel_width = 2.0;
  int bevel_segments = 3;
  BevelProfile bevel_profile = BevelProfile::Rounded;
  double rotation_x = 0.0;
  double rotation_y = 0.0;
  double rotation_z = 0.0;
  double pivot_x = 0.5;
  double pivot_y = 0.5;
  Projection projection = Projection::Perspective;
  double focal_length = 1000.0;
  bool use_text_color = true;
  QColor front_color = QColor(Qt::white);
  QColor side_color = QColor(QStringLiteral("#FFB8BEC8"));
  QColor bevel_color = QColor(QStringLiteral("#FFE7EAF0"));
  double roughness = 0.4;
  double metallic = 0.0;
  double light_azimuth = -45.0;
  double light_elevation = 45.0;
  double light_intensity = 1.0;
  double ambient = 0.25;
  bool corner_pin_enabled = false;
  // TL, TR, BR, BL in normalized layer coordinates.
  std::array<QPointF, 4> corner_pins{
      QPointF(0.0, 0.0), QPointF(1.0, 0.0),
      QPointF(1.0, 1.0), QPointF(0.0, 1.0)};

  bool operator==(const PhotoTextDimensionSettings&) const = default;
};

namespace photo_text_dimension_detail {

inline bool exact_keys(const QJsonObject& object,
                       const QStringList& expected) {
  if (object.size() != expected.size()) return false;
  for (const auto& key : expected)
    if (!object.contains(key)) return false;
  return true;
}

inline bool finite_number(const QJsonValue& value, double* output) {
  if (!value.isDouble()) return false;
  const auto number = value.toDouble();
  if (!std::isfinite(number)) return false;
  if (output != nullptr) *output = number;
  return true;
}

inline bool boolean(const QJsonValue& value, bool* output) {
  if (!value.isBool()) return false;
  if (output != nullptr) *output = value.toBool();
  return true;
}

inline bool integer(const QJsonValue& value, int minimum, int maximum,
                    int* output) {
  double number = 0.0;
  if (!finite_number(value, &number) || std::floor(number) != number ||
      number < minimum || number > maximum)
    return false;
  if (output != nullptr) *output = static_cast<int>(number);
  return true;
}

inline bool bounded_number(const QJsonValue& value, double minimum,
                           double maximum, double* output) {
  double number = 0.0;
  if (!finite_number(value, &number) || number < minimum || number > maximum)
    return false;
  if (output != nullptr) *output = number;
  return true;
}

inline bool color(const QJsonValue& value, QColor* output) {
  if (!value.isString()) return false;
  const QColor parsed(value.toString());
  if (!parsed.isValid()) return false;
  if (output != nullptr) *output = parsed;
  return true;
}

inline double cross(const QPointF& a, const QPointF& b, const QPointF& c) {
  return (b.x() - a.x()) * (c.y() - b.y()) -
      (b.y() - a.y()) * (c.x() - b.x());
}

inline double signed_area(const std::array<QPointF, 4>& points) {
  double area = 0.0;
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto& current = points[index];
    const auto& next = points[(index + 1) % points.size()];
    area += current.x() * next.y() - next.x() * current.y();
  }
  return area * 0.5;
}

inline bool valid_corner_quad(const std::array<QPointF, 4>& points) {
  constexpr double kEpsilon = 1.0e-9;
  double orientation = 0.0;
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto& point = points[index];
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
        point.x() < -4.0 || point.x() > 4.0 ||
        point.y() < -4.0 || point.y() > 4.0)
      return false;
    const auto turn = cross(points[index], points[(index + 1) % 4],
                            points[(index + 2) % 4]);
    if (std::abs(turn) <= kEpsilon) return false;
    if (index == 0) orientation = turn;
    else if ((turn > 0.0) != (orientation > 0.0)) return false;
  }
  // A strictly convex, consistently ordered quad cannot self-intersect, but
  // keep the area check explicit so collapsed and near-collinear quads fail.
  return std::abs(signed_area(points)) > kEpsilon;
}

inline QJsonArray encode_corner_pins(
    const std::array<QPointF, 4>& points) {
  QJsonArray result;
  for (const auto& point : points)
    result.append(QJsonObject{{QStringLiteral("x"), point.x()},
                              {QStringLiteral("y"), point.y()}});
  return result;
}

inline bool valid_settings(const PhotoTextDimensionSettings& settings) {
  const auto in_range = [](double value, double minimum, double maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
  };
  return settings.version == 1 && in_range(settings.depth, 0.0, 8192.0) &&
      in_range(settings.bevel_width, 0.0, 1024.0) &&
      settings.bevel_segments >= 1 && settings.bevel_segments <= 32 &&
      (settings.bevel_profile == PhotoTextDimensionSettings::BevelProfile::Rounded ||
       settings.bevel_profile == PhotoTextDimensionSettings::BevelProfile::Chamfer) &&
      in_range(settings.rotation_x, -3600.0, 3600.0) &&
      in_range(settings.rotation_y, -3600.0, 3600.0) &&
      in_range(settings.rotation_z, -3600.0, 3600.0) &&
      in_range(settings.pivot_x, 0.0, 1.0) &&
      in_range(settings.pivot_y, 0.0, 1.0) &&
      (settings.projection == PhotoTextDimensionSettings::Projection::Perspective ||
       settings.projection == PhotoTextDimensionSettings::Projection::Orthographic) &&
      in_range(settings.focal_length, 1.0, 1000000.0) &&
      settings.front_color.isValid() && settings.side_color.isValid() &&
      settings.bevel_color.isValid() &&
      in_range(settings.roughness, 0.0, 1.0) &&
      in_range(settings.metallic, 0.0, 1.0) &&
      in_range(settings.light_azimuth, -3600.0, 3600.0) &&
      in_range(settings.light_elevation, -90.0, 90.0) &&
      in_range(settings.light_intensity, 0.0, 64.0) &&
      in_range(settings.ambient, 0.0, 1.0) &&
      valid_corner_quad(settings.corner_pins);
}

}  // namespace photo_text_dimension_detail

inline QJsonObject encode_photo_text_dimension(
    const PhotoTextDimensionSettings& settings) {
  if (!photo_text_dimension_detail::valid_settings(settings)) return {};
  QJsonArray pins = photo_text_dimension_detail::encode_corner_pins(
      settings.corner_pins);
  return QJsonObject{
      {QStringLiteral("version"), settings.version},
      {QStringLiteral("enabled"), settings.enabled},
      {QStringLiteral("depth"), settings.depth},
      {QStringLiteral("bevelWidth"), settings.bevel_width},
      {QStringLiteral("bevelSegments"), settings.bevel_segments},
      {QStringLiteral("bevelProfile"),
       settings.bevel_profile == PhotoTextDimensionSettings::BevelProfile::Rounded
           ? QStringLiteral("rounded")
           : QStringLiteral("chamfer")},
      {QStringLiteral("rotationX"), settings.rotation_x},
      {QStringLiteral("rotationY"), settings.rotation_y},
      {QStringLiteral("rotationZ"), settings.rotation_z},
      {QStringLiteral("pivotX"), settings.pivot_x},
      {QStringLiteral("pivotY"), settings.pivot_y},
      {QStringLiteral("projection"),
       settings.projection == PhotoTextDimensionSettings::Projection::Perspective
           ? QStringLiteral("perspective")
           : QStringLiteral("orthographic")},
      {QStringLiteral("focalLength"), settings.focal_length},
      {QStringLiteral("useTextColor"), settings.use_text_color},
      {QStringLiteral("frontColor"),
       settings.front_color.name(QColor::HexArgb)},
      {QStringLiteral("sideColor"), settings.side_color.name(QColor::HexArgb)},
      {QStringLiteral("bevelColor"), settings.bevel_color.name(QColor::HexArgb)},
      {QStringLiteral("roughness"), settings.roughness},
      {QStringLiteral("metallic"), settings.metallic},
      {QStringLiteral("lightAzimuth"), settings.light_azimuth},
      {QStringLiteral("lightElevation"), settings.light_elevation},
      {QStringLiteral("lightIntensity"), settings.light_intensity},
      {QStringLiteral("ambient"), settings.ambient},
      {QStringLiteral("cornerPinEnabled"), settings.corner_pin_enabled},
      {QStringLiteral("cornerPins"), pins}};
}

inline QVariantMap encode_photo_text_dimension_variant_map(
    const PhotoTextDimensionSettings& settings) {
  return encode_photo_text_dimension(settings).toVariantMap();
}

inline std::optional<PhotoTextDimensionSettings> decode_photo_text_dimension(
    const QJsonObject& object, QString* error = nullptr) {
  if (error != nullptr) error->clear();
  const auto fail = [error](const QString& message)
      -> std::optional<PhotoTextDimensionSettings> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  static const QStringList keys{
      QStringLiteral("version"), QStringLiteral("enabled"),
      QStringLiteral("depth"), QStringLiteral("bevelWidth"),
      QStringLiteral("bevelSegments"), QStringLiteral("bevelProfile"),
      QStringLiteral("rotationX"), QStringLiteral("rotationY"),
      QStringLiteral("rotationZ"), QStringLiteral("pivotX"),
      QStringLiteral("pivotY"), QStringLiteral("projection"),
      QStringLiteral("focalLength"), QStringLiteral("useTextColor"),
      QStringLiteral("frontColor"), QStringLiteral("sideColor"),
      QStringLiteral("bevelColor"), QStringLiteral("roughness"),
      QStringLiteral("metallic"), QStringLiteral("lightAzimuth"),
      QStringLiteral("lightElevation"), QStringLiteral("lightIntensity"),
      QStringLiteral("ambient"), QStringLiteral("cornerPinEnabled"),
      QStringLiteral("cornerPins")};
  const bool has_optional_pins = object.contains(QStringLiteral("cornerPins"));
  if ((has_optional_pins &&
       !photo_text_dimension_detail::exact_keys(object, keys)) ||
      (!has_optional_pins && object.size() != keys.size() - 1))
    return fail(QStringLiteral(
        "Text dimension settings must contain exactly the version 1 fields."));
  if (!has_optional_pins) {
    auto required_keys = keys;
    required_keys.removeAll(QStringLiteral("cornerPins"));
    for (const auto& key : required_keys)
      if (!object.contains(key))
        return fail(QStringLiteral(
            "Text dimension settings are missing a required version 1 field."));
  }

  PhotoTextDimensionSettings settings;
  if (!photo_text_dimension_detail::integer(object.value(QStringLiteral("version")),
                                            1, 1, &settings.version))
    return fail(QStringLiteral("Unsupported text dimension settings version."));
  if (!photo_text_dimension_detail::boolean(object.value(QStringLiteral("enabled")),
                                            &settings.enabled) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("depth")),
                                                   0.0, 8192.0, &settings.depth) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("bevelWidth")),
                                                   0.0, 1024.0, &settings.bevel_width) ||
      !photo_text_dimension_detail::integer(object.value(QStringLiteral("bevelSegments")),
                                            1, 32, &settings.bevel_segments))
    return fail(QStringLiteral(
        "Text dimension depth, bevel, or enabled value has an invalid type or range."));

  const auto bevel_profile = object.value(QStringLiteral("bevelProfile"));
  if (!bevel_profile.isString())
    return fail(QStringLiteral("bevelProfile must be a supported string."));
  if (bevel_profile.toString() == QLatin1String("rounded"))
    settings.bevel_profile = PhotoTextDimensionSettings::BevelProfile::Rounded;
  else if (bevel_profile.toString() == QLatin1String("chamfer"))
    settings.bevel_profile = PhotoTextDimensionSettings::BevelProfile::Chamfer;
  else
    return fail(QStringLiteral("Unsupported text dimension bevel profile."));

  if (!photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("rotationX")),
                                                   -3600.0, 3600.0, &settings.rotation_x) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("rotationY")),
                                                   -3600.0, 3600.0, &settings.rotation_y) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("rotationZ")),
                                                   -3600.0, 3600.0, &settings.rotation_z) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("pivotX")),
                                                   0.0, 1.0, &settings.pivot_x) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("pivotY")),
                                                   0.0, 1.0, &settings.pivot_y))
    return fail(QStringLiteral(
        "Text dimension rotation or normalized pivot has an invalid type or range."));

  const auto projection = object.value(QStringLiteral("projection"));
  if (!projection.isString())
    return fail(QStringLiteral("projection must be a supported string."));
  if (projection.toString() == QLatin1String("perspective"))
    settings.projection = PhotoTextDimensionSettings::Projection::Perspective;
  else if (projection.toString() == QLatin1String("orthographic"))
    settings.projection = PhotoTextDimensionSettings::Projection::Orthographic;
  else
    return fail(QStringLiteral("Unsupported text dimension projection."));

  if (!photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("focalLength")),
                                                   1.0, 1000000.0, &settings.focal_length) ||
      !photo_text_dimension_detail::boolean(object.value(QStringLiteral("useTextColor")),
                                            &settings.use_text_color) ||
      !photo_text_dimension_detail::color(object.value(QStringLiteral("frontColor")),
                                          &settings.front_color) ||
      !photo_text_dimension_detail::color(object.value(QStringLiteral("sideColor")),
                                          &settings.side_color) ||
      !photo_text_dimension_detail::color(object.value(QStringLiteral("bevelColor")),
                                          &settings.bevel_color))
    return fail(QStringLiteral(
        "Text dimension projection or front, side, or bevel color is invalid."));

  if (!photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("roughness")),
                                                   0.0, 1.0, &settings.roughness) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("metallic")),
                                                   0.0, 1.0, &settings.metallic) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("lightAzimuth")),
                                                   -3600.0, 3600.0, &settings.light_azimuth) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("lightElevation")),
                                                   -90.0, 90.0, &settings.light_elevation) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("lightIntensity")),
                                                   0.0, 64.0, &settings.light_intensity) ||
      !photo_text_dimension_detail::bounded_number(object.value(QStringLiteral("ambient")),
                                                   0.0, 1.0, &settings.ambient) ||
      !photo_text_dimension_detail::boolean(object.value(QStringLiteral("cornerPinEnabled")),
                                            &settings.corner_pin_enabled))
    return fail(QStringLiteral(
        "Text dimension material or lighting values have an invalid type or range."));

  if (has_optional_pins) {
    const auto pin_array = object.value(QStringLiteral("cornerPins"));
    if (!pin_array.isArray() || pin_array.toArray().size() != 4)
      return fail(QStringLiteral("cornerPins must contain exactly four points."));
    const auto points = pin_array.toArray();
    for (qsizetype index = 0; index < 4; ++index) {
      if (!points.at(index).isObject())
        return fail(QStringLiteral("Each cornerPins entry must be an {x,y} object."));
      const auto point = points.at(index).toObject();
      if (!photo_text_dimension_detail::exact_keys(
              point, {QStringLiteral("x"), QStringLiteral("y")}) ||
          !photo_text_dimension_detail::finite_number(
              point.value(QStringLiteral("x")), nullptr) ||
          !photo_text_dimension_detail::finite_number(
              point.value(QStringLiteral("y")), nullptr))
        return fail(QStringLiteral(
            "Each cornerPins point must contain exactly finite numeric x and y fields."));
      double x = 0.0;
      double y = 0.0;
      photo_text_dimension_detail::finite_number(point.value(QStringLiteral("x")), &x);
      photo_text_dimension_detail::finite_number(point.value(QStringLiteral("y")), &y);
      settings.corner_pins[static_cast<std::size_t>(index)] = QPointF(x, y);
    }
  }
  if (!photo_text_dimension_detail::valid_corner_quad(settings.corner_pins))
    return fail(QStringLiteral(
        "cornerPins must form a convex, non-self-intersecting quad within [-4,4]."));
  return settings;
}

inline std::optional<PhotoTextDimensionSettings>
decode_photo_text_dimension_variant_map(const QVariantMap& map,
                                        QString* error = nullptr) {
  return decode_photo_text_dimension(QJsonObject::fromVariantMap(map), error);
}

inline bool photo_text_dimensions_present_in_layers(
    const QVariantList& layers, int depth = 0);

inline bool validate_photo_text_dimensions_in_layers(
    const QVariantList& layers, QString* error = nullptr, int depth = 0) {
  if (depth > 72) {
    if (error != nullptr)
      *error = QStringLiteral("Text dimension layer nesting exceeds the supported limit.");
    return false;
  }
  for (qsizetype index = 0; index < layers.size(); ++index) {
    if (layers.at(index).metaType().id() != QMetaType::QVariantMap) continue;
    const auto layer = layers.at(index).toMap();
    if (layer.contains(QStringLiteral("textDimension"))) {
      const auto context = !layer.value(QStringLiteral("id")).toString().trimmed().isEmpty()
          ? layer.value(QStringLiteral("id")).toString().trimmed()
          : QString::number(index);
      if (layer.value(QStringLiteral("kind")).toString().trimmed().toLower() !=
          QStringLiteral("text")) {
        if (error != nullptr)
          *error = QStringLiteral("Layer %1 textDimension is only valid on a text layer.").arg(context);
        return false;
      }
      if (layer.value(QStringLiteral("textDimension")).metaType().id() !=
          QMetaType::QVariantMap) {
        if (error != nullptr)
          *error = QStringLiteral("Layer %1 textDimension must be an object.").arg(context);
        return false;
      }
      QString decode_error;
      if (!decode_photo_text_dimension_variant_map(
              layer.value(QStringLiteral("textDimension")).toMap(),
              &decode_error)) {
        if (error != nullptr)
          *error = QStringLiteral("Layer %1 textDimension: %2")
                       .arg(context, decode_error);
        return false;
      }
    }
    for (const auto& key : {QStringLiteral("children"), QStringLiteral("layers")}) {
      const auto children_value = layer.value(key);
      if (!children_value.isValid()) continue;
      if (children_value.metaType().id() != QMetaType::QVariantList) {
        if (error != nullptr)
          *error = QStringLiteral("Layer %1 %2 must be a list.").arg(
              layer.value(QStringLiteral("id")).toString(), key);
        return false;
      }
      if (!validate_photo_text_dimensions_in_layers(
              children_value.toList(), error, depth + 1))
        return false;
    }
    const auto nested_value = layer.value(QStringLiteral("smartObjectDocument"));
    if (nested_value.isValid()) {
      if (nested_value.metaType().id() != QMetaType::QVariantMap) {
        if (error != nullptr)
          *error = QStringLiteral("Layer %1 smartObjectDocument must be an object.")
                       .arg(layer.value(QStringLiteral("id")).toString());
        return false;
      }
      const auto nested = nested_value.toMap();
      const auto nested_layers = nested.value(QStringLiteral("layers"));
      if (nested_layers.metaType().id() != QMetaType::QVariantList) {
        if (error != nullptr)
          *error = QStringLiteral("Layer %1 smartObjectDocument.layers must be a list.")
                       .arg(layer.value(QStringLiteral("id")).toString());
        return false;
      }
      if (!validate_photo_text_dimensions_in_layers(
              nested_layers.toList(), error, depth + 1))
        return false;
      const bool nested_has_dimensions = photo_text_dimensions_present_in_layers(
          nested_layers.toList(), depth + 1);
      const auto nested_version = nested.value(QStringLiteral("version"));
      const auto version_type = nested_version.metaType().id();
      const bool numeric_version = version_type == QMetaType::Int ||
          version_type == QMetaType::UInt ||
          version_type == QMetaType::LongLong ||
          version_type == QMetaType::ULongLong ||
          version_type == QMetaType::Double;
      bool version_ok = false;
      const auto version_number = nested_version.toDouble(&version_ok);
      if (!numeric_version || !version_ok || !std::isfinite(version_number) ||
          std::floor(version_number) != version_number ||
          (nested_has_dimensions && version_number != 3.0) ||
          (!nested_has_dimensions && version_number == 3.0)) {
        if (error != nullptr)
          *error = QStringLiteral(
              "Layer %1 smartObjectDocument must negotiate version 3 exactly when it contains dimensional text.")
                       .arg(layer.value(QStringLiteral("id")).toString());
        return false;
      }
    }
  }
  if (error != nullptr) error->clear();
  return true;
}

inline bool photo_text_dimensions_present_in_layers(
    const QVariantList& layers, int depth) {
  if (depth > 72) return false;
  for (const auto& value : layers) {
    if (value.metaType().id() != QMetaType::QVariantMap) continue;
    const auto layer = value.toMap();
    if (layer.contains(QStringLiteral("textDimension"))) return true;
    for (const auto& key : {QStringLiteral("children"), QStringLiteral("layers")})
      if (photo_text_dimensions_present_in_layers(
              layer.value(key).toList(), depth + 1))
        return true;
    const auto nested = layer.value(QStringLiteral("smartObjectDocument")).toMap();
    if (!nested.isEmpty() && photo_text_dimensions_present_in_layers(
            nested.value(QStringLiteral("layers")).toList(), depth + 1))
      return true;
  }
  return false;
}

}  // namespace photo_editor::desktop

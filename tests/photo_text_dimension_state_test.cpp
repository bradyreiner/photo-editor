#include "photo_text_dimension.h"

#include <QJsonDocument>

#include <iostream>
#include <limits>

#define DIMENSION_FAILURE() \
  (std::cerr << "Text dimension assertion failed at line " << __LINE__ << '\n', 1)

int main() {
  using namespace photo_editor::desktop;

  const PhotoTextDimensionSettings defaults;
  const auto encoded_defaults = encode_photo_text_dimension(defaults);
  const auto decoded_defaults = decode_photo_text_dimension(
      QJsonDocument::fromJson(
          QJsonDocument(encoded_defaults).toJson(QJsonDocument::Compact))
          .object());
  if (!decoded_defaults || *decoded_defaults != defaults ||
      decoded_defaults->enabled || decoded_defaults->depth != 32.0 ||
      decoded_defaults->bevel_width != 2.0 ||
      decoded_defaults->bevel_segments != 3 ||
      decoded_defaults->projection != PhotoTextDimensionSettings::Projection::Perspective ||
      decoded_defaults->focal_length != 1000.0 ||
      decoded_defaults->use_text_color != true ||
      decoded_defaults->corner_pin_enabled ||
      decoded_defaults->corner_pins != defaults.corner_pins) {
    return DIMENSION_FAILURE();
  }

  auto custom = defaults;
  custom.enabled = true;
  custom.depth = 81.25;
  custom.bevel_width = 5.5;
  custom.bevel_segments = 8;
  custom.bevel_profile = PhotoTextDimensionSettings::BevelProfile::Chamfer;
  custom.rotation_x = -27.5;
  custom.rotation_y = 12.25;
  custom.rotation_z = 90.0;
  custom.pivot_x = 0.25;
  custom.pivot_y = 0.75;
  custom.projection = PhotoTextDimensionSettings::Projection::Orthographic;
  custom.focal_length = 725.5;
  custom.use_text_color = false;
  custom.front_color = QColor(QStringLiteral("#FFEECCAA"));
  custom.side_color = QColor(QStringLiteral("#FF445566"));
  custom.bevel_color = QColor(QStringLiteral("#FFABCDEF"));
  custom.roughness = 0.62;
  custom.metallic = 0.31;
  custom.light_azimuth = 312.5;
  custom.light_elevation = -23.0;
  custom.light_intensity = 1.75;
  custom.ambient = 0.18;
  custom.corner_pin_enabled = true;
  custom.corner_pins = {QPointF(0.1, 0.05), QPointF(0.95, 0.2),
                        QPointF(0.8, 0.9), QPointF(0.15, 0.8)};
  const auto encoded_custom = encode_photo_text_dimension(custom);
  const auto decoded_custom = decode_photo_text_dimension(encoded_custom);
  if (!decoded_custom || *decoded_custom != custom) return DIMENSION_FAILURE();

  auto omitted_optional_pins = encoded_defaults;
  omitted_optional_pins.remove(QStringLiteral("cornerPins"));
  const auto decoded_without_pins =
      decode_photo_text_dimension(omitted_optional_pins);
  if (!decoded_without_pins ||
      decoded_without_pins->corner_pins != defaults.corner_pins ||
      !encode_photo_text_dimension(*decoded_without_pins).contains(
          QStringLiteral("cornerPins"))) {
    return DIMENSION_FAILURE();
  }
  auto invalid_typed_settings = defaults;
  invalid_typed_settings.depth = std::numeric_limits<double>::infinity();
  if (!encode_photo_text_dimension(invalid_typed_settings).isEmpty())
    return DIMENSION_FAILURE();

  const auto variant = encode_photo_text_dimension_variant_map(custom);
  const auto decoded_variant = decode_photo_text_dimension_variant_map(variant);
  if (!decoded_variant || *decoded_variant != custom) return DIMENSION_FAILURE();

  const auto expect_rejected = [](QJsonObject invalid) {
    QString error;
    const auto decoded = decode_photo_text_dimension(invalid, &error);
    return !decoded && !error.isEmpty();
  };
  for (const auto& pair : {
           qMakePair(QStringLiteral("version"), QJsonValue(2)),
           qMakePair(QStringLiteral("enabled"), QJsonValue(1)),
           qMakePair(QStringLiteral("depth"), QJsonValue(QStringLiteral("32"))),
           qMakePair(QStringLiteral("depth"), QJsonValue(-1)),
           qMakePair(QStringLiteral("bevelWidth"), QJsonValue(1025)),
           qMakePair(QStringLiteral("bevelSegments"), QJsonValue(3.5)),
           qMakePair(QStringLiteral("bevelProfile"), QJsonValue(QStringLiteral("round"))),
           qMakePair(QStringLiteral("rotationX"), QJsonValue(3601)),
           qMakePair(QStringLiteral("pivotY"), QJsonValue(1.01)),
           qMakePair(QStringLiteral("projection"), QJsonValue(QStringLiteral("fisheye"))),
           qMakePair(QStringLiteral("focalLength"), QJsonValue(0)),
           qMakePair(QStringLiteral("useTextColor"), QJsonValue(QStringLiteral("true"))),
           qMakePair(QStringLiteral("frontColor"), QJsonValue(QStringLiteral("invalid"))),
           qMakePair(QStringLiteral("roughness"), QJsonValue(1.01)),
           qMakePair(QStringLiteral("metallic"), QJsonValue(-0.01)),
           qMakePair(QStringLiteral("lightElevation"), QJsonValue(90.01)),
           qMakePair(QStringLiteral("lightIntensity"), QJsonValue(64.01)),
           qMakePair(QStringLiteral("ambient"), QJsonValue(-0.1)),
           qMakePair(QStringLiteral("cornerPinEnabled"), QJsonValue(0))}) {
    auto invalid = encoded_custom;
    invalid.insert(pair.first, pair.second);
    if (!expect_rejected(invalid)) return DIMENSION_FAILURE();
  }

  auto unknown = encoded_custom;
  unknown.insert(QStringLiteral("futureDimensionMode"), true);
  if (!expect_rejected(unknown)) return DIMENSION_FAILURE();
  auto missing = encoded_custom;
  missing.remove(QStringLiteral("bevelColor"));
  if (!expect_rejected(missing)) return DIMENSION_FAILURE();

  auto bad_pins = encoded_custom;
  bad_pins.insert(QStringLiteral("cornerPins"), QJsonArray{
      QJsonObject{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QJsonObject{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
      QJsonObject{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 1.0}},
      QJsonObject{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.0}}});
  if (!expect_rejected(bad_pins)) return DIMENSION_FAILURE();
  bad_pins = encoded_custom;
  bad_pins.insert(QStringLiteral("cornerPins"), QJsonArray{
      QJsonObject{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QJsonObject{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.0}},
      QJsonObject{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}}});
  if (!expect_rejected(bad_pins)) return DIMENSION_FAILURE();
  bad_pins = encoded_custom;
  bad_pins.insert(QStringLiteral("cornerPins"), QJsonArray{
      QJsonObject{{QStringLiteral("x"), -4.01}, {QStringLiteral("y"), 0.0}},
      QJsonObject{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.0}},
      QJsonObject{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
      QJsonObject{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 1.0}}});
  if (!expect_rejected(bad_pins)) return DIMENSION_FAILURE();
  bad_pins = encoded_custom;
  bad_pins.insert(QStringLiteral("cornerPins"), QJsonArray{
      QJsonObject{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0},
                  {QStringLiteral("z"), 0.0}},
      QJsonObject{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.0}},
      QJsonObject{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
      QJsonObject{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 1.0}}});
  if (!expect_rejected(bad_pins)) return DIMENSION_FAILURE();

  // The serialized envelope is independent of the old typography envelope.
  auto legacy_style = QJsonObject{{QStringLiteral("version"), 10},
                                  {QStringLiteral("family"), QStringLiteral("Helvetica")}};
  if (!decode_photo_text_dimension(encoded_custom) ||
      encoded_custom.value(QStringLiteral("version")).toInt() != 1 ||
      legacy_style.value(QStringLiteral("version")).toInt() != 10) {
    return DIMENSION_FAILURE();
  }
  return 0;
}

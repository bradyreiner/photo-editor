#include "photo_text_style.h"
#include <QJsonDocument>
#include <iostream>

int main() {
  using namespace photo_editor::desktop;
  PhotoTextStyle style;
  style.family = QString::fromUtf8("Noto Sans 日本語");
  style.face = "Italic";
  style.size = 27.375;
  style.weight = 650;
  style.italic = true;
  style.underline = true;
  style.tracking = -0.375;
  style.line_height = 1.45;
  style.baseline_offset = 3.25;
  style.color = QColor("#7fabcdef");
  style.axes.insert(QStringLiteral("wdth"), 112.5);
  style.axes.insert(QStringLiteral("opsz"), 18.25);
  const auto encoded = encode_photo_text_style(style);
  const auto restored = decode_photo_text_style(QJsonDocument::fromJson(QJsonDocument(encoded).toJson()).object());
  if (!restored || *restored != style) return 1;
  for (const auto& pair : {qMakePair(QString("version"), QJsonValue(2)),
      qMakePair(QString("weight"), QJsonValue(650.5)),
      qMakePair(QString("size"), QJsonValue("27")),
      qMakePair(QString("size"), QJsonValue(0)),
      qMakePair(QString("lineHeight"), QJsonValue(-1)),
      qMakePair(QString("color"), QJsonValue("invalid")),
      qMakePair(QString("italic"), QJsonValue(1)),
      qMakePair(QString("futureField"), QJsonValue(true))}) {
    auto invalid = encoded;
    invalid.insert(pair.first, pair.second);
    QString error;
    if (decode_photo_text_style(invalid, &error) || error.isEmpty()) {
      std::cerr << "Invalid or unsupported style must fail explicitly\n";
      return 1;
    }
  }
  style.axes.clear();
  style.point_text = true;
  style.paragraph_spacing = 17.25;
  const auto point = encode_photo_text_style(style);
  const auto restored_point = decode_photo_text_style(point);
  if (point.value("version").toInt() != 2 || !restored_point || *restored_point != style) return 1;
  for (const auto& pair : {qMakePair(QString("mode"), QJsonValue("unknown")),
      qMakePair(QString("paragraphSpacing"), QJsonValue(-1)),
      qMakePair(QString("paragraphSpacing"), QJsonValue("12")),
      qMakePair(QString("version"), QJsonValue(3))}) {
    auto invalid = point;
    invalid.insert(pair.first, pair.second);
    if (decode_photo_text_style(invalid)) return 1;
  }
  auto axis_style = style;
  axis_style.axes.insert(QStringLiteral("wdth"), 112.5);
  const auto valid_axes = encode_photo_text_style(axis_style);
  if (valid_axes.value("version").toInt() != 4 || !decode_photo_text_style(valid_axes)) return 1;
  for (const auto& invalid_axes : {QJsonObject{{"axes", QJsonObject{{"width", 100}}}},
      QJsonObject{{"axes", QJsonObject{{"wdth", "wide"}}}},
      QJsonObject{{"axes", QJsonObject{{"wdth", 1e20}}}}}) {
    auto invalid = valid_axes;
    invalid.insert("axes", invalid_axes.value("axes"));
    if (decode_photo_text_style(invalid)) return 1;
  }
  auto contour_style = axis_style;
  contour_style.path_points = {QPointF(0.05,0.5),QPointF(0.35,0.25),QPointF(0.7,0.75),QPointF(0.95,0.5)};
  contour_style.path_start = 0.1;
  contour_style.path_end = 0.9;
  contour_style.path_offset = -12.5;
  contour_style.path_spacing = 1.25;
  contour_style.path_side = QStringLiteral("right");
  contour_style.path_closed = false;
  const auto valid_contour = encode_photo_text_style(contour_style);
  const auto restored_contour = decode_photo_text_style(valid_contour);
  if (valid_contour.value("version").toInt() != 5 || !restored_contour || *restored_contour != contour_style) return 1;
  for (const auto& invalid_field : {qMakePair(QStringLiteral("pathStart"), QJsonValue(-0.1)),
      qMakePair(QStringLiteral("pathEnd"), QJsonValue(0.05)),
      qMakePair(QStringLiteral("pathSide"), QJsonValue("up")),
      qMakePair(QStringLiteral("pathPoints"), QJsonValue(QJsonArray{QJsonObject{{"x", -1}, {"y", 0.5}}}))}) {
    auto invalid = valid_contour;
    invalid.insert(invalid_field.first, invalid_field.second);
    if (decode_photo_text_style(invalid)) return 1;
  }
  auto exclusion_style = contour_style;
  exclusion_style.point_text = false;
  exclusion_style.path_closed = true;
  exclusion_style.path_exclusion = true;
  const auto encoded_exclusion = encode_photo_text_style(exclusion_style);
  const auto decoded_exclusion = decode_photo_text_style(encoded_exclusion);
  if (encoded_exclusion.value("version").toInt() != 6 || !decoded_exclusion ||
      *decoded_exclusion != exclusion_style) return 1;
  auto invalid_exclusion = encoded_exclusion;
  invalid_exclusion.insert("pathClosed", false);
  if (decode_photo_text_style(invalid_exclusion)) return 1;
  exclusion_style.path_contours = {
      {QPointF(0.2,0.2), QPointF(0.8,0.2), QPointF(0.8,0.8), QPointF(0.2,0.8)},
      {QPointF(0.4,0.4), QPointF(0.7,0.4), QPointF(0.7,0.6), QPointF(0.4,0.6)}};
  const auto encoded_contours = encode_photo_text_style(exclusion_style);
  const auto decoded_contours = decode_photo_text_style(encoded_contours);
  if (encoded_contours.value("version").toInt() != 7 || !decoded_contours ||
      *decoded_contours != exclusion_style) return 1;
  exclusion_style.material = QStringLiteral("frosted_glass");
  exclusion_style.material_tint = QColor(QStringLiteral("#caa4bfd1"));
  exclusion_style.material_blur = 12.5;
  exclusion_style.material_transmission = 0.72;
  exclusion_style.material_edge_softness = 2.25;
  exclusion_style.material_depth = 7.5;
  exclusion_style.material_light_angle = 38;
  const auto encoded_material = encode_photo_text_style(exclusion_style);
  const auto decoded_material = decode_photo_text_style(encoded_material);
  if (encoded_material.value("version").toInt() != 8 || !decoded_material ||
      *decoded_material != exclusion_style) return 1;
  for (const auto& metal : photo_text_metal_material_ids()) {
    auto metal_style = exclusion_style;
    metal_style.material = metal;
    metal_style.material_tint = QColor(QStringLiteral("#c9c7a56e"));
    metal_style.material_transmission = 0.83;
    metal_style.material_edge_softness = 1.35;
    metal_style.material_depth = 9.25;
    metal_style.material_light_angle = 27.0;
    const auto encoded_metal = encode_photo_text_style(metal_style);
    const auto decoded_metal = decode_photo_text_style(encoded_metal);
    if (encoded_metal.value("version").toInt() != 9 || !decoded_metal || *decoded_metal != metal_style) {
      std::cerr << "Metal typography material must retain its editable color, reflection, bevel and light fields: "
          << metal.toStdString() << '\n';
      return 1;
    }
    auto legacy_metal = encoded_metal;
    legacy_metal.insert("version", 8);
    if (decode_photo_text_style(legacy_metal)) return 1;
  }
  auto plain_material = PhotoTextStyle{};
  plain_material.material = QStringLiteral("smoked_plastic");
  const auto plain_material_recipe = encode_photo_text_style(plain_material);
  const auto decoded_plain_material = decode_photo_text_style(plain_material_recipe);
  if (!decoded_plain_material || *decoded_plain_material != plain_material) return 1;
  for (const auto& invalid_field : {qMakePair(QStringLiteral("material"), QJsonValue("unsupported")),
      qMakePair(QStringLiteral("materialBlur"), QJsonValue(128)),
      qMakePair(QStringLiteral("materialTransmission"), QJsonValue(-0.1)),
      qMakePair(QStringLiteral("materialTint"), QJsonValue("bad-color"))}) {
    auto invalid = encoded_material;
    invalid.insert(invalid_field.first, invalid_field.second);
    if (decode_photo_text_style(invalid)) return 1;
  }
  auto invalid_metal = encode_photo_text_style(exclusion_style);
  invalid_metal.insert("version", 9);
  invalid_metal.insert("material", "polished_plastic");
  if (decode_photo_text_style(invalid_metal)) return 1;
  style.point_text = false;
  style.paragraph_spacing = 0;
  if (encode_photo_text_style(style).value("version").toInt() != 1) return 1;
  auto missing = encoded;
  missing.remove("family");
  return decode_photo_text_style(missing) ? 1 : 0;
}

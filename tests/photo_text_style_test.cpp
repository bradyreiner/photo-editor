#include "photo_text_style.h"
#include <QJsonDocument>
#include <iostream>

#define STYLE_FAILURE() (std::cerr << "Text style assertion failed at line " << __LINE__ << '\n', 1)

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
  if (!restored || *restored != style) return STYLE_FAILURE();
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
      return STYLE_FAILURE();
    }
  }
  style.axes.clear();
  style.point_text = true;
  style.paragraph_spacing = 17.25;
  const auto point = encode_photo_text_style(style);
  const auto restored_point = decode_photo_text_style(point);
  if (point.value("version").toInt() != 2 || !restored_point || *restored_point != style) return STYLE_FAILURE();
  for (const auto& pair : {qMakePair(QString("mode"), QJsonValue("unknown")),
      qMakePair(QString("paragraphSpacing"), QJsonValue(-1)),
      qMakePair(QString("paragraphSpacing"), QJsonValue("12")),
      qMakePair(QString("version"), QJsonValue(3))}) {
    auto invalid = point;
    invalid.insert(pair.first, pair.second);
    if (decode_photo_text_style(invalid)) return STYLE_FAILURE();
  }
  auto axis_style = style;
  axis_style.axes.insert(QStringLiteral("wdth"), 112.5);
  const auto valid_axes = encode_photo_text_style(axis_style);
  if (valid_axes.value("version").toInt() != 4 || !decode_photo_text_style(valid_axes)) return STYLE_FAILURE();
  for (const auto& invalid_axes : {QJsonObject{{"axes", QJsonObject{{"width", 100}}}},
      QJsonObject{{"axes", QJsonObject{{"wdth", "wide"}}}},
      QJsonObject{{"axes", QJsonObject{{"wdth", 1e20}}}}}) {
    auto invalid = valid_axes;
    invalid.insert("axes", invalid_axes.value("axes"));
    if (decode_photo_text_style(invalid)) return STYLE_FAILURE();
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
  if (valid_contour.value("version").toInt() != 5 || !restored_contour || *restored_contour != contour_style) return STYLE_FAILURE();
  for (const auto& invalid_field : {qMakePair(QStringLiteral("pathStart"), QJsonValue(-0.1)),
      qMakePair(QStringLiteral("pathEnd"), QJsonValue(0.05)),
      qMakePair(QStringLiteral("pathSide"), QJsonValue("up")),
      qMakePair(QStringLiteral("pathPoints"), QJsonValue(QJsonArray{QJsonObject{{"x", -1}, {"y", 0.5}}}))}) {
    auto invalid = valid_contour;
    invalid.insert(invalid_field.first, invalid_field.second);
    if (decode_photo_text_style(invalid)) return STYLE_FAILURE();
  }
  auto exclusion_style = contour_style;
  exclusion_style.point_text = false;
  exclusion_style.path_closed = true;
  exclusion_style.path_exclusion = true;
  const auto encoded_exclusion = encode_photo_text_style(exclusion_style);
  const auto decoded_exclusion = decode_photo_text_style(encoded_exclusion);
  if (encoded_exclusion.value("version").toInt() != 6 || !decoded_exclusion ||
      *decoded_exclusion != exclusion_style) return STYLE_FAILURE();
  auto invalid_exclusion = encoded_exclusion;
  invalid_exclusion.insert("pathClosed", false);
  if (decode_photo_text_style(invalid_exclusion)) return STYLE_FAILURE();
  exclusion_style.path_contours = {
      {QPointF(0.2,0.2), QPointF(0.8,0.2), QPointF(0.8,0.8), QPointF(0.2,0.8)},
      {QPointF(0.4,0.4), QPointF(0.7,0.4), QPointF(0.7,0.6), QPointF(0.4,0.6)}};
  const auto encoded_contours = encode_photo_text_style(exclusion_style);
  const auto decoded_contours = decode_photo_text_style(encoded_contours);
  if (encoded_contours.value("version").toInt() != 7 || !decoded_contours ||
      *decoded_contours != exclusion_style) return STYLE_FAILURE();
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
      *decoded_material != exclusion_style) return STYLE_FAILURE();
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
      return STYLE_FAILURE();
    }
    auto legacy_metal = encoded_metal;
    legacy_metal.insert("version", 8);
    if (decode_photo_text_style(legacy_metal)) return STYLE_FAILURE();
  }
  auto plain_material = PhotoTextStyle{};
  plain_material.material = QStringLiteral("smoked_plastic");
  const auto plain_material_recipe = encode_photo_text_style(plain_material);
  const auto decoded_plain_material = decode_photo_text_style(plain_material_recipe);
  if (!decoded_plain_material || *decoded_plain_material != plain_material) return STYLE_FAILURE();
  for (const auto& invalid_field : {qMakePair(QStringLiteral("material"), QJsonValue("unsupported")),
      qMakePair(QStringLiteral("materialBlur"), QJsonValue(128)),
      qMakePair(QStringLiteral("materialTransmission"), QJsonValue(-0.1)),
      qMakePair(QStringLiteral("materialTint"), QJsonValue("bad-color"))}) {
    auto invalid = encoded_material;
    invalid.insert(invalid_field.first, invalid_field.second);
    if (decode_photo_text_style(invalid)) return STYLE_FAILURE();
  }
  auto invalid_metal = encode_photo_text_style(exclusion_style);
  invalid_metal.insert("version", 9);
  invalid_metal.insert("material", "polished_plastic");
  if (decode_photo_text_style(invalid_metal)) return STYLE_FAILURE();
  style.point_text = false;
  style.paragraph_spacing = 0;
  if (encode_photo_text_style(style).value("version").toInt() != 1) return STYLE_FAILURE();
  auto missing = encoded;
  missing.remove("family");
  if (decode_photo_text_style(missing)) return STYLE_FAILURE();

  auto ranged = PhotoTextStyle{};
  ranged.runs = {QVariantMap{{"start", 1}, {"length", 3}, {"color", "#ffff0000"},
      {"family", "Georgia"}, {"strokeWidth", 1.5}}};
  const auto encoded_ranges = encode_photo_text_style(ranged);
  const auto decoded_ranges = decode_photo_text_style(encoded_ranges);
  if (encoded_ranges.value("version").toInt() != 10 || !decoded_ranges || *decoded_ranges != ranged) return STYLE_FAILURE();

  const QVariantMap base_style = encode_photo_text_style(PhotoTextStyle{}).toVariantMap();
  QVariantList one_run{QVariantMap{{"start", 0}, {"length", 4}, {"color", "#ffff0000"}}};
  const QVariantMap red{{"color", "#ffff0000"}};
  const auto reapplied = photo_text_runs_with_range_style(one_run, 0, 4, red, base_style);
  if (reapplied != one_run) {
    std::cerr << "Reapplying an existing character style must be idempotent\n";
    return STYLE_FAILURE();
  }
  const auto cleared = photo_text_runs_with_range_style(one_run, 0, 4,
      QVariantMap{{"color", "#ffffffff"}}, base_style);
  if (!cleared.isEmpty()) return STYLE_FAILURE();
  const auto outside_untouched = photo_text_runs_with_range_style(one_run, 6, 9,
      QVariantMap{{"family", "Georgia"}}, base_style);
  if (outside_untouched.size() != 2 || outside_untouched.at(0).toMap() != one_run.front().toMap() ||
      outside_untouched.at(1).toMap().value("start").toInt() != 6 ||
      outside_untouched.at(1).toMap().value("length").toInt() != 3 ||
      outside_untouched.at(1).toMap().value("family").toString() != "Georgia") return STYLE_FAILURE();

  const auto remapped = remap_photo_text_runs(
      QVariantList{QVariantMap{{"start", 1}, {"length", 3}, {"color", "#ffff0000"}}},
      QStringLiteral("abcdef"), QStringLiteral("aXbcdef"));
  if (remapped.size() != 1 || remapped.front().toMap().value("start").toInt() != 1 ||
      remapped.front().toMap().value("length").toInt() != 4) return STYLE_FAILURE();
  const auto line_break_style = PhotoTextStyle{.runs = {
      QVariantMap{{"start", 3}, {"length", 1}, {"color", "#ffff0000"}},
      QVariantMap{{"start", 5}, {"length", 1}, {"family", "Georgia"}}}};
  const auto normalized_line_break_style = photo_text_style_with_normalized_line_breaks(
      line_break_style, QStringLiteral("A\r\nB\u2029C"));
  if (normalized_line_break_style.runs.size() != 2 ||
      normalized_line_break_style.runs.at(0).value("start").toInt() != 2 ||
      normalized_line_break_style.runs.at(1).value("start").toInt() != 4) return STYLE_FAILURE();
  if (photo_text_runs_fit_content({QVariantMap{{"start", 0}, {"length", 20}, {"color", "#ffff0000"}}},
          QStringLiteral("short text")) ||
      photo_text_runs_fit_content({QVariantMap{{"start", 1}, {"length", 1}, {"color", "#ffff0000"}}},
          QStringLiteral("A\r\nB")) ||
      photo_text_runs_fit_content({QVariantMap{{"start", 2}, {"length", 1}, {"color", "#ffff0000"}}},
          QString::fromUtf8("A😀B"))) return STYLE_FAILURE();
  return 0;
}

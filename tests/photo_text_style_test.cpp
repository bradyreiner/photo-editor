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
  style.point_text = false;
  style.paragraph_spacing = 0;
  if (encode_photo_text_style(style).value("version").toInt() != 1) return 1;
  auto missing = encoded;
  missing.remove("family");
  return decode_photo_text_style(missing) ? 1 : 0;
}

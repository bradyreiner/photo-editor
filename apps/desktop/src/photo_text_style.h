#pragma once

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QString>

#include <cmath>
#include <optional>

namespace photo_editor::desktop {

// Document-pixel units. This model is opt-in; legacy text keeps its old layout.
struct PhotoTextStyle final {
  QString family = QStringLiteral("Helvetica");
  QString face;
  double size = 48;
  int weight = 400;
  bool italic = false;
  bool underline = false;
  double tracking = 0;
  double line_height = 1.2;
  double baseline_offset = 0;
  double paragraph_spacing = 0;
  bool point_text = false;
  QColor color = Qt::white;

  bool operator==(const PhotoTextStyle&) const = default;
};

inline QJsonObject encode_photo_text_style(const PhotoTextStyle& style) {
  QJsonObject result{{"version", 1}, {"family", style.family}, {"face", style.face},
      {"size", style.size}, {"weight", style.weight}, {"italic", style.italic},
      {"underline", style.underline}, {"tracking", style.tracking},
      {"lineHeight", style.line_height}, {"baselineOffset", style.baseline_offset},
      {"color", style.color.name(QColor::HexArgb)}};
  if (style.point_text || style.paragraph_spacing != 0) {
    result.insert("version", 2);
    result.insert("mode", style.point_text ? "point" : "paragraph");
    result.insert("paragraphSpacing", style.paragraph_spacing);
  }
  return result;
}

inline std::optional<PhotoTextStyle> decode_photo_text_style(
    const QJsonObject& object, QString* error = nullptr) {
  if (error) error->clear();
  const auto fail = [error](const QString& message) -> std::optional<PhotoTextStyle> {
    if (error) *error = message;
    return std::nullopt;
  };
  const auto version = object.value("version").toDouble();
  if (!object.value("version").isDouble() || (version != 1 && version != 2))
    return fail(QStringLiteral("Unsupported text style version."));
  const auto keys = encode_photo_text_style(PhotoTextStyle{});
  for (auto it = object.begin(); it != object.end(); ++it)
    if (!keys.contains(it.key()) && !(version == 2 && (it.key() == "mode" || it.key() == "paragraphSpacing")))
      return fail(QStringLiteral("Unsupported text style field: %1").arg(it.key()));
  for (const auto* key : {"family", "face", "color"})
    if (!object.value(QLatin1String(key)).isString())
      return fail(QStringLiteral("Text style requires string field %1.").arg(QLatin1String(key)));
  for (const auto* key : {"italic", "underline"})
    if (!object.value(QLatin1String(key)).isBool())
      return fail(QStringLiteral("Text style requires boolean field %1.").arg(QLatin1String(key)));
  for (const auto* key : {"size", "weight", "tracking", "lineHeight", "baselineOffset"})
    if (!object.value(QLatin1String(key)).isDouble() ||
        !std::isfinite(object.value(QLatin1String(key)).toDouble()))
      return fail(QStringLiteral("Text style requires finite numeric field %1.").arg(QLatin1String(key)));
  PhotoTextStyle style;
  if (version == 2) {
    const auto mode = object.value("mode").toString();
    const auto spacing = object.value("paragraphSpacing");
    if ((mode != "point" && mode != "paragraph") || !spacing.isDouble() ||
        !std::isfinite(spacing.toDouble()) || spacing.toDouble() < 0 || spacing.toDouble() > 8192)
      return fail(QStringLiteral("Invalid text mode or paragraph spacing."));
    style.point_text = mode == "point";
    style.paragraph_spacing = spacing.toDouble();
  }
  style.family = object.value("family").toString();
  style.face = object.value("face").toString();
  style.size = object.value("size").toDouble();
  const auto weight = object.value("weight").toDouble();
  style.italic = object.value("italic").toBool();
  style.underline = object.value("underline").toBool();
  style.tracking = object.value("tracking").toDouble();
  style.line_height = object.value("lineHeight").toDouble();
  style.baseline_offset = object.value("baselineOffset").toDouble();
  style.color = QColor(object.value("color").toString());
  if (style.family.trimmed().isEmpty() || style.family.size() > 1024 || style.face.size() > 1024 ||
      style.size < 0.25 || style.size > 8192 || weight < 1 || weight > 1000 || std::floor(weight) != weight ||
      std::abs(style.tracking) > 8192 || style.line_height < 0.1 || style.line_height > 20 ||
      std::abs(style.baseline_offset) > 8192 || !style.color.isValid())
    return fail(QStringLiteral("Text style contains out-of-range typography or an invalid color."));
  style.weight = static_cast<int>(weight);
  return style;
}

}  // namespace photo_editor::desktop

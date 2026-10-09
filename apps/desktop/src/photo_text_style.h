#pragma once

#include <QColor>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <optional>

namespace photo_editor::desktop {

inline const QStringList& photo_text_metal_material_ids() {
  static const QStringList materials{
      QStringLiteral("polished_chrome"), QStringLiteral("brushed_silver"),
      QStringLiteral("warm_gold"), QStringLiteral("copper"),
      QStringLiteral("dark_steel"), QStringLiteral("champagne_gold"),
      QStringLiteral("rose_gold"), QStringLiteral("antique_brass"),
      QStringLiteral("titanium"), QStringLiteral("black_chrome")};
  return materials;
}

inline bool photo_text_material_is_metal(const QString& material) {
  return photo_text_metal_material_ids().contains(material);
}

inline bool photo_text_material_is_supported(const QString& material) {
  return material == QStringLiteral("frosted_glass") ||
      material == QStringLiteral("smoked_plastic") ||
      material == QStringLiteral("soft_emboss") ||
      photo_text_material_is_metal(material);
}

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
  double stroke_width = 0;
  QColor stroke_color = Qt::black;
  double shadow_x = 0;
  double shadow_y = 0;
  QColor shadow_color = QColor(0, 0, 0, 160);
  QMap<QString, double> axes;
  QVector<QPointF> path_points;
  QVector<QVector<QPointF>> path_contours;
  double path_start = 0;
  double path_end = 1;
  double path_offset = 0;
  double path_spacing = 0;
  QString path_side = QStringLiteral("left");
  bool path_closed = false;
  bool path_exclusion = false;
  QString material = QStringLiteral("none");
  QColor material_tint = QColor(QStringLiteral("#9eb8c8"));
  double material_blur = 6;
  double material_transmission = 0.68;
  double material_edge_softness = 1;
  double material_depth = 4;
  double material_light_angle = -45;

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
  if (style.stroke_width != 0 || style.shadow_x != 0 || style.shadow_y != 0 ||
      style.stroke_color != QColor(Qt::black) || style.shadow_color != QColor(0, 0, 0, 160)) {
    result.insert("version", 3);
    result.insert("mode", style.point_text ? "point" : "paragraph");
    result.insert("paragraphSpacing", style.paragraph_spacing);
    result.insert("strokeWidth", style.stroke_width);
    result.insert("strokeColor", style.stroke_color.name(QColor::HexArgb));
    result.insert("shadowX", style.shadow_x);
    result.insert("shadowY", style.shadow_y);
    result.insert("shadowColor", style.shadow_color.name(QColor::HexArgb));
  }
  if (!style.axes.isEmpty()) {
    result.insert("version", 4);
    result.insert("mode", style.point_text ? "point" : "paragraph");
    result.insert("paragraphSpacing", style.paragraph_spacing);
    result.insert("strokeWidth", style.stroke_width);
    result.insert("strokeColor", style.stroke_color.name(QColor::HexArgb));
    result.insert("shadowX", style.shadow_x);
    result.insert("shadowY", style.shadow_y);
    result.insert("shadowColor", style.shadow_color.name(QColor::HexArgb));
    QJsonObject axes;
    for (auto it = style.axes.cbegin(); it != style.axes.cend(); ++it)
      axes.insert(it.key(), it.value());
    result.insert("axes", axes);
  }
  if (!style.path_points.isEmpty()) {
    result.insert("version", 5);
    result.insert("mode", style.point_text ? "point" : "paragraph");
    result.insert("paragraphSpacing", style.paragraph_spacing);
    result.insert("strokeWidth", style.stroke_width);
    result.insert("strokeColor", style.stroke_color.name(QColor::HexArgb));
    result.insert("shadowX", style.shadow_x);
    result.insert("shadowY", style.shadow_y);
    result.insert("shadowColor", style.shadow_color.name(QColor::HexArgb));
    QJsonObject axes;
    for (auto it = style.axes.cbegin(); it != style.axes.cend(); ++it)
      axes.insert(it.key(), it.value());
    result.insert("axes", axes);
    QJsonArray points;
    for (const auto& point : style.path_points)
      points.append(QJsonObject{{"x", point.x()}, {"y", point.y()}});
    result.insert("pathPoints", points);
    result.insert("pathStart", style.path_start);
    result.insert("pathEnd", style.path_end);
    result.insert("pathOffset", style.path_offset);
    result.insert("pathSpacing", style.path_spacing);
    result.insert("pathSide", style.path_side);
    result.insert("pathClosed", style.path_closed);
    if (style.path_exclusion) {
      result.insert("version", 6);
      result.insert("pathExclusion", true);
    }
    if (!style.path_contours.isEmpty()) {
      result.insert("version", 7);
      QJsonArray contours;
      for (const auto& contour : style.path_contours) {
        QJsonArray contour_points;
        for (const auto& point : contour)
          contour_points.append(QJsonObject{{"x", point.x()}, {"y", point.y()}});
        contours.append(contour_points);
      }
      result.insert("pathContours", contours);
    }
  }
  if (style.material != QStringLiteral("none")) {
    result.insert("version", photo_text_material_is_metal(style.material) ? 9 : 8);
    result.insert("mode", style.point_text ? "point" : "paragraph");
    result.insert("paragraphSpacing", style.paragraph_spacing);
    result.insert("strokeWidth", style.stroke_width);
    result.insert("strokeColor", style.stroke_color.name(QColor::HexArgb));
    result.insert("shadowX", style.shadow_x);
    result.insert("shadowY", style.shadow_y);
    result.insert("shadowColor", style.shadow_color.name(QColor::HexArgb));
    QJsonObject axes;
    for (auto it = style.axes.cbegin(); it != style.axes.cend(); ++it)
      axes.insert(it.key(), it.value());
    result.insert("axes", axes);
    result.insert("material", style.material);
    result.insert("materialTint", style.material_tint.name(QColor::HexArgb));
    result.insert("materialBlur", style.material_blur);
    result.insert("materialTransmission", style.material_transmission);
    result.insert("materialEdgeSoftness", style.material_edge_softness);
    result.insert("materialDepth", style.material_depth);
    result.insert("materialLightAngle", style.material_light_angle);
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
  if (!object.value("version").isDouble() || (version < 1 || version > 9 || std::floor(version) != version))
    return fail(QStringLiteral("Unsupported text style version."));
  const auto keys = encode_photo_text_style(PhotoTextStyle{});
  for (auto it = object.begin(); it != object.end(); ++it)
    if (!keys.contains(it.key()) && !(version >= 2 && (it.key() == "mode" || it.key() == "paragraphSpacing")) &&
        !(version >= 3 && QStringList{"strokeWidth", "strokeColor", "shadowX", "shadowY", "shadowColor"}.contains(it.key())) &&
        !(version >= 4 && it.key() == "axes") &&
        !(version >= 5 && QStringList{"pathPoints", "pathStart", "pathEnd", "pathOffset", "pathSpacing", "pathSide", "pathClosed"}.contains(it.key())) &&
        !(version >= 6 && it.key() == "pathExclusion") &&
        !(version >= 7 && it.key() == "pathContours") &&
        !(version >= 8 && QStringList{"material", "materialTint", "materialBlur", "materialTransmission",
            "materialEdgeSoftness", "materialDepth", "materialLightAngle"}.contains(it.key())))
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
  if (version >= 2) {
    const auto mode = object.value("mode").toString();
    const auto spacing = object.value("paragraphSpacing");
    if ((mode != "point" && mode != "paragraph") || !spacing.isDouble() ||
        !std::isfinite(spacing.toDouble()) || spacing.toDouble() < 0 || spacing.toDouble() > 8192)
      return fail(QStringLiteral("Invalid text mode or paragraph spacing."));
    style.point_text = mode == "point";
    style.paragraph_spacing = spacing.toDouble();
  }
  if (version >= 3) {
    for (const auto* key : {"strokeWidth", "shadowX", "shadowY"})
      if (!object.value(QLatin1String(key)).isDouble() || !std::isfinite(object.value(QLatin1String(key)).toDouble()))
        return fail(QStringLiteral("Invalid text effect distance."));
    style.stroke_width = object.value("strokeWidth").toDouble();
    style.shadow_x = object.value("shadowX").toDouble();
    style.shadow_y = object.value("shadowY").toDouble();
    style.stroke_color = QColor(object.value("strokeColor").toString());
    style.shadow_color = QColor(object.value("shadowColor").toString());
    if (style.stroke_width < 0 || style.stroke_width > 256 || std::abs(style.shadow_x) > 8192 ||
        std::abs(style.shadow_y) > 8192 || !style.stroke_color.isValid() || !style.shadow_color.isValid())
      return fail(QStringLiteral("Invalid text stroke or shadow."));
  }
  if (version >= 4) {
    const auto axes = object.value("axes");
    if (!axes.isObject() || axes.toObject().size() > 32)
      return fail(QStringLiteral("Invalid variable font axes."));
    const auto axis_values = axes.toObject();
    for (auto it = axis_values.begin(); it != axis_values.end(); ++it) {
      if (it.key().size() != 4)
        return fail(QStringLiteral("Invalid variable font axis: %1").arg(it.key()));
      const auto tag = QFont::Tag::fromString(it.key());
      if (!tag || !tag->isValid() || !it.value().isDouble() ||
          !std::isfinite(it.value().toDouble()) || std::abs(it.value().toDouble()) > 1000000)
        return fail(QStringLiteral("Invalid variable font axis: %1").arg(it.key()));
      style.axes.insert(it.key(), it.value().toDouble());
    }
  }
  if (version >= 5 && (version < 8 || object.contains("pathPoints"))) {
    const auto points = object.value("pathPoints");
    if (!points.isArray() || points.toArray().size() < 2 || points.toArray().size() > 8192)
      return fail(QStringLiteral("Text contour requires 2 to 8192 baseline points."));
    for (const auto& value : points.toArray()) {
      if (!value.isObject()) return fail(QStringLiteral("Invalid text contour point."));
      const auto point = value.toObject();
      const auto x = point.value("x"), y = point.value("y");
      if (!x.isDouble() || !y.isDouble() || !std::isfinite(x.toDouble()) || !std::isfinite(y.toDouble()) ||
          x.toDouble() < 0 || x.toDouble() > 1 || y.toDouble() < 0 || y.toDouble() > 1)
        return fail(QStringLiteral("Text contour points must be finite normalized coordinates."));
      style.path_points.push_back(QPointF(x.toDouble(), y.toDouble()));
    }
    const auto number = [&](const char* key) {
      const auto value = object.value(QLatin1String(key));
      return value.isDouble() && std::isfinite(value.toDouble());
    };
    if (!number("pathStart") || !number("pathEnd") || !number("pathOffset") || !number("pathSpacing") ||
        !object.value("pathSide").isString() || !object.value("pathClosed").isBool())
      return fail(QStringLiteral("Invalid text contour controls."));
    style.path_start = object.value("pathStart").toDouble();
    style.path_end = object.value("pathEnd").toDouble();
    style.path_offset = object.value("pathOffset").toDouble();
    style.path_spacing = object.value("pathSpacing").toDouble();
    style.path_side = object.value("pathSide").toString();
    style.path_closed = object.value("pathClosed").toBool();
    if (style.path_start < 0 || style.path_start >= 1 || style.path_end <= style.path_start ||
        style.path_end > 1 || std::abs(style.path_offset) > 8192 || style.path_spacing < 0 ||
        style.path_spacing > 8192 || (style.path_side != "left" && style.path_side != "right"))
      return fail(QStringLiteral("Text contour controls are out of range."));
  }
  if (version >= 6 && object.contains("pathExclusion")) {
    if (!object.value("pathExclusion").isBool())
      return fail(QStringLiteral("Invalid paragraph exclusion setting."));
    if (object.value("pathExclusion").toBool()) {
      if (style.path_points.size() < 3 || !style.path_closed || style.point_text)
        return fail(QStringLiteral("Paragraph exclusion requires a closed contour and paragraph text."));
      style.path_exclusion = true;
    }
  }
  if (version == 6 && !style.path_exclusion)
    return fail(QStringLiteral("Version-6 text styles require paragraph exclusion."));
  if (version >= 7 && (version < 8 || object.contains("pathContours"))) {
    const auto contours = object.value("pathContours");
    if (!contours.isArray() || contours.toArray().isEmpty() || contours.toArray().size() > 256)
      return fail(QStringLiteral("Text contour collection must contain 1 to 256 outlines."));
    for (const auto& contour_value : contours.toArray()) {
      if (!contour_value.isArray() || contour_value.toArray().size() < 3 || contour_value.toArray().size() > 8192)
        return fail(QStringLiteral("Each text contour must contain 3 to 8192 points."));
      QVector<QPointF> contour;
      for (const auto& value : contour_value.toArray()) {
        if (!value.isObject()) return fail(QStringLiteral("Invalid text contour point."));
        const auto point = value.toObject();
        const auto x = point.value("x"), y = point.value("y");
        if (!x.isDouble() || !y.isDouble() || !std::isfinite(x.toDouble()) || !std::isfinite(y.toDouble()) ||
            x.toDouble() < 0 || x.toDouble() > 1 || y.toDouble() < 0 || y.toDouble() > 1)
          return fail(QStringLiteral("Text contour points must be finite normalized coordinates."));
        contour.push_back(QPointF(x.toDouble(), y.toDouble()));
      }
      style.path_contours.push_back(contour);
    }
    if (style.path_points.size() < 3 || !style.path_closed)
      return fail(QStringLiteral("Contour collections require a closed editable baseline."));
  }
  if (version >= 8) {
    const auto number = [&](const char* key) {
      const auto value = object.value(QLatin1String(key));
      return value.isDouble() && std::isfinite(value.toDouble());
    };
    if (!object.value("material").isString() || !object.value("materialTint").isString() ||
        !number("materialBlur") || !number("materialTransmission") ||
        !number("materialEdgeSoftness") || !number("materialDepth") || !number("materialLightAngle"))
      return fail(QStringLiteral("Invalid typography material controls."));
    style.material = object.value("material").toString();
    style.material_tint = QColor(object.value("materialTint").toString());
    style.material_blur = object.value("materialBlur").toDouble();
    style.material_transmission = object.value("materialTransmission").toDouble();
    style.material_edge_softness = object.value("materialEdgeSoftness").toDouble();
    style.material_depth = object.value("materialDepth").toDouble();
    style.material_light_angle = object.value("materialLightAngle").toDouble();
    if (!photo_text_material_is_supported(style.material) ||
        (version == 8 && photo_text_material_is_metal(style.material)) ||
        !style.material_tint.isValid() || style.material_blur < 0 || style.material_blur > 64 ||
        style.material_transmission < 0 || style.material_transmission > 1 ||
        style.material_edge_softness < 0 || style.material_edge_softness > 16 ||
        style.material_depth < 0 || style.material_depth > 32 ||
        style.material_light_angle < -180 || style.material_light_angle > 180)
      return fail(QStringLiteral("Typography material controls are out of range."));
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

inline void apply_photo_text_variable_axes(QFont& font, const PhotoTextStyle& style) {
  for (auto it = style.axes.cbegin(); it != style.axes.cend(); ++it) {
    const auto tag = QFont::Tag::fromString(it.key());
    if (tag) font.setVariableAxis(*tag, static_cast<float>(it.value()));
  }
}

}  // namespace photo_editor::desktop

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
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <algorithm>
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
  // Character overrides are UTF-16 ranges, matching QString and QTextDocument
  // cursor positions. Empty fields inherit the layer's base typography.
  QVector<QVariantMap> runs;

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
  if (!style.runs.isEmpty()) {
    // Version 10 carries a sparse character-run collection in addition to the
    // complete base-style envelope required by the versioned decoder.
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
    QJsonArray runs;
    for (const auto& run : style.runs)
      runs.append(QJsonObject::fromVariantMap(run));
    result.insert("version", 10);
    result.insert("runs", runs);
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
  if (!object.value("version").isDouble() || (version < 1 || version > 10 || std::floor(version) != version))
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
            "materialEdgeSoftness", "materialDepth", "materialLightAngle"}.contains(it.key())) &&
        !(version >= 10 && it.key() == "runs"))
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
  if (version >= 8 && (version <= 9 || object.contains("material"))) {
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
  if (version >= 10) {
    const auto runs_value = object.value("runs");
    if (!runs_value.isArray() || runs_value.toArray().size() > 4096)
      return fail(QStringLiteral("Character styles must contain at most 4096 ranges."));
    static const QStringList run_keys{"family", "size", "weight", "italic", "underline",
        "color", "strokeWidth", "strokeColor"};
    qsizetype previous_end = -1;
    for (const auto& run_value : runs_value.toArray()) {
      if (!run_value.isObject()) return fail(QStringLiteral("Invalid character style range."));
      const auto run = run_value.toObject();
      for (auto it = run.begin(); it != run.end(); ++it)
        if (it.key() != QStringLiteral("start") && it.key() != QStringLiteral("length") &&
            !run_keys.contains(it.key()))
          return fail(QStringLiteral("Unsupported character style field: %1").arg(it.key()));
      const auto start_value = run.value(QStringLiteral("start"));
      const auto length_value = run.value(QStringLiteral("length"));
      if (!start_value.isDouble() || !length_value.isDouble() ||
          std::floor(start_value.toDouble()) != start_value.toDouble() ||
          std::floor(length_value.toDouble()) != length_value.toDouble() ||
          start_value.toDouble() < 0 || length_value.toDouble() < 1 ||
          start_value.toDouble() + length_value.toDouble() > 1048576 ||
          static_cast<qsizetype>(start_value.toDouble()) < previous_end || run.size() <= 2)
        return fail(QStringLiteral("Character style ranges must be sorted, nonempty, and bounded."));
      previous_end = static_cast<qsizetype>(start_value.toDouble() + length_value.toDouble());
      for (const auto* key : {"family", "color", "strokeColor"}) {
        const auto value = run.value(QLatin1String(key));
        if (!value.isUndefined() && !value.isString())
          return fail(QStringLiteral("Character style field %1 must be a string.").arg(QLatin1String(key)));
      }
      if (run.contains(QStringLiteral("family")) &&
          (run.value(QStringLiteral("family")).toString().trimmed().isEmpty() ||
           run.value(QStringLiteral("family")).toString().size() > 1024))
        return fail(QStringLiteral("Character font family is invalid."));
      for (const auto* key : {"italic", "underline"}) {
        const auto value = run.value(QLatin1String(key));
        if (!value.isUndefined() && !value.isBool())
          return fail(QStringLiteral("Character style field %1 must be boolean.").arg(QLatin1String(key)));
      }
      for (const auto* key : {"size", "weight", "strokeWidth"}) {
        const auto value = run.value(QLatin1String(key));
        if (!value.isUndefined() && (!value.isDouble() || !std::isfinite(value.toDouble())))
          return fail(QStringLiteral("Character style field %1 must be finite.").arg(QLatin1String(key)));
      }
      if (run.contains(QStringLiteral("size")) &&
          (run.value(QStringLiteral("size")).toDouble() < 0.25 ||
           run.value(QStringLiteral("size")).toDouble() > 8192))
        return fail(QStringLiteral("Character font size is out of range."));
      if (run.contains(QStringLiteral("weight")) &&
          (run.value(QStringLiteral("weight")).toDouble() < 1 ||
           run.value(QStringLiteral("weight")).toDouble() > 1000 ||
           std::floor(run.value(QStringLiteral("weight")).toDouble()) != run.value(QStringLiteral("weight")).toDouble()))
        return fail(QStringLiteral("Character font weight is out of range."));
      if (run.contains(QStringLiteral("strokeWidth")) &&
          (run.value(QStringLiteral("strokeWidth")).toDouble() < 0 ||
           run.value(QStringLiteral("strokeWidth")).toDouble() > 256))
        return fail(QStringLiteral("Character stroke width is out of range."));
      for (const auto* key : {"color", "strokeColor"}) {
        const auto value = run.value(QLatin1String(key));
        if (value.isString() && !QColor(value.toString()).isValid())
          return fail(QStringLiteral("Character color field %1 is invalid.").arg(QLatin1String(key)));
      }
      style.runs.push_back(run.toVariantMap());
    }
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

inline bool photo_text_runs_fit_content(const QVector<QVariantMap>& runs, const QString& text) {
  if (text.size() > 1048576) return false;
  for (const auto& run : runs) {
    const auto start = run.value(QStringLiteral("start")).toInt();
    const auto length = run.value(QStringLiteral("length")).toInt();
    const auto end = start + length;
    if (start < 0 || length <= 0 || end > text.size()) return false;
    const auto splits_surrogate = [&text](int boundary) {
      return boundary > 0 && boundary < text.size() &&
          text.at(boundary - 1).isHighSurrogate() && text.at(boundary).isLowSurrogate();
    };
    const auto splits_crlf = [&text](int boundary) {
      return boundary > 0 && boundary < text.size() &&
          text.at(boundary - 1) == QChar('\r') && text.at(boundary) == QChar('\n');
    };
    if (splits_surrogate(start) || splits_surrogate(end) || splits_crlf(start) || splits_crlf(end)) return false;
  }
  return true;
}

inline PhotoTextStyle photo_text_style_with_normalized_line_breaks(
    const PhotoTextStyle& style, const QString& text) {
  if (style.runs.isEmpty() || text.size() > 1048576 ||
      (!text.contains(QChar('\r')) && !text.contains(QChar(0x2029)))) return style;
  QVector<int> normalized_offsets(text.size() + 1);
  int normalized_length = 0;
  normalized_offsets[0] = 0;
  for (qsizetype index = 0; index < text.size();) {
    const auto character = text.at(index);
    if (character == QChar('\r')) {
      ++normalized_length;
      normalized_offsets[index + 1] = normalized_length;
      if (index + 1 < text.size() && text.at(index + 1) == QChar('\n')) {
        normalized_offsets[index + 2] = normalized_length;
        index += 2;
      } else {
        ++index;
      }
    } else {
      ++normalized_length;
      normalized_offsets[index + 1] = normalized_length;
      ++index;
    }
  }
  auto result = style;
  result.runs.clear();
  result.runs.reserve(style.runs.size());
  for (const auto& run : style.runs) {
    const auto start = run.value(QStringLiteral("start")).toInt();
    const auto end = start + run.value(QStringLiteral("length")).toInt();
    if (start < 0 || end <= start || end > text.size()) continue;
    const auto normalized_start = normalized_offsets.at(start);
    const auto normalized_end = normalized_offsets.at(end);
    if (normalized_end <= normalized_start) continue;
    auto mapped = run;
    mapped.insert(QStringLiteral("start"), normalized_start);
    mapped.insert(QStringLiteral("length"), normalized_end - normalized_start);
    result.runs.push_back(mapped);
  }
  return result;
}

inline void apply_photo_text_variable_axes(QFont& font, const PhotoTextStyle& style) {
  for (auto it = style.axes.cbegin(); it != style.axes.cend(); ++it) {
    const auto tag = QFont::Tag::fromString(it.key());
    if (tag) font.setVariableAxis(*tag, static_cast<float>(it.value()));
  }
}

inline QVariantMap photo_text_run_attributes(const QVariantMap& run) {
  auto attributes = run;
  attributes.remove(QStringLiteral("start"));
  attributes.remove(QStringLiteral("length"));
  return attributes;
}

inline QVariantList photo_text_runs_with_range_style(
    const QVariantList& current_runs, int selection_start, int selection_end,
    const QVariantMap& changes, const QVariantMap& base_style) {
  struct Segment final {
    int start = 0;
    int end = 0;
    QVariantMap attributes;
  };
  QVector<int> boundaries{selection_start, selection_end};
  QVector<QVariantMap> runs;
  runs.reserve(current_runs.size());
  for (const auto& value : current_runs) {
    const auto run = value.toMap();
    const auto start = run.value(QStringLiteral("start")).toInt();
    const auto end = start + run.value(QStringLiteral("length")).toInt();
    if (start < 0 || end <= start || end > 1048576) continue;
    runs.push_back(run);
    boundaries.push_back(start);
    boundaries.push_back(end);
  }
  std::sort(runs.begin(), runs.end(), [](const auto& left, const auto& right) {
    return left.value(QStringLiteral("start")).toInt() < right.value(QStringLiteral("start")).toInt();
  });
  QVector<int> run_ends;
  run_ends.reserve(runs.size());
  for (const auto& run : runs)
    run_ends.push_back(run.value(QStringLiteral("start")).toInt() + run.value(QStringLiteral("length")).toInt());
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
  QVariantList result;
  qsizetype run_index = 0;
  for (qsizetype i = 1; i < boundaries.size(); ++i) {
    const auto start = boundaries.at(i - 1);
    const auto end = boundaries.at(i);
    if (start >= end) continue;
    QVariantMap attributes;
    while (run_index < runs.size() && run_ends.at(run_index) <= start) ++run_index;
    if (run_index < runs.size()) {
      const auto run_start = runs.at(run_index).value(QStringLiteral("start")).toInt();
      if (run_start <= start && run_ends.at(run_index) >= end)
        attributes = photo_text_run_attributes(runs.at(run_index));
    }
    if (start >= selection_start && end <= selection_end) {
      for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const auto base_value = base_style.value(it.key());
        const auto requested = it.value();
        const auto values_equal = [&](const QVariant& left, const QVariant& right) {
          if (it.key() == QStringLiteral("color") || it.key() == QStringLiteral("strokeColor")) {
            const QColor left_color(left.toString());
            const QColor right_color(right.toString());
            return left_color.isValid() && right_color.isValid() && left_color == right_color;
          }
          if (it.key() == QStringLiteral("family"))
            return left.toString().compare(right.toString(), Qt::CaseInsensitive) == 0;
          if (it.key() == QStringLiteral("italic") || it.key() == QStringLiteral("underline"))
            return left.toBool() == right.toBool();
          bool left_ok = false;
          bool right_ok = false;
          const auto left_number = left.toDouble(&left_ok);
          const auto right_number = right.toDouble(&right_ok);
          return left_ok && right_ok && std::abs(left_number - right_number) < 1e-9;
        };
        const auto effective_value = attributes.value(it.key(), base_value);
        if (values_equal(effective_value, requested)) continue;
        const bool same_as_base = values_equal(base_value, requested);
        if (same_as_base) attributes.remove(it.key());
        else attributes.insert(it.key(), requested);
      }
    }
    if (attributes.isEmpty()) continue;
    if (!result.isEmpty()) {
      auto previous = result.last().toMap();
      const auto previous_end = previous.value(QStringLiteral("start")).toInt() +
          previous.value(QStringLiteral("length")).toInt();
      if (previous_end == start && photo_text_run_attributes(previous) == attributes) {
        previous.insert(QStringLiteral("length"), previous.value(QStringLiteral("length")).toInt() + end - start);
        result.last() = previous;
        continue;
      }
    }
    attributes.insert(QStringLiteral("start"), start);
    attributes.insert(QStringLiteral("length"), end - start);
    result.push_back(attributes);
  }
  return result;
}

inline QVariantList remap_photo_text_runs(
    const QVariantList& current_runs, const QString& before, const QString& after) {
  if (before == after || current_runs.isEmpty()) return current_runs;
  if (before.size() > 1048576 || after.size() > 1048576) return {};
  const auto before_size = static_cast<int>(before.size());
  const auto after_size = static_cast<int>(after.size());
  int prefix = 0;
  const auto max_prefix = std::min(before_size, after_size);
  while (prefix < max_prefix && before.at(prefix) == after.at(prefix)) ++prefix;
  if (prefix > 0 && prefix < before_size && prefix < after_size &&
      before.at(prefix - 1).isHighSurrogate() && before.at(prefix).isLowSurrogate()) --prefix;
  int suffix = 0;
  while (suffix < before_size - prefix && suffix < after_size - prefix &&
      before.at(before.size() - suffix - 1) == after.at(after.size() - suffix - 1)) ++suffix;
  if (suffix > 0 && suffix < before_size - prefix && suffix < after_size - prefix &&
      before.at(before.size() - suffix).isLowSurrogate() && before.at(before.size() - suffix - 1).isHighSurrogate()) --suffix;
  const auto old_end = before_size - suffix;
  const auto new_end = after_size - suffix;
  const auto inserted = new_end - prefix;
  const auto delta = after_size - before_size;
  QVector<QVariantMap> runs;
  runs.reserve(current_runs.size());
  for (const auto& value : current_runs) {
    const auto run = value.toMap();
    const auto start = run.value(QStringLiteral("start")).toInt();
    const auto end = start + run.value(QStringLiteral("length")).toInt();
    if (start >= 0 && end > start && end <= before_size) runs.push_back(run);
  }
  int insertion_owner = -1;
  if (inserted > 0) {
    for (qsizetype i = 0; i < runs.size(); ++i) {
      const auto start = runs.at(i).value(QStringLiteral("start")).toInt();
      const auto end = start + runs.at(i).value(QStringLiteral("length")).toInt();
      if (start <= prefix && prefix < end) {
        insertion_owner = static_cast<int>(i);
        break;
      }
    }
    if (insertion_owner < 0) {
      for (qsizetype i = 0; i < runs.size(); ++i) {
        const auto start = runs.at(i).value(QStringLiteral("start")).toInt();
        const auto end = start + runs.at(i).value(QStringLiteral("length")).toInt();
        if (end == prefix) insertion_owner = static_cast<int>(i);
      }
    }
    if (insertion_owner < 0) {
      for (qsizetype i = 0; i < runs.size(); ++i) {
        if (runs.at(i).value(QStringLiteral("start")).toInt() == prefix) {
          insertion_owner = static_cast<int>(i);
          break;
        }
      }
    }
  }
  QVariantList result;
  for (qsizetype run_index = 0; run_index < runs.size(); ++run_index) {
    const auto run = runs.at(run_index);
    const auto run_start = run.value(QStringLiteral("start")).toInt();
    const auto run_end = run_start + run.value(QStringLiteral("length")).toInt();
    const auto attributes = photo_text_run_attributes(run);
    if (old_end == prefix && inserted > 0) {
      if (run_end <= prefix) {
        auto previous = run;
        if (run_index == insertion_owner)
          previous.insert(QStringLiteral("length"), run.value(QStringLiteral("length")).toInt() + inserted);
        result.push_back(previous);
        continue;
      }
      if (run_start >= prefix) {
        auto following = run;
        following.insert(QStringLiteral("start"), run_start + delta);
        if (run_index == insertion_owner) {
          following.insert(QStringLiteral("start"), prefix);
          following.insert(QStringLiteral("length"), run.value(QStringLiteral("length")).toInt() + inserted);
        }
        result.push_back(following);
        continue;
      }
      auto containing = run;
      if (run_index == insertion_owner)
        containing.insert(QStringLiteral("length"), run.value(QStringLiteral("length")).toInt() + inserted);
      result.push_back(containing);
      continue;
    }
    if (run_end <= prefix) {
      result.push_back(run);
      continue;
    }
    if (run_start >= old_end) {
      auto shifted = run;
      shifted.insert(QStringLiteral("start"), run_start + delta);
      result.push_back(shifted);
      continue;
    }
    int mapped_start = -1;
    int mapped_end = -1;
    if (run_start < prefix) {
      mapped_start = run_start;
      mapped_end = std::min(run_end, prefix);
    }
    if (run_end > old_end) {
      const auto right_start = std::max(run_start, old_end) + delta;
      const auto right_end = run_end + delta;
      if (mapped_start < 0) mapped_start = right_start;
      else mapped_start = std::min(mapped_start, right_start);
      mapped_end = std::max(mapped_end, right_end);
    }
    const bool owns_insertion = inserted > 0 && run_index == insertion_owner;
    if (owns_insertion) {
      if (mapped_start < 0) mapped_start = prefix;
      mapped_end = std::max(mapped_end, new_end);
    }
    if (mapped_start >= 0 && mapped_end > mapped_start) {
      auto mapped = attributes;
      mapped.insert(QStringLiteral("start"), mapped_start);
      mapped.insert(QStringLiteral("length"), mapped_end - mapped_start);
      result.push_back(mapped);
    }
  }
  std::sort(result.begin(), result.end(), [](const QVariant& left, const QVariant& right) {
    return left.toMap().value(QStringLiteral("start")).toInt() <
        right.toMap().value(QStringLiteral("start")).toInt();
  });
  QVariantList normalized;
  for (const auto& value : result) {
    auto run = value.toMap();
    if (normalized.isEmpty()) {
      normalized.push_back(run);
      continue;
    }
    auto previous = normalized.last().toMap();
    const auto previous_end = previous.value(QStringLiteral("start")).toInt() +
        previous.value(QStringLiteral("length")).toInt();
    if (previous_end == run.value(QStringLiteral("start")).toInt() &&
        photo_text_run_attributes(previous) == photo_text_run_attributes(run)) {
      previous.insert(QStringLiteral("length"), previous.value(QStringLiteral("length")).toInt() +
          run.value(QStringLiteral("length")).toInt());
      normalized.last() = previous;
    } else {
      normalized.push_back(run);
    }
  }
  return normalized;
}

}  // namespace photo_editor::desktop

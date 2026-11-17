#pragma once

#include "timeline_rig_scene.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSize>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop::timeline_motion {

// Editable vector commands and time samples, rather than a raster clip. Every
// frame is drawn by the native renderer at the requested output resolution.
inline QString validate_vector_path_parameters(const QVariantMap& parameters) {
  if (parameters.contains(QStringLiteral("scene"))) {
    QString error;
    static_cast<void>(prepare_rig_scene(parameters.value(QStringLiteral("scene")).toMap(), &error));
    return error;
  }
  const auto width = parameters.value(QStringLiteral("viewBoxWidth")).toDouble();
  const auto height = parameters.value(QStringLiteral("viewBoxHeight")).toDouble();
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0 ||
      width > 32768.0 || height > 32768.0)
    return QStringLiteral("Choose finite positive vector view-box dimensions up to 32768.");
  const auto commands = parameters.value(QStringLiteral("commands")).toList();
  if (commands.isEmpty() || commands.size() > 4096)
    return QStringLiteral("A vector path requires 1 to 4096 drawing commands.");
  qsizetype coordinate_count = 0;
  for (qsizetype i = 0; i < commands.size(); ++i) {
    const auto command = commands.at(i).toMap();
    const auto verb = command.value(QStringLiteral("verb")).toString();
    const auto count = verb == QStringLiteral("M") || verb == QStringLiteral("L") ? 2
                     : verb == QStringLiteral("C") ? 6
                     : verb == QStringLiteral("Q") ? 4
                     : verb == QStringLiteral("Z") ? 0 : -1;
    const auto points = command.value(QStringLiteral("points")).toList();
    if (count < 0 || points.size() != count || (i == 0 && verb != QStringLiteral("M")))
      return QStringLiteral("Use M/L/C/Q/Z vector commands with their exact coordinate counts.");
    for (const auto& point : points)
      if (!std::isfinite(point.toDouble()) || std::abs(point.toDouble()) > 131072.0)
        return QStringLiteral("Vector coordinates must be finite and within 131072 units.");
    coordinate_count += points.size();
  }
  const auto frames = parameters.value(QStringLiteral("frames")).toList();
  if (frames.size() > 9000 || frames.size() * coordinate_count > 4000000)
    return QStringLiteral("Vector animation exceeds its bounded frame/coordinate budget.");
  double previous = -1.0;
  for (const auto& entry : frames) {
    const auto frame = entry.toMap();
    const auto time = frame.value(QStringLiteral("seconds")).toDouble();
    const auto points = frame.value(QStringLiteral("coordinates")).toList();
    if (!std::isfinite(time) || time < 0.0 || time <= previous || points.size() != coordinate_count)
      return QStringLiteral("Vector samples need increasing nonnegative times and matching coordinates.");
    for (const auto& point : points)
      if (!std::isfinite(point.toDouble()) || std::abs(point.toDouble()) > 131072.0)
        return QStringLiteral("Animated vector coordinates are out of range.");
    previous = time;
  }
  const auto stroke_width = parameters.value(QStringLiteral("strokeWidth"), 1.0).toDouble();
  if (!std::isfinite(stroke_width) || stroke_width < 0.0 || stroke_width > 4096.0 ||
      !QColor(parameters.value(QStringLiteral("stroke"), QStringLiteral("#00000000")).toString()).isValid() ||
      !QColor(parameters.value(QStringLiteral("fill"), QStringLiteral("#00000000")).toString()).isValid())
    return QStringLiteral("Choose valid native colors and a bounded vector stroke width.");
  return {};
}

inline QImage render_vector_path_frame(
    const QSize& size, const QVariantMap& parameters, double local_seconds) {
  if (parameters.contains(QStringLiteral("scene"))) {
    const auto scene = prepare_rig_scene(parameters.value(QStringLiteral("scene")).toMap());
    return scene ? render_rig_scene_frame(size, *scene, local_seconds) : QImage{};
  }
  if (!size.isValid() || size.isEmpty() || !std::isfinite(local_seconds) ||
      !validate_vector_path_parameters(parameters).isEmpty()) return {};
  QVariantList coordinates;
  const auto commands = parameters.value(QStringLiteral("commands")).toList();
  for (const auto& entry : commands)
    coordinates.append(entry.toMap().value(QStringLiteral("points")).toList());
  const auto frames = parameters.value(QStringLiteral("frames")).toList();
  if (!frames.isEmpty()) {
    auto upper = qsizetype{0};
    while (upper < frames.size() &&
           frames.at(upper).toMap().value(QStringLiteral("seconds")).toDouble() < local_seconds)
      ++upper;
    if (upper == 0 || upper == frames.size()) {
      coordinates = frames.at(upper == 0 ? 0 : frames.size()-1).toMap()
                        .value(QStringLiteral("coordinates")).toList();
    } else {
      const auto before = frames.at(upper-1).toMap();
      const auto after = frames.at(upper).toMap();
      const auto a = before.value(QStringLiteral("coordinates")).toList();
      const auto b = after.value(QStringLiteral("coordinates")).toList();
      const auto start = before.value(QStringLiteral("seconds")).toDouble();
      const auto end = after.value(QStringLiteral("seconds")).toDouble();
      const auto amount = std::clamp((local_seconds-start)/(end-start), 0.0, 1.0);
      coordinates.clear();
      coordinates.reserve(a.size());
      for (qsizetype i = 0; i < a.size(); ++i)
        coordinates.append(a.at(i).toDouble() + (b.at(i).toDouble()-a.at(i).toDouble())*amount);
    }
  }
  QPainterPath path;
  qsizetype offset = 0;
  const auto point = [&coordinates](qsizetype index) {
    return QPointF(coordinates.at(index).toDouble(), coordinates.at(index+1).toDouble());
  };
  for (const auto& entry : commands) {
    const auto command = entry.toMap();
    const auto verb = command.value(QStringLiteral("verb")).toString();
    if (verb == QStringLiteral("M")) path.moveTo(point(offset));
    else if (verb == QStringLiteral("L")) path.lineTo(point(offset));
    else if (verb == QStringLiteral("C")) path.cubicTo(point(offset), point(offset+2), point(offset+4));
    else if (verb == QStringLiteral("Q")) path.quadTo(point(offset), point(offset+2));
    else path.closeSubpath();
    offset += command.value(QStringLiteral("points")).toList().size();
  }
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.scale(size.width()/parameters.value(QStringLiteral("viewBoxWidth")).toDouble(),
                size.height()/parameters.value(QStringLiteral("viewBoxHeight")).toDouble());
  const auto stroke = QColor(parameters.value(QStringLiteral("stroke"), QStringLiteral("#00000000")).toString());
  const auto stroke_width = parameters.value(QStringLiteral("strokeWidth"), 1.0).toDouble();
  painter.setPen(stroke.alpha() == 0 || stroke_width == 0 ? QPen(Qt::NoPen)
      : QPen(stroke, stroke_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(QColor(parameters.value(QStringLiteral("fill"), QStringLiteral("#00000000")).toString()));
  painter.drawPath(path);
  return image;
}

}  // namespace photo_editor::desktop::timeline_motion

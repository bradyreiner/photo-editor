#pragma once

#include "timeline_vector_path.h"
#include "timeline_fractal_noise.h"

#include <QColor>
#include <QString>
#include <QVariantMap>

#include <cmath>

namespace photo_editor::desktop {

inline bool timeline_source_is_generated(const QString& media_type) {
  return media_type == QStringLiteral("title") || media_type == QStringLiteral("caption") ||
         media_type == QStringLiteral("adjustment") || media_type == QStringLiteral("motion-generator");
}

inline QString timeline_motion_source_error(const QString& kind, const QVariantMap& parameters) {
  if (kind == QStringLiteral("transition-veil")) return {};
  if (kind == QStringLiteral("fractal-noise")) {
    timeline_motion::FractalNoiseParameters parsed;
    QString error;
    if (timeline_motion::fractal_noise_parameters_from_variant_map(
            parameters, &parsed, nullptr, &error)) {
      return {};
    }
    return error.isEmpty()
               ? QStringLiteral("The fractal-noise generator parameters are invalid.")
               : error;
  }
  if (kind == QStringLiteral("vector-path"))
    return timeline_motion::validate_vector_path_parameters(
        parameters.value(QStringLiteral("vectorPath")).toMap());
  if (kind == QStringLiteral("desktop-light")) {
    const auto time = parameters.value(QStringLiteral("timeOffsetSeconds"), 0.0).toDouble();
    const auto scale = parameters.value(QStringLiteral("timeScale"), 1.0).toDouble();
    const auto gather = parameters.value(QStringLiteral("gather"), 0.0).toDouble();
    if (std::isfinite(time) && std::isfinite(scale) && std::isfinite(gather) &&
        time >= -3600.0 && time <= 3600.0 && scale >= 0.0 && scale <= 100.0 &&
        gather >= 0.0 && gather <= 1.0) return {};
    return QStringLiteral("The light generator contains invalid timing or gather controls.");
  }
  if (kind == QStringLiteral("linear-gradient")) {
    const auto angle = parameters.value(QStringLiteral("angleDegrees"), 0.0).toDouble();
    const auto stops = parameters.value(QStringLiteral("stops")).toList();
    if (!std::isfinite(angle) || angle < -360.0 || angle > 360.0 ||
        stops.size() < 2 || stops.size() > 8)
      return QStringLiteral("The gradient requires a valid angle and 2 to 8 ordered stops.");
    double previous = -1.0;
    for (const auto& entry : stops) {
      const auto stop = entry.toMap();
      const auto position = stop.value(QStringLiteral("position")).toDouble();
      const auto opacity = stop.value(QStringLiteral("opacity"), 1.0).toDouble();
      if (!std::isfinite(position) || position < 0.0 || position > 1.0 || position <= previous ||
          !std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0 ||
          !QColor(stop.value(QStringLiteral("color")).toString()).isValid())
        return QStringLiteral("The gradient contains an invalid color, position, or opacity.");
      previous = position;
    }
    return {};
  }
  return QStringLiteral("This native motion generator is unavailable in this build.");
}

}  // namespace photo_editor::desktop

#pragma once

#include "photo_text_rasterizer.h"

#include <QImage>
#include <QPainter>
#include <QSize>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>

// Build an acceptance mask from the same native text raster used by the photo
// compositor, then place it with the layer's recipe geometry. This mask is
// coverage, not a text bounding rectangle: transparent leading/trailing space
// inside the text box remains protected photo.
inline QImage agent_native_text_alpha_mask(
    const QVariantMap& layer,
    const QSize& canvas_size,
    QString* error = nullptr) {
  if (error != nullptr) error->clear();
  const auto fail = [error](const QString& message) {
    if (error != nullptr) *error = message;
    return QImage{};
  };
  if (layer.value(QStringLiteral("kind")).toString() !=
      QStringLiteral("text")) {
    return fail(QStringLiteral("The native glyph mask requires a text layer."));
  }
  if (canvas_size.isEmpty() ||
      std::abs(layer.value(QStringLiteral("textPerspectiveHorizontal"))
                   .toDouble()) > 1.0e-9 ||
      std::abs(layer.value(QStringLiteral("textPerspectiveVertical"))
                   .toDouble()) > 1.0e-9) {
    return fail(QStringLiteral(
        "The native glyph mask does not support perspective-warped text."));
  }

  QString raster_error;
  const auto local = photo_editor::desktop::rasterize_legacy_photo_text(
      layer, {}, &raster_error);
  if (local.isNull()) {
    return fail(raster_error.isEmpty()
        ? QStringLiteral("The native text raster is empty.")
        : raster_error);
  }
  QImage full(canvas_size, QImage::Format_ARGB32_Premultiplied);
  if (full.isNull()) {
    return fail(QStringLiteral("Could not allocate the native glyph mask."));
  }
  full.fill(Qt::transparent);

  const auto scale = qBound(
      0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
  const auto rotation = layer.value(QStringLiteral("rotation"), 0.0).toDouble();
  const auto x = layer.value(QStringLiteral("x")).toDouble();
  const auto y = layer.value(QStringLiteral("y")).toDouble();
  const auto width = layer.value(QStringLiteral("pixelWidth"), local.width())
                         .toDouble();
  const auto height = layer.value(QStringLiteral("pixelHeight"), local.height())
                          .toDouble();
  QPainter painter(&full);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter.translate(x + width * scale * 0.5, y + height * scale * 0.5);
  painter.rotate(rotation);
  painter.scale(scale, scale);
  painter.drawImage(QPointF(-local.width() * 0.5, -local.height() * 0.5), local);
  painter.end();
  return full;
}

inline QImage agent_native_text_coverage_mask(
    const QVariantList& layers,
    const QSize& canvas_size,
    QString* error = nullptr) {
  if (error != nullptr) error->clear();
  if (canvas_size.isEmpty()) {
    if (error != nullptr) *error = QStringLiteral("The native glyph canvas is empty.");
    return {};
  }
  QImage coverage(canvas_size, QImage::Format_ARGB32_Premultiplied);
  if (coverage.isNull()) {
    if (error != nullptr) *error = QStringLiteral("Could not allocate the native glyph coverage mask.");
    return {};
  }
  coverage.fill(Qt::transparent);
  QPainter painter(&coverage);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    QString layer_error;
    const auto alpha = agent_native_text_alpha_mask(
        layer, canvas_size, &layer_error);
    if (alpha.isNull()) {
      painter.end();
      if (error != nullptr) *error = layer_error;
      return {};
    }
    painter.drawImage(QPoint(0, 0), alpha);
  }
  painter.end();
  return coverage;
}

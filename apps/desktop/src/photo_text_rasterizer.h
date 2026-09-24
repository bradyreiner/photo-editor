#pragma once

#include "photo_text_layout.h"
#include "photo_point_text.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QVariantMap>

namespace photo_editor::desktop {

inline Qt::AlignmentFlag legacy_photo_text_alignment(const QVariantMap& layer) {
  const auto alignment = layer.value(QStringLiteral("textAlignment")).toString().trimmed().toLower();
  if (alignment == QStringLiteral("center")) return Qt::AlignHCenter;
  if (alignment == QStringLiteral("right")) return Qt::AlignRight;
  if (alignment == QStringLiteral("justify")) return Qt::AlignJustify;
  return Qt::AlignLeft;
}

// Legacy recipe layout stays fixed until an explicit typography version upgrades it.
inline QImage rasterize_legacy_photo_text(const QVariantMap& layer, const QSize& requested_size = {}, QString* error = nullptr) {
  if (error) error->clear();
  const auto fail = [error](const QString& message) {
    if (error) *error = message;
    return QImage{};
  };
  const auto text = layer.value(QStringLiteral("text"), QStringLiteral("Text")).toString();
  const auto width = qBound(16, layer.value(QStringLiteral("pixelWidth"), 512).toInt(), 8192);
  const auto height = qBound(16, layer.value(QStringLiteral("pixelHeight"), 128).toInt(), 8192);
  if (layer.contains(QStringLiteral("textTypography"))) {
    QString style_error;
    const auto style = decode_photo_text_style(
        QJsonObject::fromVariantMap(layer.value(QStringLiteral("textTypography")).toMap()), &style_error);
    if (!style) return fail(style_error);
    const auto raster_size = requested_size == QSize() ? QSize(width, height) : requested_size;
    auto result = rasterize_photo_text(text, *style, QSizeF(width, height), raster_size,
        legacy_photo_text_alignment(layer), 0, style->point_text ? photo_text_origin(layer) : QPointF{});
    if (!result.error.isEmpty()) return fail(result.error);
    return result.image;
  }
  auto color = QColor(layer.value(QStringLiteral("textColor"), QStringLiteral("#ffffff")).toString());
  if (!color.isValid()) {
    color = QColor(QStringLiteral("#ffffff"));
  }

  const auto raster_size = requested_size == QSize() ? QSize(width, height) : requested_size;
  if (raster_size.width() <= 0 || raster_size.height() <= 0 ||
      raster_size.width() > 16384 || raster_size.height() > 16384 ||
      static_cast<qint64>(raster_size.width()) * raster_size.height() > 64LL * 1024 * 1024)
    return fail(QStringLiteral("Text raster dimensions exceed the supported size or memory budget."));
  if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr)
    return fail(QStringLiteral("Text rendering requires an initialized font runtime."));
  QImage image(raster_size, QImage::Format_RGBA8888);
  if (image.isNull()) return fail(QStringLiteral("Could not allocate the text raster."));
  image.setDotsPerMeterX(3780);
  image.setDotsPerMeterY(3780);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.scale(static_cast<double>(raster_size.width()) / width,
      static_cast<double>(raster_size.height()) / height);
  QFont font(layer.value(QStringLiteral("fontFamily"), QStringLiteral("Helvetica")).toString());
  font.setPointSizeF(qBound(0.25, layer.value(QStringLiteral("fontSize"), 48).toDouble(), 8192.0) * 72.0 / 96.0);
  font.setBold(layer.value(QStringLiteral("fontWeight")).toString().trimmed().toLower() ==
               QStringLiteral("bold"));
  painter.setFont(font);
  painter.setPen(color);
  const auto text_flags = static_cast<int>(legacy_photo_text_alignment(layer)) |
                          static_cast<int>(Qt::AlignVCenter) |
                          static_cast<int>(Qt::TextWordWrap);
  painter.drawText(
      QRect(0, 0, width, height).adjusted(8, 4, -8, -4),
      text_flags,
      text);
  return image;
}

}  // namespace photo_editor::desktop

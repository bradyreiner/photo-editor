#pragma once

#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <cmath>

namespace photo_editor::desktop {

// Generic delivery sizes, independent of changing platform upload requirements.
inline QVariantList campaign_delivery_formats() {
  return {
      QVariantMap{{"id", "original"}, {"name", "Original"}, {"width", 0}, {"height", 0}},
      QVariantMap{{"id", "square"}, {"name", "Square"}, {"width", 1080}, {"height", 1080}},
      QVariantMap{{"id", "portrait"}, {"name", "Portrait"}, {"width", 1080}, {"height", 1350}},
      QVariantMap{{"id", "story"}, {"name", "Story"}, {"width", 1080}, {"height", 1920}},
      QVariantMap{{"id", "landscape"}, {"name", "Landscape"}, {"width", 1920}, {"height", 1080}},
  };
}

inline QStringList normalized_campaign_delivery_formats(const QStringList& ids) {
  QStringList result;
  for (const auto& value : campaign_delivery_formats()) {
    const auto id = value.toMap().value(QStringLiteral("id")).toString();
    if (ids.contains(id)) result.append(id);
  }
  return result;
}

struct CampaignDeliveryGeometry {
  QSize render_size;
  QRectF placement;
  bool cropped = false;
  bool valid = false;
};

inline CampaignDeliveryGeometry campaign_delivery_geometry(
    const QSize& source, const QSize& output, bool fill) {
  CampaignDeliveryGeometry result;
  if (source.isEmpty() || output.isEmpty() || output.width() > 16384 || output.height() > 16384 ||
      qint64(output.width()) * output.height() > 64LL * 1024 * 1024) return result;
  const double horizontal = double(output.width()) / source.width();
  const double vertical = double(output.height()) / source.height();
  const double scale = fill ? qMax(horizontal, vertical) : qMin(horizontal, vertical);
  const double width = source.width() * scale;
  const double height = source.height() * scale;
  if (width > 16384 || height > 16384 || std::ceil(width) * std::ceil(height) > 64.0 * 1024 * 1024)
    return result;
  result.render_size = QSize(qMax(1, int(std::ceil(width))), qMax(1, int(std::ceil(height))));
  result.placement = QRectF((output.width() - width) * 0.5, (output.height() - height) * 0.5, width, height);
  result.cropped = fill && (width > output.width() + 0.01 || height > output.height() + 0.01);
  result.valid = true;
  return result;
}

}  // namespace photo_editor::desktop

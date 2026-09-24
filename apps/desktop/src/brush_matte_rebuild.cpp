#include "brush_matte_rebuild.h"

#include "brush_input_schema.h"
#include "brush_rasterizer.h"
#include "edit_recipe_mask_schema.h"

#include <algorithm>

#include <QElapsedTimer>

namespace photo_editor::desktop {

QVariantMap mask_with_rebuilt_brush_matte(
    const QVariantMap& mask,
    const QSize& target_size,
    const QString& purpose) {
  QElapsedTimer rebuild_timer;
  rebuild_timer.start();
  const auto target_width = std::max(1, target_size.width());
  const auto target_height = std::max(1, target_size.height());
  if (normalized_mask_kind(mask.value(QStringLiteral("kind")).toString()) !=
          QStringLiteral("brush") ||
      target_width <= 1 || target_height <= 1) {
    return {};
  }

  const auto points = brush_points_for_target_raster_replay(mask);
  if (points.isEmpty()) {
    return {};
  }

  auto rebuilt_mask = mask;
  rebuilt_mask.insert(
      QStringLiteral("segmentationData"),
      QByteArray(target_width * target_height, char(0)));
  rebuilt_mask.insert(QStringLiteral("segmentationWidth"), target_width);
  rebuilt_mask.insert(QStringLiteral("segmentationHeight"), target_height);
  const auto feather = mask.value(QStringLiteral("feather"), 0.75).toDouble();
  for (const auto& point : points) {
    (void)rasterize_brush_point_into_mask(
        &rebuilt_mask,
        nullptr,
        point.x,
        point.y,
        point.size,
        point.flow,
        point.opacity,
        feather,
        point.erase,
        false,
        0.18,
        point.auto_mask_anchor_x,
        point.auto_mask_anchor_y);
  }
  rebuilt_mask.insert(QStringLiteral("scaledMattePurpose"), purpose);
  rebuilt_mask.insert(
      QStringLiteral("scaledMatteSourceWidth"),
      mask.value(QStringLiteral("segmentationWidth"), target_width).toInt());
  rebuilt_mask.insert(
      QStringLiteral("scaledMatteSourceHeight"),
      mask.value(QStringLiteral("segmentationHeight"), target_height).toInt());
  rebuilt_mask.insert(QStringLiteral("scaledMatteWidth"), target_width);
  rebuilt_mask.insert(QStringLiteral("scaledMatteHeight"), target_height);
  rebuilt_mask.insert(QStringLiteral("scaledMatteRebuiltFromBrushStrokes"), true);
  rebuilt_mask.insert(QStringLiteral("scaledMatteBrushReplayPointCount"), points.size());
  rebuilt_mask.insert(QStringLiteral("scaled_matte_rebuild_ms"), rebuild_timer.elapsed());
  rebuilt_mask.insert(
      QStringLiteral("scaled_matte_rebuild_pixels"),
      static_cast<qint64>(target_width) * static_cast<qint64>(target_height));
  if (purpose == QStringLiteral("full-resolution-render")) {
    rebuilt_mask.insert(
        QStringLiteral("full_resolution_matte_rebuild_ms"),
        rebuild_timer.elapsed());
    rebuilt_mask.insert(
        QStringLiteral("full_resolution_matte_rebuild_pixels"),
        static_cast<qint64>(target_width) * static_cast<qint64>(target_height));
  }
  return rebuilt_mask;
}

}  // namespace photo_editor::desktop

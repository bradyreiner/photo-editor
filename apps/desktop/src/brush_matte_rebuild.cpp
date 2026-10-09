#include "brush_matte_rebuild.h"

#include "brush_input_schema.h"
#include "brush_rasterizer.h"
#include "edit_recipe_mask_schema.h"

#include <algorithm>

#include <QElapsedTimer>

namespace photo_editor::desktop {

QRect rasterize_brush_history_into_mask(
    QVariantMap* mask, const QImage* source_image,
    photo_editor::pipeline::CancellationToken cancellation,
    bool* cancelled, qint64* candidate_pixels, qint64* edge_pixels) {
  if (cancelled) *cancelled = false;
  if (!mask) return {};
  QRect dirty;
  auto alpha = mask->value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask->value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask->value(QStringLiteral("segmentationHeight")).toInt();
  auto strokes = mask->value(QStringLiteral("brushStrokes")).toList();
  if (strokes.isEmpty()) {
    strokes.append(QVariantMap{{QStringLiteral("samples"), mask->value(QStringLiteral("brushPoints"))}});
  }
  for (const auto& stroke_value : strokes) {
    const auto stroke = stroke_value.toMap();
    const auto settings = stroke.value(QStringLiteral("settings")).toMap();
    const auto version2 = settings.value(QStringLiteral("rasterVersion")).toInt() >= 2;
    // Old recipes used the mask's feather for the entire history. Version 2
    // records the tip per stroke so later tool changes cannot repaint history.
    const auto feather = version2 ? settings.value(QStringLiteral("feather"), 0.75).toDouble()
                                 : mask->value(QStringLiteral("feather"), 0.75).toDouble();
    const BrushRasterGeometry geometry{.circular = version2,
        .hardness = settings.value(QStringLiteral("hardness"), 0.25).toDouble()};
    const auto auto_mask = settings.value(QStringLiteral("autoMask"),
        mask->value(QStringLiteral("brushAutoMask"), false)).toBool();
    const auto protect_edges = settings.value(QStringLiteral("protectEdges"),
        mask->value(QStringLiteral("brushProtectEdges"), false)).toBool();
    const auto tolerance = settings.value(QStringLiteral("autoMaskTolerance"),
        mask->value(QStringLiteral("brushAutoMaskTolerance"), 0.18)).toDouble();
    for (const auto& sample : stroke.value(QStringLiteral("samples")).toList()) {
      auto point = brush_preview_point_from_variant(sample, 0.5, 0.5,
          mask->value(QStringLiteral("brushSize"), 0.08).toDouble(),
          mask->value(QStringLiteral("brushFlow"), 1.0).toDouble());
      if (stroke.value(QStringLiteral("operation")).toString() == QStringLiteral("erase")) point.erase = true;
      bool point_cancelled = false;
      dirty = dirty.united(rasterize_brush_point_into_alpha(&alpha, width, height, source_image,
          point.x, point.y, point.size, point.flow, point.opacity, feather, point.erase,
          auto_mask, tolerance, point.auto_mask_anchor_x, point.auto_mask_anchor_y,
          protect_edges, cancellation, &point_cancelled, candidate_pixels, edge_pixels, geometry));
      if (point_cancelled) {
        if (cancelled) *cancelled = true;
        return {};
      }
    }
  }
  mask->insert(QStringLiteral("segmentationData"), alpha);
  return dirty;
}

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
  // Source-aware strokes cannot be reconstructed without their source colors.
  // Keep their committed matte and let the caller scale that authoritative alpha.
  if (mask.value(QStringLiteral("brushAutoMask")).toBool() ||
      mask.value(QStringLiteral("brushProtectEdges")).toBool()) return {};
  for (const auto& stroke : mask.value(QStringLiteral("brushStrokes")).toList()) {
    const auto settings = stroke.toMap().value(QStringLiteral("settings")).toMap();
    if (settings.value(QStringLiteral("autoMask")).toBool() ||
        settings.value(QStringLiteral("protectEdges")).toBool()) return {};
  }
  (void)rasterize_brush_history_into_mask(&rebuilt_mask);
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

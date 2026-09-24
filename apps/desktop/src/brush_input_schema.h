#pragma once

#include "brush_preview_compositor.h"

#include <QRectF>
#include <QSize>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace photo_editor::desktop {

[[nodiscard]] bool ensure_brush_raster_mask(QVariantMap* mask, const QSize& raster_size);

[[nodiscard]] QVariantMap brush_point_variant(
    double normalized_x, double normalized_y, double brush_size, double brush_flow,
    double brush_opacity, double auto_mask_anchor_x, double auto_mask_anchor_y,
    double pressure = 1.0, double tilt_x = 0.0, double tilt_y = 0.0,
    bool erase = false);

[[nodiscard]] QVariantMap brush_point_variant(const BrushPreviewPoint& point);

[[nodiscard]] QVariantMap brush_stroke_variant(
    const QVariantMap& mask, const QVector<BrushPreviewPoint>& samples, int created_at_order);

[[nodiscard]] BrushPreviewPoint brush_preview_point(
    double normalized_x, double normalized_y, double brush_size, double brush_flow,
    double pressure_size_response, double pressure_flow_response,
    double pressure_opacity_response, double pressure = 1.0, double tilt_x = 0.0,
    double tilt_y = 0.0, bool erase = false);

[[nodiscard]] BrushPreviewPoint brush_preview_point_from_variant(
    const QVariant& value, double fallback_x, double fallback_y,
    double fallback_size, double fallback_flow);

[[nodiscard]] QVector<BrushPreviewPoint> brush_points_for_target_raster_replay(
    const QVariantMap& mask);

[[nodiscard]] QRectF dirty_region_for_brush_points(
    const QVector<BrushPreviewPoint>& points);

[[nodiscard]] int rounded_brush_decode_edge(double edge);

[[nodiscard]] int auto_mask_source_decode_edge_for_brush_commit(
    const QVector<BrushPreviewPoint>& points, const QRectF& dirty_region,
    int existing_width, int existing_height, bool existing_segmentation_valid);

}  // namespace photo_editor::desktop

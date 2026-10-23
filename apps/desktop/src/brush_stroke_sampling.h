#pragma once

#include "brush_preview_compositor.h"

#include <QVector>

#include <functional>

namespace photo_editor::desktop {

[[nodiscard]] double normalized_brush_dab_spacing(
    double brush_size, double brush_spacing);

[[nodiscard]] QVector<BrushPreviewPoint> sampled_brush_points_for_segment(
    const BrushPreviewPoint* last_point, double normalized_x, double normalized_y,
    double brush_size, double brush_flow, double pressure_size_response,
    double pressure_flow_response, double pressure_opacity_response,
    double brush_spacing, double brush_smoothing, bool erase,
    double pressure = 1.0, double tilt_x = 0.0, double tilt_y = 0.0,
    double image_aspect = 0.0);

[[nodiscard]] BrushPreviewPoint interpolated_brush_preview_point(
    const BrushPreviewPoint& start, const BrushPreviewPoint& end, double t);

[[nodiscard]] QVector<BrushPreviewPoint> resampled_committed_brush_stroke(
    const QVector<BrushPreviewPoint>& stroke_points, double brush_spacing,
    double image_aspect = 0.0,
    std::function<bool()> is_cancelled = {});

}  // namespace photo_editor::desktop

#pragma once

#include "timeline_frame_quad_warp.h"

#include <QMargins>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QTransform>

namespace photo_editor::desktop::timeline_motion::frame_warp {

// Describes a symmetric local canvas large enough to retain all pixels
// projected outside the nominal frame. The expanded input-to-output
// transform is T(pad) * local_transform * T(-pad), so the original frame
// center remains the center of the expanded canvas.
struct FrameQuadOverscanPlan final {
  QSize original_size;
  QSize output_size;
  QMargins padding;
  QPoint original_origin;
  QPointF original_center_in_output;
  QTransform local_transform;
  QString perspective_filter;
  QString visible_content_alpha_filter;
  FrameQuad expanded_canvas_destination_quad;
  FrameQuad visible_content_quad;
};

[[nodiscard]] bool make_frame_quad_overscan_plan(
    const QSize& original_size,
    const FrameQuad& quad,
    FrameQuadOverscanPlan* plan,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop::timeline_motion::frame_warp

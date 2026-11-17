#pragma once

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QTransform>
#include <QVariantMap>

#include <array>

namespace photo_editor::desktop::timeline_motion::frame_warp {

inline constexpr double kMinimumNormalizedCorner = -4.0;
inline constexpr double kMaximumNormalizedCorner = 4.0;
inline constexpr double kQuadConvexityEpsilon = 1.0e-9;

// Normalized edge coordinates, ordered top-left, top-right, bottom-right,
// bottom-left. Values outside [0, 1] are intentional for perspective crops.
struct FrameQuad final {
  std::array<QPointF, 4> points{
      QPointF(0.0, 0.0), QPointF(1.0, 0.0),
      QPointF(1.0, 1.0), QPointF(0.0, 1.0)};
};

[[nodiscard]] bool validate_frame_quad(
    const FrameQuad& quad,
    QString* error_message = nullptr);

// QVariant/JSON contract for persistence and QML projection:
// {enabled: true, points: [{x, y}, ...]} in TL, TR, BR, BL order.
[[nodiscard]] QVariantMap frame_quad_to_variant_map(
    const FrameQuad& quad,
    bool enabled = true);
[[nodiscard]] bool frame_quad_from_variant_map(
    const QVariantMap& value,
    FrameQuad* quad,
    QString* error_message = nullptr);

[[nodiscard]] bool frame_quad_to_transform(
    const QSize& source_size,
    const QSize& output_size,
    const FrameQuad& quad,
    QTransform* transform,
    QString* error_message = nullptr);

// Projects a centered rectangle through CSS perspective(d) rotateY(angle),
// then returns its four normalized edge corners on the original canvas.
[[nodiscard]] bool css_perspective_rotate_y_quad(
    const QSize& frame_size,
    double perspective_distance_pixels,
    double angle_degrees,
    FrameQuad* quad,
    QString* error_message = nullptr);

[[nodiscard]] QImage render_frame_quad_warp(
    const QImage& source,
    const QSize& output_size,
    const FrameQuad& quad,
    QString* error_message = nullptr);

// Emits FFmpeg's `perspective` filter with the same destination corner values
// used by frame_quad_to_transform/render_frame_quad_warp.
[[nodiscard]] QString ffmpeg_destination_perspective_filter(
    const QSize& output_size,
    const FrameQuad& quad,
    QString* error_message = nullptr);

// Warps a frame in its local pixel domain and clears pixels outside the
// destination quad, matching render_frame_quad_warp's transparent canvas.
[[nodiscard]] QString ffmpeg_destination_perspective_alpha_filter(
    const QSize& output_size,
    const FrameQuad& quad,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop::timeline_motion::frame_warp

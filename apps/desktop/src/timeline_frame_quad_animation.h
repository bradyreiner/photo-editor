#pragma once

#include "timeline_frame_quad_warp.h"

#include <QMargins>
#include <QSize>
#include <QString>
#include <QVector>

#include <array>

namespace photo_editor::desktop::timeline_motion::frame_warp {

// These eight scalar channels use the same local-seconds keyframe clock as the
// other Timeline framing parameters. Point order is TL, TR, BR, BL.
enum class FrameQuadChannel : std::size_t {
  TopLeftX = 0,
  TopLeftY,
  TopRightX,
  TopRightY,
  BottomRightX,
  BottomRightY,
  BottomLeftX,
  BottomLeftY,
};

struct FrameQuadScalarKeyframe final {
  double local_seconds = 0.0;
  double value = 0.0;
  // The keyframe's interpolation controls the interval that follows it.
  bool hold = false;
};

using FrameQuadAnimation =
    std::array<QVector<FrameQuadScalarKeyframe>, 8>;

struct FrameQuadAnimationOverscanPlan final {
  QSize original_size;
  QSize output_size;
  QMargins padding;
  QPointF center;
};

[[nodiscard]] QString frame_quad_channel_parameter(FrameQuadChannel channel);
[[nodiscard]] bool is_frame_quad_channel_parameter(const QString& parameter);
[[nodiscard]] bool frame_quad_channel_from_parameter(
    const QString& parameter,
    FrameQuadChannel* channel);

[[nodiscard]] bool frame_quad_animation_has_keyframes(
    const FrameQuadAnimation& animation);

// Validates every keyframe and every interpolated quad. For each unioned
// interval the four convexity cross-products are quadratic, so endpoints and
// each interior extremum provide a bounded exact check for linear/hold curves.
[[nodiscard]] bool validate_frame_quad_animation(
    const FrameQuad& fallback,
    const FrameQuadAnimation& animation,
    double duration_seconds,
    QString* error_message = nullptr);

[[nodiscard]] FrameQuad evaluate_frame_quad_animation(
    const FrameQuad& fallback,
    const FrameQuadAnimation& animation,
    double local_seconds);

// Computes one symmetric output canvas large enough for every animated pose.
// Piecewise-linear coordinate extrema occur at their unioned keyframe times.
[[nodiscard]] bool make_frame_quad_animation_overscan_plan(
    const QSize& original_size,
    const FrameQuad& fallback,
    const FrameQuadAnimation& animation,
    double duration_seconds,
    FrameQuadAnimationOverscanPlan* plan,
    QString* error_message = nullptr);

// Conjugates one pose through the plan's padded canvas. The returned
// content_quad marks the transformed original frame inside the overscan.
[[nodiscard]] bool conjugate_frame_quad_for_overscan(
    const QSize& original_size,
    const QMargins& padding,
    const FrameQuad& quad,
    FrameQuad* expanded_quad,
    FrameQuad* content_quad,
    QString* error_message = nullptr);

// Builds per-frame FFmpeg filters from the same eight normalized corner
// expressions. The input is already padded by `padding`; the projective map is
// conjugated through that padding so the original frame keeps its exact pose.
[[nodiscard]] QString ffmpeg_animated_destination_perspective_filter(
    const QSize& original_size,
    const QMargins& padding,
    const std::array<QString, 8>& corner_expressions,
    QString* error_message = nullptr);
[[nodiscard]] QString ffmpeg_animated_destination_perspective_alpha_filter(
    const QSize& original_size,
    const QMargins& padding,
    const std::array<QString, 8>& corner_expressions,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop::timeline_motion::frame_warp

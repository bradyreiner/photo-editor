#pragma once

#include <QString>
#include <QtGlobal>

#include <cmath>
#include <limits>

namespace photo_editor::desktop::timeline_motion::bounded_h264 {

// FFconcat otherwise infers each MP4 container's rounded duration. Round
// cumulative frame boundaries, rather than individual window lengths, so
// microsecond rounding cannot accumulate across a long sequence.
[[nodiscard]] inline QString concat_frame_duration_directive(
    qint64 start_frame, qint64 end_frame, double frames_per_second) {
  if (start_frame < 0 || end_frame <= start_frame ||
      !std::isfinite(frames_per_second) || frames_per_second <= 0.0) {
    return {};
  }
  const auto boundary = [frames_per_second](qint64 frame) {
    return static_cast<long double>(frame) * 1000000.0L /
        static_cast<long double>(frames_per_second);
  };
  const auto start = boundary(start_frame);
  const auto end = boundary(end_frame);
  // Leave a full tick of headroom before llround's conversion.
  const auto limit = static_cast<long double>(std::numeric_limits<qint64>::max()) - 1.0L;
  if (!std::isfinite(start) || !std::isfinite(end) || end >= limit) {
    return {};
  }
  const auto duration = std::llround(end) - std::llround(start);
  if (duration <= 0) {
    return {};
  }
  return QStringLiteral("duration %1.%2")
      .arg(duration / 1000000)
      .arg(duration % 1000000, 6, 10, QLatin1Char('0'));
}

}  // namespace photo_editor::desktop::timeline_motion::bounded_h264

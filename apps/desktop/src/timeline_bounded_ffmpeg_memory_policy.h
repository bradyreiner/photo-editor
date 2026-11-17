#pragma once

#include <QtGlobal>
#include <QString>

#include <cmath>
#include <limits>
#include <numeric>

namespace photo_editor::desktop::timeline_motion::bounded_h264 {

// Decoder and filtergraph residency grows with frames held by the active
// graph, beyond the raw source-image estimate. Keep a hard frame ceiling in
// addition to the existing byte planner so dense full-resolution graphs do
// not turn a four-second cache window into a multi-gigabyte working set.
inline constexpr double kBoundedWindowMaximumSeconds = 4.0;
inline constexpr qint64 kBoundedWindowMaximumFrames = 16;

[[nodiscard]] inline qint64 bounded_window_frame_limit(
    double frames_per_second,
    double maximum_seconds = kBoundedWindowMaximumSeconds) {
  if (!std::isfinite(frames_per_second) || frames_per_second <= 0.0 ||
      !std::isfinite(maximum_seconds) || maximum_seconds <= 0.0) {
    return 0;
  }
  const auto seconds_limited_frames =
      std::floor(maximum_seconds * frames_per_second + 1.0e-9);
  if (!std::isfinite(seconds_limited_frames) ||
      seconds_limited_frames >= static_cast<double>(kBoundedWindowMaximumFrames)) {
    return kBoundedWindowMaximumFrames;
  }
  return qMax<qint64>(
      1, static_cast<qint64>(seconds_limited_frames));
}

[[nodiscard]] inline qint64 bounded_window_count(qint64 total_frames,
                                                 qint64 frame_limit) {
  if (total_frames <= 0 || frame_limit <= 0) {
    return 0;
  }
  return total_frames / frame_limit + (total_frames % frame_limit == 0 ? 0 : 1);
}

// Return an FFmpeg filter sequence that re-anchors selected frames to an
// integer global frame clock. `setpts=N/(fps*TB)` is quantized in whatever
// timebase concat inherited from its source clips (often 1/1000), which can
// make otherwise exact CFR frame windows report a slightly wrong average
// frame rate. Express the stored decimal rate as a reduced micro-rate
// rational, set that frame period as the timebase, then assign integer PTS.
[[nodiscard]] inline QString rational_frame_clock_filter(
    double frames_per_second) {
  constexpr qint64 kRateScale = 1'000'000;
  if (!std::isfinite(frames_per_second) || frames_per_second <= 0.0 ||
      frames_per_second > 240.0) {
    return {};
  }
  const auto scaled_rate = static_cast<long double>(frames_per_second) *
                           static_cast<long double>(kRateScale);
  if (!std::isfinite(scaled_rate) || scaled_rate < 1.0L ||
      scaled_rate > static_cast<long double>(std::numeric_limits<qint64>::max())) {
    return {};
  }
  auto rate_numerator = static_cast<qint64>(std::llround(scaled_rate));
  auto rate_denominator = kRateScale;
  if (rate_numerator <= 0) return {};
  const auto divisor = std::gcd(rate_numerator, rate_denominator);
  rate_numerator /= divisor;
  rate_denominator /= divisor;
  const auto represented_rate = static_cast<double>(rate_numerator) /
                                static_cast<double>(rate_denominator);
  if (!std::isfinite(represented_rate) ||
      std::abs(represented_rate - frames_per_second) > 5.1e-7) {
    return {};
  }
  const auto timestamp_expression = rate_denominator == 1
      ? QStringLiteral("N")
      : QStringLiteral("N*%1").arg(rate_denominator);
  return QStringLiteral("settb=expr=1/%1,setpts=%2")
      .arg(rate_numerator)
      .arg(timestamp_expression);
}

}  // namespace photo_editor::desktop::timeline_motion::bounded_h264

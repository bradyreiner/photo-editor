#pragma once

#include <QByteArray>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>

namespace photo_editor::desktop {

// Frames are decoded by the caller and must be ordered by their source-media time.
// The runtime processes them in order and does not retain a frame cache.
struct TimelineMotionFrame final {
  qint64 source_time_us = 0;
  QImage image;
};

enum class TimelineAnalysisStatus {
  Success,
  Cancelled,
  Unavailable,
  InvalidInput,
  NoTrack,
  Failed,
  MemoryLimitExceeded,
};

struct TimelineMotionSample final {
  qint64 source_time_us = 0;
  // Normalized image coordinates with a top-left origin, matching timeline overlays.
  QRectF normalized_bounds;
  double confidence = 0.0;
  bool tracked = false;
};

struct TimelineMotionResult final {
  TimelineAnalysisStatus status = TimelineAnalysisStatus::Failed;
  QVector<TimelineMotionSample> samples;
  QString message;
};

struct TimelineForegroundMask final {
  qint64 source_time_us = 0;
  int width = 0;
  int height = 0;
  // One 8-bit alpha value per mask pixel, row-major from the image's top-left.
  // Each frame is segmented independently; foreground instance IDs do not imply
  // temporal identity. The longest mask edge is capped at 640 pixels.
  QByteArray alpha;
};

struct TimelineForegroundResult final {
  TimelineAnalysisStatus status = TimelineAnalysisStatus::Failed;
  QVector<TimelineForegroundMask> masks;
  QString message;
};

using TimelineAnalysisProgress = std::function<void(int completed_frames, int total_frames)>;
// Providers decode one frame on demand. The runtime keeps only the current frame,
// which allows a long clip to be analyzed without retaining all decoded frames.
using TimelineMotionFrameProvider =
    std::function<bool(int frame_index, TimelineMotionFrame* frame, QString* error_message)>;

inline constexpr int kTimelineMotionMaximumFrames = 900;
inline constexpr int kTimelineForegroundMaximumDimension = 640;
inline constexpr qsizetype kTimelineForegroundMaximumResultBytes = 256 * 1024 * 1024;

// Shared input/geometry policy helpers. Vision uses a lower-left origin; timeline
// overlays use a top-left origin. These helpers define the conversion at the API edge.
[[nodiscard]] bool timelineMotionInputIsValid(
    const QVector<TimelineMotionFrame>& frames,
    QString* error_message = nullptr);
[[nodiscard]] bool timelineMotionFrameIsValid(
    const TimelineMotionFrame& frame,
    qint64 previous_source_time_us,
    QString* error_message = nullptr);
[[nodiscard]] bool timelineNormalizedBoxIsValid(const QRectF& box);
[[nodiscard]] QRectF timelineTopLeftToBottomLeft(const QRectF& box);
[[nodiscard]] QRectF timelineBottomLeftToTopLeft(const QRectF& box);

// These calls perform synchronous analysis and must be invoked from a worker thread.
// Cancellation is polled between frames. Progress callbacks run on the calling thread.
[[nodiscard]] TimelineMotionResult trackTimelineObject(
    const QVector<TimelineMotionFrame>& frames,
    const QRectF& seed_box_top_left,
    const std::atomic_bool* cancelled = nullptr,
    const TimelineAnalysisProgress& progress = {});
[[nodiscard]] TimelineMotionResult trackTimelineObject(
    int frame_count,
    const TimelineMotionFrameProvider& frame_provider,
    const QRectF& seed_box_top_left,
    const std::atomic_bool* cancelled = nullptr,
    const TimelineAnalysisProgress& progress = {});

[[nodiscard]] TimelineForegroundResult analyzeTimelineForeground(
    const QVector<TimelineMotionFrame>& frames,
    const std::atomic_bool* cancelled = nullptr,
    const TimelineAnalysisProgress& progress = {},
    qsizetype maximum_result_bytes = kTimelineForegroundMaximumResultBytes);
[[nodiscard]] TimelineForegroundResult analyzeTimelineForeground(
    int frame_count,
    const TimelineMotionFrameProvider& frame_provider,
    const std::atomic_bool* cancelled = nullptr,
    const TimelineAnalysisProgress& progress = {},
    qsizetype maximum_result_bytes = kTimelineForegroundMaximumResultBytes);

[[nodiscard]] bool timelineMotionAnalysisAvailable();

}  // namespace photo_editor::desktop

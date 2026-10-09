#include "timeline_motion_runtime.h"

namespace photo_editor::desktop {

TimelineMotionResult trackTimelineObject(
    const QVector<TimelineMotionFrame>& frames,
    const QRectF& seed_box_top_left,
    const std::atomic_bool* cancelled,
    const TimelineAnalysisProgress& progress) {
  QString validation_error;
  if (!timelineMotionInputIsValid(frames, &validation_error)) {
    return {TimelineAnalysisStatus::InvalidInput, {}, validation_error};
  }
  return trackTimelineObject(
      static_cast<int>(frames.size()),
      [&frames](int index, TimelineMotionFrame* frame, QString*) {
        if (frame == nullptr || index < 0 || index >= frames.size()) {
          return false;
        }
        *frame = frames.at(index);
        return true;
      },
      seed_box_top_left,
      cancelled,
      progress);
}

TimelineMotionResult trackTimelineObject(
    int frame_count,
    const TimelineMotionFrameProvider& frame_provider,
    const QRectF& seed_box_top_left,
    const std::atomic_bool* cancelled,
    const TimelineAnalysisProgress& progress) {
  Q_UNUSED(progress);
  if (frame_count < 2 || frame_count > kTimelineMotionMaximumFrames || !frame_provider) {
    return {TimelineAnalysisStatus::InvalidInput, {},
            QStringLiteral("Frame count or decoded frame provider is invalid")};
  }
  if (!timelineNormalizedBoxIsValid(seed_box_top_left)) {
    return {TimelineAnalysisStatus::InvalidInput, {},
            QStringLiteral("The initial object box must be normalized and inside the frame")};
  }
  if (cancelled != nullptr && cancelled->load()) {
    return {TimelineAnalysisStatus::Cancelled, {}, QStringLiteral("Timeline motion analysis was cancelled")};
  }
  TimelineMotionFrame first_frame;
  QString frame_error;
  if (!frame_provider(0, &first_frame, &frame_error) ||
      !timelineMotionFrameIsValid(first_frame, -1, &frame_error)) {
    return {TimelineAnalysisStatus::InvalidInput, {}, frame_error};
  }
  return {TimelineAnalysisStatus::Unavailable, {},
          QStringLiteral("Vision timeline tracking is available only on macOS")};
}

TimelineForegroundResult analyzeTimelineForeground(
    const QVector<TimelineMotionFrame>& frames,
    const std::atomic_bool* cancelled,
    const TimelineAnalysisProgress& progress,
    qsizetype maximum_result_bytes) {
  QString validation_error;
  if (!timelineMotionInputIsValid(frames, &validation_error)) {
    return {TimelineAnalysisStatus::InvalidInput, {}, validation_error};
  }
  return analyzeTimelineForeground(
      static_cast<int>(frames.size()),
      [&frames](int index, TimelineMotionFrame* frame, QString*) {
        if (frame == nullptr || index < 0 || index >= frames.size()) {
          return false;
        }
        *frame = frames.at(index);
        return true;
      },
      cancelled,
      progress,
      maximum_result_bytes);
}

TimelineForegroundResult analyzeTimelineForeground(
    int frame_count,
    const TimelineMotionFrameProvider& frame_provider,
    const std::atomic_bool* cancelled,
    const TimelineAnalysisProgress& progress,
    qsizetype maximum_result_bytes) {
  Q_UNUSED(progress);
  if (frame_count < 2 || frame_count > kTimelineMotionMaximumFrames || !frame_provider ||
      maximum_result_bytes <= 0) {
    return {TimelineAnalysisStatus::InvalidInput, {},
            QStringLiteral("Frame count, decoded frame provider, or output limit is invalid")};
  }
  if (cancelled != nullptr && cancelled->load()) {
    return {TimelineAnalysisStatus::Cancelled, {}, QStringLiteral("Timeline foreground analysis was cancelled")};
  }
  TimelineMotionFrame first_frame;
  QString frame_error;
  if (!frame_provider(0, &first_frame, &frame_error) ||
      !timelineMotionFrameIsValid(first_frame, -1, &frame_error)) {
    return {TimelineAnalysisStatus::InvalidInput, {}, frame_error};
  }
  return {TimelineAnalysisStatus::Unavailable, {},
          QStringLiteral("Vision foreground analysis is available only on macOS 14 or later")};
}

bool timelineMotionAnalysisAvailable() {
  return false;
}

}  // namespace photo_editor::desktop

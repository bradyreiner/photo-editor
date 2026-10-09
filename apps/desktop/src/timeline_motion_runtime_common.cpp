#include "timeline_motion_runtime.h"

#include <cmath>

namespace photo_editor::desktop {

bool timelineNormalizedBoxIsValid(const QRectF& box) {
  return std::isfinite(box.x()) && std::isfinite(box.y()) &&
         std::isfinite(box.width()) && std::isfinite(box.height()) &&
         box.width() > 0.0 && box.height() > 0.0 &&
         box.x() >= 0.0 && box.y() >= 0.0 &&
         box.right() <= 1.0 && box.bottom() <= 1.0;
}

QRectF timelineTopLeftToBottomLeft(const QRectF& box) {
  if (!timelineNormalizedBoxIsValid(box)) {
    return {};
  }
  return QRectF(box.x(), 1.0 - box.y() - box.height(), box.width(), box.height());
}

QRectF timelineBottomLeftToTopLeft(const QRectF& box) {
  if (!timelineNormalizedBoxIsValid(box)) {
    return {};
  }
  return QRectF(box.x(), 1.0 - box.y() - box.height(), box.width(), box.height());
}

bool timelineMotionInputIsValid(
    const QVector<TimelineMotionFrame>& frames,
    QString* error_message) {
  const auto fail = [error_message](const QString& message) {
    if (error_message != nullptr) {
      *error_message = message;
    }
    return false;
  };
  if (frames.size() < 2) {
    return fail(QStringLiteral("At least two decoded frames are required"));
  }
  if (frames.size() > kTimelineMotionMaximumFrames) {
    return fail(QStringLiteral("Frame count exceeds the timeline analysis limit"));
  }

  qint64 previous_time_us = -1;
  for (const auto& frame : frames) {
    QString frame_error;
    if (!timelineMotionFrameIsValid(frame, previous_time_us, &frame_error)) {
      return fail(frame_error);
    }
    previous_time_us = frame.source_time_us;
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

bool timelineMotionFrameIsValid(
    const TimelineMotionFrame& frame,
    qint64 previous_source_time_us,
    QString* error_message) {
  if (frame.source_time_us < 0 || frame.source_time_us <= previous_source_time_us) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Frame source times must be non-negative and strictly increasing");
    }
    return false;
  }
  if (frame.image.isNull() || frame.image.width() < 2 || frame.image.height() < 2 ||
      static_cast<qint64>(frame.image.width()) * frame.image.height() > 33554432) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Frame image is invalid or exceeds the pixel limit");
    }
    return false;
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

}  // namespace photo_editor::desktop

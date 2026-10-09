#include <cstdlib>
#include <iostream>

#include <QImage>
#include <QRectF>

#include "timeline_motion_runtime.h"

namespace {

using namespace photo_editor::desktop;

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QVector<TimelineMotionFrame> valid_frames() {
  QImage frame(64, 48, QImage::Format_RGB32);
  frame.fill(Qt::black);
  return {{1000, frame}, {2000, frame}};
}

}  // namespace

int main() {
  const QRectF top_left(0.12, 0.18, 0.31, 0.27);
  if (!timelineNormalizedBoxIsValid(top_left)) {
    return fail(QStringLiteral("Valid normalized tracking box was rejected"));
  }
  const QRectF bottom_left = timelineTopLeftToBottomLeft(top_left);
  if (qAbs(bottom_left.x() - top_left.x()) > 1.0e-9 ||
      qAbs(bottom_left.y() - 0.55) > 1.0e-9 ||
      timelineBottomLeftToTopLeft(bottom_left) != top_left) {
    return fail(QStringLiteral("Normalized top-left and Vision bottom-left conversions disagree"));
  }
  if (timelineNormalizedBoxIsValid(QRectF(0.8, 0.1, 0.3, 0.2)) ||
      timelineNormalizedBoxIsValid(QRectF(0.2, 0.2, 0.0, 0.2))) {
    return fail(QStringLiteral("Out-of-bounds or empty tracking box was accepted"));
  }

  auto frames = valid_frames();
  QString error;
  if (!timelineMotionInputIsValid(frames, &error)) {
    return fail(QStringLiteral("Valid source-time ordered frames were rejected: %1").arg(error));
  }
  frames[1].source_time_us = frames[0].source_time_us;
  if (timelineMotionInputIsValid(frames, &error) || error.isEmpty()) {
    return fail(QStringLiteral("Duplicate source frame times must be rejected with a diagnostic"));
  }
  frames = valid_frames();
  frames[1].image = {};
  if (timelineMotionInputIsValid(frames, &error)) {
    return fail(QStringLiteral("Missing decoded frames must be rejected"));
  }

  frames = valid_frames();
  std::atomic_bool cancel{true};
  const auto cancelled = trackTimelineObject(frames, top_left, &cancel);
  if (cancelled.status != TimelineAnalysisStatus::Cancelled) {
    return fail(QStringLiteral("A cancelled track must not return synthetic track samples"));
  }
  const auto masks_cancelled = analyzeTimelineForeground(frames, &cancel);
  if (masks_cancelled.status != TimelineAnalysisStatus::Cancelled) {
    return fail(QStringLiteral("Cancelled foreground analysis must report cancellation"));
  }

  if (!timelineMotionAnalysisAvailable()) {
    const auto unavailable = trackTimelineObject(frames, top_left);
    if (unavailable.status != TimelineAnalysisStatus::Unavailable || !unavailable.samples.isEmpty()) {
      return fail(QStringLiteral("Unavailable tracking must not return placeholder motion samples"));
    }
  }
  return EXIT_SUCCESS;
}

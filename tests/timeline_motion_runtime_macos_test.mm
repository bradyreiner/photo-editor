#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QImage>
#include <QPainter>

#include "timeline_motion_runtime.h"

namespace {

using namespace photo_editor::desktop;

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QVector<TimelineMotionFrame> moving_pattern_frames() {
  QVector<TimelineMotionFrame> frames;
  frames.reserve(10);
  for (int index = 0; index < 10; ++index) {
    QImage image(320, 180, QImage::Format_RGB32);
    image.fill(QColor(24, 34, 52));
    QPainter painter(&image);
    painter.fillRect(QRect(54 + index * 5, 58, 78, 64), QColor(212, 138, 42));
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 5; ++x) {
        const auto color = ((x + y) % 2 == 0) ? QColor(248, 214, 126) : QColor(112, 64, 26);
        painter.fillRect(QRect(58 + index * 5 + x * 14, 62 + y * 14, 10, 10), color);
      }
    }
    painter.setPen(QPen(Qt::white, 2));
    painter.drawEllipse(QPoint(92 + index * 5, 90), 9, 9);
    painter.end();
    frames.append({100000 + static_cast<qint64>(index) * 33333, image});
  }
  return frames;
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    QCoreApplication app(argc, argv);
    const auto frames = moving_pattern_frames();
    int provider_reads = 0;
    const TimelineMotionFrameProvider provider = [&frames, &provider_reads](
                                                    int index,
                                                    TimelineMotionFrame* frame,
                                                    QString* error) {
      if (frame == nullptr || index < 0 || index >= frames.size()) {
        if (error != nullptr) {
          *error = QStringLiteral("frame index outside the synthetic clip");
        }
        return false;
      }
      ++provider_reads;
      *frame = frames.at(index);
      return true;
    };
    const auto motion = trackTimelineObject(
        static_cast<int>(frames.size()), provider, QRectF(0.16875, 0.3222, 0.24375, 0.3556));
    if (motion.status != TimelineAnalysisStatus::Success || motion.samples.size() != frames.size()) {
      if (motion.message.contains(QStringLiteral("CVPixelBuffer"), Qt::CaseInsensitive) ||
          motion.message.contains(QStringLiteral("ANE model"), Qt::CaseInsensitive)) {
        std::cerr << "SKIP: native Vision model access is unavailable: "
                  << motion.message.toStdString() << '\n';
        return 77;
      }
      return fail(QStringLiteral("Vision failed to track the synthetic moving object: %1")
                      .arg(motion.message));
    }
    if (provider_reads != frames.size()) {
      return fail(QStringLiteral("The motion runtime did not decode source frames incrementally"));
    }
    if (motion.samples.first().source_time_us != frames.first().source_time_us ||
        motion.samples.last().source_time_us != frames.last().source_time_us ||
        motion.samples.last().normalized_bounds.center().x() <=
            motion.samples.first().normalized_bounds.center().x() + 0.08) {
      return fail(QStringLiteral("Tracked source-time boxes did not follow the synthetic object motion"));
    }

    const bool foreground_fixture_requested = qEnvironmentVariableIsSet(
        "PHOTO_EDITOR_TRACKING_FOREGROUND_FIXTURE");
    if (foreground_fixture_requested) {
      if (!timelineMotionAnalysisAvailable()) {
        return fail(QStringLiteral(
            "Foreground fixture was requested but the native Vision analysis runtime is unavailable"));
      }
      const QString portrait_path = qEnvironmentVariable("PHOTO_EDITOR_TRACKING_FOREGROUND_FIXTURE");
      const QImage portrait_source(portrait_path);
      if (portrait_source.isNull()) {
        return fail(QStringLiteral("Configured foreground fixture cannot be opened: %1")
                        .arg(portrait_path));
      }
      const QImage portrait = portrait_source.scaled(
          512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      const QVector<TimelineMotionFrame> portrait_frames{
          {1000, portrait}, {34000, portrait}};
      const auto foreground = analyzeTimelineForeground(portrait_frames);
      if (foreground.message.contains(QStringLiteral("CVPixelBuffer"), Qt::CaseInsensitive) ||
          foreground.message.contains(QStringLiteral("ANE model"), Qt::CaseInsensitive)) {
        std::cerr << "SKIP: native Vision model access is unavailable: "
                  << foreground.message.toStdString() << '\n';
        return 77;
      }
      if (foreground.status != TimelineAnalysisStatus::Success ||
          foreground.masks.size() != portrait_frames.size()) {
        return fail(QStringLiteral("Vision failed to produce real portrait foreground masks: %1")
                        .arg(foreground.message));
      }
      if (foreground.status == TimelineAnalysisStatus::Success &&
          foreground.masks.size() == portrait_frames.size()) {
        for (int index = 0; index < foreground.masks.size(); ++index) {
          const auto& mask = foreground.masks.at(index);
          if (mask.source_time_us != portrait_frames.at(index).source_time_us ||
              mask.width <= 0 || mask.height <= 0 || mask.width > 640 || mask.height > 640 ||
              mask.alpha.size() != mask.width * mask.height) {
            return fail(QStringLiteral("Foreground output violated its source-time or mask-size contract"));
          }
          const bool has_coverage = std::any_of(
              mask.alpha.cbegin(), mask.alpha.cend(), [](char value) {
                return static_cast<unsigned char>(value) != 0;
              });
          if (!has_coverage) {
            return fail(QStringLiteral("Vision reported a portrait foreground mask with no covered pixels"));
          }
        }
      }
    }
    std::cout << "samples=" << motion.samples.size()
              << " firstX=" << motion.samples.first().normalized_bounds.center().x()
              << " lastX=" << motion.samples.last().normalized_bounds.center().x() << '\n';
    return EXIT_SUCCESS;
  }
}

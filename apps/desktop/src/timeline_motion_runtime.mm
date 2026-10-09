#include "timeline_motion_runtime.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QByteArray>

namespace photo_editor::desktop {
namespace {

CGImageRef cgimage_from_qimage(const QImage& image) {
  const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
  if (rgba.isNull()) {
    return nullptr;
  }
  const auto width = static_cast<size_t>(rgba.width());
  const auto height = static_cast<size_t>(rgba.height());
  const auto row_bytes = static_cast<size_t>(rgba.bytesPerLine());
  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  if (color_space == nullptr) {
    return nullptr;
  }
  CFDataRef data = CFDataCreate(
      nullptr,
      reinterpret_cast<const UInt8*>(rgba.constBits()),
      static_cast<CFIndex>(rgba.sizeInBytes()));
  CGDataProviderRef provider = data ? CGDataProviderCreateWithCFData(data) : nullptr;
  if (data != nullptr) {
    CFRelease(data);
  }
  if (provider == nullptr) {
    CGColorSpaceRelease(color_space);
    return nullptr;
  }
  CGImageRef cg_image = CGImageCreate(
      width,
      height,
      8,
      32,
      row_bytes,
      color_space,
      kCGBitmapByteOrderDefault | static_cast<CGBitmapInfo>(kCGImageAlphaLast),
      provider,
      nullptr,
      false,
      kCGRenderingIntentDefault);
  CGDataProviderRelease(provider);
  CGColorSpaceRelease(color_space);
  return cg_image;
}

TimelineMotionResult fail_motion(TimelineAnalysisStatus status, const QString& message) {
  TimelineMotionResult result;
  result.status = status;
  result.message = message;
  return result;
}

TimelineForegroundResult fail_foreground(
    TimelineAnalysisStatus status,
    const QString& message) {
  TimelineForegroundResult result;
  result.status = status;
  result.message = message;
  return result;
}

QString vision_error(NSError* error, NSString* fallback) {
  const QString message = QString::fromNSString(error != nil ? [error localizedDescription] : fallback);
  return message.trimmed().isEmpty() ? QString::fromNSString(fallback) : message;
}

bool cancellation_requested(const std::atomic_bool* cancelled) {
  return cancelled != nullptr && cancelled->load(std::memory_order_relaxed);
}

QByteArray mask_data_from_pixel_buffer(CVPixelBufferRef buffer) {
  if (buffer == nullptr ||
      CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
    return {};
  }
  const int width = static_cast<int>(CVPixelBufferGetWidth(buffer));
  const int height = static_cast<int>(CVPixelBufferGetHeight(buffer));
  const size_t row_bytes = CVPixelBufferGetBytesPerRow(buffer);
  const auto* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
  QByteArray output;
  if (base != nullptr && width > 0 && height > 0 &&
      static_cast<qint64>(width) * height <= std::numeric_limits<int>::max()) {
    output.resize(width * height);
    const OSType pixel_format = CVPixelBufferGetPixelFormatType(buffer);
    for (int y = 0; y < height; ++y) {
      const uint8_t* row = base + static_cast<size_t>(y) * row_bytes;
      for (int x = 0; x < width; ++x) {
        uint8_t value = 0;
        if (pixel_format == kCVPixelFormatType_OneComponent32Float) {
          const auto* float_row = reinterpret_cast<const float*>(row);
          value = static_cast<uint8_t>(std::lround(std::clamp(float_row[x], 0.0f, 1.0f) * 255.0f));
        } else {
          value = row[x];
        }
        output[y * width + x] = static_cast<char>(value);
      }
    }
  }
  CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
  return output;
}

QByteArray resize_mask_nearest(
    const QByteArray& source,
    int source_width,
    int source_height,
    int width,
    int height) {
  if (source.isEmpty() || source_width <= 0 || source_height <= 0 || width <= 0 || height <= 0) {
    return {};
  }
  QByteArray output(width * height, '\0');
  for (int y = 0; y < height; ++y) {
    const int source_y = std::min(source_height - 1,
                                  static_cast<int>((static_cast<qint64>(y) * source_height) / height));
    for (int x = 0; x < width; ++x) {
      const int source_x = std::min(source_width - 1,
                                    static_cast<int>((static_cast<qint64>(x) * source_width) / width));
      output[y * width + x] = source[source_y * source_width + source_x];
    }
  }
  return output;
}

}  // namespace

TimelineMotionResult trackTimelineObject(
    const QVector<TimelineMotionFrame>& frames,
    const QRectF& seed_box_top_left,
    const std::atomic_bool* cancelled,
    const TimelineAnalysisProgress& progress) {
  QString validation_error;
  if (!timelineMotionInputIsValid(frames, &validation_error)) {
    return fail_motion(TimelineAnalysisStatus::InvalidInput, validation_error);
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
  if (frame_count < 2 || frame_count > kTimelineMotionMaximumFrames || !frame_provider) {
    return fail_motion(
        TimelineAnalysisStatus::InvalidInput,
        QStringLiteral("Frame count or decoded frame provider is invalid"));
  }
  if (!timelineNormalizedBoxIsValid(seed_box_top_left)) {
    return fail_motion(
        TimelineAnalysisStatus::InvalidInput,
        QStringLiteral("The initial object box must be normalized and inside the frame"));
  }
  if (cancellation_requested(cancelled)) {
    return fail_motion(TimelineAnalysisStatus::Cancelled,
                       QStringLiteral("Timeline motion analysis was cancelled"));
  }

  TimelineMotionResult result;
  result.samples.reserve(frame_count);
  TimelineMotionFrame frame;
  QString provider_error;
  if (!frame_provider(0, &frame, &provider_error)) {
    return fail_motion(TimelineAnalysisStatus::Failed,
                       provider_error.isEmpty() ? QStringLiteral("Could not decode the first source frame")
                                                : provider_error);
  }
  if (!timelineMotionFrameIsValid(frame, -1, &provider_error)) {
    return fail_motion(TimelineAnalysisStatus::InvalidInput, provider_error);
  }
  @autoreleasepool {
    CGImageRef first_image = cgimage_from_qimage(frame.image);
    if (first_image == nullptr) {
      return fail_motion(TimelineAnalysisStatus::Failed, QStringLiteral("Could not prepare the first decoded frame"));
    }
    const QRectF vision_seed = timelineTopLeftToBottomLeft(seed_box_top_left);
    VNDetectedObjectObservation* observation = [VNDetectedObjectObservation
        observationWithBoundingBox:CGRectMake(
            static_cast<CGFloat>(vision_seed.x()),
            static_cast<CGFloat>(vision_seed.y()),
            static_cast<CGFloat>(vision_seed.width()),
            static_cast<CGFloat>(vision_seed.height()))];
    CGImageRelease(first_image);
    if (observation == nil) {
      return fail_motion(TimelineAnalysisStatus::Failed, QStringLiteral("Could not initialize the Vision object tracker"));
    }
    result.samples.append(TimelineMotionSample{
        frame.source_time_us, seed_box_top_left, 1.0, true});
    VNTrackObjectRequest* request = [[VNTrackObjectRequest alloc]
        initWithDetectedObjectObservation:observation];
    request.revision = VNTrackObjectRequestRevision1;
    request.trackingLevel = VNRequestTrackingLevelAccurate;
    VNSequenceRequestHandler* sequence_handler = [[VNSequenceRequestHandler alloc] init];
    if (progress) {
      progress(1, frame_count);
    }

    qint64 previous_source_time_us = frame.source_time_us;
    frame = {};
    for (int index = 1; index < frame_count; ++index) {
      if (cancellation_requested(cancelled)) {
        result.status = TimelineAnalysisStatus::Cancelled;
        result.message = QStringLiteral("Timeline motion analysis was cancelled");
        return result;
      }
      provider_error.clear();
      if (!frame_provider(index, &frame, &provider_error)) {
        result.status = TimelineAnalysisStatus::Failed;
        result.message = provider_error.isEmpty() ? QStringLiteral("Could not decode a source frame")
                                                  : provider_error;
        return result;
      }
      if (!timelineMotionFrameIsValid(frame, previous_source_time_us, &provider_error)) {
        result.status = TimelineAnalysisStatus::InvalidInput;
        result.message = provider_error;
        return result;
      }
      CGImageRef cg_image = cgimage_from_qimage(frame.image);
      if (cg_image == nullptr) {
        result.status = TimelineAnalysisStatus::Failed;
        result.message = QStringLiteral("Could not prepare a decoded frame for Vision tracking");
        return result;
      }

      request.inputObservation = observation;
      NSError* error = nil;
      const BOOL performed = [sequence_handler performRequests:@[ request ]
                                                       onCGImage:cg_image
                                                          error:&error];
      CGImageRelease(cg_image);
      if (!performed || error != nil) {
        result.status = TimelineAnalysisStatus::Failed;
        result.message = vision_error(error, @"Vision tracking request failed");
        return result;
      }
      VNDetectedObjectObservation* tracked = request.results.firstObject;
      if (tracked == nil || tracked.confidence <= 0.0f) {
        result.status = TimelineAnalysisStatus::NoTrack;
        result.message = QStringLiteral("Vision lost the selected object at source time %1 us")
                             .arg(frame.source_time_us);
        return result;
      }
      const CGRect vision_box = tracked.boundingBox;
      const QRectF top_left_box = timelineBottomLeftToTopLeft(
          QRectF(vision_box.origin.x, vision_box.origin.y, vision_box.size.width, vision_box.size.height));
      if (!timelineNormalizedBoxIsValid(top_left_box)) {
        result.status = TimelineAnalysisStatus::NoTrack;
        result.message = QStringLiteral("Vision returned an invalid object box at source time %1 us")
                             .arg(frame.source_time_us);
        return result;
      }
      result.samples.append(TimelineMotionSample{
          frame.source_time_us,
          top_left_box,
          static_cast<double>(tracked.confidence),
          true});
      observation = tracked;
      previous_source_time_us = frame.source_time_us;
      frame = {};
      if (progress) {
        progress(index + 1, frame_count);
      }
    }
  }
  result.status = TimelineAnalysisStatus::Success;
  return result;
}

TimelineForegroundResult analyzeTimelineForeground(
    const QVector<TimelineMotionFrame>& frames,
    const std::atomic_bool* cancelled,
    const TimelineAnalysisProgress& progress,
    qsizetype maximum_result_bytes) {
  QString validation_error;
  if (!timelineMotionInputIsValid(frames, &validation_error)) {
    return fail_foreground(TimelineAnalysisStatus::InvalidInput, validation_error);
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
  if (frame_count < 2 || frame_count > kTimelineMotionMaximumFrames || !frame_provider ||
      maximum_result_bytes <= 0) {
    return fail_foreground(
        TimelineAnalysisStatus::InvalidInput,
        QStringLiteral("Frame count, decoded frame provider, or output limit is invalid"));
  }
  if (cancellation_requested(cancelled)) {
    return fail_foreground(TimelineAnalysisStatus::Cancelled,
                           QStringLiteral("Timeline foreground analysis was cancelled"));
  }
  if (@available(macOS 14.0, *)) {
    TimelineForegroundResult result;
    qsizetype result_bytes = 0;
    result.masks.reserve(frame_count);
    qint64 previous_source_time_us = -1;
    for (int index = 0; index < frame_count; ++index) {
      if (cancellation_requested(cancelled)) {
        result.status = TimelineAnalysisStatus::Cancelled;
        result.message = QStringLiteral("Timeline foreground analysis was cancelled");
        return result;
      }
      TimelineMotionFrame frame;
      QString provider_error;
      if (!frame_provider(index, &frame, &provider_error)) {
        result.status = TimelineAnalysisStatus::Failed;
        result.message = provider_error.isEmpty() ? QStringLiteral("Could not decode a source frame")
                                                  : provider_error;
        return result;
      }
      if (!timelineMotionFrameIsValid(frame, previous_source_time_us, &provider_error)) {
        result.status = TimelineAnalysisStatus::InvalidInput;
        result.message = provider_error;
        return result;
      }
      previous_source_time_us = frame.source_time_us;
      @autoreleasepool {
        CGImageRef cg_image = cgimage_from_qimage(frame.image);
        if (cg_image == nullptr) {
          result.status = TimelineAnalysisStatus::Failed;
          result.message = QStringLiteral("Could not prepare a decoded frame for Vision foreground analysis");
          return result;
        }
        VNGenerateForegroundInstanceMaskRequest* request = [[VNGenerateForegroundInstanceMaskRequest alloc] init];
        VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCGImage:cg_image options:@{}];
        NSError* error = nil;
        const BOOL performed = [handler performRequests:@[ request ] error:&error];
        if (!performed || error != nil) {
          CGImageRelease(cg_image);
          result.status = TimelineAnalysisStatus::Failed;
          result.message = vision_error(error, @"Vision foreground request failed");
          return result;
        }
        VNInstanceMaskObservation* observation = request.results.firstObject;
        if (observation == nil || observation.allInstances.count == 0) {
          CGImageRelease(cg_image);
          result.status = TimelineAnalysisStatus::Failed;
          result.message = QStringLiteral("Vision found no foreground instances at source time %1 us")
                               .arg(frame.source_time_us);
          return result;
        }
        CVPixelBufferRef pixel_buffer = [observation
            generateScaledMaskForImageForInstances:observation.allInstances
                                 fromRequestHandler:handler
                                              error:&error];
        CGImageRelease(cg_image);
        if (pixel_buffer == nullptr || error != nil) {
          if (pixel_buffer != nullptr) {
            CVPixelBufferRelease(pixel_buffer);
          }
          result.status = TimelineAnalysisStatus::Failed;
          result.message = vision_error(error, @"Vision could not generate the foreground mask");
          return result;
        }
        const int source_width = static_cast<int>(CVPixelBufferGetWidth(pixel_buffer));
        const int source_height = static_cast<int>(CVPixelBufferGetHeight(pixel_buffer));
        QByteArray source_mask = mask_data_from_pixel_buffer(pixel_buffer);
        CVPixelBufferRelease(pixel_buffer);
        if (source_mask.isEmpty() || source_width <= 0 || source_height <= 0) {
          result.status = TimelineAnalysisStatus::Failed;
          result.message = QStringLiteral("Vision returned an empty foreground mask");
          return result;
        }

        const double scale = std::min(
            1.0,
            static_cast<double>(kTimelineForegroundMaximumDimension) /
                static_cast<double>(std::max(source_width, source_height)));
        const int width = std::max(1, static_cast<int>(std::lround(source_width * scale)));
        const int height = std::max(1, static_cast<int>(std::lround(source_height * scale)));
        const qsizetype output_bytes = static_cast<qsizetype>(width) * height;
        if (output_bytes > maximum_result_bytes - result_bytes) {
          result.status = TimelineAnalysisStatus::MemoryLimitExceeded;
          result.message = QStringLiteral("Foreground masks exceed the configured output memory limit");
          return result;
        }
        TimelineForegroundMask mask;
        mask.source_time_us = frame.source_time_us;
        mask.width = width;
        mask.height = height;
        mask.alpha = resize_mask_nearest(source_mask, source_width, source_height, width, height);
        if (mask.alpha.isEmpty()) {
          result.status = TimelineAnalysisStatus::Failed;
          result.message = QStringLiteral("Could not resize the Vision foreground mask");
          return result;
        }
        result_bytes += mask.alpha.size();
        result.masks.append(std::move(mask));
      }
      frame = {};
      if (progress) {
        progress(index + 1, frame_count);
      }
    }
    result.status = TimelineAnalysisStatus::Success;
    return result;
  }
  return fail_foreground(
      TimelineAnalysisStatus::Unavailable,
      QStringLiteral("Vision foreground analysis requires macOS 14 or later"));
}

bool timelineMotionAnalysisAvailable() {
  if (@available(macOS 14.0, *)) {
    return true;
  }
  return false;
}

}  // namespace photo_editor::desktop

#include "vision_segmentation.h"
#include "prompt_mask_engine.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include <QColor>
#include <QDebug>

namespace photo_editor::desktop {

namespace {

struct VisionMaskContext final {
  VNImageRequestHandler* handler = nil;
  VNInstanceMaskObservation* observation = nil;
  CGImageRef cg_image = nullptr;
};

CGImageRef cgimage_from_qimage(const QImage& image) {
  const QImage rgb = image.convertToFormat(QImage::Format_RGBA8888);
  const auto width = static_cast<size_t>(rgb.width());
  const auto height = static_cast<size_t>(rgb.height());
  const auto bytes_per_row = static_cast<size_t>(rgb.bytesPerLine());

  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CFDataRef data = CFDataCreate(
      nullptr,
      reinterpret_cast<const UInt8*>(rgb.constBits()),
      static_cast<CFIndex>(rgb.sizeInBytes()));
  CGDataProviderRef provider = data ? CGDataProviderCreateWithCFData(data) : nullptr;
  if (!provider) {
    if (data) {
      CFRelease(data);
    }
    CGColorSpaceRelease(color_space);
    return nullptr;
  }

  CGImageRef cg_image = CGImageCreate(
      width,
      height,
      8,
      32,
      bytes_per_row,
      color_space,
      kCGBitmapByteOrderDefault | static_cast<CGBitmapInfo>(kCGImageAlphaLast),
      provider,
      nullptr,
      false,
      kCGRenderingIntentDefault);

  CGDataProviderRelease(provider);
  if (data) {
    CFRelease(data);
  }
  CGColorSpaceRelease(color_space);
  return cg_image;
}

bool build_instance_mask_context(
    const QImage& source_image,
    VisionMaskContext* context,
    QString* error_message) {
  if (context == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Invalid Vision mask context");
    }
    return false;
  }

  context->cg_image = cgimage_from_qimage(source_image);
  if (!context->cg_image) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Failed to create CGImage");
    }
    return false;
  }

  VNGenerateForegroundInstanceMaskRequest* request =
      [[VNGenerateForegroundInstanceMaskRequest alloc] init];
  context->handler =
      [[VNImageRequestHandler alloc] initWithCGImage:context->cg_image options:@{}];

  NSError* error = nil;
  const BOOL success = [context->handler performRequests:@[ request ] error:&error];
  if (!success || error) {
    if (error_message != nullptr) {
      *error_message = QString::fromNSString(
          error ? [error localizedDescription] : @"Vision request failed");
    }
    return false;
  }

  NSArray<VNInstanceMaskObservation*>* observations = request.results;
  if (!observations || observations.count == 0) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("No segmentation results");
    }
    return false;
  }

  context->observation = observations.firstObject;
  if (!context->observation || !context->observation.allInstances ||
      context->observation.allInstances.count == 0) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("No foreground instances detected");
    }
    return false;
  }

  return true;
}

void release_instance_mask_context(VisionMaskContext* context) {
  if (context == nullptr) {
    return;
  }
  if (context->cg_image) {
    CGImageRelease(context->cg_image);
    context->cg_image = nullptr;
  }
  context->handler = nil;
  context->observation = nil;
}

QByteArray copy_mask_data_from_pixel_buffer(CVPixelBufferRef pixel_buffer) {
  if (!pixel_buffer) {
    return {};
  }

  const auto mask_width = static_cast<int>(CVPixelBufferGetWidth(pixel_buffer));
  const auto mask_height = static_cast<int>(CVPixelBufferGetHeight(pixel_buffer));
  const auto bytes_per_row = CVPixelBufferGetBytesPerRow(pixel_buffer);
  const auto* base_address =
      static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pixel_buffer));
  const auto pixel_format = CVPixelBufferGetPixelFormatType(pixel_buffer);

  QByteArray mask_data;
  mask_data.resize(mask_width * mask_height);

  if (pixel_format == kCVPixelFormatType_OneComponent8) {
    for (int y = 0; y < mask_height; ++y) {
      const auto* row = base_address + static_cast<size_t>(y) * bytes_per_row;
      for (int x = 0; x < mask_width; ++x) {
        mask_data[y * mask_width + x] = static_cast<char>(row[x]);
      }
    }
  } else if (pixel_format == kCVPixelFormatType_OneComponent32Float) {
    for (int y = 0; y < mask_height; ++y) {
      const auto* row = reinterpret_cast<const float*>(
          base_address + static_cast<size_t>(y) * bytes_per_row);
      for (int x = 0; x < mask_width; ++x) {
        const auto value = std::clamp(row[x], 0.0f, 1.0f);
        mask_data[y * mask_width + x] =
            static_cast<char>(static_cast<uint8_t>(value * 255.0f));
      }
    }
  } else {
    for (int y = 0; y < mask_height; ++y) {
      const auto* row = base_address + static_cast<size_t>(y) * bytes_per_row;
      for (int x = 0; x < mask_width; ++x) {
        mask_data[y * mask_width + x] = static_cast<char>(row[x]);
      }
    }
  }

  return mask_data;
}

SegmentationResult segmentation_result_from_pixel_buffer(CVPixelBufferRef pixel_buffer) {
  SegmentationResult result;
  if (!pixel_buffer) {
    result.error_message = QStringLiteral("Failed to create mask buffer");
    return result;
  }

  CVPixelBufferLockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
  result.width = static_cast<int>(CVPixelBufferGetWidth(pixel_buffer));
  result.height = static_cast<int>(CVPixelBufferGetHeight(pixel_buffer));
  result.mask_data = copy_mask_data_from_pixel_buffer(pixel_buffer);
  CVPixelBufferUnlockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
  CVPixelBufferRelease(pixel_buffer);

  result.success = !result.mask_data.isEmpty() && result.width > 0 && result.height > 0;
  if (!result.success) {
    result.error_message = QStringLiteral("Failed to copy mask buffer");
  }
  return result;
}

QString normalized_semantic_kind(const QString& semantic_kind) {
  const auto normalized = semantic_kind.trimmed().toLower();
  if (normalized == QStringLiteral("person")) {
    return QStringLiteral("people");
  }
  if (normalized == QStringLiteral("mountain")) {
    return QStringLiteral("mountains");
  }
  if (normalized == QStringLiteral("facial hair") ||
      normalized == QStringLiteral("facial_hair")) {
    return QStringLiteral("facial-hair");
  }
  return normalized;
}

bool is_supported_semantic_kind(const QString& kind) {
  return kind == QStringLiteral("subject") ||
         kind == QStringLiteral("sky") ||
         kind == QStringLiteral("background");
}

SegmentationResult unsupported_semantic_result(const QString& kind) {
  SegmentationResult result;
  result.error_message =
      QStringLiteral("No semantic segmentation model is available for %1").arg(kind);
  return result;
}

SegmentationResult invert_segmentation_result(SegmentationResult result) {
  if (!result.success) {
    return result;
  }
  for (auto index = 0; index < result.mask_data.size(); ++index) {
    const auto value = static_cast<unsigned char>(result.mask_data.at(index));
    result.mask_data[index] = static_cast<char>(0xff - value);
  }
  return result;
}

SegmentationResult generate_sky_heuristic_mask(const QImage& source_image) {
  SegmentationResult result;
  if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
    result.error_message = QStringLiteral("Invalid source image");
    return result;
  }

  const auto image = source_image.convertToFormat(QImage::Format_RGB32);
  result.width = image.width();
  result.height = image.height();
  result.mask_data = QByteArray(result.width * result.height, '\0');

  auto has_sky_pixels = false;
  for (int y = 0; y < result.height; ++y) {
    const auto vertical = result.height > 1
                              ? static_cast<double>(y) / static_cast<double>(result.height - 1)
                              : 0.0;
    const auto top_bias = std::clamp((0.70 - vertical) / 0.70, 0.0, 1.0);
    const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < result.width; ++x) {
      const auto pixel = row[x];
      const auto red = static_cast<double>(qRed(pixel)) / 255.0;
      const auto green = static_cast<double>(qGreen(pixel)) / 255.0;
      const auto blue = static_cast<double>(qBlue(pixel)) / 255.0;
      const auto max_channel = std::max({red, green, blue});
      const auto min_channel = std::min({red, green, blue});
      const auto chroma = max_channel - min_channel;
      const auto brightness = (red + green + blue) / 3.0;
      const auto blue_dominance = std::clamp((blue - std::max(red, green) + 0.08) / 0.22, 0.0, 1.0);
      const auto bright_sky = std::clamp((brightness - 0.38) / 0.40, 0.0, 1.0);
      const auto low_texture_haze = std::clamp((0.36 - chroma) / 0.36, 0.0, 1.0);
      const auto score = top_bias * std::max(blue_dominance, 0.55 * bright_sky * low_texture_haze);
      const auto alpha = static_cast<int>(std::lround(std::clamp(score, 0.0, 1.0) * 255.0));
      if (alpha > 0) {
        result.mask_data[y * result.width + x] = static_cast<char>(alpha);
        has_sky_pixels = true;
      }
    }
  }

  result.success = has_sky_pixels;
  if (!result.success) {
    result.error_message = QStringLiteral("No sky-like region detected");
  }
  return result;
}

}  // namespace

SegmentationResult generate_subject_mask(const QImage& source_image) {
  SegmentationResult result;

  if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
    result.error_message = QStringLiteral("Invalid source image");
    return result;
  }

  if (@available(macOS 14.0, *)) {
    @autoreleasepool {
      VisionMaskContext context;
      if (!build_instance_mask_context(source_image, &context, &result.error_message)) {
        release_instance_mask_context(&context);
        return result;
      }

      NSError* mask_error = nil;
      CVPixelBufferRef pixel_buffer =
          [context.observation generateScaledMaskForImageForInstances:context.observation.allInstances
                                                   fromRequestHandler:context.handler
                                                                error:&mask_error];

      if (!pixel_buffer || mask_error) {
        release_instance_mask_context(&context);
        result.error_message = QString::fromNSString(
            mask_error ? [mask_error localizedDescription] : @"Failed to create mask buffer");
        return result;
      }

      result = segmentation_result_from_pixel_buffer(pixel_buffer);
      release_instance_mask_context(&context);
      return result;
    }
  }

  result.error_message = QStringLiteral("Vision segmentation requires macOS 14.0 or later");
  return result;
}

SegmentationResult generate_semantic_mask(const QImage& source_image, const QString& semantic_kind) {
  const auto kind = normalized_semantic_kind(semantic_kind);
  if (!is_supported_semantic_kind(kind)) {
    return unsupported_semantic_result(kind);
  }
  if (kind == QStringLiteral("sky")) {
    return generate_sky_heuristic_mask(source_image);
  }

  auto foreground_result = generate_subject_mask(source_image);
  if (!foreground_result.success) {
    return foreground_result;
  }
  if (kind == QStringLiteral("background")) {
    return invert_segmentation_result(std::move(foreground_result));
  }
  return foreground_result;
}

ForegroundInstanceMap generate_foreground_instance_map(const QImage& source_image) {
  ForegroundInstanceMap result;

  if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
    result.error_message = QStringLiteral("Invalid source image");
    return result;
  }

  if (@available(macOS 14.0, *)) {
    @autoreleasepool {
      VisionMaskContext context;
      if (!build_instance_mask_context(source_image, &context, &result.error_message)) {
        release_instance_mask_context(&context);
        return result;
      }

      CVPixelBufferRef instance_mask = context.observation.instanceMask;
      if (!instance_mask) {
        release_instance_mask_context(&context);
        result.error_message = QStringLiteral("Failed to create instance map");
        return result;
      }

      CVPixelBufferRetain(instance_mask);
      result.width = static_cast<int>(CVPixelBufferGetWidth(instance_mask));
      result.height = static_cast<int>(CVPixelBufferGetHeight(instance_mask));
      CVPixelBufferLockBaseAddress(instance_mask, kCVPixelBufferLock_ReadOnly);
      result.instance_data = copy_mask_data_from_pixel_buffer(instance_mask);
      CVPixelBufferUnlockBaseAddress(instance_mask, kCVPixelBufferLock_ReadOnly);
      CVPixelBufferRelease(instance_mask);

      result.success =
          !result.instance_data.isEmpty() && result.width > 0 && result.height > 0;
      if (!result.success) {
        result.error_message = QStringLiteral("Failed to copy instance map");
      }
      release_instance_mask_context(&context);
      return result;
    }
  }

  result.error_message = QStringLiteral("Vision object masking requires macOS 14.0 or later");
  return result;
}

SegmentationResult generate_object_mask_for_instance(
    const QImage& source_image,
    int instance_id) {
  return generate_object_mask_for_instances(source_image, QVector<int>{instance_id});
}

SegmentationResult generate_object_mask_for_instances(
    const QImage& source_image,
    const QVector<int>& instance_ids) {
  SegmentationResult result;

  if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
    result.error_message = QStringLiteral("Invalid source image");
    return result;
  }

  QVector<int> normalized_ids;
  normalized_ids.reserve(instance_ids.size());
  for (const auto instance_id : instance_ids) {
    if (instance_id > 0 && !normalized_ids.contains(instance_id)) {
      normalized_ids.append(instance_id);
    }
  }

  if (normalized_ids.isEmpty()) {
    result.error_message = QStringLiteral("No selectable object at that point");
    return result;
  }

  if (@available(macOS 14.0, *)) {
    @autoreleasepool {
      VisionMaskContext context;
      if (!build_instance_mask_context(source_image, &context, &result.error_message)) {
        release_instance_mask_context(&context);
        return result;
      }

      NSMutableIndexSet* selected_instances = [[NSMutableIndexSet alloc] init];
      for (const auto instance_id : normalized_ids) {
        if ([context.observation.allInstances containsIndex:static_cast<NSUInteger>(instance_id)]) {
          [selected_instances addIndex:static_cast<NSUInteger>(instance_id)];
        }
      }

      if (selected_instances.count == 0) {
        release_instance_mask_context(&context);
        result.error_message = QStringLiteral("No selectable object at that point");
        return result;
      }

      NSError* mask_error = nil;
      CVPixelBufferRef pixel_buffer =
          [context.observation
              generateScaledMaskForImageForInstances:selected_instances
                                   fromRequestHandler:context.handler
                                                error:&mask_error];
      if (!pixel_buffer || mask_error) {
        release_instance_mask_context(&context);
        result.error_message = QString::fromNSString(
            mask_error ? [mask_error localizedDescription] : @"Failed to create object mask");
        return result;
      }

      result = segmentation_result_from_pixel_buffer(pixel_buffer);
      release_instance_mask_context(&context);
      return result;
    }
  }

  result.error_message = QStringLiteral("Vision object masking requires macOS 14.0 or later");
  return result;
}

SegmentationResult generate_object_mask(
    const QImage& source_image,
    double normalized_x,
    double normalized_y) {
  QVector<SmartSelectPrompt> prompts;
  prompts.append(SmartSelectPrompt{
      .x = normalized_x,
      .y = normalized_y,
      .negative = false,
  });
  return generate_smart_select_mask(source_image, prompts);
}

SegmentationResult generate_smart_select_mask(
    const QImage& source_image,
    const QVector<SmartSelectPrompt>& prompts) {
  QVector<prompt_mask_engine::PromptPoint> normalized_prompts;
  normalized_prompts.reserve(prompts.size());
  for (const auto& prompt : prompts) {
    normalized_prompts.push_back(prompt_mask_engine::PromptPoint{
        .x = prompt.x,
        .y = prompt.y,
        .negative = prompt.negative,
    });
  }
  const auto result = prompt_mask_engine::generate_mask(source_image, normalized_prompts);
  return SegmentationResult{
      .mask_data = result.mask_data,
      .width = result.width,
      .height = result.height,
      .success = result.success,
      .error_message = result.error_message,
  };
}

bool vision_segmentation_available() {
  if (@available(macOS 14.0, *)) {
    return true;
  }
  return false;
}

}  // namespace photo_editor::desktop

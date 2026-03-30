#include "vision_segmentation.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

#include <algorithm>

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
  SegmentationResult result;

  if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
    result.error_message = QStringLiteral("Invalid source image");
    return result;
  }

  if (instance_id <= 0) {
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

      if (![context.observation.allInstances containsIndex:static_cast<NSUInteger>(instance_id)]) {
        release_instance_mask_context(&context);
        result.error_message = QStringLiteral("No selectable object at that point");
        return result;
      }

      NSError* mask_error = nil;
      CVPixelBufferRef pixel_buffer =
          [context.observation
              generateScaledMaskForImageForInstances:[NSIndexSet indexSetWithIndex:static_cast<NSUInteger>(instance_id)]
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
  const auto instance_map = generate_foreground_instance_map(source_image);
  if (!instance_map.success) {
    SegmentationResult result;
    result.error_message = instance_map.error_message;
    return result;
  }

  const auto sample_instance = [&](double sample_y) {
    const auto x = std::clamp(
        static_cast<int>(std::lround(std::clamp(normalized_x, 0.0, 1.0) *
                                     std::max(instance_map.width - 1, 0))),
        0,
        std::max(instance_map.width - 1, 0));
    const auto y = std::clamp(
        static_cast<int>(std::lround(std::clamp(sample_y, 0.0, 1.0) *
                                     std::max(instance_map.height - 1, 0))),
        0,
        std::max(instance_map.height - 1, 0));
    return static_cast<int>(
        static_cast<unsigned char>(instance_map.instance_data.at(y * instance_map.width + x)));
  };

  const auto direct_instance = sample_instance(normalized_y);
  const auto flipped_instance = sample_instance(1.0 - normalized_y);
  auto selected_instance = direct_instance;
  if (selected_instance == 0 && flipped_instance > 0) {
    selected_instance = flipped_instance;
  } else if (selected_instance > 0 && flipped_instance > 0 &&
             selected_instance != flipped_instance) {
    selected_instance = flipped_instance;
  }

  return generate_object_mask_for_instance(source_image, selected_instance);
}

bool vision_segmentation_available() {
  if (@available(macOS 14.0, *)) {
    return true;
  }
  return false;
}

}  // namespace photo_editor::desktop

#include "raw_image_decoder.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>

#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {

namespace {

const QStringList& raw_extensions() {
  static const QStringList kExtensions = {
      QStringLiteral("3fr"),
      QStringLiteral("arw"),
      QStringLiteral("cr2"),
      QStringLiteral("cr3"),
      QStringLiteral("dng"),
      QStringLiteral("erf"),
      QStringLiteral("iiq"),
      QStringLiteral("kdc"),
      QStringLiteral("mef"),
      QStringLiteral("mos"),
      QStringLiteral("nef"),
      QStringLiteral("nrw"),
      QStringLiteral("orf"),
      QStringLiteral("pef"),
      QStringLiteral("raf"),
      QStringLiteral("raw"),
      QStringLiteral("rw2"),
      QStringLiteral("sr2"),
      QStringLiteral("srf"),
      QStringLiteral("x3f"),
  };
  return kExtensions;
}

const QStringList& native_extensions() {
  static const QStringList kExtensions = {
      QStringLiteral("tif"),
      QStringLiteral("tiff"),
  };
  return kExtensions;
}

bool has_raw_extension(const QString& file_path) {
  return raw_extensions().contains(QFileInfo(file_path).suffix().toLower());
}

bool has_native_extension(const QString& file_path) {
  return native_extensions().contains(QFileInfo(file_path).suffix().toLower());
}

NSURL* file_url_for_path(const QString& file_path) {
  const auto utf8 = file_path.toUtf8();
  NSString* path = [NSString stringWithUTF8String:utf8.constData()];
  return path == nil ? nil : [NSURL fileURLWithPath:path];
}

CIContext* shared_ci_context() {
  static CIContext* context = [CIContext contextWithOptions:@{
    kCIContextUseSoftwareRenderer : @NO,
  }];
  return context;
}

QImage qimage_from_cgimage(CGImageRef cg_image) {
  if (cg_image == nullptr) {
    return {};
  }

  const auto width = static_cast<int>(CGImageGetWidth(cg_image));
  const auto height = static_cast<int>(CGImageGetHeight(cg_image));
  if (width <= 0 || height <= 0) {
    return {};
  }

  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    return {};
  }

  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      image.bits(),
      static_cast<size_t>(width),
      static_cast<size_t>(height),
      8,
      static_cast<size_t>(image.bytesPerLine()),
      color_space,
      static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host));

  if (context == nullptr) {
    CGColorSpaceRelease(color_space);
    return {};
  }

  CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(height));
  CGContextScaleCTM(context, 1.0, -1.0);
  CGContextDrawImage(
      context,
      CGRectMake(0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)),
      cg_image);

  CGContextRelease(context);
  CGColorSpaceRelease(color_space);
  return image.convertToFormat(QImage::Format_ARGB32);
}

CGImageSourceRef image_source_for_path(const QString& file_path) {
  NSURL* url = file_url_for_path(file_path);
  if (url == nil) {
    return nullptr;
  }

  return CGImageSourceCreateWithURL(
      (__bridge CFURLRef)url,
      (__bridge CFDictionaryRef)@{
          (__bridge NSString*)kCGImageSourceShouldCache : @NO,
      });
}

QSize image_source_size(CGImageSourceRef source) {
  if (source == nullptr) {
    return {};
  }

  CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
  if (properties == nullptr) {
    return {};
  }

  NSDictionary* dictionary = CFBridgingRelease(properties);
  NSNumber* width = dictionary[(__bridge NSString*)kCGImagePropertyPixelWidth];
  NSNumber* height = dictionary[(__bridge NSString*)kCGImagePropertyPixelHeight];
  if (width == nil || height == nil) {
    return {};
  }

  const auto pixel_width = static_cast<int>(std::lround(width.doubleValue));
  const auto pixel_height = static_cast<int>(std::lround(height.doubleValue));
  if (pixel_width <= 0 || pixel_height <= 0) {
    return {};
  }
  return QSize(pixel_width, pixel_height);
}

CGSize raw_native_size(const QString& file_path) {
  CGImageSourceRef source = image_source_for_path(file_path);
  if (source == nullptr) {
    return CGSizeZero;
  }

  const QSize size = image_source_size(source);
  CFRelease(source);
  return size.isValid() ? CGSizeMake(size.width(), size.height()) : CGSizeZero;
}

CGImageRef decode_native_cgimage(CGImageSourceRef source, int max_edge) {
  if (source == nullptr) {
    return nullptr;
  }

  auto target_edge = max_edge;
  if (target_edge <= 0) {
    const QSize source_size = image_source_size(source);
    target_edge = std::max(source_size.width(), source_size.height());
  }

  if (target_edge > 0) {
    NSDictionary* options = @{
      (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
      (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform : @YES,
      (__bridge NSString*)kCGImageSourceShouldCacheImmediately : @YES,
      (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize : @(target_edge),
    };
    CGImageRef thumbnail =
        CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
    if (thumbnail != nullptr) {
      return thumbnail;
    }
  }

  return CGImageSourceCreateImageAtIndex(
      source,
      0,
      (__bridge CFDictionaryRef)@{
          (__bridge NSString*)kCGImageSourceShouldCacheImmediately : @YES,
      });
}

bool configure_raw_filter(
    CIRAWFilter* filter,
    int max_edge,
    const RawDecodeSettings& settings) {
  if (filter == nil) {
    return false;
  }

  const CGSize native_size = filter.nativeSize;
  const auto long_edge = std::max(native_size.width, native_size.height);
  auto scale_factor = 1.0f;
  if (max_edge > 0 && long_edge > 0.0) {
    scale_factor = static_cast<float>(std::clamp(
        static_cast<double>(max_edge) / static_cast<double>(long_edge),
        0.01,
        1.0));
  }

  filter.scaleFactor = scale_factor;
  filter.draftModeEnabled = max_edge > 0 && scale_factor < 1.0f;
  filter.exposure = static_cast<float>(settings.exposure);
  filter.boostAmount = static_cast<float>(settings.boost_amount);
  filter.boostShadowAmount = static_cast<float>(settings.boost_shadow_amount);
  filter.extendedDynamicRangeAmount = 0.0f;
  filter.gamutMappingEnabled = settings.gamut_mapping_enabled ? YES : NO;
  if (filter.isLensCorrectionSupported) {
    filter.lensCorrectionEnabled = settings.lens_correction_enabled ? YES : NO;
  }
  if (filter.isLocalToneMapSupported) {
    filter.localToneMapAmount = 0.0f;
  }
  if (filter.isDetailSupported) {
    filter.detailAmount = static_cast<float>(settings.detail_amount);
  }
  if (@available(macOS 16.0, *)) {
    if (filter.isHighlightRecoverySupported) {
      filter.highlightRecoveryEnabled = settings.highlight_recovery_enabled ? YES : NO;
    }
  }
  if (settings.white_balance_override_enabled) {
    filter.neutralTemperature = static_cast<float>(settings.white_balance_temperature);
    filter.neutralTint = static_cast<float>(settings.white_balance_tint);
  }
  return true;
}

}  // namespace

bool raw_image_supported(const QString& file_path) { return has_raw_extension(file_path); }

bool native_image_supported(const QString& file_path) { return has_native_extension(file_path); }

QSize raw_image_size(const QString& file_path) {
  if (!has_raw_extension(file_path)) {
    return {};
  }

  @autoreleasepool {
    const auto size = raw_native_size(file_path);
    if (size.width <= 0.0 || size.height <= 0.0) {
      return {};
    }
    return QSize(
        static_cast<int>(std::lround(size.width)),
        static_cast<int>(std::lround(size.height)));
  }
}

QSize native_image_size(const QString& file_path) {
  if (!has_native_extension(file_path)) {
    return {};
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  @autoreleasepool {
    CGImageSourceRef source = image_source_for_path(file_path);
    if (source == nullptr) {
      return {};
    }

    const QSize size = image_source_size(source);
    CFRelease(source);
    return size;
  }
}

bool decode_raw_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    const RawDecodeSettings& settings) {
  if (image == nullptr || !has_raw_extension(file_path)) {
    return false;
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }

  @autoreleasepool {
    NSURL* url = file_url_for_path(file_path);
    if (url == nil) {
      return false;
    }

    CIRAWFilter* filter = [CIRAWFilter filterWithImageURL:url];
    if (filter == nil || !configure_raw_filter(filter, max_edge, settings)) {
      return false;
    }

    CIImage* output_image = filter.outputImage;
    if (output_image == nil) {
      return false;
    }

    const CGRect extent = CGRectIntegral(output_image.extent);
    if (CGRectIsEmpty(extent)) {
      return false;
    }

    CGImageRef cg_image = [shared_ci_context() createCGImage:output_image fromRect:extent];
    if (cg_image == nullptr) {
      return false;
    }

    *image = qimage_from_cgimage(cg_image);
    CGImageRelease(cg_image);
    return !image->isNull();
  }
}

bool decode_native_image(
    const QString& file_path,
    QImage* image,
    int max_edge) {
  if (image == nullptr || !has_native_extension(file_path)) {
    return false;
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }

  @autoreleasepool {
    CGImageSourceRef source = image_source_for_path(file_path);
    if (source == nullptr) {
      return false;
    }

    CGImageRef cg_image = decode_native_cgimage(source, max_edge);
    CFRelease(source);
    if (cg_image == nullptr) {
      return false;
    }

    *image = qimage_from_cgimage(cg_image);
    CGImageRelease(cg_image);
    return !image->isNull();
  }
}

}  // namespace photo_editor::desktop

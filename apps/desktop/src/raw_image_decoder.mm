#include "raw_image_decoder.h"
#include "working_image.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>

#include <QColorSpace>
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

[[maybe_unused]] QImage qimage_from_cgimage(CGImageRef cg_image) {
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

  CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  if (color_space == nullptr) {
    color_space = CGColorSpaceCreateDeviceRGB();
  }
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
  auto output = image.convertToFormat(QImage::Format_ARGB32);
  output.setColorSpace(preview_color_space());
  return output;
}

bool render_ciimage_to_linear_qimage(CIImage* input_image, QImage* image) {
  if (input_image == nil || image == nullptr) {
    return false;
  }

  const CGRect extent = CGRectIntegral(input_image.extent);
  if (CGRectIsEmpty(extent)) {
    return false;
  }

  const auto width = static_cast<int>(CGRectGetWidth(extent));
  const auto height = static_cast<int>(CGRectGetHeight(extent));
  if (width <= 0 || height <= 0) {
    return false;
  }

  QImage linear_image(width, height, QImage::Format_RGBA16FPx4);
  if (linear_image.isNull()) {
    return false;
  }
  linear_image.setColorSpace(linear_srgb_color_space());

  CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
  if (color_space == nullptr) {
    return false;
  }

  [shared_ci_context() render:input_image
                     toBitmap:linear_image.bits()
                     rowBytes:static_cast<ptrdiff_t>(linear_image.bytesPerLine())
                       bounds:extent
                       format:kCIFormatRGBAh
                   colorSpace:color_space];
  CGColorSpaceRelease(color_space);

  *image = std::move(linear_image);
  return !image->isNull();
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

QString string_property_from_dictionary(NSDictionary* dictionary, NSString* key) {
  if (dictionary == nil || key == nil) {
    return {};
  }
  id value = dictionary[key];
  if (![value isKindOfClass:[NSString class]]) {
    return {};
  }
  const auto* string_value = static_cast<NSString*>(value);
  return QString::fromUtf8(string_value.UTF8String).trimmed();
}

void populate_decoded_metadata_from_source(
    CGImageSourceRef source,
    photo_editor::pipeline::DecodedImageMetadata* metadata) {
  if (source == nullptr || metadata == nullptr) {
    return;
  }

  CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
  if (properties == nullptr) {
    return;
  }

  NSDictionary* dictionary = CFBridgingRelease(properties);
  NSDictionary* tiff = dictionary[(__bridge NSString*)kCGImagePropertyTIFFDictionary];
  metadata->camera_make =
      string_property_from_dictionary(tiff, (__bridge NSString*)kCGImagePropertyTIFFMake).toStdString();
  metadata->camera_model =
      string_property_from_dictionary(tiff, (__bridge NSString*)kCGImagePropertyTIFFModel).toStdString();
}

void populate_decoded_metadata_from_filter(
    CIRAWFilter* filter,
    const RawDecodeSettings& settings,
    photo_editor::pipeline::DecodedImageMetadata* metadata) {
  if (filter == nil || metadata == nullptr) {
    return;
  }

  metadata->valid = true;
  metadata->is_raw = true;
  metadata->input_profile_name =
      photo_editor::pipeline::normalize_input_color_profile_name(settings.input_color_profile.toStdString());
  metadata->as_shot_temperature =
      std::clamp(static_cast<double>(filter.neutralTemperature), 2000.0, 12000.0);
  metadata->as_shot_tint = std::clamp(static_cast<double>(filter.neutralTint), -150.0, 150.0);
  metadata->baseline_exposure = 0.0;
  metadata->highlight_headroom = 1.0;
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
  const auto input_transform_enabled = settings.input_transform_enabled;
  const auto highlight_mode =
      settings.highlight_reconstruction_mode.trimmed().compare(QStringLiteral("Disabled"), Qt::CaseInsensitive) == 0
          ? 0
          : (settings.highlight_reconstruction_mode.trimmed().compare(
                     QStringLiteral("Camera Default"), Qt::CaseInsensitive) == 0
                 ? 1
                 : 2);
  filter.exposure = static_cast<float>(input_transform_enabled ? settings.exposure : 0.0);
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
    const auto supported_selector = NSSelectorFromString(@"isHighlightRecoverySupported");
    const auto set_enabled_selector = NSSelectorFromString(@"setHighlightRecoveryEnabled:");
    if ([filter respondsToSelector:supported_selector] &&
        [filter respondsToSelector:set_enabled_selector]) {
      auto* supported_method = reinterpret_cast<BOOL (*)(id, SEL)>(
          [filter methodForSelector:supported_selector]);
      auto* set_enabled_method = reinterpret_cast<void (*)(id, SEL, BOOL)>(
          [filter methodForSelector:set_enabled_selector]);
      if (supported_method(filter, supported_selector)) {
        set_enabled_method(
            filter,
            set_enabled_selector,
            (highlight_mode == 2 || (highlight_mode == 1 && settings.highlight_recovery_enabled)) ? YES : NO);
      }
    }
  }
  if (input_transform_enabled && settings.white_balance_override_enabled) {
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
    const RawDecodeSettings& settings,
    photo_editor::pipeline::DecodedImageMetadata* metadata) {
  QImage linear_image;
  if (!decode_raw_linear_image(file_path, &linear_image, max_edge, settings, metadata)) {
    return false;
  }

  *image = finalize_output_image(std::move(linear_image), preview_color_space());
  return !image->isNull();
}

bool decode_raw_linear_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    const RawDecodeSettings& settings,
    photo_editor::pipeline::DecodedImageMetadata* metadata) {
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

    CGImageSourceRef source = image_source_for_path(file_path);
    CIRAWFilter* filter = [CIRAWFilter filterWithImageURL:url];
    if (filter == nil || !configure_raw_filter(filter, max_edge, settings)) {
      if (source != nullptr) {
        CFRelease(source);
      }
      return false;
    }

    if (metadata != nullptr) {
      *metadata = {};
      populate_decoded_metadata_from_source(source, metadata);
      populate_decoded_metadata_from_filter(filter, settings, metadata);
    }
    if (source != nullptr) {
      CFRelease(source);
    }

    CIImage* output_image = filter.outputImage;
    if (output_image == nil) {
      return false;
    }

    return render_ciimage_to_linear_qimage(output_image, image);
  }
}

bool read_raw_decoded_metadata(
    const QString& file_path,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    const RawDecodeSettings& settings) {
  if (metadata == nullptr || !has_raw_extension(file_path)) {
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

    CGImageSourceRef source = image_source_for_path(file_path);
    CIRAWFilter* filter = [CIRAWFilter filterWithImageURL:url];
    if (filter == nil) {
      if (source != nullptr) {
        CFRelease(source);
      }
      return false;
    }

    *metadata = {};
    populate_decoded_metadata_from_source(source, metadata);
    populate_decoded_metadata_from_filter(filter, settings, metadata);
    if (source != nullptr) {
      CFRelease(source);
    }
    return metadata->valid;
  }
}

bool decode_native_image(
    const QString& file_path,
    QImage* image,
    int max_edge) {
  QImage linear_image;
  if (!decode_native_linear_image(file_path, &linear_image, max_edge)) {
    return false;
  }

  *image = finalize_output_image(std::move(linear_image), preview_color_space());
  return !image->isNull();
}

bool decode_native_linear_image(
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

    CIImage* ci_image = [[CIImage alloc] initWithCGImage:cg_image];
    CGImageRelease(cg_image);
    if (ci_image == nil) {
      return false;
    }

    return render_ciimage_to_linear_qimage(ci_image, image);
  }
}

}  // namespace photo_editor::desktop

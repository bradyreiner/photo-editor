#include "raw_image_decoder.h"
#include "working_image.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>

#include <QColor>
#include <QColorSpace>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <array>
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
  const QSize source_size = image_source_size(source);
  const auto source_long_edge = std::max(source_size.width(), source_size.height());
  if (target_edge <= 0) {
    target_edge = std::max(source_size.width(), source_size.height());
  }

  if (target_edge > 0 && source_long_edge > target_edge) {
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
  filter.draftModeEnabled =
      raw_decode_uses_draft_demosaic(settings, max_edge > 0 && scale_factor < 1.0f);
  const auto input_transform_enabled = settings.input_transform_enabled;
  const auto highlight_mode =
      settings.highlight_reconstruction_mode.trimmed().compare(QStringLiteral("Disabled"), Qt::CaseInsensitive) == 0
          ? 0
          : (settings.highlight_reconstruction_mode.trimmed().compare(
                     QStringLiteral("Camera Default"), Qt::CaseInsensitive) == 0
                 ? 1
                 : 2);
  const auto decode_boost = std::clamp(settings.boost_amount, 0.0, 2.0);
  const auto shadow_boost = std::clamp(settings.boost_shadow_amount, 0.0, 2.0);
  const auto edr_amount =
      input_transform_enabled ? std::clamp(0.45 + decode_boost * 0.25, 0.0, 1.0) : 0.0;
  const auto local_tone_amount = std::clamp((shadow_boost - 1.0) * 0.65, 0.0, 1.0);

  filter.exposure = static_cast<float>(input_transform_enabled ? settings.exposure : 0.0);
  filter.boostAmount = static_cast<float>(decode_boost);
  filter.boostShadowAmount = static_cast<float>(shadow_boost);
  filter.extendedDynamicRangeAmount = static_cast<float>(edr_amount);
  filter.gamutMappingEnabled = settings.gamut_mapping_enabled ? YES : NO;
  if (filter.isLensCorrectionSupported) {
    filter.lensCorrectionEnabled = settings.lens_correction_enabled ? YES : NO;
  }
  if (filter.isLocalToneMapSupported) {
    filter.localToneMapAmount = static_cast<float>(local_tone_amount);
  }
  if (filter.isDetailSupported) {
    filter.detailAmount = static_cast<float>(settings.detail_amount);
  }
  const auto chroma_noise_reduction =
      static_cast<float>(raw_decode_chroma_noise_reduction_amount(settings));
  const auto noise_reduction_selector = NSSelectorFromString(@"setNoiseReductionAmount:");
  if ([filter respondsToSelector:noise_reduction_selector]) {
    auto* set_noise_reduction_method = reinterpret_cast<void (*)(id, SEL, float)>(
        [filter methodForSelector:noise_reduction_selector]);
    set_noise_reduction_method(filter, noise_reduction_selector, chroma_noise_reduction);
  }
  const auto moire_reduction = static_cast<float>(raw_decode_moire_reduction_amount(settings));
  const auto moire_reduction_selector = NSSelectorFromString(@"setMoireReductionAmount:");
  if ([filter respondsToSelector:moire_reduction_selector]) {
    auto* set_moire_reduction_method = reinterpret_cast<void (*)(id, SEL, float)>(
        [filter methodForSelector:moire_reduction_selector]);
    set_moire_reduction_method(filter, moire_reduction_selector, moire_reduction);
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

bool raw_decode_uses_draft_demosaic(const RawDecodeSettings& settings, bool scaled_preview) {
  const auto quality = settings.demosaic_quality.trimmed();
  if (quality.compare(QStringLiteral("Preview"), Qt::CaseInsensitive) == 0 ||
      quality.compare(QStringLiteral("Draft"), Qt::CaseInsensitive) == 0) {
    return true;
  }
  if (quality.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0 ||
      quality.compare(QStringLiteral("Best"), Qt::CaseInsensitive) == 0) {
    return false;
  }
  return scaled_preview;
}

double raw_decode_chroma_noise_reduction_amount(const RawDecodeSettings& settings) {
  return std::clamp(settings.chroma_noise_reduction_amount, 0.0, 1.0);
}

double raw_decode_moire_reduction_amount(const RawDecodeSettings& settings) {
  return std::clamp(settings.moire_reduction_amount, 0.0, 1.0);
}

double raw_decode_hot_dead_pixel_cleanup_amount(const RawDecodeSettings& settings) {
  return std::clamp(settings.hot_dead_pixel_cleanup_amount, 0.0, 1.0);
}

double raw_decode_false_color_suppression_amount(const RawDecodeSettings& settings) {
  return std::clamp(settings.false_color_suppression_amount, 0.0, 1.0);
}

bool apply_raw_hot_dead_pixel_cleanup(QImage* image, double amount) {
  amount = std::clamp(amount, 0.0, 1.0);
  if (image == nullptr || image->isNull() || amount <= 0.0 || image->width() < 3 || image->height() < 3) {
    return false;
  }

  const QImage source = image->copy();
  QImage output = image->copy();
  const auto channel_median = [](std::array<double, 8> values) {
    std::sort(values.begin(), values.end());
    return (values[3] + values[4]) * 0.5;
  };
  const auto luma = [](const QColor& color) {
    return color.redF() * 0.2126 + color.greenF() * 0.7152 + color.blueF() * 0.0722;
  };

  auto changed = false;
  const auto threshold_scale = 0.45 - amount * 0.37;
  for (int y = 1; y < source.height() - 1; ++y) {
    for (int x = 1; x < source.width() - 1; ++x) {
      std::array<double, 8> red{};
      std::array<double, 8> green{};
      std::array<double, 8> blue{};
      std::array<double, 8> neighbor_luma{};
      std::size_t index = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          const auto neighbor = source.pixelColor(x + dx, y + dy);
          red[index] = neighbor.redF();
          green[index] = neighbor.greenF();
          blue[index] = neighbor.blueF();
          neighbor_luma[index] = luma(neighbor);
          ++index;
        }
      }

      const auto center = source.pixelColor(x, y);
      const auto median_luma = channel_median(neighbor_luma);
      const auto center_luma = luma(center);
      const auto scaled_threshold = threshold_scale * std::max(1.0, median_luma);
      if (std::abs(center_luma - median_luma) <= scaled_threshold) {
        continue;
      }

      QColor repaired = center;
      repaired.setRedF(std::clamp(
          static_cast<float>(center.redF() + (channel_median(red) - center.redF()) * amount),
          0.0F,
          1.0F));
      repaired.setGreenF(std::clamp(
          static_cast<float>(center.greenF() + (channel_median(green) - center.greenF()) * amount),
          0.0F,
          1.0F));
      repaired.setBlueF(std::clamp(
          static_cast<float>(center.blueF() + (channel_median(blue) - center.blueF()) * amount),
          0.0F,
          1.0F));
      output.setPixelColor(x, y, repaired);
      changed = true;
    }
  }

  if (changed) {
    *image = std::move(output);
  }
  return changed;
}

bool apply_raw_false_color_suppression(QImage* image, double amount) {
  amount = std::clamp(amount, 0.0, 1.0);
  if (image == nullptr || image->isNull() || amount <= 0.0 || image->width() < 3 || image->height() < 3) {
    return false;
  }

  const QImage source = image->copy();
  QImage output = image->copy();
  const auto channel_median = [](std::array<double, 8> values) {
    std::sort(values.begin(), values.end());
    return (values[3] + values[4]) * 0.5;
  };
  const auto luma = [](const QColor& color) {
    return color.redF() * 0.2126 + color.greenF() * 0.7152 + color.blueF() * 0.0722;
  };

  auto changed = false;
  const auto threshold = 0.18 - amount * 0.12;
  for (int y = 1; y < source.height() - 1; ++y) {
    for (int x = 1; x < source.width() - 1; ++x) {
      std::array<double, 8> red{};
      std::array<double, 8> green{};
      std::array<double, 8> blue{};
      std::array<double, 8> neighbor_luma{};
      std::size_t index = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          const auto neighbor = source.pixelColor(x + dx, y + dy);
          red[index] = neighbor.redF();
          green[index] = neighbor.greenF();
          blue[index] = neighbor.blueF();
          neighbor_luma[index] = luma(neighbor);
          ++index;
        }
      }

      const auto center = source.pixelColor(x, y);
      const auto center_luma = luma(center);
      const auto median_luma = channel_median(neighbor_luma);
      const auto median_red = channel_median(red);
      const auto median_green = channel_median(green);
      const auto median_blue = channel_median(blue);
      const auto chroma_error = std::max({
          std::abs((center.redF() - center_luma) - (median_red - median_luma)),
          std::abs((center.greenF() - center_luma) - (median_green - median_luma)),
          std::abs((center.blueF() - center_luma) - (median_blue - median_luma)),
      });
      if (chroma_error <= threshold) {
        continue;
      }

      QColor repaired = center;
      repaired.setRedF(std::clamp(
          static_cast<float>(center.redF() + (center_luma + median_red - median_luma - center.redF()) * amount),
          0.0F,
          1.0F));
      repaired.setGreenF(std::clamp(
          static_cast<float>(center.greenF() + (center_luma + median_green - median_luma - center.greenF()) * amount),
          0.0F,
          1.0F));
      repaired.setBlueF(std::clamp(
          static_cast<float>(center.blueF() + (center_luma + median_blue - median_luma - center.blueF()) * amount),
          0.0F,
          1.0F));
      output.setPixelColor(x, y, repaired);
      changed = true;
    }
  }

  if (changed) {
    *image = std::move(output);
  }
  return changed;
}

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

bool decode_raw_preview_image(const QString& file_path, QImage* image, int max_edge) {
  if (image == nullptr || !has_raw_extension(file_path)) {
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

    if (!render_ciimage_to_linear_qimage(output_image, image)) {
      return false;
    }
    apply_raw_hot_dead_pixel_cleanup(
        image,
        raw_decode_hot_dead_pixel_cleanup_amount(settings));
    apply_raw_false_color_suppression(
        image,
        raw_decode_false_color_suppression_amount(settings));
    return !image->isNull();
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

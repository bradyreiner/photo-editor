#include "raw_image_decoder.h"
#include "raw_image_decoder_libraw.h"
#include "working_image.h"
#include "capture_camera_metadata.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>

#include <QColor>
#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

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

CIContext* shared_default_ci_context() {
  static CIContext* context = [CIContext context];
  return context;
}

CIContext* shared_ci_context() {
  static CIContext* context = [CIContext contextWithOptions:@{
    kCIContextUseSoftwareRenderer : @NO,
  }];
  return context;
}

CIContext* shared_software_ci_context() {
  static CIContext* context = [CIContext contextWithOptions:@{
    kCIContextUseSoftwareRenderer : @YES,
  }];
  return context;
}

quint64 ci_render_sentinel_sample_count(const QImage& image) {
  if (image.isNull() || image.format() != QImage::Format_RGBA16FPx4) {
    return std::numeric_limits<quint64>::max();
  }
  constexpr auto kSentinel = static_cast<quint16>(0xFFFFU);
  auto count = quint64{0U};
  for (int y = 0; y < image.height(); ++y) {
    const auto* scanline =
        reinterpret_cast<const quint16*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const auto offset = static_cast<std::size_t>(x) * 4U;
      for (std::size_t channel = 0; channel < 4U; ++channel) {
        if (scanline[offset + channel] == kSentinel) {
          ++count;
        }
      }
    }
  }
  return count;
}

void replace_ci_render_sentinels(QImage* image) {
  constexpr auto kSentinel = static_cast<quint16>(0xFFFFU);
  for (int y = 0; y < image->height(); ++y) {
    auto* scanline = reinterpret_cast<quint16*>(image->scanLine(y));
    for (int x = 0; x < image->width(); ++x) {
      const auto offset = static_cast<std::size_t>(x) * 4U;
      for (std::size_t channel = 0; channel < 4U; ++channel) {
        if (scanline[offset + channel] == kSentinel) {
          scanline[offset + channel] = 0U;
        }
      }
    }
  }
}

bool render_ciimage_to_verified_bitmap(
    CIContext* context,
    CIImage* input_image,
    CGRect extent,
    CGColorSpaceRef color_space,
    QImage* image) {
  if (context == nil || input_image == nil || color_space == nullptr ||
      image == nullptr) {
    return false;
  }
  const auto width = static_cast<int>(CGRectGetWidth(extent));
  const auto height = static_cast<int>(CGRectGetHeight(extent));
  QImage candidate(width, height, QImage::Format_RGBA16FPx4);
  if (candidate.isNull()) {
    return false;
  }
  std::memset(
      candidate.bits(), 0xFF, static_cast<std::size_t>(candidate.sizeInBytes()));
  [context render:input_image
           toBitmap:candidate.bits()
           rowBytes:static_cast<ptrdiff_t>(candidate.bytesPerLine())
             bounds:extent
             format:kCIFormatRGBAh
         colorSpace:color_space];
  const auto sentinel_count = ci_render_sentinel_sample_count(candidate);
  const auto sample_count = static_cast<quint64>(candidate.width()) *
                            static_cast<quint64>(candidate.height()) * 4U;
  if (sentinel_count >= sample_count) {
    return false;
  }
  if (sentinel_count != 0U) {
    replace_ci_render_sentinels(&candidate);
  }
  candidate.setColorSpace(linear_srgb_color_space());
  *image = std::move(candidate);
  return true;
}

bool render_ciimage_to_verified_destination(
    CIContext* context,
    CIImage* input_image,
    CGRect extent,
    CGColorSpaceRef color_space,
    QImage* image) {
  if (context == nil || input_image == nil || color_space == nullptr ||
      image == nullptr) {
    return false;
  }
  const auto width = static_cast<int>(CGRectGetWidth(extent));
  const auto height = static_cast<int>(CGRectGetHeight(extent));
  QImage candidate(width, height, QImage::Format_RGBA16FPx4);
  if (candidate.isNull()) {
    return false;
  }
  std::memset(
      candidate.bits(), 0xFF, static_cast<std::size_t>(candidate.sizeInBytes()));
  auto* destination = [[CIRenderDestination alloc]
      initWithBitmapData:candidate.bits()
                   width:static_cast<NSUInteger>(width)
                  height:static_cast<NSUInteger>(height)
             bytesPerRow:static_cast<NSUInteger>(candidate.bytesPerLine())
                  format:kCIFormatRGBAh];
  if (destination == nil) {
    return false;
  }
  destination.colorSpace = color_space;
  destination.alphaMode = CIRenderDestinationAlphaUnpremultiplied;
  destination.flipped = YES;
  destination.clamped = NO;
  NSError* scheduling_error = nil;
  CIRenderTask* task = [context startTaskToRender:input_image
                                         fromRect:extent
                                    toDestination:destination
                                          atPoint:CGPointZero
                                            error:&scheduling_error];
  if (task == nil || scheduling_error != nil) {
    return false;
  }
  NSError* completion_error = nil;
  CIRenderInfo* render_info =
      [task waitUntilCompletedAndReturnError:&completion_error];
  if (render_info == nil || completion_error != nil) {
    return false;
  }
  const auto sentinel_count = ci_render_sentinel_sample_count(candidate);
  const auto sample_count = static_cast<quint64>(candidate.width()) *
                            static_cast<quint64>(candidate.height()) * 4U;
  if (sentinel_count >= sample_count) {
    return false;
  }
  if (sentinel_count != 0U) {
    replace_ci_render_sentinels(&candidate);
  }
  candidate.setColorSpace(linear_srgb_color_space());
  *image = std::move(candidate);
  return true;
}

bool create_verified_ciimage_bitmap(
    CIContext* context,
    CIImage* input_image,
    CGRect extent,
    CGColorSpaceRef color_space,
    QImage* image) {
  if (context == nil || input_image == nil || color_space == nullptr ||
      image == nullptr) {
    return false;
  }
  CGImageRef cg_image = [context createCGImage:input_image
                                      fromRect:extent
                                        format:kCIFormatRGBAh
                                    colorSpace:color_space
                                      deferred:NO];
  if (cg_image == nullptr) {
    return false;
  }
  const auto width = static_cast<int>(CGRectGetWidth(extent));
  const auto height = static_cast<int>(CGRectGetHeight(extent));
  const auto pixel_bytes = static_cast<std::size_t>(width) * 8U;
  const auto source_row_bytes = CGImageGetBytesPerRow(cg_image);
  const auto bitmap_info = CGImageGetBitmapInfo(cg_image);
  const auto byte_order = bitmap_info & kCGBitmapByteOrderMask;
  const auto alpha_info = CGImageGetAlphaInfo(cg_image);
  const auto format_valid =
      CGImageGetWidth(cg_image) == static_cast<std::size_t>(width) &&
      CGImageGetHeight(cg_image) == static_cast<std::size_t>(height) &&
      CGImageGetBitsPerComponent(cg_image) == 16U &&
      CGImageGetBitsPerPixel(cg_image) == 64U &&
      (bitmap_info & kCGBitmapFloatComponents) != 0U &&
      (byte_order == kCGBitmapByteOrderDefault ||
       byte_order == kCGBitmapByteOrder16Little) &&
      (alpha_info == kCGImageAlphaPremultipliedLast ||
       alpha_info == kCGImageAlphaLast ||
       alpha_info == kCGImageAlphaNoneSkipLast) &&
      source_row_bytes >= pixel_bytes;
  CGDataProviderRef provider = CGImageGetDataProvider(cg_image);
  CFDataRef data = provider == nullptr ? nullptr : CGDataProviderCopyData(provider);
  if (!format_valid || data == nullptr || CFDataGetLength(data) < 0 ||
      static_cast<quint64>(CFDataGetLength(data)) <
          static_cast<quint64>(source_row_bytes) *
              static_cast<quint64>(height)) {
    if (data != nullptr) {
      CFRelease(data);
    }
    CGImageRelease(cg_image);
    return false;
  }

  QImage candidate(width, height, QImage::Format_RGBA16FPx4);
  if (candidate.isNull()) {
    CFRelease(data);
    CGImageRelease(cg_image);
    return false;
  }
  const auto* source = CFDataGetBytePtr(data);
  for (int y = 0; y < height; ++y) {
    std::memcpy(
        candidate.scanLine(y),
        source + static_cast<std::size_t>(y) * source_row_bytes,
        pixel_bytes);
  }
  CFRelease(data);
  CGImageRelease(cg_image);
  candidate.setColorSpace(linear_srgb_color_space());
  *image = std::move(candidate);
  return true;
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

  CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
  if (color_space == nullptr) {
    return false;
  }
  auto rendered = render_ciimage_to_verified_destination(
      shared_default_ci_context(), input_image, extent, color_space, image);
  if (!rendered) {
    rendered = render_ciimage_to_verified_destination(
      shared_ci_context(), input_image, extent, color_space, image);
  }
  if (!rendered) {
    rendered = render_ciimage_to_verified_destination(
        shared_software_ci_context(), input_image, extent, color_space, image);
  }
  if (!rendered) {
    rendered = render_ciimage_to_verified_bitmap(
        shared_ci_context(), input_image, extent, color_space, image);
  }
  if (!rendered) {
    rendered = render_ciimage_to_verified_bitmap(
        shared_software_ci_context(), input_image, extent, color_space, image);
  }
  if (!rendered) {
    rendered = create_verified_ciimage_bitmap(
        shared_default_ci_context(), input_image, extent, color_space, image);
  }
  if (!rendered) {
    rendered = create_verified_ciimage_bitmap(
        shared_ci_context(), input_image, extent, color_space, image);
  }
  if (!rendered) {
    rendered = create_verified_ciimage_bitmap(
        shared_software_ci_context(), input_image, extent, color_space, image);
  }
  CGColorSpaceRelease(color_space);
  return rendered;
}

bool decode_native_linear_qt_fallback(
    const QString& file_path,
    QImage* image,
    int max_edge) {
  if (image == nullptr) {
    return false;
  }
  QImageReader reader(file_path);
  reader.setAutoTransform(true);
  const auto source_size = reader.size();
  if (max_edge > 0 && source_size.isValid() &&
      std::max(source_size.width(), source_size.height()) > max_edge) {
    reader.setScaledSize(source_size.scaled(
        max_edge, max_edge, Qt::KeepAspectRatio));
  }
  auto decoded = reader.read();
  if (decoded.isNull()) {
    return false;
  }
  if (!decoded.colorSpace().isValid()) {
    decoded.setColorSpace(preview_color_space());
  }
  *image = working_image_qimage(
      std::move(decoded),
      QStringLiteral("Linear sRGB"));
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

}  // namespace

QVariantMap read_capture_camera_metadata(const QString& path) {
  if (has_raw_extension(path)) {
    const auto raw = read_libraw_capture_camera_metadata(path);
    if (raw.value("available").toBool()) return raw;
  }
  @autoreleasepool {
    CGImageSourceRef source = image_source_for_path(path);
    if (!source) return {{"available", false}, {"backend", "ImageIO"}};
    CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!properties) return {{"available", false}, {"backend", "ImageIO"}};
    NSDictionary* dictionary = CFBridgingRelease(properties);
    NSDictionary* tiff = dictionary[(__bridge NSString*)kCGImagePropertyTIFFDictionary];
    NSDictionary* exif = dictionary[(__bridge NSString*)kCGImagePropertyExifDictionary];
    if (![tiff isKindOfClass:[NSDictionary class]]) tiff = @{};
    if (![exif isKindOfClass:[NSDictionary class]]) exif = @{};
    const auto text = [](id value) {
      return [value isKindOfClass:[NSString class]] ? QString::fromNSString(value).trimmed().left(256) : QString();
    };
    const auto number = [](id value) {
      const auto result = [value isKindOfClass:[NSNumber class]] ? [value doubleValue] : 0.0;
      return std::isfinite(result) && result > 0 && result <= 10000 ? result : 0.0;
    };
    return {{"available", true}, {"backend", "ImageIO"},
        {"make", text(tiff[(__bridge NSString*)kCGImagePropertyTIFFMake])},
        {"model", text(tiff[(__bridge NSString*)kCGImagePropertyTIFFModel])},
        {"lens", text(exif[(__bridge NSString*)kCGImagePropertyExifLensModel])},
        {"bodySerial", text(exif[(__bridge NSString*)kCGImagePropertyExifBodySerialNumber])},
        {"focalMm", number(exif[(__bridge NSString*)kCGImagePropertyExifFocalLength])},
        {"focal35Mm", number(exif[(__bridge NSString*)kCGImagePropertyExifFocalLenIn35mmFilm])}};
  }
}

bool raw_image_supported(const QString& file_path) { return has_raw_extension(file_path); }

bool raw_decode_uses_draft_demosaic(const RawDecodeSettings& settings, bool scaled_preview) {
  Q_UNUSED(scaled_preview);
  const auto quality = settings.demosaic_quality.trimmed();
  if (quality.compare(QStringLiteral("Preview"), Qt::CaseInsensitive) == 0 ||
      quality.compare(QStringLiteral("Draft"), Qt::CaseInsensitive) == 0) {
    return true;
  }
  if (quality.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0 ||
      quality.compare(QStringLiteral("Best"), Qt::CaseInsensitive) == 0) {
    return false;
  }
  // A bounded preview may use fewer output pixels, but Balanced must retain
  // the same demosaic/color semantics as the full-resolution render. Core
  // Image's draft mode is a separate RAW rendering path and can visibly move
  // white balance and camera color when the full render replaces it.
  return false;
}

double raw_decode_hot_dead_pixel_cleanup_amount(const RawDecodeSettings& settings) {
  return std::clamp(settings.hot_dead_pixel_cleanup_amount, 0.0, 1.0);
}

double raw_decode_false_color_suppression_amount(const RawDecodeSettings& settings) {
  return std::clamp(settings.false_color_suppression_amount, 0.0, 1.0);
}

bool apply_raw_hot_dead_pixel_cleanup(
    QImage* image,
    double amount,
    photo_editor::pipeline::CancellationToken
        cancellation) {
  amount = std::clamp(amount, 0.0, 1.0);
  if (image == nullptr || image->isNull() ||
      amount <= 0.0 || image->width() < 3 ||
      image->height() < 3 ||
      cancellation.cancelled()) {
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
    if (cancellation.cancelled()) {
      return false;
    }
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

  if (changed && !cancellation.cancelled()) {
    *image = std::move(output);
  }
  return changed && !cancellation.cancelled();
}

bool apply_raw_false_color_suppression(
    QImage* image,
    double amount,
    photo_editor::pipeline::CancellationToken
        cancellation) {
  amount = std::clamp(amount, 0.0, 1.0);
  if (image == nullptr || image->isNull() ||
      amount <= 0.0 || image->width() < 3 ||
      image->height() < 3 ||
      cancellation.cancelled()) {
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
    if (cancellation.cancelled()) {
      return false;
    }
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

  if (changed && !cancellation.cancelled()) {
    *image = std::move(output);
  }
  return changed && !cancellation.cancelled();
}

bool native_image_supported(const QString& file_path) { return has_native_extension(file_path); }

QSize raw_image_size(const QString& file_path) {
  if (!has_raw_extension(file_path)) {
    return {};
  }

  const auto libraw_size = libraw_camera_linear_image_size(file_path);
  if (libraw_size.isValid()) {
    return libraw_size;
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
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    photo_editor::pipeline::CancellationToken
        cancellation) {
  if (image == nullptr || !has_raw_extension(file_path) ||
      cancellation.cancelled()) {
    return false;
  }
  QImage linear_image;
  auto characterized_settings = settings;
  characterized_settings.input_transform_enabled = false;
  if (!decode_raw_linear_image(file_path, &linear_image, max_edge,
                               characterized_settings, metadata,
                               cancellation)) {
    return false;
  }
  if (cancellation.cancelled()) {
    return false;
  }
  auto finalized = finalize_output_image(
      std::move(linear_image),
      preview_color_space());
  if (finalized.isNull() ||
      cancellation.cancelled()) {
    return false;
  }
  *image = std::move(finalized);
  return true;
}

bool decode_raw_linear_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    const RawDecodeSettings& settings,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    photo_editor::pipeline::CancellationToken
        cancellation) {
  if (image == nullptr || !has_raw_extension(file_path) ||
      cancellation.cancelled()) {
    return false;
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }

  if (raw_decode_requests_camera_linear_backend(settings)) {
    if (!decode_libraw_camera_linear_image(
            file_path,
            image,
            max_edge,
            settings,
            metadata,
            cancellation)) {
      return false;
    }
    apply_raw_hot_dead_pixel_cleanup(
        image,
        raw_decode_hot_dead_pixel_cleanup_amount(settings),
        cancellation);
    apply_raw_false_color_suppression(
        image,
        raw_decode_false_color_suppression_amount(settings),
        cancellation);
    return !cancellation.cancelled();
  }

  const auto decode_portable_fallback = [&]() {
    auto fallback_image = QImage{};
    auto fallback_metadata = photo_editor::pipeline::DecodedImageMetadata{};
    if (!decode_libraw_scene_linear_image(
            file_path,
            &fallback_image,
            max_edge,
            settings,
            metadata == nullptr ? nullptr : &fallback_metadata,
            cancellation)) {
      return false;
    }
    apply_raw_hot_dead_pixel_cleanup(
        &fallback_image,
        raw_decode_hot_dead_pixel_cleanup_amount(settings),
        cancellation);
    apply_raw_false_color_suppression(
        &fallback_image,
        raw_decode_false_color_suppression_amount(settings),
        cancellation);
    if (fallback_image.isNull() ||
        cancellation.cancelled()) {
      return false;
    }
    *image = std::move(fallback_image);
    if (metadata != nullptr) {
      *metadata = std::move(fallback_metadata);
    }
    return true;
  };

  // Settled/final RAW color must not change with the host platform. Core
  // Image remains available only for the separately labelled embedded/native
  // navigation preview; the editable linear image always comes from LibRaw.
  return decode_portable_fallback();
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

  if (raw_decode_requests_camera_linear_backend(settings)) {
    return read_libraw_camera_linear_metadata(
        file_path,
        metadata,
        settings);
  }

  return read_libraw_scene_linear_metadata(file_path, metadata, settings);
}

bool decode_native_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    photo_editor::pipeline::CancellationToken
        cancellation) {
  if (cancellation.cancelled()) {
    return false;
  }
  QImage linear_image;
  if (!decode_native_linear_image(
          file_path,
          &linear_image,
          max_edge,
          cancellation)) {
    return false;
  }

  auto finalized = finalize_output_image(
      std::move(linear_image),
      preview_color_space());
  if (finalized.isNull() ||
      cancellation.cancelled()) {
    return false;
  }
  *image = std::move(finalized);
  return true;
}

bool decode_native_linear_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    photo_editor::pipeline::CancellationToken
        cancellation) {
  if (image == nullptr || !has_native_extension(file_path) ||
      cancellation.cancelled()) {
    return false;
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }

  @autoreleasepool {
    CGImageSourceRef source = image_source_for_path(file_path);
    if (source == nullptr) {
      return !cancellation.cancelled() &&
             decode_native_linear_qt_fallback(file_path, image, max_edge) &&
             !cancellation.cancelled();
    }

    CGImageRef cg_image = decode_native_cgimage(source, max_edge);
    CFRelease(source);
    if (cancellation.cancelled()) {
      if (cg_image != nullptr) {
        CGImageRelease(cg_image);
      }
      return false;
    }
    if (cg_image == nullptr) {
      return decode_native_linear_qt_fallback(file_path, image, max_edge) &&
             !cancellation.cancelled();
    }

    CIImage* ci_image = [[CIImage alloc] initWithCGImage:cg_image];
    CGImageRelease(cg_image);
    if (ci_image == nil) {
      return decode_native_linear_qt_fallback(file_path, image, max_edge) &&
             !cancellation.cancelled();
    }

    if (!cancellation.cancelled() &&
        render_ciimage_to_linear_qimage(ci_image, image) &&
        !cancellation.cancelled()) {
      return true;
    }
    return !cancellation.cancelled() &&
           decode_native_linear_qt_fallback(file_path, image, max_edge) &&
           !cancellation.cancelled();
  }
}

}  // namespace photo_editor::desktop

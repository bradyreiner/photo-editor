#include "editor_preview_texture.h"

#if defined(Q_OS_MACOS)

#import <Metal/Metal.h>

#include <algorithm>

namespace photo_editor::desktop {

namespace {

class MetalEditorPreviewTexture final : public EditorPreviewTexture {
 public:
  MetalEditorPreviewTexture() {
    @autoreleasepool {
      device_ = MTLCreateSystemDefaultDevice();
      if (device_ == nil) {
        return;
      }
      command_queue_ = [device_ newCommandQueue];
      available_ = command_queue_ != nil;
    }
  }

  [[nodiscard]] bool available() const override { return available_; }
  [[nodiscard]] const char* backendName() const override { return "metal"; }

  bool upload(const QImage& image) override {
    if (!available_ || image.isNull() || image.width() <= 0 || image.height() <= 0) {
      clear();
      return false;
    }

    @autoreleasepool {
      const auto pixel_format = preferredPixelFormatForImage(image);
      const auto upload_image = uploadImageForFormat(image, pixel_format);
      if (upload_image.isNull()) {
        clear();
        return false;
      }

      const auto size = upload_image.size();
      if (!ensureTexture(size, pixel_format)) {
        clear();
        return false;
      }

      const auto region = MTLRegionMake2D(
          0,
          0,
          static_cast<NSUInteger>(size.width()),
          static_cast<NSUInteger>(size.height()));
      [texture_ replaceRegion:region
                   mipmapLevel:0
                     withBytes:upload_image.constBits()
                   bytesPerRow:static_cast<NSUInteger>(upload_image.bytesPerLine())];
      return true;
    }
  }

  bool uploadRegion(const QImage& image, const QRect& region) override {
    if (!available_ || image.isNull() || image.width() <= 0 || image.height() <= 0 ||
        !region.isValid()) {
      return false;
    }

    @autoreleasepool {
      const auto image_bounds = QRect(QPoint(0, 0), image.size());
      const auto upload_rect = region.intersected(image_bounds);
      if (upload_rect.isEmpty()) {
        return false;
      }
      if (upload_rect == image_bounds) {
        return upload(image);
      }
      if (texture_ == nil ||
          static_cast<int>(texture_.width) != image.width() ||
          static_cast<int>(texture_.height) != image.height()) {
        return false;
      }

      const auto upload_image = uploadImageForFormat(image, pixel_format_);
      if (upload_image.isNull()) {
        return false;
      }

      const auto bytes_per_pixel = qsizetype{bytesPerPixelForFormat(pixel_format_)};
      const auto* source = upload_image.constBits() +
                           upload_rect.y() * upload_image.bytesPerLine() +
                           upload_rect.x() * bytes_per_pixel;
      const auto metal_region = MTLRegionMake2D(
          static_cast<NSUInteger>(upload_rect.x()),
          static_cast<NSUInteger>(upload_rect.y()),
          static_cast<NSUInteger>(upload_rect.width()),
          static_cast<NSUInteger>(upload_rect.height()));
      [texture_ replaceRegion:metal_region
                   mipmapLevel:0
                     withBytes:source
                   bytesPerRow:static_cast<NSUInteger>(upload_image.bytesPerLine())];
      return true;
    }
  }

  bool uploadPatch(const QImage& image, const QRect& destination_region) override {
    if (!available_ || image.isNull() || image.width() <= 0 || image.height() <= 0 ||
        !destination_region.isValid() || destination_region.size() != image.size()) {
      return false;
    }

    @autoreleasepool {
      if (texture_ == nil || destination_region.x() < 0 || destination_region.y() < 0 ||
          destination_region.right() >= static_cast<int>(texture_.width) ||
          destination_region.bottom() >= static_cast<int>(texture_.height)) {
        return false;
      }

      const auto upload_image = uploadImageForFormat(image, pixel_format_);
      if (upload_image.isNull()) {
        return false;
      }

      const auto metal_region = MTLRegionMake2D(
          static_cast<NSUInteger>(destination_region.x()),
          static_cast<NSUInteger>(destination_region.y()),
          static_cast<NSUInteger>(destination_region.width()),
          static_cast<NSUInteger>(destination_region.height()));
      [texture_ replaceRegion:metal_region
                   mipmapLevel:0
                     withBytes:upload_image.constBits()
                   bytesPerRow:static_cast<NSUInteger>(upload_image.bytesPerLine())];
      return true;
    }
  }

  void clear() override {
    texture_ = nil;
    pixel_format_ = MTLPixelFormatInvalid;
  }

  [[nodiscard]] bool hasNativeTexture() const override {
    return texture_ != nil && texture_.width > 0 && texture_.height > 0;
  }

  [[nodiscard]] quint64 nativeTextureHandle() const override {
    return hasNativeTexture() ? static_cast<quint64>(reinterpret_cast<quintptr>(texture_)) : 0;
  }

  [[nodiscard]] QSize nativeTextureSize() const override {
    return hasNativeTexture()
               ? QSize(static_cast<int>(texture_.width), static_cast<int>(texture_.height))
               : QSize{};
  }
  [[nodiscard]] const char* nativeTextureFormatName() const override {
    if (!hasNativeTexture()) {
      return "none";
    }
    return pixel_format_ == MTLPixelFormatRGBA16Float ? "rgba16float" : "bgra8unorm";
  }
  [[nodiscard]] int nativeTextureBytesPerPixel() const override {
    if (!hasNativeTexture()) {
      return 0;
    }
    return bytesPerPixelForFormat(pixel_format_);
  }

 private:
  [[nodiscard]] static bool isDirectRgba16FloatUploadFormat(QImage::Format format) {
    return format == QImage::Format_RGBA16FPx4;
  }

  [[nodiscard]] static bool isDirectBgraUploadFormat(QImage::Format format) {
    return format == QImage::Format_ARGB32 ||
           format == QImage::Format_ARGB32_Premultiplied ||
           format == QImage::Format_RGB32;
  }

  [[nodiscard]] static MTLPixelFormat preferredPixelFormatForImage(const QImage& image) {
    return isDirectRgba16FloatUploadFormat(image.format())
               ? MTLPixelFormatRGBA16Float
               : MTLPixelFormatBGRA8Unorm;
  }

  [[nodiscard]] static int bytesPerPixelForFormat(MTLPixelFormat format) {
    return format == MTLPixelFormatRGBA16Float ? 8 : 4;
  }

  [[nodiscard]] static QImage uploadImageForFormat(const QImage& image, MTLPixelFormat format) {
    if (format == MTLPixelFormatRGBA16Float) {
      return isDirectRgba16FloatUploadFormat(image.format())
                 ? image
                 : image.convertToFormat(QImage::Format_RGBA16FPx4);
    }
    return isDirectBgraUploadFormat(image.format())
               ? image
               : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  }

  bool ensureTexture(const QSize& size, MTLPixelFormat pixel_format) {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
      return false;
    }
    if (texture_ != nil &&
        static_cast<int>(texture_.width) == size.width() &&
        static_cast<int>(texture_.height) == size.height() &&
        pixel_format_ == pixel_format) {
      return true;
    }

    auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixel_format
                                                                           width:static_cast<NSUInteger>(size.width())
                                                                          height:static_cast<NSUInteger>(size.height())
                                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    texture_ = [device_ newTextureWithDescriptor:descriptor];
    if (texture_ != nil) {
      pixel_format_ = pixel_format;
    }
    return texture_ != nil;
  }

  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> command_queue_ = nil;
  id<MTLTexture> texture_ = nil;
  MTLPixelFormat pixel_format_ = MTLPixelFormatInvalid;
  bool available_ = false;
};

}  // namespace

std::unique_ptr<EditorPreviewTexture> create_metal_editor_preview_texture() {
  auto texture = std::make_unique<MetalEditorPreviewTexture>();
  return texture->available() ? std::unique_ptr<EditorPreviewTexture>(std::move(texture)) : nullptr;
}

}  // namespace photo_editor::desktop

#endif

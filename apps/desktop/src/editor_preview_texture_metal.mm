#include "editor_preview_texture.h"

#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"

#if defined(Q_OS_MACOS)

#import <Metal/Metal.h>

#include <algorithm>
#include <atomic>
#include <QCoreApplication>
#include <QMetaObject>

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

  ~MetalEditorPreviewTexture() override {
    alive_->store(false, std::memory_order_release);
    ++publication_serial_;
  }

  [[nodiscard]] bool available() const override { return available_; }
  [[nodiscard]] const char* backendName() const override { return "metal"; }

  bool ensureNativeTexture(const QSize& size, QImage::Format format) override {
    if (!available_ || !size.isValid() || size.width() <= 0 || size.height() <= 0) {
      return false;
    }

    @autoreleasepool {
      return ensureTexture(size, preferredPixelFormatForImageFormat(format));
    }
  }

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
      ++publication_serial_;
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
      ++publication_serial_;
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
      ++publication_serial_;
      return true;
    }
  }

  [[nodiscard]] bool supportsNativeOutputSurface() const override {
    return available_ && texture_ != nil &&
           pixel_format_ == MTLPixelFormatBGRA8Unorm;
  }

  bool uploadNativePatchAsync(
      const photo_editor::pipeline::metal::MetalOutputSurface& surface,
      const QRect& destination_region,
      NativePatchCommitGuard commit_guard,
      NativePatchUploadCompletion completion) override {
    if (!supportsNativeOutputSurface() || !destination_region.isValid() ||
        destination_region.size() != QSize(surface.width(), surface.height()) ||
        surface.format() != photo_editor::pipeline::metal::
                                MetalOutputSurfaceFormat::Bgra8UnormSrgbEncoded ||
        surface.native_texture_handle() == 0U || destination_region.x() < 0 ||
        destination_region.y() < 0 ||
        destination_region.right() >= static_cast<int>(texture_.width) ||
        destination_region.bottom() >= static_cast<int>(texture_.height)) {
      return false;
    }
    @autoreleasepool {
      auto source = (__bridge id<MTLTexture>)(
          reinterpret_cast<void*>(surface.native_texture_handle()));
      if (source == nil || source.pixelFormat != MTLPixelFormatBGRA8Unorm ||
          surface.device_registry_id() != static_cast<std::uint64_t>(device_.registryID) ||
          source.device.registryID != device_.registryID ||
          static_cast<int>(source.width) != surface.width() ||
          static_cast<int>(source.height) != surface.height()) {
        return false;
      }
      const auto destination_size = nativeTextureSize();
      auto* descriptor = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                       width:static_cast<NSUInteger>(destination_size.width())
                                      height:static_cast<NSUInteger>(destination_size.height())
                                   mipmapped:NO];
      descriptor.usage = MTLTextureUsageShaderRead;
      descriptor.storageMode = MTLStorageModeShared;
      id<MTLTexture> replacement = [device_ newTextureWithDescriptor:descriptor];
      id<MTLCommandBuffer> command_buffer = [command_queue_ commandBuffer];
      if (replacement == nil || command_buffer == nil) {
        return false;
      }
      if (surface.native_ready_event_handle() != 0U) {
        auto ready_event = (__bridge id<MTLSharedEvent>)(
            reinterpret_cast<void*>(surface.native_ready_event_handle()));
        // Shared events are device-independent (their device property is nil).
        // The texture and surface device identities were checked above.
        if (ready_event == nil || surface.ready_event_value() == 0U) {
          return false;
        }
        [command_buffer encodeWaitForEvent:ready_event value:surface.ready_event_value()];
      }
      id<MTLBlitCommandEncoder> encoder = [command_buffer blitCommandEncoder];
      if (encoder == nil) return false;
      [encoder copyFromTexture:texture_
                   sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                     sourceSize:MTLSizeMake(texture_.width, texture_.height, 1)
                      toTexture:replacement destinationSlice:0 destinationLevel:0
             destinationOrigin:MTLOriginMake(0, 0, 0)];
      [encoder copyFromTexture:source
                   sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                     sourceSize:MTLSizeMake(source.width, source.height, 1)
                      toTexture:replacement destinationSlice:0 destinationLevel:0
             destinationOrigin:MTLOriginMake(
                                   static_cast<NSUInteger>(destination_region.x()),
                                   static_cast<NSUInteger>(destination_region.y()), 0)];
      [encoder endEncoding];
      const auto serial = ++publication_serial_;
      const auto alive = alive_;
      [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        const auto gpu_completed = completed.status == MTLCommandBufferStatusCompleted;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [=, this] {
          if (!alive->load(std::memory_order_acquire)) return;
          const auto publish = gpu_completed && publication_serial_ == serial &&
                               (!commit_guard || commit_guard());
          if (publish) texture_ = replacement;
          if (completion) completion(publish);
        }, Qt::QueuedConnection);
      }];
      [command_buffer commit];
      return true;
    }
  }

  bool adoptNativeSurface(
      const photo_editor::pipeline::metal::MetalOutputSurface& surface) override {
    if (!available_ ||
        surface.format() != photo_editor::pipeline::metal::
                                MetalOutputSurfaceFormat::Bgra8UnormSrgbEncoded ||
        surface.native_texture_handle() == 0U ||
        !surface.producer_completed_successfully() ||
        surface.device_registry_id() != static_cast<std::uint64_t>(device_.registryID)) {
      return false;
    }
    @autoreleasepool {
      auto source = (__bridge id<MTLTexture>)(
          reinterpret_cast<void*>(surface.native_texture_handle()));
      if (source == nil || source.pixelFormat != MTLPixelFormatBGRA8Unorm ||
          source.device.registryID != device_.registryID ||
          static_cast<int>(source.width) != surface.width() ||
          static_cast<int>(source.height) != surface.height()) {
        return false;
      }
      texture_ = source;
      pixel_format_ = MTLPixelFormatBGRA8Unorm;
      ++publication_serial_;
      return true;
    }
  }

  bool adoptNativeSurfaceAsync(
      const photo_editor::pipeline::metal::MetalOutputSurface& surface,
      NativePatchCommitGuard commit_guard,
      NativePatchUploadCompletion completion) override {
    if (!available_ ||
        surface.format() != photo_editor::pipeline::metal::
                                MetalOutputSurfaceFormat::Bgra8UnormSrgbEncoded ||
        surface.native_texture_handle() == 0U ||
        surface.device_registry_id() != static_cast<std::uint64_t>(device_.registryID)) {
      return false;
    }
    @autoreleasepool {
      auto source = (__bridge id<MTLTexture>)(
          reinterpret_cast<void*>(surface.native_texture_handle()));
      auto ready_event = (__bridge id<MTLSharedEvent>)(
          reinterpret_cast<void*>(surface.native_ready_event_handle()));
      if (source == nil || source.pixelFormat != MTLPixelFormatBGRA8Unorm ||
          source.device.registryID != device_.registryID ||
          static_cast<int>(source.width) != surface.width() ||
          static_cast<int>(source.height) != surface.height() || ready_event == nil ||
          surface.ready_event_value() == 0U) {
        return false;
      }
      id<MTLCommandBuffer> command_buffer = [command_queue_ commandBuffer];
      if (command_buffer == nil) return false;
      [command_buffer encodeWaitForEvent:ready_event value:surface.ready_event_value()];
      const auto serial = ++publication_serial_;
      const auto alive = alive_;
      [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        const auto gpu_completed = completed.status == MTLCommandBufferStatusCompleted;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [=, this] {
          if (!alive->load(std::memory_order_acquire)) return;
          const auto publish = gpu_completed && publication_serial_ == serial &&
                               (!commit_guard || commit_guard());
          if (publish) {
            texture_ = source;
            pixel_format_ = MTLPixelFormatBGRA8Unorm;
          }
          if (completion) completion(publish);
        }, Qt::QueuedConnection);
      }];
      [command_buffer commit];
      return true;
    }
  }

  void clear() override {
    ++publication_serial_;
    texture_ = nil;
    pixel_format_ = MTLPixelFormatInvalid;
  }

  [[nodiscard]] bool hasNativeTexture() const override {
    return texture_ != nil && texture_.width > 0 && texture_.height > 0;
  }

  [[nodiscard]] quint64 nativeTextureHandle() const override {
    return hasNativeTexture() ? static_cast<quint64>(reinterpret_cast<quintptr>(texture_)) : 0;
  }

  [[nodiscard]] std::shared_ptr<void> nativeTextureLease() const override {
    if (texture_ == nil) return {};
    // This source is compiled both with and without ARC. A bridge-retained
    // cast alone does not retain anything in a non-ARC target.
    void* retained = const_cast<void*>(CFRetain((__bridge CFTypeRef)texture_));
    return std::shared_ptr<void>(retained, [](void* texture) {
      CFRelease(static_cast<CFTypeRef>(texture));
    });
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
    return preferredPixelFormatForImageFormat(image.format());
  }

  [[nodiscard]] static MTLPixelFormat preferredPixelFormatForImageFormat(QImage::Format format) {
    return isDirectRgba16FloatUploadFormat(format)
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
      ++publication_serial_;
    }
    return texture_ != nil;
  }

  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> command_queue_ = nil;
  id<MTLTexture> texture_ = nil;
  MTLPixelFormat pixel_format_ = MTLPixelFormatInvalid;
  bool available_ = false;
  std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
  std::uint64_t publication_serial_ = 0U;
};

}  // namespace

std::unique_ptr<EditorPreviewTexture> create_metal_editor_preview_texture() {
  auto texture = std::make_unique<MetalEditorPreviewTexture>();
  return texture->available() ? std::unique_ptr<EditorPreviewTexture>(std::move(texture)) : nullptr;
}

}  // namespace photo_editor::desktop

#endif

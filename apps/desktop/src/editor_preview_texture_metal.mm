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
      const auto upload_image =
          isDirectBgraUploadFormat(image.format())
              ? image
              : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
      if (upload_image.isNull()) {
        clear();
        return false;
      }

      const auto size = upload_image.size();
      if (!ensureTexture(size)) {
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

  void clear() override {
    texture_ = nil;
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

 private:
  [[nodiscard]] static bool isDirectBgraUploadFormat(QImage::Format format) {
    return format == QImage::Format_ARGB32 ||
           format == QImage::Format_ARGB32_Premultiplied ||
           format == QImage::Format_RGB32;
  }

  bool ensureTexture(const QSize& size) {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
      return false;
    }
    if (texture_ != nil &&
        static_cast<int>(texture_.width) == size.width() &&
        static_cast<int>(texture_.height) == size.height()) {
      return true;
    }

    auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                          width:static_cast<NSUInteger>(size.width())
                                                                         height:static_cast<NSUInteger>(size.height())
                                                                      mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    texture_ = [device_ newTextureWithDescriptor:descriptor];
    return texture_ != nil;
  }

  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> command_queue_ = nil;
  id<MTLTexture> texture_ = nil;
  bool available_ = false;
};

}  // namespace

std::unique_ptr<EditorPreviewTexture> create_metal_editor_preview_texture() {
  auto texture = std::make_unique<MetalEditorPreviewTexture>();
  return texture->available() ? std::unique_ptr<EditorPreviewTexture>(std::move(texture)) : nullptr;
}

}  // namespace photo_editor::desktop

#endif

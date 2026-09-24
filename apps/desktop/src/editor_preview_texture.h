#pragma once

#include <QImage>
#include <QRect>
#include <QSize>

#include <functional>
#include <memory>

namespace photo_editor::pipeline::metal {
class MetalOutputSurface;
}

namespace photo_editor::desktop {

class EditorPreviewTexture {
 public:
  using NativePatchCommitGuard = std::function<bool()>;
  using NativePatchUploadCompletion = std::function<void(bool)>;

  virtual ~EditorPreviewTexture() = default;

  [[nodiscard]] virtual bool available() const = 0;
  [[nodiscard]] virtual const char* backendName() const = 0;
  virtual bool ensureNativeTexture(const QSize& size, QImage::Format format) = 0;
  virtual bool upload(const QImage& image) = 0;
  virtual bool uploadRegion(const QImage& image, const QRect& region) = 0;
  virtual bool uploadPatch(const QImage& image, const QRect& destination_region) = 0;
  [[nodiscard]] virtual bool supportsNativeOutputSurface() const { return false; }
  virtual bool uploadNativePatchAsync(
      const photo_editor::pipeline::metal::MetalOutputSurface&,
      const QRect&,
      NativePatchCommitGuard,
      NativePatchUploadCompletion completion) {
    if (completion) completion(false);
    return false;
  }
  virtual bool adoptNativeSurface(
      const photo_editor::pipeline::metal::MetalOutputSurface&) {
    return false;
  }
  virtual bool adoptNativeSurfaceAsync(
      const photo_editor::pipeline::metal::MetalOutputSurface& surface,
      NativePatchCommitGuard commit_guard,
      NativePatchUploadCompletion completion) {
    if (commit_guard && !commit_guard()) {
      if (completion) completion(false);
      return false;
    }
    const auto adopted = adoptNativeSurface(surface);
    if (completion) completion(adopted);
    return adopted;
  }
  virtual void clear() = 0;
  [[nodiscard]] virtual bool hasNativeTexture() const = 0;
  [[nodiscard]] virtual quint64 nativeTextureHandle() const = 0;
  // Keep an externally wrapped native texture alive through scene-graph use.
  [[nodiscard]] virtual std::shared_ptr<void> nativeTextureLease() const { return {}; }
  [[nodiscard]] virtual QSize nativeTextureSize() const = 0;
  [[nodiscard]] virtual const char* nativeTextureFormatName() const = 0;
  [[nodiscard]] virtual int nativeTextureBytesPerPixel() const = 0;

  [[nodiscard]] static std::unique_ptr<EditorPreviewTexture> create();
};

}  // namespace photo_editor::desktop

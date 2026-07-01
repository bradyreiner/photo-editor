#include "editor_preview_texture.h"

namespace photo_editor::desktop {

#if defined(Q_OS_MACOS) && defined(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND) && \
    PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND
std::unique_ptr<EditorPreviewTexture> create_metal_editor_preview_texture();
#endif

namespace {

class NullEditorPreviewTexture final : public EditorPreviewTexture {
 public:
  [[nodiscard]] bool available() const override { return false; }
  [[nodiscard]] const char* backendName() const override { return "cpu"; }
  bool upload(const QImage&) override { return false; }
  bool uploadRegion(const QImage&, const QRect&) override { return false; }
  bool uploadPatch(const QImage&, const QRect&) override { return false; }
  void clear() override {}
  [[nodiscard]] bool hasNativeTexture() const override { return false; }
  [[nodiscard]] quint64 nativeTextureHandle() const override { return 0; }
  [[nodiscard]] QSize nativeTextureSize() const override { return {}; }
  [[nodiscard]] const char* nativeTextureFormatName() const override { return "none"; }
  [[nodiscard]] int nativeTextureBytesPerPixel() const override { return 0; }
};

}  // namespace

std::unique_ptr<EditorPreviewTexture> EditorPreviewTexture::create() {
#if defined(Q_OS_MACOS) && defined(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND) && \
    PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND
  if (auto metal = create_metal_editor_preview_texture();
      metal != nullptr && metal->available()) {
    return metal;
  }
#endif
  return std::make_unique<NullEditorPreviewTexture>();
}

}  // namespace photo_editor::desktop

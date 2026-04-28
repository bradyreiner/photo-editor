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
  void clear() override {}
  [[nodiscard]] bool hasNativeTexture() const override { return false; }
  [[nodiscard]] quint64 nativeTextureHandle() const override { return 0; }
  [[nodiscard]] QSize nativeTextureSize() const override { return {}; }
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

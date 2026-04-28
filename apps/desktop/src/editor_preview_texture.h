#pragma once

#include <QImage>
#include <QSize>

#include <memory>

namespace photo_editor::desktop {

class EditorPreviewTexture {
 public:
  virtual ~EditorPreviewTexture() = default;

  [[nodiscard]] virtual bool available() const = 0;
  [[nodiscard]] virtual const char* backendName() const = 0;
  virtual bool upload(const QImage& image) = 0;
  virtual void clear() = 0;
  [[nodiscard]] virtual bool hasNativeTexture() const = 0;
  [[nodiscard]] virtual quint64 nativeTextureHandle() const = 0;
  [[nodiscard]] virtual QSize nativeTextureSize() const = 0;

  [[nodiscard]] static std::unique_ptr<EditorPreviewTexture> create();
};

}  // namespace photo_editor::desktop

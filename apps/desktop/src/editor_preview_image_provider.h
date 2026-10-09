#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

#include "editor_preview_image_sink.h"

namespace photo_editor::desktop {

class EditorPreviewImageProvider final : public QQuickImageProvider, public EditorPreviewImageSink {
 public:
  EditorPreviewImageProvider();

  [[nodiscard]] QImage requestImage(
      const QString& id,
      QSize* size,
      const QSize& requested_size) override;

  void setImage(const QString& key, const QImage& image) override;
  void clearImage(const QString& key) override;
  [[nodiscard]] QImage image(const QString& key) const override;

 private:
  [[nodiscard]] static QString normalized_key(const QString& id);

  mutable QMutex mutex_;
  QHash<QString, QImage> images_;
};

}  // namespace photo_editor::desktop

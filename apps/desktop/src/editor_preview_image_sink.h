#pragma once

#include <QImage>
#include <QString>

namespace photo_editor::desktop {

class EditorPreviewImageSink {
 public:
  virtual ~EditorPreviewImageSink() = default;

  virtual void setImage(const QString& key, const QImage& image) = 0;
  virtual void clearImage(const QString& key) = 0;
};

}  // namespace photo_editor::desktop

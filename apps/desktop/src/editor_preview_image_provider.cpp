#include "editor_preview_image_provider.h"

#include <QMutexLocker>

namespace photo_editor::desktop {

EditorPreviewImageProvider::EditorPreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage EditorPreviewImageProvider::requestImage(
    const QString& id,
    QSize* size,
    const QSize& requested_size) {
  QImage image;
  {
    QMutexLocker locker(&mutex_);
    image = images_.value(normalized_key(id));
  }

  if (size != nullptr) {
    *size = image.size();
  }
  if (image.isNull() || !requested_size.isValid()) {
    return image;
  }

  return image.scaled(
      requested_size,
      Qt::KeepAspectRatio,
      Qt::SmoothTransformation);
}

void EditorPreviewImageProvider::setImage(const QString& key, const QImage& image) {
  QMutexLocker locker(&mutex_);
  if (image.isNull()) {
    images_.remove(key);
    return;
  }
  images_.insert(key, image);
}

void EditorPreviewImageProvider::clearImage(const QString& key) {
  QMutexLocker locker(&mutex_);
  images_.remove(key);
}

QString EditorPreviewImageProvider::normalized_key(const QString& id) {
  const auto query_index = id.indexOf(QLatin1Char('?'));
  return query_index >= 0 ? id.left(query_index) : id;
}

}  // namespace photo_editor::desktop

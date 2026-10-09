#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QStringList>

namespace photo_editor::desktop {

// Full-resolution culling source loader. Qt invokes requestImage off the UI
// thread so RAW decoding never blocks review input or animation.
class LibraryOriginalImageProvider final : public QQuickImageProvider {
 public:
  LibraryOriginalImageProvider();

  [[nodiscard]] QImage requestImage(
      const QString& id,
      QSize* size,
      const QSize& requested_size) override;

  [[nodiscard]] static QString sourceUrl(const QString& file_path);

 private:
  struct CacheEntry final {
    QImage image;
    qint64 bytes = 0;
  };

  [[nodiscard]] static QString decoded_path(const QString& id);
  [[nodiscard]] static QString revision_key(const QString& file_path);
  void cacheImage(const QString& key, const QImage& image);

  mutable QMutex mutex_;
  QHash<QString, CacheEntry> cache_;
  QStringList most_recent_first_;
  qint64 cached_bytes_ = 0;
};

}  // namespace photo_editor::desktop

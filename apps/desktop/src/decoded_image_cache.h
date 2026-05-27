#pragma once

#include <QFileInfo>
#include <QImage>
#include <QString>
#include <QtGlobal>

namespace photo_editor::desktop {

struct DecodedImageCacheSnapshot final {
  int entries = 0;
  qint64 bytes = 0;
};

[[nodiscard]] QString decoded_image_cache_key(const QFileInfo& file_info, int max_edge);
[[nodiscard]] DecodedImageCacheSnapshot decoded_image_cache_snapshot();
[[nodiscard]] bool find_decoded_image_cache_entry(
    const QString& key,
    QImage* image,
    DecodedImageCacheSnapshot* snapshot = nullptr);
void insert_decoded_image_cache_entry(
    const QString& key,
    const QImage& image,
    DecodedImageCacheSnapshot* snapshot = nullptr);

}  // namespace photo_editor::desktop

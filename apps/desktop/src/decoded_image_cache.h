#pragma once

#include "photo_editor/pipeline/color_pipeline.h"

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
[[nodiscard]] qint64 decoded_image_cache_budget_bytes();
void set_decoded_image_cache_budget_bytes(qint64 bytes);
void clear_decoded_image_cache();
[[nodiscard]] bool find_decoded_image_cache_entry(
    const QString& key,
    QImage* image,
    DecodedImageCacheSnapshot* snapshot = nullptr);
void insert_decoded_image_cache_entry(
    const QString& key,
    const QImage& image,
    DecodedImageCacheSnapshot* snapshot = nullptr);
[[nodiscard]] bool find_decoded_image_cache_entry_with_metadata(
    const QString& key,
    QImage* image,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    DecodedImageCacheSnapshot* snapshot = nullptr);
void insert_decoded_image_cache_entry_with_metadata(
    const QString& key,
    const QImage& image,
    const photo_editor::pipeline::DecodedImageMetadata& metadata,
    DecodedImageCacheSnapshot* snapshot = nullptr);

}  // namespace photo_editor::desktop

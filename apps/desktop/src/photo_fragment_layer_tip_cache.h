#pragma once

#include "photo_paint_surface.h"
#include "photo_fragment_brush_preset_store.h"

#include <QImage>
#include <QVariantMap>

namespace photo_editor::desktop {

// Reuses an immutable, cropped paint-layer tip for repeated fragment strokes.
// The exact source fields form the key, so undo/redo and source edits cannot
// collide through a lossy digest.
class PhotoFragmentLayerTipCache final {
 public:
  static constexpr qsizetype kMaxRetainedBytes = 64 * 1024 * 1024;

  QImage prepare(const QString& asset_id, const QVariantMap& layer) {
    const auto key = sourceKey(asset_id, layer);
    if (!image_.isNull() && key == key_) return image_;

    auto image = PhotoFragmentBrushPresetStore::cropToVisiblePixels(
        rasterize_photo_paint_layer(layer, {}, nullptr));
    const auto retained = retainedBytes(key, image);
    if (image.isNull() || retained > kMaxRetainedBytes) {
      clear();
      return image;
    }
    key_ = key;
    image_ = image;
    retained_bytes_ = retained;
    return image;
  }

  void clear() {
    key_.clear();
    image_ = {};
    retained_bytes_ = 0;
  }

  QImage cachedImage() const { return image_; }
  QVariantMap cachedKey() const { return key_; }
  qsizetype retainedByteCount() const { return retained_bytes_; }

 private:
  static QVariantMap sourceKey(const QString& asset_id, const QVariantMap& layer) {
    return {{QStringLiteral("assetId"), asset_id},
        {QStringLiteral("layerId"), layer.value(QStringLiteral("id"))},
        {QStringLiteral("pixelWidth"), layer.value(QStringLiteral("pixelWidth"))},
        {QStringLiteral("pixelHeight"), layer.value(QStringLiteral("pixelHeight"))},
        {QStringLiteral("pixelData"), layer.value(QStringLiteral("pixelData"))},
        {QStringLiteral("paintTiles"), layer.value(QStringLiteral("paintTiles"))},
        {QStringLiteral("paintDraftTiles"), layer.value(QStringLiteral("paintDraftTiles"))}};
  }

  static qsizetype estimateVariant(const QVariant& value) {
    constexpr qsizetype kContainerEntryOverhead = 128;
    const auto type_id = value.metaType().id();
    if (type_id == QMetaType::QString) {
      return static_cast<qsizetype>(value.toString().size()) *
          static_cast<qsizetype>(sizeof(QChar)) + kContainerEntryOverhead;
    }
    if (type_id == QMetaType::QByteArray)
      return value.toByteArray().size() + kContainerEntryOverhead;
    if (type_id == QMetaType::QImage) {
      const auto image = value.value<QImage>();
      return image.isNull() ? kContainerEntryOverhead : image.sizeInBytes() + kContainerEntryOverhead;
    }
    if (type_id == QMetaType::QVariantMap) {
      qsizetype bytes = kContainerEntryOverhead;
      const auto map = value.toMap();
      for (auto it = map.cbegin(); it != map.cend(); ++it) {
        bytes += static_cast<qsizetype>(it.key().size()) * static_cast<qsizetype>(sizeof(QChar));
        bytes += kContainerEntryOverhead + estimateVariant(it.value());
      }
      return bytes;
    }
    if (type_id == QMetaType::QVariantList) {
      qsizetype bytes = kContainerEntryOverhead;
      const auto list = value.toList();
      for (const auto& item : list) bytes += kContainerEntryOverhead + estimateVariant(item);
      return bytes;
    }
    return kContainerEntryOverhead;
  }

  static qsizetype retainedBytes(const QVariantMap& key, const QImage& image) {
    return image.sizeInBytes() + estimateVariant(QVariant::fromValue(key));
  }

  QVariantMap key_;
  QImage image_;
  qsizetype retained_bytes_ = 0;
};

}  // namespace photo_editor::desktop

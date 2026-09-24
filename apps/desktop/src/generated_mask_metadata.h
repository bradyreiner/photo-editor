#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

struct GeneratedMaskMetadataFacts final {
  QSize source_size;
  double crop_x = 0.0;
  double crop_y = 0.0;
  double crop_width = 1.0;
  double crop_height = 1.0;
  double crop_zoom = 1.0;
  double transform_rotation = 0.0;
  QString crop_preset;
  QString source_image_version;
  QString runtime_label;
};

[[nodiscard]] QVariantMap make_generated_mask_metadata(
    const QString& kind,
    const GeneratedMaskMetadataFacts& facts);

[[nodiscard]] bool stamp_generated_mask_metadata(
    QVariantMap* mask,
    const GeneratedMaskMetadataFacts& facts);

[[nodiscard]] bool generated_mask_metadata_is_stale(
    const QVariantMap& mask,
    const QVariantMap& current_metadata);

}  // namespace photo_editor::desktop

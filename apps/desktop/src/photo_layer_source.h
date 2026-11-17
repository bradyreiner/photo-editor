#pragma once

#include <QImage>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

// Decode placed media with its own metadata. RAW is characterized into linear
// ProPhoto RGB, with no document grade or display/output transform baked in.
[[nodiscard]] QImage decode_photo_layer_source_image(const QString& path, int max_edge = 0);
[[nodiscard]] QImage decode_placed_photo_source_image(const QVariantMap& layer, const QString& working_profile);

}  // namespace photo_editor::desktop

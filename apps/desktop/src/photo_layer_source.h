#pragma once

#include <QImage>
#include <QString>

namespace photo_editor::desktop {

// Decode placed media with its own metadata. RAW is characterized into linear
// ProPhoto RGB, with no document grade or display/output transform baked in.
[[nodiscard]] QImage decode_photo_layer_source_image(const QString& path, int max_edge = 0);

}  // namespace photo_editor::desktop

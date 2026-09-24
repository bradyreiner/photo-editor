#pragma once

#include <QSize>
#include <QString>
#include <QFileInfo>

#include <algorithm>

#include "decoded_image_cache.h"
#include "edit_recipe_state.h"
#include "raw_decode_policy.h"
#include "raw_image_decoder.h"

namespace photo_editor::desktop {

[[nodiscard]] inline QString mask_tile_source_revision(
    const QString& file_path,
    const EditRecipeState& state,
    const QSize& raster_size) {
  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !raster_size.isValid()) {
    return {};
  }
  const auto source_edge = std::max(raster_size.width(), raster_size.height());
  auto revision = decoded_image_cache_key(source_info, source_edge);
  if (raw_image_supported(file_path)) {
    revision += QStringLiteral("|raw|") +
                raw_decode_settings_key_for_state(state);
  }
  return revision;
}

}  // namespace photo_editor::desktop

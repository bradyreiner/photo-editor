#pragma once
#include "edit_recipe_state.h"
#include "photo_pixel_stroke.h"
#include <deque>

namespace photo_editor::desktop {
struct PhotoPaintSession final {
  QString asset_id, layer_id, source_path;
  QVariantMap requested_layer, original_layer;
  EditRecipeState selection_state;
  QVariantList masks;
  QSize canvas_size;
  bool clip_selection = false, started = false, released = false;
  std::vector<PhotoPaintPoint> pending_points;
  std::shared_ptr<PhotoPixelStroke> stroke = std::make_shared<PhotoPixelStroke>();
  QVariantMap draft_tiles;
  QImage preview_base;
};
struct PhotoPaintWorkResult final {
  QVariantMap draft_tiles, committed_tiles;
  QString error;
  bool final = false, changed = false;
};
}  // namespace photo_editor::desktop

#pragma once
#include "edit_recipe_state.h"
#include "photo_pixel_stroke.h"
#include <deque>

namespace photo_editor::desktop {
struct PhotoPaintSession final {
  QString asset_id, layer_id, source_path, agent_task_id;
  QVariantMap requested_layer, original_layer;
  EditRecipeState selection_state;
  QVariantList masks;
  QSize canvas_size;
  bool erase_selection = false, clip_selection = false, materialize_only = false;
  bool materialize_layer_snapshot = false;
  bool base_image_erase = false;
  bool started = false, released = false;
  std::vector<PhotoPaintPoint> pending_points;
  std::shared_ptr<PhotoPixelStroke> stroke = std::make_shared<PhotoPixelStroke>();
  QVariantMap draft_tiles, materialized_tiles;
  QImage preview_base;
};
struct PhotoPaintWorkResult final {
  QVariantMap draft_tiles, committed_tiles, materialized_tiles;
  QString error;
  bool final = false, changed = false;
  double initialize_ms = 0.0, raster_ms = 0.0, finalize_ms = 0.0, commit_ms = 0.0;
};
}  // namespace photo_editor::desktop

#pragma once

#include <vector>

namespace photo_editor::pipeline {

struct RenderDirtyRegion final {
  double x = 0.0;
  double y = 0.0;
  double width = 1.0;
  double height = 1.0;
  bool full_frame = true;
};

struct RenderPixelRect final {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

enum class RenderTilePriority {
  Visible,
  Adjacent,
  Background,
};

struct RenderTileRequest final {
  int image_width = 0;
  int image_height = 0;
  int tile_size = 512;
  int padding_px = 0;
  int scale_level = 0;
  RenderDirtyRegion dirty_region;
  RenderDirtyRegion visible_region;
  bool prioritize_visible_region = false;
};

struct RenderTile final {
  int tile_x = 0;
  int tile_y = 0;
  int scale_level = 0;
  RenderPixelRect output_rect;
  RenderPixelRect padded_rect;
  RenderTilePriority priority = RenderTilePriority::Background;
};

[[nodiscard]] RenderPixelRect pixel_rect_for_dirty_region(
    const RenderDirtyRegion& region,
    int image_width,
    int image_height);
[[nodiscard]] RenderPixelRect expand_pixel_rect(
    const RenderPixelRect& rect,
    int padding_px,
    int image_width,
    int image_height);
[[nodiscard]] std::vector<RenderTile> plan_render_tiles(
    const RenderTileRequest& request);

}  // namespace photo_editor::pipeline

#include "photo_editor/pipeline/render_tile_planner.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {
namespace {

[[nodiscard]] RenderDirtyRegion normalize_dirty_region(
    RenderDirtyRegion region) {
  if (region.full_frame || region.width <= 0.0 || region.height <= 0.0) {
    return {};
  }
  const auto left = std::clamp(region.x, 0.0, 1.0);
  const auto top = std::clamp(region.y, 0.0, 1.0);
  const auto right = std::clamp(region.x + region.width, 0.0, 1.0);
  const auto bottom = std::clamp(region.y + region.height, 0.0, 1.0);
  if (right <= left || bottom <= top) {
    return {};
  }
  return {
      .x = left,
      .y = top,
      .width = right - left,
      .height = bottom - top,
      .full_frame = false,
  };
}

[[nodiscard]] bool rect_empty(const RenderPixelRect& rect) {
  return rect.width <= 0 || rect.height <= 0;
}

[[nodiscard]] int rect_right(const RenderPixelRect& rect) {
  return rect.x + rect.width;
}

[[nodiscard]] int rect_bottom(const RenderPixelRect& rect) {
  return rect.y + rect.height;
}

[[nodiscard]] bool rect_intersects(
    const RenderPixelRect& left,
    const RenderPixelRect& right) {
  return !rect_empty(left) && !rect_empty(right) &&
         left.x < rect_right(right) && rect_right(left) > right.x &&
         left.y < rect_bottom(right) && rect_bottom(left) > right.y;
}

}  // namespace

RenderPixelRect pixel_rect_for_dirty_region(
    const RenderDirtyRegion& region,
    int image_width,
    int image_height) {
  if (image_width <= 0 || image_height <= 0) {
    return {};
  }
  const auto normalized = normalize_dirty_region(region);
  if (normalized.full_frame) {
    return {.width = image_width, .height = image_height};
  }
  const auto left = std::clamp(
      static_cast<int>(std::floor(normalized.x * image_width)), 0, image_width);
  const auto top = std::clamp(
      static_cast<int>(std::floor(normalized.y * image_height)), 0, image_height);
  const auto right = std::clamp(
      static_cast<int>(
          std::ceil((normalized.x + normalized.width) * image_width)),
      0,
      image_width);
  const auto bottom = std::clamp(
      static_cast<int>(
          std::ceil((normalized.y + normalized.height) * image_height)),
      0,
      image_height);
  return right <= left || bottom <= top
             ? RenderPixelRect{.width = image_width, .height = image_height}
             : RenderPixelRect{
                   .x = left,
                   .y = top,
                   .width = right - left,
                   .height = bottom - top,
               };
}

RenderPixelRect expand_pixel_rect(
    const RenderPixelRect& rect,
    int padding_px,
    int image_width,
    int image_height) {
  if (image_width <= 0 || image_height <= 0 || rect_empty(rect)) {
    return {};
  }
  const auto padding = std::max(0, padding_px);
  const auto left = std::clamp(rect.x - padding, 0, image_width);
  const auto top = std::clamp(rect.y - padding, 0, image_height);
  const auto right =
      std::clamp(rect_right(rect) + padding, 0, image_width);
  const auto bottom =
      std::clamp(rect_bottom(rect) + padding, 0, image_height);
  return right <= left || bottom <= top
             ? RenderPixelRect{}
             : RenderPixelRect{
                   .x = left,
                   .y = top,
                   .width = right - left,
                   .height = bottom - top,
               };
}

std::vector<RenderTile> plan_render_tiles(
    const RenderTileRequest& request) {
  if (request.image_width <= 0 || request.image_height <= 0 ||
      request.tile_size <= 0) {
    return {};
  }
  const auto dirty = pixel_rect_for_dirty_region(
      request.dirty_region, request.image_width, request.image_height);
  if (rect_empty(dirty)) {
    return {};
  }
  auto visible = RenderPixelRect{};
  auto adjacent = RenderPixelRect{};
  if (request.prioritize_visible_region) {
    visible = pixel_rect_for_dirty_region(
        request.visible_region, request.image_width, request.image_height);
    adjacent = expand_pixel_rect(
        visible, request.tile_size, request.image_width, request.image_height);
  }
  const auto columns =
      (request.image_width + request.tile_size - 1) / request.tile_size;
  const auto rows =
      (request.image_height + request.tile_size - 1) / request.tile_size;
  std::vector<RenderTile> tiles;
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < columns; ++x) {
      const auto output = RenderPixelRect{
          .x = x * request.tile_size,
          .y = y * request.tile_size,
          .width = std::min(
              request.tile_size, request.image_width - x * request.tile_size),
          .height = std::min(
              request.tile_size, request.image_height - y * request.tile_size),
      };
      if (!rect_intersects(output, dirty)) {
        continue;
      }
      auto priority = RenderTilePriority::Background;
      if (request.prioritize_visible_region &&
          rect_intersects(output, visible)) {
        priority = RenderTilePriority::Visible;
      } else if (request.prioritize_visible_region &&
                 rect_intersects(output, adjacent)) {
        priority = RenderTilePriority::Adjacent;
      }
      tiles.push_back({
          .tile_x = x,
          .tile_y = y,
          .scale_level = request.scale_level,
          .output_rect = output,
          .padded_rect = expand_pixel_rect(
              output,
              request.padding_px,
              request.image_width,
              request.image_height),
          .priority = priority,
      });
    }
  }
  std::ranges::sort(tiles, [](const auto& left, const auto& right) {
    if (left.priority != right.priority) {
      return static_cast<int>(left.priority) <
             static_cast<int>(right.priority);
    }
    return left.tile_y != right.tile_y ? left.tile_y < right.tile_y
                                      : left.tile_x < right.tile_x;
  });
  return tiles;
}

}  // namespace photo_editor::pipeline

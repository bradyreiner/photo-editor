#include "photo_editor/pipeline/render_tile_planner.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

bool rect_is(
    const photo_editor::pipeline::RenderPixelRect& rect,
    int x,
    int y,
    int width,
    int height) {
  return rect.x == x && rect.y == y && rect.width == width &&
         rect.height == height;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using namespace photo_editor::pipeline;
  const auto mapped = pixel_rect_for_dirty_region(
      {.x = 0.125,
       .y = 0.25,
       .width = 0.25,
       .height = 0.125,
       .full_frame = false},
      800,
      600);
  if (!rect_is(mapped, 100, 150, 200, 75) ||
      !rect_is(expand_pixel_rect(mapped, 32, 800, 600), 68, 118, 264, 139)) {
    return fail("dirty-region mapping and padding must be conservative");
  }
  const auto full = plan_render_tiles({
      .image_width = 1000,
      .image_height = 800,
      .tile_size = 256,
      .padding_px = 16,
  });
  if (full.size() != 16U ||
      !rect_is(full.front().output_rect, 0, 0, 256, 256) ||
      !rect_is(full.front().padded_rect, 0, 0, 272, 272) ||
      !rect_is(full.back().output_rect, 768, 768, 232, 32) ||
      !rect_is(full.back().padded_rect, 752, 752, 248, 48)) {
    return fail("full-frame tiles must cover edges with clamped padding");
  }
  auto full_coverage =
      std::vector<std::uint8_t>(
          static_cast<std::size_t>(1000 * 800),
          0U);
  for (const auto& tile : full) {
    for (int y = tile.output_rect.y;
         y < tile.output_rect.y + tile.output_rect.height;
         ++y) {
      for (int x = tile.output_rect.x;
           x < tile.output_rect.x + tile.output_rect.width;
           ++x) {
        auto& coverage =
            full_coverage[static_cast<std::size_t>(
                y * 1000 + x)];
        if (coverage != 0U) {
          return fail(
              "tile output rectangles must not overlap at seams");
        }
        coverage = 1U;
      }
    }
  }
  if (!std::ranges::all_of(
          full_coverage,
          [](std::uint8_t coverage) {
            return coverage == 1U;
          })) {
    return fail(
        "tile output rectangles must cover every output pixel exactly once");
  }
  const auto dirty = plan_render_tiles({
      .image_width = 1000,
      .image_height = 800,
      .tile_size = 256,
      .padding_px = 8,
      .dirty_region =
          {
              .x = 0.30,
              .y = 0.30,
              .width = 0.02,
              .height = 0.02,
              .full_frame = false,
          },
  });
  if (dirty.size() != 1U || dirty.front().tile_x != 1 ||
      dirty.front().tile_y != 0 ||
      !rect_is(dirty.front().output_rect, 256, 0, 256, 256) ||
      !rect_is(dirty.front().padded_rect, 248, 0, 272, 264)) {
    return fail("localized invalidation must schedule only intersecting tiles");
  }
  const auto prioritized = plan_render_tiles({
      .image_width = 1024,
      .image_height = 1024,
      .tile_size = 256,
      .visible_region =
          {
              .x = 0.375,
              .y = 0.375,
              .width = 0.125,
              .height = 0.125,
              .full_frame = false,
          },
      .prioritize_visible_region = true,
  });
  if (prioritized.size() != 16U ||
      prioritized.front().priority != RenderTilePriority::Visible ||
      prioritized.front().tile_x != 1 || prioritized.front().tile_y != 1 ||
      prioritized.at(1).priority != RenderTilePriority::Adjacent ||
      prioritized.back().priority != RenderTilePriority::Background) {
    return fail("visible, adjacent, and background tile priority must be stable");
  }
  return EXIT_SUCCESS;
}

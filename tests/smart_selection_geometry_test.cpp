#include "smart_selection_geometry.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QByteArray map_bytes(std::initializer_list<int> values) {
  QByteArray result;
  result.reserve(static_cast<qsizetype>(values.size()));
  for (const auto value : values) {
    result.append(static_cast<char>(value));
  }
  return result;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  const auto instance_map = map_bytes({1, 2, 3, 4, 5, 6});
  if (instance_id_from_map(instance_map, 2, 3, 0.0, 0.0) != 1 ||
      instance_id_from_map(instance_map, 2, 3, 1.0, 1.0) != 6 ||
      instance_id_from_map(instance_map, 2, 3, -1.0, 2.0) != 5) {
    return fail("normalized instance lookup must clamp and prefer the direct orientation");
  }
  const auto fallback_map = map_bytes({0, 0, 3, 4, 5, 6});
  if (instance_id_from_map(fallback_map, 2, 3, 0.0, 0.0) != 5) {
    return fail("empty direct lookup must fall back to the vertically flipped orientation");
  }
  if (instance_id_from_map(map_bytes({1}), 2, 3, 0.0, 0.0) != 0 ||
      instance_id_at_map_point(map_bytes({1}), 2, 3, 0, 0) != 0) {
    return fail("truncated instance rasters must fail closed instead of indexing past data");
  }

  if (instance_pixel_count_in_map(instance_map, 2, 3, 3) != 1 ||
      instance_pixel_count_in_map(map_bytes({3, 3, 3}), 2, 3, 3) != 3 ||
      instance_pixel_count_in_map(instance_map, 2, 3, 0) != 0 ||
      instance_id_at_map_point(instance_map, 2, 3, 1, 2) != 6 ||
      instance_id_at_map_point(instance_map, 2, 3, 2, 2) != 0) {
    return fail("instance counting and point lookup must honor bounded raster geometry");
  }

  const auto seeds = smart_select_seed_points(5, 5, 0.5, 0.25);
  if (seeds.size() != 2 || seeds.at(0) != QPoint(2, 1) ||
      seeds.at(1) != QPoint(2, 3) ||
      smart_select_seed_points(5, 5, 0.5, 0.5).size() != 1 ||
      !smart_select_seed_points(0, 5, 0.5, 0.5).isEmpty()) {
    return fail("smart-selection seeds must include exact direct and flipped candidates");
  }
  return EXIT_SUCCESS;
}

#include "smart_selection_instance_policy.h"

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

  const auto direct = map_bytes({
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,
      0, 0, 3, 0, 0,
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,
  });
  if (smart_select_instance_candidates_from_map(direct, 5, 5, 0.5, 0.5) !=
          QVector<int>{3} ||
      !smart_select_instance_candidates_from_map({}, 5, 5, 0.5, 0.5).isEmpty()) {
    return fail("direct instance arbitration must win and invalid maps must fail closed");
  }

  const auto nearby = map_bytes({
      0, 0, 0, 0, 0,
      0, 2, 2, 1, 0,
      0, 2, 0, 1, 0,
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,
  });
  if (smart_select_instance_candidates_from_map(nearby, 5, 5, 0.5, 0.5) !=
      QVector<int>({2, 1})) {
    return fail("equidistant nearby instances must be sorted by coverage then identity");
  }

  const auto flipped = map_bytes({
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,
      0, 0, 4, 0, 0,
      0, 0, 0, 0, 0,
  });
  if (smart_select_instance_candidates_from_map(flipped, 5, 5, 0.5, 0.25) !=
      QVector<int>{4}) {
    return fail("instance arbitration must retain the vertically flipped orientation fallback");
  }

  const auto path_map = map_bytes({1, 1, 1, 2, 2});
  if (smart_select_instance_candidates_for_path(
          path_map, 5, 1, {QPointF(0.0, 0.0), QPointF(1.0, 0.0)}) !=
          QVector<int>({1, 2}) ||
      !smart_select_instance_candidates_for_path(path_map, 5, 1, {}).isEmpty()) {
    return fail("path arbitration must vote across sampled points and preserve stable ranking");
  }

  const auto selected_map = map_bytes({1, 2, 2, 3, 2, 0});
  const auto mask = segmentation_mask_for_instances(selected_map, 3, 2, {2, 2, -1});
  const auto alpha = mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (mask.value(QStringLiteral("kind")).toString() != QStringLiteral("object") ||
      mask.value(QStringLiteral("segmentationWidth")).toInt() != 3 ||
      mask.value(QStringLiteral("segmentationHeight")).toInt() != 2 ||
      alpha.size() != 6 || static_cast<unsigned char>(alpha.at(0)) != 0U ||
      static_cast<unsigned char>(alpha.at(1)) != 255U ||
      static_cast<unsigned char>(alpha.at(2)) != 255U ||
      static_cast<unsigned char>(alpha.at(4)) != 255U ||
      !segmentation_mask_for_instances(selected_map, 3, 2, {9}).isEmpty() ||
      !segmentation_mask_for_instances(selected_map, 3, 2, {}).isEmpty()) {
    return fail("selected instances must materialize exact bounded object alpha");
  }

  return EXIT_SUCCESS;
}

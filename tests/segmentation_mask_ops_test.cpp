#include "segmentation_mask_ops.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QByteArray bytes(std::initializer_list<int> values) {
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

  if (selected_pixel_count_for_mask_data(bytes({0, 1, 255, 0})) != 2) {
    return fail("selected-pixel counting must use unsigned non-zero alpha");
  }

  const auto existing = bytes({0, 100, 250, 255});
  const auto incoming = bytes({5, 150, 10, 255});
  if (union_segmentation_masks(existing, 2, 2, incoming, 2, 2) !=
      bytes({5, 150, 250, 255})) {
    return fail("mask union must take the exact per-pixel maximum");
  }
  if (union_segmentation_masks(existing, 2, 2, incoming, 4, 1) != incoming ||
      union_segmentation_masks(existing, 2, 2, {}, 0, 0) != existing) {
    return fail(
        "mask union must replace mismatched valid input and ignore invalid input");
  }

  if (subtract_segmentation_mask(existing, 2, 2, incoming, 2, 2) !=
      bytes({0, 0, 240, 0}) ||
      subtract_segmentation_mask(existing, 2, 2, incoming, 4, 1) != existing) {
    return fail(
        "mask subtraction must be clamped and fail closed on mismatch");
  }

  const auto source = bytes({0, 64, 128, 255});
  if (resample_segmentation_mask(source, 2, 2, 2, 2) != source ||
      !resample_segmentation_mask({}, 0, 0, 2, 2).isEmpty() ||
      !resample_segmentation_mask(source, 2, 2, 0, 2).isEmpty()) {
    return fail(
        "resampling must preserve identity and reject invalid dimensions");
  }
  const auto scaled = resample_segmentation_mask(source, 2, 2, 3, 3);
  if (scaled.size() != 9 ||
      static_cast<unsigned char>(scaled.at(0)) != 0U ||
      static_cast<unsigned char>(scaled.at(2)) != 64U ||
      static_cast<unsigned char>(scaled.at(4)) != 112U ||
      static_cast<unsigned char>(scaled.at(6)) != 128U ||
      static_cast<unsigned char>(scaled.at(8)) != 255U) {
    return fail(
        "bilinear mask resampling must preserve corners and exact center interpolation");
  }
  return EXIT_SUCCESS;
}

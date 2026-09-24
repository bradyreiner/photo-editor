#include "object_pick_policy.h"

#include <cstdlib>
#include <iostream>

#include <QImage>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::SegmentationResult result_with_pixel(
    int x,
    int y,
    int width = 4,
    int height = 4) {
  photo_editor::desktop::SegmentationResult result;
  result.width = width;
  result.height = height;
  result.mask_data = QByteArray(width * height, char(0));
  result.mask_data[y * width + x] = char(0xff);
  result.success = true;
  return result;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  const auto instance = result_with_pixel(0, 0);
  const auto prompt = result_with_pixel(2, 2);
  if (!should_prefer_prompt_object_pick_result(
          instance, prompt, 0.5, 0.5) ||
      should_prefer_prompt_object_pick_result(
          prompt, instance, 0.5, 0.5) ||
      !should_prefer_prompt_object_pick_result({}, prompt, 0.5, 0.5) ||
      should_prefer_prompt_object_pick_result(instance, {}, 0.5, 0.5)) {
    return fail("object-pick arbitration must prefer valid target-centered prompt results");
  }

  if (localized_object_pick_bounds(QSize{}, 0.5, 0.5).isValid() ||
      localized_object_pick_bounds(QSize(1000, 500), 0.5, 0.5) !=
          QRect(435, 185, 130, 130) ||
      localized_object_pick_bounds(QSize(1000, 500), -1.0, -1.0) !=
          QRect(0, 0, 130, 130)) {
    return fail("localized object-pick bounds must be exact, clamped, and aspect independent");
  }

  SegmentationResult square_failure;
  square_failure.width = 100;
  square_failure.height = 100;
  square_failure.mask_data = QByteArray(10000, char(0));
  for (int y = 28; y < 72; ++y) {
    for (int x = 28; x < 72; ++x) {
      square_failure.mask_data[y * 100 + x] = char(0xff);
    }
  }
  QImage source(100, 100, QImage::Format_ARGB32);
  source.fill(Qt::black);
  if (!segmentation_result_is_localized_square_failure(
          source, square_failure, 0.5, 0.5) ||
      segmentation_result_is_localized_square_failure(
          source, result_with_pixel(2, 2, 100, 100), 0.5, 0.5) ||
      segmentation_result_is_localized_square_failure(
          QImage{}, square_failure, 0.5, 0.5)) {
    return fail("localized crop-shaped masks must be rejected without rejecting compact objects");
  }

  SegmentationResult local;
  local.width = 2;
  local.height = 2;
  local.mask_data = QByteArray("\x01\x02\x03\x04", 4);
  local.success = true;
  const auto remapped = remap_localized_segmentation_result(
      local, QRect(1, 1, 2, 2), QSize(4, 4));
  if (!remapped.success || remapped.width != 4 || remapped.height != 4 ||
      static_cast<unsigned char>(remapped.mask_data.at(5)) != 1U ||
      static_cast<unsigned char>(remapped.mask_data.at(6)) != 2U ||
      static_cast<unsigned char>(remapped.mask_data.at(9)) != 3U ||
      static_cast<unsigned char>(remapped.mask_data.at(10)) != 4U ||
      static_cast<unsigned char>(remapped.mask_data.at(0)) != 0U ||
      remap_localized_segmentation_result(
          local, QRect(3, 3, 2, 2), QSize(4, 4))
          .success) {
    return fail("localized remapping must place exact alpha and reject out-of-bounds writes");
  }

  SegmentationResult plausible;
  plausible.width = 4;
  plausible.height = 4;
  plausible.mask_data = QByteArray(16, char(0));
  for (const auto index : {5, 6, 9, 10}) {
    plausible.mask_data[index] = char(0xff);
  }
  auto full = plausible;
  full.mask_data.fill(char(0xff));
  if (!single_point_prompt_result_is_plausible(plausible, 0.5, 0.5) ||
      single_point_prompt_result_is_plausible(
          result_with_pixel(2, 2), 0.5, 0.5) ||
      single_point_prompt_result_is_plausible(full, 0.5, 0.5) ||
      single_point_prompt_result_is_plausible(plausible, 0.0, 0.0)) {
    return fail("single-point results must be centered, bounded, nontrivial, and non-global");
  }
  return EXIT_SUCCESS;
}

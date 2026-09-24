#include "localized_object_refinement.h"

#include "segmentation_result_schema.h"

#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QImage>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  SegmentationResult original;
  original.width = 4;
  original.height = 4;
  original.mask_data = QByteArray(16, char(0xff));
  original.success = true;
  const auto null_result = refine_localized_object_mask_by_seed_color(
      QImage{}, original, 0.5, 0.5);
  if (null_result.mask_data != original.mask_data ||
      null_result.width != original.width || null_result.height != original.height) {
    return fail("null localized images must preserve the supplied model result");
  }

  QImage uniform(4, 4, QImage::Format_ARGB32);
  uniform.fill(QColor(180, 80, 40));
  auto bounded = original;
  bounded.mask_data.fill(char(0));
  for (const auto index : {5, 6, 9, 10}) {
    bounded.mask_data[index] = char(0xff);
  }
  const auto unchanged = refine_localized_object_mask_by_seed_color(
      uniform, bounded, 0.5, 0.5);
  if (unchanged.mask_data != bounded.mask_data ||
      unchanged.width != bounded.width || unchanged.height != bounded.height) {
    return fail("refinement must preserve a model mask when color flooding cannot narrow it");
  }

  QImage split(8, 4, QImage::Format_ARGB32);
  for (int y = 0; y < split.height(); ++y) {
    for (int x = 0; x < split.width(); ++x) {
      split.setPixelColor(
          x,
          y,
          x < 4 ? QColor(220, 40, 30) : QColor(25, 45, 220));
    }
  }
  const auto refined = refine_localized_object_mask_by_seed_color(
      split, SegmentationResult{}, 0.1, 0.5);
  if (!refined.success || refined.width != 8 || refined.height != 4 ||
      segmentation_result_nonzero_count(refined) != 16 ||
      static_cast<unsigned char>(refined.mask_data.at(1 * 8 + 1)) != 255U ||
      static_cast<unsigned char>(refined.mask_data.at(1 * 8 + 6)) != 0U) {
    return fail("seed-color refinement must flood the connected matching region only");
  }
  const auto repeated = refine_localized_object_mask_by_seed_color(
      split, SegmentationResult{}, 0.1, 0.5);
  if (repeated.mask_data != refined.mask_data) {
    return fail("localized seed-color refinement must be deterministic");
  }
  return EXIT_SUCCESS;
}

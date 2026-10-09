#include "segmentation_result_schema.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::SegmentationResult square_result() {
  photo_editor::desktop::SegmentationResult result;
  result.width = 4;
  result.height = 4;
  result.mask_data = QByteArray(16, char(0));
  for (const auto index : {5, 6, 9, 10}) {
    result.mask_data[index] = char(0xff);
  }
  result.success = true;
  return result;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  const auto square = square_result();
  const auto variant =
      segmentation_mask_variant(square, QStringLiteral(" Subject "));
  if (variant.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("subject") ||
      variant.value(QStringLiteral("segmentationData")).toByteArray() !=
          square.mask_data ||
      !segmentation_mask_variant({}, QStringLiteral("subject")).isEmpty()) {
    return fail("segmentation variants must normalize valid results and reject invalid payloads");
  }
  const auto restored = segmentation_result_from_mask_variant(variant);
  const auto invalid = segmentation_result_from_mask_variant({});
  if (!restored.success || restored.mask_data != square.mask_data ||
      invalid.success || invalid.error_message !=
          QStringLiteral("Invalid segmentation payload")) {
    return fail("segmentation variant round trips must be exact and fail closed");
  }
  auto refinement_result = square;
  const float refinement_logits[4]{-2.0F, -0.5F, 0.5F, 2.0F};
  refinement_result.refinement_logits = QByteArray(
      reinterpret_cast<const char*>(refinement_logits),
      static_cast<qsizetype>(sizeof(refinement_logits)));
  refinement_result.refinement_logits_width = 2;
  refinement_result.refinement_logits_height = 2;
  const auto refinement_variant =
      segmentation_mask_variant(refinement_result, QStringLiteral("object"));
  const auto refinement_restored =
      segmentation_result_from_mask_variant(refinement_variant);
  if (refinement_restored.refinement_logits != refinement_result.refinement_logits ||
      refinement_restored.refinement_logits_width != 2 ||
      refinement_restored.refinement_logits_height != 2) {
    return fail("Smart Select preview variants must preserve exact decoder logits for iterative refinement");
  }

  if (segmentation_result_nonzero_count(square) != 4 ||
      segmentation_result_coverage(square) != 0.25 ||
      segmentation_result_bounds(square) != QRect(1, 1, 2, 2) ||
      segmentation_result_is_too_small_to_use(square) ||
      segmentation_mask_value_at_normalized(
          square.mask_data, 4, 4, 0.5, 0.5) != 255 ||
      segmentation_mask_value_at_normalized(
          square.mask_data, 4, 4, -1.0, -1.0) != 0) {
    return fail("segmentation statistics, bounds, and normalized sampling must be exact");
  }
  auto small = square;
  small.mask_data.fill(char(0));
  small.mask_data[5] = char(0xff);
  if (!segmentation_result_is_too_small_to_use(small)) {
    return fail("single-pixel segmentation results must be rejected as too small");
  }
  SegmentationResult thin;
  thin.width = 10;
  thin.height = 10;
  thin.mask_data = QByteArray(100, char(0));
  for (int y = 2; y < 7; ++y) {
    thin.mask_data[y * 10 + 4] = char(0xff);
  }
  if (!subject_segmentation_result_is_thin_fragment(thin)) {
    return fail("low-coverage high-aspect subject fragments must be classified deterministically");
  }

  auto confidence_result = square;
  confidence_result.confidence_data = QByteArray(4, char(127));
  confidence_result.confidence_width = 2;
  confidence_result.confidence_height = 2;
  QVariantMap mask{
      {QStringLiteral("generatedMaskMetadata"),
       QVariantMap{{QStringLiteral("preserved"), true}}},
  };
  apply_generated_confidence_map(&mask, confidence_result);
  const auto metadata =
      mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (!segmentation_result_has_confidence_map(confidence_result) ||
      mask.value(QStringLiteral("confidenceData")).toByteArray() !=
          confidence_result.confidence_data ||
      metadata.value(QStringLiteral("confidenceResolution")).toString() !=
          QStringLiteral("2x2") ||
      !metadata.value(QStringLiteral("preserved")).toBool()) {
    return fail("confidence publication must preserve exact raster and additive provenance");
  }
  clear_generated_confidence_map(&mask);
  const auto cleared_metadata =
      mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (mask.contains(QStringLiteral("confidenceData")) ||
      cleared_metadata.contains(QStringLiteral("confidenceMapAvailable")) ||
      !cleared_metadata.value(QStringLiteral("preserved")).toBool()) {
    return fail("confidence clearing must remove owned fields and preserve unrelated provenance");
  }
  return EXIT_SUCCESS;
}

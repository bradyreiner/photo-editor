#include "generated_mask_cleanup.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;
  constexpr int kWidth = 10;
  constexpr int kHeight = 10;
  QByteArray alpha(kWidth * kHeight, char(0));
  for (int y = 2; y <= 4; ++y) {
    for (int x = 2; x <= 4; ++x) {
      alpha[y * kWidth + x] = char(0xff);
    }
  }
  alpha[3 * kWidth + 3] = char(0);
  alpha[8 * kWidth + 8] = char(0xff);

  QVariantMap mask{
      {QStringLiteral("kind"), QStringLiteral("subject")},
      {QStringLiteral("segmentationData"), alpha},
      {QStringLiteral("segmentationWidth"), kWidth},
      {QStringLiteral("segmentationHeight"), kHeight},
      {QStringLiteral("generatedMaskHoleFillThreshold"), 0.50},
      {QStringLiteral("generatedMaskIslandRemovalThreshold"), 0.50},
      {QStringLiteral("generatedMaskMetadata"),
       QVariantMap{{QStringLiteral("modelName"), QStringLiteral("fixture")}}},
  };
  if (!apply_generated_mask_cleanup(&mask)) {
    return fail("valid generated alpha should admit cleanup");
  }
  const auto cleaned = mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (static_cast<unsigned char>(cleaned.at(3 * kWidth + 3)) != 255U ||
      static_cast<unsigned char>(cleaned.at(8 * kWidth + 8)) != 0U ||
      mask.value(QStringLiteral("generatedMaskHolesFilled")).toInt() != 1 ||
      mask.value(QStringLiteral("generatedMaskIslandsRemoved")).toInt() != 1 ||
      mask.value(QStringLiteral("generatedMaskPixelsFilled")).toInt() != 1 ||
      mask.value(QStringLiteral("generatedMaskPixelsRemoved")).toInt() != 1) {
    return fail("cleanup should fill the enclosed hole and remove the isolated island exactly");
  }
  if (mask.value(QStringLiteral("generatedMaskHoleFillThreshold")).toDouble() !=
          kMaxGeneratedMaskCleanupThreshold ||
      mask.value(QStringLiteral("generatedMaskIslandRemovalThreshold")).toDouble() !=
          kMaxGeneratedMaskCleanupThreshold ||
      mask.value(QStringLiteral("generatedMaskCleanupSourceData")).toByteArray() != alpha ||
      mask.value(QStringLiteral("generatedMaskMetadata"))
              .toMap()
              .value(QStringLiteral("cleanupSchemaVersion"))
              .toInt() != 1 ||
      !generated_mask_cleanup_summary_for_mask(mask).contains(
          QStringLiteral("5.00%"))) {
    return fail("cleanup should clamp thresholds, preserve its source, and publish metadata and summary");
  }

  auto repeated = mask;
  if (!apply_generated_mask_cleanup(&repeated) ||
      repeated.value(QStringLiteral("segmentationData")).toByteArray() != cleaned) {
    return fail("cleanup should deterministically replay from the preserved source");
  }
  QVariantMap raster_mask{
      {QStringLiteral("kind"), QStringLiteral("alpha")},
      {QStringLiteral("segmentationData"), alpha},
      {QStringLiteral("segmentationWidth"), kWidth},
      {QStringLiteral("segmentationHeight"), kHeight},
  };
  if (apply_generated_mask_cleanup(&raster_mask) ||
      apply_generated_mask_cleanup(nullptr) ||
      !cleanup_generated_mask_components(QByteArray{}, 0, 0, 0.01, 0.01)
           .data
           .isEmpty()) {
    return fail("cleanup should fail closed for non-generated, null, and invalid payloads");
  }
  return EXIT_SUCCESS;
}

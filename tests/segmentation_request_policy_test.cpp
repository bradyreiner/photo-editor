#include "segmentation_request_policy.h"

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

  if (normalized_generated_mask_request_kind(QStringLiteral(" Person ")) !=
          QStringLiteral("people") ||
      normalized_generated_mask_request_kind(QStringLiteral("MOUNTAIN")) !=
          QStringLiteral("mountains") ||
      normalized_generated_mask_request_kind(QStringLiteral("facial_hair")) !=
          QStringLiteral("facial-hair")) {
    return fail("generated-mask request aliases must normalize deterministically");
  }
  if (!is_unsupported_landscape_semantic_mask_kind(QStringLiteral("mountain")) ||
      unsupported_landscape_semantic_label(QStringLiteral("mountain")) !=
          QStringLiteral("Mountains") ||
      is_unsupported_landscape_semantic_mask_kind(QStringLiteral("subject")) ||
      unsupported_landscape_semantic_label({}) != QStringLiteral("Landscape") ||
      !is_unsupported_people_part_semantic_mask_kind(QStringLiteral("facial hair")) ||
      unsupported_people_part_semantic_label(QStringLiteral("person")) !=
          QStringLiteral("Person") ||
      unsupported_people_part_semantic_label({}) !=
          QStringLiteral("People-part")) {
    return fail("unsupported semantic families and labels must share canonical aliases");
  }

  SegmentationResult result;
  if (segmentation_failure_reason_code(result, false) !=
          QStringLiteral("no-target") ||
      !actionable_segmentation_failure_status(
           QStringLiteral("Vision"), QStringLiteral("Object"), result, false)
           .contains(QStringLiteral("Click or drag"))) {
    return fail("missing prompts must produce actionable no-target guidance");
  }
  result.error_message = QStringLiteral("Invalid source decode");
  if (segmentation_failure_reason_code(result, true) !=
          QStringLiteral("decode-failed") ||
      !actionable_segmentation_failure_status(
           QStringLiteral("Vision"), QStringLiteral("Object"), result, true)
           .contains(QStringLiteral("Reload"))) {
    return fail("decode failures must produce the stable decode reason and recovery action");
  }

  result = {};
  result.width = 4;
  result.height = 4;
  result.mask_data = QByteArray(16, char(0));
  result.mask_data[5] = char(0xff);
  if (segmentation_failure_reason_code(result, true) !=
          QStringLiteral("insufficient-selection") ||
      !actionable_segmentation_failure_status(
           QStringLiteral("Vision"), QStringLiteral("Subject"), result, true)
           .contains(QStringLiteral("too small"))) {
    return fail("tiny selections must produce the stable enlargement guidance");
  }

  result = {};
  result.error_message = QStringLiteral("Model runtime not linked");
  if (segmentation_failure_reason_code(result, true) !=
          QStringLiteral("model-unavailable") ||
      !actionable_segmentation_failure_status(
           QStringLiteral("Vision"), QStringLiteral("Subject"), result, true)
           .contains(QStringLiteral("unavailable"))) {
    return fail("runtime failures must produce model diagnostics guidance");
  }
  result.error_message = QStringLiteral("No foreground detected");
  if (segmentation_failure_reason_code(result, true) !=
          QStringLiteral("no-target") ||
      !actionable_segmentation_failure_status(
           QStringLiteral("Vision"), QStringLiteral("Subject"), result, true)
           .contains(QStringLiteral("No object found"))) {
    return fail("empty model results must remain distinct from runtime failure");
  }
  result.error_message = QStringLiteral("unexpected failure");
  if (segmentation_failure_reason_code(result, true) !=
          QStringLiteral("unknown") ||
      !actionable_segmentation_failure_status(
           QStringLiteral("Vision"), QString{}, result, true)
           .contains(QStringLiteral("Vision failed for object"))) {
    return fail("unknown failures must preserve runtime, target, and detail context");
  }
  return EXIT_SUCCESS;
}

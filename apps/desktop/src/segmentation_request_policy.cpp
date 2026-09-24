#include "segmentation_request_policy.h"

#include "segmentation_result_schema.h"

namespace photo_editor::desktop {

QString normalized_generated_mask_request_kind(const QString& kind) {
  const auto normalized = kind.trimmed().toLower();
  if (normalized == QStringLiteral("person")) {
    return QStringLiteral("people");
  }
  if (normalized == QStringLiteral("mountain")) {
    return QStringLiteral("mountains");
  }
  if (normalized == QStringLiteral("facial hair") ||
      normalized == QStringLiteral("facial_hair")) {
    return QStringLiteral("facial-hair");
  }
  return normalized;
}

bool is_unsupported_landscape_semantic_mask_kind(const QString& kind) {
  const auto normalized = normalized_generated_mask_request_kind(kind);
  return normalized == QStringLiteral("water") ||
         normalized == QStringLiteral("vegetation") ||
         normalized == QStringLiteral("mountains") ||
         normalized == QStringLiteral("ground") ||
         normalized == QStringLiteral("architecture") ||
         normalized == QStringLiteral("snow");
}

QString unsupported_landscape_semantic_label(const QString& kind) {
  const auto normalized = normalized_generated_mask_request_kind(kind);
  if (normalized == QStringLiteral("water")) {
    return QStringLiteral("Water");
  }
  if (normalized == QStringLiteral("vegetation")) {
    return QStringLiteral("Vegetation");
  }
  if (normalized == QStringLiteral("mountains")) {
    return QStringLiteral("Mountains");
  }
  if (normalized == QStringLiteral("ground")) {
    return QStringLiteral("Ground");
  }
  if (normalized == QStringLiteral("architecture")) {
    return QStringLiteral("Architecture");
  }
  if (normalized == QStringLiteral("snow")) {
    return QStringLiteral("Snow");
  }
  return normalized.isEmpty() ? QStringLiteral("Landscape") : normalized;
}

bool is_unsupported_people_part_semantic_mask_kind(const QString& kind) {
  const auto normalized = normalized_generated_mask_request_kind(kind);
  return normalized == QStringLiteral("people") ||
         normalized == QStringLiteral("face") ||
         normalized == QStringLiteral("hair") ||
         normalized == QStringLiteral("clothing") ||
         normalized == QStringLiteral("facial-hair") ||
         normalized == QStringLiteral("lips") ||
         normalized == QStringLiteral("teeth") ||
         normalized == QStringLiteral("eyes") ||
         normalized == QStringLiteral("skin");
}

QString unsupported_people_part_semantic_label(const QString& kind) {
  const auto normalized = normalized_generated_mask_request_kind(kind);
  if (normalized == QStringLiteral("people")) {
    return QStringLiteral("Person");
  }
  if (normalized == QStringLiteral("face")) {
    return QStringLiteral("Face");
  }
  if (normalized == QStringLiteral("hair")) {
    return QStringLiteral("Hair");
  }
  if (normalized == QStringLiteral("clothing")) {
    return QStringLiteral("Clothing");
  }
  if (normalized == QStringLiteral("facial-hair")) {
    return QStringLiteral("Facial hair");
  }
  if (normalized == QStringLiteral("lips")) {
    return QStringLiteral("Lips");
  }
  if (normalized == QStringLiteral("teeth")) {
    return QStringLiteral("Teeth");
  }
  if (normalized == QStringLiteral("eyes")) {
    return QStringLiteral("Eyes");
  }
  if (normalized == QStringLiteral("skin")) {
    return QStringLiteral("Skin");
  }
  return normalized.isEmpty() ? QStringLiteral("People-part") : normalized;
}

QString actionable_segmentation_failure_status(
    const QString& runtime_label,
    const QString& target_label,
    const SegmentationResult& result,
    bool has_prompt) {
  const auto detail = result.error_message.trimmed();
  const auto lower_detail = detail.toLower();
  const auto label = target_label.trimmed().isEmpty()
                         ? QStringLiteral("object")
                         : target_label.trimmed().toLower();

  if (!has_prompt) {
    return QStringLiteral(
               "No object found. Click or drag across the target before creating a %1 selection.")
        .arg(label);
  }
  if (lower_detail.contains(QStringLiteral("invalid source")) ||
      lower_detail.contains(QStringLiteral("decode"))) {
    return QStringLiteral(
               "Image decode failed for %1. Reload the source image and try again.")
        .arg(label);
  }
  if (segmentation_result_is_too_small_to_use(result)) {
    return QStringLiteral(
               "Selection too small for %1. Drag across more of the target or use Brush Add.")
        .arg(label);
  }
  if (lower_detail.contains(QStringLiteral("not linked")) ||
      lower_detail.contains(QStringLiteral("runtime")) ||
      lower_detail.contains(QStringLiteral("model")) ||
      lower_detail.contains(QStringLiteral("requires macos")) ||
      lower_detail.contains(QStringLiteral("only available"))) {
    return detail.isEmpty()
               ? QStringLiteral(
                     "%1 is unavailable for %2. Check model diagnostics or use Brush.")
                     .arg(runtime_label, label)
               : QStringLiteral("%1 is unavailable for %2: %3")
                     .arg(runtime_label, label, detail);
  }
  if (lower_detail.contains(QStringLiteral("no selectable")) ||
      lower_detail.contains(QStringLiteral("not detected")) ||
      lower_detail.contains(QStringLiteral("no segmentation")) ||
      lower_detail.contains(QStringLiteral("no foreground"))) {
    return detail.isEmpty()
               ? QStringLiteral(
                     "No object found for %1. Try another target or paint the selection manually.")
                     .arg(label)
               : QStringLiteral("No object found for %1: %2").arg(label, detail);
  }
  if (!detail.isEmpty()) {
    return QStringLiteral("%1 failed for %2: %3")
        .arg(runtime_label, label, detail);
  }
  return QStringLiteral(
             "No object found for %1. Try another target or paint the selection manually.")
      .arg(label);
}

QString segmentation_failure_reason_code(
    const SegmentationResult& result,
    bool has_prompt) {
  const auto detail = result.error_message.trimmed().toLower();
  if (!has_prompt) {
    return QStringLiteral("no-target");
  }
  if (detail.contains(QStringLiteral("invalid source")) ||
      detail.contains(QStringLiteral("decode"))) {
    return QStringLiteral("decode-failed");
  }
  if (segmentation_result_is_too_small_to_use(result)) {
    return QStringLiteral("insufficient-selection");
  }
  if (detail.contains(QStringLiteral("not linked")) ||
      detail.contains(QStringLiteral("runtime")) ||
      detail.contains(QStringLiteral("model")) ||
      detail.contains(QStringLiteral("requires macos")) ||
      detail.contains(QStringLiteral("only available"))) {
    return QStringLiteral("model-unavailable");
  }
  if (detail.contains(QStringLiteral("no selectable")) ||
      detail.contains(QStringLiteral("not detected")) ||
      detail.contains(QStringLiteral("no segmentation")) ||
      detail.contains(QStringLiteral("no foreground"))) {
    return QStringLiteral("no-target");
  }
  return QStringLiteral("unknown");
}

}  // namespace photo_editor::desktop

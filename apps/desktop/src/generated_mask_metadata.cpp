#include "generated_mask_metadata.h"

#include "edit_recipe_mask_schema.h"

#include <cmath>

#include <QStringList>

namespace photo_editor::desktop {

QVariantMap make_generated_mask_metadata(
    const QString& kind,
    const GeneratedMaskMetadataFacts& facts) {
  const auto normalized_kind = normalized_mask_kind(kind);
  const auto runtime_label = facts.runtime_label.trimmed();
  QVariantMap metadata;
  metadata.insert(QStringLiteral("schemaVersion"), 1);
  metadata.insert(QStringLiteral("sourceSchemaVersion"), 1);
  metadata.insert(QStringLiteral("rasterSchemaVersion"), 1);
  metadata.insert(QStringLiteral("generatedOutputSchemaVersion"), 1);
  metadata.insert(QStringLiteral("kind"), normalized_kind);
  metadata.insert(
      QStringLiteral("promptType"),
      QStringLiteral("semantic:%1").arg(normalized_kind));
  metadata.insert(QStringLiteral("sourceWidth"), facts.source_size.width());
  metadata.insert(QStringLiteral("sourceHeight"), facts.source_size.height());
  metadata.insert(
      QStringLiteral("sourceResolution"),
      QStringLiteral("%1x%2")
          .arg(facts.source_size.width())
          .arg(facts.source_size.height()));
  metadata.insert(
      QStringLiteral("coordinateSpaceContract"),
      QStringLiteral("source-normalized-prompts-to-source-alpha-raster:v1"));
  metadata.insert(
      QStringLiteral("sourceCoordinateSpace"),
      QStringLiteral("source-image-pixels"));
  metadata.insert(
      QStringLiteral("promptCoordinateSpace"),
      QStringLiteral("source-normalized-0-1"));
  metadata.insert(
      QStringLiteral("rasterCoordinateSpace"),
      QStringLiteral("source-alpha-raster"));
  metadata.insert(QStringLiteral("cropX"), facts.crop_x);
  metadata.insert(QStringLiteral("cropY"), facts.crop_y);
  metadata.insert(QStringLiteral("cropW"), facts.crop_width);
  metadata.insert(QStringLiteral("cropH"), facts.crop_height);
  metadata.insert(QStringLiteral("cropZoom"), facts.crop_zoom);
  metadata.insert(QStringLiteral("cropPreset"), facts.crop_preset);
  metadata.insert(
      QStringLiteral("transformRotation"), facts.transform_rotation);
  metadata.insert(
      QStringLiteral("sourceImageVersion"), facts.source_image_version);
  metadata.insert(QStringLiteral("runtime"), runtime_label);
  metadata.insert(QStringLiteral("modelName"), runtime_label);
  metadata.insert(QStringLiteral("modelVersion"), QStringLiteral("semantic-mask-v1"));
  QVariantMap dependencies;
  dependencies.insert(
      QStringLiteral("sourceDimensions"),
      metadata.value(QStringLiteral("sourceResolution")));
  dependencies.insert(QStringLiteral("cropPreset"), facts.crop_preset);
  dependencies.insert(QStringLiteral("cropZoom"), facts.crop_zoom);
  dependencies.insert(
      QStringLiteral("transformRotation"), facts.transform_rotation);
  dependencies.insert(
      QStringLiteral("sourceImageVersion"), facts.source_image_version);
  dependencies.insert(QStringLiteral("runtime"), runtime_label);
  dependencies.insert(
      QStringLiteral("modelVersion"),
      metadata.value(QStringLiteral("modelVersion")));
  dependencies.insert(
      QStringLiteral("promptType"),
      metadata.value(QStringLiteral("promptType")));
  dependencies.insert(
      QStringLiteral("coordinateSpaceContract"),
      metadata.value(QStringLiteral("coordinateSpaceContract")));
  dependencies.insert(
      QStringLiteral("sourceCoordinateSpace"),
      metadata.value(QStringLiteral("sourceCoordinateSpace")));
  dependencies.insert(
      QStringLiteral("promptCoordinateSpace"),
      metadata.value(QStringLiteral("promptCoordinateSpace")));
  dependencies.insert(
      QStringLiteral("rasterCoordinateSpace"),
      metadata.value(QStringLiteral("rasterCoordinateSpace")));
  metadata.insert(QStringLiteral("dependencies"), dependencies);
  return metadata;
}

bool stamp_generated_mask_metadata(
    QVariantMap* mask,
    const GeneratedMaskMetadataFacts& facts) {
  if (mask == nullptr) {
    return false;
  }
  const auto kind = normalized_mask_kind(
      mask->value(QStringLiteral("kind")).toString());
  if (!is_ai_segmentation_mask_kind(kind)) {
    return false;
  }
  mask->insert(
      QStringLiteral("generatedMaskMetadata"),
      make_generated_mask_metadata(kind, facts));
  mask->insert(QStringLiteral("generatedMaskStale"), false);
  return true;
}

bool generated_mask_metadata_is_stale(
    const QVariantMap& mask,
    const QVariantMap& current_metadata) {
  const auto kind = normalized_mask_kind(
      mask.value(QStringLiteral("kind")).toString());
  if (!is_ai_segmentation_mask_kind(kind)) {
    return false;
  }
  if (kind == QStringLiteral("object") &&
      !mask.contains(QStringLiteral("generatedMaskMetadata"))) {
    return false;
  }
  const auto stored =
      mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  const QStringList required_keys{
      QStringLiteral("schemaVersion"),
      QStringLiteral("sourceSchemaVersion"),
      QStringLiteral("rasterSchemaVersion"),
      QStringLiteral("generatedOutputSchemaVersion"),
      QStringLiteral("kind"),
      QStringLiteral("promptType"),
      QStringLiteral("sourceWidth"),
      QStringLiteral("sourceHeight"),
      QStringLiteral("sourceResolution"),
      QStringLiteral("coordinateSpaceContract"),
      QStringLiteral("sourceCoordinateSpace"),
      QStringLiteral("promptCoordinateSpace"),
      QStringLiteral("rasterCoordinateSpace"),
      QStringLiteral("cropX"),
      QStringLiteral("cropY"),
      QStringLiteral("cropW"),
      QStringLiteral("cropH"),
      QStringLiteral("cropZoom"),
      QStringLiteral("cropPreset"),
      QStringLiteral("transformRotation"),
      QStringLiteral("sourceImageVersion"),
      QStringLiteral("runtime"),
      QStringLiteral("modelName"),
      QStringLiteral("modelVersion"),
      QStringLiteral("dependencies"),
  };
  for (const auto& key : required_keys) {
    if (!stored.contains(key)) {
      return true;
    }
  }
  for (const auto& key :
       {QStringLiteral("sourceWidth"), QStringLiteral("sourceHeight")}) {
    if (stored.value(key).toInt() != current_metadata.value(key).toInt()) {
      return true;
    }
  }
  if (stored.value(QStringLiteral("sourceImageVersion")).toString() !=
      current_metadata.value(QStringLiteral("sourceImageVersion")).toString()) {
    return true;
  }
  for (const auto& key :
       {QStringLiteral("cropX"), QStringLiteral("cropY"),
        QStringLiteral("cropW"), QStringLiteral("cropH"),
        QStringLiteral("cropZoom"), QStringLiteral("transformRotation")}) {
    if (std::abs(stored.value(key).toDouble() -
                 current_metadata.value(key).toDouble()) > 1e-6) {
      return true;
    }
  }
  for (const auto& key :
       {QStringLiteral("kind"), QStringLiteral("promptType"),
        QStringLiteral("sourceResolution"), QStringLiteral("cropPreset"),
        QStringLiteral("runtime"), QStringLiteral("modelName"),
        QStringLiteral("modelVersion")}) {
    if (stored.value(key).toString() != current_metadata.value(key).toString()) {
      return true;
    }
  }
  return mask.value(QStringLiteral("generatedMaskStale"), false).toBool();
}

}  // namespace photo_editor::desktop

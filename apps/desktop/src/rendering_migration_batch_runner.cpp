#include "rendering_migration_batch_runner.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

namespace photo_editor::desktop {

RenderingMigrationBatchRunResult
run_rendering_migration_batch(
    const QJsonObject& inventory,
    const QString& report_path,
    const RenderingMigrationAssetAssessor& assessor,
    RenderingMigrationBatchThresholds thresholds,
    const RenderingMigrationBatchProducer& producer,
    const QSet<QString>& selected_asset_ids) {
  RenderingMigrationBatchRunResult result;
  if (inventory.value(QStringLiteral("schema"))
          .toString() !=
      QStringLiteral(
          "photo-editor-rendering-migration-inventory:v1")) {
    result.error =
        QStringLiteral("migration inventory schema is invalid");
    return result;
  }
  if (!assessor) {
    result.error =
        QStringLiteral("migration asset assessor is required");
    return result;
  }
  const auto inventory_assets =
      inventory.value(QStringLiteral("assets")).toArray();
  if (inventory
          .value(QStringLiteral("edited_asset_count"))
          .toInteger() != inventory_assets.size()) {
    result.error =
        QStringLiteral("migration inventory count is inconsistent");
    return result;
  }

  std::vector<RenderingMigrationBatchAsset> assessments;
  assessments.reserve(
      static_cast<std::size_t>(
          selected_asset_ids.isEmpty()
              ? inventory_assets.size()
              : selected_asset_ids.size()));
  QSet<QString> seen_asset_ids;
  for (const auto& value : inventory_assets) {
    if (!value.isObject()) {
      result.error =
          QStringLiteral("migration inventory asset is invalid");
      return result;
    }
    const auto inventory_asset =
        value.toObject().toVariantMap();
    const auto asset_id =
        inventory_asset
            .value(QStringLiteral("asset_id"))
            .toString()
            .trimmed();
    if (asset_id.isEmpty() ||
        seen_asset_ids.contains(asset_id)) {
      result.error =
          QStringLiteral(
              "migration inventory contains an invalid or duplicate asset id");
      return result;
    }
    seen_asset_ids.insert(asset_id);
    if (!selected_asset_ids.isEmpty() &&
        !selected_asset_ids.contains(asset_id)) {
      continue;
    }
    const auto source_path =
        inventory_asset
            .value(QStringLiteral("source_path"))
            .toString();
    const auto source_available =
        inventory_asset
            .value(QStringLiteral("source_available"))
            .toBool() &&
        QFileInfo::exists(source_path);
    RenderingMigrationBatchAsset assessment;
    if (source_available) {
      assessment = assessor(inventory_asset);
      if (assessment.asset_id.trimmed() != asset_id) {
        result.error = QStringLiteral(
                           "migration assessor returned the wrong asset identity for %1")
                           .arg(asset_id);
        return result;
      }
    } else {
      assessment.asset_id = asset_id;
      assessment.source_path = source_path;
      assessment.edited = true;
      assessment.original_available = false;
      assessment.candidate_error =
          QStringLiteral("original source is unavailable");
    }
    assessment.edited = true;
    assessment.original_available = source_available;
    assessment.source_path = source_path;
    assessment.migration_required =
        inventory_asset
            .value(QStringLiteral("source_renderer"))
            .toString() != QStringLiteral("aces2-ocio-v1");
    assessments.push_back(std::move(assessment));
  }
  if (!selected_asset_ids.isEmpty()) {
    auto missing = selected_asset_ids;
    missing.subtract(seen_asset_ids);
    if (!missing.isEmpty()) {
      auto missing_ids = missing.values();
      missing_ids.sort();
      result.error = QStringLiteral(
                         "migration authorization contains assets absent from the catalog: %1")
                         .arg(missing_ids.join(QStringLiteral(",")));
      return result;
    }
  }

  result.report =
      build_rendering_migration_batch_report(
          assessments, thresholds, producer);
  if (!result.report.valid) {
    result.error = result.report.error;
    return result;
  }
  if (!report_path.trimmed().isEmpty() &&
      !write_immutable_rendering_migration_batch_report(
          report_path,
          result.report.document,
          &result.error)) {
    return result;
  }
  result.valid = true;
  return result;
}

}  // namespace photo_editor::desktop

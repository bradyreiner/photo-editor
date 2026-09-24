#pragma once

#include "rendering_migration_batch_report.h"

#include <QJsonObject>
#include <QSet>

#include <functional>

namespace photo_editor::desktop {

using RenderingMigrationAssetAssessor =
    std::function<RenderingMigrationBatchAsset(
        const QVariantMap& inventory_asset)>;

struct RenderingMigrationBatchRunResult final {
  bool valid = false;
  QString error;
  RenderingMigrationBatchReport report;
};

[[nodiscard]] RenderingMigrationBatchRunResult
run_rendering_migration_batch(
    const QJsonObject& inventory,
    const QString& report_path,
    const RenderingMigrationAssetAssessor& assessor,
    RenderingMigrationBatchThresholds thresholds,
    const RenderingMigrationBatchProducer& producer,
    const QSet<QString>& selected_asset_ids = {});

}  // namespace photo_editor::desktop

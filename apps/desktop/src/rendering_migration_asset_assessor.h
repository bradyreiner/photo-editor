#pragma once

#include "rendering_migration_batch_report.h"

#include <QVariantMap>

namespace photo_editor::desktop {

[[nodiscard]] RenderingMigrationBatchAsset
assess_rendering_migration_inventory_asset(
    const QVariantMap& inventory_asset,
    const QString& recipe_asset_root,
    int maximum_decode_edge = 0,
    const QString& evidence_artifact_root = {});

}  // namespace photo_editor::desktop

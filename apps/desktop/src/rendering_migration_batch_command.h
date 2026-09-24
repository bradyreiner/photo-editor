#pragma once

#include "rendering_migration_batch_report.h"

#include <QString>
#include <QStringList>

namespace photo_editor::desktop {

struct RenderingMigrationBatchCommandResult final {
  bool valid = false;
  QString error;
  QString inventory_program;
  QString evidence_artifact_root;
  QString report_file_sha256;
  RenderingMigrationBatchReport report;
};

[[nodiscard]] RenderingMigrationBatchCommandResult
run_packaged_rendering_migration_batch_command(
    const QString& report_path,
    const QString& recipe_asset_root,
    int inventory_timeout_ms = 120000,
    bool plan_only = false,
    const QString& chunk_authorization_path = {});

[[nodiscard]] RenderingMigrationBatchCommandResult
run_packaged_rendering_migration_report_merge_command(
    const QString& report_path,
    const QStringList& component_report_paths,
    const QStringList& component_receipt_paths);

}  // namespace photo_editor::desktop

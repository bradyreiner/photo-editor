#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace photo_editor::desktop {

struct RenderingMigrationReceiptValidationResult final {
  bool valid = false;
  QString error;
  QString plan_sha256;
  QString plan_file_sha256;
};

struct RenderingMigrationChunkAuthorizationResult final {
  bool valid = false;
  QString error;
  QString plan_sha256;
  QString plan_file_sha256;
  QStringList asset_ids;
  QVariantMap producer;
};

[[nodiscard]] RenderingMigrationChunkAuthorizationResult
validate_rendering_migration_chunk_authorization(
    const QString& authorization_path,
    const QString& executable_sha256);

[[nodiscard]] RenderingMigrationReceiptValidationResult
validate_rendering_migration_component_receipts(
    const QStringList& component_report_paths,
    const QStringList& component_receipt_paths);

}  // namespace photo_editor::desktop

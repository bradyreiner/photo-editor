#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <vector>

namespace photo_editor::desktop {

struct RenderingMigrationBatchAsset final {
  QString asset_id;
  QString source_path;
  bool edited = false;
  bool original_available = false;
  bool migration_required = true;
  bool candidate_compiled = false;
  QString candidate_error;
  QStringList unsupported_profile_identities;
  QStringList preset_identities;
  QStringList evidence_artifact_file_names;
  QVariantMap evidence;
};

struct RenderingMigrationBatchThresholds final {
  double p95_delta_e00 = 2.0;
  double maximum_delta_e00 = 10.0;
  int maximum_added_shadow_clips = 0;
  int maximum_added_highlight_clips = 0;
};

struct RenderingMigrationBatchProducer final {
  QString executable_sha256;
  QString executable_name;
  QString build_mode;
};

struct RenderingMigrationBatchReport final {
  bool valid = false;
  QString error;
  QString report_hash;
  QVariantMap document;
};

[[nodiscard]] RenderingMigrationBatchReport
build_rendering_migration_batch_report(
    const std::vector<RenderingMigrationBatchAsset>& assets,
    RenderingMigrationBatchThresholds thresholds,
    const RenderingMigrationBatchProducer& producer);

[[nodiscard]] QByteArray
canonical_rendering_migration_batch_report_payload(
    const QVariantMap& report);

[[nodiscard]] bool
write_immutable_rendering_migration_batch_report(
    const QString& file_path,
    const QVariantMap& report,
    QString* error = nullptr);

[[nodiscard]] RenderingMigrationBatchReport
merge_rendering_migration_batch_reports(
    const QVariantMap& base_report,
    const std::vector<QVariantMap>& overlay_reports);

}  // namespace photo_editor::desktop

#pragma once

#include <QByteArray>
#include <QImage>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace photo_editor::desktop {

struct RenderingMigrationEvidence final {
  bool valid = false;
  QString error;
  QString evidence_hash;
  QVariantMap metrics;
  QByteArray candidate_export_png;
  QByteArray reference_export_png;
  QByteArray delta_e00_heatmap_png;
  QMap<QString, QByteArray>
      candidate_shipping_format_bytes;
  QMap<QString, QByteArray>
      reference_shipping_format_bytes;
  QMap<QString, QByteArray> candidate_target_png_bytes;
  QMap<QString, QByteArray> reference_target_png_bytes;
  QMap<QString, QByteArray> candidate_attribution_png_bytes;
  QMap<QString, QByteArray> reference_attribution_png_bytes;
};

[[nodiscard]] RenderingMigrationEvidence build_rendering_migration_evidence(
    const QImage& candidate,
    const QImage& reference,
    const QString& candidate_model,
    const QString& reference_model,
    const QString& plan_cache_key,
    const QString& source_adjustment_payload_json);

[[nodiscard]] QByteArray canonical_rendering_migration_evidence_payload(
    const QVariantMap& evidence);

[[nodiscard]] bool bind_rendering_migration_evidence_context(
    RenderingMigrationEvidence* evidence,
    const QVariantMap& context,
    QString* error = nullptr);

[[nodiscard]] bool add_rendering_migration_target_comparison(
    RenderingMigrationEvidence* evidence,
    const QString& target_identity,
    const QString& output_color_space,
    const QString& plan_cache_key,
    const QImage& candidate,
    const QImage& reference,
    QString* error = nullptr);

[[nodiscard]] bool add_rendering_migration_hdr_avif_comparison(
    RenderingMigrationEvidence* evidence,
    const QString& target_identity,
    const QString& plan_cache_key,
    const QString& hdr_export_preset,
    const QImage& candidate,
    const QImage& reference,
    QString* error = nullptr);

[[nodiscard]] bool add_rendering_migration_structure_attribution(
    RenderingMigrationEvidence* evidence,
    const QString& structure_identity,
    int removed_count,
    const QString& plan_cache_key,
    const QImage& candidate,
    const QImage& reference,
    QString* error = nullptr);

[[nodiscard]] bool add_unavailable_rendering_migration_structure_attribution(
    RenderingMigrationEvidence* evidence,
    const QString& structure_identity,
    int structure_count,
    const QString& reason,
    QString* error = nullptr);

[[nodiscard]] bool
write_immutable_rendering_migration_evidence_artifacts(
    const QString& directory_path,
    const RenderingMigrationEvidence& evidence,
    QStringList* artifact_file_names = nullptr,
    QString* error = nullptr);

}  // namespace photo_editor::desktop

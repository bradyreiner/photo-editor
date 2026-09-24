#include <cstdlib>
#include <iostream>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "rendering_migration_batch_report.h"
#include "rendering_migration_batch_runner.h"
#include "rendering_migration_chunk_receipt.h"
#include "rendering_migration_evidence.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QString sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash digest(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    digest.addData(file.read(1024 * 1024));
  }
  return QString::fromLatin1(digest.result().toHex());
}

bool write_json(const QString& path, const QVariantMap& value) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
         file.write(QJsonDocument::fromVariant(value).toJson()) > 0;
}

QVariantMap evidence_for_colors(
    const QColor& candidate_color,
    const QColor& reference_color,
    const QString& asset_id) {
  QImage candidate(16, 12, QImage::Format_ARGB32);
  candidate.fill(candidate_color);
  QImage reference(16, 12, QImage::Format_ARGB32);
  reference.fill(reference_color);
  auto evidence =
      photo_editor::desktop::
          build_rendering_migration_evidence(
              candidate,
              reference,
              QStringLiteral("aces2-ocio-v1"),
              QStringLiteral("legacy-oklch-v1"),
              QStringLiteral("plan-") + asset_id,
              QStringLiteral("patch-") + asset_id);
  if (asset_id == QStringLiteral("risky") &&
      !photo_editor::desktop::
           add_rendering_migration_target_comparison(
               &evidence,
               QStringLiteral("display-p3-simulation"),
               QStringLiteral("Display P3"),
               QStringLiteral("p3-plan-risky"),
               candidate,
               reference)) {
    return {};
  }
  if (asset_id == QStringLiteral("risky") &&
      !photo_editor::desktop::
           add_rendering_migration_hdr_avif_comparison(
               &evidence,
               QStringLiteral("rec2100-pq"),
               QStringLiteral("pq-plan-risky"),
               QStringLiteral("HDR PQ AVIF"),
               candidate,
               reference)) {
    return {};
  }
  if (asset_id == QStringLiteral("risky") &&
      !photo_editor::desktop::
           add_unavailable_rendering_migration_structure_attribution(
               &evidence,
               QStringLiteral("presets"),
               1,
               QStringLiteral(
                   "legacy preset provenance is not reversible"))) {
    return {};
  }
  if (asset_id == QStringLiteral("risky") &&
      !photo_editor::desktop::
           bind_rendering_migration_evidence_context(
               &evidence,
               QVariantMap{
                   {QStringLiteral("maskCount"), 2},
                   {QStringLiteral("layerCount"), 1},
                   {QStringLiteral("softProofEnabled"), true},
                   {QStringLiteral("profileIdentities"),
                    QStringList{
                        QStringLiteral("proof-icc:profile")}},
               })) {
    return {};
  }
  return evidence.metrics;
}

}  // namespace

int main() {
  using photo_editor::desktop::
      RenderingMigrationBatchAsset;
  const auto producer =
      photo_editor::desktop::RenderingMigrationBatchProducer{
          .executable_sha256 = QString(64, QLatin1Char('a')),
          .executable_name = QStringLiteral("migration-report-test"),
          .build_mode = QStringLiteral("test"),
      };

  const auto ready_evidence = evidence_for_colors(
      QColor(96, 112, 128),
      QColor(96, 112, 128),
      QStringLiteral("ready"));
  const auto risky_evidence = evidence_for_colors(
      QColor(255, 255, 255),
      QColor(32, 48, 64),
      QStringLiteral("risky"));
  auto tampered_evidence = ready_evidence;
  tampered_evidence.insert(
      QStringLiteral("p95DeltaE00"), 99.0);

  const std::vector<RenderingMigrationBatchAsset> assets{
      RenderingMigrationBatchAsset{
          .asset_id = QStringLiteral("ready"),
          .source_path = QStringLiteral("/photos/ready.dng"),
          .edited = true,
          .original_available = true,
          .candidate_compiled = true,
          .evidence = ready_evidence,
      },
      RenderingMigrationBatchAsset{
          .asset_id = QStringLiteral("missing"),
          .source_path = QString{},
          .edited = true,
          .original_available = false,
          .candidate_compiled = false,
          .candidate_error = QStringLiteral("source unavailable"),
      },
      RenderingMigrationBatchAsset{
          .asset_id = QStringLiteral("profile"),
          .source_path = QStringLiteral("/photos/profile.dng"),
          .edited = true,
          .original_available = true,
          .candidate_compiled = false,
          .candidate_error = QStringLiteral("profile unsupported"),
          .unsupported_profile_identities =
              {QStringLiteral("sha256:unsupported")},
      },
      RenderingMigrationBatchAsset{
          .asset_id = QStringLiteral("risky"),
          .source_path = QStringLiteral("/photos/risky.dng"),
          .edited = true,
          .original_available = true,
          .candidate_compiled = true,
          .preset_identities =
              {QStringLiteral("preset:portrait")},
          .evidence = risky_evidence,
      },
      RenderingMigrationBatchAsset{
          .asset_id = QStringLiteral("tampered"),
          .source_path = QStringLiteral("/photos/tampered.dng"),
          .edited = true,
          .original_available = true,
          .candidate_compiled = true,
          .evidence = tampered_evidence,
      },
      RenderingMigrationBatchAsset{
          .asset_id = QStringLiteral("unedited"),
          .source_path = QStringLiteral("/photos/unedited.dng"),
          .original_available = true,
      },
  };

  const auto report =
      photo_editor::desktop::
          build_rendering_migration_batch_report(
              assets, {}, producer);
  if (!report.valid || report.report_hash.size() != 64) {
    return fail("batch migration report should be valid and identity-bound");
  }
  const auto summary =
      report.document.value(QStringLiteral("summary"))
          .toMap();
  if (summary.value(QStringLiteral("assetCount"))
              .toULongLong() != 6 ||
      summary.value(QStringLiteral("editedAssetCount"))
              .toInt() != 5 ||
      summary.value(QStringLiteral("candidateCompiledCount"))
              .toInt() != 3 ||
      summary.value(QStringLiteral("evidenceReadyCount"))
              .toInt() != 2 ||
      summary.value(QStringLiteral("readyCount"))
              .toInt() != 1 ||
      summary.value(
                 QStringLiteral(
                     "allEditedAssetsCandidateCompiled"))
          .toBool() ||
      summary.value(
                 QStringLiteral(
                     "allEditedAssetsHaveEvidence"))
          .toBool()) {
    return fail("batch migration report should summarize every edited asset");
  }
  if (!summary
           .value(QStringLiteral("missingOriginalAssetIds"))
           .toStringList()
           .contains(QStringLiteral("missing")) ||
      !summary
           .value(QStringLiteral("unsupportedProfileAssetIds"))
           .toStringList()
           .contains(QStringLiteral("profile")) ||
      !summary
           .value(QStringLiteral("candidateFailureAssetIds"))
           .toStringList()
           .contains(QStringLiteral("profile")) ||
      !summary
           .value(QStringLiteral("missingEvidenceAssetIds"))
           .toStringList()
           .contains(QStringLiteral("tampered")) ||
      !summary
           .value(QStringLiteral("highDifferenceAssetIds"))
           .toStringList()
           .contains(QStringLiteral("risky")) ||
      !summary
           .value(QStringLiteral("presetsNeedingAttention"))
           .toStringList()
           .contains(QStringLiteral("preset:portrait")) ||
      !summary
           .value(QStringLiteral("maskLayerReviewAssetIds"))
           .toStringList()
           .contains(QStringLiteral("risky")) ||
      !summary
           .value(QStringLiteral("colorTargetReviewAssetIds"))
           .toStringList()
           .contains(QStringLiteral("risky")) ||
      !summary
           .value(QStringLiteral("targetDifferenceRiskAssetIds"))
           .toStringList()
           .contains(QStringLiteral("risky")) ||
      !summary
           .value(QStringLiteral(
               "hdrPerceptualApprovalRequiredAssetIds"))
           .toStringList()
           .contains(QStringLiteral("risky")) ||
      !summary
           .value(QStringLiteral(
               "structureAttributionUnavailableAssetIds"))
           .toStringList()
           .contains(QStringLiteral("risky"))) {
    return fail("batch migration report should classify migration blockers and risks");
  }

  const auto payload =
      photo_editor::desktop::
          canonical_rendering_migration_batch_report_payload(
              report.document);
  const auto payload_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          payload,
          QCryptographicHash::Sha256)
          .toHex());
  if (payload_hash != report.report_hash ||
      photo_editor::desktop::
              build_rendering_migration_batch_report(
                  assets, {}, producer)
              .report_hash != report.report_hash) {
    return fail("batch migration report identity must be canonical and deterministic");
  }

  const auto merge_base =
      photo_editor::desktop::build_rendering_migration_batch_report(
          {
              RenderingMigrationBatchAsset{
                  .asset_id = QStringLiteral("recover"),
                  .source_path = QStringLiteral("/photos/recover.dng"),
                  .edited = true,
                  .original_available = true,
                  .candidate_error = QStringLiteral("legacy structure"),
              },
              RenderingMigrationBatchAsset{
                  .asset_id = QStringLiteral("stable"),
                  .source_path = QStringLiteral("/photos/stable.dng"),
                  .edited = true,
                  .original_available = true,
                  .candidate_compiled = true,
                  .evidence = ready_evidence,
              },
          }, {}, producer);
  const auto merge_overlay =
      photo_editor::desktop::build_rendering_migration_batch_report(
          {RenderingMigrationBatchAsset{
              .asset_id = QStringLiteral("recover"),
              .source_path = QStringLiteral("/photos/recover.dng"),
              .edited = true,
              .original_available = true,
              .candidate_compiled = true,
              .evidence = ready_evidence,
          }}, {}, producer);
  const auto merged =
      photo_editor::desktop::merge_rendering_migration_batch_reports(
          merge_base.document,
          {merge_overlay.document});
  const auto merged_summary =
      merged.document.value(QStringLiteral("summary")).toMap();
  if (!merged.valid || merged.report_hash.size() != 64 ||
      merged.document
              .value(QStringLiteral("componentReportSha256s"))
              .toStringList()
              .size() != 2 ||
      merged_summary
              .value(QStringLiteral("candidateCompiledCount"))
              .toInt() != 2 ||
      merged_summary
              .value(QStringLiteral("evidenceReadyCount"))
              .toInt() != 2 ||
      !merged_summary
           .value(QStringLiteral("candidateFailureAssetIds"))
           .toStringList()
           .isEmpty()) {
    return fail(
        "identity-verified report overlays must replace only blocked assets and rebuild the summary");
  }
  auto tampered_overlay = merge_overlay.document;
  tampered_overlay.insert(
      QStringLiteral("reportSha256"),
      QString(64, QLatin1Char('0')));
  if (photo_editor::desktop::merge_rendering_migration_batch_reports(
          merge_base.document,
          {tampered_overlay})
          .valid) {
    return fail("report merge must reject a tampered overlay identity");
  }
  const auto different_producer_overlay =
      photo_editor::desktop::build_rendering_migration_batch_report(
          {RenderingMigrationBatchAsset{
              .asset_id = QStringLiteral("recover"),
              .source_path = QStringLiteral("/photos/recover.dng"),
              .edited = true,
              .original_available = true,
              .candidate_compiled = true,
              .evidence = ready_evidence,
          }},
          {},
          photo_editor::desktop::RenderingMigrationBatchProducer{
              .executable_sha256 = QString(64, QLatin1Char('b')),
              .executable_name = QStringLiteral("other-binary"),
              .build_mode = QStringLiteral("release"),
          });
  if (photo_editor::desktop::merge_rendering_migration_batch_reports(
          merge_base.document,
          {different_producer_overlay.document})
          .valid) {
    return fail(
        "report merge must reject components from different executables");
  }
  auto changed_source_overlay = merge_overlay.document;
  auto changed_source_assets =
      changed_source_overlay.value(QStringLiteral("assets")).toList();
  auto changed_source_asset = changed_source_assets.front().toMap();
  changed_source_asset.insert(
      QStringLiteral("sourcePath"),
      QStringLiteral("/photos/different.dng"));
  changed_source_assets.front() = changed_source_asset;
  changed_source_overlay.insert(
      QStringLiteral("assets"), changed_source_assets);
  changed_source_overlay.remove(QStringLiteral("reportSha256"));
  changed_source_overlay.insert(
      QStringLiteral("reportSha256"),
      QString::fromLatin1(
          QCryptographicHash::hash(
              photo_editor::desktop::
                  canonical_rendering_migration_batch_report_payload(
                      changed_source_overlay),
              QCryptographicHash::Sha256)
              .toHex()));
  if (photo_editor::desktop::merge_rendering_migration_batch_reports(
          merge_base.document,
          {changed_source_overlay})
          .valid) {
    return fail("report merge must reject changed source identity");
  }

  QTemporaryDir receipt_root;
  if (!receipt_root.isValid()) {
    return fail("batch migration receipt test needs temporary storage");
  }
  const auto base_report_path =
      receipt_root.filePath(QStringLiteral("base.json"));
  const auto overlay_report_path =
      receipt_root.filePath(QStringLiteral("overlay.json"));
  QString receipt_error;
  if (!photo_editor::desktop::write_immutable_rendering_migration_batch_report(
          base_report_path, merge_base.document, &receipt_error) ||
      !photo_editor::desktop::write_immutable_rendering_migration_batch_report(
          overlay_report_path, merge_overlay.document, &receipt_error)) {
    return fail("receipt test reports must be written immutably");
  }
  QVariantMap receipt{
      {QStringLiteral("contract"),
       QStringLiteral(
           "photo-editor-rendering-migration-refresh-chunk-receipt:v1")},
      {QStringLiteral("status"), QStringLiteral("validated-evidence")},
      {QStringLiteral("producer"),
       merge_overlay.document.value(QStringLiteral("producer"))},
      {QStringLiteral("planSha256"), QString(64, QLatin1Char('c'))},
      {QStringLiteral("planFileSha256"), QString(64, QLatin1Char('d'))},
      {QStringLiteral("chunkIndex"), 1},
      {QStringLiteral("assetIds"), QStringList{QStringLiteral("recover")}},
      {QStringLiteral("componentReportSha256"), merge_overlay.report_hash},
      {QStringLiteral("componentReportFileSha256"),
       sha256_file(overlay_report_path)},
      {QStringLiteral("artifactCount"), 3},
  };
  const auto seal_receipt = [](QVariantMap value) {
    value.remove(QStringLiteral("receiptSha256"));
    value.insert(
        QStringLiteral("receiptSha256"),
        QString::fromLatin1(
            QCryptographicHash::hash(
                QJsonDocument::fromVariant(value)
                    .toJson(QJsonDocument::Compact),
                QCryptographicHash::Sha256)
                .toHex()));
    return value;
  };
  const QVariantMap python_canonical_receipt_fixture{
      {QStringLiteral("artifactCount"), 3},
      {QStringLiteral("assetIds"),
       QStringList{QStringLiteral("asset-b"), QStringLiteral("asset-a")}},
      {QStringLiteral("chunkIndex"), 7},
      {QStringLiteral("componentReportFileSha256"),
       QString(64, QLatin1Char('1'))},
      {QStringLiteral("componentReportSha256"),
       QString(64, QLatin1Char('2'))},
      {QStringLiteral("contract"),
       QStringLiteral(
           "photo-editor-rendering-migration-refresh-chunk-receipt:v1")},
      {QStringLiteral("planFileSha256"), QString(64, QLatin1Char('3'))},
      {QStringLiteral("planSha256"), QString(64, QLatin1Char('4'))},
      {QStringLiteral("producer"),
       QVariantMap{
           {QStringLiteral("buildMode"), QStringLiteral("release")},
           {QStringLiteral("contract"),
            QStringLiteral(
                "photo-editor-rendering-migration-batch-producer:v1")},
           {QStringLiteral("executableName"), QStringLiteral("Looksy")},
           {QStringLiteral("executableSha256"),
            QString(64, QLatin1Char('5'))},
       }},
      {QStringLiteral("status"), QStringLiteral("validated-evidence")},
  };
  if (QString::fromLatin1(
          QCryptographicHash::hash(
              QJsonDocument::fromVariant(python_canonical_receipt_fixture)
                  .toJson(QJsonDocument::Compact),
              QCryptographicHash::Sha256)
              .toHex()) !=
      QStringLiteral(
          "f758d365052aeb654a5bb7964d917a68b29117dd456f418d018de57070a59e0e")) {
    return fail("Python and Qt receipt canonical identities must match");
  }
  receipt = seal_receipt(receipt);
  const auto receipt_path =
      receipt_root.filePath(QStringLiteral("overlay-receipt.json"));
  if (!write_json(receipt_path, receipt)) {
    return fail("component receipt fixture must be written");
  }
  const auto valid_receipts = photo_editor::desktop::
      validate_rendering_migration_component_receipts(
          {base_report_path, overlay_report_path}, {receipt_path});
  if (!valid_receipts.valid ||
      valid_receipts.plan_sha256 != QString(64, QLatin1Char('c')) ||
      valid_receipts.plan_file_sha256 != QString(64, QLatin1Char('d'))) {
    return fail("merge receipt must bind the exact plan and overlay report");
  }
  if (photo_editor::desktop::
          validate_rendering_migration_component_receipts(
              {base_report_path, overlay_report_path}, {})
          .valid) {
    return fail("merge must reject a missing component receipt");
  }
  auto wrong_report_receipt = receipt;
  wrong_report_receipt.insert(
      QStringLiteral("componentReportFileSha256"),
      QString(64, QLatin1Char('e')));
  wrong_report_receipt = seal_receipt(wrong_report_receipt);
  const auto wrong_receipt_path =
      receipt_root.filePath(QStringLiteral("wrong-receipt.json"));
  if (!write_json(wrong_receipt_path, wrong_report_receipt) ||
      photo_editor::desktop::
          validate_rendering_migration_component_receipts(
              {base_report_path, overlay_report_path}, {wrong_receipt_path})
          .valid) {
    return fail("merge must reject a receipt for a different component file");
  }
  QVariantMap authorization{
      {QStringLiteral("contract"),
       QStringLiteral(
           "photo-editor-rendering-migration-refresh-chunk-authorization:v1")},
      {QStringLiteral("status"), QStringLiteral("authorized")},
      {QStringLiteral("producer"),
       merge_overlay.document.value(QStringLiteral("producer"))},
      {QStringLiteral("planSha256"), QString(64, QLatin1Char('c'))},
      {QStringLiteral("planFileSha256"), QString(64, QLatin1Char('d'))},
      {QStringLiteral("chunkIndex"), 1},
      {QStringLiteral("assetIds"), QStringList{QStringLiteral("recover")}},
      {QStringLiteral("assetCount"), 1},
      {QStringLiteral("totalPixels"), 20000000},
      {QStringLiteral("overBudgetSingleAsset"), true},
  };
  const auto seal_authorization = [](QVariantMap value) {
    value.remove(QStringLiteral("authorizationSha256"));
    value.insert(
        QStringLiteral("authorizationSha256"),
        QString::fromLatin1(
            QCryptographicHash::hash(
                QJsonDocument::fromVariant(value)
                    .toJson(QJsonDocument::Compact),
                QCryptographicHash::Sha256)
                .toHex()));
    return value;
  };
  const QVariantMap python_canonical_authorization_fixture{
      {QStringLiteral("assetCount"), 1},
      {QStringLiteral("assetIds"), QStringList{QStringLiteral("asset-a")}},
      {QStringLiteral("chunkIndex"), 1},
      {QStringLiteral("contract"),
       QStringLiteral(
           "photo-editor-rendering-migration-refresh-chunk-authorization:v1")},
      {QStringLiteral("overBudgetSingleAsset"), true},
      {QStringLiteral("planFileSha256"), QString(64, QLatin1Char('1'))},
      {QStringLiteral("planSha256"), QString(64, QLatin1Char('2'))},
      {QStringLiteral("producer"),
       QVariantMap{
           {QStringLiteral("buildMode"), QStringLiteral("release")},
           {QStringLiteral("contract"),
            QStringLiteral(
                "photo-editor-rendering-migration-batch-producer:v1")},
           {QStringLiteral("executableName"), QStringLiteral("Looksy")},
           {QStringLiteral("executableSha256"),
            QString(64, QLatin1Char('3'))},
       }},
      {QStringLiteral("status"), QStringLiteral("authorized")},
      {QStringLiteral("totalPixels"), 20000000},
  };
  if (QString::fromLatin1(
          QCryptographicHash::hash(
              QJsonDocument::fromVariant(python_canonical_authorization_fixture)
                  .toJson(QJsonDocument::Compact),
              QCryptographicHash::Sha256)
              .toHex()) !=
      QStringLiteral(
          "8f7af2f35d126d9fb02abcced9439edb027f6dbd8466bdacdde4877c57cb8f10")) {
    return fail("Python and Qt authorization canonical identities must match");
  }
  authorization = seal_authorization(authorization);
  const auto authorization_path =
      receipt_root.filePath(QStringLiteral("chunk-authorization.json"));
  if (!write_json(authorization_path, authorization)) {
    return fail("chunk authorization fixture must be written");
  }
  const auto valid_authorization = photo_editor::desktop::
      validate_rendering_migration_chunk_authorization(
          authorization_path, producer.executable_sha256);
  if (!valid_authorization.valid ||
      valid_authorization.asset_ids != QStringList{QStringLiteral("recover")} ||
      valid_authorization.plan_sha256 != QString(64, QLatin1Char('c'))) {
    return fail("native rendering must accept an exact approved chunk authorization");
  }
  if (photo_editor::desktop::
          validate_rendering_migration_chunk_authorization(
              authorization_path, QString(64, QLatin1Char('f')))
          .valid) {
    return fail("native rendering must reject authorization for another executable");
  }

  QTemporaryDir report_root;
  if (!report_root.isValid()) {
    return fail("batch migration report test needs temporary storage");
  }
  QImage artifact_candidate(12, 8, QImage::Format_ARGB32);
  artifact_candidate.fill(QColor(72, 96, 144));
  QImage artifact_reference(12, 8, QImage::Format_ARGB32);
  artifact_reference.fill(QColor(68, 92, 136));
  auto artifact_evidence =
      photo_editor::desktop::
          build_rendering_migration_evidence(
              artifact_candidate,
              artifact_reference,
              QStringLiteral("aces2-ocio-v1"),
              QStringLiteral("legacy-oklch-v1"),
              QStringLiteral("artifact-plan"),
              QStringLiteral("artifact-patch"));
  QString artifact_error;
  if (!photo_editor::desktop::
          add_rendering_migration_target_comparison(
              &artifact_evidence,
              QStringLiteral("display-p3-simulation"),
              QStringLiteral("Display P3"),
              QStringLiteral("artifact-p3-plan"),
              artifact_candidate,
              artifact_reference,
              &artifact_error)) {
    return fail(
        "migration target comparison should bind dedicated artifacts");
  }
  if (!photo_editor::desktop::
          add_rendering_migration_structure_attribution(
              &artifact_evidence,
              QStringLiteral("layers"),
              1,
              QStringLiteral("artifact-without-layers-plan"),
              artifact_candidate,
              artifact_reference,
              &artifact_error)) {
    return fail(
        "migration structure attribution should bind dedicated artifacts");
  }
  const auto artifact_root =
      report_root.filePath(
          QStringLiteral("evidence-artifacts"));
  QStringList artifact_file_names;
  if (!photo_editor::desktop::
          write_immutable_rendering_migration_evidence_artifacts(
              artifact_root,
              artifact_evidence,
              &artifact_file_names,
              &artifact_error)) {
    return fail(
        "migration evidence artifacts must be complete and immutable");
  }
  artifact_evidence.candidate_export_png = {};
  artifact_evidence.reference_export_png = {};
  artifact_evidence.delta_e00_heatmap_png = {};
  artifact_evidence.candidate_shipping_format_bytes.clear();
  artifact_evidence.reference_shipping_format_bytes.clear();
  artifact_evidence.candidate_target_png_bytes.clear();
  artifact_evidence.reference_target_png_bytes.clear();
  artifact_evidence.candidate_attribution_png_bytes.clear();
  artifact_evidence.reference_attribution_png_bytes.clear();
  if (!photo_editor::desktop::
          write_immutable_rendering_migration_evidence_artifacts(
              artifact_root,
              artifact_evidence,
              nullptr,
              &artifact_error) ||
      !photo_editor::desktop::
          write_immutable_rendering_migration_evidence_artifacts(
              artifact_root,
              artifact_evidence,
              nullptr,
              &artifact_error) || artifact_file_names.size() != 11) {
    return fail(
        "streamed migration evidence artifacts must remain hash-verified and idempotent");
  }
  for (const auto& artifact_file_name :
       artifact_file_names) {
    if (!QFileInfo::exists(
            QDir(artifact_root)
                .filePath(artifact_file_name))) {
      return fail(
          "migration evidence artifact file should exist");
    }
  }
  const auto report_path =
      report_root.filePath(
          QStringLiteral("reports/batch.json"));
  QString write_error;
  if (!photo_editor::desktop::
          write_immutable_rendering_migration_batch_report(
              report_path,
              report.document,
              &write_error) ||
      !photo_editor::desktop::
          write_immutable_rendering_migration_batch_report(
              report_path,
              report.document,
              &write_error)) {
    return fail("batch migration report persistence must be immutable and idempotent");
  }
  QFile report_file(report_path);
  if (!report_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      report_file.write("tampered") != 8) {
    return fail("batch migration report test should be able to corrupt its fixture");
  }
  report_file.close();
  if (photo_editor::desktop::
          write_immutable_rendering_migration_batch_report(
              report_path,
              report.document,
              &write_error) ||
      !write_error.contains(QStringLiteral("does not match"))) {
    return fail("batch migration report persistence must reject tampered artifacts");
  }

  auto duplicate_assets = assets;
  duplicate_assets.push_back(assets.front());
  if (photo_editor::desktop::
          build_rendering_migration_batch_report(
              duplicate_assets, {}, producer)
          .valid) {
    return fail("batch migration report must reject duplicate asset identities");
  }
  auto mismatched_artifact_assets = assets;
  mismatched_artifact_assets.front()
      .evidence_artifact_file_names =
      {QStringLiteral("candidate-export-sha256-invalid.png")};
  if (photo_editor::desktop::
          build_rendering_migration_batch_report(
              mismatched_artifact_assets, {}, producer)
          .valid) {
    return fail(
        "batch migration report must reject evidence artifact identity mismatches");
  }
  if (photo_editor::desktop::
          build_rendering_migration_batch_report(
              assets,
              {.p95_delta_e00 = 3.0,
               .maximum_delta_e00 = 2.0},
              producer)
          .valid) {
    return fail("batch migration report must reject inconsistent thresholds");
  }

  const auto legacy_source =
      report_root.filePath(QStringLiteral("legacy.dng"));
  const auto current_source =
      report_root.filePath(QStringLiteral("current.dng"));
  for (const auto& source_path :
       {legacy_source, current_source}) {
    QFile source_file(source_path);
    if (!source_file.open(QIODevice::WriteOnly) ||
        source_file.write("raw") != 3) {
      return fail("batch runner test should create source fixtures");
    }
  }
  const auto inventory = QJsonObject{
      {QStringLiteral("schema"),
       QStringLiteral(
           "photo-editor-rendering-migration-inventory:v1")},
      {QStringLiteral("edited_asset_count"), 3},
      {QStringLiteral("assets"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("asset_id"),
                QStringLiteral("legacy")},
               {QStringLiteral("source_path"), legacy_source},
               {QStringLiteral("source_available"), true},
               {QStringLiteral("source_renderer"),
                QStringLiteral("legacy-oklch-v1")},
           },
           QJsonObject{
               {QStringLiteral("asset_id"),
                QStringLiteral("current")},
               {QStringLiteral("source_path"), current_source},
               {QStringLiteral("source_available"), true},
               {QStringLiteral("source_renderer"),
                QStringLiteral("aces2-ocio-v1")},
           },
           QJsonObject{
               {QStringLiteral("asset_id"),
                QStringLiteral("offline")},
               {QStringLiteral("source_path"),
                report_root.filePath(
                    QStringLiteral("offline.dng"))},
               {QStringLiteral("source_available"), false},
               {QStringLiteral("source_renderer"),
                QStringLiteral("legacy-oklch-v1")},
           },
       }},
  };
  auto assessed_count = 0;
  const auto batch_run =
      photo_editor::desktop::
          run_rendering_migration_batch(
              inventory,
              report_root.filePath(
                  QStringLiteral("runner-report.json")),
              [&assessed_count, &ready_evidence](
                  const QVariantMap& inventory_asset) {
                ++assessed_count;
                const auto asset_id =
                    inventory_asset
                        .value(QStringLiteral("asset_id"))
                        .toString();
                return RenderingMigrationBatchAsset{
                    .asset_id = asset_id,
                    .candidate_compiled = true,
                    .evidence =
                        asset_id == QStringLiteral("legacy")
                            ? ready_evidence
                            : QVariantMap{},
                };
              },
              {},
              producer);
  const auto run_summary =
      batch_run.report.document
          .value(QStringLiteral("summary"))
          .toMap();
  if (!batch_run.valid || assessed_count != 2 ||
      run_summary
              .value(QStringLiteral("editedAssetCount"))
              .toInt() != 3 ||
      run_summary
              .value(QStringLiteral("candidateCompiledCount"))
              .toInt() != 2 ||
      run_summary.value(QStringLiteral("readyCount"))
              .toInt() != 2 ||
      !run_summary
           .value(QStringLiteral("missingOriginalAssetIds"))
           .toStringList()
           .contains(QStringLiteral("offline"))) {
    return fail("batch runner must assess every available edited asset and report offline originals");
  }
  auto selected_assessed_count = 0;
  const auto selected_run =
      photo_editor::desktop::run_rendering_migration_batch(
          inventory,
          QString{},
          [&selected_assessed_count, &ready_evidence](
              const QVariantMap& inventory_asset) {
            ++selected_assessed_count;
            return RenderingMigrationBatchAsset{
                .asset_id = inventory_asset
                    .value(QStringLiteral("asset_id"))
                    .toString(),
                .candidate_compiled = true,
                .evidence = ready_evidence,
            };
          },
          {},
          producer,
          QSet<QString>{QStringLiteral("legacy")});
  if (!selected_run.valid || selected_assessed_count != 1 ||
      selected_run.report.document
              .value(QStringLiteral("summary"))
              .toMap()
              .value(QStringLiteral("assetCount"))
              .toInt() != 1) {
    return fail("authorized batch runner must assess only the selected chunk assets");
  }
  if (photo_editor::desktop::run_rendering_migration_batch(
          inventory,
          QString{},
          [](const QVariantMap&) {
            return RenderingMigrationBatchAsset{};
          },
          {},
          producer,
          QSet<QString>{QStringLiteral("absent")})
          .valid) {
    return fail("authorized batch runner must reject assets absent from the catalog");
  }
  auto inconsistent_inventory = inventory;
  inconsistent_inventory.insert(
      QStringLiteral("edited_asset_count"), 2);
  if (photo_editor::desktop::
          run_rendering_migration_batch(
              inconsistent_inventory,
              QString{},
              [](const QVariantMap&) {
                return RenderingMigrationBatchAsset{};
              },
              {},
              producer)
          .valid) {
    return fail("batch runner must reject an incomplete catalog inventory");
  }

  return EXIT_SUCCESS;
}

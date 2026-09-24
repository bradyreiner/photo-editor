#include <cstdlib>
#include <iostream>
#include <string_view>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>

#include "app_bootstrap.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "rendering_migration_asset_assessor.h"
#include "rendering_migration_evidence.h"

#include "photo_editor/io/hdr_avif_io.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static QStringList rendering_upgrade_request_models(
      const EditorPreviewController& controller) {
    const auto request = controller.currentRenderRequest(false);
    return QStringList{request.state.photographic_rendering_model,
                       request.rendering_upgrade_reference_model,
                       request.state.renderer_transition_look};
  }

  static bool full_render_timer_active(
      const EditorPreviewController& controller) {
    return controller.render_scheduler_.fullActive();
  }

  static bool full_render_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }

  static bool full_render_running(const EditorPreviewController& controller) {
    return controller.full_render_watcher_.isRunning();
  }

  static bool photo_layer_adjustment_render_timer_active(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_timer_.isActive();
  }
};

class MigrationProofImageSink final : public EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override {
    images_.insert(key, image);
  }

  void clearImage(const QString& key) override { images_.remove(key); }

  [[nodiscard]] QImage image(const QString& key) const {
    return images_.value(key);
  }

 private:
  QHash<QString, QImage> images_;
};

}  // namespace photo_editor::desktop

namespace {

constexpr auto kSchema = "photo-editor-rendering-migration-proof:v1";
constexpr auto kToolVersion = "photo-editor-rendering-migration-proof-test:v1";
constexpr auto kLegacyModel = "legacy-oklch-v1";
constexpr auto kCandidateModel = "aces2-ocio-v1";

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

[[nodiscard]] QByteArray sha256_bytes(const QByteArray& bytes) {
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

[[nodiscard]] QByteArray sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash digest(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto block = file.read(1024 * 1024);
    if (block.isEmpty() && !file.atEnd()) {
      return {};
    }
    digest.addData(block);
  }
  return digest.result().toHex();
}

bool wait_for_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms = 5000) {
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!controller.processing() &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            full_render_timer_active(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            full_render_running(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            full_render_pending(controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            photo_layer_adjustment_render_timer_active(controller)) {
      return true;
    }
    QThread::msleep(25);
    elapsed_ms += 25;
  }
  return false;
}

[[nodiscard]] QImage source_fixture() {
  QImage source(24, 16, QImage::Format_ARGB32);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixel(x, y, qRgba(24 + x * 8, 18 + y * 12,
                                 220 - x * 5, 255));
    }
  }
  return source;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef NDEBUG
  constexpr auto kBuildMode = "release";
#else
  constexpr auto kBuildMode = "debug";
#endif
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorProofs"));
  QCoreApplication::setApplicationName(QStringLiteral("RenderingMigrationProof"));
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");

  auto report_path = QString{};
  if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--report")) {
    report_path = QString::fromLocal8Bit(argv[2]);
  } else if (argc != 1) {
    return fail("usage: rendering_migration_proof_test [--report report.json]");
  }
  if (!report_path.isEmpty() &&
      (QFileInfo::exists(report_path) ||
       std::string_view(kBuildMode) != std::string_view("release"))) {
    return fail(
        "rendering migration proof requires a new report path and a Release executable");
  }

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("rendering migration proof requires a temporary runtime directory");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  if (!photo_editor::desktop::AppBootstrap::ensureRuntimeDirectories()) {
    return fail("rendering migration proof could not configure runtime storage");
  }

  const auto source_path =
      runtime_root.filePath(QStringLiteral("rendering-migration-source.png"));
  const auto source = source_fixture();
  if (!source.save(source_path)) {
    return fail("rendering migration proof source fixture should be writable");
  }

  photo_editor::desktop::EditState legacy_state;
  legacy_state.exposure = 0.35;
  legacy_state.contrast = 18.0;
  legacy_state.vibrance = 22.0;
  legacy_state.photographic_rendering_model = QString::fromLatin1(kLegacyModel);
  auto batch_legacy_state = legacy_state;
  batch_legacy_state.raw_gamut_mapping = true;
  batch_legacy_state.highlight_reconstruction_model =
      QStringLiteral("legacy-working-rgb-v1");
  batch_legacy_state.linear_mask_enabled = true;
  batch_legacy_state.linear_mask_exposure = 0.24;
  batch_legacy_state.mask_stack = {
      QVariantMap{
          {QStringLiteral("id"),
           QStringLiteral("migration-proof-ellipse")},
          {QStringLiteral("maskSetId"),
           QStringLiteral("migration-proof-ellipse-set")},
          {QStringLiteral("kind"), QStringLiteral("ellipse")},
          {QStringLiteral("operation"), QStringLiteral("add")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("centerX"), 0.45},
          {QStringLiteral("centerY"), 0.55},
          {QStringLiteral("radiusX"), 0.30},
          {QStringLiteral("radiusY"), 0.20},
          {QStringLiteral("feather"), 0.12},
          {QStringLiteral("exposure"), 0.24},
      },
  };
  batch_legacy_state.layers = {
      QVariantMap{
          {QStringLiteral("id"),
           QStringLiteral("migration-proof-layer")},
          {QStringLiteral("kind"),
           QStringLiteral("adjustment")},
          {QStringLiteral("name"),
           QStringLiteral("Migration Proof Layer")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("opacity"), 0.72},
          {QStringLiteral("blendMode"),
           QStringLiteral("normal")},
          {QStringLiteral("selectionScoped"), true},
          {QStringLiteral("selectionMaskSetId"),
           QStringLiteral("missing-legacy-mask-set")},
          {QStringLiteral("exposure"), 0.18},
          {QStringLiteral("vibrance"), 9.0},
      },
  };
  auto before_preset_state = batch_legacy_state;
  before_preset_state.contrast = 0.0;
  before_preset_state.vibrance = 0.0;
  const auto preset_groups = QStringList{
      QStringLiteral("tone"),
      QStringLiteral("color"),
  };
  const auto preset_before_patch =
      photo_editor::desktop::serialize_adjustment_patch_json(
          before_preset_state,
          preset_groups,
          photo_editor::desktop::
              AdjustmentPatchScope::AssetBound);
  const auto preset_after_patch =
      photo_editor::desktop::serialize_adjustment_patch_json(
          batch_legacy_state,
          preset_groups,
          photo_editor::desktop::
              AdjustmentPatchScope::AssetBound);
  batch_legacy_state.preset_applications = {
      QVariantMap{
          {QStringLiteral("id"),
           QStringLiteral("migration-proof-preset-application")},
          {QStringLiteral("identity"),
           QStringLiteral("preset:migration-proof")},
          {QStringLiteral("name"),
           QStringLiteral("Migration Proof")},
          {QStringLiteral("groups"), preset_groups},
          {QStringLiteral("beforeAdjustmentPatchJson"),
           preset_before_patch},
          {QStringLiteral("beforeAdjustmentPatchSha256"),
           QString::fromLatin1(
               QCryptographicHash::hash(
                   preset_before_patch.toUtf8(),
                   QCryptographicHash::Sha256)
                   .toHex())},
          {QStringLiteral("afterAdjustmentPatchJson"),
           preset_after_patch},
          {QStringLiteral("afterAdjustmentPatchSha256"),
           QString::fromLatin1(
               QCryptographicHash::hash(
                   preset_after_patch.toUtf8(),
                   QCryptographicHash::Sha256)
                   .toHex())},
      },
  };
  const auto expected_legacy_recipe =
      photo_editor::desktop::serialize_edit_recipe_json(legacy_state);
  const auto batch_legacy_recipe =
      photo_editor::desktop::serialize_edit_recipe_json(
          batch_legacy_state);
  const auto expected_legacy_patch =
      photo_editor::desktop::
          serialize_adjustment_patch_json(
              legacy_state,
              photo_editor::desktop::
                  all_adjustment_groups(),
              photo_editor::desktop::
                  AdjustmentPatchScope::
                      AssetBound);

  const auto batch_assessment =
      photo_editor::desktop::
          assess_rendering_migration_inventory_asset(
              QVariantMap{
                  {QStringLiteral("asset_id"),
                   QStringLiteral(
                       "rendering-migration-proof-asset")},
                  {QStringLiteral("source_path"),
                   source_path},
                  {QStringLiteral("source_available"), true},
                  {QStringLiteral("legacy_recipe"),
                   QJsonDocument::fromJson(
                       batch_legacy_recipe.toUtf8())
                       .object()
                       .toVariantMap()},
              },
              runtime_root.filePath(
                  QStringLiteral("recipe-assets")),
              0,
              runtime_root.filePath(
                  QStringLiteral(
                      "migration-evidence-artifacts")));
  const auto structure_attributions =
      batch_assessment.evidence
          .value(QStringLiteral(
              "structureAttributionEvidence"))
          .toMap();
  const auto shipping_formats =
      batch_assessment.evidence
          .value(QStringLiteral("shippingFormatEvidence"))
          .toMap();
  const auto target_comparisons =
      batch_assessment.evidence
          .value(QStringLiteral("targetComparisonEvidence"))
          .toMap();
  const auto input_semantic_migrations =
      batch_assessment.evidence
          .value(QStringLiteral("migrationContext"))
          .toMap()
          .value(QStringLiteral("inputSemanticMigrations"))
          .toList();
  const auto structural_semantic_migrations =
      batch_assessment.evidence
          .value(QStringLiteral("migrationContext"))
          .toMap()
          .value(QStringLiteral("structuralSemanticMigrations"))
          .toList();
  const auto renderer_semantic_migrations =
      batch_assessment.evidence
          .value(QStringLiteral("migrationContext"))
          .toMap()
          .value(QStringLiteral("rendererSemanticMigrations"))
          .toList();
  const auto hdr_backend_available =
      photo_editor::io::hdr_avif_backend_available();
  const auto pq_shipping =
      shipping_formats.value(QStringLiteral("pq-avif-10")).toMap();
  const auto hlg_shipping =
      shipping_formats.value(QStringLiteral("hlg-avif-10")).toMap();
  const auto pq_target =
      target_comparisons.value(QStringLiteral("rec2100-pq")).toMap();
  const auto hlg_target =
      target_comparisons.value(QStringLiteral("rec2100-hlg")).toMap();
  if (!batch_assessment.candidate_compiled ||
      !batch_assessment.migration_required ||
      input_semantic_migrations.size() != 2 ||
      input_semantic_migrations.at(0)
              .toMap()
              .value(QStringLiteral("classification"))
              .toString() !=
          QStringLiteral("retired-no-op-control") ||
      input_semantic_migrations.at(1)
              .toMap()
              .value(QStringLiteral("classification"))
              .toString() !=
          QStringLiteral("measured-semantic-upgrade") ||
      structural_semantic_migrations.size() != 2 ||
      structural_semantic_migrations.at(0)
              .toMap()
              .value(QStringLiteral("classification"))
              .toString() !=
          QStringLiteral("legacy-geometry-synonym") ||
      structural_semantic_migrations.at(1)
              .toMap()
              .value(QStringLiteral("classification"))
              .toString() !=
          QStringLiteral("dangling-reference-normalization") ||
      renderer_semantic_migrations.size() != 1 ||
      renderer_semantic_migrations.at(0)
              .toMap()
              .value(QStringLiteral("classification"))
              .toString() !=
          QStringLiteral("measured-renderer-transition") ||
      renderer_semantic_migrations.at(0)
              .toMap()
              .value(QStringLiteral("candidateValue"))
              .toString() !=
          QStringLiteral("preserve-legacy-direct-luminance-v1") ||
      batch_assessment.evidence
              .value(QStringLiteral("fullResolutionWidth"))
              .toInt() != source.width() ||
      batch_assessment.evidence
              .value(QStringLiteral("fullResolutionHeight"))
              .toInt() != source.height() ||
      batch_assessment.evidence
              .value(QStringLiteral(
                  "candidateFullImageSha256"))
              .toString()
              .size() != 64 ||
      batch_assessment.evidence
              .value(QStringLiteral(
                  "referenceFullImageSha256"))
              .toString()
              .size() != 64 ||
      !batch_assessment.evidence
           .value(QStringLiteral(
               "candidateEncodedRoundTripExact"))
           .toBool() ||
      !batch_assessment.evidence
           .value(QStringLiteral(
               "referenceEncodedRoundTripExact"))
           .toBool() ||
      batch_assessment.evidence
              .value(QStringLiteral(
                  "deltaE00HeatmapPngSha256"))
              .toString()
              .size() != 64 ||
      !batch_assessment.evidence
           .value(QStringLiteral(
               "shippingFormatEvidence"))
           .toMap()
           .value(QStringLiteral("jpeg-8"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool() ||
      !batch_assessment.evidence
           .value(QStringLiteral(
               "shippingFormatEvidence"))
           .toMap()
           .value(QStringLiteral("tiff-16"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool() ||
      !target_comparisons.contains(
          QStringLiteral("display-p3-simulation")) ||
      pq_target
          .value(QStringLiteral("planCacheKey"))
          .toString()
          .trimmed()
          .isEmpty() ||
      hlg_target
          .value(QStringLiteral("planCacheKey"))
          .toString()
          .trimmed()
          .isEmpty() ||
      pq_target
          .value(QStringLiteral("linearTargetDifference"))
          .toMap()
          .isEmpty() ||
      hlg_target
          .value(QStringLiteral("linearTargetDifference"))
          .toMap()
          .isEmpty() ||
      pq_target.value(QStringLiteral("hdrPerceptualApproval")).toBool() ||
      hlg_target.value(QStringLiteral("hdrPerceptualApproval")).toBool() ||
      pq_shipping.isEmpty() || hlg_shipping.isEmpty() ||
      pq_shipping.value(QStringLiteral("available")).toBool() !=
          hdr_backend_available ||
      hlg_shipping.value(QStringLiteral("available")).toBool() !=
          hdr_backend_available ||
      (!hdr_backend_available &&
       (pq_shipping.value(QStringLiteral("failureReason")).toString() !=
            QStringLiteral("hdr_avif_backend_unavailable") ||
        hlg_shipping.value(QStringLiteral("failureReason")).toString() !=
            QStringLiteral("hdr_avif_backend_unavailable"))) ||
      (hdr_backend_available &&
       (!pq_shipping
             .value(QStringLiteral("candidateArtifactContractVerified"))
             .toBool() ||
        !pq_shipping
             .value(QStringLiteral("referenceArtifactContractVerified"))
             .toBool() ||
        !hlg_shipping
             .value(QStringLiteral("candidateArtifactContractVerified"))
             .toBool() ||
        !hlg_shipping
             .value(QStringLiteral("referenceArtifactContractVerified"))
             .toBool() ||
        pq_shipping
            .value(QStringLiteral("candidateRoundTrip"))
            .toMap()
            .isEmpty() ||
        pq_shipping
            .value(QStringLiteral("referenceRoundTrip"))
            .toMap()
            .isEmpty() ||
        hlg_shipping
            .value(QStringLiteral("candidateRoundTrip"))
            .toMap()
            .isEmpty() ||
        hlg_shipping
            .value(QStringLiteral("referenceRoundTrip"))
            .toMap()
            .isEmpty() ||
        pq_shipping
            .value(QStringLiteral("encodedSignalDifference"))
            .toMap()
            .isEmpty() ||
        hlg_shipping
            .value(QStringLiteral("encodedSignalDifference"))
            .toMap()
            .isEmpty() ||
        !pq_shipping
             .value(QStringLiteral("candidateInspection"))
             .toMap()
             .value(QStringLiteral("hasMasteringDisplay"))
             .toBool() ||
        !hlg_shipping
             .value(QStringLiteral("candidateInspection"))
             .toMap()
             .value(QStringLiteral("hasMasteringDisplay"))
             .toBool())) ||
      structure_attributions.size() != 3 ||
      !structure_attributions
           .value(QStringLiteral("masks"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool() ||
      !structure_attributions
           .value(QStringLiteral("layers"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool() ||
      !structure_attributions
           .value(QStringLiteral("presets"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool() ||
      structure_attributions
          .value(QStringLiteral("presets"))
          .toMap()
          .value(QStringLiteral(
              "withoutStructureMinusFullComposite"))
          .toMap()
          .isEmpty() ||
      batch_assessment.evidence_artifact_file_names.size() !=
          (hdr_backend_available ? 19 : 15)) {
    std::cerr
        << "batch assessment compiled="
        << batch_assessment.candidate_compiled
        << " required="
        << batch_assessment.migration_required
        << " error="
        << batch_assessment.candidate_error.toStdString()
        << " width="
        << batch_assessment.evidence
               .value(QStringLiteral("fullResolutionWidth"))
               .toInt()
        << " height="
        << batch_assessment.evidence
               .value(QStringLiteral("fullResolutionHeight"))
               .toInt()
        << " artifacts="
        << batch_assessment.evidence_artifact_file_names.size()
        << " shipping="
        << shipping_formats.keys().join(QLatin1Char(',')).toStdString()
        << " targets="
        << target_comparisons.keys().join(QLatin1Char(',')).toStdString()
        << '\n';
    return fail(
        "batch assessor must compile and compare a full-resolution migration candidate");
  }
  for (const auto& artifact_file_name :
       batch_assessment.evidence_artifact_file_names) {
    if (!QFileInfo::exists(
            runtime_root.filePath(
                QStringLiteral(
                    "migration-evidence-artifacts/%1")
                    .arg(artifact_file_name)))) {
      return fail(
          "batch assessor must persist every content-addressed evidence artifact");
    }
  }
  const auto repeated_batch_assessment =
      photo_editor::desktop::
          assess_rendering_migration_inventory_asset(
              QVariantMap{
                  {QStringLiteral("asset_id"),
                   QStringLiteral(
                       "rendering-migration-proof-asset")},
                  {QStringLiteral("source_path"), source_path},
                  {QStringLiteral("source_available"), true},
                  {QStringLiteral("legacy_recipe"),
                   QJsonDocument::fromJson(
                       batch_legacy_recipe.toUtf8())
                       .object()
                       .toVariantMap()},
              },
              runtime_root.filePath(
                  QStringLiteral("recipe-assets")),
              0,
              runtime_root.filePath(
                  QStringLiteral(
                      "migration-evidence-artifacts-repeat")));
  const auto repeated_shipping_formats =
      repeated_batch_assessment.evidence
          .value(QStringLiteral("shippingFormatEvidence"))
          .toMap();
  const auto repeated_target_comparisons =
      repeated_batch_assessment.evidence
          .value(QStringLiteral("targetComparisonEvidence"))
          .toMap();
  if (repeated_batch_assessment.evidence
          .value(QStringLiteral("structureAttributionEvidence")) !=
      batch_assessment.evidence
          .value(QStringLiteral("structureAttributionEvidence"))) {
    return fail(
        "isolated structure attribution evidence must be deterministic");
  }
  const auto hdr_artifact_names = [](const QStringList& names) {
    QStringList result;
    for (const auto& name : names) {
      if (name.contains(QStringLiteral("-avif-10-"))) {
        result.append(name);
      }
    }
    return result;
  };
  if (!repeated_batch_assessment.candidate_compiled ||
      repeated_batch_assessment.evidence
              .value(QStringLiteral("evidenceSha256"))
              .toString() !=
          batch_assessment.evidence
              .value(QStringLiteral("evidenceSha256"))
              .toString() ||
      repeated_batch_assessment.evidence_artifact_file_names !=
          batch_assessment.evidence_artifact_file_names ||
      repeated_shipping_formats.value(QStringLiteral("pq-avif-10")) !=
          shipping_formats.value(QStringLiteral("pq-avif-10")) ||
      repeated_shipping_formats.value(QStringLiteral("hlg-avif-10")) !=
          shipping_formats.value(QStringLiteral("hlg-avif-10")) ||
      repeated_target_comparisons.value(QStringLiteral("rec2100-pq")) !=
          target_comparisons.value(QStringLiteral("rec2100-pq")) ||
      repeated_target_comparisons.value(QStringLiteral("rec2100-hlg")) !=
          target_comparisons.value(QStringLiteral("rec2100-hlg")) ||
      hdr_artifact_names(
          repeated_batch_assessment.evidence_artifact_file_names) !=
          hdr_artifact_names(
              batch_assessment.evidence_artifact_file_names)) {
    return fail(
        "PQ/HLG target renders and production AVIF artifacts must be deterministic");
  }
  auto legacy_preset_only_state = legacy_state;
  legacy_preset_only_state.preset_applications = {
      QVariantMap{
          {QStringLiteral("id"),
           QStringLiteral("legacy-provenance-only")},
          {QStringLiteral("identity"),
           QStringLiteral("preset:legacy")},
      },
  };
  const auto legacy_preset_only_assessment =
      photo_editor::desktop::
          assess_rendering_migration_inventory_asset(
              QVariantMap{
                  {QStringLiteral("asset_id"),
                   QStringLiteral(
                       "rendering-migration-legacy-preset")},
                  {QStringLiteral("source_path"), source_path},
                  {QStringLiteral("source_available"), true},
                  {QStringLiteral("legacy_recipe"),
                   QJsonDocument::fromJson(
                       photo_editor::desktop::
                           serialize_edit_recipe_json(
                               legacy_preset_only_state)
                               .toUtf8())
                       .object()
                       .toVariantMap()},
              },
              runtime_root.filePath(
                  QStringLiteral("recipe-assets")));
  const auto unavailable_preset_attribution =
      legacy_preset_only_assessment.evidence
          .value(QStringLiteral(
              "structureAttributionEvidence"))
          .toMap()
          .value(QStringLiteral("presets"))
          .toMap();
  if (!legacy_preset_only_assessment.candidate_compiled ||
      unavailable_preset_attribution
          .value(QStringLiteral("available"))
          .toBool() ||
      !unavailable_preset_attribution
           .value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("reversible"))) {
    return fail(
        "legacy preset provenance without verified before/after patches must be classified as unavailable attribution");
  }
  auto missing_profile_state = legacy_state;
  missing_profile_state.export_icc_profile_asset.asset_id =
      QStringLiteral("missing-export-profile");
  missing_profile_state.export_icc_profile_asset.payload_sha256 =
      QString(64, QLatin1Char('a'));
  missing_profile_state.export_icc_profile_asset.display_name =
      QStringLiteral("Missing export profile");
  missing_profile_state.export_icc_profile_asset.source_uri =
      runtime_root.filePath(
          QStringLiteral("missing-export-profile.icc"));
  missing_profile_state.export_icc_profile_asset.source_kind =
      QStringLiteral("user-file");
  missing_profile_state.export_icc_profile_asset.byte_size = 128;
  const auto missing_profile_assessment =
      photo_editor::desktop::
          assess_rendering_migration_inventory_asset(
              QVariantMap{
                  {QStringLiteral("asset_id"),
                   QStringLiteral(
                       "rendering-migration-missing-profile")},
                  {QStringLiteral("source_path"),
                   source_path},
                  {QStringLiteral("source_available"), true},
                  {QStringLiteral("legacy_recipe"),
                   QJsonDocument::fromJson(
                       photo_editor::desktop::
                           serialize_edit_recipe_json(
                               missing_profile_state)
                               .toUtf8())
                       .object()
                       .toVariantMap()},
              },
              runtime_root.filePath(
                  QStringLiteral("recipe-assets")));
  if (missing_profile_assessment.candidate_compiled ||
      !missing_profile_assessment
           .unsupported_profile_identities
           .contains(QStringLiteral(
               "export-icc:missing-export-profile"))) {
    return fail(
        "batch assessor must classify unavailable profile identities before rendering");
  }

  photo_editor::desktop::EditorPreviewController controller;
  photo_editor::desktop::MigrationProofImageSink sink;
  controller.setImageProvider(&sink);
  controller.loadAsset(
      QStringLiteral("rendering-migration-proof-asset"), source_path,
      QString(), QString(), QString(), QString(), expected_legacy_recipe);
  if (!wait_for_idle(controller) ||
      controller.currentPhotographicRenderingModel() !=
          QString::fromLatin1(kLegacyModel) ||
      controller.currentAssetBoundAdjustmentPatchJson() != expected_legacy_patch ||
      controller.canUndo()) {
    return fail("legacy rendering model must load without implicit migration");
  }
  if (controller.renderingUpgradeStatus() ==
      QStringLiteral("official-processor-unavailable")) {
    return report_path.isEmpty()
               ? 77
               : fail("Release migration proof requires the pinned official ACES 2 processor");
  }

  const auto history_index_before = controller.editHistoryCurrentIndex();
  if (!controller.renderingUpgradeAvailable() ||
      !controller.startRenderingUpgradePreview()) {
    return fail("legacy rendering model must offer an explicit ACES 2 preview");
  }
  const auto preview_models =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          rendering_upgrade_request_models(controller);
  if (!controller.renderingUpgradePreviewActive() || !controller.compareMode() ||
      !controller.renderingUpgradeEvidence().isEmpty() ||
      preview_models != QStringList{QString::fromLatin1(kCandidateModel),
                                    QString::fromLatin1(kLegacyModel),
                                    QStringLiteral(
                                        "preserve-legacy-direct-luminance-v1")} ||
      controller.currentAssetBoundAdjustmentPatchJson() != expected_legacy_patch ||
      controller.editHistoryCurrentIndex() != history_index_before ||
      controller.canUndo()) {
    return fail("migration preview must compare both models without mutating recipe or history");
  }
  if (!wait_for_idle(controller) || sink.image(QStringLiteral("edited")).isNull() ||
      sink.image(QStringLiteral("reference")).isNull()) {
    return fail("migration preview must publish synchronized candidate and legacy images");
  }
  const auto migration_evidence = controller.renderingUpgradeEvidence();
  if (migration_evidence.isEmpty() ||
      migration_evidence.value(QStringLiteral("evidenceSha256"))
              .toString()
              .size() != 64 ||
      migration_evidence.value(QStringLiteral("sampleCount")).toULongLong() == 0 ||
      migration_evidence.value(QStringLiteral("p95DeltaE00")).toDouble() < 0.0 ||
      migration_evidence.value(QStringLiteral("maximumDeltaE00")).toDouble() <
          migration_evidence.value(QStringLiteral("p95DeltaE00")).toDouble()) {
    return fail("migration preview must publish deterministic perceptual and clipping evidence");
  }
  if (QString::fromLatin1(
          sha256_bytes(
              photo_editor::desktop::
                  canonical_rendering_migration_evidence_payload(
                      migration_evidence))) !=
      migration_evidence.value(QStringLiteral("evidenceSha256")).toString()) {
    return fail("migration evidence identity must match the exact immutable helper payload");
  }

  controller.cancelRenderingUpgradePreview();
  if (!wait_for_idle(controller) || controller.renderingUpgradePreviewActive() ||
      !controller.renderingUpgradeEvidence().isEmpty() ||
      controller.compareMode() ||
      controller.currentAssetBoundAdjustmentPatchJson() != expected_legacy_patch ||
      controller.editHistoryCurrentIndex() != history_index_before ||
      controller.canUndo()) {
    return fail("canceling migration preview must restore the exact legacy state");
  }

  if (!controller.startRenderingUpgradePreview() ||
      !wait_for_idle(controller) ||
      controller.renderingUpgradeEvidence().isEmpty() ||
      !controller.applyRenderingUpgrade() || !wait_for_idle(controller)) {
    return fail("rendering migration must apply through the production workflow");
  }
  auto expected_candidate_state = legacy_state;
  expected_candidate_state.photographic_rendering_model =
      QString::fromLatin1(kCandidateModel);
  expected_candidate_state.renderer_transition_look =
      QStringLiteral("preserve-legacy-direct-luminance-v1");
  const auto expected_candidate_patch =
      photo_editor::desktop::
          serialize_adjustment_patch_json(
              expected_candidate_state,
              photo_editor::desktop::
                  all_adjustment_groups(),
              photo_editor::desktop::
                  AdjustmentPatchScope::
                      AssetBound);
  const auto applied_recipe =
      controller
          .currentAssetBoundAdjustmentPatchJson();
  if (applied_recipe != expected_candidate_patch ||
      controller.currentPhotographicRenderingModel() !=
          QString::fromLatin1(kCandidateModel) ||
      !controller.canUndo() || controller.canRedo() ||
      controller.editHistoryCurrentIndex() != history_index_before + 1 ||
      controller.undoLabel() != QStringLiteral("Upgrade rendering to ACES 2")) {
    return fail("migration apply must be exactly one undoable rendering-model change");
  }

  controller.undo();
  const auto undo_recipe =
      controller
          .currentAssetBoundAdjustmentPatchJson();
  if (!wait_for_idle(controller) || undo_recipe != expected_legacy_patch ||
      !controller.canRedo()) {
    return fail("migration undo must recover the exact legacy recipe");
  }
  controller.redo();
  const auto redo_recipe =
      controller
          .currentAssetBoundAdjustmentPatchJson();
  if (!wait_for_idle(controller) || redo_recipe != expected_candidate_patch ||
      controller.renderingUpgradeStatus() != QStringLiteral("current")) {
    return fail("migration redo must recover the exact candidate recipe");
  }

  photo_editor::desktop::EditorPreviewController reloaded_controller;
  reloaded_controller.loadAsset(
      QStringLiteral("rendering-migration-proof-reload"), source_path,
      QString(), QString(), QString(), QString(), redo_recipe);
  const auto reload_recipe =
      reloaded_controller
          .currentAssetBoundAdjustmentPatchJson();
  if (!wait_for_idle(reloaded_controller) ||
      reload_recipe != expected_candidate_patch ||
      reloaded_controller.currentPhotographicRenderingModel() !=
          QString::fromLatin1(kCandidateModel)) {
    return fail("migrated rendering model must survive a clean controller reload");
  }

  if (!report_path.isEmpty()) {
    const auto executable_path =
        QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const auto executable_sha256 = sha256_file(executable_path);
    const auto source_sha256 = sha256_file(source_path);
    const auto legacy_sha256 = sha256_bytes(expected_legacy_patch.toUtf8());
    const auto applied_sha256 = sha256_bytes(applied_recipe.toUtf8());
    const auto undo_sha256 = sha256_bytes(undo_recipe.toUtf8());
    const auto redo_sha256 = sha256_bytes(redo_recipe.toUtf8());
    const auto reload_sha256 = sha256_bytes(reload_recipe.toUtf8());
    if (executable_sha256.size() != 64 || source_sha256.size() != 64 ||
        legacy_sha256.size() != 64 || applied_sha256.size() != 64 ||
        undo_sha256.size() != 64 || redo_sha256.size() != 64 ||
        reload_sha256.size() != 64) {
      return fail("rendering migration proof could not hash its immutable evidence");
    }
    const auto report = QJsonObject{
        {QStringLiteral("schema"), QString::fromLatin1(kSchema)},
        {QStringLiteral("tool_version"), QString::fromLatin1(kToolVersion)},
        {QStringLiteral("hardware"),
         QJsonObject{
             {QStringLiteral("kernel_type"), QSysInfo::kernelType()},
             {QStringLiteral("kernel_version"), QSysInfo::kernelVersion()},
             {QStringLiteral("product_type"), QSysInfo::productType()},
             {QStringLiteral("product_version"), QSysInfo::productVersion()},
             {QStringLiteral("cpu_architecture"),
              QSysInfo::currentCpuArchitecture()},
             {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
             {QStringLiteral("build_mode"), QString::fromLatin1(kBuildMode)},
         }},
        {QStringLiteral("executable_sha256"),
         QString::fromLatin1(executable_sha256)},
        {QStringLiteral("source_fixture_sha256"),
         QString::fromLatin1(source_sha256)},
        {QStringLiteral("rendering_models"),
         QJsonObject{
             {QStringLiteral("legacy"), QString::fromLatin1(kLegacyModel)},
             {QStringLiteral("candidate"), QString::fromLatin1(kCandidateModel)},
             {QStringLiteral("official_processor_available"), true},
         }},
        {QStringLiteral("recipe_sha256"),
         QJsonObject{
             {QStringLiteral("legacy"), QString::fromLatin1(legacy_sha256)},
             {QStringLiteral("applied"), QString::fromLatin1(applied_sha256)},
             {QStringLiteral("undo"), QString::fromLatin1(undo_sha256)},
             {QStringLiteral("redo"), QString::fromLatin1(redo_sha256)},
             {QStringLiteral("reload"), QString::fromLatin1(reload_sha256)},
         }},
        {QStringLiteral("contracts"),
         QJsonObject{
             {QStringLiteral("legacy_load_preserved"), true},
             {QStringLiteral("preview_non_mutating"), true},
             {QStringLiteral("preview_compares_models"), true},
             {QStringLiteral("preview_generates_measured_evidence"), true},
             {QStringLiteral("cancel_non_mutating"), true},
             {QStringLiteral("apply_single_undo_step"), true},
             {QStringLiteral("apply_only_model_change"), true},
             {QStringLiteral("undo_exact"), true},
             {QStringLiteral("redo_exact"), true},
             {QStringLiteral("reload_exact"), true},
         }},
        {QStringLiteral("promotion_eligible"), false},
        {QStringLiteral("result"), QStringLiteral("passed")},
    };
    QSaveFile report_file(report_path);
    if (!report_file.open(QIODevice::WriteOnly) ||
        report_file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0 ||
        !report_file.commit()) {
      return fail("unable to publish rendering migration proof report");
    }
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}

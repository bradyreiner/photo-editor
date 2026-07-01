#include "agent_edit_controller.h"

#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_recipe_state.h"
#include "export_job_controller.h"
#include "observability.h"

#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTime>
#include <QTimer>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::CatalogAssetItem make_asset(
    const QString& asset_id,
    const QString& file_path,
    const QString& recipe_json) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Test Camera");
  item.file_path = file_path;
  item.capture_date = QStringLiteral("Jun 29, 2026");
  item.capture_date_value = QDate(2026, 6, 29);
  item.lens_model = QStringLiteral("RF 50mm F1.2L USM");
  item.iso_label = QStringLiteral("ISO 200");
  item.shutter_speed = QStringLiteral("1/500 s");
  item.aperture_label = QStringLiteral("f/2");
  item.imported_at = QStringLiteral("Jun 29, 2026  1:00 PM");
  item.imported_at_value = QDateTime(QDate(2026, 6, 29), QTime(13, 0));
  item.media_type = QStringLiteral("photo");
  item.recipe_json = recipe_json;
  item.online = true;
  return item;
}

bool write_fixture_image(const QString& file_path) {
  QImage image(24, 16, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(92 + x, 105 + y, 118));
    }
  }
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }
  return image.save(file_path, "PNG");
}

QJsonObject read_json_object(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray read_file_bytes(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

int observability_event_count(const QString& file_path, const QString& event_name) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return 0;
  }

  auto count = 0;
  while (!file.atEnd()) {
    const auto record = QJsonDocument::fromJson(file.readLine()).object();
    if (record.value(QStringLiteral("event")).toString() == event_name) {
      ++count;
    }
  }
  return count;
}

bool has_citation_field(const QVariantList& citations, const QString& field) {
  for (const auto& citation_value : citations) {
    const auto citation = citation_value.toMap();
    if (citation.value(QStringLiteral("field")).toString() == field) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AgentEditControllerTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv(
      "PHOTO_EDITOR_OBSERVABILITY_RUN_DIR",
      QDir(runtime_root.path()).filePath(QStringLiteral("observability")).toUtf8());
  photo_editor::desktop::observability::initialize();
  const auto observability_log_path =
      photo_editor::desktop::observability::desktopLogFilePath();

  const auto source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source.png"));
  if (!write_fixture_image(source_path)) {
    return fail("fixture image should be written");
  }
  const auto second_source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-2.png"));
  if (!write_fixture_image(second_source_path)) {
    return fail("second fixture image should be written");
  }
  const auto original_source_bytes = read_file_bytes(source_path);
  const auto original_second_source_bytes = read_file_bytes(second_source_path);
  if (original_source_bytes.isEmpty() || original_second_source_bytes.isEmpty()) {
    return fail("fixture source image bytes should be readable before agent workflows");
  }

  photo_editor::desktop::EditRecipeState original_state;
  original_state.exposure = 0.0;
  original_state.contrast = 0.0;
  original_state.saturation = 0.0;
  original_state.output_color_space = QStringLiteral("Display P3");
  const auto original_recipe = photo_editor::desktop::serialize_edit_recipe_json(original_state);
  photo_editor::desktop::EditRecipeState second_original_state;
  second_original_state.exposure = 0.0;
  second_original_state.contrast = 0.0;
  second_original_state.saturation = 0.0;
  second_original_state.temperature = 12.0;
  const auto second_original_recipe =
      photo_editor::desktop::serialize_edit_recipe_json(second_original_state);

  photo_editor::desktop::CatalogAssetModel catalog_model;
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.toggleSelectionIndex(0);

  photo_editor::desktop::AgentEditController controller;
  photo_editor::desktop::ExportJobController export_jobs;
  controller.setCatalogModel(&catalog_model);
  controller.setExportJobController(&export_jobs);

  const auto invalid_style = controller.createStyle(
      QString(),
      QStringLiteral("Needs a name"),
      {},
      {},
      {},
      {});
  if (invalid_style.value(QStringLiteral("ok")).toBool()) {
    return fail("style creation should require a name and goal");
  }

  const QVariantMap allowed_tools{
      {QStringLiteral("raw"), true},
      {QStringLiteral("global_color"), true},
      {QStringLiteral("curves"), true},
      {QStringLiteral("hsl"), true},
      {QStringLiteral("color_grading"), false},
      {QStringLiteral("detail"), true},
      {QStringLiteral("crop"), QStringLiteral("conservative")},
      {QStringLiteral("masks"), true},
      {QStringLiteral("retouch"), false},
      {QStringLiteral("layers_luts"), false},
      {QStringLiteral("export"), false},
  };
  const auto created_style = controller.createStyle(
      QStringLiteral("Moody Test Style"),
      QStringLiteral("Moody editorial contrast with protected highlights and restrained saturation."),
      allowed_tools,
      QVariantMap{
          {QStringLiteral("avoid_clipped_highlights"), true},
          {QStringLiteral("avoid_crushed_blacks"), true},
          {QStringLiteral("max_exposure_shift"), 0.9},
          {QStringLiteral("max_temperature_shift"), 1200},
          {QStringLiteral("max_saturation_shift"), 10},
      },
      QVariantMap{
          {QStringLiteral("highlight_clip_limit_percent"), 0.4},
          {QStringLiteral("shadow_clip_limit_percent"), 0.8},
          {QStringLiteral("require_before_after_proof"), true},
      },
      QVariantMap{
          {QStringLiteral("cluster_by_lighting"), true},
          {QStringLiteral("maintain_sequence_consistency"), true},
          {QStringLiteral("correct_outliers_individually"), true},
      });
  if (!created_style.value(QStringLiteral("ok")).toBool()) {
    std::cerr << created_style.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("style creation should persist a valid user style");
  }
  if (controller.selectedStyleId() != QStringLiteral("moody-test-style")) {
    return fail("newly created style should become the selected agent style");
  }
  const auto style_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("agent-styles/moody-test-style.json"));
  const auto persisted_style = read_json_object(style_path);
  if (persisted_style.value(QStringLiteral("name")).toString() != QStringLiteral("Moody Test Style") ||
      !persisted_style.value(QStringLiteral("allowed_tools")).toObject().value(QStringLiteral("masks")).toBool() ||
      persisted_style.value(QStringLiteral("constraints"))
              .toObject()
              .value(QStringLiteral("max_exposure_shift"))
              .toDouble() != 0.9) {
    return fail("created style should write user-controlled intent, permissions, and constraints");
  }
  const auto style_export_dir = QDir(runtime_root.path()).filePath(QStringLiteral("style-exports"));
  const auto exported_style = controller.exportStyle(QStringLiteral("moody-test-style"), style_export_dir);
  const auto exported_style_path = exported_style.value(QStringLiteral("exportPath")).toString();
  if (!exported_style.value(QStringLiteral("ok")).toBool() ||
      exported_style.value(QStringLiteral("mode")).toString() != QStringLiteral("style-export") ||
      exported_style.value(QStringLiteral("styleId")).toString() != QStringLiteral("moody-test-style") ||
      !QFileInfo(exported_style_path).isFile()) {
    return fail("style export should write a validated shareable style JSON file");
  }
  const auto imported_style = controller.importStyleJson(
      QString::fromUtf8(read_file_bytes(exported_style_path)),
      QStringLiteral("Imported Moody Test Style"));
  if (!imported_style.value(QStringLiteral("ok")).toBool() ||
      imported_style.value(QStringLiteral("mode")).toString() != QStringLiteral("style-import") ||
      imported_style.value(QStringLiteral("styleId")).toString() != QStringLiteral("moody-test-style-2") ||
      imported_style.value(QStringLiteral("styleName")).toString() !=
          QStringLiteral("Imported Moody Test Style") ||
      !imported_style.value(QStringLiteral("renamedOnImport")).toBool() ||
      controller.selectedStyleId() != QStringLiteral("moody-test-style-2") ||
      !QFileInfo(imported_style.value(QStringLiteral("importPath")).toString()).isFile()) {
    return fail("style import should validate JSON, avoid id collisions, reload styles, and select the import");
  }

  controller.setUserInstructions(QStringLiteral("bright warm editorial draft with soft highlights"));

  const auto recipe_before_manual_gate = catalog_model.selectedRecipeJson();
  controller.setManualEditingActive(true);
  const auto paused_agent_run =
      controller.runDraftOnSelected(QStringLiteral("should wait for manual edits"));
  if (paused_agent_run.value(QStringLiteral("ok")).toBool() ||
      paused_agent_run.value(QStringLiteral("backgroundWorkPolicy"))
              .toMap()
              .value(QStringLiteral("action"))
              .toString() != QStringLiteral("deferred") ||
      !paused_agent_run.value(QStringLiteral("backgroundWorkPolicy"))
           .toMap()
           .value(QStringLiteral("manualEditingActive"))
           .toBool() ||
      controller.draftActive() ||
      catalog_model.selectedRecipeJson() != recipe_before_manual_gate) {
    return fail("agent draft work should pause without mutating recipes during active manual editing");
  }
  controller.setManualEditingActive(false);

  const auto recipe_before_analysis = catalog_model.selectedRecipeJson();
  const auto analysis = controller.analyzeSelected();
  if (!analysis.value(QStringLiteral("ok")).toBool()) {
    std::cerr << analysis.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("agent analyze-only run should succeed");
  }
  if (analysis.value(QStringLiteral("mode")).toString() != QStringLiteral("analyze")) {
    return fail("analyze-only report should identify its run mode");
  }
  if (controller.draftActive()) {
    return fail("analyze-only run should not create an active draft");
  }
  if (catalog_model.selectedRecipeJson() != recipe_before_analysis) {
    return fail("analyze-only run should not mutate the selected recipe");
  }
  if (analysis.value(QStringLiteral("assetMetadata")).toMap().value(QStringLiteral("cameraLabel")).toString() !=
      QStringLiteral("Test Camera")) {
    return fail("analyze-only report should include selected asset metadata");
  }
  if (analysis.value(QStringLiteral("recipeSummary")).toMap().value(QStringLiteral("snapshotCount")).toInt() != 0) {
    return fail("analyze-only report should inspect the current recipe before edits");
  }
  if (analysis.value(QStringLiteral("editGoals")).toList().isEmpty() ||
      !analysis.value(QStringLiteral("riskAssessment")).toMap().contains(QStringLiteral("riskCount"))) {
    return fail("analyze-only report should name edit goals and current-image risks");
  }
  if (!QFileInfo(analysis.value(QStringLiteral("currentProofPath")).toString()).isFile() ||
      !QFileInfo(analysis.value(QStringLiteral("analysisReportPath")).toString()).isFile() ||
      !QFileInfo(analysis.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("analyze-only run should persist current proof, report, and manifest artifacts");
  }
  if (!analysis.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
    return fail("analyze-only report should validate the rendered current image");
  }
  const auto analysis_proof_source =
      analysis.value(QStringLiteral("proofRenderSource")).toMap();
  if (analysis_proof_source.value(QStringLiteral("sourceResolutionPolicy")).toString() !=
          QStringLiteral("bounded-preview") ||
      analysis_proof_source.value(QStringLiteral("sourceDecodeMaxEdge")).toInt() <= 0 ||
      !analysis_proof_source.value(QStringLiteral("usesDecodedImageCache")).toBool() ||
      analysis_proof_source.value(QStringLiteral("fullResolutionRequired")).toBool()) {
    return fail("analyze-only report should use a bounded cached preview source for proof rendering");
  }
  if (analysis.value(QStringLiteral("durationMs")).toInt() < 0 ||
      analysis.value(QStringLiteral("startedAtUtc")).toString().isEmpty() ||
      analysis.value(QStringLiteral("finishedAtUtc")).toString().isEmpty() ||
      analysis.value(QStringLiteral("toolCalls")).toList().isEmpty() ||
      analysis.value(QStringLiteral("performanceBudget"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("within-budget") ||
      !analysis.value(QStringLiteral("performanceProofGate"))
           .toMap()
           .value(QStringLiteral("passed"))
           .toBool()) {
    return fail("analyze-only report should include timing, performance budget, proof gate, and tool call metadata");
  }
  auto run_history = controller.runHistory();
  if (run_history.isEmpty() ||
      run_history.first().toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("analyze") ||
      run_history.first().toMap().value(QStringLiteral("manifestPath")).toString().isEmpty() ||
      !run_history.first().toMap().contains(QStringLiteral("durationMs")) ||
      run_history.first().toMap().value(QStringLiteral("durationMs")).toInt() < 0 ||
      run_history.first()
              .toMap()
              .value(QStringLiteral("performanceBudget"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("within-budget")) {
    return fail("agent controller should expose analyze runs in user-facing run history");
  }
  const auto persisted_analysis = read_json_object(analysis.value(QStringLiteral("analysisReportPath")).toString());
  if (persisted_analysis.value(QStringLiteral("mode")).toString() != QStringLiteral("analyze")) {
    return fail("analysis artifact should persist the read-only report");
  }
  const auto analysis_manifest = read_json_object(analysis.value(QStringLiteral("manifestPath")).toString());
  if (analysis_manifest.value(QStringLiteral("style"))
              .toObject()
              .value(QStringLiteral("schemaVersion"))
              .toInt() != 1 ||
      analysis_manifest.value(QStringLiteral("model"))
              .toObject()
              .value(QStringLiteral("backend"))
              .toString() != QStringLiteral("local")) {
    return fail("analysis manifest should persist style schema and model backend metadata");
  }
  if (analysis_manifest.value(QStringLiteral("toolCalls")).toArray().isEmpty() ||
      analysis_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0 ||
      analysis_manifest.value(QStringLiteral("performanceBudget"))
              .toObject()
              .value(QStringLiteral("budgetMs"))
              .toInt() <= 0 ||
      !analysis_manifest.value(QStringLiteral("performanceProofGate"))
           .toObject()
           .value(QStringLiteral("passed"))
           .toBool()) {
    return fail("analysis manifest should persist timing, performance proof gate, and tool call metadata");
  }

  const auto export_dir = QDir(runtime_root.path()).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(export_dir)) {
    return fail("export destination should be created for agent preflight testing");
  }
  const auto recipe_before_preflight = catalog_model.selectedRecipeJson();
  const auto preflight = controller.exportPreflightForSelected(
      QStringLiteral("Web Proof"),
      export_dir,
      QVariantMap{
          {QStringLiteral("histogramSampleCount"), 100},
          {QStringLiteral("shadowClipCount"), 0},
          {QStringLiteral("highlightClipCount"), 2},
      });
  if (!preflight.value(QStringLiteral("ok")).toBool() ||
      preflight.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      preflight.value(QStringLiteral("checkedCount")).toInt() != 1 ||
      preflight.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      preflight.value(QStringLiteral("warningCount")).toInt() < 2 ||
      preflight.value(QStringLiteral("assetReports")).toList().size() != 1 ||
      !QFileInfo(preflight.value(QStringLiteral("preflightReportPath")).toString()).isFile() ||
      !QFileInfo(preflight.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("agent export preflight should report and persist selected asset export readiness without creating a draft");
  }
  const auto persisted_preflight =
      read_json_object(preflight.value(QStringLiteral("preflightReportPath")).toString());
  if (persisted_preflight.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      persisted_preflight.value(QStringLiteral("assetReports")).toArray().size() != 1 ||
      persisted_preflight.value(QStringLiteral("toolCalls")).toArray().isEmpty() ||
      persisted_preflight.value(QStringLiteral("durationMs")).toInt(-1) < 0 ||
      !persisted_preflight.value(QStringLiteral("performanceBudget"))
           .toObject()
           .value(QStringLiteral("withinBudget"))
           .toBool()) {
    return fail("agent export preflight artifact should persist the report and performance budget");
  }
  const auto preflight_asset_report = preflight.value(QStringLiteral("assetReports")).toList().first().toMap();
  const auto preflight_parity =
      preflight_asset_report.value(QStringLiteral("previewExportParity")).toMap();
  if (preflight_asset_report.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
      preflight_asset_report.value(QStringLiteral("preset")).toString() != QStringLiteral("Web Proof") ||
      preflight_asset_report.value(QStringLiteral("destinationDirectory")).toString() != export_dir ||
      preflight_asset_report.value(QStringLiteral("status")).toString() != QStringLiteral("ready") ||
      !preflight_parity.value(QStringLiteral("required")).toBool() ||
      preflight_parity.value(QStringLiteral("status")).toString() != QStringLiteral("required-at-export") ||
      !preflight_parity.value(QStringLiteral("highRiskGroups"))
           .toStringList()
           .contains(QStringLiteral("output-profile-proofing"))) {
    return fail("agent export preflight should include per-asset preset, destination, status, and parity proof requirements");
  }
  if (catalog_model.selectedRecipeJson() != recipe_before_preflight) {
    return fail("agent export preflight should not mutate the selected recipe");
  }

  const auto unconfirmed_export = controller.queueExportForSelected(
      QStringLiteral("Web Proof"),
      export_dir,
      false,
      QVariantMap{{QStringLiteral("histogramSampleCount"), 100}});
  if (unconfirmed_export.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_export.value(QStringLiteral("queuedCount")).toInt() != 0 ||
      export_jobs.jobCount() != 0) {
    return fail("agent export should require explicit confirmation before queueing jobs");
  }
  const auto confirmed_export = controller.queueExportForSelected(
      QStringLiteral("Web Proof"),
      export_dir,
      true,
      QVariantMap{
          {QStringLiteral("histogramSampleCount"), 100},
          {QStringLiteral("highlightClipCount"), 2},
      });
  if (!confirmed_export.value(QStringLiteral("ok")).toBool() ||
      confirmed_export.value(QStringLiteral("mode")).toString() != QStringLiteral("export-batch") ||
      confirmed_export.value(QStringLiteral("queuedCount")).toInt() != 1 ||
      confirmed_export.value(QStringLiteral("expectedCount")).toInt() != 1 ||
      confirmed_export.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      export_jobs.jobCount() != 1 ||
      !QFileInfo(confirmed_export.value(QStringLiteral("exportBatchReportPath")).toString()).isFile() ||
      !QFileInfo(confirmed_export.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("confirmed agent export should queue and persist the selected asset through the export controller");
  }
  const auto persisted_export_batch =
      read_json_object(confirmed_export.value(QStringLiteral("exportBatchReportPath")).toString());
  if (persisted_export_batch.value(QStringLiteral("mode")).toString() != QStringLiteral("export-batch") ||
      persisted_export_batch.value(QStringLiteral("toolCalls")).toArray().size() < 2 ||
      persisted_export_batch.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("confirmed agent export artifact should persist timing and tool call outcomes");
  }

  const auto revised_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{
               {QStringLiteral("exposure"), 5.0},
               {QStringLiteral("highlights"), 100.0},
               {QStringLiteral("whites"), 100.0},
           }},
      });
  const auto revised_asset_reports = revised_draft.value(QStringLiteral("assetReports")).toList();
  const auto revised_asset_report =
      revised_asset_reports.isEmpty() ? QVariantMap{} : revised_asset_reports.first().toMap();
  if (!revised_draft.value(QStringLiteral("ok")).toBool() ||
      revised_asset_report.value(QStringLiteral("revisionCount")).toInt() < 1 ||
      revised_asset_report.value(QStringLiteral("validationIterations")).toList().size() < 2 ||
      revised_draft.value(QStringLiteral("requestedRecipePatch")).toMap() ==
          revised_draft.value(QStringLiteral("recipePatch")).toMap() ||
      !controller.draftActive()) {
    return fail("agent draft should revise unsafe patches from validation feedback before applying");
  }
  const auto revised_rollback = controller.rollbackDraft();
  if (!revised_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("revised validation-feedback draft should remain rollbackable");
  }

  const auto representative_proof = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{
               {QStringLiteral("exposure"), 0.20},
               {QStringLiteral("contrast"), 4.0},
           }},
          {QStringLiteral("crop"),
           QVariantMap{
               {QStringLiteral("cropX"), 0.10},
               {QStringLiteral("cropY"), 0.10},
               {QStringLiteral("cropWidth"), 0.80},
               {QStringLiteral("cropHeight"), 0.80},
               {QStringLiteral("cropZoom"), 1.02},
           }},
          {QStringLiteral("masks"),
           QVariantMap{
               {QStringLiteral("maskStack"),
                QVariantList{
                    QVariantMap{
                        {QStringLiteral("id"), QStringLiteral("representative-brush")},
                        {QStringLiteral("kind"), QStringLiteral("brush")},
                        {QStringLiteral("name"), QStringLiteral("Representative Brush")},
                        {QStringLiteral("operation"), QStringLiteral("add")},
                        {QStringLiteral("exposure"), 0.35},
                        {QStringLiteral("feather"), 0.08},
                        {QStringLiteral("brushPoints"),
                         QVariantList{
                             QVariantMap{
                                 {QStringLiteral("x"), 0.50},
                                 {QStringLiteral("y"), 0.48},
                                 {QStringLiteral("size"), 0.12},
                                 {QStringLiteral("flow"), 0.75},
                             },
                         }},
                    },
                }},
           }},
      });
  const auto representative_validation =
      representative_proof.value(QStringLiteral("validationReport")).toMap();
  const auto representative_artifacts =
      representative_validation.value(QStringLiteral("artifacts")).toMap();
  const auto representative_diff = representative_proof.value(QStringLiteral("recipeDiff")).toMap();
  const auto representative_mask_local_proof =
      representative_proof.value(QStringLiteral("maskLocalProof")).toMap();
  const auto representative_batch_summary =
      representative_proof.value(QStringLiteral("batchSummary")).toMap();
  if (!representative_proof.value(QStringLiteral("ok")).toBool() ||
      !QFileInfo(representative_artifacts.value(QStringLiteral("beforeProofPath")).toString()).isFile() ||
      !QFileInfo(representative_artifacts.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      representative_validation.value(QStringLiteral("proofWidth")).toInt() <= 0 ||
      representative_validation.value(QStringLiteral("proofHeight")).toInt() <= 0 ||
      !representative_diff.contains(QStringLiteral("globalTone")) ||
      !representative_diff.contains(QStringLiteral("crop")) ||
      !representative_diff.contains(QStringLiteral("masks")) ||
      !representative_mask_local_proof.value(QStringLiteral("required")).toBool() ||
      !representative_mask_local_proof.value(QStringLiteral("proofAvailable")).toBool() ||
      representative_mask_local_proof.value(QStringLiteral("uiProofSurface")).toString() !=
          QStringLiteral("agent-dialog-before-after-proof") ||
      representative_mask_local_proof.value(QStringLiteral("maskStackCount")).toInt() != 1 ||
      representative_mask_local_proof.value(QStringLiteral("localAdjustmentFieldCount")).toInt() <= 0 ||
      representative_batch_summary.value(QStringLiteral("maskLocalProofReadyCount")).toInt() != 1 ||
      !controller.draftActive()) {
    return fail("representative color, crop, and mask draft should render proof artifacts and grouped diffs");
  }
  const QImage representative_after(
      representative_artifacts.value(QStringLiteral("afterProofPath")).toString());
  if (representative_after.isNull()) {
    return fail("representative draft after-proof image should be readable");
  }
  const auto representative_rollback = controller.rollbackDraft();
  if (!representative_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("representative proof draft should remain rollbackable");
  }

  const auto report = controller.runDraftOnSelected(QString());
  if (!report.value(QStringLiteral("ok")).toBool()) {
    std::cerr << report.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("agent draft should succeed");
  }
  if (!controller.draftActive()) {
    return fail("successful agent run should leave a draft active");
  }
  if (report.value(QStringLiteral("runId")).toString().isEmpty()) {
    return fail("draft report should include a run id");
  }
  if (report.value(QStringLiteral("durationMs")).toInt() < 0 ||
      !report.contains(QStringLiteral("queueWaitMs")) ||
      report.value(QStringLiteral("queueWaitMs")).toInt() < 0 ||
      report.value(QStringLiteral("toolCalls")).toList().size() < 3 ||
      report.value(QStringLiteral("performanceBudget"))
              .toMap()
              .value(QStringLiteral("budgetMs"))
              .toInt() <= 0 ||
      !report.value(QStringLiteral("performanceProofGate"))
           .toMap()
           .value(QStringLiteral("passed"))
           .toBool()) {
    return fail("draft report should include run timing, queue wait timing, performance gate, and structured tool call outcomes");
  }
  if (!report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
    return fail("draft report should include a passing validation report");
  }
  const auto proof_render_policy =
      report.value(QStringLiteral("validationReport"))
          .toMap()
          .value(QStringLiteral("proofRenderPolicy"))
          .toMap();
  const auto draft_proof_source =
      report.value(QStringLiteral("validationReport"))
          .toMap()
          .value(QStringLiteral("proofRenderSource"))
          .toMap();
  const auto proof_render_checkpoints =
      proof_render_policy.value(QStringLiteral("checkpoints")).toStringList();
  if (!proof_render_policy.value(QStringLiteral("cancellable")).toBool() ||
      !proof_render_policy.value(QStringLiteral("timeoutAware")).toBool() ||
      !proof_render_checkpoints.contains(QStringLiteral("before-decode")) ||
      !proof_render_checkpoints.contains(QStringLiteral("before-validation"))) {
    return fail("draft validation should expose cancellable proof-render checkpoints");
  }
  if (draft_proof_source.value(QStringLiteral("sourceResolutionPolicy")).toString() !=
          QStringLiteral("bounded-preview") ||
      draft_proof_source.value(QStringLiteral("sourceDecodeMaxEdge")).toInt() <= 0 ||
      !draft_proof_source.value(QStringLiteral("usesDecodedImageCache")).toBool() ||
      draft_proof_source.value(QStringLiteral("fullResolutionRequired")).toBool()) {
    return fail("draft validation should use a bounded cached preview source for proof rendering");
  }
  const auto preview_proof_policy = report.value(QStringLiteral("previewProofPolicy")).toMap();
  const auto preview_asset_reports = report.value(QStringLiteral("assetReports")).toList();
  const auto preview_first_asset_report =
      preview_asset_reports.isEmpty() ? QVariantMap{} : preview_asset_reports.first().toMap();
  if (!preview_proof_policy.value(QStringLiteral("draftPreviewsDuringPlanning")).toBool() ||
      preview_proof_policy.value(QStringLiteral("draftPreviewCount")).toInt() <= 0 ||
      !preview_proof_policy.value(QStringLiteral("settledPreviewsForFinalProof")).toBool() ||
      preview_proof_policy.value(QStringLiteral("settledPreviewCount")).toInt() <= 0 ||
      !preview_first_asset_report.value(QStringLiteral("usesDraftPreviewsForPlanning")).toBool() ||
      preview_first_asset_report.value(QStringLiteral("draftPreviewCount")).toInt() <= 0 ||
      !preview_first_asset_report.value(QStringLiteral("usesSettledPreviewForFinalProof")).toBool() ||
      !preview_first_asset_report.value(QStringLiteral("settledPreviewReady")).toBool()) {
    return fail("draft report should prove iterative draft previews and settled final proof previews");
  }
  if (report.value(QStringLiteral("planSummary")).toString().isEmpty()) {
    return fail("draft report should include model plan summary");
  }
  if (report.value(QStringLiteral("editGoals")).toList().isEmpty() ||
      !report.value(QStringLiteral("riskAssessment")).toMap().contains(QStringLiteral("riskCount"))) {
    return fail("draft report should include explicit edit goals and risk assessment");
  }
  if (!report.value(QStringLiteral("reviewRequired")).toBool() ||
      report.value(QStringLiteral("qualityGuarantee")).toBool() ||
      report.value(QStringLiteral("reviewRequiredReason")).toString().isEmpty()) {
    return fail("draft report should require user review without promising perfect taste");
  }
  if (report.value(QStringLiteral("modelBackend")).toString() != QStringLiteral("local") ||
      !QFileInfo(report.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("draft report should include model backend and persist a run manifest");
  }
  if (report.value(QStringLiteral("modelRunnerMode")).toString() != QStringLiteral("worker-thread") ||
      report.value(QStringLiteral("uiEventPumpCount")).toInt() < 0) {
    return fail("draft report should show that model planning ran on the worker runner");
  }
  if (controller.recipeBaseMode() != QStringLiteral("current") ||
      report.value(QStringLiteral("recipeBaseMode")).toString() != QStringLiteral("current") ||
      report.value(QStringLiteral("manualEditPolicy")).toString() !=
          QStringLiteral("preserve-unspecified-current-recipe-fields")) {
    return fail("default agent draft should preserve unspecified fields from the current recipe");
  }
  const auto style_citations = report.value(QStringLiteral("styleConstraintCitations")).toList();
  if (style_citations.isEmpty() ||
      !has_citation_field(style_citations, QStringLiteral("constraints.max_exposure_shift")) ||
      !has_citation_field(
          style_citations,
          QStringLiteral("validation.highlight_clip_limit_percent"))) {
    return fail("draft report should include model style constraint citations");
  }
  const auto artifact_directory = report.value(QStringLiteral("artifactDirectory")).toString();
  const auto before_proof_path = report.value(QStringLiteral("beforeProofPath")).toString();
  const auto after_proof_path = report.value(QStringLiteral("afterProofPath")).toString();
  const auto recipe_diff_path = report.value(QStringLiteral("recipeDiffPath")).toString();
  const auto validation_report_path = report.value(QStringLiteral("validationReportPath")).toString();
  if (!QFileInfo::exists(artifact_directory) ||
      !QFileInfo(before_proof_path).isFile() ||
      !QFileInfo(after_proof_path).isFile() ||
      !QFileInfo(recipe_diff_path).isFile() ||
      !QFileInfo(validation_report_path).isFile()) {
    return fail("agent draft should persist before/after proof, recipe diff, and validation artifacts");
  }
  const auto recipe_diff = report.value(QStringLiteral("recipeDiff")).toMap();
  const auto exposure_diff = recipe_diff.value(QStringLiteral("globalTone"))
                                 .toMap()
                                 .value(QStringLiteral("exposure"))
                                 .toMap();
  if (exposure_diff.isEmpty() ||
      exposure_diff.value(QStringLiteral("before")).toDouble() != original_state.exposure ||
      exposure_diff.value(QStringLiteral("after")).toDouble() <= original_state.exposure) {
    return fail("draft report should include a grouped before/after recipe diff");
  }
  const auto persisted_diff = read_json_object(recipe_diff_path);
  if (!persisted_diff.contains(QStringLiteral("globalTone"))) {
    return fail("recipe diff artifact should contain grouped edit changes");
  }
  const auto persisted_validation = read_json_object(validation_report_path);
  if (!persisted_validation.value(QStringLiteral("ok")).toBool()) {
    return fail("validation artifact should contain a passing report");
  }
  const auto draft_manifest = read_json_object(report.value(QStringLiteral("manifestPath")).toString());
  if (draft_manifest.value(QStringLiteral("mode")).toString() != QStringLiteral("draft") ||
      draft_manifest.value(QStringLiteral("styleConstraintCitations")).toArray().isEmpty() ||
      draft_manifest.value(QStringLiteral("editGoals")).toArray().isEmpty() ||
      !draft_manifest.value(QStringLiteral("riskAssessment")).toObject().contains(QStringLiteral("riskCount")) ||
      draft_manifest.value(QStringLiteral("toolCalls")).toArray().size() < 3 ||
      draft_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0 ||
      draft_manifest.value(QStringLiteral("queueWaitMs")).toInt(-1) < 0 ||
      draft_manifest.value(QStringLiteral("performanceBudget"))
              .toObject()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("within-budget") ||
      !draft_manifest.value(QStringLiteral("performanceProofGate"))
           .toObject()
           .value(QStringLiteral("passed"))
           .toBool() ||
      draft_manifest.value(QStringLiteral("modelRunnerMode")).toString() !=
          QStringLiteral("worker-thread") ||
      !draft_manifest.value(QStringLiteral("reviewRequired")).toBool() ||
      draft_manifest.value(QStringLiteral("qualityGuarantee")).toBool() ||
      draft_manifest.value(QStringLiteral("recipeBaseMode")).toString() != QStringLiteral("current") ||
      draft_manifest.value(QStringLiteral("manualEditPolicy")).toString() !=
          QStringLiteral("preserve-unspecified-current-recipe-fields") ||
      draft_manifest.value(QStringLiteral("recipeStorageMode")).toString() !=
          QStringLiteral("recipe-diff-first") ||
      !draft_manifest.value(QStringLiteral("previewProofPolicy"))
           .toObject()
           .value(QStringLiteral("draftPreviewsDuringPlanning"))
           .toBool() ||
      !draft_manifest.value(QStringLiteral("previewProofPolicy"))
           .toObject()
           .value(QStringLiteral("settledPreviewsForFinalProof"))
           .toBool() ||
      draft_manifest.value(QStringLiteral("recipeDiff")).toObject().isEmpty() ||
      draft_manifest.value(QStringLiteral("model"))
              .toObject()
              .value(QStringLiteral("backend"))
              .toString() != QStringLiteral("local")) {
    return fail("draft manifest should persist mode, model metadata, style citations, preview proof policy, and performance proof gate");
  }
  const auto shareable_manifest_path = report.value(QStringLiteral("shareableManifestPath")).toString();
  const auto shareable_manifest_bytes = read_file_bytes(shareable_manifest_path);
  const auto shareable_manifest = QJsonDocument::fromJson(shareable_manifest_bytes).object();
  if (shareable_manifest_path.isEmpty() ||
      !QFileInfo(shareable_manifest_path).isFile() ||
      !shareable_manifest.value(QStringLiteral("redacted")).toBool() ||
      shareable_manifest.value(QStringLiteral("mode")).toString() != QStringLiteral("draft") ||
      shareable_manifest.value(QStringLiteral("styleConstraintCitations")).toArray().isEmpty()) {
    return fail("draft run should persist a redacted shareable manifest with useful run metadata");
  }
  if (shareable_manifest_bytes.contains(runtime_root.path().toUtf8()) ||
      shareable_manifest_bytes.contains(source_path.toUtf8()) ||
      shareable_manifest_bytes.contains(before_proof_path.toUtf8()) ||
      !shareable_manifest_bytes.contains("<app-data>")) {
    return fail("shareable draft manifest should redact absolute app-data and source paths");
  }

  const auto drafted_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (drafted_state.exposure <= original_state.exposure) {
    return fail("agent draft should update exposure");
  }
  if (drafted_state.temperature <= original_state.temperature) {
    return fail("agent draft should update white balance temperature");
  }
  if (drafted_state.output_color_space != original_state.output_color_space) {
    return fail("current-base agent draft should preserve manual recipe fields outside the patch");
  }
  if (drafted_state.recipe_snapshots.empty()) {
    return fail("agent draft should snapshot the original recipe before applying changes");
  }
  const auto agent_snapshot = drafted_state.recipe_snapshots.back().toMap();
  if (!agent_snapshot.value(QStringLiteral("agentGenerated")).toBool() ||
      agent_snapshot.value(QStringLiteral("agentRunId")).toString() != report.value(QStringLiteral("runId")).toString() ||
      agent_snapshot.value(QStringLiteral("agentModelId")).toString() != report.value(QStringLiteral("modelId")).toString() ||
      agent_snapshot.value(QStringLiteral("agentStyleId")).toString() != controller.selectedStyleId() ||
      agent_snapshot.value(QStringLiteral("agentCreatedAtUtc")).toString().isEmpty()) {
    return fail("agent draft snapshot should include run, model, style, and timestamp provenance");
  }
  if (report.value(QStringLiteral("recipeStorageMode")).toString() != QStringLiteral("recipe-diff-first") ||
      agent_snapshot.value(QStringLiteral("agentChangeStorage")).toString() !=
          QStringLiteral("recipe-diff-first") ||
      agent_snapshot.value(QStringLiteral("agentRecipePatch")).toMap().isEmpty() ||
      agent_snapshot.value(QStringLiteral("agentRecipeDiff")).toMap().isEmpty()) {
    return fail("agent draft should store change provenance as recipe diffs before full snapshots");
  }

  const auto user_revision = controller.runDraftOnSelected(QStringLiteral("revise the draft warmer"));
  if (!user_revision.value(QStringLiteral("ok")).toBool() ||
      user_revision.value(QStringLiteral("runId")).toString() != report.value(QStringLiteral("runId")).toString() ||
      !QFileInfo(user_revision.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      !controller.draftActive()) {
    return fail("user-requested draft revision should reuse the active draft transaction");
  }
  const auto revised_drafted_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (revised_drafted_state.recipe_snapshots.size() < drafted_state.recipe_snapshots.size()) {
    return fail("user-requested draft revision should preserve agent draft provenance");
  }

  const auto rollback = controller.rollbackDraft();
  if (!rollback.value(QStringLiteral("ok")).toBool() ||
      rollback.value(QStringLiteral("mode")).toString() != QStringLiteral("rollback")) {
    return fail("rollback should succeed");
  }
  run_history = controller.runHistory();
  if (run_history.isEmpty() ||
      run_history.first().toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("rollback")) {
    return fail("agent run history should record draft rollbacks as the latest run event");
  }
  if (controller.draftActive()) {
    return fail("rollback should clear the active draft");
  }

  const auto restored_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  if (restored_state.exposure != original_state.exposure ||
      restored_state.temperature != original_state.temperature ||
      restored_state.saturation != original_state.saturation) {
    return fail("rollback should restore the original recipe values");
  }

  controller.setRecipeBaseMode(QStringLiteral("neutral"));
  const auto neutral_report = controller.runDraftOnSelected(QStringLiteral("clean neutral draft"));
  if (!neutral_report.value(QStringLiteral("ok")).toBool() ||
      neutral_report.value(QStringLiteral("recipeBaseMode")).toString() != QStringLiteral("neutral") ||
      neutral_report.value(QStringLiteral("manualEditPolicy")).toString() !=
          QStringLiteral("replace-current-edits-with-neutral-base")) {
    return fail("neutral-base agent draft should report that it replaces current edits with a neutral base");
  }
  const auto neutral_drafted_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.selectedRecipeJson());
  const photo_editor::desktop::EditRecipeState neutral_defaults;
  if (neutral_drafted_state.output_color_space != neutral_defaults.output_color_space) {
    return fail("neutral-base agent draft should start from neutral recipe fields outside the patch");
  }
  const auto neutral_snapshot = neutral_drafted_state.recipe_snapshots.back().toMap();
  const auto neutral_snapshot_state =
      photo_editor::desktop::parse_edit_recipe_json(neutral_snapshot.value(QStringLiteral("recipeJson")).toString());
  if (neutral_snapshot_state.output_color_space != original_state.output_color_space) {
    return fail("neutral-base agent draft should still snapshot the original current recipe for rollback");
  }
  const auto neutral_rollback = controller.rollbackDraft();
  if (!neutral_rollback.value(QStringLiteral("ok")).toBool()) {
    return fail("neutral-base draft rollback should succeed");
  }
  controller.setRecipeBaseMode(QStringLiteral("current"));

  catalog_model.toggleSelectionIndex(1);
  const auto recipe_before_batch_validation = catalog_model.selectedRecipeJson();
  const auto batch_validation = controller.validateBatchForSelected();
  if (!batch_validation.value(QStringLiteral("ok")).toBool() ||
      batch_validation.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-batch") ||
      batch_validation.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      batch_validation.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      batch_validation.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      batch_validation.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      !QFileInfo(batch_validation.value(QStringLiteral("batchValidationReportPath")).toString()).isFile() ||
      !QFileInfo(batch_validation.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("selected-set batch validation should persist per-asset reports without creating a draft");
  }
  for (const auto& asset_report_value : batch_validation.value(QStringLiteral("assetReports")).toList()) {
    const auto asset_report = asset_report_value.toMap();
    if (asset_report.value(QStringLiteral("status")).toString() != QStringLiteral("validated") ||
        !QFileInfo(asset_report.value(QStringLiteral("currentProofPath")).toString()).isFile() ||
        !QFileInfo(asset_report.value(QStringLiteral("analysisReportPath")).toString()).isFile() ||
        !asset_report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
      return fail("selected-set batch validation should persist each asset proof and validation report");
    }
  }
  if (catalog_model.selectedRecipeJson() != recipe_before_batch_validation) {
    return fail("selected-set batch validation should not mutate recipes");
  }

  const auto disallowed_global_crop = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("crop"),
           QVariantMap{
               {QStringLiteral("cropZoom"), 1.1},
           }},
      });
  if (disallowed_global_crop.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive() ||
      !disallowed_global_crop.value(QStringLiteral("errors"))
           .toStringList()
           .join(QStringLiteral("; "))
           .contains(QStringLiteral("cannot apply crop edits globally"))) {
    return fail("selected-set drafts should reject global crop edits unless style batch behavior allows them");
  }

  const auto batch_report = controller.runDraftOnSelected(QStringLiteral("apply the style consistently"));
  if (!batch_report.value(QStringLiteral("ok")).toBool()) {
    std::cerr << batch_report.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("agent selected-set draft should succeed");
  }
  if (batch_report.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      batch_report.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      batch_report.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      batch_report.value(QStringLiteral("assetReports")).toList().size() != 2) {
    return fail("selected-set draft should report batch scope and per-asset results");
  }
  const auto batch_summary = batch_report.value(QStringLiteral("batchSummary")).toMap();
  const auto batch_status_counts = batch_summary.value(QStringLiteral("statusCounts")).toMap();
  const auto batch_transaction = batch_report.value(QStringLiteral("batchTransaction")).toMap();
  const auto batch_clusters = batch_report.value(QStringLiteral("batchClusters")).toList();
  const auto batch_cluster_plans = batch_report.value(QStringLiteral("clusterPlanning")).toList();
  if (batch_summary.value(QStringLiteral("assetCount")).toInt() != 2 ||
      batch_summary.value(QStringLiteral("clusterCount")).toInt() != 2 ||
      batch_status_counts.value(QStringLiteral("drafted")).toInt() != 2 ||
      batch_summary.value(QStringLiteral("proofCount")).toInt() != 2 ||
      batch_summary.value(QStringLiteral("recipeDiffCount")).toInt() != 2 ||
      batch_clusters.size() != 2 ||
      batch_cluster_plans.size() != 2 ||
      batch_report.value(QStringLiteral("representativeAssetIds")).toStringList().size() != 2 ||
      !batch_summary.value(QStringLiteral("readyForCommit")).toBool() ||
      !batch_summary.value(QStringLiteral("confirmationRequired")).toBool() ||
      batch_transaction.value(QStringLiteral("transactionId")).toString() !=
          batch_report.value(QStringLiteral("runId")).toString() ||
      batch_transaction.value(QStringLiteral("status")).toString() != QStringLiteral("drafted") ||
      !batch_transaction.value(QStringLiteral("rollbackAvailable")).toBool() ||
      batch_transaction.value(QStringLiteral("assetCount")).toInt() != 2) {
    return fail("selected-set draft should expose a pre-commit batch summary");
  }
  QFile batch_manifest_file(batch_report.value(QStringLiteral("manifestPath")).toString());
  if (!batch_manifest_file.open(QIODevice::ReadOnly)) {
    return fail("selected-set draft manifest should be readable");
  }
  const auto batch_manifest_document =
      QJsonDocument::fromJson(batch_manifest_file.readAll()).object();
  if (batch_manifest_document.value(QStringLiteral("batchSummary"))
          .toObject()
          .value(QStringLiteral("assetCount"))
          .toInt() != 2 ||
      batch_manifest_document.value(QStringLiteral("batchClusters")).toArray().size() != 2 ||
      batch_manifest_document.value(QStringLiteral("clusterPlanning")).toArray().size() != 2 ||
      batch_manifest_document.value(QStringLiteral("batchTransaction"))
              .toObject()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("drafted") ||
      batch_manifest_document.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1) {
    return fail("selected-set draft manifest should persist the pre-commit batch summary and transaction");
  }
  const auto batch_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString());
  const auto batch_second_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString());
  if (batch_first_state.exposure <= original_state.exposure ||
      batch_second_state.exposure <= second_original_state.exposure) {
    return fail("selected-set draft should update every selected recipe");
  }
  const auto batch_asset_reports = batch_report.value(QStringLiteral("assetReports")).toList();
  for (const auto& asset_report_value : batch_asset_reports) {
    const auto asset_report = asset_report_value.toMap();
    const auto validation_report = asset_report.value(QStringLiteral("validationReport")).toMap();
    const auto artifacts = validation_report.value(QStringLiteral("artifacts")).toMap();
    if (asset_report.value(QStringLiteral("status")).toString() != QStringLiteral("drafted") ||
        asset_report.value(QStringLiteral("clusterId")).toString().isEmpty() ||
        asset_report.value(QStringLiteral("clusterRepresentativeAssetId")).toString().isEmpty() ||
        asset_report.value(QStringLiteral("clusterSyncGroups")).toStringList().isEmpty() ||
        !QFileInfo(artifacts.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
        validation_report.value(QStringLiteral("recipeDiff")).toMap().isEmpty()) {
      return fail("selected-set draft should persist per-asset cluster metadata, proof artifacts, and diffs");
    }
  }
  const auto batch_rollback = controller.rollbackDraft();
  if (!batch_rollback.value(QStringLiteral("ok")).toBool() ||
      batch_rollback.value(QStringLiteral("restoredCount")).toInt() != 2 ||
      batch_rollback.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("rolled_back") ||
      batch_rollback.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("assetCount"))
              .toInt() != 2) {
    return fail("selected-set rollback should restore both drafted recipes and report the transaction");
  }
  const auto restored_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString());
  const auto restored_second_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString());
  if (restored_first_state.exposure != original_state.exposure ||
      restored_second_state.exposure != second_original_state.exposure) {
    return fail("selected-set rollback should restore each asset's original recipe");
  }

  catalog_model.addAssetToProject(QStringLiteral("asset-1"), QStringLiteral("Agent Shoot"));
  catalog_model.addAssetToProject(QStringLiteral("asset-2"), QStringLiteral("Agent Shoot"));
  catalog_model.openProject(QStringLiteral("Agent Shoot"));
  const auto project_validation = controller.validateBatchForScope(QStringLiteral("project"));
  if (!project_validation.value(QStringLiteral("ok")).toBool() ||
      project_validation.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-batch") ||
      project_validation.value(QStringLiteral("scope")).toString() != QStringLiteral("project") ||
      project_validation.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      project_validation.value(QStringLiteral("assetIds")).toStringList().size() != 2 ||
      project_validation.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("assetCount")).toInt() != 2 ||
      project_validation.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      controller.draftActive()) {
    return fail("project scope validation should cover the current album/project without creating a draft");
  }

  const auto project_preflight =
      controller.exportPreflightForScope(QStringLiteral("project"), QStringLiteral("Web Proof"), export_dir, {});
  if (!project_preflight.value(QStringLiteral("ok")).toBool() ||
      project_preflight.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      project_preflight.value(QStringLiteral("scope")).toString() != QStringLiteral("project") ||
      project_preflight.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      project_preflight.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      !QFileInfo(project_preflight.value(QStringLiteral("preflightReportPath")).toString()).isFile() ||
      !QFileInfo(project_preflight.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("project scope export preflight should inspect every project asset without creating a draft");
  }

  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.setSearchFilter(QStringLiteral("source-2"));
  const auto filtered_report = controller.runDraftForScope(
      QStringLiteral("filtered-library"),
      QStringLiteral("apply a filtered one-photo draft"));
  if (!filtered_report.value(QStringLiteral("ok")).toBool() ||
      filtered_report.value(QStringLiteral("scope")).toString() != QStringLiteral("filtered-library") ||
      filtered_report.value(QStringLiteral("draftedCount")).toInt() != 1 ||
      filtered_report.value(QStringLiteral("assetIds")).toStringList() != QStringList{QStringLiteral("asset-2")} ||
      filtered_report.value(QStringLiteral("assetReports")).toList().size() != 1 ||
      !controller.draftActive()) {
    return fail("filtered-library scope draft should apply only the currently visible filtered asset");
  }
  const auto filtered_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetsForProject(QStringLiteral("all"))
                                                        .at(0)
                                                        .toMap()
                                                        .value(QStringLiteral("recipeJson"))
                                                        .toString());
  const auto filtered_visible_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.visibleAssets()
                                                        .first()
                                                        .toMap()
                                                        .value(QStringLiteral("recipeJson"))
                                                        .toString());
  if (filtered_first_state.exposure != original_state.exposure ||
      filtered_visible_state.exposure <= second_original_state.exposure) {
    return fail("filtered-library draft should leave hidden assets unchanged and update the visible target");
  }
  const auto filtered_rollback = controller.rollbackDraft();
  if (!filtered_rollback.value(QStringLiteral("ok")).toBool() ||
      filtered_rollback.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      controller.draftActive()) {
    return fail("filtered-library rollback should restore the one scoped draft");
  }
  catalog_model.setSearchFilter(QString());
  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  catalog_model.toggleSelectionIndex(1);

  const auto second_batch_report = controller.runDraftOnSelected(QStringLiteral("commit the style consistently"));
  if (!second_batch_report.value(QStringLiteral("ok")).toBool() ||
      second_batch_report.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      second_batch_report.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      !controller.draftActive()) {
    return fail("selected-set draft should be ready for commit verification");
  }
  const auto unconfirmed_commit_batch = controller.commitDraft();
  if (unconfirmed_commit_batch.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_commit_batch.value(QStringLiteral("committedCount")).toInt() != 0 ||
      !unconfirmed_commit_batch.value(QStringLiteral("confirmationRequired")).toBool() ||
      !unconfirmed_commit_batch.value(QStringLiteral("draftActive")).toBool() ||
      unconfirmed_commit_batch.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      unconfirmed_commit_batch.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      unconfirmed_commit_batch.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("statusCounts"))
              .toMap()
              .value(QStringLiteral("drafted"))
              .toInt() != 2 ||
      unconfirmed_commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("awaiting_commit_confirmation") ||
      !unconfirmed_commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("rollbackAvailable"))
              .toBool() ||
      !controller.draftActive()) {
    return fail("selected-set commit should require explicit confirmation while preserving the batch summary");
  }
  const auto commit_batch = controller.commitDraft(true);
  if (!commit_batch.value(QStringLiteral("ok")).toBool() ||
      commit_batch.value(QStringLiteral("mode")).toString() != QStringLiteral("commit") ||
      commit_batch.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      commit_batch.value(QStringLiteral("committedCount")).toInt() != 2 ||
      !commit_batch.value(QStringLiteral("confirmed")).toBool() ||
      commit_batch.value(QStringLiteral("assetIds")).toStringList().size() != 2 ||
      commit_batch.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      commit_batch.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("statusCounts"))
              .toMap()
              .value(QStringLiteral("committed"))
              .toInt() != 2 ||
      commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("committed") ||
      commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("rollbackAvailable"))
              .toBool() ||
      commit_batch.value(QStringLiteral("draftActive")).toBool() ||
      controller.draftActive()) {
    return fail("selected-set commit should report and clear a two-asset draft transaction");
  }
  const auto committed_first_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString());
  const auto committed_second_state =
      photo_editor::desktop::parse_edit_recipe_json(catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString());
  if (committed_first_state.exposure <= original_state.exposure ||
      committed_second_state.exposure <= second_original_state.exposure) {
    return fail("selected-set commit should preserve drafted recipe changes");
  }
  run_history = controller.runHistory();
  if (run_history.size() < 5 ||
      run_history.first().toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("commit") ||
      run_history.first().toMap().value(QStringLiteral("assetCount")).toInt() != 2 ||
      run_history.size() > 20) {
    return fail("agent run history should retain recent user-facing run summaries newest-first");
  }
  const auto empty_commit = controller.commitDraft(true);
  if (empty_commit.value(QStringLiteral("ok")).toBool() ||
      empty_commit.value(QStringLiteral("committedCount")).toInt() != 0 ||
      controller.draftActive()) {
    return fail("commit without an active draft should fail without mutating state");
  }

  const auto source_recipe_before_virtual_copy =
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString();
  const auto virtual_copy_id =
      catalog_model.createVirtualCopyForAsset(QStringLiteral("asset-1"), QStringLiteral("Agent Variant"));
  if (virtual_copy_id.isEmpty()) {
    return fail("agent virtual-copy independence test should be able to create a variant");
  }
  auto virtual_copy_row = -1;
  for (auto row = 0; row < catalog_model.assetCount(); ++row) {
    if (catalog_model.assetAt(row).value(QStringLiteral("assetId")).toString() == virtual_copy_id) {
      virtual_copy_row = row;
      break;
    }
  }
  if (virtual_copy_row < 0) {
    return fail("agent virtual-copy variant should be visible in the catalog");
  }
  catalog_model.selectIndex(virtual_copy_row);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(virtual_copy_row);
  const auto virtual_copy_report =
      controller.runDraftForScope(QStringLiteral("selected-assets"), QStringLiteral("edit the virtual copy only"));
  const auto virtual_copy_asset_reports = virtual_copy_report.value(QStringLiteral("assetReports")).toList();
  const auto virtual_copy_batch_summary = virtual_copy_report.value(QStringLiteral("batchSummary")).toMap();
  const auto variant_families = virtual_copy_batch_summary.value(QStringLiteral("variantFamilies")).toList();
  if (!virtual_copy_report.value(QStringLiteral("ok")).toBool() ||
      virtual_copy_report.value(QStringLiteral("draftedCount")).toInt() != 1 ||
      virtual_copy_asset_reports.size() != 1 ||
      !virtual_copy_asset_reports.first().toMap().value(QStringLiteral("virtualCopy")).toBool() ||
      virtual_copy_asset_reports.first().toMap().value(QStringLiteral("sourceAssetId")).toString() !=
          QStringLiteral("asset-1") ||
      virtual_copy_asset_reports.first().toMap().value(QStringLiteral("virtualCopyPolicy")).toString() !=
          QStringLiteral("target-asset-id-only") ||
      virtual_copy_batch_summary.value(QStringLiteral("virtualCopyPolicy")).toString() !=
          QStringLiteral("target-asset-id-only") ||
      variant_families.size() != 1 ||
      variant_families.first().toMap().value(QStringLiteral("sourceAssetId")).toString() !=
          QStringLiteral("asset-1") ||
      variant_families.first().toMap().value(QStringLiteral("virtualCopyAssetIds")).toStringList() !=
          QStringList{virtual_copy_id} ||
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString() !=
          source_recipe_before_virtual_copy ||
      !controller.draftActive()) {
    return fail("agent virtual-copy draft should report variant metadata and leave the source recipe untouched");
  }
  if (catalog_model.assetAt(virtual_copy_row).value(QStringLiteral("recipeJson")).toString() ==
      source_recipe_before_virtual_copy) {
    return fail("agent virtual-copy draft should edit the variant recipe");
  }
  const auto virtual_copy_rollback = controller.rollbackDraft();
  if (!virtual_copy_rollback.value(QStringLiteral("ok")).toBool() ||
      virtual_copy_rollback.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString() !=
          source_recipe_before_virtual_copy ||
      controller.draftActive()) {
    return fail("agent virtual-copy rollback should restore only the drafted variant");
  }
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  catalog_model.toggleSelectionIndex(1);

  const auto first_recipe_before_cancel =
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString();
  const auto second_recipe_before_cancel =
      catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString();
  QTimer::singleShot(0, [&controller]() {
    controller.cancelRun(QStringLiteral("user_stop"));
  });
  const auto user_canceled_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{{QStringLiteral("exposure"), 0.2}}},
      });
  if (user_canceled_draft.value(QStringLiteral("ok")).toBool() ||
      user_canceled_draft.value(QStringLiteral("cancellationReason")).toString() !=
          QStringLiteral("user_stop") ||
      !user_canceled_draft.value(QStringLiteral("rollbackOnCancel")).toBool() ||
      user_canceled_draft.value(QStringLiteral("partialDraftRollback")).toString() !=
          QStringLiteral("nothing_applied") ||
      user_canceled_draft.value(QStringLiteral("appliedCount")).toInt() != 0 ||
      user_canceled_draft.value(QStringLiteral("restoredCount")).toInt() != 0 ||
      user_canceled_draft.value(QStringLiteral("draftActive")).toBool() ||
      controller.draftActive() ||
      controller.cancellationRequested() ||
      catalog_model.assetAt(0).value(QStringLiteral("recipeJson")).toString() !=
          first_recipe_before_cancel ||
      catalog_model.assetAt(1).value(QStringLiteral("recipeJson")).toString() !=
          second_recipe_before_cancel ||
      !QFileInfo(user_canceled_draft.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("user-canceled agent draft should stop and report rollback of any partial draft writes");
  }
  const auto user_canceled_manifest =
      read_json_object(user_canceled_draft.value(QStringLiteral("manifestPath")).toString());
  if (user_canceled_manifest.value(QStringLiteral("cancellationReason")).toString() !=
          QStringLiteral("user_stop") ||
      !user_canceled_manifest.value(QStringLiteral("rollbackOnCancel")).toBool() ||
      user_canceled_manifest.value(QStringLiteral("partialDraftRollback")).toString() !=
          QStringLiteral("nothing_applied") ||
      user_canceled_manifest.value(QStringLiteral("batchTransaction"))
              .toObject()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("canceled_rolled_back")) {
    return fail("user-canceled draft manifest should persist rollback-on-cancel transaction metadata");
  }

  controller.setRunTimeoutMs(0);
  const auto timed_out_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{{QStringLiteral("exposure"), 0.2}}},
      });
  if (timed_out_draft.value(QStringLiteral("ok")).toBool() ||
      timed_out_draft.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_draft.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      !QFileInfo(timed_out_draft.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive() ||
      controller.cancellationRequested()) {
    return fail("agent draft timeout should record cancellation metadata without leaving an active draft");
  }
  const auto timed_out_manifest =
      read_json_object(timed_out_draft.value(QStringLiteral("manifestPath")).toString());
  if (timed_out_manifest.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      timed_out_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("timed-out draft manifest should persist cancellation reason, failure reason, and timing");
  }
  const auto timed_out_batch_validation = controller.validateBatchForSelected();
  if (timed_out_batch_validation.value(QStringLiteral("ok")).toBool() ||
      timed_out_batch_validation.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_batch_validation.value(QStringLiteral("checkedCount"), -1).toInt() != 0 ||
      !QFileInfo(timed_out_batch_validation.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.cancellationRequested()) {
    return fail("batch validation timeout should stop before processing assets and persist cancellation metadata");
  }
  const auto timed_out_batch_manifest =
      read_json_object(timed_out_batch_validation.value(QStringLiteral("manifestPath")).toString());
  if (timed_out_batch_manifest.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_batch_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      timed_out_batch_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("timed-out batch validation manifest should persist cancellation reason, failure reason, and timing");
  }
  const auto timed_out_preflight =
      controller.exportPreflightForSelected(QString(), QString(), {});
  if (timed_out_preflight.value(QStringLiteral("ok")).toBool() ||
      timed_out_preflight.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_preflight.value(QStringLiteral("checkedCount"), -1).toInt() != 0 ||
      !QFileInfo(timed_out_preflight.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.cancellationRequested()) {
    return fail("export preflight timeout should stop before processing assets and persist cancellation metadata");
  }
  const auto timed_out_preflight_manifest =
      read_json_object(timed_out_preflight.value(QStringLiteral("manifestPath")).toString());
  if (timed_out_preflight_manifest.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_preflight_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      timed_out_preflight_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("timed-out export preflight manifest should persist cancellation reason, failure reason, and timing");
  }
  controller.setRunTimeoutMs(300000);

  catalog_model.replaceAssetsForTesting({
      make_asset(
          QStringLiteral("asset-missing"),
          QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/missing.png")),
          original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.toggleSelectionIndex(0);
  const auto missing_source_validation = controller.validateBatchForSelected();
  if (missing_source_validation.value(QStringLiteral("ok")).toBool() ||
      missing_source_validation.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      !QFileInfo(missing_source_validation.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("missing source batch validation should fail with persisted failure metadata");
  }
  const auto missing_source_manifest =
      read_json_object(missing_source_validation.value(QStringLiteral("manifestPath")).toString());
  if (missing_source_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      missing_source_manifest.value(QStringLiteral("errors")).toArray().isEmpty() ||
      missing_source_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("failed batch validation manifest should record failure reason, errors, and timing");
  }

  if (observability_log_path.isEmpty() ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.run_started")) < 4 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.tool_call")) < 4 ||
      observability_event_count(
          observability_log_path,
          QStringLiteral("agent.edit.proof_artifacts_written")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.validation_failed")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.draft_applied")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.commit")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.rollback")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.export_preflight")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.export_queued")) < 1) {
    return fail("agent controller should write observability events for runs, tools, validation, proof, draft, commit, rollback, and export");
  }
  if (read_file_bytes(source_path) != original_source_bytes ||
      read_file_bytes(second_source_path) != original_second_source_bytes) {
    return fail("agent workflows must not overwrite source image files");
  }

  return EXIT_SUCCESS;
}

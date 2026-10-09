#include "agent_task_coordinator.h"
#include "agent_task_recipe_persistence.h"
#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#include <QTemporaryDir>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <functional>
#include <iostream>
#include <utility>

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

bool write_receipt(const QString& path, const QJsonObject& receipt) {
  QSaveFile file(path);
  const auto bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool wait_until(const std::function<bool()>& done, int timeout_ms = 120000) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!done() && elapsed.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return done();
}

bool guarded_helper_payload(const QByteArray& payload, const QString& asset_id,
                            const QString& source_path, const QString& source_sha256) {
  const auto envelope = QJsonDocument::fromJson(payload).object();
  const auto arguments = envelope.value(QStringLiteral("arguments")).toArray();
  if (envelope.value(QStringLiteral("schema")) !=
          QStringLiteral("photo-editor-catalog-arguments:v1") ||
      arguments.size() < 4 ||
      arguments.at(0).toString() != QStringLiteral("--agent-owned-v1"))
    return false;
  const auto snapshot = QJsonDocument::fromJson(arguments.at(1).toString().toUtf8()).object();
  return snapshot.value(QStringLiteral("schema")) == QStringLiteral("agent-asset-recipe:v1") &&
      snapshot.value(QStringLiteral("state")) == QStringLiteral("present") &&
      snapshot.value(QStringLiteral("asset_id")).toString() == asset_id &&
      QFileInfo(snapshot.value(QStringLiteral("source_path")).toString()).canonicalFilePath() == source_path &&
      snapshot.value(QStringLiteral("source_sha256")).toString() == source_sha256;
}

bool export_preview(photo_editor::desktop::EditorPreviewController& preview,
                    const QString& path, QString* error) {
  if (!preview.startFlattenedPhotoExport(path)) {
    *error = QStringLiteral("The native preview export was not admitted.");
    return false;
  }
  if (!wait_until([&preview] { return !preview.photoExportBusy(); })) {
    *error = QStringLiteral("The native preview export did not settle.");
    return false;
  }
  if (!preview.photoExportError().isEmpty()) {
    *error = preview.photoExportError();
    return false;
  }
  return QFileInfo(path).isFile();
}

}  // namespace

int main(int argc, char** argv) {
  // Opt in with a private runtime root containing an imported test asset. The
  // catalog database and recipe rows are never inferred from the normal profile.
  if (argc != 4) return 77;
  const QDir fixture_root(QString::fromLocal8Bit(argv[1]));
  const auto asset_id = QString::fromLocal8Bit(argv[2]).trimmed();
  const QFileInfo source_info(QString::fromLocal8Bit(argv[3]));
  const auto test_database_url =
      qEnvironmentVariable("PHOTO_EDITOR_AGENT_RECIPE_PERSISTENCE_TEST_DATABASE_URL").trimmed();
  const QUrl database_authority(test_database_url);
  if (!fixture_root.isAbsolute() || asset_id.isEmpty() || !source_info.isFile() ||
      !QFileInfo::exists(fixture_root.filePath(QStringLiteral("agent-recipe-persistence.marker"))))
    return 77;
  const auto canonical_fixture_root = fixture_root.canonicalPath();
  const auto source_path = source_info.canonicalFilePath();
  const auto relative_source_path = QDir(canonical_fixture_root).relativeFilePath(source_path);
  if (canonical_fixture_root.isEmpty() || source_path.isEmpty() ||
      relative_source_path.isEmpty() || relative_source_path == QStringLiteral("..") ||
      relative_source_path.startsWith(QStringLiteral("../")) ||
      QDir::isAbsolutePath(relative_source_path))
    return 77;
  if (!database_authority.isValid() ||
      !QStringList{QStringLiteral("postgres"), QStringLiteral("postgresql")}
           .contains(database_authority.scheme(), Qt::CaseInsensitive) ||
      !database_authority.path().contains(QStringLiteral("test"), Qt::CaseInsensitive))
    return 77;

  QTemporaryDir temporary_runtime(fixture_root.filePath(QStringLiteral("runtime-XXXXXX")));
  if (!temporary_runtime.isValid()) return fail(QStringLiteral("Could not create temporary runtime data."));
  temporary_runtime.setAutoRemove(false);

  QFile source_file(source_path);
  if (!source_file.open(QIODevice::ReadOnly))
    return fail(QStringLiteral("Could not hash the private fixture source image."));
  QCryptographicHash source_hasher(QCryptographicHash::Sha256);
  while (!source_file.atEnd()) {
    const auto chunk = source_file.read(256 * 1024);
    if (chunk.isEmpty() && source_file.error() != QFileDevice::NoError)
      return fail(QStringLiteral("Could not read the private fixture source image."));
    source_hasher.addData(chunk);
  }
  const auto source_sha256 = QString::fromLatin1(source_hasher.result().toHex());
  auto evidence = QJsonObject{
      {QStringLiteral("schema"), QStringLiteral("agent-recipe-persistence-native-evidence-v1")},
      {QStringLiteral("status"), QStringLiteral("in_progress")},
      {QStringLiteral("scope"), QStringLiteral("actual Preview/Catalog adapter integration")},
      {QStringLiteral("localModelPlannerProven"), false},
      {QStringLiteral("fullNativeUiProven"), false},
      {QStringLiteral("applicationRestartProven"), false},
      {QStringLiteral("fixtureRoot"), canonical_fixture_root},
      {QStringLiteral("runtimePath"), temporary_runtime.path()},
      {QStringLiteral("sourcePath"), source_path},
      {QStringLiteral("sourceSha256"), source_sha256},
      {QStringLiteral("assetId"), asset_id},
  };
  const auto evidence_path = fixture_root.filePath(QStringLiteral("agent-recipe-persistence-receipt.json"));
  const auto save_evidence_checkpoint = [&](const QString& checkpoint) {
    evidence.insert(QStringLiteral("lastCheckpoint"), checkpoint);
    return write_receipt(evidence_path, evidence);
  };
  if (!save_evidence_checkpoint(QStringLiteral("fixture_validated")))
    return fail(QStringLiteral("Could not write the retained private fixture evidence receipt."));

  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", QDir(temporary_runtime.path())
      .filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", QDir(temporary_runtime.path())
      .filePath(QStringLiteral("cache")).toUtf8());
  // Only the dedicated, explicitly isolated test DB is allowed to receive writes.
  qputenv("PHOTO_EDITOR_DATABASE_URL", test_database_url.toUtf8());
  qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
  if (!QDir().mkpath(qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR")) ||
      !QDir().mkpath(QDir(temporary_runtime.path()).filePath(QStringLiteral("cache"))))
    return fail(QStringLiteral("Could not create the isolated temporary application directories."));

  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AgentRecipePersistenceNativeContract"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
      fixture_root.filePath(QStringLiteral("preferences")));

  struct DrainWorkers final {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(120000); }
  } drain;

  using namespace photo_editor::desktop;
  QString bootstrap_error;
  if (!AppBootstrap::ensureCatalogService(&bootstrap_error))
    return fail(QStringLiteral("Could not open the isolated test catalog: %1").arg(bootstrap_error));

  CatalogAssetModel catalog;
  catalog.setEditAccessPolicy([] { return true; });
  catalog.refresh();
  if (!wait_until([&catalog] { return !catalog.loading(); }) ||
      !catalog.errorMessage().isEmpty() || !catalog.selectAssetById(asset_id, true))
    return fail(QStringLiteral("The isolated catalog asset is unavailable: %1").arg(catalog.errorMessage()));
  const auto asset = catalog.assetById(asset_id);
  if (asset.isEmpty() || source_path.isEmpty() ||
      QFileInfo(asset.value(QStringLiteral("filePath")).toString()).canonicalFilePath() != source_path)
    return fail(QStringLiteral("The imported fixture asset does not match the explicitly supplied source file."));

  const auto catalog_before = catalog.agentTaskRecipeResourceAdapter(asset_id).read();
  if (catalog_before.isEmpty())
    return fail(QStringLiteral("The exact baseline catalog recipe could not be read."));

  EditorPreviewController preview;
  preview.setEditAccessPolicy([] { return true; });
  preview.loadAsset(asset_id, source_path, {}, {}, {}, {},
      catalog.selectedAdjustmentPatchJson());
  const auto preview_before = preview.currentAssetBoundAdjustmentPatchJson().toUtf8();
  if (preview_before.isEmpty())
    return fail(QStringLiteral("The actual preview did not load an asset-bound recipe."));

  QString export_error;
  const auto before_export_path = QDir(AppBootstrap::appDataLocation())
      .filePath(QStringLiteral("agent-recipe-before.png"));
  if (!export_preview(preview, before_export_path, &export_error))
    return fail(QStringLiteral("Could not render the native baseline composition: %1").arg(export_error));
  evidence.insert(QStringLiteral("beforeExportPath"), before_export_path);
  if (!save_evidence_checkpoint(QStringLiteral("native_baseline_rendered")))
    return fail(QStringLiteral("Could not retain the native baseline render evidence."));
  const QImage before_pixels(before_export_path);
  if (before_pixels.isNull()) return fail(QStringLiteral("The baseline composition export is unreadable."));

  AgentTaskCoordinator tasks;
  const auto task_id = QStringLiteral("recipe-native-%1")
      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  evidence.insert(QStringLiteral("taskId"), task_id);
  if (!save_evidence_checkpoint(QStringLiteral("task_identity_allocated")))
    return fail(QStringLiteral("Could not retain the task identity in the fixture evidence receipt."));
  if (!tasks.begin(AppBootstrap::appDataLocation(), task_id).value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("The isolated agent task journal did not begin."));
  const auto catalog_resource = QStringLiteral("catalog-recipe:%1").arg(asset_id);
  const auto catalog_enrollment = tasks.enroll(catalog_resource,
      catalog.agentTaskRecipeResourceAdapter(asset_id, task_id));
  if (!catalog_enrollment.value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("The actual Catalog resource adapter did not capture its baseline."));
  preview.setAgentTaskJournal(tasks.journal());
  if (!preview.beginAgentTask(task_id).value(QStringLiteral("ok")).toBool() ||
      !tasks.enroll(asset_id, preview.agentTaskResourceAdapter(task_id))
           .value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("The actual Preview resource adapter did not join the same task."));
  if (!save_evidence_checkpoint(QStringLiteral("task_resources_enrolled")))
    return fail(QStringLiteral("Could not retain the native task resource enrollment evidence."));

  AgentTaskRecipePersistence persistence(preview, catalog, tasks);
  QStringList persistence_stages;
  QVariantList persistence_receipts;
  QByteArray guarded_payload;
  const auto handler = [&](const QString& stage, const QVariantMap& context) {
    persistence_stages.append(stage);
    persistence_receipts.append(QVariantMap{
        {QStringLiteral("stage"), stage},
        {QStringLiteral("taskId"), context.value(QStringLiteral("taskId"))},
        {QStringLiteral("assetId"), context.value(QStringLiteral("assetId"))},
        {QStringLiteral("sourceOwner"), context.value(QStringLiteral("sourceOwner"))},
        {QStringLiteral("attemptId"), context.value(QStringLiteral("attemptId"))},
        {QStringLiteral("revisionId"), context.value(QStringLiteral("revisionId"))},
        {QStringLiteral("payload"), context.value(QStringLiteral("payload"))},
        {QStringLiteral("ok"), context.value(QStringLiteral("ok"))},
    });
    const auto result = persistence.handle(stage, context);
    if (stage == QStringLiteral("guard") && result.value(QStringLiteral("ok")).toBool())
      guarded_payload = result.value(QStringLiteral("payload")).toByteArray();
    return result;
  };
  preview.setAgentRecipePersistenceHandler(handler);
  catalog.setAgentRecipePersistenceHandler(handler);

  QString terminal_action;
  QString terminal_receipt_id;
  QVariantMap terminal_result;
  auto terminal_deferred = false;
  auto terminal_settled = false;
  auto terminal_failed = false;
  const auto saves_pending = [&]() {
    return preview.recipeSavePending() || persistence.pending() ||
        !catalog.agentTaskRecipePersistenceSettled(asset_id);
  };
  const auto finish_requested_terminal = [&]() {
    if (terminal_action.isEmpty() || terminal_settled || saves_pending()) return;
    if (!persistence.error().isEmpty() || !preview.recipeSaveError().isEmpty()) {
      const auto error = !persistence.error().isEmpty()
          ? persistence.error() : preview.recipeSaveError();
      terminal_result = {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
      terminal_failed = true;
      return;
    }
    terminal_result = terminal_action == QStringLiteral("revert")
        ? tasks.revert() : tasks.finish(terminal_action);
    terminal_settled = terminal_result.value(QStringLiteral("ok")).toBool();
    terminal_failed = !terminal_settled;
  };
  persistence.setSettlementHandler(finish_requested_terminal);
  const auto request_terminal = [&](const QString& action) {
    terminal_action = action;
    terminal_receipt_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    terminal_result.clear();
    terminal_settled = false;
    terminal_failed = false;
    terminal_deferred = saves_pending();
    finish_requested_terminal();
    return QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("pending"), terminal_deferred},
        {QStringLiteral("taskId"), task_id},
        {QStringLiteral("receiptId"), terminal_receipt_id},
    };
  };

  const auto add_text = preview.runAgentTaskComposition(task_id, QVariantMap{
      {QStringLiteral("assetId"), asset_id},
      {QStringLiteral("operation"), QStringLiteral("add_text")},
      {QStringLiteral("values"), QVariantMap{
          {QStringLiteral("text"), QStringLiteral("TASK OWNED TITLE")},
          {QStringLiteral("x"), 64}, {QStringLiteral("y"), 48},
      }},
  });
  const auto text_layer_id = add_text.value(QStringLiteral("createdLayerId")).toString();
  evidence.insert(QStringLiteral("stableLayerId"), text_layer_id);
  if (!save_evidence_checkpoint(QStringLiteral("native_text_added")))
    return fail(QStringLiteral("Could not retain the stable layer ID in the fixture evidence receipt."));
  if (!add_text.value(QStringLiteral("ok")).toBool() || text_layer_id.isEmpty() ||
      !add_text.value(QStringLiteral("changed")).toBool() ||
      !preview.recipeSavePending())
    return fail(QStringLiteral("The real task-owned add_text command did not change and queue the native Preview recipe."));
  evidence.insert(QStringLiteral("returnedLayerId"), text_layer_id);
  if (!save_evidence_checkpoint(QStringLiteral("native_text_save_queued")))
    return fail(QStringLiteral("Could not retain the queued native text edit evidence."));

  const auto pending_review = request_terminal(QStringLiteral("review"));
  evidence.insert(QStringLiteral("reviewReceiptId"), pending_review.value(QStringLiteral("receiptId")).toString());
  evidence.insert(QStringLiteral("reviewWasDeferred"), pending_review.value(QStringLiteral("pending")).toBool());
  if (!save_evidence_checkpoint(QStringLiteral("review_pending")))
    return fail(QStringLiteral("Could not retain the pending Review receipt."));
  if (!pending_review.value(QStringLiteral("pending")).toBool() || terminal_settled ||
      tasks.journal()->report().value(QStringLiteral("status")).toString() != QStringLiteral("running"))
    return fail(QStringLiteral("Review must remain pending until Preview save and exact Catalog readback settle."));
  if (!wait_until([&] { return terminal_settled || terminal_failed; }))
    return fail(QStringLiteral("The actual Preview helper and Catalog receipt did not settle Review."));
  if (!terminal_settled)
    return fail(QStringLiteral("Review settlement failed: %1").arg(terminal_result.value(QStringLiteral("error")).toString()));
  if (!terminal_result.value(QStringLiteral("ok")).toBool() ||
      tasks.journal()->report().value(QStringLiteral("status")).toString() != QStringLiteral("review") ||
      persistence.pending() || !persistence.error().isEmpty() ||
      !preview.recipeSaveError().isEmpty() ||
      tasks.journal()->checkpointHash(catalog_resource).isEmpty() ||
      tasks.journal()->checkpointHash(asset_id).isEmpty())
    return fail(QStringLiteral("Review did not settle against both exact native and Catalog checkpoints."));
  evidence.insert(QStringLiteral("reviewSettled"), true);
  if (!save_evidence_checkpoint(QStringLiteral("review_settled_native")))
    return fail(QStringLiteral("Could not retain the settled Review checkpoint."));

  const auto after_export_path = QDir(AppBootstrap::appDataLocation())
      .filePath(QStringLiteral("agent-recipe-after.png"));
  if (!export_preview(preview, after_export_path, &export_error))
    return fail(QStringLiteral("Could not render the edited native composition: %1").arg(export_error));
  evidence.insert(QStringLiteral("afterExportPath"), after_export_path);
  if (!save_evidence_checkpoint(QStringLiteral("edited_native_rendered")))
    return fail(QStringLiteral("Could not retain the edited native render evidence."));
  const QImage after_pixels(after_export_path);
  if (after_pixels.isNull() || before_pixels.size() != after_pixels.size() || before_pixels == after_pixels)
    return fail(QStringLiteral("The actual Preview text edit did not change the rendered composition pixels."));

  const auto first_layer_recipe = parse_adjustment_patch_json(preview.currentAssetBoundAdjustmentPatchJson());
  if (!first_layer_recipe.ok() || !first_layer_recipe.state.has_value() ||
      first_layer_recipe.state->layers.isEmpty() ||
      first_layer_recipe.state->layers.first().toMap().value(QStringLiteral("id")) != text_layer_id)
    return fail(QStringLiteral("The returned Preview layer ID was not stable in the saved native recipe."));
  const auto catalog_after_review =
      catalog.agentTaskRecipeResourceAdapter(asset_id, task_id).read();
  if (catalog_after_review.isEmpty() ||
      !tasks.journal()->matchesCheckpoint(catalog_resource, catalog_after_review))
    return fail(QStringLiteral("Review lacks the exact persisted Catalog recipe receipt."));
  evidence.insert(QStringLiteral("catalogCheckpointAfterReview"),
      QString::fromLatin1(QCryptographicHash::hash(catalog_after_review,
          QCryptographicHash::Sha256).toHex()));
  if (!save_evidence_checkpoint(QStringLiteral("review_settled_exactly")))
    return fail(QStringLiteral("Could not retain the exact settled Catalog checkpoint."));

  const auto continued = tasks.continueTask();
  if (!continued.value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("The settled task could not continue for a second real native edit."));
  terminal_action.clear();
  terminal_settled = false;
  const auto revised_text = preview.runAgentTaskComposition(task_id, QVariantMap{
      {QStringLiteral("assetId"), asset_id},
      {QStringLiteral("operation"), QStringLiteral("text")},
      {QStringLiteral("layerId"), text_layer_id},
      {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("REVISED TASK TITLE")}}},
  });
  const auto revised_layers = revised_text.value(QStringLiteral("layers")).toList();
  const auto revised_layer_present = std::any_of(
      revised_layers.cbegin(), revised_layers.cend(),
      [&text_layer_id](const QVariant& value) {
        const auto layer = value.toMap();
        return layer.value(QStringLiteral("id")).toString() == text_layer_id &&
            layer.value(QStringLiteral("text")).toString() == QStringLiteral("REVISED TASK TITLE");
      });
  if (!revised_text.value(QStringLiteral("ok")).toBool() ||
      !revised_text.value(QStringLiteral("changed")).toBool() ||
      !revised_text.value(QStringLiteral("createdLayerId")).toString().isEmpty() ||
      !revised_layer_present ||
      !preview.recipeSavePending())
    return fail(QStringLiteral("The second native command did not revise the original stable text layer."));

  const auto pending_revert = request_terminal(QStringLiteral("revert"));
  evidence.insert(QStringLiteral("revertReceiptId"), pending_revert.value(QStringLiteral("receiptId")).toString());
  evidence.insert(QStringLiteral("revertWasDeferred"), pending_revert.value(QStringLiteral("pending")).toBool());
  if (!save_evidence_checkpoint(QStringLiteral("revert_pending")))
    return fail(QStringLiteral("Could not retain the pending Revert receipt."));
  if (!pending_revert.value(QStringLiteral("pending")).toBool() || terminal_settled ||
      tasks.journal()->report().value(QStringLiteral("status")).toString() != QStringLiteral("running"))
    return fail(QStringLiteral("Revert must wait for the second Preview helper and exact Catalog receipt."));
  if (!wait_until([&] { return terminal_settled || terminal_failed; }))
    return fail(QStringLiteral("The actual owner did not settle the pending task Revert."));
  if (!terminal_settled)
    return fail(QStringLiteral("Revert settlement failed: %1").arg(terminal_result.value(QStringLiteral("error")).toString()));
  if (!terminal_result.value(QStringLiteral("ok")).toBool() ||
      tasks.journal()->report().value(QStringLiteral("status")).toString() != QStringLiteral("reverted") ||
      preview.currentAssetBoundAdjustmentPatchJson().toUtf8() != preview_before ||
      catalog.agentTaskRecipeResourceAdapter(asset_id, task_id).read() != catalog_before ||
      persistence.pending() || !persistence.error().isEmpty())
    return fail(QStringLiteral("Whole-task Revert did not restore the exact Preview and Catalog baselines."));
  evidence.insert(QStringLiteral("previewRecipeRestoredExactly"), true);
  evidence.insert(QStringLiteral("catalogSnapshotRestoredExactly"), true);
  if (!save_evidence_checkpoint(QStringLiteral("exact_recipe_and_catalog_reverted")))
    return fail(QStringLiteral("Could not retain exact native and Catalog Revert evidence."));

  const auto reverted_export_path = QDir(AppBootstrap::appDataLocation())
      .filePath(QStringLiteral("agent-recipe-reverted.png"));
  if (!export_preview(preview, reverted_export_path, &export_error))
    return fail(QStringLiteral("Could not render the reverted native composition: %1").arg(export_error));
  const QImage reverted_pixels(reverted_export_path);
  if (reverted_pixels.isNull() || before_pixels.size() != reverted_pixels.size() ||
      before_pixels != reverted_pixels || before_pixels.colorSpace() != reverted_pixels.colorSpace())
    return fail(QStringLiteral("The reverted composition pixels or color profile differ from the original baseline."));
  evidence.insert(QStringLiteral("revertedExportPath"), reverted_export_path);
  evidence.insert(QStringLiteral("revertedPixelsAndProfileMatchBaseline"), true);
  if (!save_evidence_checkpoint(QStringLiteral("reverted_render_matches_baseline")))
    return fail(QStringLiteral("Could not retain the reverted native render evidence."));

  const auto first_guard = std::find(persistence_stages.cbegin(), persistence_stages.cend(),
      QStringLiteral("guard"));
  const auto first_prepare = std::find(persistence_stages.cbegin(), persistence_stages.cend(),
      QStringLiteral("prepare"));
  const auto first_accepted = std::find(persistence_stages.cbegin(), persistence_stages.cend(),
      QStringLiteral("accepted"));
  const auto first_finished = std::find(persistence_stages.cbegin(), persistence_stages.cend(),
      QStringLiteral("finished"));
  if (first_guard == persistence_stages.cend() || first_prepare == persistence_stages.cend() ||
      first_accepted == persistence_stages.cend() || first_finished == persistence_stages.cend() ||
      !(first_guard < first_prepare && first_prepare < first_accepted && first_accepted < first_finished) ||
      !guarded_helper_payload(guarded_payload, asset_id, source_path, source_sha256))
    return fail(QStringLiteral("The owner did not verify guard, preparation, actual process start, and helper completion in order."));

  QHash<QString, QHash<QString, QVariantMap>> immutable_attempts;
  for (const auto& receipt_value : persistence_receipts) {
    const auto receipt = receipt_value.toMap();
    if (receipt.value(QStringLiteral("taskId")).toString() != task_id ||
        receipt.value(QStringLiteral("assetId")).toString() != asset_id ||
        receipt.value(QStringLiteral("sourceOwner")).toString() != QStringLiteral("preview") ||
        receipt.value(QStringLiteral("attemptId")).toString().isEmpty())
      return fail(QStringLiteral("A native save stage escaped the original task, target, or Preview owner."));
    immutable_attempts[receipt.value(QStringLiteral("attemptId")).toString()].insert(
        receipt.value(QStringLiteral("stage")).toString(), receipt);
  }
  if (immutable_attempts.isEmpty())
    return fail(QStringLiteral("The native Preview writer returned no immutable save attempt identity."));
  for (auto attempt = immutable_attempts.cbegin(); attempt != immutable_attempts.cend(); ++attempt) {
    const auto stages = attempt.value();
    const auto prepared = stages.value(QStringLiteral("prepare"));
    const auto accepted = stages.value(QStringLiteral("accepted"));
    const auto finished = stages.value(QStringLiteral("finished"));
    if (prepared.isEmpty() || accepted.isEmpty() || finished.isEmpty() ||
        prepared.value(QStringLiteral("payload")).toByteArray().isEmpty() ||
        prepared.value(QStringLiteral("payload")).toByteArray() !=
            accepted.value(QStringLiteral("payload")).toByteArray() ||
        prepared.value(QStringLiteral("payload")).toByteArray() !=
            finished.value(QStringLiteral("payload")).toByteArray() ||
        prepared.value(QStringLiteral("revisionId")) != accepted.value(QStringLiteral("revisionId")) ||
        prepared.value(QStringLiteral("revisionId")) != finished.value(QStringLiteral("revisionId")))
      return fail(QStringLiteral("A helper launch or completion changed its exact prepared payload or revision."));
  }

  QJsonArray stage_history;
  for (const auto& stage : persistence_stages) stage_history.append(stage);
  QJsonArray immutable_attempt_history;
  for (auto attempt = immutable_attempts.cbegin(); attempt != immutable_attempts.cend(); ++attempt) {
    const auto prepared = attempt.value().value(QStringLiteral("prepare"));
    immutable_attempt_history.append(QJsonObject{
        {QStringLiteral("attemptId"), attempt.key()},
        {QStringLiteral("revisionId"), prepared.value(QStringLiteral("revisionId")).toString()},
        {QStringLiteral("preparedPayloadSha256"), QString::fromLatin1(QCryptographicHash::hash(
            prepared.value(QStringLiteral("payload")).toByteArray(),
            QCryptographicHash::Sha256).toHex())},
    });
  }
  evidence.insert(QStringLiteral("helperStageHistory"), stage_history);
  evidence.insert(QStringLiteral("immutableHelperAttempts"), immutable_attempt_history);
  evidence.insert(QStringLiteral("immutablePreparedPayloadsVerified"), true);
  evidence.insert(QStringLiteral("helperAttemptCount"), static_cast<int>(immutable_attempts.size()));
  evidence.insert(QStringLiteral("scopeNotes"), QJsonArray{
      QStringLiteral("Actual native Preview and Catalog task adapters were used."),
      QStringLiteral("This fixture does not qualify local-model planning, full native UI behavior, or app restart recovery.")});
  evidence.insert(QStringLiteral("status"), QStringLiteral("passed"));
  if (!write_receipt(evidence_path, evidence))
    return fail(QStringLiteral("Could not write the final retained native persistence receipt."));

  std::cout << "native_preview_recipe_persistence=passed "
            << "review_deferred=" << pending_review.value(QStringLiteral("pending")).toBool() << ' '
            << "revert_deferred=" << pending_revert.value(QStringLiteral("pending")).toBool() << ' '
            << "stable_layer_id=" << text_layer_id.toStdString() << ' '
            << "exact_catalog_revert=1" << std::endl;
  return EXIT_SUCCESS;
}

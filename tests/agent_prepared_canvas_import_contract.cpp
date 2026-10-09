#include "catalog_asset_model.h"

#include "agent_created_file_resource.h"
#include "agent_task_coordinator.h"
#include "app_bootstrap.h"
#include "blank_canvas_document.h"
#include "job_runner.h"
#include "job_store.h"

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QUuid>
#include <functional>

namespace {

using photo_editor::desktop::AgentCreatedFileResource;
using photo_editor::desktop::AgentTaskCoordinator;
using photo_editor::desktop::AppBootstrap;
using photo_editor::desktop::CatalogAssetModel;
using photo_editor::desktop::JobRunner;

bool wait_until(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return predicate();
}

bool save_recipe_with_native_helper(const QString& asset_id, const QString& legacy_recipe,
                                    const QString& canonical_recipe, const QString& cache_key,
                                    const QString& provenance, QString* error) {
  QProcessEnvironment environment;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, error)) return false;
  QProcess process;
  process.setWorkingDirectory(QDir::currentPath());
  process.setProcessEnvironment(environment);
  process.start(AppBootstrap::helperProgram(QStringLiteral("save_edit_recipe")),
      AppBootstrap::helperArguments(QStringLiteral("save_edit_recipe"),
          {asset_id, legacy_recipe, canonical_recipe, cache_key, provenance}));
  if (!process.waitForStarted(10000)) {
    if (error != nullptr) *error = process.errorString();
    return false;
  }
  if (!process.waitForFinished(120000)) {
    process.kill();
    process.waitForFinished();
    if (error != nullptr) *error = QStringLiteral("The native recipe helper did not settle.");
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error != nullptr) *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    return false;
  }
  return true;
}

QVariantMap exact_recipe_receipt(const QString& legacy_recipe, const QString& canonical_recipe,
                                 const QString& cache_key, const QString& provenance) {
  const auto legacy = QJsonDocument::fromJson(legacy_recipe.toUtf8()).object().toVariantMap();
  return {{QStringLiteral("legacyRecipe"), legacy},
          {QStringLiteral("canonicalRecipe"), QVariantMap{
              {QStringLiteral("recipe_payload"), canonical_recipe},
              {QStringLiteral("cache_key"), cache_key},
              {QStringLiteral("provenance_payload"), provenance}}}};
}

class PreparedCanvasTaskProbe final : public QObject {
 public:
  PreparedCanvasTaskProbe() {
    QObject::connect(&catalog, &CatalogAssetModel::preparedBlankCanvasSourceCreated, this,
        [this](const QString& received_task, const QString& received_document,
               const QString& received_path, const QString& received_sha) {
      if (received_task != task_id || received_document != document_id || received_path != source_path) return;
      source_created = true;
      source_sha256 = received_sha;
      if (!file_owner || !file_owner->recordCreatedReceipt(received_sha)) {
        fail(QStringLiteral("The importer source receipt did not match the exact created file."));
        return;
      }
      const auto settled = tasks.journal()->settle(source_resource_id, file_owner->snapshot(), true);
      if (!settled.value(QStringLiteral("ok")).toBool()) {
        fail(settled.value(QStringLiteral("error")).toString());
        return;
      }
      const auto prepared = tasks.journal()->prepare(row_resource_id, QStringLiteral("catalog.import.canvas"),
          {{QStringLiteral("jobId"), import_job_id}, {QStringLiteral("path"), source_path}}, rowSnapshot());
      if (!prepared.value(QStringLiteral("ok")).toBool()) {
        fail(prepared.value(QStringLiteral("error")).toString());
        return;
      }
      row_step_id = prepared.value(QStringLiteral("stepId")).toLongLong();
      if (!catalog.acceptPreparedBlankCanvasSource(task_id, received_sha))
        fail(catalog.errorMessage().isEmpty() ? QStringLiteral("The prepared importer admission failed.")
                                              : catalog.errorMessage());
    });
    QObject::connect(&catalog, &CatalogAssetModel::preparedBlankCanvasImportAccepted, this,
        [this](const QString& received_task, const QString& received_job,
               const QString& received_path, const QString& received_sha) {
      if (received_task != task_id) return;
      import_accepted = true;
      if (received_job != import_job_id || received_path != source_path || received_sha != source_sha256 ||
          row_step_id < 0) {
        fail(QStringLiteral("The accepted importer identity differed from its preallocated task receipt."));
        return;
      }
      const auto completed = tasks.journal()->complete(row_step_id, rowSnapshot(),
          {{QStringLiteral("ok"), true}, {QStringLiteral("jobId"), received_job},
           {QStringLiteral("sourceSha256"), received_sha}}, true);
      if (!completed.value(QStringLiteral("ok")).toBool()) fail(completed.value(QStringLiteral("error")).toString());
    });
    QObject::connect(&catalog, &CatalogAssetModel::preparedBlankCanvasFinished, this,
        [this](const QString& received_task, const QString& received_job, const QString& status,
               const QString& received_asset, const QString& received_path, const QString& received_sha,
               const QString& received_error) {
      if (received_task != task_id || received_job != import_job_id) return;
      finished = true;
      final_status = status;
      asset_id = received_asset;
      final_error = received_error;
      if (catalog.preparedBlankCanvasBusy(task_id)) {
        fail(QStringLiteral("The producer reported completion before becoming quiescent."));
        return;
      }
      if (received_path != source_path || (!source_sha256.isEmpty() && received_sha != source_sha256))
        fail(QStringLiteral("The terminal importer receipt changed its source identity."));
      if (tasks.journal()->pending(source_resource_id)) {
        const auto source = file_owner ? file_owner->snapshot() : QByteArray{};
        const auto exact = file_owner && !source_sha256.isEmpty() && file_owner->recordCreatedReceipt(source_sha256);
        const auto settled = tasks.journal()->settle(source_resource_id, source, exact && status == QStringLiteral("completed"));
        if (!settled.value(QStringLiteral("ok")).toBool()) fail(settled.value(QStringLiteral("error")).toString());
      }
      if (tasks.journal()->pending(row_resource_id)) {
        const auto row = rowSnapshot();
        const auto settled = tasks.journal()->settle(row_resource_id, row,
            status == QStringLiteral("completed") && !row.isEmpty());
        if (!settled.value(QStringLiteral("ok")).toBool()) fail(settled.value(QStringLiteral("error")).toString());
      }
    });
  }

  bool prepareAndStart(const QVariantMap& settings) {
    task_id = QStringLiteral("prepared-owned-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    document_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    import_job_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto document_root = QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("canvas-documents"));
    source_path = photo_editor::desktop::planned_blank_canvas_source_path(document_root, settings, document_id);
    source_resource_id = QStringLiteral("created-file:%1").arg(document_id);
    row_resource_id = QStringLiteral("catalog-row:%1").arg(document_id);
    if (source_path.isEmpty()) return fail(QStringLiteral("The prepared source path could not be planned."));
    file_owner = std::make_shared<AgentCreatedFileResource>(document_root, source_path,
        [this] { static_cast<void>(catalog.cancelPreparedBlankCanvas(task_id)); },
        [this] { return !catalog.preparedBlankCanvasBusy(task_id); });
    const auto file_before = file_owner->snapshot();
    const auto row_before = rowSnapshot();
    if (file_before.isEmpty() || row_before.isEmpty() ||
        QJsonDocument::fromJson(file_before).object().value(QStringLiteral("exists")).toBool() ||
        QJsonDocument::fromJson(row_before).object().value(QStringLiteral("state")).toString() != QStringLiteral("absent"))
      return fail(QStringLiteral("The planned source and catalog row must be absent before admission."));
    if (!tasks.begin(AppBootstrap::appDataLocation(), task_id).value(QStringLiteral("ok")).toBool())
      return fail(QStringLiteral("The durable task journal could not begin."));
    if (!tasks.journal()->recordApprovedSnapshot({
            {QStringLiteral("contract"), QStringLiteral("agent-approved-session-v1")},
            {QStringLiteral("prompt"), QStringLiteral("Create and edit a prepared canvas for ownership verification.")}})
             .value(QStringLiteral("ok")).toBool())
      return fail(QStringLiteral("The task journal could not pin its approved prompt."));
    auto row_last_read = std::make_shared<QByteArray>();
    photo_editor::desktop::AgentTaskJournal::ResourceAdapter row_adapter;
    row_adapter.read = [this, row_last_read] {
      *row_last_read = rowSnapshot();
      return *row_last_read;
    };
    row_adapter.restore = [this, row_last_read](const QByteArray& before) {
      catalog_restore_error.clear();
      return catalog.restorePreparedBlankCanvasCatalogSnapshot(
          source_path, *row_last_read, before, task_id, import_job_id, &catalog_restore_error);
    };
    row_adapter.cancel = [this] { static_cast<void>(catalog.cancelPreparedBlankCanvas(task_id)); };
    if (!tasks.enroll(source_resource_id, file_owner->adapter()).value(QStringLiteral("ok")).toBool() ||
        !tasks.enroll(row_resource_id, std::move(row_adapter)).value(QStringLiteral("ok")).toBool())
      return fail(QStringLiteral("The source and row absence baselines could not be enrolled."));
    const auto prepared = tasks.journal()->prepare(source_resource_id, QStringLiteral("canvas.create"),
        {{QStringLiteral("documentId"), document_id}, {QStringLiteral("path"), source_path}}, file_before);
    if (!prepared.value(QStringLiteral("ok")).toBool()) return fail(prepared.value(QStringLiteral("error")).toString());
    source_step_id = prepared.value(QStringLiteral("stepId")).toLongLong();
    const auto started = catalog.startPreparedBlankCanvas(task_id, document_id, import_job_id, settings);
    const auto accepted = tasks.journal()->complete(source_step_id, file_owner->snapshot(),
        {{QStringLiteral("ok"), started}, {QStringLiteral("jobId"), import_job_id},
         {QStringLiteral("documentId"), document_id}}, started);
    if (!accepted.value(QStringLiteral("ok")).toBool()) return fail(accepted.value(QStringLiteral("error")).toString());
    if (!started) return fail(catalog.errorMessage());
    return true;
  }

  bool cancelImporter() {
    return catalog.cancelPreparedBlankCanvas(task_id);
  }

  QByteArray rowSnapshot() const {
    return catalog.preparedBlankCanvasCatalogSnapshot(source_path, task_id, import_job_id);
  }

  bool fail(const QString& message) {
    if (error.isEmpty()) error = message;
    if (!task_id.isEmpty()) static_cast<void>(catalog.cancelPreparedBlankCanvas(task_id));
    return false;
  }

  CatalogAssetModel catalog;
  AgentTaskCoordinator tasks;
  std::shared_ptr<AgentCreatedFileResource> file_owner;
  QString task_id, document_id, import_job_id, source_path, source_resource_id, row_resource_id;
  QString source_sha256, final_status, asset_id, final_error, error;
  QString catalog_restore_error;
  qint64 source_step_id = -1, row_step_id = -1;
  bool source_created = false, import_accepted = false, finished = false;
};

bool run_prepared_recipe_edit_and_revert(PreparedCanvasTaskProbe* probe) {
  if (probe == nullptr || probe->final_status != QStringLiteral("completed") || probe->asset_id.isEmpty()) return false;
  const auto recipe_resource_id = QStringLiteral("catalog-recipe:%1").arg(probe->asset_id);
  auto recipe_adapter = probe->catalog.agentTaskRecipeResourceAdapter(probe->asset_id, probe->task_id);
  const auto enrolled_recipe = probe->tasks.enroll(recipe_resource_id, recipe_adapter);
  if (!enrolled_recipe.value(QStringLiteral("ok")).toBool()) {
    probe->error = enrolled_recipe.value(QStringLiteral("error")).toString();
    return false;
  }
  auto native_photo_state = std::make_shared<QByteArray>(QByteArrayLiteral("prepared native photo baseline"));
  photo_editor::desktop::AgentTaskJournal::ResourceAdapter photo_adapter;
  photo_adapter.read = [native_photo_state] { return *native_photo_state; };
  photo_adapter.restore = [native_photo_state](const QByteArray& before) {
    if (before.isEmpty()) return false;
    *native_photo_state = before;
    return true;
  };
  const auto enrolled_photo = probe->tasks.enroll(probe->asset_id, std::move(photo_adapter));
  if (!enrolled_photo.value(QStringLiteral("ok")).toBool()) {
    probe->error = enrolled_photo.value(QStringLiteral("error")).toString();
    return false;
  }
  const auto adopted = probe->tasks.adoptPreparedTask(probe->task_id, {probe->asset_id});
  if (!adopted.value(QStringLiteral("ok")).toBool()) {
    probe->error = adopted.value(QStringLiteral("error")).toString();
    return false;
  }

  const QString legacy = QStringLiteral(R"({"adjustments":{"exposure":0.35}})");
  const QString canonical = QStringLiteral("{}");
  const auto cache_key = QStringLiteral("prepared-canvas-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  const QString provenance = QStringLiteral(
      R"({"schema":"photo-editor-edit-recipe-provenance:v1","presetApplications":[]})");
  const auto before_recipe = recipe_adapter.read();
  const auto planned_recipe = exact_recipe_receipt(legacy, canonical, cache_key, provenance);
  if (before_recipe.isEmpty() || !probe->catalog.registerAgentTaskRecipeCheckpoint(
          probe->task_id, probe->asset_id, before_recipe, planned_recipe) ||
      !probe->catalog.cancelAgentTaskRecipeCheckpoint(probe->task_id, probe->asset_id, before_recipe) ||
      !probe->catalog.registerAgentTaskRecipeCheckpoint(probe->task_id, probe->asset_id, before_recipe, planned_recipe)) {
    probe->error = QStringLiteral("The exact task-owned recipe save could not be registered before persistence.");
    return false;
  }
  const auto prepared_save = probe->tasks.journal()->prepare(recipe_resource_id, QStringLiteral("design.save.recipe"),
      {{QStringLiteral("cacheKey"), cache_key}}, before_recipe);
  if (!prepared_save.value(QStringLiteral("ok")).toBool()) {
    probe->error = prepared_save.value(QStringLiteral("error")).toString();
    return false;
  }
  QString helper_error;
  if (!save_recipe_with_native_helper(probe->asset_id, legacy, canonical, cache_key, provenance, &helper_error)) {
    probe->error = probe->catalog.cancelAgentTaskRecipeCheckpoint(
        probe->task_id, probe->asset_id, before_recipe)
        ? helper_error
        : helper_error + QStringLiteral(" The exact checkpoint was retained because the catalog may have changed.");
    return false;
  }
  const auto recipe_receipt = probe->catalog.completeAgentTaskRecipeCheckpoint(probe->task_id, probe->asset_id);
  if (recipe_receipt.isEmpty() || recipe_receipt == before_recipe) {
    probe->error = QStringLiteral("The completed helper write did not advance the exact recipe receipt.");
    return false;
  }
  const auto completed_save = probe->tasks.journal()->complete(prepared_save.value(QStringLiteral("stepId")).toLongLong(),
      recipe_receipt, {{QStringLiteral("ok"), true}, {QStringLiteral("cacheKey"), cache_key}});
  if (!completed_save.value(QStringLiteral("ok")).toBool()) {
    probe->error = completed_save.value(QStringLiteral("error")).toString();
    return false;
  }
  const auto photo_before = *native_photo_state;
  const auto prepared_photo_edit = probe->tasks.journal()->prepare(probe->asset_id,
      QStringLiteral("design.update.native-document"), {}, photo_before);
  if (!prepared_photo_edit.value(QStringLiteral("ok")).toBool()) {
    probe->error = prepared_photo_edit.value(QStringLiteral("error")).toString();
    return false;
  }
  *native_photo_state = QByteArrayLiteral("prepared native photo edited");
  if (!probe->tasks.journal()->complete(prepared_photo_edit.value(QStringLiteral("stepId")).toLongLong(),
          *native_photo_state, {{QStringLiteral("ok"), true}}).value(QStringLiteral("ok")).toBool()) {
    probe->error = QStringLiteral("The native edit checkpoint could not be recorded.");
    return false;
  }
  const auto review = probe->tasks.finish(QStringLiteral("review"));
  if (!review.value(QStringLiteral("ok")).toBool()) {
    probe->error = review.value(QStringLiteral("error")).toString();
    return false;
  }
  const auto reverted = probe->tasks.revert();
  if (!reverted.value(QStringLiteral("ok")).toBool()) {
    const QVariantMap diagnostic{
        {QStringLiteral("revertResult"), reverted},
        {QStringLiteral("tasksState"), probe->tasks.state()},
        {QStringLiteral("catalogRestoreError"), probe->catalog_restore_error},
        {QStringLiteral("catalogErrorMessage"), probe->catalog.errorMessage()},
        {QStringLiteral("rowSnapshot"), QString::fromUtf8(probe->rowSnapshot())},
        {QStringLiteral("recipeSnapshot"), QString::fromUtf8(recipe_adapter.read())},
        {QStringLiteral("nativePhotoState"), QString::fromUtf8(*native_photo_state)},
    };
    probe->error = QStringLiteral("%1 Diagnostics: %2")
        .arg(reverted.value(QStringLiteral("error")).toString(),
             QString::fromUtf8(QJsonDocument::fromVariant(diagnostic).toJson(QJsonDocument::Compact)));
    return false;
  }
  const auto source_state = QJsonDocument::fromJson(probe->file_owner->snapshot()).object();
  const auto row_state = QJsonDocument::fromJson(probe->rowSnapshot()).object();
  const auto durable_state = probe->tasks.state();
  return durable_state.value(QStringLiteral("status")).toString() == QStringLiteral("reverted") &&
      source_state.value(QStringLiteral("exists")).toBool() == false &&
      row_state.value(QStringLiteral("state")).toString() == QStringLiteral("absent") &&
      *native_photo_state == photo_before;
}

}  // namespace

int run_agent_prepared_canvas_import_contract() {
  using photo_editor::desktop::CatalogAssetModel;
  const auto fail = [](const char* message) {
    std::cerr << message << '\n';
    return EXIT_FAILURE;
  };

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) return fail("Prepared canvas runtime root unavailable");
  const auto previous_app_data = qgetenv("PHOTO_EDITOR_APP_DATA_DIR");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());

  const auto task_id = QStringLiteral("prepared-canvas-cancel-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  const auto document_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto import_job_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QVariantMap settings{{QStringLiteral("width"), 640},
                             {QStringLiteral("height"), 360},
                             {QStringLiteral("name"), QStringLiteral("Prepared cancellation")},
                             {QStringLiteral("background"), QStringLiteral("transparent")}};
  CatalogAssetModel model;
  auto source_created = false;
  auto import_accepted = false;
  auto finished = false;
  auto finished_status = QString{};
  auto source_path = QString{};
  auto source_sha256 = QString{};
  QObject::connect(&model, &CatalogAssetModel::preparedBlankCanvasSourceCreated, &model,
                   [&](const QString& received_task_id, const QString& received_document_id,
                       const QString& received_path, const QString& received_sha256) {
    if (received_task_id != task_id || received_document_id != document_id) return;
    source_created = true;
    source_path = received_path;
    source_sha256 = received_sha256;
    // Revert before importer admission: the source producer has settled, but
    // no catalog row or import job may be adopted by this task.
    if (!model.cancelPreparedBlankCanvas(task_id)) return;
  });
  QObject::connect(&model, &CatalogAssetModel::preparedBlankCanvasImportAccepted, &model,
                   [&](const QString& received_task_id, const QString&, const QString&, const QString&) {
    if (received_task_id == task_id) import_accepted = true;
  });
  QObject::connect(&model, &CatalogAssetModel::preparedBlankCanvasFinished, &model,
                   [&](const QString& received_task_id, const QString&, const QString& status,
                       const QString&, const QString&, const QString&, const QString&) {
    if (received_task_id != task_id) return;
    finished = true;
    finished_status = status;
  });

  const bool started = model.startPreparedBlankCanvas(task_id, document_id, import_job_id, settings);
  if (!started) {
    if (previous_app_data.isNull()) qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    else qputenv("PHOTO_EDITOR_APP_DATA_DIR", previous_app_data);
    return fail("A prepared source should start from an absent document path");
  }
  QElapsedTimer timer;
  timer.start();
  while (!finished && timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  const auto status = model.preparedBlankCanvasStatus(task_id);
  const auto source_exists = !source_path.isEmpty() && QFileInfo(source_path).isFile();
  QByteArray source_bytes;
  if (source_exists) {
    QFile source(source_path);
    if (source.open(QIODevice::ReadOnly)) source_bytes = source.readAll();
  }
  const auto actual_sha256 = source_bytes.isEmpty() ? QString{} : QString::fromLatin1(
      QCryptographicHash::hash(source_bytes, QCryptographicHash::Sha256).toHex());
  const auto passed = finished && source_created && !import_accepted && finished_status == QStringLiteral("canceled") &&
      !status.value(QStringLiteral("producerBusy")).toBool() && source_exists &&
      !source_sha256.isEmpty() && actual_sha256 == source_sha256 &&
      status.value(QStringLiteral("importJobId")).toString() == import_job_id;

  if (previous_app_data.isNull()) qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  else qputenv("PHOTO_EDITOR_APP_DATA_DIR", previous_app_data);
  if (!passed) return fail("Cancellation after source creation must settle the producer and prevent import admission");
  std::cout << "Prepared canvas cancellation settles the exact source before catalog admission\n";
  return EXIT_SUCCESS;
}

int run_agent_prepared_canvas_ownership_contract() {
  const auto fail = [](const QString& message) {
    std::cerr << message.toStdString() << '\n';
    return EXIT_FAILURE;
  };
  const auto isolated_database = qgetenv("PHOTO_EDITOR_AGENT_TASK_TEST_DATABASE_URL");
  if (isolated_database.isEmpty()) {
    std::cout << "SKIP: PHOTO_EDITOR_AGENT_TASK_TEST_DATABASE_URL is not set\n";
    return 77;
  }

  const auto previous_app_data = qgetenv("PHOTO_EDITOR_APP_DATA_DIR");
  const auto had_app_data = qEnvironmentVariableIsSet("PHOTO_EDITOR_APP_DATA_DIR");
  const auto previous_database = qgetenv("PHOTO_EDITOR_DATABASE_URL");
  const auto had_database = qEnvironmentVariableIsSet("PHOTO_EDITOR_DATABASE_URL");
  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) return fail(QStringLiteral("Prepared ownership runtime root unavailable"));
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", isolated_database);
  const auto restore_environment = [&] {
    if (had_app_data) qputenv("PHOTO_EDITOR_APP_DATA_DIR", previous_app_data);
    else qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    if (had_database) qputenv("PHOTO_EDITOR_DATABASE_URL", previous_database);
    else qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  };

  JobRunner runner;
  PreparedCanvasTaskProbe schema_probe;
  const QVariantMap small_settings{{QStringLiteral("width"), 640},
      {QStringLiteral("height"), 360}, {QStringLiteral("name"), QStringLiteral("Owned integration canvas")},
      {QStringLiteral("background"), QStringLiteral("transparent")}};
  const auto schema_root = QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("canvas-documents"));
  const auto schema_document = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto schema_path = photo_editor::desktop::planned_blank_canvas_source_path(
      schema_root, small_settings, schema_document);
  const auto schema_snapshot = schema_probe.catalog.preparedBlankCanvasCatalogSnapshot(
      schema_path, QStringLiteral("schema-probe"), QUuid::createUuid().toString(QUuid::WithoutBraces));
  if (schema_snapshot.isEmpty()) {
    restore_environment();
    return fail(QStringLiteral("The isolated catalog could not run its migrations and row-snapshot helper."));
  }

  bool runner_ready = false;
  QString runner_error;
  QObject::connect(&runner, &JobRunner::startupRecoveryFinished, &runner,
      [&](const QString& error) { runner_error = error; runner_ready = true; });
  runner.initializeFromStore();
  if (!wait_until([&] { return runner_ready; }, 120000) || !runner_error.isEmpty()) {
    restore_environment();
    return fail(runner_error.isEmpty() ? QStringLiteral("The isolated job runner did not become ready.") : runner_error);
  }

  PreparedCanvasTaskProbe created;
  if (!created.prepareAndStart(small_settings) || !wait_until([&] { return created.finished; }, 180000)) {
    const auto message = created.error.isEmpty() ? QStringLiteral("The prepared create/import did not settle.") : created.error;
    restore_environment();
    return fail(message);
  }
  bool success_job_found = false;
  auto success_job = photo_editor::desktop::PersistentJobRecord{};
  for (const auto& record : photo_editor::desktop::loadPersistedJobRecords()) {
    if (record.job_id == created.import_job_id) {
      success_job = record;
      success_job_found = true;
      break;
    }
  }
  const auto success_receipts_match = created.source_created && created.import_accepted &&
      created.error.isEmpty() && created.final_status == QStringLiteral("completed") &&
      created.asset_id.size() > 0 && !created.source_sha256.isEmpty() &&
      created.catalog.preparedBlankCanvasStatus(created.task_id).value(QStringLiteral("importJobId")).toString() ==
          created.import_job_id && success_job_found && success_job.status == QStringLiteral("Completed") &&
      success_job.metadata_json.value(QStringLiteral("preparedTaskId")).toString() == created.task_id &&
      success_job.metadata_json.value(QStringLiteral("preparedDocumentId")).toString() == created.document_id &&
      success_job.metadata_json.value(QStringLiteral("preparedSourceSha256")).toString() == created.source_sha256;
  if (!success_receipts_match) {
    const auto detail = created.error.isEmpty() ? created.final_error : created.error;
    restore_environment();
    return fail(QStringLiteral("The successful import did not preserve the preallocated task/job/source identity. %1")
                    .arg(detail));
  }
  const auto adopted_reverted = run_prepared_recipe_edit_and_revert(&created);
  if (!adopted_reverted) {
    const auto message = created.error.isEmpty()
        ? QStringLiteral("The created target did not adopt, edit, and revert through one exact journal.")
        : created.error;
    restore_environment();
    return fail(message);
  }

  // A large real canvas gives the cooperative import worker enough source data
  // to observe a cancellation while its persisted job is Running.
  PreparedCanvasTaskProbe canceled;
  const QVariantMap large_settings{{QStringLiteral("width"), 8192},
      {QStringLiteral("height"), 3900}, {QStringLiteral("name"), QStringLiteral("Canceled importer canvas")},
      {QStringLiteral("background"), QStringLiteral("white")}};
  if (!canceled.prepareAndStart(large_settings)) {
    restore_environment();
    return fail(canceled.error.isEmpty() ? QStringLiteral("The cancel-during-import fixture did not start.") : canceled.error);
  }
  const auto running = wait_until([&] {
    if (canceled.import_job_id.isEmpty()) return false;
    const auto records = photo_editor::desktop::loadPersistedJobRecords();
    return std::any_of(records.cbegin(), records.cend(), [&](const auto& record) {
      return record.job_id == canceled.import_job_id && record.status == QStringLiteral("Running");
    });
  }, 180000);
  if (!running || !canceled.cancelImporter()) {
    static_cast<void>(canceled.cancelImporter());
    static_cast<void>(wait_until([&] { return canceled.finished; }, 180000));
    if (canceled.tasks.journal()->hasChanges()) static_cast<void>(canceled.tasks.revert());
    restore_environment();
    return fail(QStringLiteral("The importer did not accept cancellation while its exact job was Running."));
  }
  bool cancellation_persisted = false;
  for (const auto& record : photo_editor::desktop::loadPersistedJobRecords()) {
    if (record.job_id != canceled.import_job_id) continue;
    cancellation_persisted = record.status == QStringLiteral("Canceling") ||
        record.status == QStringLiteral("Canceled") ||
        record.metadata_json.value(QStringLiteral("cancellationRequested")).toBool();
    break;
  }
  if (!cancellation_persisted || !wait_until([&] { return canceled.finished; }, 180000) ||
      canceled.error.size() > 0 || canceled.import_job_id !=
          canceled.catalog.preparedBlankCanvasStatus(canceled.task_id).value(QStringLiteral("importJobId")).toString()) {
    const auto message = canceled.error.isEmpty()
        ? QStringLiteral("The canceled importer did not preserve its producer/job identity through settlement.")
        : canceled.error;
    restore_environment();
    return fail(message);
  }
  if (!canceled.file_owner || !canceled.file_owner->recordCreatedReceipt(canceled.source_sha256)) {
    restore_environment();
    return fail(QStringLiteral("Cancellation settlement lost the exact authored source receipt."));
  }
  const auto canceled_reverted = canceled.tasks.revert();
  const auto canceled_file = QJsonDocument::fromJson(canceled.file_owner->snapshot()).object();
  const auto canceled_row = QJsonDocument::fromJson(canceled.rowSnapshot()).object();
  if (!canceled_reverted.value(QStringLiteral("ok")).toBool() ||
      canceled_reverted.value(QStringLiteral("status")).toString() != QStringLiteral("reverted") ||
      canceled_file.value(QStringLiteral("exists")).toBool() ||
      canceled_row.value(QStringLiteral("state")).toString() != QStringLiteral("absent")) {
    restore_environment();
    return fail(QStringLiteral("The canceled create/import did not settle and revert exact owned source and row state."));
  }

  restore_environment();
  std::cout << "Prepared canvas import retained its task identity, adopted and reverted exact recipe/file ownership, and canceled an active importer\n";
  return EXIT_SUCCESS;
}

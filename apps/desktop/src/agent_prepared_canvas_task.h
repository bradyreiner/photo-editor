#pragma once

#include "agent_created_file_resource.h"
#include "agent_edit_controller.h"
#include "agent_session_controller.h"
#include "agent_task_coordinator.h"
#include "app_bootstrap.h"
#include "blank_canvas_document.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"

namespace photo_editor::desktop {

// Creation is the first operation of the same reversible editing task. Source
// and row absence are captured before the worker starts; accepted jobs settle
// before restoration or adoption by the ordinary photo executor.
class AgentPreparedCanvasTask final : public QObject {
 public:
  AgentPreparedCanvasTask(CatalogAssetModel& catalog, EditorPreviewController& preview,
      AgentEditController& executor, AgentSessionController& session, AgentTaskCoordinator& tasks)
      : catalog_(catalog), preview_(preview), executor_(executor), session_(session), tasks_(tasks) {
    connect(&catalog_, &CatalogAssetModel::preparedBlankCanvasSourceCreated, this,
        [this](const QString& task, const QString&, const QString& path, const QString& sha) {
      if (!preparing_ || task != task_id_ || path != source_path_) return;
      if (!file_->recordCreatedReceipt(sha)) {
        stopWithError("The created source does not match the native completion receipt.");
        return;
      }
      source_sha_ = sha;
      if (!tasks_.journal()->settle(source_id_, file_->snapshot(), true).value("ok").toBool()) {
        stopWithError("The created source receipt could not be persisted. Its file and recovery journal were preserved.");
        return;
      }
      if (stopping_) { static_cast<void>(catalog_.cancelPreparedBlankCanvas(task)); return; }
      const auto prepared = tasks_.journal()->prepare(row_id_, "catalog.import.canvas",
          {{"jobId", import_job_id_}, {"path", source_path_}}, rowSnapshot());
      if (!prepared.value("ok").toBool()) { stopWithError(prepared.value("error").toString()); return; }
      row_step_ = prepared.value("stepId").toLongLong();
      session_.reportExecutionActivity("Adding the canvas to the project.");
      if (!catalog_.acceptPreparedBlankCanvasSource(task, sha)) stopWithError("The canvas import was not admitted.");
    });
    connect(&catalog_, &CatalogAssetModel::preparedBlankCanvasImportAccepted, this,
        [this](const QString& task, const QString& job, const QString& path, const QString& sha) {
      if (!preparing_ || task != task_id_) return;
      if (job != import_job_id_ || path != source_path_ || sha != source_sha_ || row_step_ < 0) {
        stopWithError("The import receipt does not match this creation task."); return;
      }
      const auto accepted = tasks_.journal()->complete(row_step_, rowSnapshot(),
          {{"ok", true}, {"jobId", job}, {"sourceSha256", sha}}, true);
      if (!accepted.value("ok").toBool()) stopWithError(accepted.value("error").toString());
    });
    connect(&catalog_, &CatalogAssetModel::preparedBlankCanvasFinished, this,
        [this](const QString& task, const QString& job, const QString& status, const QString& asset,
               const QString& path, const QString& sha, const QString& error) {
      if (!preparing_ || task != task_id_ || job != import_job_id_) return;
      // A frontend cancellation flag cannot certify that an importer has stopped.
      if (catalog_.preparedBlankCanvasBusy(task)) { stopWithError("The canvas producer has not settled."); return; }
      const auto journal = tasks_.journal();
      const auto source = file_->snapshot();
      bool receipts_ok = !source.isEmpty();
      if (journal->pending(source_id_)) {
        if (QJsonDocument::fromJson(source).object().value("exists").toBool() && source_sha_.isEmpty()) receipts_ok = false;
        if (!source_sha_.isEmpty()) receipts_ok = receipts_ok && file_->recordCreatedReceipt(source_sha_);
        receipts_ok = receipts_ok && journal->settle(source_id_, source, status == "completed").value("ok").toBool();
      }
      if (row_step_ >= 0) {
        const auto row = rowSnapshot();
        receipts_ok = receipts_ok && !row.isEmpty() && (journal->pending(row_id_)
            ? journal->settle(row_id_, row, status == "completed")
            : journal->complete(row_step_, row, {{"ok", false}, {"error", error}})).value("ok").toBool();
      }
      if (!receipts_ok) { finishFailure("The canvas operation needs recovery before editing or reversal."); return; }
      if (stopping_ || status != "completed" || asset.isEmpty() || path != source_path_ || sha != source_sha_) {
        finishFailure(error.isEmpty() ? QStringLiteral("Canvas creation stopped.") : error);
        return;
      }
      const auto recipe_id = QStringLiteral("catalog-recipe:%1").arg(asset);
      const auto recipe = tasks_.enroll(recipe_id, catalog_.agentTaskRecipeResourceAdapter(asset, task));
      if (!recipe.value("ok").toBool()) { finishFailure(recipe.value("error").toString()); return; }
      const auto resolved = session_.resolvePreparedCanvasTarget(task, asset);
      if (!resolved.value("ok").toBool()) { finishFailure(resolved.value("error").toString()); return; }
      auto approved = resolved.value("approvedSnapshot").toMap();
      approved.insert("approvedInstructions", approved_.value("approvedInstructions"));
      approved.insert("taskScopedCapabilities", approved_.value("taskScopedCapabilities"));
      const auto persisted = journal->recordApprovedSnapshot(approved);
      const auto pinned = persisted.value("ok").toBool() ? executor_.setApprovedTaskSnapshot(approved) : persisted;
      if (!pinned.value("ok").toBool()) { finishFailure(pinned.value("error").toString()); return; }
      const auto item = catalog_.assetById(asset);
      static_cast<void>(catalog_.selectAssetById(asset, true));
      preview_.loadAsset(asset, path, item.value("thumbnailSource").toString(), item.value("cameraLabel").toString(),
          item.value("captureDate").toString(), item.value("lensModel").toString(), item.value("adjustmentPatchJson").toString());
      session_.reportExecutionActivity("Building the approved design on the new canvas.");
      const auto started = executor_.startDraftForPreparedTask({asset}, approved.value("approvedInstructions").toString(), task);
      preparing_ = false;
      if (!started.value("ok").toBool()) session_.acknowledgeExecution(started);
    });
  }

  [[nodiscard]] bool preparing() const { return preparing_; }
  [[nodiscard]] bool ownsTask() const {
    // Ownership transfers to AgentEditController when it adopts this journal.
    // Keep that transfer sticky after the execution request settles: activeTaskId
    // is intentionally empty in Review, while taskTransaction retains the owner.
    const auto editor_task_id = executor_.taskTransaction().value("taskId").toString();
    return !task_id_.isEmpty() && tasks_.journal()->taskId() == task_id_ &&
        editor_task_id != task_id_ && tasks_.journal()->hasChanges();
  }

  [[nodiscard]] QVariantMap start(const QVariantMap& approved) {
    if (preparing_ || executor_.running() || executor_.draftActive())
      return failure("Review or revert the current task before creating another canvas.");
    const auto settings = approved.value("plan").toMap().value("canvasSettings").toMap();
    if (!approved.value("blankCanvas").toBool() || !approved.value("targetIds").toStringList().isEmpty() ||
        !settings.contains("width") || !settings.contains("height"))
      return failure("The approved canvas plan must include its output dimensions.");
    const auto task = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto document = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto job = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto root = QDir(AppBootstrap::appDataLocation()).filePath("canvas-documents");
    const auto path = planned_blank_canvas_source_path(root, settings, document);
    auto file = std::make_shared<AgentCreatedFileResource>(root, path,
        [this, task] { static_cast<void>(catalog_.cancelPreparedBlankCanvas(task)); },
        [this, task] { return !catalog_.preparedBlankCanvasBusy(task); });
    const auto source_before = file->snapshot();
    const auto row_before = catalog_.preparedBlankCanvasCatalogSnapshot(path, task, job);
    if (path.isEmpty() || source_before.isEmpty() || row_before.isEmpty() ||
        QJsonDocument::fromJson(source_before).object().value("exists").toBool() ||
        QJsonDocument::fromJson(row_before).object().value("state") != "absent")
      return failure("The planned canvas path is unavailable or already occupied.");
    const auto begun = tasks_.begin(AppBootstrap::appDataLocation(), task);
    if (!begun.value("ok").toBool()) return begun;
    task_id_ = task; import_job_id_ = job; source_path_ = path; source_sha_.clear();
    source_id_ = "created-file:" + document; row_id_ = "catalog-row:" + document;
    file_ = std::move(file); approved_ = approved; stopping_ = false; reverting_ = false; row_step_ = -1; failure_.clear();
    const auto persisted = tasks_.journal()->recordApprovedSnapshot(approved);
    if (!persisted.value("ok").toBool()) return rejectAdmission(persisted);
    const auto enrolled_file = tasks_.enroll(source_id_, file_->adapter());
    if (!enrolled_file.value("ok").toBool()) return rejectAdmission(enrolled_file);
    const auto enrolled_row = tasks_.enroll(row_id_,
        catalog_.preparedBlankCanvasCatalogResourceAdapter(source_path_, task_id_, import_job_id_));
    if (!enrolled_row.value("ok").toBool()) return rejectAdmission(enrolled_row);
    const auto prepared = tasks_.journal()->prepare(source_id_, "canvas.create",
        {{"documentId", document}, {"path", path}}, source_before);
    if (!prepared.value("ok").toBool()) return rejectAdmission(prepared);
    preparing_ = true;
    const auto started = catalog_.startPreparedBlankCanvas(task, document, job, settings);
    const auto accepted = tasks_.journal()->complete(prepared.value("stepId").toLongLong(), file_->snapshot(),
        {{"ok", started}, {"jobId", job}, {"documentId", document}}, started);
    if (!accepted.value("ok").toBool()) { stopWithError(accepted.value("error").toString()); return accepted; }
    if (!started) { preparing_ = false; static_cast<void>(tasks_.finish("interrupted")); return failure("Canvas creation was not admitted."); }
    session_.reportExecutionActivity("Creating the approved blank canvas.");
    return {{"ok", true}, {"queued", true}, {"running", true}, {"taskId", task}};
  }

  [[nodiscard]] QVariantMap control(const QString& action) {
    if (!preparing_) {
      if (action != "revert" || !ownsTask()) return failure("This canvas preparation has already settled.");
      auto result = tasks_.revert();
      result.insert("taskTransaction", tasks_.state());
      if (result.value("ok").toBool()) result.insert("status", "reverted");
      session_.acknowledgeExecution(result);
      return result;
    }
    stopping_ = true;
    reverting_ = reverting_ || action == "revert";
    static_cast<void>(catalog_.cancelPreparedBlankCanvas(task_id_));
    return {{"ok", true}, {"pending", true}, {"taskId", task_id_}};
  }

 private:
  static QVariantMap failure(const QString& message) { return {{"ok", false}, {"error", message}}; }
  QByteArray rowSnapshot() const { return catalog_.preparedBlankCanvasCatalogSnapshot(source_path_, task_id_, import_job_id_); }
  QVariantMap rejectAdmission(const QVariantMap& result) {
    static_cast<void>(tasks_.abandonAdmission());
    return result;
  }
  void stopWithError(const QString& message) {
    stopping_ = true;
    failure_ = message;
    static_cast<void>(catalog_.cancelPreparedBlankCanvas(task_id_));
  }
  void finishFailure(const QString& message) {
    preparing_ = false;
    static_cast<void>(tasks_.finish("interrupted"));
    auto result = failure(failure_.isEmpty() ? message : failure_);
    result.insert("taskId", task_id_);
    result.insert("taskTransaction", tasks_.state());
    if (reverting_) {
      result = tasks_.revert();
      result.insert("taskTransaction", tasks_.state());
      result.insert("status", result.value("ok").toBool() ? "reverted" : "interrupted");
    } else if (stopping_) result.insert("status", "interrupted");
    session_.acknowledgeExecution(result);
  }
  CatalogAssetModel& catalog_;
  EditorPreviewController& preview_;
  AgentEditController& executor_;
  AgentSessionController& session_;
  AgentTaskCoordinator& tasks_;
  std::shared_ptr<AgentCreatedFileResource> file_;
  QVariantMap approved_;
  QString task_id_, import_job_id_, source_path_, source_sha_, source_id_, row_id_, failure_;
  qint64 row_step_ = -1;
  bool preparing_ = false, stopping_ = false, reverting_ = false;
};

}  // namespace photo_editor::desktop

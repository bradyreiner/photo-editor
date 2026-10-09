#pragma once

#include "agent_created_file_resource.h"
#include "agent_task_coordinator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QVariantMap>

#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace photo_editor::desktop {

struct AgentPhotoOutputPublishResult final {
  bool published = false;
  bool receipt_recorded = false;
  bool cancelled = false;
  QString sha256;
  qint64 byte_count = 0;
  QByteArray authored_snapshot;
  QString error;
};

// Owns one new flattened/layer PNG or JPEG export. The editor renders to a
// private staging directory. A completed stage is linked into the requested
// destination with create-if-absent semantics, so a file appearing during the
// render is never replaced. Existing-file replacement needs a separate backup
// resource and is deliberately not admitted by this owner.
class AgentPhotoOutputTask final : public QObject {
 public:
  using AdmissionHandler = std::function<QVariantMap(
      const QString& task_id, const QString& asset_id, const QString& output_path,
      const QString& operation, const QString& layer_id)>;
  using StartHandler = std::function<bool(
      const QString& operation, const QString& layer_id, const QString& staging_path)>;
  using CancelHandler = std::function<bool()>;
  using BusyHandler = std::function<bool()>;
  using CompletionSnapshotHandler = std::function<QVariantMap(
      const QString& task_id, const QString& asset_id, const QString& output_path,
      const QString& operation, const QString& layer_id,
      const QVariantMap& approved_snapshot)>;
  using FinishedHandler = std::function<void(const QVariantMap&)>;

  AgentPhotoOutputTask(AgentTaskCoordinator& tasks, AdmissionHandler admit,
      StartHandler start_native, CancelHandler cancel_native, BusyHandler native_busy,
      CompletionSnapshotHandler completion_snapshot, QObject* parent = nullptr)
      : QObject(parent), tasks_(tasks), admit_(std::move(admit)),
        start_native_(std::move(start_native)), cancel_native_(std::move(cancel_native)),
        native_busy_(std::move(native_busy)),
        completion_snapshot_(std::move(completion_snapshot)) {
    connect(&publisher_watcher_, &QFutureWatcher<AgentPhotoOutputPublishResult>::finished,
        this, [this] { finishStagedHash(); });
    connect(&receipt_watcher_, &QFutureWatcher<AgentPhotoOutputPublishResult>::finished,
        this, [this] { finishOutputReceipt(); });
    connect(&settlement_watcher_, &QFutureWatcher<SettlementAuditResult>::finished,
        this, [this] { finishSettlementAudit(); });
  }

  ~AgentPhotoOutputTask() override {
    if (!active_) return;
    requestStop(active_);
    // A worker may still be writing the staged file. Retain its private bytes
    // until the process has quiesced; startup recovery can inspect the recorded
    // staging path without replaying the export.
    if (active_->staging_directory) active_->staging_directory->setAutoRemove(false);
  }

  [[nodiscard]] bool busy() const { return static_cast<bool>(active_); }
  [[nodiscard]] QString resourceId() const {
    return active_ ? active_->resource_id : last_resource_id_;
  }
  [[nodiscard]] QString stagingPath() const {
    return active_ ? active_->staging_path : last_staging_path_;
  }
  [[nodiscard]] QVariantMap lastResult() const { return last_result_; }
  void setFinishedHandler(FinishedHandler handler) { finished_handler_ = std::move(handler); }

  // operation is "flattened" or "layer"; layerId is required for a layer
  // export. Admission must verify the approved task target and currently open
  // source. It returns {ok:true, approvedSnapshot:{...}}. The snapshot is an
  // opaque exact map that should bind source/catalog/native recipe state,
  // relevant layer state, and approved destination identity. The mandatory
  // completion callback returns {ok:true, currentSnapshot:{...}} with the same
  // shape; publication is refused unless those maps compare exactly. The
  // source's own resource checkpoint must already be enrolled.
  [[nodiscard]] QVariantMap start(const QString& task_id, const QString& asset_id,
      const QString& output_path, const QString& operation, const QString& layer_id = {}) {
    if (busy()) return failure(QStringLiteral("A task-owned photo export is already running."));
    if (!tasks_.journal() || tasks_.journal()->taskId() != task_id ||
        tasks_.journal()->report().value(QStringLiteral("status")) != QStringLiteral("running"))
      return failure(QStringLiteral("Start or resume the approved task before exporting."));
    if (asset_id.trimmed().isEmpty() || tasks_.journal()->checkpointHash(asset_id).isEmpty())
      return failure(QStringLiteral("The export source is not enrolled in this task."));
    if (operation != QStringLiteral("flattened") && operation != QStringLiteral("layer"))
      return failure(QStringLiteral("Choose a flattened or layer photo output."));
    if (operation == QStringLiteral("layer") && layer_id.trimmed().isEmpty())
      return failure(QStringLiteral("A layer export requires an enrolled layer ID."));
    if (!admit_ || !start_native_ || !cancel_native_ || !native_busy_ || !completion_snapshot_)
      return failure(QStringLiteral("The task-owned export adapter is unavailable."));
    if (native_busy_())
      return failure(QStringLiteral("Wait for the current photo export to finish."));

    QString normalized_path;
    QString format;
    QString output_root;
    const auto path_error = normalizeOutputPath(output_path, &normalized_path, &output_root, &format);
    if (!path_error.isEmpty()) return failure(path_error);
    const auto admission = admit_(task_id, asset_id, normalized_path, operation, layer_id);
    if (!admission.value(QStringLiteral("ok")).toBool()) return admission;
    const auto approved_snapshot = admission.value(QStringLiteral("approvedSnapshot")).toMap();
    if (approved_snapshot.isEmpty())
      return failure(QStringLiteral("The export source and approved destination could not be pinned."));

    const QFileInfo target_info(normalized_path);
    if (target_info.exists() || target_info.isSymLink())
      return failure(QStringLiteral("This destination already exists. An approved replacement backup is required before overwriting it."));

    const auto resource_id = resourceIdFor(task_id, normalized_path);
    auto state = std::make_shared<ActiveOutput>();
    state->task_id = task_id;
    state->asset_id = asset_id;
    state->resource_id = resource_id;
    state->output_path = normalized_path;
    state->output_root = output_root;
    state->format = format;
    state->operation = operation;
    state->layer_id = layer_id;
    state->approved_snapshot = approved_snapshot;
    state->cancel_native = cancel_native_;
    state->file = std::make_shared<AgentCreatedFileResource>(output_root, normalized_path,
        [weak = std::weak_ptr<ActiveOutput>(state)] {
          if (const auto active = weak.lock()) requestStop(active);
        },
        [weak = std::weak_ptr<ActiveOutput>(state)] {
          const auto active = weak.lock();
          return !active || !active->producer_busy.load();
        });
    auto adapter = state->file->adapter();
    const auto file_read = adapter.read;
    const auto file_restore = adapter.restore;
    adapter.read = [state, file_read] {
      // Revert first sees the unchanged destination, then invokes cancel. Hide
      // a canceled in-flight producer from the follow-up preflight until the
      // actual native terminal callback settles it.
      if (state->producer_busy.load() && state->cancel_requested.load()) return QByteArray{};
      return file_read();
    };
    adapter.restore = [state, file_restore](const QByteArray& baseline_bytes) {
      return state && file_restore(baseline_bytes);
    };
    adapter.cancel = [weak = std::weak_ptr<ActiveOutput>(state)] {
      if (const auto active = weak.lock()) requestStop(active);
    };
    const auto baseline = state->file->snapshot();
    const auto baseline_state = QJsonDocument::fromJson(baseline).object();
    if (baseline.isEmpty() || baseline_state.value(QStringLiteral("exists")).toBool())
      return failure(QStringLiteral("The output destination is not an absent, safe file path."));
    const auto journal = tasks_.journal();
    if (journal->checkpointHash(resource_id).isEmpty()) {
      const auto captured = journal->capture(resource_id, baseline);
      if (!captured.value(QStringLiteral("ok")).toBool()) return captured;
    } else if (journal->baseline(resource_id) != baseline || !journal->matchesCheckpoint(resource_id, baseline)) {
      return failure(QStringLiteral("This output path already has a different task checkpoint."));
    }
    const auto enrolled = tasks_.enroll(resource_id, std::move(adapter));
    if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
    // A user may create the requested file after the first absence read. Keep
    // the pre-enrolled absence as the task baseline and fail closed on the
    // race; never capture the newly appeared file as agent-owned state.
    if (state->file->snapshot() != baseline)
      return failure(QStringLiteral("The destination changed during export admission; its current file was preserved."));
    state->baseline = baseline;
    state->publication = std::make_shared<PublicationControl>();
    state->staging_directory = std::make_shared<QTemporaryDir>(
        QDir(output_root).filePath(QStringLiteral(".looksy-agent-output-XXXXXX")));
    if (!state->staging_directory->isValid())
      return failure(QStringLiteral("Could not create a private export staging directory."));
    const auto stage_suffix = format == QStringLiteral("jpeg") ? QStringLiteral("jpg") : format;
    state->staging_path = QDir(state->staging_directory->path()).filePath(
        QStringLiteral("render.%1").arg(stage_suffix));

    const auto prepared = tasks_.journal()->prepare(resource_id, QStringLiteral("photo.export_%1").arg(format),
        {{QStringLiteral("assetId"), asset_id}, {QStringLiteral("outputPath"), normalized_path},
         {QStringLiteral("operation"), operation}, {QStringLiteral("layerId"), layer_id},
         {QStringLiteral("format"), format}, {QStringLiteral("stagingPath"), state->staging_path},
         {QStringLiteral("approvedSnapshot"), approved_snapshot}}, baseline);
    if (!prepared.value(QStringLiteral("ok")).toBool()) return prepared;
    state->step_id = prepared.value(QStringLiteral("stepId")).toLongLong();
    state->producer_busy.store(true);
    active_ = state;
    last_resource_id_ = resource_id;
    last_staging_path_ = state->staging_path;

    // The real editor callback must invoke only the native asynchronous start
    // API and return true only after its producer is actually accepted. Journal
    // acceptance is persisted afterwards; const synchronous file writers are
    // never called by this adapter.
    bool started = false;
    try {
      started = start_native_(operation, layer_id, state->staging_path);
    } catch (...) {
      started = false;
    }
    if (!started) {
      auto result = failure(QStringLiteral("The editor did not confirm acceptance of the staged photo export."));
      if (native_busy_()) {
        // A false admission result cannot certify that no worker escaped. Keep
        // the prepared journal step and wait for its real terminal callback.
        requestStop(state);
        result.insert(QStringLiteral("pending"), true);
        result.insert(QStringLiteral("journalPending"), tasks_.journal()->pending(resource_id));
        result.insert(QStringLiteral("unresolved"), true);
        result.insert(QStringLiteral("recoveryRequired"), true);
        result.insert(QStringLiteral("resourceId"), resource_id);
        result.insert(QStringLiteral("taskStepId"), state->step_id);
        last_result_ = result;
        return result;
      }
      state->producer_busy.store(false);
      const auto actual_destination = state->file->snapshot();
      if (actual_destination.isEmpty() || actual_destination != baseline) {
        retainStageIfPresent(state);
        result.insert(QStringLiteral("pending"), true);
        result.insert(QStringLiteral("journalPending"), tasks_.journal()->pending(resource_id));
        result.insert(QStringLiteral("unresolved"), true);
        result.insert(QStringLiteral("recoveryRequired"), true);
        result.insert(QStringLiteral("destinationConflict"), true);
        result.insert(QStringLiteral("resourceId"), resource_id);
        result.insert(QStringLiteral("taskStepId"), state->step_id);
        result.insert(QStringLiteral("actualSnapshotHash"), snapshotHash(actual_destination));
        last_result_ = result;
        return result;
      }
      const auto completed = tasks_.journal()->complete(state->step_id, baseline,
          {{QStringLiteral("ok"), false}, {QStringLiteral("error"), result.value(QStringLiteral("error"))}});
      if (!completed.value(QStringLiteral("ok")).toBool()) {
        result.insert(QStringLiteral("recoveryRequired"), true);
        result.insert(QStringLiteral("journalError"), completed.value(QStringLiteral("error")));
      }
      retainStageIfPresent(state);
      result.insert(QStringLiteral("pending"), false);
      result.insert(QStringLiteral("resourceId"), resource_id);
      result.insert(QStringLiteral("taskStepId"), state->step_id);
      if (completed.value(QStringLiteral("ok")).toBool()) active_.reset();
      last_result_ = result;
      return result;
    }

    const auto accepted = tasks_.journal()->complete(state->step_id, baseline,
        {{QStringLiteral("ok"), true}, {QStringLiteral("accepted"), true},
         {QStringLiteral("assetId"), asset_id}, {QStringLiteral("outputPath"), normalized_path}}, true);
    if (!accepted.value(QStringLiteral("ok")).toBool()) {
      requestStop(state);
      state->acceptance_persisted = false;
      auto result = failure(QStringLiteral("The export started, but its acceptance receipt could not be persisted. Recovery is required."));
      result.insert(QStringLiteral("recoveryRequired"), true);
      result.insert(QStringLiteral("taskStepId"), state->step_id);
      last_result_ = result;
      return result;
    }
    state->acceptance_persisted = true;
    QVariantMap result{{QStringLiteral("ok"), true}, {QStringLiteral("accepted"), true},
        {QStringLiteral("pending"), true}, {QStringLiteral("taskId"), task_id},
        {QStringLiteral("assetId"), asset_id}, {QStringLiteral("resourceId"), resource_id},
        {QStringLiteral("taskStepId"), state->step_id}, {QStringLiteral("outputPath"), normalized_path},
        {QStringLiteral("format"), format}, {QStringLiteral("stagingPath"), state->staging_path}};
    last_result_ = result;
    return result;
  }

  [[nodiscard]] QVariantMap stop(const QString& task_id) {
    if (!active_ || active_->task_id != task_id)
      return failure(QStringLiteral("This task does not own an active photo export."));
    const auto phase = active_->publication->phase.load();
    if (phase != PublicationControl::Phase::Running)
      return failure(QStringLiteral("The output is already publishing or settled; wait for its terminal receipt."));
    requestStop(active_);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("stopRequested"), true},
        {QStringLiteral("pending"), true}, {QStringLiteral("taskId"), task_id},
        {QStringLiteral("resourceId"), active_->resource_id},
        {QStringLiteral("nativeCancelAccepted"), active_->native_cancel_accepted}};
  }

  // Connect EditorPreviewController::photoExportFinished to this function.
  // The signal is the terminal producer receipt, not a launch-progress event.
  [[nodiscard]] QVariantMap nativeExportFinished(bool success, bool cancelled,
      const QString& staging_path, const QString& error = {}) {
    if (!active_ || active_->native_terminal_received ||
        QDir::cleanPath(staging_path) != QDir::cleanPath(active_->staging_path))
      return failure(QStringLiteral("The photo export terminal receipt does not match the active staged output."));
    if (native_busy_ && native_busy_())
      return failure(QStringLiteral("The editor reported export completion before its producer became idle."));
    active_->native_terminal_received = true;
    if (!active_->acceptance_persisted) {
      active_->producer_busy.store(false);
      retainStageIfPresent(active_);
      return settleUnacceptedTerminal(active_, QStringLiteral(
          "The staged export ended without a persisted native acceptance receipt."));
    }
    if (cancelled || active_->cancel_requested.load() || !success) {
      active_->producer_busy.store(false);
      retainStageIfPresent(active_);
      auto result = failure(active_->cancel_requested.load() || cancelled
          ? QStringLiteral("The photo export was stopped after its native producer settled.")
          : error.isEmpty() ? QStringLiteral("The photo export failed after its native producer settled.") : error);
      result.insert(QStringLiteral("cancelled"), cancelled || active_->cancel_requested.load());
      return settleTerminal(active_, false, {}, result);
    }
    if (!QFileInfo(active_->staging_path).isFile() || QFileInfo(active_->staging_path).isSymLink()) {
      active_->producer_busy.store(false);
      auto result = failure(QStringLiteral("The editor reported success without a regular staged output file."));
      return settleTerminal(active_, false, {}, result);
    }

    const auto state = active_;
    publisher_watcher_.setFuture(QtConcurrent::run([state] { return hashStagedOutput(state); }));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("pending"), true},
        {QStringLiteral("taskId"), active_->task_id}, {QStringLiteral("resourceId"), active_->resource_id},
        {QStringLiteral("taskStepId"), active_->step_id}};
  }

  // Retry only the journal settlement after a durable receipt was obtained.
  // This never republishes the staged image or replays the native export.
  [[nodiscard]] QVariantMap retrySettlement(const QString& task_id) {
    if (!active_ || active_->task_id != task_id || active_->producer_busy.load())
      return failure(QStringLiteral("There is no settled export receipt to reconcile."));
    if (!active_->acceptance_persisted)
      return settleUnacceptedTerminal(active_, QStringLiteral(
          "The staged export ended without a persisted native acceptance receipt."));
    if (!active_->publication_result.has_value())
      return failure(QStringLiteral("There is no settled export receipt to reconcile."));
    const auto publish = *active_->publication_result;
    const auto result = publish.published && publish.receipt_recorded
        ? successResult(active_, publish)
        : failure(publish.error.isEmpty() ? QStringLiteral("The staged output was not published.") : publish.error);
    return settleTerminal(active_, publish.published && publish.receipt_recorded, publish, result);
  }

  // Reattach an output owner after the coordinator has recovered its journal.
  // A settled output is trusted only when its live file snapshot matches the
  // persisted task checkpoint. A pending producer from a prior process is
  // never replayed; an uncertain already-published path is preserved as a
  // conflict without claiming an authored receipt.
  [[nodiscard]] QVariantMap reattach(const QString& task_id, const QString& asset_id,
      const QString& output_path) {
    if (busy()) return failure(QStringLiteral("Another task-owned photo export is active."));
    if (!native_busy_ || native_busy_())
      return failure(QStringLiteral("Wait until the native photo exporter is idle before recovery."));
    const auto journal = tasks_.journal();
    if (!journal || journal->taskId() != task_id || asset_id.trimmed().isEmpty() ||
        journal->checkpointHash(asset_id).isEmpty())
      return failure(QStringLiteral("The recovered task does not own this photo source."));
    QString normalized_path;
    QString format;
    QString output_root;
    const auto path_error = normalizeOutputPath(output_path, &normalized_path, &output_root, &format);
    if (!path_error.isEmpty()) return failure(path_error);
    const auto resource_id = resourceIdFor(task_id, normalized_path);
    const auto state_report = journal->report();
    QVariantMap resource_record;
    for (const auto& value : state_report.value(QStringLiteral("resources")).toList()) {
      const auto candidate = value.toMap();
      if (candidate.value(QStringLiteral("id")).toString() == resource_id) {
        resource_record = candidate;
        break;
      }
    }
    if (resource_record.isEmpty()) return failure(QStringLiteral("The recovered journal has no matching output owner."));

    QString recorded_stage;
    QVariantMap recorded_step;
    QVariantMap recorded_arguments;
    for (const auto& value : state_report.value(QStringLiteral("steps")).toList()) {
      const auto step = value.toMap();
      if (step.value(QStringLiteral("resourceId")).toString() != resource_id ||
          !step.value(QStringLiteral("command")).toString().startsWith(QStringLiteral("photo.export_"))) continue;
      const auto arguments = step.value(QStringLiteral("arguments")).toMap();
      if (arguments.value(QStringLiteral("assetId")).toString() != asset_id ||
          arguments.value(QStringLiteral("outputPath")).toString() != normalized_path) continue;
      recorded_step = step;
      recorded_arguments = arguments;
      recorded_stage = arguments.value(QStringLiteral("stagingPath")).toString();
    }
    if (recorded_step.isEmpty()) return failure(QStringLiteral("The recovered output step does not match this source and destination."));
    const auto approved_snapshot = recorded_arguments.value(QStringLiteral("approvedSnapshot")).toMap();
    if (approved_snapshot.isEmpty())
      return failure(QStringLiteral("The recovered output has no pinned source and destination snapshot."));

    auto state = std::make_shared<ActiveOutput>();
    state->task_id = task_id;
    state->asset_id = asset_id;
    state->resource_id = resource_id;
    state->output_path = normalized_path;
    state->output_root = output_root;
    state->format = format;
    state->operation = recorded_arguments.value(QStringLiteral("operation")).toString();
    state->layer_id = recorded_arguments.value(QStringLiteral("layerId")).toString();
    state->approved_snapshot = approved_snapshot;
    state->staging_path = recorded_stage;
    state->cancel_native = cancel_native_;
    state->file = std::make_shared<AgentCreatedFileResource>(output_root, normalized_path,
        [] {}, [] { return true; });
    auto adapter = state->file->adapter();
    const auto file_read = adapter.read;
    const auto file_restore = adapter.restore;
    adapter.read = [state, file_read] { return state ? file_read() : QByteArray{}; };
    adapter.restore = [state, file_restore](const QByteArray& baseline) {
      return state && file_restore(baseline);
    };
    adapter.cancel = [] {};
    const auto enrolled = tasks_.enroll(resource_id, std::move(adapter));
    if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;

    state->baseline = journal->baseline(resource_id);
    const auto baseline_state = QJsonDocument::fromJson(state->baseline).object();
    if (state->baseline.isEmpty() || baseline_state.value(QStringLiteral("exists")).toBool())
      return failure(QStringLiteral("The task output baseline is not an absent destination."));
    const auto current = state->file->snapshot();
    if (current.isEmpty()) return failure(QStringLiteral("The recovered output path is not a safe regular file."));
    if (resource_record.value(QStringLiteral("pending")).toBool()) {
      if (current == state->baseline) {
        const auto settled = journal->settle(resource_id, state->baseline, false);
        if (!settled.value(QStringLiteral("ok")).toBool()) {
          auto result = settled;
          result.insert(QStringLiteral("recoveryRequired"), true);
          result.insert(QStringLiteral("pending"), true);
          result.insert(QStringLiteral("journalPending"), journal->pending(resource_id));
          return result;
        }
      } else {
        if (!recorded_stage.isEmpty() && QFileInfo(recorded_stage).isFile())
          preserved_stage_path_ = recorded_stage;
        last_resource_id_ = resource_id;
        last_staging_path_ = recorded_stage;
        auto result = failure(QStringLiteral("The interrupted export has an unreceipted destination. Its current bytes and accepted journal step were preserved."));
        result.insert(QStringLiteral("recoveryRequired"), true);
        result.insert(QStringLiteral("pending"), true);
        result.insert(QStringLiteral("journalPending"), true);
        result.insert(QStringLiteral("destinationConflict"), true);
        result.insert(QStringLiteral("actualSnapshotHash"), snapshotHash(current));
        result.insert(QStringLiteral("resourceId"), resource_id);
        result.insert(QStringLiteral("taskStepId"), recorded_step.value(QStringLiteral("id")));
        if (!preserved_stage_path_.isEmpty()) result.insert(QStringLiteral("recoveryPath"), preserved_stage_path_);
        last_result_ = result;
        return result;
      }
      if (!recorded_stage.isEmpty() && QFileInfo(recorded_stage).isFile())
        preserved_stage_path_ = recorded_stage;
      if (!recorded_stage.isEmpty() && QFileInfo(recorded_stage).isFile())
        state->recovery_path = recorded_stage;
      auto result = failure(QStringLiteral("The interrupted export was not replayed. Inspect or resolve its preserved recovery bytes."));
      result.insert(QStringLiteral("recoveryRequired"), true);
      result.insert(QStringLiteral("resourceId"), resource_id);
      result.insert(QStringLiteral("taskStepId"), recorded_step.value(QStringLiteral("id")));
      if (!preserved_stage_path_.isEmpty()) result.insert(QStringLiteral("recoveryPath"), preserved_stage_path_);
      last_result_ = result;
      return result;
    }

    const auto baseline_hash = resource_record.value(QStringLiteral("beforeHash")).toString();
    const auto current_hash = QString::fromLatin1(QCryptographicHash::hash(current, QCryptographicHash::Sha256).toHex());
    if (current_hash == resource_record.value(QStringLiteral("currentHash")).toString() &&
        current != state->baseline) {
      const auto current_state = QJsonDocument::fromJson(current).object();
      if (!state->file->recordCreatedReceipt(current_state.value(QStringLiteral("sha256")).toString()))
        return failure(QStringLiteral("The recovered output could not be authenticated to its exact persisted checkpoint."));
    } else if (current_hash != baseline_hash && current != state->baseline) {
      last_resource_id_ = resource_id;
      last_staging_path_ = recorded_stage;
      return failure(QStringLiteral("The recovered output changed outside its task receipt; its current file was preserved."));
    }
    if (!completion_snapshot_)
      return failure(QStringLiteral("The recovered output cannot revalidate its approved source snapshot."));
    QVariantMap validation;
    try {
      validation = completion_snapshot_(task_id, asset_id, normalized_path,
          state->operation, state->layer_id, state->approved_snapshot);
    } catch (...) {
      validation = failure(QStringLiteral("The recovered output source could not be revalidated."));
    }
    const auto current_source = validation.value(QStringLiteral("currentSnapshot")).toMap();
    if (!validation.value(QStringLiteral("ok")).toBool() || current_source.isEmpty() ||
        current_source != state->approved_snapshot) {
      last_resource_id_ = resource_id;
      last_staging_path_ = recorded_stage;
      auto result = failure(validation.value(QStringLiteral("error")).toString().isEmpty()
          ? QStringLiteral("The recovered output's approved source or destination changed; its file was preserved.")
          : validation.value(QStringLiteral("error")).toString());
      result.insert(QStringLiteral("recoveryRequired"), true);
      result.insert(QStringLiteral("sourceSnapshotChanged"), !current_source.isEmpty() &&
          current_source != state->approved_snapshot);
      result.insert(QStringLiteral("resourceId"), resource_id);
      return result;
    }
    last_resource_id_ = resource_id;
    last_staging_path_ = recorded_stage;
    last_result_ = {{QStringLiteral("ok"), true}, {QStringLiteral("reattached"), true},
        {QStringLiteral("taskId"), task_id}, {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("resourceId"), resource_id}, {QStringLiteral("outputPath"), normalized_path}};
    return last_result_;
  }

 private:
  struct PublicationControl final {
    enum class Phase { Running, Cancelled, Publishing, Finished };
    std::atomic<Phase> phase{Phase::Running};

    bool cancel() {
      auto expected = Phase::Running;
      return phase.compare_exchange_strong(expected, Phase::Cancelled);
    }
    bool beginPublishing() {
      auto expected = Phase::Running;
      return phase.compare_exchange_strong(expected, Phase::Publishing);
    }
  };

  struct ActiveOutput final {
    QString task_id;
    QString asset_id;
    QString resource_id;
    QString output_path;
    QString output_root;
    QString format;
    QString operation;
    QString layer_id;
    QVariantMap approved_snapshot;
    QString staging_path;
    QString recovery_path;
    QByteArray baseline;
    qsizetype step_id = -1;
    std::shared_ptr<AgentCreatedFileResource> file;
    std::shared_ptr<QTemporaryDir> staging_directory;
    std::shared_ptr<PublicationControl> publication;
    CancelHandler cancel_native;
    std::atomic_bool producer_busy{false};
    std::atomic_bool cancel_requested{false};
    bool native_cancel_accepted = false;
    bool native_terminal_received = false;
    bool acceptance_persisted = false;
    std::optional<AgentPhotoOutputPublishResult> publication_result;
  };

  struct SettlementAuditResult final {
    std::shared_ptr<ActiveOutput> state;
    bool success_requested = false;
    AgentPhotoOutputPublishResult publish;
    QVariantMap result;
    QByteArray actual_snapshot;
  };

  [[nodiscard]] static QString normalizeOutputPath(const QString& path,
      QString* normalized_path, QString* output_root, QString* format) {
    const auto trimmed = path.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("Choose an output path.");
    const QFileInfo requested(trimmed);
    const auto suffix = requested.suffix().toLower();
    if (suffix != QStringLiteral("png") && suffix != QStringLiteral("jpg") &&
        suffix != QStringLiteral("jpeg"))
      return QStringLiteral("Task-owned photo output supports PNG and JPEG destinations.");
    const QFileInfo requested_parent(requested.absolutePath());
    if (!requested_parent.exists() || !requested_parent.isDir())
      return QStringLiteral("Choose an existing output folder.");
    const auto canonical_root = requested_parent.canonicalFilePath();
    if (canonical_root.isEmpty())
      return QStringLiteral("The output folder could not be resolved safely.");
    const auto name = requested.fileName();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
      return QStringLiteral("Choose a file name for the photo output.");
    *output_root = QDir::cleanPath(canonical_root);
    *normalized_path = QDir(*output_root).filePath(name);
    *format = suffix == QStringLiteral("png") ? QStringLiteral("png") : QStringLiteral("jpeg");
    return {};
  }

  static AgentPhotoOutputPublishResult hashStagedOutput(const std::shared_ptr<ActiveOutput>& state) {
    AgentPhotoOutputPublishResult result;
    if (state->cancel_requested.load() ||
        state->publication->phase.load() == PublicationControl::Phase::Cancelled) {
      result.cancelled = true;
      result.error = QStringLiteral("The photo output was stopped before publication.");
      return result;
    }
    QFile staged(state->staging_path);
    if (!staged.open(QIODevice::ReadOnly)) {
      result.error = staged.errorString();
      return result;
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while (!staged.atEnd()) {
      const auto block = staged.read(1024 * 1024);
      if (block.isEmpty() && staged.error() != QFileDevice::NoError) {
        result.error = staged.errorString();
        return result;
      }
      digest.addData(block);
    }
    result.byte_count = staged.size();
    result.sha256 = QString::fromLatin1(digest.result().toHex());
    return result;
  }

  static AgentPhotoOutputPublishResult publishHashedOutput(
      const std::shared_ptr<ActiveOutput>& state, AgentPhotoOutputPublishResult result) {
    if (state->cancel_requested.load() || !state->publication->beginPublishing()) {
      result.cancelled = state->cancel_requested.load() ||
          state->publication->phase.load() == PublicationControl::Phase::Cancelled;
      result.error = result.cancelled
          ? QStringLiteral("The photo output was stopped before publication.")
          : QStringLiteral("The photo output publication was already settled.");
      state->publication->phase.store(PublicationControl::Phase::Finished);
      return result;
    }
    std::error_code link_error;
    const auto staged_native_path = nativeFilesystemPath(state->staging_path);
    const auto output_native_path = nativeFilesystemPath(state->output_path);
    if (!staged_native_path || !output_native_path) {
      result.error = QStringLiteral("The staged output path could not be encoded safely.");
      state->publication->phase.store(PublicationControl::Phase::Finished);
      return result;
    }
    std::filesystem::create_hard_link(*staged_native_path, *output_native_path, link_error);
    if (link_error) {
      result.error = link_error == std::errc::file_exists
          ? QStringLiteral("The destination appeared during export; its existing file was preserved.")
          : QStringLiteral("Could not publish the completed image without replacing an existing file: %1")
                .arg(QString::fromStdString(link_error.message()));
      state->publication->phase.store(PublicationControl::Phase::Finished);
      return result;
    }
    result.published = true;
    return result;
  }

  void finishStagedHash() {
    if (!active_) return;
    auto publish = publisher_watcher_.result();
    const auto state = active_;
    if (publish.cancelled || !publish.error.isEmpty() || state->cancel_requested.load()) {
      state->publication->phase.store(PublicationControl::Phase::Finished);
      state->producer_busy.store(false);
      retainStageIfPresent(state);
      auto result = failure(publish.error.isEmpty()
          ? QStringLiteral("The photo output was stopped before publication.") : publish.error);
      result.insert(QStringLiteral("cancelled"), publish.cancelled || state->cancel_requested.load());
      if (!state->recovery_path.isEmpty()) result.insert(QStringLiteral("recoveryPath"), state->recovery_path);
      settleTerminal(state, false, publish, result);
      return;
    }

    QVariantMap validation;
    try {
      validation = completion_snapshot_(state->task_id, state->asset_id,
          state->output_path, state->operation, state->layer_id, state->approved_snapshot);
    } catch (...) {
      validation = failure(QStringLiteral("The completed export could not revalidate its approved source."));
    }
    const auto current_snapshot = validation.value(QStringLiteral("currentSnapshot")).toMap();
    if (!validation.value(QStringLiteral("ok")).toBool() || current_snapshot.isEmpty() ||
        current_snapshot != state->approved_snapshot) {
      state->publication->phase.store(PublicationControl::Phase::Finished);
      state->producer_busy.store(false);
      retainStageIfPresent(state);
      const bool changed = !current_snapshot.isEmpty() && current_snapshot != state->approved_snapshot;
      auto message = validation.value(QStringLiteral("error")).toString();
      if (message.isEmpty()) {
        message = changed
            ? QStringLiteral("The source, layer or approved destination changed during export; the staged result was preserved.")
            : QStringLiteral("The completed export could not be revalidated; the staged result was preserved.");
      }
      auto result = failure(message);
      result.insert(QStringLiteral("conflict"), true);
      result.insert(QStringLiteral("validationFailed"), true);
      result.insert(QStringLiteral("snapshotChanged"), changed);
      if (!state->recovery_path.isEmpty()) result.insert(QStringLiteral("recoveryPath"), state->recovery_path);
      settleTerminal(state, false, publish, result);
      return;
    }

    // Hashing the rendered image is asynchronous. Once that work is done, the
    // current source snapshot is checked on the owning thread immediately
    // before the single no-overwrite filesystem operation.
    publish = publishHashedOutput(state, std::move(publish));
    if (!publish.published) {
      state->publication->phase.store(PublicationControl::Phase::Finished);
      state->producer_busy.store(false);
      retainStageIfPresent(state);
      auto result = failure(publish.error.isEmpty()
          ? QStringLiteral("The completed image could not be published.") : publish.error);
      result.insert(QStringLiteral("conflict"), true);
      if (publish.cancelled || state->cancel_requested.load())
        result.insert(QStringLiteral("cancelled"), true);
      if (!state->recovery_path.isEmpty()) result.insert(QStringLiteral("recoveryPath"), state->recovery_path);
      settleTerminal(state, false, publish, result);
      return;
    }
    receipt_watcher_.setFuture(QtConcurrent::run([state, publish = std::move(publish)]() mutable {
      publish.receipt_recorded = state->file->recordCreatedReceipt(publish.sha256);
      if (publish.receipt_recorded) {
        publish.authored_snapshot = state->file->snapshot();
        const auto authored = QJsonDocument::fromJson(publish.authored_snapshot).object();
        publish.receipt_recorded = !publish.authored_snapshot.isEmpty() &&
            authored.value(QStringLiteral("exists")).toBool() &&
            authored.value(QStringLiteral("sha256")).toString() == publish.sha256;
      }
      if (!publish.receipt_recorded && publish.error.isEmpty())
        publish.error = QStringLiteral("The published image changed before its exact output receipt was recorded.");
      state->publication->phase.store(PublicationControl::Phase::Finished);
      return publish;
    }));
  }

  void finishOutputReceipt() {
    if (!active_) return;
    const auto publish = receipt_watcher_.result();
    const auto state = active_;
    state->publication_result = publish;
    if (!publish.published || !publish.receipt_recorded) retainStageIfPresent(state);
    const auto result = publish.published && publish.receipt_recorded
        ? successResult(state, publish)
        : failure(publish.error.isEmpty() ? QStringLiteral("The photo output could not be authenticated.") : publish.error);
    settleTerminal(state, publish.published && publish.receipt_recorded, publish, result);
  }

  QVariantMap settleTerminal(const std::shared_ptr<ActiveOutput>& state, bool success,
      const AgentPhotoOutputPublishResult& publish, QVariantMap result) {
    if (!active_ || active_ != state) return failure(QStringLiteral("The output settlement belongs to a stale task."));
    state->producer_busy.store(true);
    state->publication_result = publish;
    const auto future = QtConcurrent::run([state, success, publish, result] {
      SettlementAuditResult audit;
      audit.state = state;
      audit.success_requested = success;
      audit.publish = publish;
      audit.result = result;
      audit.actual_snapshot = state->file->snapshot();
      return audit;
    });
    settlement_watcher_.setFuture(future);
    result.insert(QStringLiteral("pending"), true);
    result.insert(QStringLiteral("resourceId"), state->resource_id);
    result.insert(QStringLiteral("taskStepId"), state->step_id);
    result.insert(QStringLiteral("taskId"), state->task_id);
    last_result_ = result;
    return result;
  }

  void finishSettlementAudit() {
    const auto audit = settlement_watcher_.result();
    if (!active_ || active_ != audit.state) return;
    const auto state = audit.state;
    state->producer_busy.store(false);
    bool success = audit.success_requested;
    const auto& publish = audit.publish;
    auto result = audit.result;
    const auto& actual = audit.actual_snapshot;
    const bool exact_output = success && !publish.authored_snapshot.isEmpty() &&
        actual == publish.authored_snapshot;
    const bool exact_absence = actual == state->baseline;
    if (!exact_output && !exact_absence) {
      retainStageIfPresent(state);
      auto unresolved = failure(actual.isEmpty()
          ? QStringLiteral("The output destination could not be read after its producer settled. The journal remains pending.")
          : QStringLiteral("The output destination no longer matches either its absent baseline or exact task receipt. Its current bytes were preserved and the journal remains pending."));
      unresolved.insert(QStringLiteral("conflict"), true);
      unresolved.insert(QStringLiteral("recoveryRequired"), true);
      unresolved.insert(QStringLiteral("pending"), true);
      unresolved.insert(QStringLiteral("journalPending"), tasks_.journal()->pending(state->resource_id));
      unresolved.insert(QStringLiteral("unresolved"), true);
      unresolved.insert(QStringLiteral("resourceId"), state->resource_id);
      unresolved.insert(QStringLiteral("taskStepId"), state->step_id);
      unresolved.insert(QStringLiteral("taskId"), state->task_id);
      unresolved.insert(QStringLiteral("actualSnapshotHash"), snapshotHash(actual));
      if (!state->recovery_path.isEmpty()) unresolved.insert(QStringLiteral("recoveryPath"), state->recovery_path);
      state->publication_result = publish;
      last_result_ = unresolved;
      if (finished_handler_) finished_handler_(unresolved);
      return;
    }

    if (success && !exact_output) {
      success = false;
      result = failure(QStringLiteral("The output disappeared before its exact receipt could be committed."));
      result.insert(QStringLiteral("conflict"), true);
      retainStageIfPresent(state);
    }
    const auto settled = tasks_.journal()->settle(state->resource_id, actual, success);
    if (!settled.value(QStringLiteral("ok")).toBool()) {
      result.insert(QStringLiteral("ok"), false);
      result.insert(QStringLiteral("recoveryRequired"), true);
      result.insert(QStringLiteral("journalError"), settled.value(QStringLiteral("error")));
      result.insert(QStringLiteral("pending"), true);
      result.insert(QStringLiteral("journalPending"), tasks_.journal()->pending(state->resource_id));
      result.insert(QStringLiteral("unresolved"), true);
      state->publication_result = publish;
      last_result_ = result;
      if (finished_handler_) finished_handler_(result);
      return;
    }
    result.insert(QStringLiteral("pending"), false);
    result.insert(QStringLiteral("taskState"), settled);
    result.insert(QStringLiteral("resourceId"), state->resource_id);
    result.insert(QStringLiteral("taskStepId"), state->step_id);
    result.insert(QStringLiteral("taskId"), state->task_id);
    if (success) {
      result.insert(QStringLiteral("outputPath"), state->output_path);
      result.insert(QStringLiteral("format"), state->format);
      result.insert(QStringLiteral("sha256"), publish.sha256);
      result.insert(QStringLiteral("byteCount"), publish.byte_count);
      // The journal now durably owns the exact published bytes and the native
      // producer is terminal. Resource adapters retain ActiveOutput for later
      // Revert, so explicitly remove only this private staging directory here.
      // Failed, conflicting, or unsettled outputs keep their recovery bytes.
      const auto staging_removed = state->staging_directory && state->staging_directory->remove();
      result.insert(QStringLiteral("stagingRemoved"), staging_removed);
      if (!staging_removed)
        result.insert(QStringLiteral("stagingCleanupError"),
            QStringLiteral("The output receipt settled, but its private staging directory could not be removed."));
    } else {
      if (!state->recovery_path.isEmpty())
        result.insert(QStringLiteral("recoveryPath"), state->recovery_path);
    }
    last_result_ = result;
    active_.reset();
    if (finished_handler_) finished_handler_(result);
  }

  QVariantMap settleUnacceptedTerminal(const std::shared_ptr<ActiveOutput>& state,
      const QString& message) {
    if (!active_ || active_ != state)
      return failure(QStringLiteral("The uncertain output settlement belongs to a stale task."));
    state->producer_busy.store(false);
    retainStageIfPresent(state);
    QVariantMap result = failure(message);
    result.insert(QStringLiteral("resourceId"), state->resource_id);
    result.insert(QStringLiteral("taskStepId"), state->step_id);
    result.insert(QStringLiteral("taskId"), state->task_id);
    if (!state->recovery_path.isEmpty()) result.insert(QStringLiteral("recoveryPath"), state->recovery_path);

    const auto actual = state->file->snapshot();
    if (actual.isEmpty() || actual != state->baseline) {
      result.insert(QStringLiteral("recoveryRequired"), true);
      result.insert(QStringLiteral("pending"), true);
      result.insert(QStringLiteral("journalPending"), tasks_.journal()->pending(state->resource_id));
      result.insert(QStringLiteral("unresolved"), true);
      result.insert(QStringLiteral("destinationConflict"), true);
      result.insert(QStringLiteral("actualSnapshotHash"), snapshotHash(actual));
      last_result_ = result;
      if (finished_handler_) finished_handler_(result);
      return result;
    }

    QVariantMap settled;
    if (tasks_.journal()->pending(state->resource_id)) {
      settled = tasks_.journal()->settle(state->resource_id, actual, false);
    } else {
      settled = tasks_.journal()->complete(state->step_id, actual,
          {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message},
           {QStringLiteral("acceptanceUncertain"), true}});
    }
    if (!settled.value(QStringLiteral("ok")).toBool()) {
      result.insert(QStringLiteral("recoveryRequired"), true);
      result.insert(QStringLiteral("pending"), tasks_.journal()->pending(state->resource_id));
      result.insert(QStringLiteral("journalPending"), tasks_.journal()->pending(state->resource_id));
      result.insert(QStringLiteral("unresolved"), true);
      result.insert(QStringLiteral("journalError"), settled.value(QStringLiteral("error")));
      last_result_ = result;
      if (finished_handler_) finished_handler_(result);
      return result;
    }
    result.insert(QStringLiteral("pending"), false);
    result.insert(QStringLiteral("taskState"), settled);
    last_result_ = result;
    active_.reset();
    if (finished_handler_) finished_handler_(result);
    return result;
  }

  static QVariantMap successResult(const std::shared_ptr<ActiveOutput>& state,
      const AgentPhotoOutputPublishResult& publish) {
    return {{QStringLiteral("ok"), true}, {QStringLiteral("taskId"), state->task_id},
        {QStringLiteral("assetId"), state->asset_id}, {QStringLiteral("outputPath"), state->output_path},
        {QStringLiteral("format"), state->format}, {QStringLiteral("sha256"), publish.sha256},
        {QStringLiteral("byteCount"), publish.byte_count}, {QStringLiteral("resourceId"), state->resource_id},
        {QStringLiteral("taskStepId"), state->step_id}};
  }

  void finalizeWithoutSettlement(const std::shared_ptr<ActiveOutput>& state, QVariantMap result) {
    if (active_ != state) return;
    result.insert(QStringLiteral("pending"), false);
    last_result_ = result;
    active_.reset();
    if (finished_handler_) finished_handler_(result);
  }

  static void requestStop(const std::shared_ptr<ActiveOutput>& state) {
    if (!state || !state->producer_busy.load()) return;
    if (state->publication &&
        state->publication->phase.load() == PublicationControl::Phase::Publishing) {
      // The final no-overwrite link has started and cannot be recalled. Keep
      // the task pending until its terminal receipt so Revert cannot finish
      // while the target might still appear.
      state->cancel_requested.store(true);
      return;
    }
    state->cancel_requested.store(true);
    if (state->publication) static_cast<void>(state->publication->cancel());
    if (!state->native_terminal_received && state->cancel_native)
      state->native_cancel_accepted = state->cancel_native();
  }

  void retainStageIfPresent(const std::shared_ptr<ActiveOutput>& state) {
    if (!state || !QFileInfo(state->staging_path).isFile()) return;
    if (state->staging_directory) state->staging_directory->setAutoRemove(false);
    state->recovery_path = state->staging_path;
    preserved_stage_path_ = state->staging_path;
  }

  static QVariantMap failure(const QString& error) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
  }

  static QString snapshotHash(const QByteArray& snapshot) {
    if (snapshot.isEmpty()) return {};
    return QString::fromLatin1(QCryptographicHash::hash(snapshot, QCryptographicHash::Sha256).toHex());
  }

  static QString resourceIdFor(const QString& task_id, const QString& output_path) {
    return QStringLiteral("photo-output-%1").arg(
        QString::fromLatin1(QCryptographicHash::hash(
            (task_id + QLatin1Char('\n') + output_path).toUtf8(),
            QCryptographicHash::Sha256).toHex()));
  }

  static std::optional<std::filesystem::path> nativeFilesystemPath(const QString& path) {
#ifdef Q_OS_WIN
    const auto native = path.toStdWString();
    if (native.empty()) return std::nullopt;
    return std::filesystem::path(native);
#else
    const auto encoded = path.toUtf8();
    if (encoded.isEmpty()) return std::nullopt;
    return std::filesystem::path(encoded.constData());
#endif
  }

  AgentTaskCoordinator& tasks_;
  AdmissionHandler admit_;
  StartHandler start_native_;
  CancelHandler cancel_native_;
  BusyHandler native_busy_;
  CompletionSnapshotHandler completion_snapshot_;
  FinishedHandler finished_handler_;
  QFutureWatcher<AgentPhotoOutputPublishResult> publisher_watcher_;
  QFutureWatcher<AgentPhotoOutputPublishResult> receipt_watcher_;
  QFutureWatcher<SettlementAuditResult> settlement_watcher_;
  std::shared_ptr<ActiveOutput> active_;
  QString last_resource_id_;
  QString last_staging_path_;
  QString preserved_stage_path_;
  QVariantMap last_result_;
};

}  // namespace photo_editor::desktop

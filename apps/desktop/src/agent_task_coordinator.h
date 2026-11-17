#pragma once

#include "agent_task_journal.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSaveFile>
#include <QUuid>
#include <functional>
#include <memory>

namespace photo_editor::desktop {

// One durable journal per task, with a separate native adapter for each owned
// resource. Enroll lazily so an untouched open document does not block reversal.
class AgentTaskCoordinator final {
 public:
  using PreimageInvocation = std::function<QVariantMap(const QByteArray&)>;

  struct OwnedFileResource final {
    QString resource_id;
    QString canonical_path;
    QByteArray absent_state;
    AgentTaskJournal::ResourceAdapter adapter;
  };

  AgentTaskCoordinator() : journal_(std::make_shared<AgentTaskJournal>()) {}
  std::shared_ptr<AgentTaskJournal> journal() const { return journal_; }
  void setBeforeRevertHandler(std::function<QVariantMap(bool)> handler) {
    before_revert_ = std::move(handler);
  }
  QVariantMap begin(const QString& root, const QString& task_id) {
    const auto result = journal_->begin(root, task_id);
    if (result.value("ok").toBool()) adapters_.clear();
    return result;
  }
  QVariantMap recover(const QString& root, const QString& task_id) {
    const auto result = journal_->recover(root, task_id);
    if (result.value("ok").toBool()) adapters_.clear();
    return result;
  }
  QVariantMap enroll(const QString& resource_id, AgentTaskJournal::ResourceAdapter adapter) {
    if (resource_id.isEmpty() || !adapter.read || !adapter.restore)
      return failure("A native resource adapter is required.");
    if (journal_->checkpointHash(resource_id).isEmpty()) {
      if (journal_->report().value("status") != "running")
        return failure("New resource ownership requires a running task.");
      const auto captured = journal_->capture(resource_id, adapter.read());
      if (!captured.value("ok").toBool()) return captured;
    } else if (!journal_->hasChanges()) {
      return failure("The task no longer has a document to restore.");
    }
    adapters_.insert(resource_id, std::move(adapter));
    return journal_->report();
  }
  [[nodiscard]] static OwnedFileResource taskOwnedFileResource(const QString& path) {
    const QFileInfo input(path);
    const auto canonical_parent = input.dir().canonicalPath();
    if (path.trimmed().isEmpty() || !input.isAbsolute() || canonical_parent.isEmpty() ||
        input.fileName().isEmpty()) return {};
    const auto canonical_path = QDir(canonical_parent).filePath(input.fileName());
    if (input.exists() && input.canonicalFilePath() != canonical_path) return {};
    const auto resource_id = QStringLiteral("task-output:%1").arg(
        QString::fromLatin1(QCryptographicHash::hash(canonical_path.toUtf8(), QCryptographicHash::Sha256).toHex()));
    static const QByteArray absent_state = QByteArrayLiteral("agent-task-owned-file-v1:absent");
    static const QByteArray present_prefix = QByteArrayLiteral("agent-task-owned-file-v1:present\n");
    const auto current_state = [canonical_path, canonical_parent]() {
      const QFileInfo info(canonical_path);
      if (!info.exists()) {
        const auto actual_parent = QDir(info.dir().absolutePath()).canonicalPath();
        return actual_parent == canonical_parent ? absent_state : QByteArray{};
      }
      if (!info.isFile() || info.canonicalFilePath() != canonical_path ||
          QDir(info.dir().absolutePath()).canonicalPath() != canonical_parent) return QByteArray{};
      QFile file(canonical_path);
      if (!file.open(QIODevice::ReadOnly)) return QByteArray{};
      return present_prefix + file.readAll();
    };
    const auto restore_state = [canonical_path, canonical_parent](const QByteArray& state) {
      const QFileInfo info(canonical_path);
      if (QDir(info.dir().absolutePath()).canonicalPath() != canonical_parent) return false;
      if (state == absent_state) {
        if (!info.exists()) return true;
        if (info.canonicalFilePath() != canonical_path || !info.isFile()) return false;
        return QFile::remove(canonical_path);
      }
      if (!state.startsWith(present_prefix)) return false;
      if (info.exists() && (!info.isFile() || info.canonicalFilePath() != canonical_path)) return false;
      QSaveFile file(canonical_path);
      const auto payload = state.mid(present_prefix.size());
      return file.open(QIODevice::WriteOnly) && file.write(payload) == payload.size() && file.commit();
    };
    return {resource_id, canonical_path, absent_state,
            {current_state, restore_state, {}}};
  }
  // Compound renders can be hundreds of megabytes or larger. Their baseline
  // is always required to be absent, so hash present files as a compact
  // descriptor and delete them only after the revert preflight observed the
  // same exact contents. This deliberately cannot restore a pre-existing
  // file; callers must reject any existing/symlink target before enrollment.
  [[nodiscard]] static OwnedFileResource taskOwnedAbsentFileResource(
      const QString& path) {
    const QFileInfo input(path);
    const auto canonical_parent = input.dir().canonicalPath();
    if (path.trimmed().isEmpty() || !input.isAbsolute() ||
        input.fileName().isEmpty() || canonical_parent.isEmpty() ||
        input.isSymLink()) {
      return {};
    }
    const auto canonical_path = QDir(canonical_parent).filePath(input.fileName());
    if (input.exists() && input.canonicalFilePath() != canonical_path) return {};
    const auto resource_id = QStringLiteral("task-output:%1").arg(
        QString::fromLatin1(QCryptographicHash::hash(canonical_path.toUtf8(), QCryptographicHash::Sha256).toHex()));
    static const QByteArray absent_state = QByteArrayLiteral("agent-task-owned-file-v1:absent");
    static const QByteArray present_prefix = QByteArrayLiteral("agent-task-owned-file-sha256-v1\n");
    const auto fingerprint = [canonical_path, canonical_parent]() {
      const QFileInfo info(canonical_path);
      if (!info.exists()) {
        if (info.isSymLink() ||
            QDir(info.dir().absolutePath()).canonicalPath() != canonical_parent) {
          return QByteArray{};
        }
        return absent_state;
      }
      if (!info.isFile() || info.isSymLink() ||
          info.canonicalFilePath() != canonical_path ||
          QDir(info.dir().absolutePath()).canonicalPath() != canonical_parent) {
        return QByteArray{};
      }
      QFile file(canonical_path);
      if (!file.open(QIODevice::ReadOnly)) return QByteArray{};
      QCryptographicHash hash(QCryptographicHash::Sha256);
      qint64 size = 0;
      while (!file.atEnd()) {
        const auto block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) return QByteArray{};
        if (block.isEmpty()) break;
        size += block.size();
        hash.addData(block);
      }
      if (file.error() != QFileDevice::NoError ||
          QFileInfo(canonical_path).size() != size) {
        return QByteArray{};
      }
      return present_prefix + QByteArray::number(size) + '\n' + hash.result().toHex();
    };
    const auto last_observed = std::make_shared<QByteArray>();
    const auto read_state = [fingerprint, last_observed]() {
      const auto state = fingerprint();
      *last_observed = state;
      return state;
    };
    const auto restore_state = [canonical_path, canonical_parent, fingerprint,
                                last_observed](const QByteArray& state) {
      if (state != absent_state) return false;
      const auto expected = *last_observed;
      const auto current = fingerprint();
      if (current == absent_state) return true;
      if (expected.isEmpty() || current != expected) return false;
      const QFileInfo info(canonical_path);
      if (!info.isFile() || info.isSymLink() ||
          info.canonicalFilePath() != canonical_path ||
          QDir(info.dir().absolutePath()).canonicalPath() != canonical_parent) {
        return false;
      }
      return QFile::remove(canonical_path);
    };
    return {resource_id, canonical_path, absent_state,
            {read_state, restore_state, {}}};
  }
  [[nodiscard]] static OwnedFileResource taskOwnedTimelineOutputFileResource(
      const QString& path) {
    const auto name = QFileInfo(path).fileName();
    if (!name.startsWith(QStringLiteral("compound-")))
      return taskOwnedFileResource(path);
    const auto tail = name.mid(QStringLiteral("compound-").size());
    QString child_id;
    const QStringList suffixes{QStringLiteral(".partial.mov"),
                               QStringLiteral(".producer.json"),
                               QStringLiteral(".ffgraph"),
                               QStringLiteral(".json"),
                               QStringLiteral(".mov")};
    for (const auto& suffix : suffixes) {
      if (!tail.endsWith(suffix)) continue;
      child_id = tail.left(tail.size() - suffix.size());
      break;
    }
    const QUuid parsed_child(child_id);
    if (parsed_child.isNull() ||
        parsed_child.toString(QUuid::WithoutBraces).toLower() != child_id) {
      // Never fall back to the legacy readAll-backed adapter for a path that
      // claims to be a compound output. Malformed compound names are rejected.
      if (name.startsWith(QStringLiteral("compound-"))) return {};
      return taskOwnedFileResource(path);
    }
    return taskOwnedAbsentFileResource(path);
  }
  [[nodiscard]] QVariantMap checkpointOwnedResource(const QString& resource_id) {
    const auto adapter = adapters_.value(resource_id);
    if (!adapter.read) return failure("The owned task resource is unavailable.");
    return journal_->recordOwnedState(resource_id, adapter.read());
  }
  QVariantMap invokeIdempotentBatch(const QString& resource_id, const QString& command_id,
      const QString& batch_id, const QString& request_sha256, const QVariantMap& arguments,
      const std::function<QVariantMap()>& invoke_native) {
    if (!invoke_native) return failure("A stable batch ID, request checksum and native action are required.");
    return invokeIdempotentBatchWithPreimage(resource_id, command_id, batch_id,
        request_sha256, arguments,
        [&invoke_native](const QByteArray&) { return invoke_native(); });
  }
  QVariantMap invokeIdempotentBatchWithPreimage(const QString& resource_id,
      const QString& command_id, const QString& batch_id, const QString& request_sha256,
      const QVariantMap& arguments, const PreimageInvocation& invoke_native) {
    const auto digest_bytes = QByteArray::fromHex(request_sha256.toLatin1());
    if (batch_id.isEmpty() || digest_bytes.size() != 32 ||
        digest_bytes.toHex() != request_sha256.toLatin1() || !invoke_native)
      return failure("A stable batch ID, request checksum and native action are required.");
    const auto lookup = journal_->lookupIdempotentBatchStep(batch_id);
    if (!lookup.value(QStringLiteral("ok")).toBool()) return lookup;
    const auto task_is_running =
        lookup.value(QStringLiteral("taskStatus")).toString() == QStringLiteral("running");
    if (lookup.value(QStringLiteral("found")).toBool()) {
      if (lookup.value(QStringLiteral("command")).toString() != command_id ||
          lookup.value(QStringLiteral("resourceId")).toString() != resource_id)
        return failure("This batch ID already belongs to a different task command or resource.");
      if (lookup.value(QStringLiteral("requestSha256")).toString() != request_sha256)
        return failure("This batch ID was already used with different operations.");
      const auto status = lookup.value(QStringLiteral("stepStatus")).toString();
      if (status == QStringLiteral("prepared") || status == QStringLiteral("accepted"))
        return failure("This batch has an unresolved native outcome. Inspect or recover the task; it will not be replayed.");
      if (!task_is_running)
        return failure("An idempotent batch retry requires the same running owned task.");
      auto prior = lookup.value(QStringLiteral("outcome")).toMap();
      if (status != QStringLiteral("completed") && status != QStringLiteral("failed"))
        return failure("This batch has no reusable completed outcome.");
      const auto adapter = adapters_.value(resource_id);
      if (!adapter.read || journal_->pending(resource_id) ||
          !journal_->matchesCheckpoint(resource_id, adapter.read()))
        return failure("The Timeline changed after its last task checkpoint; the stored batch result was preserved.");
      prior.insert(QStringLiteral("idempotentReplay"), true);
      prior.insert(QStringLiteral("resourceId"), resource_id);
      prior.insert(QStringLiteral("taskStepId"), lookup.value(QStringLiteral("stepId")));
      prior.insert(QStringLiteral("taskStateHash"), journal_->checkpointHash(resource_id));
      return prior;
    }
    if (!task_is_running)
      return failure("An idempotent batch retry requires the same running owned task.");
    auto journal_arguments = arguments;
    journal_arguments.insert(QStringLiteral("_batchId"), batch_id);
    journal_arguments.insert(QStringLiteral("_batchRequestSha256"), request_sha256);
    return invokeWithPreimage(resource_id, command_id, journal_arguments, invoke_native);
  }
  QVariantMap reconcilePreparedUnchangedTimelineDraftBatch(
      const QString& timeline_resource_id) {
    const auto task_report = journal_->report();
    const auto steps = task_report.value(QStringLiteral("steps")).toList();
    auto prepared_count = 0;
    for (const auto& value : steps) {
      if (value.toMap().value(QStringLiteral("status")).toString() ==
          QStringLiteral("prepared")) {
        ++prepared_count;
      }
    }
    if (prepared_count == 0 &&
        !task_report.value(QStringLiteral("uncertainOperation")).toBool()) {
      return {{QStringLiteral("ok"), true},
              {QStringLiteral("recoveredUnchanged"), false},
              {QStringLiteral("reconciled"), false}};
    }
    if (prepared_count != 1 || timeline_resource_id.trimmed().isEmpty()) {
      return failure("Recovery requires exactly one prepared synchronous Timeline batch and a stable Timeline resource ID.");
    }

    const auto scope = journal_->nativeTimelineScope();
    const auto target_ids = scope.value(QStringLiteral("targetIds")).toStringList();
    const auto output_directory = scope.value(QStringLiteral("outputDirectory")).toString();
    const QFileInfo output_directory_info(output_directory);
    if (target_ids != QStringList{timeline_resource_id} ||
        output_directory.isEmpty() ||
        output_directory_info.canonicalFilePath() != output_directory ||
        !output_directory_info.isDir()) {
      return failure("The recovered task has no exact Timeline and canonical output-directory scope.");
    }

    QSet<QString> expected_resource_ids{timeline_resource_id};
    for (const auto& path : journal_->nativeTimelineOutputPaths()) {
      const QFileInfo output(path);
      const auto output_parent = output.dir().canonicalPath();
      const auto canonical_path = output_parent.isEmpty()
                                      ? QString{}
                                      : QDir(output_parent).filePath(output.fileName());
      const auto relative_path = QDir(output_directory).relativeFilePath(canonical_path);
      if (!output.isAbsolute() || output.fileName().isEmpty() || path != canonical_path ||
          output_parent.isEmpty() || relative_path.isEmpty() ||
          relative_path == QStringLiteral(".") || relative_path == QStringLiteral("..") ||
          relative_path.startsWith(QStringLiteral("../")) || QFileInfo(relative_path).isAbsolute()) {
        return failure("A recovered output path is not a canonical file inside its admitted task directory.");
      }
      const auto output_resource = taskOwnedTimelineOutputFileResource(path);
      if (output_resource.resource_id.isEmpty() ||
          expected_resource_ids.contains(output_resource.resource_id)) {
        return failure("A recovered output path has no unique task-owned file adapter.");
      }
      expected_resource_ids.insert(output_resource.resource_id);
    }

    const auto resources = task_report.value(QStringLiteral("resources")).toList();
    if (resources.size() != expected_resource_ids.size()) {
      return failure("The recovered task contains resources outside its Timeline and admitted output-file scope.");
    }
    QHash<QString, QByteArray> observed_states;
    for (const auto& value : resources) {
      const auto resource = value.toMap();
      const auto id = resource.value(QStringLiteral("id")).toString();
      if (!expected_resource_ids.contains(id) || observed_states.contains(id) ||
          resource.value(QStringLiteral("pending")).toBool()) {
        return failure("A recovered resource is outside the admitted scope, duplicated, or has another pending operation.");
      }
      const auto adapter = adapters_.value(id);
      if (!adapter.read) {
        return failure("Every Timeline and admitted output resource must be enrolled before batch reconciliation.");
      }
      const auto current = adapter.read();
      if (current.isEmpty() || !journal_->matchesCheckpoint(id, current)) {
        return failure("A recovered Timeline or output file no longer matches its exact task checkpoint; the uncertain batch remains unresolved.");
      }
      observed_states.insert(id, current);
    }

    if (observed_states.size() != expected_resource_ids.size()) {
      return failure("An admitted output file is missing its exact recovery checkpoint.");
    }
    return journal_->reconcilePreparedUnchangedTimelineDraftBatch(
        timeline_resource_id, observed_states);
  }
  QVariantMap invoke(const QString& resource_id, const QString& command_id, const QVariantMap& values,
      const std::function<QVariantMap()>& invoke_native) {
    if (!invoke_native) return failure("The command's native resource is unavailable.");
    return invokeWithPreimage(resource_id, command_id, values,
        [&invoke_native](const QByteArray&) { return invoke_native(); });
  }
  // Passes the exact bytes already prepared in the task journal to a trusted,
  // synchronous adapter. The callback must not retain or modify the preimage.
  QVariantMap invokeWithPreimage(const QString& resource_id, const QString& command_id,
      const QVariantMap& values, const PreimageInvocation& invoke_native) {
    const auto adapter = adapters_.value(resource_id);
    if (!adapter.read || !invoke_native) return failure("The command's native resource is unavailable.");
    const auto before = adapter.read();
    const auto prepared = journal_->prepare(resource_id, command_id, values, before);
    if (!prepared.value("ok").toBool()) return prepared;
    auto result = invoke_native(before);
    const auto after = adapter.read();
    result.insert("changed", before != after);
    // Only synchronous document commands are admitted through this adapter.
    // Never use unchanged document bytes to certify external file/job effects.
    if (!result.value("ok").toBool() && before == after && !after.isEmpty() &&
        !result.value("recoveryRequired").toBool()) {
      result.insert("recoverableError", true);
      result.insert("mutationNotApplied", true);
    }
    QVariantMap receipt;
    for (const auto& key : {QStringLiteral("ok"), QStringLiteral("error"), QStringLiteral("changed"),
        QStringLiteral("alreadySatisfied"), QStringLiteral("recoverableError"), QStringLiteral("mutationNotApplied"),
        QStringLiteral("createdClipId"), QStringLiteral("createdClipIds"), QStringLiteral("result"),
        QStringLiteral("batchId"), QStringLiteral("batchRequestSha256"), QStringLiteral("batchResults"),
        QStringLiteral("operationCount"), QStringLiteral("rolledBack")})
      if (result.contains(key)) receipt.insert(key, result.value(key));
    const auto completed = journal_->complete(prepared.value("stepId").toLongLong(), after, receipt);
    if (!completed.value("ok").toBool()) {
      result.insert("ok", false);
      result.insert("error", completed.value("error"));
      result.insert("recoveryRequired", true);
      result.remove("recoverableError");
      result.remove("mutationNotApplied");
    }
    result.insert("resourceId", resource_id);
    result.insert("taskStepId", prepared.value("stepId"));
    result.insert("taskStateHash", journal_->checkpointHash(resource_id));
    return result;
  }
  QVariantMap finish(const QString& status) {
    if (status == "review" || status == "committed") {
      for (const auto& value : journal_->report().value("resources").toList()) {
        const auto id = value.toMap().value("id").toString();
        const auto adapter = adapters_.value(id);
        if (!adapter.read || !journal_->matchesCheckpoint(id, adapter.read()))
          return failure("An owned document is unavailable or changed after its last task checkpoint. Its current edits were preserved.");
      }
    }
    return journal_->finish(status);
  }
  QVariantMap releaseReviewedDraft() {
    if (journal_->report().value("status") != "review")
      return failure("Only a reviewed native task can release Timeline ownership.");
    QHash<QString, QByteArray> snapshots;
    for (const auto& value : journal_->report().value("resources").toList()) {
      const auto id = value.toMap().value("id").toString();
      const auto adapter = adapters_.value(id);
      if (!adapter.read || journal_->pending(id)) {
        return failure("An owned resource is unavailable, pending, or changed after review. Its recovery baseline was preserved.");
      }
      const auto current = adapter.read();
      if (!journal_->matchesCheckpoint(id, current))
        return failure("An owned resource is unavailable, pending, or changed after review. Its recovery baseline was preserved.");
      snapshots.insert(id, current);
    }
    return journal_->releaseReviewedDraft(snapshots);
  }
  QVariantMap continueTask() {
    if (journal_->report().value(QStringLiteral("status")).toString() == QStringLiteral("released")) {
      const auto targets = journal_->nativeTimelineScope().value(QStringLiteral("targetIds")).toStringList();
      if (targets.size() != 1) return failure("The released task has no exact Timeline source-readiness scope.");
      const auto adapter = adapters_.value(targets.front());
      if (!adapter.read) return failure("The released Timeline owner is unavailable for source-readiness checks.");
      const auto readiness = journal_->validateReleasedTimelineSourceIntegrityReady(targets.front(), adapter.read());
      if (!readiness.value(QStringLiteral("ok")).toBool()) return readiness;
    }
    const auto derived_reconciliation = reconcileReleasedTimelineSourceIntegrity();
    if (!derived_reconciliation.value(QStringLiteral("ok")).toBool()) return derived_reconciliation;
    for (const auto& value : journal_->report().value("resources").toList()) {
      const auto id = value.toMap().value("id").toString();
      const auto adapter = adapters_.value(id);
      if (!adapter.read || journal_->pending(id) || !journal_->matchesCheckpoint(id, adapter.read()))
        return failure("An owned document is unavailable, processing, or changed after its last task checkpoint. Its current edits were preserved.");
    }
    return journal_->continueTask();
  }
  // Creation/import has already changed owned resources under this journal.
  // Hand execution the same task only after every operation has a known result.
  QVariantMap adoptPreparedTask(const QString& task_id, const QStringList& target_ids) const {
    const auto report = journal_->report();
    if (task_id.isEmpty() || task_id != journal_->taskId() ||
        report.value("status") != "running" || !journal_->hasChanges() || target_ids.isEmpty())
      return failure("The prepared creation task is not active and reversible.");
    bool creation_changed = false;
    for (const auto& value : report.value("resources").toList()) {
      const auto resource = value.toMap();
      if (resource.value("beforeHash") != resource.value("currentHash")) creation_changed = true;
    }
    if (!creation_changed || report.value("steps").toList().isEmpty())
      return failure("A prepared document requires a completed creation receipt and changed owned resource.");
    for (const auto& id : target_ids)
      if (id.isEmpty() || journal_->checkpointHash(id).isEmpty())
        return failure("A prepared target is missing its native resource checkpoint.");
    for (const auto& value : report.value("steps").toList()) {
      const auto status = value.toMap().value("status").toString();
      if (status == "prepared" || status == "accepted")
        return failure("Reconcile the unfinished creation operation before editing its document.");
    }
    for (const auto& value : report.value("resources").toList()) {
      const auto id = value.toMap().value("id").toString();
      const auto adapter = adapters_.value(id);
      if (!adapter.read || journal_->pending(id) || !journal_->matchesCheckpoint(id, adapter.read()))
        return failure("A prepared resource is missing, processing or changed. Its current data was preserved.");
    }
    auto result = report;
    result.insert("adopted", true);
    return result;
  }
  QVariantMap abandonAdmission() { return journal_->abandonAdmission(); }
  QVariantMap state() const { return journal_->report(); }
  void cancel() {
    for (auto it = adapters_.cbegin(); it != adapters_.cend(); ++it)
      if (it.value().cancel) it.value().cancel();
  }
  QVariantMap revert(bool check_only = false) {
    if (before_revert_) {
      const auto guard = before_revert_(check_only);
      if (!guard.value(QStringLiteral("ok")).toBool()) return guard;
    }
    if (check_only) {
      QHash<QString, QString> verified_derived_hashes;
      QVariantList derived_proposals;
      if (journal_->report().value(QStringLiteral("status")).toString() == QStringLiteral("released")) {
        const auto targets = journal_->nativeTimelineScope().value(QStringLiteral("targetIds")).toStringList();
        for (const auto& id : targets) {
          const auto adapter = adapters_.value(id);
          if (!adapter.read) return failure("The released Timeline owner is unavailable for source-integrity preflight.");
          const auto proposal = journal_->reconcileReleasedTimelineSourceIntegrity(id, adapter.read(), false);
          if (!proposal.value(QStringLiteral("ok")).toBool()) return proposal;
          if (proposal.value(QStringLiteral("changed")).toBool()) {
            verified_derived_hashes.insert(id, proposal.value(QStringLiteral("proposedCurrentHash")).toString());
            derived_proposals.append(proposal);
          }
        }
      }
      auto preflight = journal_->checkRevert(adapters_, verified_derived_hashes);
      if (preflight.value(QStringLiteral("ok")).toBool() && !derived_proposals.isEmpty())
        preflight.insert(QStringLiteral("derivedIntegrityProposals"), derived_proposals);
      return preflight;
    }
    const auto derived_reconciliation = reconcileReleasedTimelineSourceIntegrity();
    if (!derived_reconciliation.value(QStringLiteral("ok")).toBool()) return derived_reconciliation;
    return journal_->revert(adapters_);
  }
  QVariantMap reconcileReleasedTimelineSourceIntegrity() {
    if (journal_->report().value(QStringLiteral("status")).toString() != QStringLiteral("released"))
      return {{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false}};
    const auto targets = journal_->nativeTimelineScope().value(QStringLiteral("targetIds")).toStringList();
    if (targets.size() != 1)
      return failure("The released Timeline task has no exact source-integrity target scope.");
    auto changed = false;
    for (const auto& id : targets) {
      const auto adapter = adapters_.value(id);
      if (!adapter.read || !adapter.restore)
        return failure("The released Timeline owner is unavailable for source-integrity reconciliation.");
      const auto result = journal_->reconcileReleasedTimelineSourceIntegrity(id, adapter.read());
      if (!result.value(QStringLiteral("ok")).toBool()) return result;
      changed = changed || result.value(QStringLiteral("changed")).toBool();
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("changed"), changed}};
  }

 private:
  static QVariantMap failure(const QString& message) { return {{"ok", false}, {"error", message}}; }
  std::shared_ptr<AgentTaskJournal> journal_;
  QHash<QString, AgentTaskJournal::ResourceAdapter> adapters_;
  std::function<QVariantMap(bool)> before_revert_;
};

}  // namespace photo_editor::desktop

#pragma once

#include "agent_task_journal.h"
#include <memory>

namespace photo_editor::desktop {

// One durable journal per task, with a separate native adapter for each owned
// resource. Enroll lazily so an untouched open document does not block reversal.
class AgentTaskCoordinator final {
 public:
  AgentTaskCoordinator() : journal_(std::make_shared<AgentTaskJournal>()) {}
  std::shared_ptr<AgentTaskJournal> journal() const { return journal_; }
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
  QVariantMap invoke(const QString& resource_id, const QString& command_id, const QVariantMap& values,
      const std::function<QVariantMap()>& invoke_native) {
    const auto adapter = adapters_.value(resource_id);
    if (!adapter.read || !invoke_native) return failure("The command's native resource is unavailable.");
    const auto before = adapter.read();
    const auto prepared = journal_->prepare(resource_id, command_id, values, before);
    if (!prepared.value("ok").toBool()) return prepared;
    auto result = invoke_native();
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
        QStringLiteral("createdClipId"), QStringLiteral("createdClipIds"), QStringLiteral("result")})
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
  QVariantMap continueTask() {
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
    return check_only ? journal_->checkRevert(adapters_) : journal_->revert(adapters_);
  }

 private:
  static QVariantMap failure(const QString& message) { return {{"ok", false}, {"error", message}}; }
  std::shared_ptr<AgentTaskJournal> journal_;
  QHash<QString, AgentTaskJournal::ResourceAdapter> adapters_;
};

}  // namespace photo_editor::desktop

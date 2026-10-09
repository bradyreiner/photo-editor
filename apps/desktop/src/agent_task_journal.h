#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace photo_editor::desktop {

// A task owns explicit document resources, never the entire workspace. Baselines
// and accepted operations survive a process exit. Native adapters retain control
// of cancellation and restoration; this journal never replays an uncertain call.
class AgentTaskJournal final {
 public:
  struct ResourceAdapter {
    std::function<QByteArray()> read;
    std::function<bool(const QByteArray&)> restore;
    std::function<void()> cancel;
  };

  [[nodiscard]] QVariantMap begin(const QString& root, const QString& task_id) {
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,128}$")).match(task_id).hasMatch())
      return failure(QStringLiteral("Invalid agent task ID."));
    if (active()) return failure(QStringLiteral("Finish or revert the previous agent task first."));
    directory_ = QDir(root).filePath(QStringLiteral("agent-tasks/%1").arg(task_id));
    if (!QDir().mkpath(directory_)) return failure(QStringLiteral("Could not create the agent task journal."));
    if (QFile::exists(journalPath())) return failure(QStringLiteral("The agent task already exists; recover it instead of replaying it."));
    document_ = {{QStringLiteral("contract"), QStringLiteral("agent-task-journal-v1")},
        {QStringLiteral("taskId"), task_id}, {QStringLiteral("status"), QStringLiteral("running")},
        {QStringLiteral("resources"), QJsonArray{}}, {QStringLiteral("steps"), QJsonArray{}}};
    return save() ? report() : failure(error_);
  }

  [[nodiscard]] QVariantMap recover(const QString& root, const QString& task_id) {
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,128}$")).match(task_id).hasMatch())
      return failure(QStringLiteral("Invalid agent task ID."));
    if (active()) return failure(QStringLiteral("Finish or revert the previous agent task first."));
    directory_ = QDir(root).filePath(QStringLiteral("agent-tasks/%1").arg(task_id));
    QFile file(journalPath());
    if (!file.open(QIODevice::ReadOnly)) return failure(QStringLiteral("Could not read the agent task journal."));
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value(QStringLiteral("contract")) != QStringLiteral("agent-task-journal-v1") ||
        document.object().value(QStringLiteral("taskId")) != task_id)
      return failure(QStringLiteral("Invalid agent task journal; recovery bytes were preserved."));
    document_ = document.object();
    if (document_.contains(QStringLiteral("approvedSnapshot"))) {
      const auto approved = document_.value(QStringLiteral("approvedSnapshot"));
      if (!approved.isObject() || hash(QJsonDocument(approved.toObject()).toJson(QJsonDocument::Compact)) !=
          document_.value(QStringLiteral("approvedSnapshotHash")).toString()) {
        document_ = {};
        return failure(QStringLiteral("Approved conversation checksum mismatch; recovery bytes were preserved."));
      }
    }
    for (const auto& value : resources()) {
      const auto resource = value.toObject();
      const auto before = readBaseline(resource.value(QStringLiteral("id")).toString());
      if (before.isEmpty() || hash(before) != resource.value(QStringLiteral("beforeHash")).toString()) {
        document_ = {};
        return failure(QStringLiteral("Agent baseline checksum mismatch; recovery bytes were preserved."));
      }
    }
    // An admitted operation may have run before its outcome was persisted.
    // Recovery must surface that uncertainty, rather than duplicate a mutation.
    for (const auto& value : steps()) {
      if (value.toObject().value(QStringLiteral("status")) == QStringLiteral("prepared")) {
        document_.insert(QStringLiteral("status"), QStringLiteral("interrupted"));
        document_.insert(QStringLiteral("uncertainOperation"), true);
      }
    }
    return report();
  }

  [[nodiscard]] bool active() const {
    const auto status = document_.value(QStringLiteral("status")).toString();
    return !status.isEmpty() && status != QStringLiteral("reverted") && status != QStringLiteral("committed") &&
        status != QStringLiteral("abandoned");
  }
  [[nodiscard]] bool hasChanges() const {
    return !resources().isEmpty() && document_.value(QStringLiteral("status")) != QStringLiteral("reverted") &&
        document_.value(QStringLiteral("status")) != QStringLiteral("abandoned");
  }
  [[nodiscard]] QString taskId() const { return document_.value(QStringLiteral("taskId")).toString(); }
  [[nodiscard]] QString journalPath() const { return QDir(directory_).filePath(QStringLiteral("journal.json")); }

  [[nodiscard]] QVariantMap recordApprovedSnapshot(const QVariantMap& snapshot) {
    if (!active() || document_.value(QStringLiteral("status")) != QStringLiteral("running") ||
        snapshot.value(QStringLiteral("contract")) != QStringLiteral("agent-approved-session-v1") ||
        snapshot.value(QStringLiteral("prompt")).toString().trimmed().isEmpty())
      return failure(QStringLiteral("A running task and approved conversation are required."));
    const auto previous = document_;
    document_.insert(QStringLiteral("approvedSnapshot"), QJsonObject::fromVariantMap(snapshot));
    document_.insert(QStringLiteral("approvedSnapshotHash"), hash(QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact)));
    if (!save()) {
      document_ = previous;
      return failure(error_);
    }
    return report();
  }

  [[nodiscard]] QVariantMap capture(const QString& resource_id, const QByteArray& current) {
    if (!active() || resource_id.isEmpty() || current.isEmpty())
      return failure(QStringLiteral("A running task and readable document baseline are required."));
    if (resourceIndex(resource_id) >= 0) return failure(QStringLiteral("The resource already has a baseline."));
    if (!writeBytes(baselinePath(resource_id), current)) return failure(error_);
    auto values = resources();
    values.append(QJsonObject{{QStringLiteral("id"), resource_id},
        {QStringLiteral("beforeHash"), hash(current)}, {QStringLiteral("currentHash"), hash(current)},
        {QStringLiteral("pending"), false}, {QStringLiteral("reverted"), false}});
    document_.insert(QStringLiteral("resources"), values);
    return save() ? report() : failure(error_);
  }

  [[nodiscard]] QByteArray baseline(const QString& resource_id) const { return readBaseline(resource_id); }
  [[nodiscard]] bool pending(const QString& resource_id) const {
    const auto index = resourceIndex(resource_id);
    return index >= 0 && resources().at(index).toObject().value(QStringLiteral("pending")).toBool();
  }
  [[nodiscard]] bool matchesCheckpoint(const QString& resource_id, const QByteArray& current) const {
    const auto index = resourceIndex(resource_id);
    return index >= 0 && resources().at(index).toObject().value(QStringLiteral("currentHash")) == hash(current);
  }
  [[nodiscard]] QString checkpointHash(const QString& resource_id) const {
    const auto index = resourceIndex(resource_id);
    return index >= 0 ? resources().at(index).toObject().value(QStringLiteral("currentHash")).toString() : QString{};
  }
  [[nodiscard]] QVariantMap abandonPending(const QString& resource_id, const QString& error) {
    const auto index = resourceIndex(resource_id);
    if (index < 0 || !pending(resource_id)) return failure(QStringLiteral("No accepted operation is pending."));
    auto values = resources();
    auto resource = values.at(index).toObject();
    resource.insert(QStringLiteral("pending"), false);
    values.replace(index, resource);
    document_.insert(QStringLiteral("resources"), values);
    auto calls = steps();
    for (qsizetype i = calls.size(); i > 0; --i) {
      auto call = calls.at(i - 1).toObject();
      if (call.value(QStringLiteral("resourceId")) == resource_id &&
          call.value(QStringLiteral("status")) == QStringLiteral("accepted")) {
        call.insert(QStringLiteral("status"), QStringLiteral("failed"));
        call.insert(QStringLiteral("completionError"), error);
        calls.replace(i - 1, call);
        break;
      }
    }
    document_.insert(QStringLiteral("steps"), calls);
    return save() ? report() : failure(error_);
  }

  [[nodiscard]] QVariantMap prepare(const QString& resource_id, const QString& command,
                                   const QVariantMap& arguments, const QByteArray& current) {
    if (!active() || document_.value(QStringLiteral("status")) != QStringLiteral("running"))
      return failure(QStringLiteral("The agent task is not running."));
    if (resource_id.isEmpty() || command.isEmpty() || current.isEmpty())
      return failure(QStringLiteral("The command requires a readable document baseline."));
    auto values = resources();
    auto index = resourceIndex(resource_id);
    if (index < 0) {
      const auto captured = capture(resource_id, current);
      if (!captured.value(QStringLiteral("ok")).toBool()) return captured;
      values = resources();
      index = resourceIndex(resource_id);
    }
    const auto resource = values.at(index).toObject();
    if (resource.value(QStringLiteral("pending")).toBool())
      return failure(QStringLiteral("Wait for the resource's accepted operation before another mutation."));
    if (resource.value(QStringLiteral("currentHash")).toString() != hash(current))
      return failure(QStringLiteral("The document changed outside this agent task. No further edits were applied."));
    auto calls = steps();
    const auto step_id = calls.size();
    calls.append(QJsonObject{{QStringLiteral("id"), static_cast<double>(step_id)},
        {QStringLiteral("resourceId"), resource_id}, {QStringLiteral("command"), command},
        {QStringLiteral("arguments"), QJsonObject::fromVariantMap(arguments)},
        {QStringLiteral("status"), QStringLiteral("prepared")}});
    document_.insert(QStringLiteral("steps"), calls);
    if (!save()) return failure(error_);
    auto result = report();
    result.insert(QStringLiteral("stepId"), step_id);
    return result;
  }

  [[nodiscard]] QVariantMap complete(qsizetype step_id, const QByteArray& current,
                                    const QVariantMap& outcome, bool pending = false) {
    auto calls = steps();
    if (step_id < 0 || step_id >= calls.size() || current.isEmpty())
      return failure(QStringLiteral("Invalid agent operation completion."));
    auto call = calls.at(step_id).toObject();
    if (call.value(QStringLiteral("status")) != QStringLiteral("prepared"))
      return failure(QStringLiteral("This operation already has an outcome; it must not be replayed."));
    call.insert(QStringLiteral("status"), pending ? QStringLiteral("accepted")
        : outcome.value(QStringLiteral("ok")).toBool() ? QStringLiteral("completed") : QStringLiteral("failed"));
    call.insert(QStringLiteral("outcome"), QJsonObject::fromVariantMap(outcome));
    calls.replace(step_id, call);
    document_.insert(QStringLiteral("steps"), calls);
    auto values = resources();
    const auto index = resourceIndex(call.value(QStringLiteral("resourceId")).toString());
    if (index < 0) return failure(QStringLiteral("The operation's baseline is missing."));
    auto resource = values.at(index).toObject();
    resource.insert(QStringLiteral("currentHash"), hash(current));
    resource.insert(QStringLiteral("pending"), pending);
    values.replace(index, resource);
    document_.insert(QStringLiteral("resources"), values);
    return save() ? report() : failure(error_);
  }

  // Called by the native adapter when its owned async job finishes. Ordinary
  // inspection must not adopt arbitrary manual edits as agent-owned state.
  [[nodiscard]] QVariantMap settle(const QString& resource_id, const QByteArray& current, bool ok) {
    const auto index = resourceIndex(resource_id);
    if (index < 0 || current.isEmpty()) return failure(QStringLiteral("The accepted operation has no resource."));
    auto values = resources();
    auto resource = values.at(index).toObject();
    if (!resource.value(QStringLiteral("pending")).toBool())
      return failure(QStringLiteral("The resource has no pending operation."));
    resource.insert(QStringLiteral("currentHash"), hash(current));
    resource.insert(QStringLiteral("pending"), false);
    values.replace(index, resource);
    document_.insert(QStringLiteral("resources"), values);
    auto calls = steps();
    for (qsizetype i = calls.size(); i > 0; --i) {
      auto call = calls.at(i - 1).toObject();
      if (call.value(QStringLiteral("resourceId")) == resource_id &&
          call.value(QStringLiteral("status")) == QStringLiteral("accepted")) {
        call.insert(QStringLiteral("status"), ok ? QStringLiteral("completed") : QStringLiteral("failed"));
        calls.replace(i - 1, call);
        break;
      }
    }
    document_.insert(QStringLiteral("steps"), calls);
    return save() ? report() : failure(error_);
  }

  [[nodiscard]] QVariantMap finish(const QString& status) {
    if (!active() || !QStringList{QStringLiteral("review"), QStringLiteral("interrupted"),
                                 QStringLiteral("committed")}.contains(status))
      return failure(QStringLiteral("Invalid agent task transition."));
    for (const auto& value : resources())
      if (value.toObject().value(QStringLiteral("pending")).toBool() && status != QStringLiteral("interrupted"))
        return failure(QStringLiteral("An accepted operation is still pending."));
    document_.insert(QStringLiteral("status"), status);
    return save() ? report() : failure(error_);
  }

  [[nodiscard]] QVariantMap continueTask() {
    const auto status = document_.value(QStringLiteral("status")).toString();
    if (!QStringList{QStringLiteral("review"), QStringLiteral("interrupted")}.contains(status) ||
        document_.value(QStringLiteral("uncertainOperation")).toBool())
      return failure(QStringLiteral("Only a checkpointed unfinished task can continue."));
    for (const auto& value : resources())
      if (value.toObject().value(QStringLiteral("pending")).toBool())
        return failure(QStringLiteral("An accepted operation is still pending."));
    for (const auto& value : steps())
      if (QStringList{QStringLiteral("prepared"), QStringLiteral("accepted")}.contains(
              value.toObject().value(QStringLiteral("status")).toString()))
        return failure(QStringLiteral("Reconcile the unfinished operation before continuing."));
    const auto previous = document_;
    document_.insert(QStringLiteral("status"), QStringLiteral("running"));
    if (!save()) {
      document_ = previous;
      return failure(error_);
    }
    return report();
  }

  [[nodiscard]] QVariantMap abandonAdmission() {
    if (!active() || !steps().isEmpty()) return failure(QStringLiteral("An accepted operation cannot be abandoned as an empty task."));
    document_.insert(QStringLiteral("status"), QStringLiteral("abandoned"));
    return save() ? report() : failure(error_);
  }

  // The owner must verify this is its exact authored output, not an arbitrary
  // live state read. Used for an existing native pipeline's final recipe receipt.
  [[nodiscard]] QVariantMap recordOwnedState(const QString& resource_id, const QByteArray& authored) {
    const auto index = resourceIndex(resource_id);
    if (!active() || index < 0 || pending(resource_id) || authored.isEmpty())
      return failure(QStringLiteral("Cannot checkpoint this native recipe receipt."));
    auto values = resources();
    auto resource = values.at(index).toObject();
    resource.insert(QStringLiteral("currentHash"), hash(authored));
    values.replace(index, resource);
    document_.insert(QStringLiteral("resources"), values);
    return save() ? report() : failure(error_);
  }

  [[nodiscard]] QVariantMap checkRevert(const QHash<QString, ResourceAdapter>& adapters) const {
    if (!hasChanges()) return failure(QStringLiteral("There is no agent task to revert."));
    for (const auto& value : resources()) {
      const auto resource = value.toObject();
      const auto id = resource.value(QStringLiteral("id")).toString();
      const auto adapter = adapters.value(id);
      if (!adapter.read || !adapter.restore) return failure(QStringLiteral("The document is not available for task recovery."));
      const auto actual = hash(adapter.read());
      if (actual != resource.value(QStringLiteral("currentHash")).toString() &&
          actual != resource.value(QStringLiteral("beforeHash")).toString())
        return failure(QStringLiteral("The document changed after the agent's last checkpoint. Its current edits and recovery baseline were preserved."));
      const auto before = readBaseline(id);
      if (before.isEmpty() || hash(before) != resource.value(QStringLiteral("beforeHash")).toString())
        return failure(QStringLiteral("Agent baseline checksum mismatch; recovery bytes were preserved."));
    }
    return report();
  }

  [[nodiscard]] QVariantMap revert(const QHash<QString, ResourceAdapter>& adapters) {
    if (!hasChanges()) return failure(QStringLiteral("There is no agent task to revert."));
    const auto preflight = checkRevert(adapters);
    if (!preflight.value(QStringLiteral("ok")).toBool()) return preflight;
    const auto values = resources();
    // Cancel producers before restoring any state. Their native generation/token
    // guards must prevent a late callback from publishing into the restored job.
    for (const auto& value : values) {
      const auto id = value.toObject().value(QStringLiteral("id")).toString();
      const auto adapter = adapters.value(id);
      if (!adapter.read || !adapter.restore) return failure(QStringLiteral("The document is not available for task recovery."));
      if (adapter.cancel) adapter.cancel();
    }
    // Preflight every resource so a manual-edit conflict cannot cause a partial
    // rollback of unrelated documents.
    for (const auto& value : values) {
      const auto resource = value.toObject();
      const auto id = resource.value(QStringLiteral("id")).toString();
      const auto expected = resource.value(QStringLiteral("reverted")).toBool()
          ? resource.value(QStringLiteral("beforeHash")).toString()
          : resource.value(QStringLiteral("currentHash")).toString();
      if (hash(adapters.value(id).read()) != expected &&
          hash(adapters.value(id).read()) != resource.value(QStringLiteral("beforeHash")).toString())
        return failure(QStringLiteral("The document changed after the agent's last checkpoint. Its current edits and recovery baseline were preserved."));
      const auto before = readBaseline(id);
      if (before.isEmpty() || hash(before) != resource.value(QStringLiteral("beforeHash")).toString())
        return failure(QStringLiteral("Agent baseline checksum mismatch; recovery bytes were preserved."));
    }
    document_.insert(QStringLiteral("status"), QStringLiteral("reverting"));
    if (!save()) return failure(error_);
    auto updated = resources();
    for (qsizetype i = updated.size(); i > 0; --i) {
      auto resource = updated.at(i - 1).toObject();
      if (resource.value(QStringLiteral("reverted")).toBool()) continue;
      const auto id = resource.value(QStringLiteral("id")).toString();
      const auto before = readBaseline(id);
      const auto adapter = adapters.value(id);
      if ((hash(adapter.read()) != resource.value(QStringLiteral("beforeHash")).toString() && !adapter.restore(before)) ||
          hash(adapter.read()) != resource.value(QStringLiteral("beforeHash")).toString()) {
        auto result = failure(QStringLiteral("Could not restore the document exactly. The task journal was retained."));
        result.insert(QStringLiteral("resourceId"), id);
        result.insert(QStringLiteral("expectedHash"), resource.value(QStringLiteral("beforeHash")));
        result.insert(QStringLiteral("actualHash"), hash(adapter.read()));
        return result;
      }
      resource.insert(QStringLiteral("reverted"), true);
      resource.insert(QStringLiteral("pending"), false);
      updated.replace(i - 1, resource);
      document_.insert(QStringLiteral("resources"), updated);
      if (!save()) return failure(error_);
    }
    document_.insert(QStringLiteral("status"), QStringLiteral("reverted"));
    return save() ? report() : failure(error_);
  }

  [[nodiscard]] QVariantMap report() const {
    auto value = document_.toVariantMap();
    value.insert(QStringLiteral("ok"), true);
    value.insert(QStringLiteral("journalPath"), journalPath());
    value.insert(QStringLiteral("revertAvailable"), hasChanges());
    return value;
  }

 private:
  static QString hash(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  }
  static QVariantMap failure(const QString& message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
  }
  QJsonArray resources() const { return document_.value(QStringLiteral("resources")).toArray(); }
  QJsonArray steps() const { return document_.value(QStringLiteral("steps")).toArray(); }
  int resourceIndex(const QString& id) const {
    const auto values = resources();
    for (qsizetype i = 0; i < values.size(); ++i)
      if (values.at(i).toObject().value(QStringLiteral("id")) == id) return static_cast<int>(i);
    return -1;
  }
  QString baselinePath(const QString& id) const {
    return QDir(directory_).filePath(QStringLiteral("%1.baseline").arg(hash(id.toUtf8())));
  }
  QByteArray readBaseline(const QString& id) const {
    QFile file(baselinePath(id));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
  }
  bool writeBytes(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
      error_ = QStringLiteral("Could not durably save the agent task journal: %1").arg(file.errorString());
      return false;
    }
    return true;
  }
  bool save() { return writeBytes(journalPath(), QJsonDocument(document_).toJson(QJsonDocument::Compact)); }

  QString directory_;
  QString error_;
  QJsonObject document_;
};

}  // namespace photo_editor::desktop

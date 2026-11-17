#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <functional>

namespace photo_editor::desktop {

// A task owns explicit document resources, never the entire workspace. Baselines
// and accepted operations survive a process exit. Native adapters retain control
// of cancellation and restoration; this journal never replays an uncertain call.
class AgentTaskJournal final {
 public:
  [[nodiscard]] static QStringList synchronousTimelineDraftBatchCommandIds() {
    return {
        QStringLiteral("timeline.action.createTimelineTrack"),
        QStringLiteral("timeline.action.moveTimelineGraphicsTrack"),
        QStringLiteral("timeline.action.createTimelineTitle"),
        QStringLiteral("timeline.action.createTimelineGraphic"),
        QStringLiteral("timeline.action.createTimelineGenerator"),
        QStringLiteral("timeline.action.setTimelineClipTiming"),
        QStringLiteral("timeline.action.setTimelineClipTitle"),
        QStringLiteral("timeline.action.setTimelineClipFrame"),
        QStringLiteral("timeline.action.setTimelineClipPerspectiveQuad"),
        QStringLiteral("timeline.action.setTimelineClipPerspectiveQuadAnimation"),
        QStringLiteral("timeline.action.setTimelineClipMask"),
        QStringLiteral("timeline.action.clearTimelineClipMask"),
        QStringLiteral("timeline.action.setTimelineClipBlendMode"),
        QStringLiteral("timeline.action.setTimelineClipAnimation"),
        QStringLiteral("timeline.action.editTimelineClipKeyframe")};
  }

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
    const auto timeline_scope = document_.value(QStringLiteral("nativeTimelineScope")).toObject();
    if (document_.contains(QStringLiteral("nativeTimelineScope")) && !validNativeTimelineScope(timeline_scope)) {
      document_ = {};
      return failure(QStringLiteral("Native Timeline task scope checksum mismatch; recovery bytes were preserved."));
    }
    for (const auto& value : resources()) {
      const auto resource = value.toObject();
      const auto before = readBaseline(resource.value(QStringLiteral("id")).toString());
      if (before.isEmpty() || hash(before) != resource.value(QStringLiteral("beforeHash")).toString()) {
        document_ = {};
        return failure(QStringLiteral("Agent baseline checksum mismatch; recovery bytes were preserved."));
      }
      if (resource.contains(QStringLiteral("releasedSnapshotHash"))) {
        const auto released = readReleasedSnapshot(resource.value(QStringLiteral("id")).toString());
        if (released.isEmpty() || hash(released) != resource.value(QStringLiteral("releasedSnapshotHash")).toString()) {
          document_ = {};
          return failure(QStringLiteral("Released task checkpoint checksum mismatch; recovery bytes were preserved."));
        }
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
        status != QStringLiteral("abandoned") && status != QStringLiteral("released");
  }
  [[nodiscard]] bool hasChanges() const {
    return !resources().isEmpty() && document_.value(QStringLiteral("status")) != QStringLiteral("reverted") &&
        document_.value(QStringLiteral("status")) != QStringLiteral("abandoned");
  }
  [[nodiscard]] QString taskId() const { return document_.value(QStringLiteral("taskId")).toString(); }
  [[nodiscard]] QString journalPath() const { return QDir(directory_).filePath(QStringLiteral("journal.json")); }

  // Batch retries need only the task status and one compact receipt. Keep this
  // lookup on the existing JSON representation so large historical arguments
  // (including keyframe arrays) are not converted to QVariant on every call.
  [[nodiscard]] QVariantMap lookupIdempotentBatchStep(const QString& batch_id) const {
    if (batch_id.isEmpty()) return failure(QStringLiteral("A stable batch ID is required for journal lookup."));

    auto found = false;
    QJsonObject matched_step;
    const auto calls = document_.value(QStringLiteral("steps")).toArray();
    for (const auto& value : calls) {
      const auto step = value.toObject();
      const auto arguments = step.value(QStringLiteral("arguments")).toObject();
      if (arguments.value(QStringLiteral("_batchId")).toString() != batch_id) continue;
      if (found) {
        auto result = failure(QStringLiteral("This batch ID appears more than once in the task journal; replay is ambiguous."));
        result.insert(QStringLiteral("ambiguousBatchId"), true);
        return result;
      }
      matched_step = step;
      found = true;
    }

    QVariantMap result{
        {QStringLiteral("ok"), true},
        {QStringLiteral("taskStatus"), document_.value(QStringLiteral("status")).toString()},
        {QStringLiteral("found"), found}};
    if (!found) return result;

    const auto arguments = matched_step.value(QStringLiteral("arguments")).toObject();
    result.insert(QStringLiteral("command"), matched_step.value(QStringLiteral("command")).toString());
    result.insert(QStringLiteral("resourceId"), matched_step.value(QStringLiteral("resourceId")).toString());
    result.insert(QStringLiteral("requestSha256"),
                  arguments.value(QStringLiteral("_batchRequestSha256")).toString());
    result.insert(QStringLiteral("stepStatus"), matched_step.value(QStringLiteral("status")).toString());
    result.insert(QStringLiteral("stepId"), matched_step.value(QStringLiteral("id")).toVariant());
    result.insert(QStringLiteral("outcome"), matched_step.value(QStringLiteral("outcome")).toObject().toVariantMap());
    return result;
  }

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

  // External native tasks can own one Timeline plus an explicit set of source
  // files and an output directory. Persist this scope beside the resource
  // baselines so recovery never has to reconstruct authority from caller input.
  [[nodiscard]] QVariantMap recordNativeTimelineScope(
      const QStringList& target_ids, const QStringList& asset_paths,
      const QString& output_directory) {
    if (!active() || document_.value(QStringLiteral("status")) != QStringLiteral("running") ||
        target_ids.size() != 1 || target_ids.front().trimmed().isEmpty() ||
        asset_paths.size() > 256 || output_directory.trimmed().isEmpty())
      return failure(QStringLiteral("A running task requires one stable Timeline target and an output directory."));
    QSet<QString> unique_targets;
    for (const auto& id : target_ids) {
      if (id.trimmed() != id || unique_targets.contains(id))
        return failure(QStringLiteral("Timeline task targets must be unique stable resource IDs."));
      unique_targets.insert(id);
    }
    QSet<QString> unique_paths;
    for (const auto& path : asset_paths) {
      if (path.trimmed().isEmpty() || path.trimmed() != path || unique_paths.contains(path))
        return failure(QStringLiteral("Timeline task source paths must be unique canonical file paths."));
      unique_paths.insert(path);
    }
    if (QFileInfo(output_directory).canonicalFilePath() != output_directory ||
        !QFileInfo(output_directory).isDir())
      return failure(QStringLiteral("The Timeline task output directory must be an existing canonical directory."));
    for (const auto& path : asset_paths) {
      const QFileInfo info(path);
      if (!info.isFile() || info.canonicalFilePath() != path)
        return failure(QStringLiteral("Every Timeline task source must be an existing canonical regular file."));
    }
    const auto existing = document_.value(QStringLiteral("nativeTimelineScope"));
    if (!existing.isUndefined())
      return failure(QStringLiteral("The native Timeline task scope is already fixed."));
    const auto previous = document_;
    QJsonObject scope{
        {QStringLiteral("targetIds"), QJsonArray::fromStringList(target_ids)},
        {QStringLiteral("assetPaths"), QJsonArray::fromStringList(asset_paths)},
        {QStringLiteral("outputDirectory"), output_directory},
        {QStringLiteral("outputPaths"), QJsonArray{}}};
    sealNativeTimelineScope(&scope);
    document_.insert(QStringLiteral("nativeTimelineScope"), scope);
    if (!save()) {
      document_ = previous;
      return failure(error_);
    }
    return report();
  }

  [[nodiscard]] QVariantMap nativeTimelineScope() const {
    const auto scope = document_.value(QStringLiteral("nativeTimelineScope")).toObject();
    return validNativeTimelineScope(scope) ? scope.toVariantMap() : QVariantMap{};
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

  [[nodiscard]] QVariantMap recordPreparedProducerLaunchSpec(
      qsizetype step_id,
      const QString& resource_id,
      const QVariantMap& launch_spec) {
    const auto calls = steps();
    if (!active() || document_.value(QStringLiteral("status")) != QStringLiteral("running") ||
        step_id < 0 || step_id >= calls.size() || launch_spec.isEmpty()) {
      return failure(QStringLiteral("A running task and prepared compound operation are required."));
    }
    auto call = calls.at(step_id).toObject();
    if (call.value(QStringLiteral("resourceId")).toString() != resource_id ||
        call.value(QStringLiteral("command")).toString() !=
            QStringLiteral("timeline.action.createTimelineCompound") ||
        call.value(QStringLiteral("status")).toString() != QStringLiteral("prepared")) {
      return failure(QStringLiteral("The producer launch specification does not match a prepared compound operation."));
    }
    auto arguments = call.value(QStringLiteral("arguments")).toObject();
    if (arguments.contains(QStringLiteral("producerLaunchSpec"))) {
      return failure(QStringLiteral("The prepared compound operation already has a producer launch specification."));
    }
    arguments.insert(QStringLiteral("producerLaunchSpec"),
                     QJsonObject::fromVariantMap(launch_spec));
    call.insert(QStringLiteral("arguments"), arguments);
    auto updated_calls = calls;
    updated_calls.replace(step_id, call);
    document_.insert(QStringLiteral("steps"), updated_calls);
    return save() ? report() : failure(error_);
  }

  // Compound rendering changes only transient compoundStatus/compoundError
  // fields while its asynchronous producer runs. Persist a projection hash
  // after child+parent creation and before launch so recovery can distinguish
  // those renderer-owned status changes from unrelated Timeline edits.
  [[nodiscard]] QVariantMap recordPreparedCompoundCheckpoint(
      qsizetype step_id,
      const QString& resource_id,
      const QString& compound_clip_id,
      const QString& document_projection_sha256) {
    const auto calls = steps();
    const QUuid clip_uuid(compound_clip_id);
    if (!active() || document_.value(QStringLiteral("status")) != QStringLiteral("running") ||
        step_id < 0 || step_id >= calls.size() ||
        document_projection_sha256.size() != 64 ||
        QByteArray::fromHex(document_projection_sha256.toLatin1()).size() != 32 ||
        QByteArray::fromHex(document_projection_sha256.toLatin1()).toHex() !=
            document_projection_sha256.toLatin1() ||
        clip_uuid.isNull() ||
        clip_uuid.toString(QUuid::WithoutBraces).toLower() != compound_clip_id) {
      return failure(QStringLiteral("A prepared compound clip ID and document projection checksum are required."));
    }
    auto call = calls.at(step_id).toObject();
    if (call.value(QStringLiteral("resourceId")).toString() != resource_id ||
        call.value(QStringLiteral("command")).toString() !=
            QStringLiteral("timeline.action.createTimelineCompound") ||
        call.value(QStringLiteral("status")).toString() != QStringLiteral("prepared")) {
      return failure(QStringLiteral("The compound checkpoint does not match a prepared Timeline action."));
    }
    auto arguments = call.value(QStringLiteral("arguments")).toObject();
    if (arguments.contains(QStringLiteral("compoundClipId")) ||
        arguments.contains(QStringLiteral("compoundDocumentProjectionSha256"))) {
      return failure(QStringLiteral("The prepared compound checkpoint was already recorded."));
    }
    arguments.insert(QStringLiteral("compoundClipId"), compound_clip_id);
    arguments.insert(QStringLiteral("compoundDocumentProjectionSha256"),
                     document_projection_sha256);
    call.insert(QStringLiteral("arguments"), arguments);
    auto updated_calls = calls;
    updated_calls.replace(step_id, call);
    document_.insert(QStringLiteral("steps"), updated_calls);
    return save() ? report() : failure(error_);
  }

  [[nodiscard]] QVariantMap recordAcceptedProducerReceipt(
      qsizetype step_id,
      const QString& resource_id,
      const QVariantMap& producer_receipt) {
    const auto calls = steps();
    if (!active() || document_.value(QStringLiteral("status")) != QStringLiteral("running") ||
        step_id < 0 || step_id >= calls.size() || producer_receipt.isEmpty()) {
      return failure(QStringLiteral("A running task and accepted compound producer are required."));
    }
    auto call = calls.at(step_id).toObject();
    if (call.value(QStringLiteral("resourceId")).toString() != resource_id ||
        call.value(QStringLiteral("command")).toString() !=
            QStringLiteral("timeline.action.createTimelineCompound") ||
        call.value(QStringLiteral("status")).toString() != QStringLiteral("accepted") ||
        !pending(resource_id)) {
      return failure(QStringLiteral("The producer receipt does not match an accepted pending compound step."));
    }
    auto arguments = call.value(QStringLiteral("arguments")).toObject();
    const auto launch_spec = arguments.value(QStringLiteral("producerLaunchSpec")).toObject();
    const auto receipt = QJsonObject::fromVariantMap(producer_receipt);
    const auto exact_string = [&launch_spec, &receipt](const QString& key) {
      const auto expected = launch_spec.value(key);
      const auto actual = receipt.value(key);
      return expected.isString() && actual.isString() && expected == actual;
    };
    const auto receipt_hash = receipt.value(QStringLiteral("receiptSha256")).toString().toLatin1();
    const auto argv_equal = launch_spec.value(QStringLiteral("argv")).isArray() &&
        receipt.value(QStringLiteral("argv")).isArray() &&
        launch_spec.value(QStringLiteral("argv")) == receipt.value(QStringLiteral("argv"));
    if (launch_spec.isEmpty() || !argv_equal ||
        receipt.value(QStringLiteral("schema")).toString() !=
            QStringLiteral("looksy.timeline.compound-producer/v1") ||
        !exact_string(QStringLiteral("operationId")) ||
        !exact_string(QStringLiteral("childTimelineId")) ||
        !exact_string(QStringLiteral("executablePath")) ||
        !exact_string(QStringLiteral("bootSessionId")) ||
        !exact_string(QStringLiteral("documentPath")) ||
        !exact_string(QStringLiteral("renderPath")) ||
        !exact_string(QStringLiteral("partialPath")) ||
        !exact_string(QStringLiteral("filterGraphPath")) ||
        !exact_string(QStringLiteral("producerReceiptPath")) ||
        receipt.value(QStringLiteral("uid")).toInteger() != launch_spec.value(QStringLiteral("uid")).toInteger() ||
        receipt.value(QStringLiteral("pid")).toInteger() <= 0 ||
        receipt_hash.size() != 64 || QByteArray::fromHex(receipt_hash).size() != 32 ||
        QByteArray::fromHex(receipt_hash).toHex() != receipt_hash) {
      return failure(QStringLiteral("The producer receipt does not match its sealed launch specification."));
    }
    auto outcome = call.value(QStringLiteral("outcome")).toObject();
    if (outcome.contains(QStringLiteral("producerReceipt"))) {
      return failure(QStringLiteral("This accepted compound step already has a producer receipt."));
    }
    outcome.insert(QStringLiteral("producerReceipt"), receipt);
    outcome.insert(QStringLiteral("producerReceiptPath"), receipt.value(QStringLiteral("producerReceiptPath")));
    call.insert(QStringLiteral("outcome"), outcome);
    auto updated_calls = calls;
    updated_calls.replace(step_id, call);
    document_.insert(QStringLiteral("steps"), updated_calls);
    return save() ? report() : failure(error_);
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
  [[nodiscard]] QVariantMap settle(
      const QString& resource_id, const QByteArray& current, bool ok,
      const QVariantMap& settlement = {}) {
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
        if (!settlement.isEmpty()) {
          call.insert(QStringLiteral("settlement"), QJsonObject::fromVariantMap(settlement));
        }
        calls.replace(i - 1, call);
        break;
      }
    }
    document_.insert(QStringLiteral("steps"), calls);
    return save() ? report() : failure(error_);
  }

  [[nodiscard]] QVariantMap resolvePreparedStep(
      qsizetype step_id, const QString& resource_id, const QByteArray& current,
      const QVariantMap& outcome) {
    const auto calls = steps();
    if (step_id < 0 || step_id >= calls.size() ||
        calls.at(step_id).toObject().value(QStringLiteral("resourceId")) != resource_id ||
        calls.at(step_id).toObject().value(QStringLiteral("status")) != QStringLiteral("prepared")) {
      return failure(QStringLiteral("The interrupted operation is not a prepared step for this resource."));
    }
    const auto completed = complete(step_id, current, outcome, false);
    if (!completed.value(QStringLiteral("ok")).toBool()) return completed;
    const auto remaining_calls = steps();
    const auto has_unresolved_preparation = std::any_of(
        remaining_calls.cbegin(), remaining_calls.cend(), [](const QJsonValue& value) {
          return value.toObject().value(QStringLiteral("status")) == QStringLiteral("prepared");
        });
    if (!has_unresolved_preparation) {
      document_.remove(QStringLiteral("uncertainOperation"));
      if (!save()) return failure(error_);
    }
    return report();
  }

  [[nodiscard]] QVariantMap reconcilePreparedUnchangedTimelineDraftBatch(
      const QString& resource_id,
      const QHash<QString, QByteArray>& observed_resource_states) {
    if (document_.value(QStringLiteral("status")) != QStringLiteral("interrupted") ||
        !document_.value(QStringLiteral("uncertainOperation")).toBool() ||
        resource_id.isEmpty() || observed_resource_states.isEmpty()) {
      return failure(QStringLiteral("Unchanged Timeline batch reconciliation requires an interrupted task and exact observed resource states."));
    }

    const auto scope = nativeTimelineScope();
    const auto target_ids = scope.value(QStringLiteral("targetIds")).toStringList();
    if (target_ids != QStringList{resource_id}) {
      return failure(QStringLiteral("The interrupted task does not admit this exact Timeline resource for batch reconciliation."));
    }

    const auto owned_resources = resources();
    if (owned_resources.size() != observed_resource_states.size()) {
      return failure(QStringLiteral("Every admitted resource must be read and checkpoint-verified before reconciliation."));
    }
    for (const auto& value : owned_resources) {
      const auto resource = value.toObject();
      const auto id = resource.value(QStringLiteral("id")).toString();
      const auto state = observed_resource_states.value(id);
      if (id.isEmpty() || state.isEmpty() || resource.value(QStringLiteral("pending")).toBool() ||
          resource.value(QStringLiteral("reverted")).toBool() ||
          resource.value(QStringLiteral("currentHash")).toString() != hash(state)) {
        return failure(QStringLiteral("An owned Timeline or output resource is pending, unreadable, or changed from its exact checkpoint; reconciliation was withheld."));
      }
    }

    const auto calls = steps();
    qsizetype prepared_step_id = -1;
    auto prepared_count = qsizetype{0};
    auto accepted_count = qsizetype{0};
    for (qsizetype index = 0; index < calls.size(); ++index) {
      const auto step = calls.at(index).toObject();
      const auto status = step.value(QStringLiteral("status")).toString();
      if (status == QStringLiteral("prepared")) {
        ++prepared_count;
        prepared_step_id = index;
      } else if (status == QStringLiteral("accepted")) {
        ++accepted_count;
      } else if (status != QStringLiteral("completed") && status != QStringLiteral("failed")) {
        return failure(QStringLiteral("A task step has an unknown outcome; unchanged-state reconciliation was withheld."));
      }
    }
    if (prepared_count != 1 || accepted_count != 0 || prepared_step_id < 0) {
      return failure(QStringLiteral("Reconciliation requires exactly one prepared step and no accepted or other unresolved task outcomes."));
    }

    const auto step = calls.at(prepared_step_id).toObject();
    if (step.value(QStringLiteral("resourceId")).toString() != resource_id ||
        step.value(QStringLiteral("command")).toString() !=
            QStringLiteral("timeline.action.applyDraftBatch")) {
      return failure(QStringLiteral("Only a prepared synchronous Timeline draft batch can be resolved from an unchanged checkpoint."));
    }

    auto arguments = step.value(QStringLiteral("arguments")).toObject();
    const QSet<QString> expected_argument_keys{
        QStringLiteral("batchId"), QStringLiteral("operations"),
        QStringLiteral("_batchId"), QStringLiteral("_batchRequestSha256")};
    auto arguments_match = arguments.size() == expected_argument_keys.size();
    for (const auto& key : arguments.keys()) {
      arguments_match = arguments_match && expected_argument_keys.contains(key);
    }
    if (!arguments_match) {
      return failure(QStringLiteral("The prepared batch has unexpected arguments; the uncertain operation remains unresolved."));
    }
    const auto batch_id = arguments.value(QStringLiteral("batchId")).toString();
    const auto recorded_batch_id = arguments.value(QStringLiteral("_batchId")).toString();
    const auto request_sha = arguments.value(QStringLiteral("_batchRequestSha256")).toString();
    const QUuid parsed_batch_id(batch_id);
    if (batch_id.isEmpty() || parsed_batch_id.isNull() ||
        parsed_batch_id.toString(QUuid::WithoutBraces).toLower() != batch_id ||
        recorded_batch_id != batch_id || !is_lower_hex_sha256(request_sha)) {
      return failure(QStringLiteral("The prepared batch ID or request digest is malformed; the uncertain operation remains unresolved."));
    }

    auto request_arguments = arguments.toVariantMap();
    request_arguments.remove(QStringLiteral("_batchId"));
    request_arguments.remove(QStringLiteral("_batchRequestSha256"));
    const auto request_bytes = QJsonDocument::fromVariant(request_arguments).toJson(QJsonDocument::Compact);
    if (hash(request_bytes) != request_sha) {
      return failure(QStringLiteral("The prepared batch request does not match its recorded digest; the uncertain operation remains unresolved."));
    }

    const auto operations = arguments.value(QStringLiteral("operations")).toArray();
    const auto allowed_commands = synchronousTimelineDraftBatchCommandIds();
    const QRegularExpression operation_id_pattern(
        QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,63}$"));
    QSet<QString> seen_operation_ids;
    if (operations.isEmpty() || operations.size() > 16) {
      return failure(QStringLiteral("The prepared batch is outside the synchronous draft transaction limits."));
    }
    for (const auto& operation_value : operations) {
      if (!operation_value.isObject()) {
        return failure(QStringLiteral("The prepared batch contains a non-object operation."));
      }
      const auto operation = operation_value.toObject();
      const auto operation_id = operation.value(QStringLiteral("id")).toString();
      const auto command_id = operation.value(QStringLiteral("commandId")).toString();
      const auto values = operation.value(QStringLiteral("values"));
      if (operation.size() != 3 || !operation.contains(QStringLiteral("id")) ||
          !operation.contains(QStringLiteral("commandId")) ||
          !operation.contains(QStringLiteral("values")) ||
          !operation_id_pattern.match(operation_id).hasMatch() ||
          seen_operation_ids.contains(operation_id) || !allowed_commands.contains(command_id) ||
          !values.isObject() || values.toObject().size() > 64) {
        return failure(QStringLiteral("The prepared batch contains a command outside the synchronous document-only allowlist."));
      }
      seen_operation_ids.insert(operation_id);
    }

    const auto timeline_state = observed_resource_states.value(resource_id);
    const QVariantMap outcome{
        {QStringLiteral("ok"), false},
        {QStringLiteral("changed"), false},
        {QStringLiteral("mutationNotApplied"), true},
        {QStringLiteral("recoveredUnchanged"), true},
        {QStringLiteral("resolution"), QStringLiteral("unchanged-synchronous-timeline-batch")},
        {QStringLiteral("batchId"), batch_id},
        {QStringLiteral("batchRequestSha256"), request_sha},
        {QStringLiteral("error"), QStringLiteral("The prepared batch was not replayed; exact Timeline and output resource bytes still match their recorded checkpoints.")}};
    auto resolved_step = step;
    resolved_step.insert(QStringLiteral("status"), QStringLiteral("failed"));
    resolved_step.insert(QStringLiteral("outcome"), QJsonObject::fromVariantMap(outcome));
    auto resolved_steps = calls;
    resolved_steps.replace(prepared_step_id, resolved_step);
    auto resolved_resources = owned_resources;
    for (qsizetype index = 0; index < resolved_resources.size(); ++index) {
      auto resource = resolved_resources.at(index).toObject();
      if (resource.value(QStringLiteral("id")).toString() != resource_id) continue;
      resource.insert(QStringLiteral("currentHash"), hash(timeline_state));
      resource.insert(QStringLiteral("pending"), false);
      resolved_resources.replace(index, resource);
      break;
    }
    const auto previous_document = document_;
    document_.insert(QStringLiteral("steps"), resolved_steps);
    document_.insert(QStringLiteral("resources"), resolved_resources);
    document_.remove(QStringLiteral("uncertainOperation"));
    if (!save()) {
      document_ = previous_document;
      return failure(error_);
    }
    auto resolved = report();
    resolved.insert(QStringLiteral("reconciledStepId"), prepared_step_id);
    resolved.insert(QStringLiteral("recoveredUnchanged"), true);
    resolved.insert(QStringLiteral("mutationNotApplied"), true);
    return resolved;
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
    if (!QStringList{QStringLiteral("review"), QStringLiteral("interrupted"),
                     QStringLiteral("released")}.contains(status) ||
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

  [[nodiscard]] QVariantMap releaseReviewedDraft(const QHash<QString, QByteArray>& snapshots) {
    if (!active() || document_.value(QStringLiteral("status")) != QStringLiteral("review") ||
        document_.value(QStringLiteral("uncertainOperation")).toBool()) {
      return failure(QStringLiteral("Only a settled reviewed Timeline draft can release ownership."));
    }
    for (const auto& value : resources()) {
      if (value.toObject().value(QStringLiteral("pending")).toBool()) {
        return failure(QStringLiteral("Settle every accepted operation before releasing this draft."));
      }
    }
    for (const auto& value : steps()) {
      const auto status = value.toObject().value(QStringLiteral("status")).toString();
      if (status == QStringLiteral("prepared") || status == QStringLiteral("accepted")) {
        return failure(QStringLiteral("Reconcile the unfinished operation before releasing this draft."));
      }
    }
    const auto target_ids = nativeTimelineScope().value(QStringLiteral("targetIds")).toStringList();
    const auto previous = document_;
    auto updated_resources = resources();
    for (qsizetype index = 0; index < updated_resources.size(); ++index) {
      auto resource = updated_resources.at(index).toObject();
      const auto id = resource.value(QStringLiteral("id")).toString();
      if (!target_ids.contains(id)) continue;
      const auto snapshot = snapshots.value(id);
      const auto snapshot_hash = hash(snapshot);
      if (snapshot.isEmpty() || snapshot_hash != resource.value(QStringLiteral("currentHash")).toString())
        return failure(QStringLiteral("The reviewed Timeline release snapshot does not match its exact current checkpoint."));
      if (!writeBytes(releasedSnapshotPath(id), snapshot)) return failure(error_);
      resource.insert(QStringLiteral("releasedSnapshotHash"), snapshot_hash);
      updated_resources.replace(index, resource);
    }
    if (!target_ids.isEmpty()) {
      for (const auto& id : target_ids) {
        const auto index = resourceIndex(id);
        if (index < 0 || !updated_resources.at(index).toObject().contains(QStringLiteral("releasedSnapshotHash")))
          return failure(QStringLiteral("A reviewed Timeline target is missing its durable release snapshot."));
      }
    }
    document_.insert(QStringLiteral("resources"), updated_resources);
    document_.insert(QStringLiteral("status"), QStringLiteral("released"));
    if (!save()) {
      document_ = previous;
      return failure(error_);
    }
    return report();
  }

  [[nodiscard]] QVariantMap reconcileReleasedTimelineSourceIntegrity(
      const QString& resource_id, const QByteArray& current, bool persist = true) {
    if (document_.value(QStringLiteral("status")).toString() != QStringLiteral("released") ||
        current.isEmpty())
      return failure(QStringLiteral("Derived source identity can reconcile only for a released Timeline task."));
    const auto scope = nativeTimelineScope();
    const auto targets = scope.value(QStringLiteral("targetIds")).toStringList();
    if (targets.size() != 1 || targets.front() != resource_id)
      return failure(QStringLiteral("The released task does not admit this Timeline resource for reconciliation."));
    const auto index = resourceIndex(resource_id);
    if (index < 0) return failure(QStringLiteral("The released Timeline resource checkpoint is missing."));
    auto values = resources();
    auto resource = values.at(index).toObject();
    if (resource.value(QStringLiteral("pending")).toBool())
      return failure(QStringLiteral("The released Timeline still has an unsettled native operation."));
    const auto current_hash = hash(current);
    if (current_hash == resource.value(QStringLiteral("currentHash")).toString() ||
        current_hash == resource.value(QStringLiteral("beforeHash")).toString())
      return {{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false}};
    const auto expected_release_hash = resource.value(QStringLiteral("releasedSnapshotHash")).toString();
    if (expected_release_hash.isEmpty())
      return failure(QStringLiteral("This released task predates durable source-integrity reconciliation; its checkpoint was preserved."));
    const auto released_snapshot = readReleasedSnapshot(resource_id);
    if (released_snapshot.isEmpty() || hash(released_snapshot) != expected_release_hash)
      return failure(QStringLiteral("The released Timeline snapshot is unavailable or corrupt; its checkpoint was preserved."));
    QJsonParseError release_parse;
    QJsonParseError current_parse;
    const auto release_document = QJsonDocument::fromJson(released_snapshot, &release_parse);
    const auto current_document = QJsonDocument::fromJson(current, &current_parse);
    if (release_parse.error != QJsonParseError::NoError || !release_document.isObject() ||
        current_parse.error != QJsonParseError::NoError || !current_document.isObject())
      return failure(QStringLiteral("The released Timeline state cannot be safely compared; its checkpoint was preserved."));
    const auto allowed_paths = scope.value(QStringLiteral("assetPaths")).toStringList();
    QSet<QString> changed_source_paths;
    QString diff_error;
    if (!only_admitted_source_integrity_changed(
            release_document.object(), current_document.object(), allowed_paths,
            &changed_source_paths, &diff_error)) {
      return failure(diff_error.isEmpty()
          ? QStringLiteral("The released Timeline contains changes outside admitted source integrity metadata; its checkpoint was preserved.")
          : diff_error);
    }
    if (changed_source_paths.isEmpty())
      return failure(QStringLiteral("The released Timeline differs without a verified admitted source-integrity update."));

    QVariantMap source_sha256_by_path;
    for (const auto& path : changed_source_paths) {
      const auto clip = find_clip_by_path(current_document.object(), path);
      source_sha256_by_path.insert(path, clip.value(QStringLiteral("sourceContentSha256")).toString());
    }
    const auto previous_checkpoint_hash = resource.value(QStringLiteral("currentHash")).toString();
    if (!persist) {
      return {{QStringLiteral("ok"), true}, {QStringLiteral("changed"), true},
          {QStringLiteral("resourceId"), resource_id},
          {QStringLiteral("previousCheckpointHash"), previous_checkpoint_hash},
          {QStringLiteral("proposedCurrentHash"), current_hash},
          {QStringLiteral("sourceSha256ByPath"), source_sha256_by_path}};
    }
    resource.insert(QStringLiteral("currentHash"), current_hash);
    values.replace(index, resource);
    const auto previous_document = document_;
    document_.insert(QStringLiteral("resources"), values);
    auto settlements = document_.value(QStringLiteral("derivedIntegritySettlements")).toArray();
    settlements.append(QJsonObject{
        {QStringLiteral("resourceId"), resource_id},
        {QStringLiteral("releasedSnapshotHash"), expected_release_hash},
        {QStringLiteral("previousCheckpointHash"), previous_checkpoint_hash},
        {QStringLiteral("settledHash"), current_hash},
        {QStringLiteral("sourceSha256ByPath"), QJsonObject::fromVariantMap(source_sha256_by_path)},
        {QStringLiteral("settledAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    });
    document_.insert(QStringLiteral("derivedIntegritySettlements"), settlements);
    if (!save()) {
      document_ = previous_document;
      return failure(error_);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("changed"), true},
        {QStringLiteral("resourceId"), resource_id},
        {QStringLiteral("previousCheckpointHash"), previous_checkpoint_hash},
        {QStringLiteral("settledHash"), current_hash},
        {QStringLiteral("sourceSha256ByPath"), source_sha256_by_path}};
  }

  // A recovered running task can settle source identity only before its first
  // accepted operation. This preserves the exact before-task baseline while
  // checkpointing narrowly verified enrichment for admitted files.
  [[nodiscard]] QVariantMap reconcileUnmutatedTimelineSourceIntegrity(
      const QString& resource_id, const QByteArray& current) {
    if (document_.value(QStringLiteral("status")).toString() != QStringLiteral("running") ||
        current.isEmpty() || !steps().isEmpty() ||
        document_.value(QStringLiteral("uncertainOperation")).toBool() || resources().size() != 1) {
      return failure(QStringLiteral("Source identity can settle during recovery only for an unmutated running Timeline task."));
    }
    const auto scope = nativeTimelineScope();
    const auto targets = scope.value(QStringLiteral("targetIds")).toStringList();
    if (targets.size() != 1 || targets.front() != resource_id)
      return failure(QStringLiteral("The unmutated task does not admit this exact Timeline resource."));
    const auto index = resourceIndex(resource_id);
    if (index < 0) return failure(QStringLiteral("The unmutated Timeline baseline is missing."));
    auto values = resources();
    auto resource = values.at(index).toObject();
    const auto before_hash = resource.value(QStringLiteral("beforeHash")).toString();
    const auto previous_checkpoint_hash = resource.value(QStringLiteral("currentHash")).toString();
    if (resource.value(QStringLiteral("pending")).toBool() ||
        resource.value(QStringLiteral("reverted")).toBool() || before_hash.isEmpty() ||
        previous_checkpoint_hash != before_hash) {
      return failure(QStringLiteral("The unmutated Timeline checkpoint is pending or differs from its before-task baseline."));
    }
    const auto baseline = readBaseline(resource_id);
    if (baseline.isEmpty() || hash(baseline) != before_hash)
      return failure(QStringLiteral("The unmutated Timeline baseline is unavailable or corrupt."));

    QJsonParseError baseline_parse;
    QJsonParseError current_parse;
    const auto baseline_document = QJsonDocument::fromJson(baseline, &baseline_parse);
    const auto current_document = QJsonDocument::fromJson(current, &current_parse);
    if (baseline_parse.error != QJsonParseError::NoError || !baseline_document.isObject() ||
        current_parse.error != QJsonParseError::NoError || !current_document.isObject()) {
      return failure(QStringLiteral("The unmutated Timeline state cannot be compared safely."));
    }
    const auto admitted_paths = scope.value(QStringLiteral("assetPaths")).toStringList();
    bool source_identity_pending = false;
    QString readiness_error;
    if (!validate_admitted_source_readiness(current_document.object(), admitted_paths,
                                             &source_identity_pending, &readiness_error)) {
      return failure(readiness_error.isEmpty()
          ? QStringLiteral("Admitted source identity could not be verified; the before-task checkpoint was preserved.")
          : readiness_error);
    }
    if (source_identity_pending) {
      return {{QStringLiteral("ok"), false}, {QStringLiteral("retryable"), true},
          {QStringLiteral("status"), QStringLiteral("source_integrity_pending")},
          {QStringLiteral("error"), QStringLiteral("Wait for every admitted source path, size, and full SHA-256 to settle before recovering this unmutated task.")}};
    }

    const auto settled_hash = hash(current);
    if (settled_hash == previous_checkpoint_hash)
      return {{QStringLiteral("ok"), true}, {QStringLiteral("changed"), false},
          {QStringLiteral("resourceId"), resource_id}, {QStringLiteral("currentHash"), settled_hash}};

    QSet<QString> changed_source_paths;
    QString diff_error;
    if (!only_admitted_source_integrity_changed(
            baseline_document.object(), current_document.object(), admitted_paths,
            &changed_source_paths, &diff_error)) {
      return failure(diff_error.isEmpty()
          ? QStringLiteral("The running Timeline changed beyond admitted source integrity fields; its before-task checkpoint was preserved.")
          : diff_error);
    }
    if (changed_source_paths.isEmpty())
      return failure(QStringLiteral("The running Timeline differs without verified admitted source integrity enrichment."));

    QVariantMap source_sha256_by_path;
    for (const auto& path : changed_source_paths) {
      const auto clip = find_clip_by_path(current_document.object(), path);
      source_sha256_by_path.insert(path, clip.value(QStringLiteral("sourceContentSha256")).toString());
    }
    resource.insert(QStringLiteral("currentHash"), settled_hash);
    values.replace(index, resource);
    const auto previous_document = document_;
    document_.insert(QStringLiteral("resources"), values);
    auto settlements = document_.value(QStringLiteral("derivedIntegritySettlements")).toArray();
    settlements.append(QJsonObject{
        {QStringLiteral("resourceId"), resource_id},
        {QStringLiteral("settlementKind"), QStringLiteral("unmutated-running-recovery")},
        {QStringLiteral("beforeHash"), before_hash},
        {QStringLiteral("previousCheckpointHash"), previous_checkpoint_hash},
        {QStringLiteral("settledHash"), settled_hash},
        {QStringLiteral("sourceSha256ByPath"), QJsonObject::fromVariantMap(source_sha256_by_path)},
        {QStringLiteral("settledAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    });
    document_.insert(QStringLiteral("derivedIntegritySettlements"), settlements);
    if (!save()) {
      document_ = previous_document;
      return failure(error_);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("changed"), true},
        {QStringLiteral("resourceId"), resource_id}, {QStringLiteral("beforeHash"), before_hash},
        {QStringLiteral("previousCheckpointHash"), previous_checkpoint_hash},
        {QStringLiteral("settledHash"), settled_hash},
        {QStringLiteral("sourceSha256ByPath"), source_sha256_by_path}};
  }

  [[nodiscard]] QVariantMap validateReleasedTimelineSourceIntegrityReady(
      const QString& resource_id, const QByteArray& current) const {
    if (document_.value(QStringLiteral("status")).toString() != QStringLiteral("released"))
      return {{QStringLiteral("ok"), true}, {QStringLiteral("ready"), true}};
    const auto scope = nativeTimelineScope();
    const auto targets = scope.value(QStringLiteral("targetIds")).toStringList();
    if (targets.size() != 1 || targets.front() != resource_id || current.isEmpty())
      return failure(QStringLiteral("The released task does not admit this Timeline resource for source readiness checks."));
    QJsonParseError parse;
    const auto state = QJsonDocument::fromJson(current, &parse);
    if (parse.error != QJsonParseError::NoError || !state.isObject())
      return failure(QStringLiteral("The released Timeline state cannot be checked for source readiness."));
    bool pending = false;
    QString error;
    if (!validate_admitted_source_readiness(state.object(),
            scope.value(QStringLiteral("assetPaths")).toStringList(), &pending, &error))
      return failure(error.isEmpty()
          ? QStringLiteral("The released Timeline source identity could not be verified; its checkpoint was preserved.")
          : error);
    if (pending) {
      return {{QStringLiteral("ok"), false}, {QStringLiteral("ready"), false},
          {QStringLiteral("retryable"), true}, {QStringLiteral("status"), QStringLiteral("source_integrity_pending")},
          {QStringLiteral("error"), QStringLiteral("Wait for admitted source size and full SHA-256 verification, then retry continuing the released task.")}};
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("ready"), true}};
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

  [[nodiscard]] QVariantMap recordOwnedOutputPath(const QString& path) {
    if (!active() || document_.value(QStringLiteral("status")) != QStringLiteral("running") || path.isEmpty())
      return failure(QStringLiteral("A running task is required to record an owned output path."));
    auto scope = document_.value(QStringLiteral("nativeTimelineScope")).toObject();
    if (!validNativeTimelineScope(scope))
      return failure(QStringLiteral("The native Timeline task scope is unavailable or corrupt."));
    const QFileInfo output(path);
    const auto output_parent = output.dir().canonicalPath();
    const auto canonical_path = output_parent.isEmpty() ? QString{} : QDir(output_parent).filePath(output.fileName());
    const auto output_directory = scope.value(QStringLiteral("outputDirectory")).toString();
    const auto relative = QDir(output_directory).relativeFilePath(canonical_path);
    if (!output.isAbsolute() || output.fileName().isEmpty() || path != canonical_path ||
        output_parent.isEmpty() || (relative.isEmpty() || relative == QStringLiteral(".") ||
        relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../")) ||
        QFileInfo(relative).isAbsolute()))
      return failure(QStringLiteral("A task output must be a canonical file path inside the admitted output directory."));
    auto output_paths = scope.value(QStringLiteral("outputPaths")).toArray();
    if (output_paths.contains(path)) return report();
    output_paths.append(path);
    scope.insert(QStringLiteral("outputPaths"), output_paths);
    sealNativeTimelineScope(&scope);
    const auto previous = document_;
    document_.insert(QStringLiteral("nativeTimelineScope"), scope);
    if (!save()) {
      document_ = previous;
      return failure(error_);
    }
    return report();
  }

  [[nodiscard]] QStringList nativeTimelineOutputPaths() const {
    QStringList result;
    const auto paths = nativeTimelineScope().value(QStringLiteral("outputPaths")).toList();
    for (const auto& value : paths) {
      const auto path = value.toString();
      if (!path.isEmpty() && !result.contains(path)) result.append(path);
    }
    return result;
  }

  [[nodiscard]] QVariantMap checkRevert(
      const QHash<QString, ResourceAdapter>& adapters,
      const QHash<QString, QString>& verified_derived_hashes = {}) const {
    if (!hasChanges()) return failure(QStringLiteral("There is no agent task to revert."));
    for (const auto& value : resources()) {
      const auto resource = value.toObject();
      const auto id = resource.value(QStringLiteral("id")).toString();
      const auto adapter = adapters.value(id);
      if (!adapter.read || !adapter.restore) return failure(QStringLiteral("The document is not available for task recovery."));
      const auto actual = hash(adapter.read());
      if (actual != resource.value(QStringLiteral("currentHash")).toString() &&
          actual != resource.value(QStringLiteral("beforeHash")).toString() &&
          actual != verified_derived_hashes.value(id))
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
  static bool is_lower_hex_sha256(const QString& value) {
    if (value.size() != 64) return false;
    for (const auto character : value) {
      if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9')) ||
            (character >= QLatin1Char('a') && character <= QLatin1Char('f')))) return false;
    }
    return true;
  }
  static QString sha256_file(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
      const auto chunk = file.read(1024 * 1024);
      if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return {};
      digest.addData(chunk);
    }
    return QString::fromLatin1(digest.result().toHex());
  }
  static QString source_identity_signature(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    constexpr qint64 sample_bytes = 64 * 1024;
    const auto size = file.size();
    QCryptographicHash digest(QCryptographicHash::Sha256);
    digest.addData(QByteArray::number(size));
    const auto first = file.read(sample_bytes);
    if (first.size() != qMin(size, sample_bytes)) return {};
    digest.addData(first);
    if (size > sample_bytes) {
      if (!file.seek(qMax<qint64>(0, size - sample_bytes))) return {};
      const auto last = file.read(sample_bytes);
      if (last.size() != sample_bytes) return {};
      digest.addData(last);
    }
    return QString::fromLatin1(digest.result().toHex());
  }
  static bool validate_source_identity_update(
      const QJsonObject& before, const QJsonObject& after,
      const QStringList& admitted_paths, QString* error) {
    constexpr double maximum_exact_json_integer = 9007199254740991.0;
    const auto path = before.value(QStringLiteral("filePath")).toString();
    const auto file = QFileInfo(path);
    const auto expected_signature = before.value(QStringLiteral("sourceIdentitySignature")).toString();
    const auto actual_signature = after.value(QStringLiteral("sourceIdentitySignature")).toString();
    const auto actual_size = file.size();
    const auto before_size_value = before.value(QStringLiteral("sourceFileSize")).toDouble(0.0);
    const auto after_size_value = after.value(QStringLiteral("sourceFileSize")).toDouble(-1.0);
    if (!admitted_paths.contains(path) || path.isEmpty() || !file.isFile() || !file.isReadable() ||
        file.canonicalFilePath() != path || expected_signature.isEmpty() ||
        expected_signature != actual_signature ||
        source_identity_signature(path) != expected_signature ||
        !std::isfinite(before_size_value) || !std::isfinite(after_size_value) ||
        after_size_value < 0.0 || std::floor(after_size_value) != after_size_value ||
        after_size_value > maximum_exact_json_integer ||
        static_cast<double>(actual_size) > maximum_exact_json_integer ||
        static_cast<qint64>(after_size_value) != actual_size ||
        (before_size_value > 0.0 && before_size_value != static_cast<double>(actual_size))) {
      if (error) *error = QStringLiteral("An admitted source path, size, or ingest identity changed after Timeline release; its checkpoint was preserved.");
      return false;
    }
    const auto source_sha256 = after.value(QStringLiteral("sourceContentSha256")).toString();
    if (!is_lower_hex_sha256(source_sha256) || sha256_file(path) != source_sha256) {
      if (error) *error = QStringLiteral("The settled source SHA-256 does not match the exact admitted file bytes; its checkpoint was preserved.");
      return false;
    }
    return true;
  }
  static bool validate_admitted_source_readiness(
      const QJsonValue& value, const QStringList& admitted_paths,
      bool* pending, QString* error, int depth = 0) {
    if (depth > 64) {
      if (error) *error = QStringLiteral("Released Timeline state exceeds the source-readiness depth limit.");
      return false;
    }
    if (value.isObject()) {
      const auto object = value.toObject();
      const auto path = object.value(QStringLiteral("filePath")).toString();
      const auto is_clip = object.contains(QStringLiteral("clipId")) &&
          object.value(QStringLiteral("filePath")).isString();
      if (is_clip && admitted_paths.contains(path)) {
        const auto source_sha = object.value(QStringLiteral("sourceContentSha256")).toString();
        const auto source_size = object.value(QStringLiteral("sourceFileSize"));
        const QFileInfo file(path);
        const auto expected_signature = object.value(QStringLiteral("sourceIdentitySignature")).toString();
        const auto actual_size = file.size();
        if (path.isEmpty() || !file.isFile() || !file.isReadable() || file.canonicalFilePath() != path ||
            expected_signature.isEmpty() || source_identity_signature(path) != expected_signature ||
            (source_size.isDouble() && source_size.toDouble() > 0.0 &&
                source_size.toDouble() != static_cast<double>(actual_size))) {
          if (error) *error = QStringLiteral("An admitted source path, size, or ingest identity changed before the released task could continue.");
          return false;
        }
        if (!is_lower_hex_sha256(source_sha) || !source_size.isDouble() || source_size.toDouble() <= 0.0) {
          if (pending) *pending = true;
        } else if (!validate_source_identity_update(object, object, admitted_paths, error)) {
          return false;
        }
      }
      for (auto it = object.begin(); it != object.end(); ++it)
        if (!validate_admitted_source_readiness(it.value(), admitted_paths, pending, error, depth + 1))
          return false;
    } else if (value.isArray()) {
      for (const auto& item : value.toArray())
        if (!validate_admitted_source_readiness(item, admitted_paths, pending, error, depth + 1))
          return false;
    }
    return true;
  }
  static bool only_admitted_source_integrity_changed(
      const QJsonObject& before, const QJsonObject& after,
      const QStringList& admitted_paths, QSet<QString>* changed_paths, QString* error) {
    std::function<bool(const QJsonValue&, const QJsonValue&, int)> compare;
    compare = [&](const QJsonValue& previous, const QJsonValue& current, int depth) -> bool {
      if (previous == current) return true;
      if (depth > 64) {
        if (error) *error = QStringLiteral("Released Timeline state exceeds the reconciliation depth limit.");
        return false;
      }
      if (previous.isArray() || current.isArray()) {
        if (!previous.isArray() || !current.isArray() || previous.toArray().size() != current.toArray().size())
          return false;
        const auto previous_array = previous.toArray();
        const auto current_array = current.toArray();
        for (qsizetype index = 0; index < previous_array.size(); ++index)
          if (!compare(previous_array.at(index), current_array.at(index), depth + 1)) return false;
        return true;
      }
      if (!previous.isObject() || !current.isObject()) return false;
      const auto previous_object = previous.toObject();
      const auto current_object = current.toObject();
      const auto previous_is_clip = previous_object.contains(QStringLiteral("clipId")) &&
          previous_object.value(QStringLiteral("filePath")).isString();
      const auto current_is_clip = current_object.contains(QStringLiteral("clipId")) &&
          current_object.value(QStringLiteral("filePath")).isString();
      const auto is_clip = previous_is_clip || current_is_clip;
      if (is_clip && (!previous_is_clip || !current_is_clip ||
          previous_object.value(QStringLiteral("clipId")) != current_object.value(QStringLiteral("clipId")) ||
          previous_object.value(QStringLiteral("filePath")) != current_object.value(QStringLiteral("filePath")))) {
        if (error) *error = QStringLiteral("A Timeline clip ID or source path changed after release; its checkpoint was preserved.");
        return false;
      }
      QSet<QString> keys;
      for (const auto& key : previous_object.keys()) keys.insert(key);
      for (const auto& key : current_object.keys()) keys.insert(key);
      auto clip_integrity_changed = false;
      for (const auto& key : keys) {
        const auto had_previous = previous_object.contains(key);
        const auto has_current = current_object.contains(key);
        const auto old_value = previous_object.value(key);
        const auto new_value = current_object.value(key);
        if (had_previous == has_current && old_value == new_value) continue;
        const auto is_integrity_field = is_clip &&
            (key == QStringLiteral("sourceContentSha256") || key == QStringLiteral("sourceFileSize"));
        if (!is_integrity_field) {
          if (!had_previous || !has_current || !compare(old_value, new_value, depth + 1)) return false;
          continue;
        }
        const auto path = previous_object.value(QStringLiteral("filePath")).toString();
        if (!admitted_paths.contains(path)) {
          if (error) *error = QStringLiteral("A source identity changed for a path not admitted to this Timeline task.");
          return false;
        }
        if (key == QStringLiteral("sourceContentSha256")) {
          const auto previous_sha = old_value.toString();
          const auto current_sha = new_value.toString();
          if (!had_previous || !has_current || !previous_sha.isEmpty() || !is_lower_hex_sha256(current_sha)) {
            if (error) *error = QStringLiteral("A source SHA-256 may settle only from empty to a full lowercase digest.");
            return false;
          }
        } else {
          constexpr double maximum_exact_json_integer = 9007199254740991.0;
          const auto previous_size = old_value.toDouble(-1.0);
          const auto current_size = new_value.toDouble(-1.0);
          if (!had_previous || !has_current || !old_value.isDouble() || !new_value.isDouble() ||
              previous_size != 0.0 || current_size <= 0.0 || !std::isfinite(current_size) ||
              std::floor(current_size) != current_size || current_size > maximum_exact_json_integer) {
            if (error) *error = QStringLiteral("A source file size may settle only from unknown zero to a positive integer.");
            return false;
          }
        }
        clip_integrity_changed = true;
      }
      if (clip_integrity_changed) {
        QString identity_error;
        if (!validate_source_identity_update(previous_object, current_object, admitted_paths, &identity_error)) {
          if (error) *error = identity_error;
          return false;
        }
        changed_paths->insert(previous_object.value(QStringLiteral("filePath")).toString());
      }
      return true;
    };
    if (!compare(QJsonValue(before), QJsonValue(after), 0)) {
      if (error && error->isEmpty())
        *error = QStringLiteral("The released Timeline contains an authored change outside source integrity fields; its checkpoint was preserved.");
      return false;
    }
    return true;
  }
  static QJsonObject find_clip_by_path(const QJsonObject& document, const QString& path) {
    std::function<QJsonObject(const QJsonValue&, int)> find;
    find = [&](const QJsonValue& value, int depth) -> QJsonObject {
      if (depth > 64) return {};
      if (value.isObject()) {
        const auto object = value.toObject();
        if (object.value(QStringLiteral("filePath")).toString() == path &&
            object.contains(QStringLiteral("clipId"))) return object;
        for (auto it = object.begin(); it != object.end(); ++it) {
          const auto result = find(it.value(), depth + 1);
          if (!result.isEmpty()) return result;
        }
      } else if (value.isArray()) {
        for (const auto& item : value.toArray()) {
          const auto result = find(item, depth + 1);
          if (!result.isEmpty()) return result;
        }
      }
      return {};
    };
    return find(QJsonValue(document), 0);
  }
  static bool validNativeTimelineScope(const QJsonObject& scope) {
    if (scope.isEmpty()) return false;
    auto unsigned_scope = scope;
    const auto expected = unsigned_scope.take(QStringLiteral("scopeHash")).toString();
    return !expected.isEmpty() && hash(QJsonDocument(unsigned_scope).toJson(QJsonDocument::Compact)) == expected;
  }
  static void sealNativeTimelineScope(QJsonObject* scope) {
    if (!scope) return;
    scope->remove(QStringLiteral("scopeHash"));
    scope->insert(QStringLiteral("scopeHash"), hash(QJsonDocument(*scope).toJson(QJsonDocument::Compact)));
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
  QString releasedSnapshotPath(const QString& id) const {
    return QDir(directory_).filePath(QStringLiteral("%1.released").arg(hash(id.toUtf8())));
  }
  QByteArray readBaseline(const QString& id) const {
    QFile file(baselinePath(id));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
  }
  QByteArray readReleasedSnapshot(const QString& id) const {
    QFile file(releasedSnapshotPath(id));
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

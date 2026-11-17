#pragma once

#include "agent_app_command_registry.h"
#include "agent_task_coordinator.h"
#include "timeline_compound_producer_identity.h"
#include "timeline_compound_render.h"
#include "timeline_document_controller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QPointer>
#include <QSet>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <functional>

namespace photo_editor::desktop {

// The MCP compound adapter owns one asynchronous compound step at a time. All
// paths come from the sealed task output directory; no caller path is accepted.
class AgentTimelineCompoundCommands final {
 public:
  using Authorizer = std::function<QVariantMap(const QString&)>;

  // A pending asynchronous compound can change only the parent's volatile
  // compoundStatus/compoundError after its accepted task checkpoint. This
  // read-only proof allows recovery to attach that exact pending operation
  // without updating any journal hash or settling it. The usual whole-state
  // checkpoint remains mandatory for every other recovery.
  static QVariantMap verifyPendingCompoundRecovery(
      const QVariantMap& preview,
      const QString& resource_id,
      const QByteArray& current_state) {
    const auto reject = [](const QString& message) {
      return QVariantMap{{QStringLiteral("ok"), false},
                         {QStringLiteral("error"), message}};
    };
    const auto canonical_uuid = [](const QString& value) {
      const QUuid parsed(value);
      return !parsed.isNull() &&
             parsed.toString(QUuid::WithoutBraces).toLower() == value;
    };
    const auto sha256 = [](const QByteArray& bytes) {
      return QString::fromLatin1(
          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    };
    const auto strict_strings = [](const QVariant& value) {
      QStringList result;
      if (value.metaType().id() == QMetaType::QStringList) return value.toStringList();
      if (value.metaType().id() != QMetaType::QVariantList) return result;
      const auto entries = value.toList();
      result.reserve(entries.size());
      for (const auto& entry : entries) {
        if (entry.metaType().id() != QMetaType::QString) return QStringList{};
        result.push_back(entry.toString());
      }
      return result;
    };

    if (resource_id.isEmpty() || current_state.isEmpty() ||
        preview.value(QStringLiteral("status")).toString() != QStringLiteral("running") ||
        preview.value(QStringLiteral("uncertainOperation")).toBool()) {
      return reject(QStringLiteral("Pending-compound recovery requires a known running task and readable Timeline."));
    }
    const auto scope = preview.value(QStringLiteral("nativeTimelineScope")).toMap();
    const auto targets = strict_strings(scope.value(QStringLiteral("targetIds")));
    const auto output_directory = scope.value(QStringLiteral("outputDirectory")).toString();
    const QFileInfo output_info(output_directory);
    if (targets != QStringList{resource_id} || output_directory.isEmpty() ||
        output_info.canonicalFilePath() != output_directory || !output_info.isDir()) {
      return reject(QStringLiteral("Pending-compound recovery scope does not match the exact Timeline and output directory."));
    }

    QVariantMap compound_step;
    auto accepted_step_count = 0;
    const auto steps = preview.value(QStringLiteral("steps")).toList();
    for (const auto& value : steps) {
      const auto step = value.toMap();
      const auto status = step.value(QStringLiteral("status")).toString();
      if (status == QStringLiteral("prepared")) {
        return reject(QStringLiteral("An uncertain prepared operation prevents compound-only recovery."));
      }
      if (status != QStringLiteral("accepted")) continue;
      ++accepted_step_count;
      if (step.value(QStringLiteral("command")).toString() !=
              QStringLiteral("timeline.action.createTimelineCompound") ||
          step.value(QStringLiteral("resourceId")).toString() != resource_id) {
        return reject(QStringLiteral("The accepted pending operation is not the exact compound operation for this Timeline."));
      }
      compound_step = step;
    }
    if (accepted_step_count != 1 || compound_step.isEmpty()) {
      return reject(QStringLiteral("Recovery requires exactly one accepted pending compound operation."));
    }

    const auto arguments = compound_step.value(QStringLiteral("arguments")).toMap();
    const auto outcome = compound_step.value(QStringLiteral("outcome")).toMap();
    const auto request_id = arguments.value(QStringLiteral("requestId")).toString();
    const auto child_id = arguments.value(QStringLiteral("childTimelineId")).toString();
    const auto compound_clip_id = arguments.value(QStringLiteral("compoundClipId")).toString();
    const auto projection_hash = arguments.value(
        QStringLiteral("compoundDocumentProjectionSha256")).toString();
    const auto launch_spec = arguments.value(QStringLiteral("producerLaunchSpec")).toMap();
    const auto requested_clip_ids = strict_strings(arguments.value(QStringLiteral("clipIds")));
    const auto request_label = arguments.value(QStringLiteral("label")).toString();
    QSet<QString> unique_requested_clip_ids;
    auto request_clip_ids_valid = requested_clip_ids.size() >= 2 &&
                                  requested_clip_ids.size() <= 256;
    for (const auto& clip_id : requested_clip_ids) {
      request_clip_ids_valid = request_clip_ids_valid && canonical_uuid(clip_id) &&
                               !unique_requested_clip_ids.contains(clip_id);
      unique_requested_clip_ids.insert(clip_id);
    }
    const QVariantMap public_request{
        {QStringLiteral("requestId"), request_id},
        {QStringLiteral("childTimelineId"), child_id},
        {QStringLiteral("label"), request_label},
        {QStringLiteral("clipIds"), requested_clip_ids}};
    const auto expected_request_digest = requestDigest(public_request);
    if (!canonical_uuid(request_id) || !canonical_uuid(child_id) ||
        !canonical_uuid(compound_clip_id) ||
        arguments.value(QStringLiteral("_batchId")).toString() != request_id ||
        !request_clip_ids_valid || request_label.trimmed().isEmpty() ||
        request_label.size() > 120 ||
        arguments.value(QStringLiteral("_batchRequestSha256")).toString().size() != 64 ||
        arguments.value(QStringLiteral("_batchRequestSha256")).toString() != expected_request_digest ||
        outcome.value(QStringLiteral("ok")).toBool() != true ||
        outcome.value(QStringLiteral("accepted")).toBool() != true ||
        outcome.value(QStringLiteral("changed")).toBool() != true ||
        outcome.value(QStringLiteral("requestId")).toString() != request_id ||
        outcome.value(QStringLiteral("compoundClipId")).toString() != compound_clip_id ||
        outcome.value(QStringLiteral("childTimelineId")).toString() != child_id ||
        outcome.value(QStringLiteral("compoundDocumentProjectionSha256")).toString() != projection_hash ||
        outcome.value(QStringLiteral("producerLaunchSpecSha256")).toString() !=
            launch_spec.value(QStringLiteral("launchSpecSha256")).toString() ||
        projection_hash.size() != 64) {
      return reject(QStringLiteral("The accepted compound receipt is missing matching operation, projection, or launch identities."));
    }

    const auto path_values = arguments.value(QStringLiteral("compoundPaths")).toMap();
    const Paths paths{
        path_values.value(QStringLiteral("documentPath")).toString(),
        path_values.value(QStringLiteral("renderPath")).toString(),
        path_values.value(QStringLiteral("partialPath")).toString(),
        path_values.value(QStringLiteral("filterGraphPath")).toString(),
        path_values.value(QStringLiteral("producerReceiptPath")).toString()};
    const auto expected_paths = pathsFor(output_directory, child_id);
    if (paths.all() != expected_paths.all() ||
        !validatePaths(paths, output_directory, child_id, false)) {
      return reject(QStringLiteral("The accepted compound outputs are not the exact canonical task-owned siblings."));
    }
    const auto sealed_paths = strict_strings(scope.value(QStringLiteral("outputPaths")));
    for (const auto& path : paths.all()) {
      if (!sealed_paths.contains(path)) {
        return reject(QStringLiteral("A compound output is not recorded in the sealed task output scope."));
      }
    }

    QString launch_error;
    if (launch_spec.value(QStringLiteral("operationId")).toString() != compound_clip_id ||
        launch_spec.value(QStringLiteral("childTimelineId")).toString() != child_id ||
        launch_spec.value(QStringLiteral("documentPath")).toString() != paths.document ||
        launch_spec.value(QStringLiteral("renderPath")).toString() != paths.render ||
        launch_spec.value(QStringLiteral("partialPath")).toString() != paths.partial ||
        launch_spec.value(QStringLiteral("filterGraphPath")).toString() != paths.graph ||
        launch_spec.value(QStringLiteral("producerReceiptPath")).toString() != paths.receipt ||
        !timeline_compound::producer::validate_launch_specification(launch_spec, &launch_error)) {
      return reject(launch_error.isEmpty()
          ? QStringLiteral("The accepted compound launch specification does not match its exact output paths.")
          : launch_error);
    }

    const auto receipt_from_step = outcome.value(QStringLiteral("producerReceipt")).toMap();
    QString receipt_error;
    const auto receipt_from_disk = timeline_compound::producer::read_receipt(paths.receipt, &receipt_error);
    if (receipt_from_step.isEmpty() || receipt_from_disk.isEmpty() ||
        outcome.value(QStringLiteral("producerReceiptPath")).toString() != paths.receipt ||
        QJsonDocument::fromVariant(receipt_from_step).toJson(QJsonDocument::Compact) !=
            QJsonDocument::fromVariant(receipt_from_disk).toJson(QJsonDocument::Compact) ||
        !receiptMatchesLaunchSpec(receipt_from_disk, launch_spec)) {
      return reject(receipt_error.isEmpty()
          ? QStringLiteral("The persisted producer receipt does not match the accepted task receipt and launch spec.")
          : receipt_error);
    }
    QString identity_error;
    const auto identity = timeline_compound::producer::inspect_receipt(
        receipt_from_disk, &identity_error);
    if (identity != timeline_compound::producer::IdentityState::Matching &&
        identity != timeline_compound::producer::IdentityState::NotRunning) {
      return reject(identity_error.isEmpty()
          ? QStringLiteral("The exact compound producer identity cannot be verified.")
          : identity_error);
    }

    const auto resources = preview.value(QStringLiteral("resources")).toList();
    const auto resource_entry = [&resources](const QString& id) {
      QVariantMap match;
      auto count = 0;
      for (const auto& value : resources) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("id")).toString() != id) continue;
        ++count;
        match = entry;
      }
      return count == 1 ? match : QVariantMap{};
    };
    const auto target_resource = resource_entry(resource_id);
    if (target_resource.isEmpty() || !target_resource.value(QStringLiteral("pending")).toBool()) {
      return reject(QStringLiteral("The exact Timeline resource does not have the accepted compound step pending."));
    }
    auto pending_resource_count = 0;
    for (const auto& value : resources) {
      if (value.toMap().value(QStringLiteral("pending")).toBool()) ++pending_resource_count;
    }
    if (pending_resource_count != 1) {
      return reject(QStringLiteral("A second pending resource prevents compound-only recovery."));
    }
    const auto absent_state_hash = sha256(QByteArrayLiteral("agent-task-owned-file-v1:absent"));
    const auto exact_file_checkpoint = [&](const QString& path, bool allow_postprocess_graph_removal) {
      const auto owned = AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(path);
      if (owned.resource_id.isEmpty() || !owned.adapter.read) return false;
      const auto entry = resource_entry(owned.resource_id);
      if (entry.isEmpty()) return false;
      const auto before_hash = entry.value(QStringLiteral("beforeHash")).toString();
      const auto current_hash = entry.value(QStringLiteral("currentHash")).toString();
      if (path == paths.render || path == paths.partial) {
        const QFileInfo info(path);
        const auto safe_file = !info.exists() ||
            (info.isFile() && !info.isSymLink() && info.canonicalFilePath() == path);
        return before_hash == absent_state_hash && current_hash == absent_state_hash && safe_file;
      }
      const auto fingerprint = owned.adapter.read();
      if (fingerprint.isEmpty()) return false;
      const auto actual_hash = sha256(fingerprint);
      if (allow_postprocess_graph_removal &&
          !QFileInfo(path).exists() && identity == timeline_compound::producer::IdentityState::NotRunning) {
        // The filtergraph may have been absent at the accepted checkpoint, or
        // it may have been checkpointed while FFmpeg was using it and removed
        // during successful post-processing. In both cases only the exact
        // absent fingerprint is admissible after the recorded producer is
        // proven stopped.
        return before_hash == absent_state_hash && actual_hash == absent_state_hash &&
               (current_hash == actual_hash || current_hash != absent_state_hash);
      }
      return current_hash == actual_hash;
    };
    if (!exact_file_checkpoint(paths.document, false) ||
        !exact_file_checkpoint(paths.graph, true) ||
        !exact_file_checkpoint(paths.receipt, false) ||
        !exact_file_checkpoint(paths.render, false) ||
        !exact_file_checkpoint(paths.partial, false)) {
      return reject(QStringLiteral("A task-owned compound file differs from its accepted checkpoint or allowed pending-writer state."));
    }
    if (QFileInfo(paths.render).exists() && QFileInfo(paths.partial).exists()) {
      return reject(QStringLiteral("Both final and partial compound outputs exist; recovery remains fail-closed."));
    }

    const auto compound_projection = compoundDocumentProjection(current_state, compound_clip_id);
    if (!projectionMatches(compound_projection, projection_hash)) {
      return reject(QStringLiteral("The Timeline differs from the accepted compound projection outside volatile status fields."));
    }
    QJsonParseError state_parse_error;
    const auto state_document = QJsonDocument::fromJson(current_state, &state_parse_error);
    if (state_parse_error.error != QJsonParseError::NoError || !state_document.isObject()) {
      return reject(QStringLiteral("The Timeline state for pending-compound recovery is invalid JSON."));
    }
    auto volatile_clip_found = false;
    auto volatile_clip_inconsistent = false;
    auto track_clip_count = 0;
    auto legacy_clip_count = 0;
    auto compound_clip_on_video_track = false;
    auto compound_clip_on_primary_video_track = false;
    QString volatile_status;
    QString volatile_error;
    const auto inspect_volatile_clip = [&](const QJsonObject& clip, const bool legacy_copy) {
      if (clip.value(QStringLiteral("clipId")).toString() != compound_clip_id) return;
      if (legacy_copy) ++legacy_clip_count;
      else ++track_clip_count;
      const auto status = clip.value(QStringLiteral("compoundStatus")).toString();
      const auto error = clip.value(QStringLiteral("compoundError")).toString();
      static const QSet<QString> allowed_statuses{
          QStringLiteral("generating"), QStringLiteral("ready"),
          QStringLiteral("pending"), QStringLiteral("failed"),
          QStringLiteral("interrupted"), QStringLiteral("cancelled")};
      if (!allowed_statuses.contains(status) || error.size() > 8192 ||
          ((status == QStringLiteral("generating") || status == QStringLiteral("ready") ||
            status == QStringLiteral("pending")) && !error.isEmpty())) {
        volatile_clip_inconsistent = true;
      }
      if (!volatile_clip_found) {
        volatile_status = status;
        volatile_error = error;
      } else if (volatile_status != status || volatile_error != error) {
        volatile_clip_inconsistent = true;
      }
      volatile_clip_found = true;
    };
    const auto root = state_document.object();
    for (const auto& track_value : root.value(QStringLiteral("tracks")).toArray()) {
      const auto track = track_value.toObject();
      for (const auto& clip_value : track.value(QStringLiteral("clips")).toArray()) {
        const auto clip = clip_value.toObject();
        if (clip.value(QStringLiteral("clipId")).toString() == compound_clip_id) {
          compound_clip_on_video_track =
              track.value(QStringLiteral("kind")).toString() == QStringLiteral("video");
          compound_clip_on_primary_video_track = compound_clip_on_video_track &&
              track.value(QStringLiteral("laneIndex")).toInt(-1) == 0;
        }
        inspect_volatile_clip(clip, false);
      }
    }
    for (const auto& clip_value : root.value(QStringLiteral("clips")).toArray())
      inspect_volatile_clip(clip_value.toObject(), true);
    // Native Timeline JSON keeps the primary (lane 0) video track mirrored in
    // the legacy top-level `clips` array. Secondary media lanes live only in
    // `tracks[]`. Require exactly one video-track copy, and require the legacy
    // mirror count to match that native topology. When both copies exist,
    // inspect_volatile_clip above requires their lifecycle fields to match.
    if (!volatile_clip_found || volatile_clip_inconsistent || track_clip_count != 1 ||
        !compound_clip_on_video_track ||
        legacy_clip_count != (compound_clip_on_primary_video_track ? 1 : 0)) {
      return reject(QStringLiteral("The compound clip's only volatile fields do not match an allowed native lifecycle state."));
    }
    if (volatile_status == QStringLiteral("ready")) {
      const QFileInfo render_info(paths.render);
      if (identity != timeline_compound::producer::IdentityState::NotRunning ||
          !render_info.isFile() || render_info.isSymLink() ||
          render_info.canonicalFilePath() != paths.render || render_info.size() <= 0 ||
          QFileInfo(paths.partial).exists() || QFileInfo(paths.partial).isSymLink()) {
        return reject(QStringLiteral("A ready compound requires a stopped exact producer and one readable final render output."));
      }
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("pendingCompoundRecoveryAllowed"), true},
            {QStringLiteral("requestId"), request_id},
            {QStringLiteral("compoundClipId"), compound_clip_id},
            {QStringLiteral("childTimelineId"), child_id},
            {QStringLiteral("compoundDocumentProjectionSha256"), projection_hash},
            {QStringLiteral("producerIdentity"),
             identity == timeline_compound::producer::IdentityState::Matching
                 ? QStringLiteral("matching") : QStringLiteral("not-running")},
            {QStringLiteral("taskJournalUnchanged"), true},
            {QStringLiteral("timelineCheckpointUnchanged"), false}};
  }

  AgentTimelineCompoundCommands(
      TimelineDocumentController* timeline,
      AgentTaskCoordinator* tasks,
      std::function<QString()> task_id,
      Authorizer authorize_edit)
      : timeline_(timeline), tasks_(tasks), task_id_(std::move(task_id)),
        authorize_edit_(std::move(authorize_edit)) {}

  bool registerCommands(AgentAppCommandRegistry& registry) {
    const auto uuid_schema = QVariantMap{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("pattern"),
         QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")}};
    const auto string_schema = [](int minimum, int maximum) {
      return QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                         {QStringLiteral("minLength"), minimum},
                         {QStringLiteral("maxLength"), maximum}};
    };
    const auto request_schema = QVariantMap{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QVariantMap{
            {QStringLiteral("requestId"), uuid_schema},
            {QStringLiteral("childTimelineId"), uuid_schema},
            {QStringLiteral("label"), string_schema(1, 120)},
            {QStringLiteral("clipIds"), QVariantMap{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("items"), uuid_schema},
                {QStringLiteral("minItems"), 2},
                {QStringLiteral("maxItems"), 256},
                {QStringLiteral("uniqueItems"), true}}}}},
        {QStringLiteral("required"), QStringList{
            QStringLiteral("requestId"), QStringLiteral("childTimelineId"),
            QStringLiteral("label"), QStringLiteral("clipIds")} },
        {QStringLiteral("additionalProperties"), false}};
    const auto request_id_schema = QVariantMap{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QVariantMap{
            {QStringLiteral("requestId"), uuid_schema}}},
        {QStringLiteral("required"), QStringList{QStringLiteral("requestId")}},
        {QStringLiteral("additionalProperties"), false}};

    auto add = [&](const QString& name, const QVariantMap& schema,
                   const bool requires_edit) {
      AgentAppCommandRegistry::Policy policy;
      policy.available = true;
      policy.resource = QStringLiteral("timeline-document");
      policy.completion = (name == QStringLiteral("createTimelineCompound") ||
                           name == QStringLiteral("startTimelineCompound"))
          ? QStringLiteral("asynchronous-terminal-receipt")
          : QStringLiteral("synchronous");
      policy.reversal = QStringLiteral("exact-task-baseline-and-owned-output-paths");
      policy.reason = QStringLiteral("A ready, exact-target native Timeline task is required.");
      policy.authorize = [this, requires_edit, name]() {
        const auto scope = taskScope(requires_edit ? QStringLiteral("running") : QString{},
                                     requires_edit);
        if (!scope.value(QStringLiteral("ok")).toBool()) return scope;
        if (requires_edit) {
          if (!authorize_edit_)
            return failure(QStringLiteral("Native Timeline edit authorization is unavailable."));
          const auto permission = authorize_edit_(name);
          if (!permission.value(QStringLiteral("ok")).toBool()) return permission;
        }
        return QVariantMap{{QStringLiteral("ok"), true}};
      };
      const auto mutation_owner = [this, requires_edit](
          const QString&, const QVariantMap&, const AgentAppCommandRegistry::Invocation& invoke) {
        const auto scope = taskScope(requires_edit ? QStringLiteral("running") : QString{},
                                     requires_edit);
        if (!scope.value(QStringLiteral("ok")).toBool()) return scope;
        return invoke();
      };
      const auto handler = [this, name](const QVariantMap& values) {
        if (name == QStringLiteral("createTimelineCompound")) return create(values);
        if (name == QStringLiteral("startTimelineCompound")) return start(values);
        if (name == QStringLiteral("inspectTimelineCompound")) return inspect(values);
        if (name == QStringLiteral("cancelTimelineCompound")) return cancel(values);
        if (name == QStringLiteral("settleTimelineCompound")) return settle(values);
        if (name == QStringLiteral("reconcileTimelineCompound")) return reconcile(values);
        return failure(QStringLiteral("Unknown task-owned compound operation."));
      };
      return registry.registerAction(QStringLiteral("timeline"), name, timeline_,
                                     schema, policy, mutation_owner, handler);
    };

    const auto create_ok = add(QStringLiteral("createTimelineCompound"), request_schema, true);
    const auto start_ok = add(QStringLiteral("startTimelineCompound"), request_id_schema, true);
    const auto inspect_ok = add(QStringLiteral("inspectTimelineCompound"), request_id_schema, false);
    const auto cancel_ok = add(QStringLiteral("cancelTimelineCompound"), request_id_schema, false);
    const auto settle_ok = add(QStringLiteral("settleTimelineCompound"), request_id_schema, false);
    const auto reconcile_ok = add(QStringLiteral("reconcileTimelineCompound"), request_id_schema, false);
    return create_ok && start_ok && inspect_ok && cancel_ok && settle_ok && reconcile_ok;
  }

  // Called before the ordinary task journal revert preflight. It settles only
  // compound steps whose exact producer is proven stopped/absent and whose
  // non-transient Timeline projection still matches the persisted checkpoint.
  QVariantMap reconcileBeforeRevert() {
    if (!timeline_ || !tasks_ || !tasks_->journal()->active())
      return failure(QStringLiteral("The owned Timeline task is unavailable for compound reconciliation."));
    const auto steps = QJsonArray::fromVariantList(tasks_->journal()->report().value(QStringLiteral("steps")).toList());
    for (const auto& value : steps) {
      const auto step = value.toObject();
      if (step.value(QStringLiteral("command")).toString() !=
          QStringLiteral("timeline.action.createTimelineCompound")) continue;
      const auto status = step.value(QStringLiteral("status")).toString();
      if (status != QStringLiteral("prepared") && status != QStringLiteral("accepted")) continue;
      const auto arguments = step.value(QStringLiteral("arguments")).toObject().toVariantMap();
      const auto request_id = arguments.value(QStringLiteral("requestId")).toString();
      const auto result = reconcileStep(request_id, true);
      if (!result.value(QStringLiteral("ok")).toBool() &&
          !result.value(QStringLiteral("settled")).toBool()) return result;
    }
    // A terminal status can change between the initial observation and the
    // settlement read. Never let the actual revert path proceed merely
    // because reconciliation returned an informational pending receipt.
    if (hasUnsettledCompounds()) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("pending"), true},
              {QStringLiteral("retryable"), true},
              {QStringLiteral("compoundReconciliationRequired"), true},
              {QStringLiteral("error"), QStringLiteral("A compound producer is still unresolved; the task baseline and owned outputs were preserved.")}};
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("reconciled"), true}};
  }

  [[nodiscard]] bool hasUnsettledCompounds() const {
    if (!tasks_) return false;
    for (const auto& value : QJsonArray::fromVariantList(tasks_->journal()->report().value(QStringLiteral("steps")).toList())) {
      const auto step = value.toObject();
      if (step.value(QStringLiteral("command")).toString() !=
          QStringLiteral("timeline.action.createTimelineCompound")) continue;
      const auto status = step.value(QStringLiteral("status")).toString();
      if (status == QStringLiteral("prepared") || status == QStringLiteral("accepted"))
        return true;
    }
    return false;
  }

  QVariantMap checkBeforeRevert() const {
    if (hasUnsettledCompounds()) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("pending"), true},
              {QStringLiteral("retryable"), true},
              {QStringLiteral("compoundReconciliationRequired"), true},
              {QStringLiteral("error"), QStringLiteral("A task-owned compound must be stopped and reconciled before revert preflight.")}};
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("pending"), false}};
  }

 private:
  struct Paths final {
    QString document;
    QString render;
    QString partial;
    QString graph;
    QString receipt;
    QStringList all() const { return {document, render, partial, graph, receipt}; }
  };

  struct Step final {
    qsizetype id = -1;
    QString status;
    QString resource_id;
    QVariantMap arguments;
    QVariantMap outcome;
  };

  static QVariantMap failure(const QString& message) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), message}};
  }

  static QStringList strictStringList(const QVariant& value) {
    if (value.metaType().id() == QMetaType::QStringList) return value.toStringList();
    if (value.metaType().id() != QMetaType::QVariantList) return {};
    const auto items = value.toList();
    QStringList result;
    result.reserve(items.size());
    for (const auto& item : items) {
      if (item.metaType().id() != QMetaType::QString) return {};
      result.push_back(item.toString());
    }
    return result;
  }

  static QVariantMap preflightFailure(const QString& message) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), message},
            {QStringLiteral("errorCode"), QStringLiteral("command_admission_rejected")},
            {QStringLiteral("recoverableError"), true},
            {QStringLiteral("mutationNotApplied"), true}};
  }

  static bool canonicalUuid(const QString& value) {
    const QUuid uuid(value);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces).toLower() == value;
  }

  static QString requestDigest(const QVariantMap& values) {
    const auto bytes = QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  }

  QVariantMap taskScope(const QString& required_status = {},
                        const bool require_document_ready = true) const {
    if (!timeline_ || !tasks_ || !task_id_) {
      return failure(QStringLiteral("The native Timeline task owner is unavailable."));
    }
    const auto id = task_id_();
    const auto journal = tasks_->journal();
    const auto report = journal->report();
    const auto status = report.value(QStringLiteral("status")).toString();
    if (id.isEmpty() || journal->taskId() != id ||
        (required_status.isEmpty()
             ? !QStringList{QStringLiteral("running"), QStringLiteral("interrupted")}.contains(status)
             : status != required_status)) {
      return failure(QStringLiteral("The exact native Timeline task is not active in the required state."));
    }
    const auto resource_id = timeline_->agentTaskDocumentId();
    const auto scope = journal->nativeTimelineScope();
    if (resource_id.isEmpty() ||
        scope.value(QStringLiteral("targetIds")).toStringList() != QStringList{resource_id} ||
        !timeline_->hasActiveTimeline()) {
      return failure(QStringLiteral("The active Timeline does not match the task's sealed target."));
    }
    if (require_document_ready && !timeline_->agentTaskDocumentReady()) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("retryable"), true},
              {QStringLiteral("error"), QStringLiteral("The task-owned Timeline is still settling native work.")}};
    }
    const auto output_directory = scope.value(QStringLiteral("outputDirectory")).toString();
    if (output_directory.isEmpty() ||
        QFileInfo(output_directory).canonicalFilePath() != output_directory ||
        !QFileInfo(output_directory).isDir()) {
      return failure(QStringLiteral("The sealed task output directory is no longer available."));
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("taskId"), id},
            {QStringLiteral("resourceId"), resource_id},
            {QStringLiteral("outputDirectory"), output_directory},
            {QStringLiteral("taskStatus"), status}};
  }

  AgentTaskJournal::ResourceAdapter timelineAdapter(const QString& resource_id) const {
    const auto owner = timeline_;
    return {
        [owner, resource_id]() {
          return owner && owner->agentTaskDocumentId() == resource_id
                     ? owner->agentTaskDocumentState()
                     : QByteArray{};
        },
        [owner, resource_id](const QByteArray& bytes) {
          return owner && owner->agentTaskDocumentId() == resource_id &&
                 owner->restoreAgentTaskDocument(bytes);
        },
        {},
    };
  }

  QVariantMap ensureTimelineEnrolled(const QString& resource_id,
                                    bool require_current_checkpoint) {
    const auto enrolled = tasks_->enroll(resource_id, timelineAdapter(resource_id));
    if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
    const auto current = timeline_->agentTaskDocumentState();
    if (current.isEmpty()) return failure(QStringLiteral("The active Timeline state could not be read."));
    if (require_current_checkpoint &&
        (tasks_->journal()->pending(resource_id) ||
         !tasks_->journal()->matchesCheckpoint(resource_id, current))) {
      return failure(QStringLiteral("The Timeline changed outside its last settled task step."));
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("state"), current}};
  }

  static Paths pathsFor(const QString& output_directory,
                        const QString& child_timeline_id) {
    Paths result;
    const auto base = QStringLiteral("compound-%1").arg(child_timeline_id);
    const QDir directory(output_directory);
    result.document = directory.filePath(base + QStringLiteral(".json"));
    result.render = directory.filePath(base + QStringLiteral(".mov"));
    result.partial = directory.filePath(base + QStringLiteral(".partial.mov"));
    result.graph = directory.filePath(base + QStringLiteral(".ffgraph"));
    result.receipt = directory.filePath(base + QStringLiteral(".producer.json"));
    return result;
  }

  static bool exactPath(const QString& path, const QString& directory,
                        const QString& expected_name, bool require_absent) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isAbsolute() || info.fileName() != expected_name ||
        info.dir().canonicalPath() != directory ||
        QDir(directory).filePath(info.fileName()) != path || info.isSymLink()) return false;
    if (require_absent) return !info.exists();
    return !info.exists() || (info.isFile() && info.canonicalFilePath() == path);
  }

  static bool validatePaths(const Paths& paths, const QString& directory,
                            const QString& child_id, bool require_absent) {
    const auto base = QStringLiteral("compound-%1").arg(child_id);
    return exactPath(paths.document, directory, base + QStringLiteral(".json"), require_absent) &&
        exactPath(paths.render, directory, base + QStringLiteral(".mov"), require_absent) &&
        exactPath(paths.partial, directory, base + QStringLiteral(".partial.mov"), require_absent) &&
        exactPath(paths.graph, directory, base + QStringLiteral(".ffgraph"), require_absent) &&
        exactPath(paths.receipt, directory, base + QStringLiteral(".producer.json"), require_absent);
  }

  QVariantMap enrollOutputPaths(const Paths& paths, bool require_absent) {
    const auto scope = tasks_->journal()->nativeTimelineScope();
    const auto directory = scope.value(QStringLiteral("outputDirectory")).toString();
    if (!validatePaths(paths, directory,
                       QFileInfo(paths.document).completeBaseName().mid(
                           QStringLiteral("compound-").size()), require_absent)) {
      return failure(QStringLiteral("The five compound files are not exact canonical siblings in the admitted task output directory."));
    }
    const auto already_owned = tasks_->journal()->nativeTimelineOutputPaths();
    QVector<AgentTaskCoordinator::OwnedFileResource> resources;
    resources.reserve(paths.all().size());
    for (const auto& path : paths.all()) {
      if (require_absent && already_owned.contains(path)) {
        return failure(QStringLiteral("A compound output path is already owned by another task operation."));
      }
      const auto file = AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(path);
      if (file.resource_id.isEmpty() || file.canonical_path != path ||
          file.absent_state != QByteArrayLiteral("agent-task-owned-file-v1:absent")) {
        return failure(QStringLiteral("An exact task-owned file adapter could not be created for a compound sibling."));
      }
      if (require_absent && file.adapter.read() != file.absent_state) {
        return failure(QStringLiteral("A compound output already exists; it will not be overwritten."));
      }
      resources.append(file);
    }
    // Verify every absent baseline before enrolling the first output. This is
    // still before document creation or process launch; partial enrollment
    // cannot authorize a partially checked output set.
    for (const auto& file : resources) {
      const auto enrolled = tasks_->enroll(file.resource_id, file.adapter);
      if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
      const auto recorded = tasks_->journal()->recordOwnedOutputPath(file.canonical_path);
      if (!recorded.value(QStringLiteral("ok")).toBool()) return recorded;
    }
    return {{QStringLiteral("ok"), true}};
  }

  QVariantMap checkpointOutputPaths(const Paths& paths) {
    for (const auto& path : paths.all()) {
      const auto file = AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(path);
      if (file.resource_id.isEmpty() || file.canonical_path != path)
        return failure(QStringLiteral("A task-owned compound output adapter is unavailable."));
      const auto enrolled = tasks_->enroll(file.resource_id, file.adapter);
      if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
      const auto checkpoint = tasks_->checkpointOwnedResource(file.resource_id);
      if (!checkpoint.value(QStringLiteral("ok")).toBool()) return checkpoint;
    }
    return {{QStringLiteral("ok"), true}};
  }

  static bool recordCompoundClip(QJsonObject* clip, const QString& clip_id) {
    if (clip == nullptr || clip->value(QStringLiteral("clipId")).toString() != clip_id)
      return false;
    clip->remove(QStringLiteral("compoundStatus"));
    clip->remove(QStringLiteral("compoundError"));
    return true;
  }

  static QByteArray compoundDocumentProjection(const QByteArray& state,
                                               const QString& clip_id) {
    QJsonParseError parse;
    auto document = QJsonDocument::fromJson(state, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return {};
    auto root = document.object();
    auto found = false;
    auto tracks = root.value(QStringLiteral("tracks")).toArray();
    for (qsizetype track_index = 0; track_index < tracks.size(); ++track_index) {
      auto track = tracks.at(track_index).toObject();
      auto clips = track.value(QStringLiteral("clips")).toArray();
      for (qsizetype clip_index = 0; clip_index < clips.size(); ++clip_index) {
        auto clip = clips.at(clip_index).toObject();
        if (clip.value(QStringLiteral("clipId")).toString() != clip_id) continue;
        found = recordCompoundClip(&clip, clip_id) || found;
        clips.replace(clip_index, clip);
      }
      track.insert(QStringLiteral("clips"), clips);
      tracks.replace(track_index, track);
    }
    root.insert(QStringLiteral("tracks"), tracks);
    auto legacy = root.value(QStringLiteral("clips")).toArray();
    for (qsizetype clip_index = 0; clip_index < legacy.size(); ++clip_index) {
      auto clip = legacy.at(clip_index).toObject();
      if (clip.value(QStringLiteral("clipId")).toString() != clip_id) continue;
      found = recordCompoundClip(&clip, clip_id) || found;
      legacy.replace(clip_index, clip);
    }
    root.insert(QStringLiteral("clips"), legacy);
    if (!found) return {};
    return QCryptographicHash::hash(
        QJsonDocument(root).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex();
  }

  QByteArray currentCompoundProjection(const QString& clip_id) const {
    return timeline_ == nullptr
               ? QByteArray{}
               : compoundDocumentProjection(timeline_->agentTaskDocumentState(), clip_id);
  }

  static bool projectionMatches(const QByteArray& actual_hex,
                                const QString& expected_hex) {
    const auto encoded = expected_hex.toLatin1();
    return encoded.size() == 64 &&
           QByteArray::fromHex(encoded).size() == 32 &&
           QByteArray::fromHex(encoded).toHex() == encoded &&
           actual_hex == encoded;
  }

  static bool receiptMatchesLaunchSpec(const QVariantMap& receipt,
                                       const QVariantMap& launch_spec) {
    if (receipt.isEmpty() || launch_spec.isEmpty() ||
        launch_spec.value(QStringLiteral("schema")).toString() !=
            QStringLiteral("looksy.timeline.compound-launch/v1") ||
        receipt.value(QStringLiteral("schema")).toString() !=
            QStringLiteral("looksy.timeline.compound-producer/v1")) return false;
    for (const auto& key : {QStringLiteral("operationId"),
                            QStringLiteral("childTimelineId"),
                            QStringLiteral("executablePath"),
                            QStringLiteral("documentPath"),
                            QStringLiteral("partialPath"),
                            QStringLiteral("renderPath"),
                            QStringLiteral("filterGraphPath"),
                            QStringLiteral("producerReceiptPath"),
                            QStringLiteral("bootSessionId")}) {
      if (receipt.value(key) != launch_spec.value(key)) return false;
    }
    return strictStringList(receipt.value(QStringLiteral("argv"))) ==
               strictStringList(launch_spec.value(QStringLiteral("argv"))) &&
           receipt.value(QStringLiteral("uid")).toUInt() ==
               launch_spec.value(QStringLiteral("uid")).toUInt();
  }

  QVariantMap findStep(const QString& request_id) const {
    if (request_id.isEmpty() || !tasks_) return failure(QStringLiteral("A compound request ID is required."));
    const auto resource_id = timeline_ ? timeline_->agentTaskDocumentId() : QString{};
    const auto rows = QJsonArray::fromVariantList(tasks_->journal()->report().value(QStringLiteral("steps")).toList());
    auto found = false;
    QJsonObject match;
    for (const auto& value : rows) {
      const auto row = value.toObject();
      const auto arguments = row.value(QStringLiteral("arguments")).toObject();
      if (row.value(QStringLiteral("command")).toString() !=
              QStringLiteral("timeline.action.createTimelineCompound") ||
          arguments.value(QStringLiteral("requestId")).toString() != request_id) continue;
      if (found) return failure(QStringLiteral("The request ID appears more than once in the task journal."));
      found = true;
      match = row;
    }
    if (!found || match.value(QStringLiteral("resourceId")).toString() != resource_id)
      return failure(QStringLiteral("The request ID does not name a compound operation for this exact Timeline."));
    const auto arguments = match.value(QStringLiteral("arguments")).toObject();
    const auto outcome = match.value(QStringLiteral("outcome")).toObject();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("stepId"), match.value(QStringLiteral("id")).toVariant()},
            {QStringLiteral("resourceId"), resource_id},
            {QStringLiteral("stepStatus"), match.value(QStringLiteral("status")).toString()},
            {QStringLiteral("arguments"), arguments.toVariantMap()},
            {QStringLiteral("outcome"), outcome.toVariantMap()}};
  }

  QVariantMap pathsFromStep(const QVariantMap& step) const {
    const auto args = step.value(QStringLiteral("arguments")).toMap();
    const auto child_id = args.value(QStringLiteral("childTimelineId")).toString();
    if (!canonicalUuid(child_id)) return failure(QStringLiteral("The journaled child Timeline ID is invalid."));
    const auto directory = tasks_->journal()->nativeTimelineScope()
                               .value(QStringLiteral("outputDirectory")).toString();
    const auto paths = pathsFor(directory, child_id);
    const auto persisted = args.value(QStringLiteral("compoundPaths")).toMap();
    if (!persisted.isEmpty() &&
        (persisted.value(QStringLiteral("documentPath")).toString() != paths.document ||
         persisted.value(QStringLiteral("renderPath")).toString() != paths.render ||
         persisted.value(QStringLiteral("partialPath")).toString() != paths.partial ||
         persisted.value(QStringLiteral("filterGraphPath")).toString() != paths.graph ||
         persisted.value(QStringLiteral("producerReceiptPath")).toString() != paths.receipt)) {
      return failure(QStringLiteral("The journaled compound path set does not match its sealed output scope."));
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("documentPath"), paths.document},
            {QStringLiteral("renderPath"), paths.render},
            {QStringLiteral("partialPath"), paths.partial},
            {QStringLiteral("filterGraphPath"), paths.graph},
            {QStringLiteral("producerReceiptPath"), paths.receipt}};
  }

  static Paths unpackPaths(const QVariantMap& value) {
    return {value.value(QStringLiteral("documentPath")).toString(),
            value.value(QStringLiteral("renderPath")).toString(),
            value.value(QStringLiteral("partialPath")).toString(),
            value.value(QStringLiteral("filterGraphPath")).toString(),
            value.value(QStringLiteral("producerReceiptPath")).toString()};
  }

  QVariantMap create(const QVariantMap& values) {
    const auto scope = taskScope(QStringLiteral("running"));
    if (!scope.value(QStringLiteral("ok")).toBool()) return scope;
    const auto request_id = values.value(QStringLiteral("requestId")).toString();
    const auto child_id = values.value(QStringLiteral("childTimelineId")).toString();
    const auto label = values.value(QStringLiteral("label")).toString().simplified();
    QStringList clip_ids = values.value(QStringLiteral("clipIds")).toStringList();
    if (clip_ids.isEmpty()) {
      for (const auto& value : values.value(QStringLiteral("clipIds")).toList())
        clip_ids.append(value.toString());
    }
    if (!canonicalUuid(request_id) || !canonicalUuid(child_id) || label.isEmpty() ||
        label.size() > 120 || clip_ids.size() < 2 || clip_ids.size() > 256) {
      return preflightFailure(QStringLiteral("Supply canonical request/child UUIDs, a short label, and 2 to 256 stable clip IDs."));
    }
    const auto digest = requestDigest(values);
    const auto lookup = tasks_->journal()->lookupIdempotentBatchStep(request_id);
    if (!lookup.value(QStringLiteral("ok")).toBool()) return lookup;
    if (lookup.value(QStringLiteral("found")).toBool()) {
      if (lookup.value(QStringLiteral("command")).toString() !=
              QStringLiteral("timeline.action.createTimelineCompound") ||
          lookup.value(QStringLiteral("resourceId")).toString() !=
              scope.value(QStringLiteral("resourceId")).toString()) {
        return preflightFailure(QStringLiteral("This request ID already belongs to another task operation."));
      }
      if (lookup.value(QStringLiteral("requestSha256")).toString() != digest) {
        return preflightFailure(QStringLiteral("This request ID was already used with a different compound request."));
      }
      auto prior = lookup.value(QStringLiteral("outcome")).toMap();
      const auto step_status = lookup.value(QStringLiteral("stepStatus")).toString();
      if (step_status == QStringLiteral("prepared") || step_status == QStringLiteral("accepted")) {
        prior.insert(QStringLiteral("ok"), true);
        prior.insert(QStringLiteral("requestId"), request_id);
        prior.insert(QStringLiteral("stepStatus"), step_status);
        prior.insert(QStringLiteral("accepted"), step_status == QStringLiteral("accepted"));
        prior.insert(QStringLiteral("prepared"), step_status == QStringLiteral("prepared"));
        prior.insert(QStringLiteral("idempotentReplay"), true);
        return prior;
      }
      if (step_status != QStringLiteral("completed") && step_status != QStringLiteral("failed"))
        return failure(QStringLiteral("The compound request has an unresolved outcome; reconcile it instead of replaying it."));
      prior.insert(QStringLiteral("idempotentReplay"), true);
      prior.insert(QStringLiteral("requestId"), request_id);
      return prior;
    }

    const auto resource_id = scope.value(QStringLiteral("resourceId")).toString();
    const auto baseline = ensureTimelineEnrolled(resource_id, true);
    if (!baseline.value(QStringLiteral("ok")).toBool()) return baseline;
    const auto paths = pathsFor(scope.value(QStringLiteral("outputDirectory")).toString(), child_id);
    if (!validatePaths(paths, scope.value(QStringLiteral("outputDirectory")).toString(),
                       child_id, true)) {
      return preflightFailure(QStringLiteral("The five compound output paths must be new canonical siblings under the admitted task directory."));
    }
    const auto already_owned = tasks_->journal()->nativeTimelineOutputPaths();
    for (const auto& path : paths.all()) {
      if (already_owned.contains(path))
        return preflightFailure(QStringLiteral("A compound output path is already owned by another task operation."));
    }
    const auto files_ready = enrollOutputPaths(paths, true);
    if (!files_ready.value(QStringLiteral("ok")).toBool()) return files_ready;

    QVariantMap arguments = values;
    arguments.insert(QStringLiteral("_batchId"), request_id);
    arguments.insert(QStringLiteral("_batchRequestSha256"), digest);
    arguments.insert(QStringLiteral("compoundPaths"), QVariantMap{
        {QStringLiteral("documentPath"), paths.document},
        {QStringLiteral("renderPath"), paths.render},
        {QStringLiteral("partialPath"), paths.partial},
        {QStringLiteral("filterGraphPath"), paths.graph},
        {QStringLiteral("producerReceiptPath"), paths.receipt}});
    const auto prepared = tasks_->journal()->prepare(
        resource_id, QStringLiteral("timeline.action.createTimelineCompound"),
        arguments, baseline.value(QStringLiteral("state")).toByteArray());
    if (!prepared.value(QStringLiteral("ok")).toBool()) return prepared;
    const auto step_id = prepared.value(QStringLiteral("stepId")).toLongLong();
    const auto created = timeline_->prepareCompoundClipForAutomation(
        clip_ids, label, child_id, paths.document, paths.render);
    if (!created.value(QStringLiteral("ok")).toBool()) {
      return resolvePreparedFailure(step_id, resource_id, request_id, paths,
          created.value(QStringLiteral("error")).toString(), true);
    }
    const auto compound_clip_id = created.value(QStringLiteral("createdClipId")).toString();
    const auto staged = timeline_->prepareCompoundRenderForAutomation(compound_clip_id);
    if (!staged.value(QStringLiteral("ok")).toBool()) {
      return resolvePreparedFailure(step_id, resource_id, request_id, paths,
          staged.value(QStringLiteral("error")).toString(), false);
    }
    const auto projection = currentCompoundProjection(compound_clip_id);
    if (projection.isEmpty()) {
      return resolvePreparedFailure(step_id, resource_id, request_id, paths,
          QStringLiteral("The prepared compound's editable Timeline projection could not be verified."), false);
    }
    const auto recorded_projection = tasks_->journal()->recordPreparedCompoundCheckpoint(
        step_id, resource_id, compound_clip_id, QString::fromLatin1(projection));
    if (!recorded_projection.value(QStringLiteral("ok")).toBool()) {
      return resolvePreparedFailure(step_id, resource_id, request_id, paths,
          recorded_projection.value(QStringLiteral("error")).toString(), false);
    }
    const auto launch_spec = staged.value(QStringLiteral("producerLaunchSpec")).toMap();
    const auto recorded_launch = tasks_->journal()->recordPreparedProducerLaunchSpec(
        step_id, resource_id, launch_spec);
    if (!recorded_launch.value(QStringLiteral("ok")).toBool()) {
      return resolvePreparedFailure(step_id, resource_id, request_id, paths,
          recorded_launch.value(QStringLiteral("error")).toString(), false);
    }
    const auto checkpointed = checkpointOutputPaths(paths);
    if (!checkpointed.value(QStringLiteral("ok")).toBool()) {
      auto result = checkpointed;
      result.insert(QStringLiteral("requestId"), request_id);
      result.insert(QStringLiteral("compoundClipId"), compound_clip_id);
      result.insert(QStringLiteral("prepared"), true);
      result.insert(QStringLiteral("recoveryRequired"), true);
      return result;
    }
    const auto updated = findStep(request_id);
    const auto enriched_arguments = updated.value(QStringLiteral("arguments")).toMap();
    auto result = startPrepared(step_id, resource_id, request_id, paths,
                                enriched_arguments, launch_spec, true);
    result.insert(QStringLiteral("childTimelineId"), child_id);
    result.insert(QStringLiteral("childDocumentPath"), paths.document);
    result.insert(QStringLiteral("renderPath"), paths.render);
    result.insert(QStringLiteral("outputPaths"), paths.all());
    result.insert(QStringLiteral("clip"), created.value(QStringLiteral("clip")));
    return result;
  }

  QVariantMap resolvePreparedFailure(qsizetype step_id,
                                     const QString& resource_id,
                                     const QString& request_id,
                                     const Paths& paths,
                                     const QString& message,
                                     bool known_no_mutation) {
    const auto checkpointed = checkpointOutputPaths(paths);
    if (!checkpointed.value(QStringLiteral("ok")).toBool()) {
      auto result = checkpointed;
      result.insert(QStringLiteral("recoveryRequired"), true);
      return result;
    }
    const auto current = timeline_->agentTaskDocumentState();
    if (current.isEmpty()) return failure(QStringLiteral("The Timeline state could not be read after compound preparation."));
    const auto unchanged = tasks_->journal()->matchesCheckpoint(resource_id, current);
    // A prepared failure can be settled as no-mutation only when both the
    // controller result and the task document bytes confirm that fact.
    const auto no_mutation = known_no_mutation && unchanged &&
        QFileInfo(paths.document).exists() == false &&
        QFileInfo(paths.graph).exists() == false &&
        QFileInfo(paths.partial).exists() == false &&
        QFileInfo(paths.render).exists() == false &&
        QFileInfo(paths.receipt).exists() == false;
    const auto receipt = QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), message},
        {QStringLiteral("changed"), !no_mutation},
        {QStringLiteral("mutationNotApplied"), no_mutation},
        {QStringLiteral("recoveryRequired"), !no_mutation}};
    const auto resolved = tasks_->journal()->resolvePreparedStep(
        step_id, resource_id, current, receipt);
    if (!resolved.value(QStringLiteral("ok")).toBool()) {
      auto result = resolved;
      result.insert(QStringLiteral("recoveryRequired"), true);
      return result;
    }
    auto result = receipt;
    result.insert(QStringLiteral("requestId"), request_id);
    result.insert(QStringLiteral("stepStatus"), QStringLiteral("failed"));
    result.insert(QStringLiteral("taskStateHash"), tasks_->journal()->checkpointHash(resource_id));
    return result;
  }

  QVariantMap findChildClipId(const QString& document_path) const {
    if (!timeline_) return failure(QStringLiteral("The Timeline is unavailable."));
    QStringList matches;
    for (const auto& value : timeline_->clips()) {
      const auto clip = value.toMap();
      if (clip.value(QStringLiteral("compoundDocumentPath")).toString() == document_path)
        matches.append(clip.value(QStringLiteral("clipId")).toString());
    }
    if (matches.size() != 1 || !canonicalUuid(matches.first()))
      return failure(QStringLiteral("The task-owned child path does not identify exactly one parent compound clip."));
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("compoundClipId"), matches.first()}};
  }

  QVariantMap acceptRunningProducer(qsizetype step_id,
                                    const QString& resource_id,
                                    const QString& request_id,
                                    const Paths& paths,
                                    const QVariantMap& launch_spec,
                                    const QString& expected_projection,
                                    const QVariantMap& receipt) {
    const auto clip_id = launch_spec.value(QStringLiteral("operationId")).toString();
    if (!receiptMatchesLaunchSpec(receipt, launch_spec) ||
        !projectionMatches(currentCompoundProjection(clip_id), expected_projection)) {
      auto result = failure(QStringLiteral("The exact producer receipt or editable Timeline projection does not match the prepared compound."));
      result.insert(QStringLiteral("recoveryRequired"), true);
      result.insert(QStringLiteral("requestId"), request_id);
      result.insert(QStringLiteral("compoundClipId"), clip_id);
      return result;
    }
    const auto current = timeline_->agentTaskDocumentState();
    if (current.isEmpty()) {
      auto result = failure(QStringLiteral("The accepted compound Timeline state could not be read."));
      result.insert(QStringLiteral("recoveryRequired"), true);
      return result;
    }
    const auto accepted = tasks_->journal()->complete(
        step_id, current,
        {{QStringLiteral("ok"), true},
         {QStringLiteral("accepted"), true},
         {QStringLiteral("changed"), true},
         {QStringLiteral("requestId"), request_id},
         {QStringLiteral("compoundClipId"), clip_id},
         {QStringLiteral("childTimelineId"), launch_spec.value(QStringLiteral("childTimelineId"))},
         {QStringLiteral("compoundDocumentProjectionSha256"), expected_projection},
         {QStringLiteral("producerLaunchSpecSha256"), launch_spec.value(QStringLiteral("launchSpecSha256"))}},
        true);
    if (!accepted.value(QStringLiteral("ok")).toBool()) {
      auto result = accepted;
      result.insert(QStringLiteral("recoveryRequired"), true);
      result.insert(QStringLiteral("accepted"), true);
      result.insert(QStringLiteral("requestId"), request_id);
      result.insert(QStringLiteral("compoundClipId"), clip_id);
      return result;
    }
    const auto recorded_receipt = tasks_->journal()->recordAcceptedProducerReceipt(
        step_id, resource_id, receipt);
    if (!recorded_receipt.value(QStringLiteral("ok")).toBool()) {
      auto result = recorded_receipt;
      result.insert(QStringLiteral("recoveryRequired"), true);
      result.insert(QStringLiteral("accepted"), true);
      result.insert(QStringLiteral("requestId"), request_id);
      result.insert(QStringLiteral("compoundClipId"), clip_id);
      return result;
    }
    for (const auto& stable_path : {paths.document, paths.graph, paths.receipt}) {
      const auto file = AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(stable_path);
      if (file.resource_id.isEmpty() ||
          !tasks_->enroll(file.resource_id, file.adapter).value(QStringLiteral("ok")).toBool() ||
          !tasks_->checkpointOwnedResource(file.resource_id).value(QStringLiteral("ok")).toBool()) {
        auto result = failure(QStringLiteral("The producer was accepted, but a stable compound output checkpoint failed."));
        result.insert(QStringLiteral("recoveryRequired"), true);
        result.insert(QStringLiteral("accepted"), true);
        result.insert(QStringLiteral("requestId"), request_id);
        result.insert(QStringLiteral("compoundClipId"), clip_id);
        return result;
      }
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("accepted"), true},
            {QStringLiteral("pending"), true},
            {QStringLiteral("requestId"), request_id},
            {QStringLiteral("compoundClipId"), clip_id},
            {QStringLiteral("createdClipId"), clip_id},
            {QStringLiteral("childTimelineId"), launch_spec.value(QStringLiteral("childTimelineId"))},
            {QStringLiteral("status"), QStringLiteral("running")},
            {QStringLiteral("producerReceiptPath"), paths.receipt},
            {QStringLiteral("producerReceipt"), receipt},
            {QStringLiteral("taskStepId"), step_id},
            {QStringLiteral("resourceId"), resource_id},
            {QStringLiteral("taskStateHash"), tasks_->journal()->checkpointHash(resource_id)}};
  }

  QVariantMap startPrepared(qsizetype step_id,
                            const QString& resource_id,
                            const QString& request_id,
                            const Paths& paths,
                            const QVariantMap& arguments,
                            const QVariantMap& launch_spec,
                            bool from_create) {
    const auto clip_id = launch_spec.value(QStringLiteral("operationId")).toString();
    if (!canonicalUuid(clip_id) || launch_spec.isEmpty())
      return failure(QStringLiteral("The prepared compound operation has no sealed native launch specification."));
    const auto expected_projection = arguments.value(
        QStringLiteral("compoundDocumentProjectionSha256")).toString();
    if (!projectionMatches(currentCompoundProjection(clip_id), expected_projection)) {
      return failure(QStringLiteral("The Timeline changed after compound preparation; the producer was not started."));
    }
    const auto restored = timeline_->restoreCompoundRenderLaunchSpecForAutomation(
        clip_id, launch_spec);
    if (!restored.value(QStringLiteral("ok")).toBool()) return restored;

    // A previous invocation may have launched the exact producer and crashed
    // before the accepted journal receipt was written. Reconcile that process
    // by its sealed argv identity instead of ever spawning a duplicate.
    QVariantMap candidate_receipt;
    QString candidate_error;
    const auto candidate = timeline_compound::producer::find_unique_candidate(
        launch_spec, &candidate_receipt, &candidate_error);
    if (candidate == timeline_compound::producer::CandidateState::Unavailable ||
        candidate == timeline_compound::producer::CandidateState::Ambiguous) {
      auto result = failure(candidate_error.isEmpty()
          ? QStringLiteral("The producer identity is ambiguous or unavailable; no new writer was launched.")
          : candidate_error);
      result.insert(QStringLiteral("requestId"), request_id);
      result.insert(QStringLiteral("compoundClipId"), clip_id);
      result.insert(QStringLiteral("prepared"), true);
      result.insert(QStringLiteral("recoveryRequired"), true);
      return result;
    }

    const QFileInfo receipt_info(paths.receipt);
    const QFileInfo partial_info(paths.partial);
    const QFileInfo render_info(paths.render);
    if (candidate == timeline_compound::producer::CandidateState::UniqueMatch) {
      if (receipt_info.exists()) {
        QString receipt_error;
        const auto existing_receipt = timeline_compound::producer::read_receipt(
            paths.receipt, &receipt_error);
        if (!receiptMatchesLaunchSpec(existing_receipt, launch_spec) ||
            timeline_compound::producer::inspect_receipt(existing_receipt, &receipt_error) !=
                timeline_compound::producer::IdentityState::Matching) {
          auto result = failure(QStringLiteral("The existing producer receipt conflicts with the unique matching process; preserve every output."));
          result.insert(QStringLiteral("requestId"), request_id);
          result.insert(QStringLiteral("compoundClipId"), clip_id);
          result.insert(QStringLiteral("recoveryRequired"), true);
          return result;
        }
        candidate_receipt = existing_receipt;
      } else if (!timeline_compound::producer::write_receipt(
                     paths.receipt, candidate_receipt, &candidate_error)) {
        auto result = failure(candidate_error.isEmpty()
            ? QStringLiteral("The exact running producer receipt could not be persisted.")
            : candidate_error);
        result.insert(QStringLiteral("requestId"), request_id);
        result.insert(QStringLiteral("compoundClipId"), clip_id);
        result.insert(QStringLiteral("recoveryRequired"), true);
        return result;
      }
      return acceptRunningProducer(step_id, resource_id, request_id, paths,
                                   launch_spec, expected_projection, candidate_receipt);
    }

    // A receipt or media output without a matching live producer means this
    // prepared request already reached an uncertain terminal state. Never
    // rerun it over its partial/final bytes; let inspect/reconcile settle it.
    if (receipt_info.exists()) {
      QString receipt_error;
      const auto existing_receipt = timeline_compound::producer::read_receipt(
          paths.receipt, &receipt_error);
      if (!receiptMatchesLaunchSpec(existing_receipt, launch_spec) ||
          timeline_compound::producer::inspect_receipt(existing_receipt, &receipt_error) !=
              timeline_compound::producer::IdentityState::NotRunning) {
        auto result = failure(receipt_error.isEmpty()
            ? QStringLiteral("The persisted producer receipt cannot prove a stopped matching writer; no new producer was launched.")
            : receipt_error);
        result.insert(QStringLiteral("requestId"), request_id);
        result.insert(QStringLiteral("compoundClipId"), clip_id);
        result.insert(QStringLiteral("recoveryRequired"), true);
        return result;
      }
      return acceptRunningProducer(step_id, resource_id, request_id, paths,
                                   launch_spec, expected_projection, existing_receipt);
    }
    if (partial_info.exists() || partial_info.isSymLink() ||
        render_info.exists() || render_info.isSymLink()) {
      auto result = failure(QStringLiteral("A compound partial or final output exists without a matching producer receipt; no new writer was launched."));
      result.insert(QStringLiteral("requestId"), request_id);
      result.insert(QStringLiteral("compoundClipId"), clip_id);
      result.insert(QStringLiteral("recoveryRequired"), true);
      return result;
    }

    const auto started = timeline_->startCompoundRenderForAutomation(clip_id);
    if (!started.value(QStringLiteral("ok")).toBool() ||
        !started.value(QStringLiteral("accepted")).toBool() ||
        started.value(QStringLiteral("producerReceipt")).toMap().isEmpty()) {
      auto result = started;
      const auto start_error = started.value(QStringLiteral("error")).toString();
      const auto retryable = start_error.contains(QStringLiteral("deferred"), Qt::CaseInsensitive) ||
          start_error.contains(QStringLiteral("Another compound render is active"), Qt::CaseInsensitive);
      result.insert(QStringLiteral("requestId"), request_id);
      result.insert(QStringLiteral("compoundClipId"), clip_id);
      result.insert(QStringLiteral("prepared"), true);
      result.insert(QStringLiteral("accepted"), false);
      result.insert(QStringLiteral("retryable"), retryable);
      if (!started.value(QStringLiteral("processPresent")).toBool() && !retryable)
        result.insert(QStringLiteral("producerNotStarted"), true);
      else if (started.value(QStringLiteral("processPresent")).toBool())
        result.insert(QStringLiteral("recoveryRequired"), true);
      Q_UNUSED(from_create);
      return result;
    }
    const auto receipt = started.value(QStringLiteral("producerReceipt")).toMap();
    return acceptRunningProducer(step_id, resource_id, request_id, paths,
                                 launch_spec, expected_projection, receipt);
  }

  QVariantMap start(const QVariantMap& values) {
    const auto scope = taskScope(QStringLiteral("running"));
    if (!scope.value(QStringLiteral("ok")).toBool()) return scope;
    const auto request_id = values.value(QStringLiteral("requestId")).toString();
    const auto found = findStep(request_id);
    if (!found.value(QStringLiteral("ok")).toBool()) return found;
    if (found.value(QStringLiteral("stepStatus")).toString() != QStringLiteral("prepared"))
      return failure(QStringLiteral("Only the exact prepared compound request can be started."));
    const auto args = found.value(QStringLiteral("arguments")).toMap();
    const auto spec = args.value(QStringLiteral("producerLaunchSpec")).toMap();
    const auto path_map = pathsFromStep(found);
    if (!path_map.value(QStringLiteral("ok")).toBool()) return path_map;
    const auto paths = unpackPaths(path_map);
    if (!validatePaths(paths, scope.value(QStringLiteral("outputDirectory")).toString(),
                       args.value(QStringLiteral("childTimelineId")).toString(), false))
      return failure(QStringLiteral("The prepared compound outputs no longer match the admitted task paths."));
    const auto enriched = findStep(request_id).value(QStringLiteral("arguments")).toMap();
    auto result = startPrepared(found.value(QStringLiteral("stepId")).toLongLong(),
                                found.value(QStringLiteral("resourceId")).toString(),
                                request_id, paths, enriched, spec, false);
    result.insert(QStringLiteral("childTimelineId"), args.value(QStringLiteral("childTimelineId")));
    result.insert(QStringLiteral("childDocumentPath"), paths.document);
    result.insert(QStringLiteral("renderPath"), paths.render);
    result.insert(QStringLiteral("outputPaths"), paths.all());
    return result;
  }

  QVariantMap inspect(const QVariantMap& values) {
    const auto scope = taskScope({}, false);
    if (!scope.value(QStringLiteral("ok")).toBool()) return scope;
    const auto request_id = values.value(QStringLiteral("requestId")).toString();
    const auto found = findStep(request_id);
    if (!found.value(QStringLiteral("ok")).toBool()) return found;
    const auto step_status = found.value(QStringLiteral("stepStatus")).toString();
    const auto path_map = pathsFromStep(found);
    if (!path_map.value(QStringLiteral("ok")).toBool()) return path_map;
    const auto paths = unpackPaths(path_map);
    const auto child_id = found.value(QStringLiteral("arguments")).toMap()
                              .value(QStringLiteral("childTimelineId")).toString();
    if (!validatePaths(paths, scope.value(QStringLiteral("outputDirectory")).toString(), child_id, false))
      return failure(QStringLiteral("The compound operation paths no longer match the task's sealed output root."));
    const auto file_enrollment = enrollOutputPaths(paths, false);
    if (!file_enrollment.value(QStringLiteral("ok")).toBool()) return file_enrollment;
    const auto args = found.value(QStringLiteral("arguments")).toMap();
    const auto launch_spec = args.value(QStringLiteral("producerLaunchSpec")).toMap();
    auto clip_id = args.value(QStringLiteral("compoundClipId")).toString();
    if (clip_id.isEmpty() && !launch_spec.isEmpty())
      clip_id = launch_spec.value(QStringLiteral("operationId")).toString();
    if (clip_id.isEmpty()) {
      const auto inferred = findChildClipId(paths.document);
      if (inferred.value(QStringLiteral("ok")).toBool())
        clip_id = inferred.value(QStringLiteral("compoundClipId")).toString();
    }
    if (clip_id.isEmpty()) {
      return {{QStringLiteral("ok"), true},
              {QStringLiteral("requestId"), request_id},
              {QStringLiteral("stepStatus"), step_status},
              {QStringLiteral("status"), QStringLiteral("prepared")},
              {QStringLiteral("producerPresent"), false},
              {QStringLiteral("recoveryRequired"), true}};
    }
    if (!launch_spec.isEmpty()) {
      const auto status_map = timeline_->compoundRenderStatusForAutomation(clip_id);
      if (status_map.value(QStringLiteral("status")).toString() != QStringLiteral("ready") &&
          !QFileInfo(paths.render).exists()) {
        const auto restored = timeline_->restoreCompoundRenderLaunchSpecForAutomation(clip_id, launch_spec);
        if (!restored.value(QStringLiteral("ok")).toBool()) return restored;
      }
    }
    auto status_map = timeline_->compoundRenderStatusForAutomation(clip_id);
    if (!status_map.value(QStringLiteral("ok")).toBool()) return status_map;
    status_map.insert(QStringLiteral("requestId"), request_id);
    status_map.insert(QStringLiteral("compoundClipId"), clip_id);
    status_map.insert(QStringLiteral("childTimelineId"), child_id);
    status_map.insert(QStringLiteral("childDocumentPath"), paths.document);
    status_map.insert(QStringLiteral("stepStatus"), step_status);
    status_map.insert(QStringLiteral("resourceId"), found.value(QStringLiteral("resourceId")));
    status_map.insert(QStringLiteral("taskStepId"), found.value(QStringLiteral("stepId")));
    status_map.insert(QStringLiteral("outputPaths"), paths.all());
    return status_map;
  }

  QVariantMap verifyNoWriter(const QVariantMap& found, const Paths& paths,
                             QVariantMap* receipt_out) {
    if (receipt_out != nullptr) receipt_out->clear();
    const auto args = found.value(QStringLiteral("arguments")).toMap();
    const auto outcome = found.value(QStringLiteral("outcome")).toMap();
    const auto launch_spec = args.value(QStringLiteral("producerLaunchSpec")).toMap();
    auto receipt = outcome.value(QStringLiteral("producerReceipt")).toMap();
    if (receipt.isEmpty() && QFileInfo(paths.receipt).isFile()) {
      QString read_error;
      receipt = timeline_compound::producer::read_receipt(paths.receipt, &read_error);
      if (receipt.isEmpty()) return failure(read_error.isEmpty()
          ? QStringLiteral("The task-owned producer receipt is unreadable; preserve all files.")
          : read_error);
    }
    if (!receipt.isEmpty()) {
      if (!receiptMatchesLaunchSpec(receipt, launch_spec)) {
        return failure(QStringLiteral("The producer receipt does not match the sealed launch specification; preserve all files."));
      }
      QString identity_error;
      const auto identity = timeline_compound::producer::inspect_receipt(receipt, &identity_error);
      if (identity == timeline_compound::producer::IdentityState::Matching) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("producerStopped"), false},
                {QStringLiteral("error"), QStringLiteral("The exact compound producer is still running.")}};
      }
      if (identity != timeline_compound::producer::IdentityState::NotRunning) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("producerStopped"), false},
                {QStringLiteral("error"), identity_error.isEmpty()
                    ? QStringLiteral("The persisted producer receipt could not be verified.")
                    : identity_error}};
      }
      if (receipt_out != nullptr) *receipt_out = receipt;
      return {{QStringLiteral("ok"), true},
              {QStringLiteral("producerStopped"), true},
              {QStringLiteral("producerProof"), QStringLiteral("verified-absence")},
              {QStringLiteral("producerReceipt"), receipt}};
    }
    if (launch_spec.isEmpty()) {
      const auto any_started_output = QFileInfo(paths.partial).exists() ||
          QFileInfo(paths.partial).isSymLink() || QFileInfo(paths.render).exists() ||
          QFileInfo(paths.render).isSymLink() || QFileInfo(paths.receipt).exists() ||
          QFileInfo(paths.receipt).isSymLink();
      if (any_started_output) {
        return failure(QStringLiteral("A producer output exists without its required pre-launch specification; preserve it for manual recovery."));
      }
      return {{QStringLiteral("ok"), true},
              {QStringLiteral("producerStopped"), true},
              {QStringLiteral("producerProof"), QStringLiteral("launch-not-started")}};
    }
    QVariantMap candidate_receipt;
    QString candidate_error;
    const auto candidate = timeline_compound::producer::find_unique_candidate(
        launch_spec, &candidate_receipt, &candidate_error);
    if (candidate == timeline_compound::producer::CandidateState::NoMatch) {
      return {{QStringLiteral("ok"), true},
              {QStringLiteral("producerStopped"), true},
              {QStringLiteral("producerProof"), QStringLiteral("verified-absence")}};
    }
    if (candidate == timeline_compound::producer::CandidateState::UniqueMatch) {
      if (receipt_out != nullptr) *receipt_out = candidate_receipt;
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("producerStopped"), false},
              {QStringLiteral("producerProof"), QStringLiteral("matching-process-present")},
              {QStringLiteral("producerReceipt"), candidate_receipt}};
    }
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("producerStopped"), false},
            {QStringLiteral("error"), candidate_error.isEmpty()
                ? QStringLiteral("The producer identity is ambiguous or unavailable; outputs were preserved.")
                : candidate_error}};
  }

  QVariantMap settleAcceptedStep(const QVariantMap& found, const Paths& paths) {
    const auto step_status = found.value(QStringLiteral("stepStatus")).toString();
    if (step_status != QStringLiteral("accepted"))
      return failure(QStringLiteral("Only an accepted pending compound can be settled."));
    const auto args = found.value(QStringLiteral("arguments")).toMap();
    const auto outcome = found.value(QStringLiteral("outcome")).toMap();
    const auto clip_id = args.value(QStringLiteral("compoundClipId")).toString();
    const auto launch_spec = args.value(QStringLiteral("producerLaunchSpec")).toMap();
    if (!canonicalUuid(clip_id) || launch_spec.isEmpty())
      return failure(QStringLiteral("The accepted compound operation has no durable clip ID or launch specification."));
    const auto expected_projection = outcome.value(
        QStringLiteral("compoundDocumentProjectionSha256")).toString();
    if (!projectionMatches(currentCompoundProjection(clip_id), expected_projection)) {
      return failure(QStringLiteral("The Timeline changed while the compound producer was pending; no output checkpoint was adopted."));
    }
    const auto status_map = timeline_->compoundRenderStatusForAutomation(clip_id);
    if (!status_map.value(QStringLiteral("ok")).toBool()) return status_map;
    const auto status = status_map.value(QStringLiteral("status")).toString();
    if (!status_map.value(QStringLiteral("terminal")).toBool()) {
      return {{QStringLiteral("ok"), true},
              {QStringLiteral("accepted"), true},
              {QStringLiteral("pending"), true},
              {QStringLiteral("requestId"), args.value(QStringLiteral("requestId"))},
              {QStringLiteral("compoundClipId"), clip_id},
              {QStringLiteral("status"), status},
              {QStringLiteral("producerPresent"), status_map.value(QStringLiteral("processPresent"))}};
    }
    QVariantMap producer_receipt;
    const auto proof = verifyNoWriter(found, paths, &producer_receipt);
    if (!proof.value(QStringLiteral("ok")).toBool() ||
        !proof.value(QStringLiteral("producerStopped")).toBool()) {
      auto result = proof;
      result.insert(QStringLiteral("accepted"), true);
      result.insert(QStringLiteral("pending"), true);
      result.insert(QStringLiteral("requestId"), args.value(QStringLiteral("requestId")));
      result.insert(QStringLiteral("compoundClipId"), clip_id);
      result.insert(QStringLiteral("status"), status);
      return result;
    }
    auto succeeded = status == QStringLiteral("ready");
    QString alpha_error;
    auto transparency_verified = status_map.value(QStringLiteral("transparencyVerified")).toBool();
    if (succeeded && !transparency_verified) {
      transparency_verified = timeline_compound::verify_transparent_prores_render(
          paths.render, QSize(timeline_->sequenceWidth(), timeline_->sequenceHeight()),
          &alpha_error);
    }
    if (succeeded && !transparency_verified) {
      succeeded = false;
      if (alpha_error.isEmpty())
        alpha_error = QStringLiteral("The final compound cache failed decoded alpha verification after reopen.");
      const auto failed_state = timeline_->settleCompoundRenderForAutomation(
          clip_id, QStringLiteral("failed"), alpha_error, launch_spec, producer_receipt);
      if (!failed_state.value(QStringLiteral("ok")).toBool()) return failed_state;
    }
    if (status != QStringLiteral("ready") && status != QStringLiteral("failed") &&
        status != QStringLiteral("cancelled") && status != QStringLiteral("interrupted")) {
      return failure(QStringLiteral("The compound operation is not in a settleable terminal state."));
    }
    if (!validatePaths(paths,
                       tasks_->journal()->nativeTimelineScope()
                           .value(QStringLiteral("outputDirectory")).toString(),
                       args.value(QStringLiteral("childTimelineId")).toString(), false)) {
      return failure(QStringLiteral("A compound output changed path identity before settlement."));
    }
    const auto checkpointed = checkpointOutputPaths(paths);
    if (!checkpointed.value(QStringLiteral("ok")).toBool()) return checkpointed;
    const auto current = timeline_->agentTaskDocumentState();
    const QVariantMap settlement{
        {QStringLiteral("status"), succeeded ? QStringLiteral("ready") : status},
        {QStringLiteral("transparencyVerified"), succeeded && transparency_verified},
        {QStringLiteral("producerProof"), proof.value(QStringLiteral("producerProof"))},
        {QStringLiteral("producerReceiptPath"), paths.receipt},
        {QStringLiteral("outputPaths"), paths.all()},
        {QStringLiteral("error"), succeeded ? QString{} :
             (alpha_error.isEmpty() ? status_map.value(QStringLiteral("error")).toString() : alpha_error)}};
    const auto settled = tasks_->journal()->settle(
        found.value(QStringLiteral("resourceId")).toString(), current,
        succeeded, settlement);
    if (!settled.value(QStringLiteral("ok")).toBool()) {
      auto result = settled;
      result.insert(QStringLiteral("recoveryRequired"), true);
      return result;
    }
    return {{QStringLiteral("ok"), succeeded},
            {QStringLiteral("settled"), true},
            {QStringLiteral("accepted"), false},
            {QStringLiteral("requestId"), args.value(QStringLiteral("requestId"))},
            {QStringLiteral("compoundClipId"), clip_id},
            {QStringLiteral("childTimelineId"), args.value(QStringLiteral("childTimelineId"))},
            {QStringLiteral("status"), succeeded ? QStringLiteral("ready") : status},
            {QStringLiteral("transparencyVerified"), succeeded && transparency_verified},
            {QStringLiteral("resourceId"), found.value(QStringLiteral("resourceId"))},
            {QStringLiteral("taskStateHash"), tasks_->journal()->checkpointHash(
                found.value(QStringLiteral("resourceId")).toString())},
            {QStringLiteral("error"), settlement.value(QStringLiteral("error"))}};
  }

  QVariantMap settle(const QVariantMap& values) {
    const auto scope = taskScope({}, false);
    if (!scope.value(QStringLiteral("ok")).toBool()) return scope;
    const auto request_id = values.value(QStringLiteral("requestId")).toString();
    const auto found = findStep(request_id);
    if (!found.value(QStringLiteral("ok")).toBool()) return found;
    const auto path_map = pathsFromStep(found);
    if (!path_map.value(QStringLiteral("ok")).toBool()) return path_map;
    const auto paths = unpackPaths(path_map);
    const auto enrolled = enrollOutputPaths(paths, false);
    if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
    auto result = settleAcceptedStep(found, paths);
    const auto args = found.value(QStringLiteral("arguments")).toMap();
    result.insert(QStringLiteral("childTimelineId"), args.value(QStringLiteral("childTimelineId")));
    result.insert(QStringLiteral("childDocumentPath"), paths.document);
    result.insert(QStringLiteral("outputPaths"), paths.all());
    return result;
  }

  QVariantMap cancelOrReconcile(const QVariantMap& found,
                                const Paths& paths,
                                const bool from_revert) {
    const auto args = found.value(QStringLiteral("arguments")).toMap();
    const auto step_status = found.value(QStringLiteral("stepStatus")).toString();
    const auto request_id = args.value(QStringLiteral("requestId")).toString();
    const auto resource_id = found.value(QStringLiteral("resourceId")).toString();
    auto clip_id = args.value(QStringLiteral("compoundClipId")).toString();
    auto launch_spec = args.value(QStringLiteral("producerLaunchSpec")).toMap();
    if (clip_id.isEmpty() && !launch_spec.isEmpty())
      clip_id = launch_spec.value(QStringLiteral("operationId")).toString();
    if (clip_id.isEmpty()) {
      const auto inferred = findChildClipId(paths.document);
      if (inferred.value(QStringLiteral("ok")).toBool())
        clip_id = inferred.value(QStringLiteral("compoundClipId")).toString();
    }
    if (clip_id.isEmpty()) {
      if (step_status == QStringLiteral("prepared") &&
          tasks_->journal()->matchesCheckpoint(resource_id,
                                                timeline_->agentTaskDocumentState())) {
        const auto checkpointed = checkpointOutputPaths(paths);
        if (!checkpointed.value(QStringLiteral("ok")).toBool()) return checkpointed;
        const auto resolved = tasks_->journal()->resolvePreparedStep(
            found.value(QStringLiteral("stepId")).toLongLong(), resource_id,
            timeline_->agentTaskDocumentState(),
            {{QStringLiteral("ok"), false},
             {QStringLiteral("mutationNotApplied"), true},
             {QStringLiteral("error"), QStringLiteral("The compound operation was cancelled before child and parent creation.")}});
        if (!resolved.value(QStringLiteral("ok")).toBool()) return resolved;
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("settled"), true},
                {QStringLiteral("mutationNotApplied"), true},
                {QStringLiteral("requestId"), request_id},
                {QStringLiteral("status"), QStringLiteral("cancelled")}};
      }
      return failure(QStringLiteral("The prepared compound has no durable clip checkpoint; preserve it for explicit recovery."));
    }
    if (launch_spec.isEmpty()) {
      const auto any_started_output = QFileInfo(paths.partial).exists() ||
          QFileInfo(paths.partial).isSymLink() || QFileInfo(paths.render).exists() ||
          QFileInfo(paths.render).isSymLink() || QFileInfo(paths.receipt).exists() ||
          QFileInfo(paths.receipt).isSymLink();
      if (any_started_output) {
        return failure(QStringLiteral("A compound output exists without a durable pre-launch specification; no cleanup or settlement was applied."));
      }
      // There is no launch specification, and the action contract persists it
      // before start(). This operation can be resolved without a process kill.
      const auto interrupted = timeline_->settleCompoundRenderForAutomation(
          clip_id, QStringLiteral("interrupted"),
          QStringLiteral("The operation stopped before its sealed producer specification was persisted."));
      if (!interrupted.value(QStringLiteral("ok")).toBool()) return interrupted;
      const auto projection_hash = args.value(
          QStringLiteral("compoundDocumentProjectionSha256")).toString();
      if (projection_hash.isEmpty() ||
          !projectionMatches(currentCompoundProjection(clip_id), projection_hash)) {
        return failure(QStringLiteral("The compound Timeline changed or lacks a durable projection checkpoint; files were preserved."));
      }
      const auto checkpointed = checkpointOutputPaths(paths);
      if (!checkpointed.value(QStringLiteral("ok")).toBool()) return checkpointed;
      const auto failure_receipt = QVariantMap{
          {QStringLiteral("ok"), false},
          {QStringLiteral("error"), QStringLiteral("Cancelled before producer launch.")},
          {QStringLiteral("producerProof"), QStringLiteral("launch-not-started")},
          {QStringLiteral("outputPaths"), paths.all()}};
      if (step_status == QStringLiteral("prepared")) {
        const auto resolved = tasks_->journal()->resolvePreparedStep(
            found.value(QStringLiteral("stepId")).toLongLong(), resource_id,
            timeline_->agentTaskDocumentState(), failure_receipt);
        if (!resolved.value(QStringLiteral("ok")).toBool()) return resolved;
      } else {
        const auto settled = tasks_->journal()->settle(
            resource_id, timeline_->agentTaskDocumentState(), false, failure_receipt);
        if (!settled.value(QStringLiteral("ok")).toBool()) return settled;
      }
      return {{QStringLiteral("ok"), true},
              {QStringLiteral("settled"), true},
              {QStringLiteral("requestId"), request_id},
              {QStringLiteral("compoundClipId"), clip_id},
              {QStringLiteral("processStopped"), true},
              {QStringLiteral("producerProof"), QStringLiteral("launch-not-started")},
              {QStringLiteral("outputsRemoved"), false},
              {QStringLiteral("requiresJournalReconciliation"), true}};
    }
    if (step_status == QStringLiteral("prepared")) {
      const auto projection_hash = args.value(
          QStringLiteral("compoundDocumentProjectionSha256")).toString();
      if (projection_hash.isEmpty() ||
          !projectionMatches(currentCompoundProjection(clip_id), projection_hash)) {
        return failure(QStringLiteral("The compound Timeline changed after preparation; the producer will not be signalled."));
      }
    } else {
      const auto projection_hash = found.value(QStringLiteral("outcome")).toMap()
          .value(QStringLiteral("compoundDocumentProjectionSha256")).toString();
      if (projection_hash.isEmpty() ||
          !projectionMatches(currentCompoundProjection(clip_id), projection_hash)) {
        return failure(QStringLiteral("The compound Timeline changed while the producer was pending; outputs were preserved."));
      }
    }
    const auto restored = timeline_->restoreCompoundRenderLaunchSpecForAutomation(
        clip_id, launch_spec);
    if (!restored.value(QStringLiteral("ok")).toBool()) return restored;
    auto stopped = timeline_->cancelCompoundRenderForAutomation(clip_id);
    if (!stopped.value(QStringLiteral("ok")).toBool() ||
        !stopped.value(QStringLiteral("processStopped")).toBool() ||
        !stopped.value(QStringLiteral("producerProofAvailable")).toBool()) {
      auto result = stopped;
      result.insert(QStringLiteral("requestId"), request_id);
      result.insert(QStringLiteral("outputsRemoved"), false);
      return result;
    }
    const auto checkpointed = checkpointOutputPaths(paths);
    if (!checkpointed.value(QStringLiteral("ok")).toBool()) {
      auto result = checkpointed;
      result.insert(QStringLiteral("processStopped"), true);
      result.insert(QStringLiteral("recoveryRequired"), true);
      return result;
    }
    const auto settlement = QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QStringLiteral("The compound producer was cancelled; output cleanup remains owned by the task journal.")},
        {QStringLiteral("producerProof"), stopped.value(QStringLiteral("producerProof"))},
        {QStringLiteral("producerReceipt"), stopped.value(QStringLiteral("producerReceipt"))},
        {QStringLiteral("producerLaunchSpec"), launch_spec},
        {QStringLiteral("outputPaths"), paths.all()},
        {QStringLiteral("outputsRemoved"), false}};
    QVariantMap journal_result;
    if (step_status == QStringLiteral("prepared")) {
      journal_result = tasks_->journal()->resolvePreparedStep(
          found.value(QStringLiteral("stepId")).toLongLong(), resource_id,
          timeline_->agentTaskDocumentState(), settlement);
    } else {
      journal_result = tasks_->journal()->settle(
          resource_id, timeline_->agentTaskDocumentState(), false, settlement);
    }
    if (!journal_result.value(QStringLiteral("ok")).toBool()) {
      auto result = journal_result;
      result.insert(QStringLiteral("processStopped"), true);
      result.insert(QStringLiteral("outputsRemoved"), false);
      result.insert(QStringLiteral("recoveryRequired"), true);
      return result;
    }
    auto result = stopped;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("settled"), true);
    result.insert(QStringLiteral("requestId"), request_id);
    result.insert(QStringLiteral("compoundClipId"), clip_id);
    result.insert(QStringLiteral("outputsRemoved"), false);
    result.insert(QStringLiteral("requiresJournalReconciliation"), true);
    result.insert(QStringLiteral("taskStateHash"), tasks_->journal()->checkpointHash(resource_id));
    Q_UNUSED(from_revert);
    return result;
  }

  QVariantMap cancel(const QVariantMap& values) {
    const auto scope = taskScope({}, false);
    if (!scope.value(QStringLiteral("ok")).toBool()) return scope;
    const auto found = findStep(values.value(QStringLiteral("requestId")).toString());
    if (!found.value(QStringLiteral("ok")).toBool()) return found;
    const auto status = found.value(QStringLiteral("stepStatus")).toString();
    if (status != QStringLiteral("prepared") && status != QStringLiteral("accepted"))
      return failure(QStringLiteral("Only a prepared or accepted compound operation can be cancelled."));
    const auto path_map = pathsFromStep(found);
    if (!path_map.value(QStringLiteral("ok")).toBool()) return path_map;
    const auto paths = unpackPaths(path_map);
    const auto enrolled = enrollOutputPaths(paths, false);
    if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
    QVariantMap result;
    if (status == QStringLiteral("accepted")) {
      const auto args = found.value(QStringLiteral("arguments")).toMap();
      const auto clip_id = args.value(QStringLiteral("compoundClipId")).toString();
      const auto native_status = timeline_->compoundRenderStatusForAutomation(clip_id);
      if (native_status.value(QStringLiteral("ok")).toBool() &&
          native_status.value(QStringLiteral("terminal")).toBool()) {
        result = settleAcceptedStep(found, paths);
      }
    }
    if (result.isEmpty()) result = cancelOrReconcile(found, paths, false);
    const auto args = found.value(QStringLiteral("arguments")).toMap();
    result.insert(QStringLiteral("childTimelineId"), args.value(QStringLiteral("childTimelineId")));
    result.insert(QStringLiteral("childDocumentPath"), paths.document);
    result.insert(QStringLiteral("outputPaths"), paths.all());
    return result;
  }

  QVariantMap reconcile(const QVariantMap& values) {
    const auto scope = taskScope({}, false);
    if (!scope.value(QStringLiteral("ok")).toBool()) return scope;
    const auto request_id = values.value(QStringLiteral("requestId")).toString();
    const auto found = findStep(request_id);
    if (!found.value(QStringLiteral("ok")).toBool()) return found;
    const auto path_map = pathsFromStep(found);
    if (!path_map.value(QStringLiteral("ok")).toBool()) return path_map;
    const auto paths = unpackPaths(path_map);
    auto result = reconcileStep(request_id, false);
    const auto args = found.value(QStringLiteral("arguments")).toMap();
    result.insert(QStringLiteral("childTimelineId"), args.value(QStringLiteral("childTimelineId")));
    result.insert(QStringLiteral("childDocumentPath"), paths.document);
    result.insert(QStringLiteral("outputPaths"), paths.all());
    return result;
  }

  QVariantMap reconcileStep(const QString& request_id, bool from_revert) {
    const auto found = findStep(request_id);
    if (!found.value(QStringLiteral("ok")).toBool()) return found;
    const auto status = found.value(QStringLiteral("stepStatus")).toString();
    if (status != QStringLiteral("prepared") && status != QStringLiteral("accepted"))
      return {{QStringLiteral("ok"), true},
              {QStringLiteral("settled"), true},
              {QStringLiteral("requestId"), request_id},
              {QStringLiteral("stepStatus"), status},
              {QStringLiteral("alreadySettled"), true}};
    const auto scope = taskScope({}, false);
    if (!scope.value(QStringLiteral("ok")).toBool()) return scope;
    if (status == QStringLiteral("accepted")) {
      const auto args = found.value(QStringLiteral("arguments")).toMap();
      const auto clip_id = args.value(QStringLiteral("compoundClipId")).toString();
      const auto native_status = timeline_->compoundRenderStatusForAutomation(clip_id);
      if (native_status.value(QStringLiteral("ok")).toBool() &&
          native_status.value(QStringLiteral("terminal")).toBool()) {
        const auto path_map = pathsFromStep(found);
        if (!path_map.value(QStringLiteral("ok")).toBool()) return path_map;
        const auto paths = unpackPaths(path_map);
        const auto enrolled = enrollOutputPaths(paths, false);
        if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
        return settleAcceptedStep(found, paths);
      }
    }
    const auto path_map = pathsFromStep(found);
    if (!path_map.value(QStringLiteral("ok")).toBool()) return path_map;
    const auto paths = unpackPaths(path_map);
    if (!validatePaths(paths, scope.value(QStringLiteral("outputDirectory")).toString(),
                       found.value(QStringLiteral("arguments")).toMap()
                           .value(QStringLiteral("childTimelineId")).toString(), false)) {
      return failure(QStringLiteral("The compound outputs escaped their sealed task path scope."));
    }
    const auto enrolled = enrollOutputPaths(paths, false);
    if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
    return cancelOrReconcile(found, paths, from_revert);
  }

  QPointer<TimelineDocumentController> timeline_;
  AgentTaskCoordinator* tasks_ = nullptr;
  std::function<QString()> task_id_;
  Authorizer authorize_edit_;
};

}  // namespace photo_editor::desktop

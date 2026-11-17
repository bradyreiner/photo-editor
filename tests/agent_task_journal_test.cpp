#include "agent_task_journal.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <iostream>

using photo_editor::desktop::AgentTaskJournal;

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  CHECK(root.isValid(), "Create isolated task storage");
  QByteArray poster = "original poster";
  QByteArray timeline = "original timeline";
  int native_cancel_count = 0;
  int restore_count = 0;
  QHash<QString, AgentTaskJournal::ResourceAdapter> adapters{
      {"poster", {[&]() { return poster; }, [&](const QByteArray& value) {
        poster = value; ++restore_count; return true;
      }, [&]() { ++native_cancel_count; }}},
      {"timeline", {[&]() { return timeline; }, [&](const QByteArray& value) {
        timeline = value; ++restore_count; return true;
      }, [&]() { ++native_cancel_count; }}}};

  AgentTaskJournal journal;
  CHECK(journal.begin(root.path(), "long-task").value("ok").toBool(), "Start durable task");
  // More than the old total call limit, across separate resource owners.
  for (int i = 0; i < 24; ++i) {
    const auto id = i % 2 == 0 ? QStringLiteral("poster") : QStringLiteral("timeline");
    auto& state = i % 2 == 0 ? poster : timeline;
    const auto prepared = journal.prepare(id, "native.edit", {{"position", i}}, state);
    CHECK(prepared.value("ok").toBool(), "Every admitted edit has a durable baseline and step");
    state.append(QByteArray::number(i));
    CHECK(journal.complete(prepared.value("stepId").toLongLong(), state, {{"ok", true}})
        .value("ok").toBool(), "Checkpoint every completed edit");
  }
  CHECK(journal.finish("review").value("ok").toBool(), "Completed task remains reversible");
  CHECK(journal.report().value("steps").toList().size() == 24, "No task history omission at eight steps");

  AgentTaskJournal recovered;
  CHECK(recovered.recover(root.path(), "long-task").value("ok").toBool(), "Recover after controller/process recreation");
  const auto owned_poster = poster;
  poster.append(" manual edit");
  CHECK(!recovered.revert(adapters).value("ok").toBool(), "Refuse to overwrite a subsequent manual edit");
  CHECK(restore_count == 0 && timeline != "original timeline", "Preflight all resources before restoring any");
  poster = owned_poster;
  CHECK(recovered.revert(adapters).value("ok").toBool(), "Restore the complete recovered task");
  CHECK(poster == "original poster" && timeline == "original timeline" && restore_count == 2,
      "Restore exact pre-task bytes across document owners");
  CHECK(!recovered.hasChanges() && !recovered.revert(adapters).value("ok").toBool(), "Revert is not replayed");

  AgentTaskJournal pending;
  CHECK(pending.begin(root.path(), "pending-task").value("ok").toBool(), "Start async task");
  const auto accepted = pending.prepare("poster", "native.cutout", {}, poster);
  poster = "placeholder layer";
  CHECK(pending.complete(accepted.value("stepId").toLongLong(), poster, {{"ok", true}}, true)
      .value("ok").toBool(), "Persist acceptance separately from pixel completion");
  CHECK(!pending.finish("review").value("ok").toBool() &&
        !pending.prepare("poster", "native.move", {}, poster).value("ok").toBool(),
      "Pending pixels cannot be called completed or bypassed by another mutation");
  poster = "materialized cutout";
  CHECK(pending.settle("poster", poster, true).value("ok").toBool(), "Native owner confirms actual pixel completion");
  CHECK(pending.finish("review").value("ok").toBool(), "Ready pixels permit review");
  CHECK(pending.revert(adapters).value("ok").toBool() && poster == "original poster", "Async output also reverts to before acceptance");

  AgentTaskJournal compound;
  QByteArray compound_timeline = "compound parent before";
  CHECK(compound.begin(root.path(), "compound-task").value("ok").toBool(),
      "Start task-owned compound render");
  const auto compound_step = compound.prepare(
      "timeline", "timeline.action.createTimelineCompound",
      {{"childTimelineId", "11111111-1111-4111-8111-111111111111"},
       {"outputPaths", QVariantList{"compound.json", "compound.partial.mov",
           "compound.mov", "compound.ffgraph", "compound.producer.json"}}},
      compound_timeline);
  CHECK(compound_step.value("ok").toBool(),
      "Persist exact compound output intent before native creation");
  const QVariantMap producer_launch_spec{
      {"schema", "looksy.timeline.compound-launch/v1"},
      {"operationId", "22222222-2222-4222-8222-222222222222"},
      {"childTimelineId", "11111111-1111-4111-8111-111111111111"},
      {"executablePath", "/usr/bin/ffmpeg"},
      {"uid", 501},
      {"bootSessionId", "test-boot-session"},
      {"argv", QVariantList{"/usr/bin/ffmpeg", "-i", "source.mov", "compound.partial.mov"}},
      {"documentPath", "compound.json"},
      {"renderPath", "compound.mov"},
      {"partialPath", "compound.partial.mov"},
      {"filterGraphPath", "compound.ffgraph"},
      {"producerReceiptPath", "compound.producer.json"},
      {"launchSpecSha256", QByteArray(64, 'a')}};
  CHECK(compound.recordPreparedProducerLaunchSpec(
            compound_step.value("stepId").toLongLong(), "timeline",
            producer_launch_spec).value("ok").toBool(),
      "Persist exact producer intent before starting the renderer");
  const auto compound_arguments = compound.report().value("steps").toList()
      .first().toMap().value("arguments").toMap();
  const auto expected_launch_spec_json =
      QJsonDocument::fromVariant(producer_launch_spec).toJson(QJsonDocument::Compact);
  const auto persisted_launch_spec_json = QJsonDocument::fromVariant(
      compound_arguments.value("producerLaunchSpec").toMap()).toJson(QJsonDocument::Compact);
  CHECK(persisted_launch_spec_json == expected_launch_spec_json,
      "Prepared compound step retains the producer launch specification");
  compound_timeline = "compound parent after";
  CHECK(compound.complete(compound_step.value("stepId").toLongLong(),
                          compound_timeline,
                          {{"ok", true}, {"createdClipId", producer_launch_spec.value("operationId")}},
                          true).value("ok").toBool(),
      "Persist acceptance separately from renderer completion");
  QVariantMap producer_receipt = producer_launch_spec;
  producer_receipt.insert("schema", "looksy.timeline.compound-producer/v1");
  producer_receipt.insert("pid", 42);
  producer_receipt.insert("uid", 501);
  producer_receipt.insert("processGroupId", 42);
  producer_receipt.insert("startTimeSeconds", 100);
  producer_receipt.insert("startTimeMicroseconds", 10);
  producer_receipt.insert("bootSessionId", "test-boot-session");
  producer_receipt.insert("capturedAtUtc", "2026-10-26T00:00:00Z");
  producer_receipt.insert("receiptSha256", QByteArray(64, 'b'));
  CHECK(compound.recordAcceptedProducerReceipt(
            compound_step.value("stepId").toLongLong(), "timeline", producer_receipt)
            .value("ok").toBool(),
      "Persist the receipt only after its accepted launch specification is journaled");
  const auto accepted_outcome = compound.report().value("steps").toList()
      .first().toMap().value("outcome").toMap();
  CHECK(accepted_outcome.value("producerReceipt").toMap().value("pid").toLongLong() == 42 &&
        accepted_outcome.value("producerReceiptPath").toString() == "compound.producer.json",
      "Accepted compound receipt and exact receipt path remain available to recovery");
  CHECK(compound.settle("timeline", compound_timeline, true,
                        {{"status", "ready"}}).value("ok").toBool(),
      "Settle the compound operation after renderer completion");
  CHECK(compound.finish("review").value("ok").toBool(),
      "Review is allowed after the task-owned renderer settles");

  AgentTaskJournal interrupted;
  CHECK(interrupted.begin(root.path(), "interrupted-task").value("ok").toBool(), "Start interrupted task");
  const auto unresolved = interrupted.prepare("poster", "native.add_text", {}, poster);
  CHECK(unresolved.value("ok").toBool(), "Persist step before calling the native function");
  AgentTaskJournal uncertain;
  CHECK(uncertain.recover(root.path(), "interrupted-task").value("uncertainOperation").toBool(),
      "Crash between acceptance and receipt remains explicitly uncertain");
  CHECK(!uncertain.prepare("poster", "native.add_text", {}, poster).value("ok").toBool(),
      "Recovery cannot repeat an uncertain mutation");
  CHECK(uncertain.revert(adapters).value("ok").toBool(), "An interrupted unexecuted call can be reverted");

  AgentTaskJournal partial;
  CHECK(partial.begin(root.path(), "partial-rollback").value("ok").toBool(), "Start partial restore fixture");
  for (const auto& id : {QStringLiteral("poster"), QStringLiteral("timeline")}) {
    auto& state = id == "poster" ? poster : timeline;
    const auto prepared = partial.prepare(id, "native.edit", {}, state);
    state.append(" task edit");
    CHECK(partial.complete(prepared.value("stepId").toLongLong(), state, {{"ok", true}}).value("ok").toBool(), "Capture partial fixture edit");
  }
  auto failing_adapters = adapters;
  failing_adapters["poster"].restore = [](const QByteArray&) { return false; };
  CHECK(!partial.revert(failing_adapters).value("ok").toBool(), "Restore failure stays incomplete");
  CHECK(timeline == "original timeline" && poster != "original poster", "Persist successful resource restores before continuing");
  AgentTaskJournal resume_restore;
  CHECK(resume_restore.recover(root.path(), "partial-rollback").value("ok").toBool(), "Recover partially restored task");
  const auto before_retry_count = restore_count;
  CHECK(resume_restore.revert(adapters).value("ok").toBool() && restore_count == before_retry_count + 1,
      "Retry restores only remaining resource; already reverted resource is not replayed");

  AgentTaskJournal large_batch_seed;
  CHECK(large_batch_seed.begin(root.path(), "large-batch-lookup").value("ok").toBool(),
      "Create a large-journal batch lookup fixture");
  const auto target_batch_id = QStringLiteral("55555555-5555-4555-8555-555555555555");
  const auto target_request_hash = QString(64, QLatin1Char('d'));
  QJsonArray keyframes;
  for (int frame = 0; frame < 48; ++frame) {
    keyframes.append(QJsonObject{{QStringLiteral("localSeconds"), frame / 24.0},
        {QStringLiteral("value"), frame / 47.0},
        {QStringLiteral("interpolation"), QStringLiteral("ease-in-out")},
        {QStringLiteral("bezierInTimeSeconds"), 0.01},
        {QStringLiteral("bezierOutTimeSeconds"), 0.02}});
  }
  QJsonObject animation_channel;
  animation_channel.insert(QStringLiteral("parameter"), QStringLiteral("opacity"));
  animation_channel.insert(QStringLiteral("keyframes"), keyframes);
  QJsonObject animation_values;
  animation_values.insert(QStringLiteral("clipId"), QStringLiteral("clip-large"));
  animation_values.insert(QStringLiteral("channels"), QJsonArray{animation_channel});
  QJsonObject animation_operation;
  animation_operation.insert(QStringLiteral("id"), QStringLiteral("frameCurve"));
  animation_operation.insert(QStringLiteral("commandId"),
      QStringLiteral("timeline.action.setTimelineClipAnimation"));
  animation_operation.insert(QStringLiteral("values"), animation_values);
  const QJsonArray operation_payload{animation_operation};
  QJsonArray large_steps;
  for (int index = 0; index < 192; ++index) {
    QJsonObject arguments;
    arguments.insert(QStringLiteral("_batchId"), QStringLiteral("older-batch-%1").arg(index));
    arguments.insert(QStringLiteral("_batchRequestSha256"), target_request_hash);
    arguments.insert(QStringLiteral("operations"), operation_payload);
    QJsonObject old_outcome;
    old_outcome.insert(QStringLiteral("ok"), true);
    old_outcome.insert(QStringLiteral("batchResults"), QJsonArray{QJsonObject{
        {QStringLiteral("createdClipId"), QStringLiteral("older-clip-%1").arg(index)}}});
    QJsonObject old_step;
    old_step.insert(QStringLiteral("id"), index);
    old_step.insert(QStringLiteral("resourceId"), QStringLiteral("timeline:large"));
    old_step.insert(QStringLiteral("command"), QStringLiteral("timeline.action.applyDraftBatch"));
    old_step.insert(QStringLiteral("arguments"), arguments);
    old_step.insert(QStringLiteral("status"), QStringLiteral("completed"));
    old_step.insert(QStringLiteral("outcome"), old_outcome);
    large_steps.append(old_step);
  }
  QJsonObject target_arguments;
  target_arguments.insert(QStringLiteral("_batchId"), target_batch_id);
  target_arguments.insert(QStringLiteral("_batchRequestSha256"), target_request_hash);
  target_arguments.insert(QStringLiteral("operations"), operation_payload);
  QJsonObject target_outcome;
  target_outcome.insert(QStringLiteral("ok"), true);
  target_outcome.insert(QStringLiteral("batchResults"), QJsonArray{QJsonObject{
      {QStringLiteral("createdClipId"), QStringLiteral("target-clip-id")}}});
  QJsonObject target_step;
  target_step.insert(QStringLiteral("id"), static_cast<double>(large_steps.size()));
  target_step.insert(QStringLiteral("resourceId"), QStringLiteral("timeline:large"));
  target_step.insert(QStringLiteral("command"), QStringLiteral("timeline.action.applyDraftBatch"));
  target_step.insert(QStringLiteral("arguments"), target_arguments);
  target_step.insert(QStringLiteral("status"), QStringLiteral("completed"));
  target_step.insert(QStringLiteral("outcome"), target_outcome);
  large_steps.append(target_step);
  QFile large_journal_file(large_batch_seed.journalPath());
  CHECK(large_journal_file.open(QIODevice::ReadOnly), "Read the empty batch journal fixture");
  auto large_document = QJsonDocument::fromJson(large_journal_file.readAll()).object();
  large_journal_file.close();
  large_document.insert(QStringLiteral("steps"), large_steps);
  const auto large_json = QJsonDocument(large_document).toJson(QJsonDocument::Compact);
  QSaveFile large_journal_write(large_batch_seed.journalPath());
  CHECK(large_journal_write.open(QIODevice::WriteOnly) &&
      large_journal_write.write(large_json) == large_json.size() && large_journal_write.commit(),
      "Persist many prior keyframe-heavy batch receipts for lookup diagnostics");
  AgentTaskJournal large_batch_journal;
  CHECK(large_batch_journal.recover(root.path(), "large-batch-lookup").value("ok").toBool(),
      "Recover a journal with many historical keyframe payloads");
  const auto missing_batch_lookup = large_batch_journal.lookupIdempotentBatchStep(
      QStringLiteral("66666666-6666-4666-8666-666666666666"));
  CHECK(missing_batch_lookup.value("ok").toBool() &&
      !missing_batch_lookup.value("found").toBool() &&
      missing_batch_lookup.value("taskStatus").toString() == QStringLiteral("running"),
      "A new batch lookup inspects journal metadata without matching an unrelated history entry");
  QElapsedTimer direct_lookup_timer;
  direct_lookup_timer.start();
  const auto large_batch_lookup = large_batch_journal.lookupIdempotentBatchStep(target_batch_id);
  const auto direct_lookup_ms = direct_lookup_timer.nsecsElapsed() / 1000000.0;
  QElapsedTimer report_lookup_timer;
  report_lookup_timer.start();
  const auto legacy_steps = large_batch_journal.report().value(QStringLiteral("steps")).toList();
  auto legacy_match_id = QString{};
  for (const auto& value : legacy_steps) {
    const auto step = value.toMap();
    const auto arguments = step.value(QStringLiteral("arguments")).toMap();
    if (arguments.value(QStringLiteral("_batchId")).toString() == target_batch_id) {
      legacy_match_id = step.value(QStringLiteral("outcome")).toMap()
          .value(QStringLiteral("batchResults")).toList().first().toMap()
          .value(QStringLiteral("createdClipId")).toString();
      break;
    }
  }
  const auto report_lookup_ms = report_lookup_timer.nsecsElapsed() / 1000000.0;
  CHECK(large_batch_lookup.value(QStringLiteral("ok")).toBool() &&
      large_batch_lookup.value(QStringLiteral("found")).toBool() &&
      large_batch_lookup.value(QStringLiteral("requestSha256")).toString() == target_request_hash &&
      large_batch_lookup.value(QStringLiteral("outcome")).toMap()
          .value(QStringLiteral("batchResults")).toList().first().toMap()
          .value(QStringLiteral("createdClipId")).toString() == QStringLiteral("target-clip-id") &&
      !large_batch_lookup.contains(QStringLiteral("arguments")) &&
      legacy_match_id == QStringLiteral("target-clip-id"),
      "Direct lookup returns the same completed receipt while converting only its compact fields");
  std::cout << "Large batch journal diagnostic: " << large_steps.size() << " steps, "
            << large_json.size() << " bytes, JSON report+scan " << report_lookup_ms
            << " ms, direct lookup " << direct_lookup_ms << " ms (no threshold).\n";

  large_steps.append(target_step);
  large_document.insert(QStringLiteral("steps"), large_steps);
  const auto duplicate_json = QJsonDocument(large_document).toJson(QJsonDocument::Compact);
  QSaveFile duplicate_journal_write(large_batch_seed.journalPath());
  CHECK(duplicate_journal_write.open(QIODevice::WriteOnly) &&
      duplicate_journal_write.write(duplicate_json) == duplicate_json.size() &&
      duplicate_journal_write.commit(), "Persist an intentionally ambiguous duplicate batch ID");
  AgentTaskJournal duplicate_batch_journal;
  CHECK(duplicate_batch_journal.recover(root.path(), "large-batch-lookup").value("ok").toBool(),
      "Read a journal containing duplicate batch IDs without mutating it");
  const auto duplicate_lookup = duplicate_batch_journal.lookupIdempotentBatchStep(target_batch_id);
  CHECK(!duplicate_lookup.value(QStringLiteral("ok")).toBool() &&
      duplicate_lookup.value(QStringLiteral("ambiguousBatchId")).toBool(),
      "Duplicate batch IDs are rejected as ambiguous rather than selecting an arbitrary receipt");

  const auto cold_source_path = QDir(root.path()).filePath(QStringLiteral("cold-source.bin"));
  const QByteArray cold_source_bytes = QByteArrayLiteral("exact admitted source bytes for cold recovery");
  QFile cold_source_file(cold_source_path);
  CHECK(cold_source_file.open(QIODevice::WriteOnly) &&
      cold_source_file.write(cold_source_bytes) == cold_source_bytes.size(),
      "Write the exact cold-recovery source fixture");
  cold_source_file.close();
  const auto canonical_cold_source_path = QFileInfo(cold_source_path).canonicalFilePath();
  CHECK(!canonical_cold_source_path.isEmpty(), "Resolve the exact admitted source path");
  QCryptographicHash cold_source_signature_hash(QCryptographicHash::Sha256);
  cold_source_signature_hash.addData(QByteArray::number(cold_source_bytes.size()));
  cold_source_signature_hash.addData(cold_source_bytes);
  const auto cold_source_signature = QString::fromLatin1(cold_source_signature_hash.result().toHex());
  const auto cold_source_sha = QString::fromLatin1(
      QCryptographicHash::hash(cold_source_bytes, QCryptographicHash::Sha256).toHex());
  const auto state_hash = [](const QByteArray& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
  };
  const auto cold_resource_id = QStringLiteral("timeline:00000000-0000-4000-8000-000000000777");
  const auto cold_clip_before = QJsonObject{
      {QStringLiteral("clipId"), QStringLiteral("cold-clip-1")},
      {QStringLiteral("filePath"), canonical_cold_source_path},
      {QStringLiteral("sourceIdentitySignature"), cold_source_signature},
      {QStringLiteral("sourceFileSize"), 0},
      {QStringLiteral("sourceContentSha256"), QString{}},
      {QStringLiteral("displayLabel"), QStringLiteral("Original label")}};
  const auto cold_state_before = QJsonDocument(QJsonObject{
      {QStringLiteral("timelineId"), QStringLiteral("00000000-0000-4000-8000-000000000777")},
      {QStringLiteral("tracks"), QJsonArray{QJsonObject{
          {QStringLiteral("trackId"), QStringLiteral("video-1")},
          {QStringLiteral("clips"), QJsonArray{cold_clip_before}}}}}})
      .toJson(QJsonDocument::Compact);
  const auto cold_state_with_source_identity = [&](const QString& sha, const double size,
                                                    const QString& label,
                                                    const QString& signature) {
    auto clip = cold_clip_before;
    clip.insert(QStringLiteral("sourceContentSha256"), sha);
    clip.insert(QStringLiteral("sourceFileSize"), size);
    clip.insert(QStringLiteral("displayLabel"), label);
    clip.insert(QStringLiteral("sourceIdentitySignature"), signature);
    auto state = QJsonDocument::fromJson(cold_state_before).object();
    auto tracks = state.value(QStringLiteral("tracks")).toArray();
    auto track = tracks.first().toObject();
    track.insert(QStringLiteral("clips"), QJsonArray{clip});
    tracks.replace(0, track);
    state.insert(QStringLiteral("tracks"), tracks);
    return QJsonDocument(state).toJson(QJsonDocument::Compact);
  };
  const auto seed_cold_source_task = [&](AgentTaskJournal& target, const QString& task_id) {
    return target.begin(root.path(), task_id).value(QStringLiteral("ok")).toBool() &&
        target.recordNativeTimelineScope({cold_resource_id}, {canonical_cold_source_path},
            QFileInfo(root.path()).canonicalFilePath()).value(QStringLiteral("ok")).toBool() &&
        target.capture(cold_resource_id, cold_state_before).value(QStringLiteral("ok")).toBool();
  };

  AgentTaskJournal cold_running;
  CHECK(seed_cold_source_task(cold_running, QStringLiteral("cold-source-running")),
      "Persist a zero-step running task with an exact cold source baseline");
  AgentTaskJournal cold_recovered;
  CHECK(cold_recovered.recover(root.path(), QStringLiteral("cold-source-running"))
      .value(QStringLiteral("ok")).toBool(), "Recover the unchanged zero-step task baseline");
  const auto pending_source_state = cold_recovered.reconcileUnmutatedTimelineSourceIntegrity(
      cold_resource_id, cold_state_before);
  CHECK(!pending_source_state.value(QStringLiteral("ok")).toBool() &&
      pending_source_state.value(QStringLiteral("retryable")).toBool() &&
      pending_source_state.value(QStringLiteral("status")).toString() == QStringLiteral("source_integrity_pending") &&
      cold_recovered.checkpointHash(cold_resource_id) ==
          cold_recovered.report().value(QStringLiteral("resources")).toList().first().toMap()
              .value(QStringLiteral("beforeHash")).toString(),
      "Pending source identity stays retryable without changing the baseline or checkpoint");

  const auto cold_state_settled = cold_state_with_source_identity(
      cold_source_sha, static_cast<double>(cold_source_bytes.size()),
      QStringLiteral("Original label"), cold_source_signature);
  const auto cold_settlement = cold_recovered.reconcileUnmutatedTimelineSourceIntegrity(
      cold_resource_id, cold_state_settled);
  CHECK(cold_settlement.value(QStringLiteral("ok")).toBool() &&
      cold_settlement.value(QStringLiteral("changed")).toBool() &&
      cold_recovered.checkpointHash(cold_resource_id) ==
          cold_settlement.value(QStringLiteral("settledHash")).toString(),
      "A zero-step cold recovery checkpoints only verified admitted source SHA and size enrichment");
  const auto cold_resources_after_settlement = cold_recovered.report()
      .value(QStringLiteral("resources")).toList();
  CHECK(cold_resources_after_settlement.size() == 1 &&
      cold_resources_after_settlement.first().toMap().value(QStringLiteral("beforeHash")).toString() ==
          state_hash(cold_state_before) &&
      cold_resources_after_settlement.first().toMap().value(QStringLiteral("currentHash")).toString() ==
          state_hash(cold_state_settled),
      "Source settlement advances currentHash while preserving the exact pre-task revert baseline");

  QByteArray cold_live_state = cold_state_settled;
  AgentTaskJournal::ResourceAdapter cold_adapter{
      [&]() { return cold_live_state; },
      [&](const QByteArray& value) { cold_live_state = value; return true; }, {}};
  CHECK(cold_recovered.finish(QStringLiteral("review")).value(QStringLiteral("ok")).toBool() &&
      cold_recovered.releaseReviewedDraft({{cold_resource_id, cold_state_settled}})
          .value(QStringLiteral("ok")).toBool() &&
      cold_recovered.revert({{cold_resource_id, cold_adapter}}).value(QStringLiteral("ok")).toBool() &&
      cold_live_state == cold_state_before,
      "Reviewed cold recovery remains exactly reversible to its original source-metadata baseline");

  AgentTaskJournal authored_drift;
  CHECK(seed_cold_source_task(authored_drift, QStringLiteral("cold-source-authored-drift")),
      "Seed the authored-drift rejection case");
  const auto authored_drift_state = cold_state_with_source_identity(
      cold_source_sha, static_cast<double>(cold_source_bytes.size()),
      QStringLiteral("Changed label"), cold_source_signature);
  const auto authored_drift_result = authored_drift.reconcileUnmutatedTimelineSourceIntegrity(
      cold_resource_id, authored_drift_state);
  CHECK(!authored_drift_result.value(QStringLiteral("ok")).toBool() &&
      authored_drift.checkpointHash(cold_resource_id) == state_hash(cold_state_before),
      "Source settlement rejects unrelated authored-field drift and leaves its checkpoint unchanged");

  AgentTaskJournal wrong_source_hash;
  CHECK(seed_cold_source_task(wrong_source_hash, QStringLiteral("cold-source-wrong-hash")),
      "Seed the incorrect-source-hash rejection case");
  const auto wrong_source_state = cold_state_with_source_identity(
      QString(64, QLatin1Char('f')), static_cast<double>(cold_source_bytes.size()),
      QStringLiteral("Original label"), cold_source_signature);
  const auto wrong_source_result = wrong_source_hash.reconcileUnmutatedTimelineSourceIntegrity(
      cold_resource_id, wrong_source_state);
  CHECK(!wrong_source_result.value(QStringLiteral("ok")).toBool() &&
      wrong_source_hash.checkpointHash(cold_resource_id) == state_hash(cold_state_before),
      "Source settlement recomputes full file SHA and rejects a false digest without checkpointing it");

  AgentTaskJournal accepted_operation;
  CHECK(seed_cold_source_task(accepted_operation, QStringLiteral("cold-source-has-operation")),
      "Seed the accepted-operation rejection case");
  CHECK(accepted_operation.prepare(cold_resource_id, QStringLiteral("native.edit"), {},
      cold_state_before).value(QStringLiteral("ok")).toBool(),
      "Record an operation before attempting source reconciliation");
  CHECK(!accepted_operation.reconcileUnmutatedTimelineSourceIntegrity(
          cold_resource_id, cold_state_settled).value(QStringLiteral("ok")).toBool(),
      "Source settlement is unavailable after any accepted or uncertain operation");

  AgentTaskJournal invalid;
  CHECK(!invalid.begin(root.path(), "../escape").value("ok").toBool(), "Reject task-path traversal");
  QFile obstruction(QDir(root.path()).filePath("obstruction"));
  CHECK(obstruction.open(QIODevice::WriteOnly), "Create storage failure fixture");
  obstruction.close();
  CHECK(!invalid.begin(obstruction.fileName(), "cannot-save").value("ok").toBool(),
      "Never admit a task without writable recovery storage");
  CHECK(native_cancel_count >= 6, "Resource cancellation precedes restoration");
  std::cout << "Durable task ownership, long sequences, async completion, recovery, exact revert and conflict preservation passed.\n";
  return 0;
}

#include "agent_task_coordinator.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <iostream>

using photo_editor::desktop::AgentTaskCoordinator;
using photo_editor::desktop::AgentTaskJournal;
#define CHECK(c, m) do { if (!(c)) { std::cerr << m << '\n'; return 1; } } while (false)

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  QByteArray photo = "original photo", timeline = "original timeline";
  int canceled = 0, restored = 0;
  const auto adapter = [&](QByteArray& bytes) -> AgentTaskJournal::ResourceAdapter {
    return {[&bytes]() { return bytes; }, [&bytes, &restored, &canceled](const QByteArray& value) {
      if (canceled < 2) return false;
      bytes = value; ++restored; return true;
    }, [&canceled]() { ++canceled; }};
  };
  AgentTaskCoordinator tasks;
  CHECK(tasks.begin(root.path(), "admission").value("ok").toBool(), "Begin empty task");
  CHECK(tasks.enroll("photo", adapter(photo)).value("ok").toBool(), "Capture before admission rejection");
  CHECK(tasks.abandonAdmission().value("ok").toBool() && !tasks.journal()->active() &&
      !tasks.journal()->hasChanges(), "Failed admission releases an untouched baseline");
  CHECK(tasks.begin(root.path(), "mixed").value("ok").toBool(), "Next task after admission rejection");
  CHECK(tasks.journal()->recordApprovedSnapshot({{"contract", "agent-approved-session-v1"},
      {"prompt", "Create an editable thumbnail"}, {"answers", QVariantMap{{"title", "ABOVE THE CLOUDS"}}}})
      .value("ok").toBool(), "Approved brief is durable before the first mutation");
  CHECK(tasks.enroll("photo", adapter(photo)).value("ok").toBool(), "Lazy photo ownership");
  CHECK(tasks.enroll("timeline", adapter(timeline)).value("ok").toBool(), "Lazy Timeline ownership in the same journal");
  CHECK(!tasks.adoptPreparedTask("mixed", {"photo"}).value("ok").toBool(),
      "Prepared adoption cannot claim an untouched task as a reversible document creation");
  for (int i = 0; i < 30; ++i) {
    const auto id = i % 2 ? QStringLiteral("timeline") : QStringLiteral("photo");
    auto& bytes = i % 2 ? timeline : photo;
    const auto result = tasks.invoke(id, "native.edit", {{"step", i}}, [&]() {
      bytes.append(QByteArray::number(i)); return QVariantMap{{"ok", true}};
    });
    CHECK(result.value("ok").toBool() && result.value("changed").toBool(), "Every interleaved mutation is checkpointed");
  }
  CHECK(!tasks.abandonAdmission().value("ok").toBool(), "Accepted operations cannot be abandoned");
  CHECK(tasks.adoptPreparedTask("mixed", {"photo"}).value("adopted").toBool() && tasks.journal()->taskId() == "mixed",
      "Prepared execution adopts exact completed checkpoints without replacing the creation journal");
  CHECK(!tasks.adoptPreparedTask("wrong-task", {"photo"}).value("ok").toBool() &&
      !tasks.adoptPreparedTask("mixed", {"unowned-target"}).value("ok").toBool(),
      "Prepared execution cannot adopt another task or an unowned target");
  const auto rejected = tasks.invoke("timeline", "native.reject", {}, []() {
    return QVariantMap{{"ok", false}, {"error", "Bad input"}};
  });
  CHECK(rejected.value("recoverableError").toBool() && rejected.value("mutationNotApplied").toBool(),
      "Unchanged synchronous document rejection can be corrected");
  const auto checkpoint = timeline;
  timeline.append(" manual edit");
  CHECK(!tasks.adoptPreparedTask("mixed", {"photo"}).value("ok").toBool(),
      "Manual changes in any created resource block adoption before execution");
  bool dispatched = false;
  CHECK(!tasks.finish("review").value("ok").toBool() && tasks.state().value("status") == "running",
      "A manual change after the last receipt cannot be declared review-ready");
  CHECK(!tasks.invoke("timeline", "native.edit", {}, [&]() {
    dispatched = true; return QVariantMap{{"ok", true}};
  }).value("ok").toBool() && !dispatched, "Manual state is never adopted on mutation admission");
  CHECK(!tasks.revert().value("ok").toBool() && restored == 0 && canceled == 0,
      "Conflict blocks the entire task before cancel or any partial restoration");
  timeline = checkpoint;
  CHECK(tasks.finish("review").value("ok").toBool(), "Review all resource receipts");
  photo.append(" manual revision conflict");
  CHECK(!tasks.continueTask().value("ok").toBool() && tasks.state().value("status") == "review",
      "Continuation never adopts a manual edit or changes task status on conflict");
  photo.chop(QByteArray(" manual revision conflict").size());
  CHECK(tasks.continueTask().value("ok").toBool() && tasks.journal()->taskId() == "mixed",
      "Continue the same task without recapturing its baselines");
  CHECK(tasks.invoke("photo", "native.revision", {}, [&]() {
    photo.append(" revised"); return QVariantMap{{"ok", true}};
  }).value("ok").toBool() && tasks.finish("review").value("ok").toBool(),
      "Revision appends a checkpoint to the original task");
  AgentTaskCoordinator recovered;
  CHECK(recovered.recover(root.path(), "mixed").value("ok").toBool(), "Recreate the owner from disk");
  CHECK(recovered.state().value("approvedSnapshot").toMap().value("answers").toMap().value("title") == "ABOVE THE CLOUDS",
      "Recovery retains the approved prompt and answers without replaying editing commands");
  CHECK(!recovered.revert().value("ok").toBool(), "Missing adapters cannot silently omit a document");
  CHECK(!recovered.finish("committed").value("ok").toBool(), "Unavailable recovered owners cannot be declared committed");
  CHECK(recovered.enroll("photo", adapter(photo)).value("ok").toBool() &&
      recovered.enroll("timeline", adapter(timeline)).value("ok").toBool(), "Reattach recovered native owners");
  CHECK(recovered.revert(true).value("ok").toBool() && restored == 0, "Preflight is read-only");
  CHECK(recovered.revert().value("ok").toBool() && restored == 2 && canceled == 2 &&
      photo == "original photo" && timeline == "original timeline", "Cancel every producer, then restore every exact baseline");
  CHECK(recovered.state().value("steps").toList().size() == 32, "Initial and revision history survive complete reversal");
  CHECK(recovered.begin(root.path(), "committed").value("ok").toBool() &&
      recovered.enroll("photo", adapter(photo)).value("ok").toBool(), "Begin a committed recovery fixture");
  CHECK(recovered.invoke("photo", "native.edit", {}, [&]() {
    photo = "committed photo"; return QVariantMap{{"ok", true}};
  }).value("ok").toBool() && recovered.finish("committed").value("ok").toBool(), "Commit a durably owned edit");
  AgentTaskCoordinator committed;
  CHECK(committed.recover(root.path(), "committed").value("ok").toBool() &&
      committed.enroll("photo", adapter(photo)).value("ok").toBool(), "An existing committed baseline can reattach for reversal");
  CHECK(!committed.enroll("new-resource", adapter(timeline)).value("ok").toBool(), "Recovery never adopts a new resource after completion");
  CHECK(!committed.continueTask().value("ok").toBool(), "A committed task cannot be silently reopened for edits");
  CHECK(committed.revert().value("ok").toBool() && photo == "original photo", "Recovered committed resources also restore exact baselines");
  AgentTaskCoordinator pending;
  CHECK(pending.begin(root.path(), "pending-revision").value("ok").toBool() &&
      pending.enroll("photo", adapter(photo)).value("ok").toBool(), "Admit pending continuation fixture");
  const auto prepared = pending.journal()->prepare("photo", "queued-work", {}, photo);
  CHECK(prepared.value("ok").toBool() && pending.journal()->complete(prepared.value("stepId").toLongLong(), photo,
      {{"ok", true}}, true).value("ok").toBool() &&
      !pending.adoptPreparedTask("pending-revision", {"photo"}).value("ok").toBool() &&
      pending.finish("interrupted").value("ok").toBool(), "Persist an accepted job and block prepared adoption until it settles");
  CHECK(!pending.continueTask().value("ok").toBool() && pending.state().value("status") == "interrupted",
      "An accepted job cannot be replaced by a revision");
  CHECK(pending.journal()->abandonPending("photo", "Job canceled").value("ok").toBool() &&
      pending.continueTask().value("ok").toBool(), "A reconciled interrupted task can continue safely");
  const auto uncertain = pending.journal()->prepare("photo", "unconfirmed-work", {}, photo);
  CHECK(uncertain.value("ok").toBool() && pending.finish("interrupted").value("ok").toBool() &&
      !pending.continueTask().value("ok").toBool(), "A prepared operation without a receipt blocks continuation");
  AgentTaskCoordinator uncertain_recovery;
  CHECK(uncertain_recovery.recover(root.path(), "pending-revision").value("ok").toBool() &&
      uncertain_recovery.enroll("photo", adapter(photo)).value("ok").toBool() &&
      !uncertain_recovery.continueTask().value("ok").toBool(), "Recovery never replays an uncertain operation");
  AgentTaskCoordinator failed_write;
  CHECK(failed_write.begin(root.path(), "continuation-write-failure").value("ok").toBool() &&
      failed_write.enroll("photo", adapter(photo)).value("ok").toBool() && failed_write.finish("review").value("ok").toBool(),
      "Prepare durable continuation write failure");
  const auto journal_path = failed_write.journal()->journalPath();
  CHECK(QFile::rename(journal_path, journal_path + ".preserved") && QDir().mkpath(journal_path), "Inject a journal destination failure");
  CHECK(!failed_write.continueTask().value("ok").toBool() && failed_write.state().value("status") == "review",
      "Failed persistence cannot leave the in-memory task running");
  QFile approval_file(QDir(root.path()).filePath("agent-tasks/mixed/journal.json"));
  CHECK(approval_file.open(QIODevice::ReadOnly), "Read the completed approval journal fixture");
  auto manifest = QJsonDocument::fromJson(approval_file.readAll()).object();
  approval_file.close();
  auto approval = manifest.value("approvedSnapshot").toObject();
  approval.insert("prompt", "Tampered approval");
  manifest.insert("approvedSnapshot", approval);
  const auto corrupt_bytes = QJsonDocument(manifest).toJson();
  CHECK(approval_file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
      approval_file.write(corrupt_bytes) == corrupt_bytes.size(), "Inject a changed approval without updating its checksum");
  approval_file.close();
  AgentTaskCoordinator corrupt_approval;
  CHECK(!corrupt_approval.recover(root.path(), "mixed").value("ok").toBool() && approval_file.open(QIODevice::ReadOnly) &&
      approval_file.readAll() == corrupt_bytes, "Untrusted recovered approval is rejected and all recovery bytes are preserved");

  QTemporaryDir large_output_root;
  CHECK(large_output_root.isValid(), "Create isolated large compound output storage");
  const auto large_output_directory = QDir(large_output_root.path()).filePath(QStringLiteral("outputs"));
  CHECK(QDir().mkpath(large_output_directory), "Create a canonical compound output directory");
  const auto canonical_large_output_directory = QFileInfo(large_output_directory).canonicalFilePath();
  const auto large_child_id = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
  const auto large_output_path = QDir(canonical_large_output_directory).filePath(
      QStringLiteral("compound-%1.mov").arg(large_child_id));
  const auto large_output = AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(large_output_path);
  CHECK(!large_output.resource_id.isEmpty() && large_output.canonical_path == large_output_path &&
      large_output.absent_state == QByteArrayLiteral("agent-task-owned-file-v1:absent") &&
      large_output.adapter.read() == large_output.absent_state,
      "Compound output ownership begins with an exact absent baseline");
  AgentTaskCoordinator large_output_tasks;
  CHECK(large_output_tasks.begin(large_output_root.path(), "large-compound-output").value("ok").toBool() &&
      large_output_tasks.enroll(large_output.resource_id, large_output.adapter).value("ok").toBool(),
      "Journal the absent output before producing a large render");
  constexpr qint64 large_output_bytes = 64LL * 1024LL * 1024LL + 1LL;
  QFile large_file(large_output_path);
  CHECK(large_file.open(QIODevice::ReadWrite) && large_file.resize(large_output_bytes),
      "Create a sparse file larger than 64 MiB without loading it into memory");
  large_file.close();
  auto large_descriptor = large_output.adapter.read();
  const auto expected_size_prefix = QByteArrayLiteral("agent-task-owned-file-sha256-v1\n") +
      QByteArray::number(large_output_bytes) + '\n';
  CHECK(large_descriptor.startsWith(expected_size_prefix) && large_descriptor.size() < 128,
      "Large output checkpoints store only size and a streamed SHA-256 descriptor");
  QFile independently_hashed_file(large_output_path);
  CHECK(independently_hashed_file.open(QIODevice::ReadOnly), "Read the large output in bounded chunks");
  QCryptographicHash independent_hash(QCryptographicHash::Sha256);
  qint64 independently_read = 0;
  while (!independently_hashed_file.atEnd()) {
    const auto block = independently_hashed_file.read(1024 * 1024);
    CHECK(!block.isEmpty() || independently_hashed_file.error() == QFileDevice::NoError,
        "Stream every large output byte for the independent checksum");
    if (block.isEmpty()) break;
    independently_read += block.size();
    independent_hash.addData(block);
  }
  CHECK(independently_read == large_output_bytes &&
      large_descriptor == expected_size_prefix + independent_hash.result().toHex(),
      "The compact output descriptor hashes the complete file contents");
  CHECK(large_output_tasks.checkpointOwnedResource(large_output.resource_id).value("ok").toBool(),
      "Checkpoint the finished large render by streamed fingerprint");
  QFile edited_large_file(large_output_path);
  CHECK(edited_large_file.open(QIODevice::ReadWrite) && edited_large_file.seek(17) &&
      edited_large_file.write("x", 1) == 1 && edited_large_file.flush(),
      "Change one byte without changing the large output size");
  CHECK(!large_output_tasks.revert(true).value("ok").toBool() &&
      QFileInfo::exists(large_output_path),
      "A same-size external change blocks absent-baseline cleanup");
  CHECK(edited_large_file.seek(17) && edited_large_file.write("\0", 1) == 1 &&
      edited_large_file.flush(), "Restore the exact checkpointed large file bytes");
  CHECK(large_output_tasks.revert().value("ok").toBool() &&
      !QFileInfo::exists(large_output_path),
      "Revert removes a large output only when its current streamed hash matches the last checkpoint");

  QTemporaryDir native_task_root;
  CHECK(native_task_root.isValid(), "Create isolated native Timeline task storage");
  const auto source_path = QDir(native_task_root.path()).filePath(QStringLiteral("source.mov"));
  QFile source(source_path);
  CHECK(source.open(QIODevice::WriteOnly) && source.write("source") == 6, "Create native task source fixture");
  source.close();
  const auto output_directory = QDir(native_task_root.path()).filePath(QStringLiteral("exports"));
  CHECK(QDir().mkpath(output_directory), "Create scoped project output directory");
  const auto canonical_source_path = QFileInfo(source_path).canonicalFilePath();
  const auto canonical_output_directory = QFileInfo(output_directory).canonicalFilePath();
  const auto project_path = QDir(canonical_output_directory).filePath(QStringLiteral("owned.looksy"));
  AgentTaskCoordinator native_task;
  CHECK(native_task.begin(native_task_root.path(), "native-timeline").value("ok").toBool() &&
      native_task.journal()->recordNativeTimelineScope({"timeline:stable-1"}, {canonical_source_path},
          canonical_output_directory).value("ok").toBool(), "Persist exact Timeline target and canonical path scope");
  auto owned_file = AgentTaskCoordinator::taskOwnedFileResource(project_path);
  QByteArray timeline_document = "before";
  CHECK(!owned_file.resource_id.isEmpty() && owned_file.canonical_path == project_path &&
      native_task.enroll("timeline:stable-1", {
          [&timeline_document]() { return timeline_document; },
          [&timeline_document](const QByteArray& bytes) { timeline_document = bytes; return true; }, {}}).value("ok").toBool() &&
      native_task.enroll(owned_file.resource_id, owned_file.adapter).value("ok").toBool(),
      "Capture both the native Timeline and absent output path before authoring");
  CHECK(QFile::exists(canonical_source_path) &&
      native_task.journal()->nativeTimelineScope().value("targetIds").toStringList() == QStringList{"timeline:stable-1"} &&
      native_task.journal()->nativeTimelineScope().value("assetPaths").toStringList() == QStringList{canonical_source_path} &&
      !native_task.journal()->recordNativeTimelineScope({"timeline:other"}, {}, canonical_output_directory).value("ok").toBool(),
      "Scope is durable, explicit, and immutable after admission");
  QFile project(project_path);
  CHECK(project.open(QIODevice::WriteOnly) && project.write("task project") == 12, "Create a task-owned project output");
  project.close();
  CHECK(native_task.checkpointOwnedResource(owned_file.resource_id).value("ok").toBool() &&
      native_task.journal()->recordOwnedOutputPath(project_path).value("ok").toBool() &&
      native_task.finish("review").value("ok").toBool(), "Checkpoint the created project path under the task");
  AgentTaskCoordinator recovered_native_task;
  CHECK(recovered_native_task.recover(native_task_root.path(), "native-timeline").value("ok").toBool(),
      "Recover the native task scope from disk");
  auto recovered_file = AgentTaskCoordinator::taskOwnedFileResource(project_path);
  CHECK(recovered_file.resource_id == owned_file.resource_id &&
      recovered_native_task.enroll("timeline:stable-1", {
          [&timeline_document]() { return timeline_document; },
          [&timeline_document](const QByteArray& bytes) { timeline_document = bytes; return true; }, {}}).value("ok").toBool() &&
      recovered_native_task.enroll(recovered_file.resource_id, recovered_file.adapter).value("ok").toBool() &&
      recovered_native_task.revert().value("ok").toBool() && !QFileInfo::exists(project_path) &&
      timeline_document == "before", "Revert deletes only the output captured as absent and restores the Timeline baseline");
  const auto native_journal_path = QDir(native_task_root.path()).filePath(
      QStringLiteral("agent-tasks/native-timeline/journal.json"));
  QFile native_journal(native_journal_path);
  CHECK(native_journal.open(QIODevice::ReadOnly), "Read native Timeline task journal scope");
  auto native_manifest = QJsonDocument::fromJson(native_journal.readAll()).object();
  native_journal.close();
  auto native_scope = native_manifest.value(QStringLiteral("nativeTimelineScope")).toObject();
  native_scope.insert(QStringLiteral("outputDirectory"), native_task_root.path());
  native_manifest.insert(QStringLiteral("nativeTimelineScope"), native_scope);
  const auto tampered_scope_bytes = QJsonDocument(native_manifest).toJson(QJsonDocument::Compact);
  CHECK(native_journal.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
      native_journal.write(tampered_scope_bytes) == tampered_scope_bytes.size(), "Tamper with native task path scope");
  native_journal.close();
  AgentTaskCoordinator rejected_scope;
  CHECK(!rejected_scope.recover(native_task_root.path(), "native-timeline").value("ok").toBool() &&
      native_journal.open(QIODevice::ReadOnly) && native_journal.readAll() == tampered_scope_bytes,
      "Recovery rejects altered Timeline paths and preserves the journal bytes");
  QTemporaryDir batch_root;
  CHECK(batch_root.isValid(), "Create isolated draft batch journal storage");
  QByteArray batch_document = "batch-before";
  int batch_adapter_reads = 0;
  const auto batch_adapter = AgentTaskJournal::ResourceAdapter{
      [&batch_document, &batch_adapter_reads]() {
        ++batch_adapter_reads;
        return batch_document;
      },
      [&batch_document](const QByteArray& bytes) { batch_document = bytes; return true; }, {}};
  AgentTaskCoordinator batch_tasks;
  CHECK(batch_tasks.begin(batch_root.path(), "batch-idempotency").value("ok").toBool() &&
      batch_tasks.enroll("timeline:batch", batch_adapter).value("ok").toBool(),
      "Enroll the exact Timeline baseline before a draft batch");
  CHECK(batch_adapter_reads == 1, "Enrollment captures one baseline read before the first batch");
  const QVariantMap batch_values{
      {QStringLiteral("batchId"), QStringLiteral("11111111-1111-4111-8111-111111111111")},
      {QStringLiteral("operations"), QVariantList{QVariantMap{
          {QStringLiteral("id"), QStringLiteral("track")},
          {QStringLiteral("commandId"), QStringLiteral("timeline.action.createTimelineTrack")}}}}};
  const auto batch_id = batch_values.value(QStringLiteral("batchId")).toString();
  const auto batch_hash = QString(64, QLatin1Char('a'));
  int batch_native_calls = 0;
  QByteArray batch_preimage_seen;
  const auto batch_invocation = [&](const QByteArray& preimage) {
    ++batch_native_calls;
    batch_preimage_seen = preimage;
    batch_document = "batch-after";
    return QVariantMap{{QStringLiteral("ok"), true},
        {QStringLiteral("batchId"), batch_id},
        {QStringLiteral("batchRequestSha256"), batch_hash},
        {QStringLiteral("batchResults"), QVariantList{QVariantMap{
            {QStringLiteral("id"), QStringLiteral("track")},
            {QStringLiteral("trackId"), QStringLiteral("track-stable")}}}},
        {QStringLiteral("operationCount"), 1}};
  };
  const auto batch_once = batch_tasks.invokeIdempotentBatchWithPreimage("timeline:batch",
      "timeline.action.applyDraftBatch", batch_id, batch_hash, batch_values, batch_invocation);
  CHECK(batch_once.value("ok").toBool() && batch_once.value("batchResults").toList().size() == 1 &&
      batch_native_calls == 1 && batch_preimage_seen == QByteArrayLiteral("batch-before") &&
      batch_adapter_reads == 3,
      "The coordinator forwards its exact prepared preimage and performs only its outer before/after reads");
  const auto batch_replay = batch_tasks.invokeIdempotentBatchWithPreimage("timeline:batch",
      "timeline.action.applyDraftBatch", batch_id, batch_hash, batch_values, batch_invocation);
  CHECK(batch_replay.value("ok").toBool() && batch_replay.value("idempotentReplay").toBool() &&
      batch_replay.value("batchResults").toList().first().toMap().value("trackId") == "track-stable" &&
      batch_native_calls == 1 && batch_adapter_reads == 4,
      "A same-ID retry verifies the checkpoint with one read and does not repeat native actions");
  batch_document.append(" external edit");
  CHECK(!batch_tasks.invokeIdempotentBatchWithPreimage("timeline:batch", "timeline.action.applyDraftBatch",
      batch_id, batch_hash, batch_values, batch_invocation).value("ok").toBool() &&
      batch_native_calls == 1 && batch_adapter_reads == 5,
      "Idempotent replay refuses an externally changed checkpoint before invoking native actions");
  const auto stale_batch_id = QStringLiteral("22222222-2222-4222-8222-222222222222");
  auto stale_batch_values = batch_values;
  stale_batch_values.insert(QStringLiteral("batchId"), stale_batch_id);
  int stale_batch_native_calls = 0;
  const auto stale_batch_invocation = [&](const QByteArray&) {
    ++stale_batch_native_calls;
    return QVariantMap{{QStringLiteral("ok"), true}};
  };
  CHECK(!batch_tasks.invokeIdempotentBatchWithPreimage("timeline:batch",
      "timeline.action.applyDraftBatch", stale_batch_id, batch_hash,
      stale_batch_values, stale_batch_invocation).value("ok").toBool() &&
      stale_batch_native_calls == 0 && batch_adapter_reads == 6,
      "A failed stale-checkpoint prepare never dispatches its preimage callback");
  batch_document = "batch-after";
  CHECK(!batch_tasks.invokeIdempotentBatchWithPreimage("timeline:batch", "timeline.action.applyDraftBatch",
      batch_id, QString(64, QLatin1Char('b')), batch_values, batch_invocation).value("ok").toBool() &&
      batch_native_calls == 1, "A batch ID reused for different content is rejected without mutation");
  CHECK(!batch_tasks.invokeIdempotentBatchWithPreimage("other-resource", "timeline.action.applyDraftBatch",
      batch_id, batch_hash, batch_values, batch_invocation).value("ok").toBool() &&
      batch_native_calls == 1, "A batch receipt cannot replay under a different resource ID");

  const auto failed_batch_id = QStringLiteral("33333333-3333-4333-8333-333333333333");
  auto failed_batch_values = batch_values;
  failed_batch_values.insert(QStringLiteral("batchId"), failed_batch_id);
  const auto failed_batch_hash = QString(64, QLatin1Char('c'));
  int failed_batch_native_calls = 0;
  const auto failed_batch_invocation = [&]() {
    ++failed_batch_native_calls;
    return QVariantMap{{QStringLiteral("ok"), false},
        {QStringLiteral("rolledBack"), true},
        {QStringLiteral("batchResults"), QVariantList{QVariantMap{
            {QStringLiteral("id"), QStringLiteral("temporaryClip")},
            {QStringLiteral("createdClipId"), QStringLiteral("provisional-clip-id")},
            {QStringLiteral("ok"), true}}}}};
  };
  const auto failed_batch = batch_tasks.invokeIdempotentBatch("timeline:batch",
      "timeline.action.applyDraftBatch", failed_batch_id, failed_batch_hash,
      failed_batch_values, failed_batch_invocation);
  CHECK(!failed_batch.value("ok").toBool() && failed_batch.value("rolledBack").toBool() &&
      failed_batch_native_calls == 1, "Persist an explicitly rolled-back batch outcome");
  const auto failed_batch_replay = batch_tasks.invokeIdempotentBatch("timeline:batch",
      "timeline.action.applyDraftBatch", failed_batch_id, failed_batch_hash,
      failed_batch_values, failed_batch_invocation);
  CHECK(!failed_batch_replay.value("ok").toBool() &&
      failed_batch_replay.value("idempotentReplay").toBool() &&
      failed_batch_replay.value("batchResults").toList().first().toMap()
          .value("createdClipId").toString() == QStringLiteral("provisional-clip-id") &&
      failed_batch_native_calls == 1,
      "A failed batch replay returns its exact rollback receipt without re-invoking native edits");

  QTemporaryDir accepted_batch_root;
  CHECK(accepted_batch_root.isValid(), "Create accepted batch journal storage");
  QByteArray accepted_batch_document = "accepted-before";
  AgentTaskCoordinator accepted_batch;
  CHECK(accepted_batch.begin(accepted_batch_root.path(), "batch-accepted").value("ok").toBool() &&
      accepted_batch.enroll("timeline:accepted", {
          [&accepted_batch_document]() { return accepted_batch_document; },
          [&accepted_batch_document](const QByteArray& bytes) { accepted_batch_document = bytes; return true; }, {}})
          .value("ok").toBool(), "Enroll an accepted batch resource before producer launch");
  const auto accepted_batch_id = QStringLiteral("44444444-4444-4444-8444-444444444444");
  auto accepted_batch_arguments = batch_values;
  accepted_batch_arguments.insert(QStringLiteral("batchId"), accepted_batch_id);
  accepted_batch_arguments.insert(QStringLiteral("_batchId"), accepted_batch_id);
  accepted_batch_arguments.insert(QStringLiteral("_batchRequestSha256"), batch_hash);
  const auto accepted_batch_step = accepted_batch.journal()->prepare(
      "timeline:accepted", "timeline.action.applyDraftBatch", accepted_batch_arguments,
      accepted_batch_document);
  CHECK(accepted_batch_step.value("ok").toBool() &&
      accepted_batch.journal()->complete(accepted_batch_step.value("stepId").toLongLong(),
          accepted_batch_document, {{"ok", true}, {"batchResults", QVariantList{}}}, true)
          .value("ok").toBool(), "Persist an accepted but unsettled batch receipt");
  int accepted_batch_native_calls = 0;
  const auto accepted_batch_replay = accepted_batch.invokeIdempotentBatch(
      "timeline:accepted", "timeline.action.applyDraftBatch", accepted_batch_id,
      batch_hash, accepted_batch_arguments, [&]() {
        ++accepted_batch_native_calls;
        return QVariantMap{{"ok", true}};
      });
  CHECK(!accepted_batch_replay.value("ok").toBool() && accepted_batch_native_calls == 0 &&
      accepted_batch_replay.value("error").toString().contains(QStringLiteral("will not be replayed")),
      "An accepted pending batch stays fail-closed and is never replayed");

  CHECK(batch_tasks.finish("review").value("ok").toBool(), "Review the batch task before a task-switch replay probe");
  CHECK(!batch_tasks.invokeIdempotentBatchWithPreimage("timeline:batch", "timeline.action.applyDraftBatch",
      batch_id, batch_hash, batch_values, batch_invocation).value("ok").toBool() &&
      batch_native_calls == 1, "A completed task cannot replay a prior batch after the task status changes");

  QTemporaryDir switched_batch_root;
  CHECK(switched_batch_root.isValid(), "Create a separate task for a batch-ID scope probe");
  QByteArray switched_batch_document = "second-task-before";
  AgentTaskCoordinator switched_batch;
  CHECK(switched_batch.begin(switched_batch_root.path(), "batch-switched").value("ok").toBool() &&
      switched_batch.enroll("timeline:batch", {
          [&switched_batch_document]() { return switched_batch_document; },
          [&switched_batch_document](const QByteArray& bytes) { switched_batch_document = bytes; return true; }, {}})
          .value("ok").toBool(), "Enroll a distinct task with the same stable resource ID");
  int switched_batch_native_calls = 0;
  const auto switched_batch_result = switched_batch.invokeIdempotentBatch(
      "timeline:batch", "timeline.action.applyDraftBatch", batch_id, batch_hash,
      batch_values, [&]() {
        ++switched_batch_native_calls;
        switched_batch_document = "second-task-after";
        return QVariantMap{{"ok", true}, {"batchResults", QVariantList{QVariantMap{
            {"id", "track"}, {"trackId", "second-task-track"}}}}};
      });
  CHECK(switched_batch_result.value("ok").toBool() &&
      !switched_batch_result.value("idempotentReplay").toBool() &&
      switched_batch_result.value("batchResults").toList().first().toMap()
          .value("trackId").toString() == "second-task-track" &&
      switched_batch_native_calls == 1,
      "The same batch ID in another task is a new invocation, never a cross-task replay");

  QTemporaryDir interrupted_batch_root;
  CHECK(interrupted_batch_root.isValid(), "Create interrupted batch recovery storage");
  QByteArray interrupted_batch_document = "interrupted-before";
  AgentTaskCoordinator interrupted_batch;
  CHECK(interrupted_batch.begin(interrupted_batch_root.path(), "batch-interrupted").value("ok").toBool() &&
      interrupted_batch.enroll("timeline:interrupted", {
          [&interrupted_batch_document]() { return interrupted_batch_document; },
          [&interrupted_batch_document](const QByteArray& bytes) { interrupted_batch_document = bytes; return true; }, {}})
          .value("ok").toBool(), "Prepare an interrupted batch owner");
  QVariantMap interrupted_batch_arguments = batch_values;
  interrupted_batch_arguments.insert(QStringLiteral("_batchId"),
      QStringLiteral("22222222-2222-4222-8222-222222222222"));
  interrupted_batch_arguments.insert(QStringLiteral("_batchRequestSha256"), batch_hash);
  const auto interrupted_prepared = interrupted_batch.journal()->prepare(
      "timeline:interrupted", "timeline.action.applyDraftBatch", interrupted_batch_arguments,
      interrupted_batch_document);
  CHECK(interrupted_prepared.value("ok").toBool(), "Persist the batch intent before native calls");
  AgentTaskCoordinator interrupted_batch_recovery;
  CHECK(interrupted_batch_recovery.recover(interrupted_batch_root.path(), "batch-interrupted").value("ok").toBool() &&
      interrupted_batch_recovery.enroll("timeline:interrupted", {
          [&interrupted_batch_document]() { return interrupted_batch_document; },
          [&interrupted_batch_document](const QByteArray& bytes) { interrupted_batch_document = bytes; return true; }, {}})
          .value("ok").toBool(), "Recover the unresolved batch without replaying it");
  int uncertain_batch_calls = 0;
  const auto unresolved_replay = interrupted_batch_recovery.invokeIdempotentBatch(
      "timeline:interrupted", "timeline.action.applyDraftBatch",
      QStringLiteral("22222222-2222-4222-8222-222222222222"), batch_hash, batch_values,
      [&uncertain_batch_calls]() { ++uncertain_batch_calls; return QVariantMap{{QStringLiteral("ok"), true}}; });
  CHECK(!unresolved_replay.value("ok").toBool() && uncertain_batch_calls == 0 &&
      unresolved_replay.value("error").toString().contains(QStringLiteral("will not be replayed")),
      "An unresolved prepared batch remains fail-closed after restart");

  const auto make_synchronous_batch_values = [](const QString& id, int operation_count,
                                                 const QString& command_id) {
    QVariantList operations;
    for (int index = 0; index < operation_count; ++index) {
      operations.append(QVariantMap{
          {QStringLiteral("id"), QStringLiteral("op%1").arg(index)},
          {QStringLiteral("commandId"), command_id},
          {QStringLiteral("values"), QVariantMap{{QStringLiteral("fixtureIndex"), index}}}});
    }
    return QVariantMap{{QStringLiteral("batchId"), id},
                       {QStringLiteral("operations"), operations}};
  };
  const auto request_sha_for = [](const QVariantMap& values) {
    const auto bytes = QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  };
  const auto prepared_arguments_for = [&](const QVariantMap& values) {
    auto arguments = values;
    arguments.insert(QStringLiteral("_batchId"), values.value(QStringLiteral("batchId")));
    arguments.insert(QStringLiteral("_batchRequestSha256"), request_sha_for(values));
    return arguments;
  };

  QTemporaryDir unchanged_recovery_root;
  CHECK(unchanged_recovery_root.isValid(), "Create unchanged-batch recovery fixture storage");
  const auto unchanged_output_directory = QDir(unchanged_recovery_root.path()).filePath("outputs");
  CHECK(QDir().mkpath(unchanged_output_directory), "Create admitted P4 output directory");
  const auto canonical_unchanged_output_directory =
      QFileInfo(unchanged_output_directory).canonicalFilePath();
  const auto p4_save_path = QDir(canonical_unchanged_output_directory).filePath("looksy-p4.json");
  const auto p4_save_resource =
      AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(p4_save_path);
  QByteArray unchanged_timeline = "P3 baseline";
  int completed_native_batches = 0;
  AgentTaskCoordinator prepared_recovery_task;
  CHECK(!p4_save_resource.resource_id.isEmpty() &&
      prepared_recovery_task.begin(unchanged_recovery_root.path(), "prepared-batch-unchanged").value("ok").toBool() &&
      prepared_recovery_task.journal()->recordNativeTimelineScope(
          {"timeline:p4"}, {}, canonical_unchanged_output_directory).value("ok").toBool() &&
      prepared_recovery_task.journal()->recordOwnedOutputPath(p4_save_path).value("ok").toBool() &&
      prepared_recovery_task.enroll("timeline:p4", {
          [&unchanged_timeline]() { return unchanged_timeline; },
          [&unchanged_timeline](const QByteArray& bytes) { unchanged_timeline = bytes; return true; }, {}})
          .value("ok").toBool() &&
      prepared_recovery_task.enroll(p4_save_resource.resource_id, p4_save_resource.adapter)
          .value("ok").toBool(),
      "Admit both the native Timeline and its task-owned P4 output file");
  const auto completed_twelve_op_values = make_synchronous_batch_values(
      QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"), 12,
      QStringLiteral("timeline.action.setTimelineClipAnimation"));
  const auto completed_twelve_op_result = prepared_recovery_task.invokeIdempotentBatch(
      "timeline:p4", "timeline.action.applyDraftBatch",
      completed_twelve_op_values.value(QStringLiteral("batchId")).toString(),
      request_sha_for(completed_twelve_op_values), completed_twelve_op_values, [&]() {
        ++completed_native_batches;
        unchanged_timeline = "P4 after twelve native operations";
        return QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("operationCount"), 12},
            {QStringLiteral("batchResults"), QVariantList{}}};
      });
  CHECK(completed_twelve_op_result.value("ok").toBool() && completed_native_batches == 1,
      "Checkpoint the completed twelve-operation P4 batch before recovery");
  const auto save_project_result = prepared_recovery_task.invoke(
      p4_save_resource.resource_id, "timeline.action.saveTimeline", {}, [&]() {
        QFile file(p4_save_path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                       file.write("saved P4 checkpoint") == 19
                   ? QVariantMap{{QStringLiteral("ok"), true}}
                   : QVariantMap{{QStringLiteral("ok"), false}};
      });
  CHECK(save_project_result.value("ok").toBool(), "Checkpoint the task-owned P4 save file");
  const auto note_result = prepared_recovery_task.invoke(
      "timeline:p4", "timeline.action.createReviewNote", {}, [&]() {
        unchanged_timeline.append(" + review note");
        return QVariantMap{{QStringLiteral("ok"), true}};
      });
  CHECK(note_result.value("ok").toBool(), "Checkpoint the accepted P4 review note");
  const auto interrupted_two_op_values = make_synchronous_batch_values(
      QStringLiteral("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"), 2,
      QStringLiteral("timeline.action.setTimelineClipFrame"));
  const auto interrupted_two_op_step = prepared_recovery_task.journal()->prepare(
      "timeline:p4", "timeline.action.applyDraftBatch",
      prepared_arguments_for(interrupted_two_op_values), unchanged_timeline);
  CHECK(interrupted_two_op_step.value("ok").toBool(),
      "Persist the two-operation frame batch before simulating a process crash");

  AgentTaskCoordinator unchanged_recovery;
  CHECK(unchanged_recovery.recover(unchanged_recovery_root.path(), "prepared-batch-unchanged")
            .value("ok").toBool() &&
      unchanged_recovery.enroll("timeline:p4", {
          [&unchanged_timeline]() { return unchanged_timeline; },
          [&unchanged_timeline](const QByteArray& bytes) { unchanged_timeline = bytes; return true; }, {}})
          .value("ok").toBool() &&
      unchanged_recovery.enroll(p4_save_resource.resource_id,
          AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(p4_save_path).adapter)
          .value("ok").toBool(),
      "Recover and re-enroll the exact Timeline plus admitted P4 file adapter");
  const auto recovered_unchanged =
      unchanged_recovery.reconcilePreparedUnchangedTimelineDraftBatch("timeline:p4");
  const auto unchanged_recovery_steps = unchanged_recovery.state().value("steps").toList();
  const auto recovered_batch_step = unchanged_recovery_steps.last().toMap();
  const auto recovered_batch_outcome = recovered_batch_step.value("outcome").toMap();
  CHECK(recovered_unchanged.value("ok").toBool() &&
      recovered_unchanged.value("recoveredUnchanged").toBool() &&
      recovered_unchanged.value("mutationNotApplied").toBool() &&
      recovered_batch_step.value("status").toString() == QStringLiteral("failed") &&
      recovered_batch_outcome.value("mutationNotApplied").toBool() &&
      recovered_batch_outcome.value("recoveredUnchanged").toBool() &&
      !unchanged_recovery.state().contains(QStringLiteral("uncertainOperation")) &&
      unchanged_timeline == "P4 after twelve native operations + review note" &&
      completed_native_batches == 1,
      "Resolve an unchanged prepared batch as not applied without replaying or reverting accepted P4 work");
  QFile saved_p4_file(p4_save_path);
  CHECK(saved_p4_file.open(QIODevice::ReadOnly) && saved_p4_file.readAll() == "saved P4 checkpoint" &&
      unchanged_recovery.continueTask().value("ok").toBool() && completed_native_batches == 1,
      "Preserve the owned P4 file and continue the same task after an explicit unchanged receipt");

  const auto rejects_unsafe_batch_recovery = [&](const QString& task_id,
                                                  const QString& defect) {
    QTemporaryDir refusal_root;
    if (!refusal_root.isValid()) return false;
    const auto output_directory = QDir(refusal_root.path()).filePath("outputs");
    if (!QDir().mkpath(output_directory)) return false;
    const auto canonical_output_directory = QFileInfo(output_directory).canonicalFilePath();
    const auto output_path = QDir(canonical_output_directory).filePath("looksy-p4.json");
    const auto output_resource = AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(output_path);
    QByteArray document = "checkpoint P4";
    AgentTaskCoordinator task;
    if (output_resource.resource_id.isEmpty() ||
        !task.begin(refusal_root.path(), task_id).value("ok").toBool() ||
        !task.journal()->recordNativeTimelineScope(
            {"timeline:refusal"}, {}, canonical_output_directory).value("ok").toBool() ||
        !task.journal()->recordOwnedOutputPath(output_path).value("ok").toBool() ||
        !task.enroll("timeline:refusal", {
            [&document]() { return document; },
            [&document](const QByteArray& bytes) { document = bytes; return true; }, {}})
             .value("ok").toBool() ||
        !task.enroll(output_resource.resource_id, output_resource.adapter).value("ok").toBool()) {
      return false;
    }
    const auto batch_id = QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc");
    const auto command_id = defect == QStringLiteral("unsupported-operation")
                                ? QStringLiteral("timeline.action.createTimelineCompound")
                                : QStringLiteral("timeline.action.setTimelineClipTitle");
    const auto values = make_synchronous_batch_values(batch_id, 1, command_id);
    if (defect == QStringLiteral("multiple-prepared")) {
      const auto first_values = make_synchronous_batch_values(
          QStringLiteral("dddddddd-dddd-4ddd-8ddd-dddddddddddd"), 1,
          QStringLiteral("timeline.action.setTimelineClipTitle"));
      if (!task.journal()->prepare("timeline:refusal", "timeline.action.applyDraftBatch",
              prepared_arguments_for(first_values), document).value("ok").toBool()) return false;
    }
    if (defect == QStringLiteral("accepted-file-operation")) {
      const auto file_state = output_resource.adapter.read();
      const auto file_step = task.journal()->prepare(
          output_resource.resource_id, "timeline.outputFileJob", {}, file_state);
      if (!file_step.value("ok").toBool() ||
          !task.journal()->complete(file_step.value("stepId").toLongLong(), file_state,
              {{QStringLiteral("ok"), true}}, true).value("ok").toBool()) return false;
    }
    const auto prepared = task.journal()->prepare("timeline:refusal",
        "timeline.action.applyDraftBatch", prepared_arguments_for(values), document);
    if (!prepared.value("ok").toBool()) return false;
    if (defect == QStringLiteral("multiple-prepared")) {
      const auto second_values = make_synchronous_batch_values(
          QStringLiteral("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"), 1,
          QStringLiteral("timeline.action.setTimelineClipTitle"));
      if (!task.journal()->prepare("timeline:refusal", "timeline.action.applyDraftBatch",
              prepared_arguments_for(second_values), document).value("ok").toBool()) return false;
    }
    if (defect == QStringLiteral("changed-timeline")) document.append(" external change");
    if (defect == QStringLiteral("changed-output")) {
      QFile file(output_path);
      if (!file.open(QIODevice::WriteOnly) || file.write("outside task") != 12) return false;
    }
    AgentTaskCoordinator recovered;
    if (!recovered.recover(refusal_root.path(), task_id).value("ok").toBool() ||
        !recovered.enroll("timeline:refusal", {
            [&document]() { return document; },
            [&document](const QByteArray& bytes) { document = bytes; return true; }, {}})
             .value("ok").toBool() ||
        !recovered.enroll(output_resource.resource_id,
            AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(output_path).adapter)
             .value("ok").toBool()) return false;
    const auto resolution = recovered.reconcilePreparedUnchangedTimelineDraftBatch("timeline:refusal");
    if (resolution.value("ok").toBool()) return false;
    const auto steps_after_refusal = recovered.state().value("steps").toList();
    auto prepared_after_refusal = 0;
    for (const auto& value : steps_after_refusal) {
      if (value.toMap().value("status").toString() == QStringLiteral("prepared"))
        ++prepared_after_refusal;
    }
    return prepared_after_refusal > 0 &&
        recovered.state().value("uncertainOperation").toBool();
  };
  CHECK(rejects_unsafe_batch_recovery("changed-timeline-batch", "changed-timeline") &&
      rejects_unsafe_batch_recovery("changed-output-batch", "changed-output") &&
      rejects_unsafe_batch_recovery("unsupported-batch-command", "unsupported-operation") &&
      rejects_unsafe_batch_recovery("multiple-prepared-batches", "multiple-prepared") &&
      rejects_unsafe_batch_recovery("accepted-file-pending-batch", "accepted-file-operation"),
      "Changed resources, unsupported operations, multiple prepared steps, and pending file jobs stay unresolved");

  std::cout << "Shared ownership, interleaved edits, retained revision baselines, continuation guards, recovery and exact revert passed.\n";
  return 0;
}

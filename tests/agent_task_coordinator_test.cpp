#include "agent_task_coordinator.h"

#include <QCoreApplication>
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
  std::cout << "Shared ownership, interleaved edits, retained revision baselines, continuation guards, recovery and exact revert passed.\n";
  return 0;
}

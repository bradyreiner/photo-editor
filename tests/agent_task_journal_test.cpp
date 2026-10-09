#include "agent_task_journal.h"

#include <QCoreApplication>
#include <QFile>
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

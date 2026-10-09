#include "agent_created_file_resource.h"
#include "agent_task_coordinator.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

using namespace photo_editor::desktop;
#define CHECK(c, m) do { if (!(c)) { std::cerr << m << '\n'; return 1; } } while (false)

static bool writeFile(const QString& path, const QByteArray& bytes) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  const auto root = QDir(temporary.path()).filePath("documents");
  const auto path = QDir(root).filePath("owned/canvas.png");
  bool producer_running = false;
  int cancellations = 0;
  AgentCreatedFileResource file(root, path, [&] { ++cancellations; }, [&] { return !producer_running; });
  const auto absent = file.snapshot();
  CHECK(!absent.isEmpty(), "A planned absent file has a durable readable baseline");
  AgentTaskCoordinator task;
  CHECK(task.begin(temporary.path(), "created-output").value("ok").toBool() &&
      task.enroll("source", file.adapter()).value("ok").toBool(), "Capture absence before invoking the producer");
  const QByteArray pixels = "owned PNG bytes";
  const auto digest = QString::fromLatin1(QCryptographicHash::hash(pixels, QCryptographicHash::Sha256).toHex());
  producer_running = true;
  const auto prepared = task.journal()->prepare("source", "create.canvas", {}, absent);
  CHECK(prepared.value("ok").toBool() && task.journal()->complete(prepared.value("stepId").toLongLong(),
      absent, {{"ok", true}, {"jobId", "exact-producer"}}, true).value("ok").toBool(), "Persist acceptance before asynchronous work");
  CHECK(writeFile(path, pixels) && !file.recordCreatedReceipt(QString(64, '0')) && file.recordCreatedReceipt(digest),
      "Accept only the exact producer-authored checksum");
  CHECK(task.journal()->settle("source", file.snapshot(), true).value("ok").toBool(), "Persist actual completed file state");
  CHECK(!task.revert().value("ok").toBool() && QFile::exists(path) && cancellations == 1,
      "Cancellation request is not producer quiescence; never remove an active output");
  producer_running = false;
  CHECK(writeFile(path, "manual replacement") && !task.revert().value("ok").toBool() &&
      QFile::exists(path), "Later manual file changes block whole-task reversal");
  CHECK(writeFile(path, pixels), "Restore fixture's owned bytes");
  const auto unrelated = QFileInfo(path).dir().filePath("user.txt");
  CHECK(writeFile(unrelated, "keep"), "Create unrelated neighbor");
  CHECK(task.revert().value("ok").toBool() && !QFile::exists(path) && QFile::exists(unrelated) && file.snapshot() == absent,
      "Reversal removes only its exact output and preserves neighbors");

  const auto existing = QDir(root).filePath("existing.png");
  CHECK(writeFile(existing, "existing"), "Create existing output");
  AgentCreatedFileResource other(root, existing, {}, [] { return true; });
  CHECK(!other.adapter().restore(other.snapshot()) && QFile::exists(existing),
      "A preexisting file cannot be adopted as an absent creation baseline");
  AgentCreatedFileResource escaped(root, QDir(temporary.path()).filePath("outside.png"), {}, [] { return true; });
  CHECK(escaped.snapshot().isEmpty(), "Outside-root ownership is rejected");
  const auto link = QDir(root).filePath("redirect");
  CHECK(QFile::link(temporary.path(), link), "Create symlinked parent fixture");
  AgentCreatedFileResource redirected(root, QDir(link).filePath("outside.png"), {}, [] { return true; });
  CHECK(redirected.snapshot().isEmpty(), "Symlinked parent cannot redirect file ownership");

  // Recovery reattaches the owner using the producer checksum from its persisted
  // receipt; neither a new baseline nor an uncertain mutation is replayed.
  const auto recovery_path = QDir(root).filePath("recover/canvas.png");
  AgentCreatedFileResource first(root, recovery_path, {}, [] { return true; });
  AgentTaskCoordinator initial;
  CHECK(initial.begin(temporary.path(), "recover-output").value("ok").toBool() &&
      initial.enroll("source", first.adapter()).value("ok").toBool(), "Admit recoverable file");
  CHECK(initial.invoke("source", "create.canvas", {}, [&] {
    const auto ok = writeFile(recovery_path, pixels) && first.recordCreatedReceipt(digest);
    return QVariantMap{{"ok", ok}};
  }).value("ok").toBool() && initial.finish("review").value("ok").toBool(), "Checkpoint completed file");
  AgentCreatedFileResource reattached(root, recovery_path, {}, [] { return true; });
  AgentTaskCoordinator recovered;
  CHECK(recovered.recover(temporary.path(), "recover-output").value("ok").toBool() &&
      reattached.recordCreatedReceipt(digest) && recovered.enroll("source", reattached.adapter()).value("ok").toBool() &&
      recovered.revert().value("ok").toBool() && !QFile::exists(recovery_path), "Recovered exact output can be reversed without recreating it");
  std::cout << "Created file ownership, producer quiescence, conflicts and recovery passed.\n";
  return 0;
}

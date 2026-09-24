#include "editor_recipe_save_journal.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
QByteArray read(const QString& path) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) return {};
  return input.readAll();
}
bool independent_envelope_match(const QByteArray& bytes) {
  const auto document = QJsonDocument::fromJson(bytes);
  const auto envelope = document.object();
  const auto payload = QJsonDocument(envelope.value("payload").toObject()).toJson(QJsonDocument::Compact);
  return document.isObject() && document.toJson(QJsonDocument::Compact) == bytes &&
      envelope.value("sha256").toString().toLatin1() ==
          QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
}
bool legacy_checkpoint_roundtrip(const QByteArray& current_bytes) {
  const auto original = QJsonDocument::fromJson(current_bytes).object().value("payload").toObject();
  auto legacy = original;
  legacy.insert("schema", "photo-editor-recipe-outbox:v1");
  const auto legacy_snapshot = [](const QJsonValue& value) -> QJsonValue {
    if (value.isNull()) return value;
    auto object = value.toObject();
    object.insert("recipe", QString::fromUtf8(QJsonDocument(object.value("recipe").toObject()).toJson(QJsonDocument::Compact)));
    return object;
  };
  QJsonArray pending;
  for (const auto& value : legacy.value("pending").toArray()) pending.append(legacy_snapshot(value));
  legacy.insert("pending", pending);
  legacy.insert("active", legacy_snapshot(legacy.value("active")));
  legacy.insert("retryMigration", legacy_snapshot(legacy.value("retryMigration")));
  const auto payload_bytes = QJsonDocument(legacy).toJson(QJsonDocument::Compact);
  const auto bytes = QJsonDocument(QJsonObject{{"payload", legacy}, {"sha256", QString::fromLatin1(
      QCryptographicHash::hash(payload_bytes, QCryptographicHash::Sha256).toHex())}}).toJson(QJsonDocument::Compact);
  QTemporaryDir root;
  QString path, error;
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), "catalog-a", &recovered, &error)) return false;
    path = journal.path();
  }
  {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return false;
  }
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), "catalog-a", &recovered, &error) || !journal.write(recovered, &error)) return false;
    const auto upgraded = read(path);
    if (!independent_envelope_match(upgraded) ||
        QJsonDocument::fromJson(upgraded).object().value("payload").toObject() != original) return false;
  }
  EditorRecipeSaveJournal final;
  EditorRecipeSaveCheckpoint recovered;
  return final.open(root.path(), "catalog-a", &recovered, &error) && recovered.active && recovered.pending.size() == 2;
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc == 3 && app.arguments().at(1) == "--crash-writer") {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    QString error;
    if (!journal.open(app.arguments().at(2), "catalog-a", &recovered, &error)) return 2;
    EditorRecipeSaveQueue queue;
    EditorRecipeSaveSnapshot a{.asset_id = "a", .file_path = "/a.dng"};
    a.state.exposure = 1;
    a.migration_evidence.insert("migrationPending", true);
    a.migration_evidence.insert("approvalTime", "exact approval");
    a.migration_evidence.insert("unicodeProof", QString::fromUtf8("proof-λ-雪-\\-\"\n").repeated(65536));
    queue.enqueue(a);
    queue.begin();
    a.state.exposure = 2;
    a.migration_evidence.clear();
    queue.enqueue(a);
    auto b = a; b.asset_id = "b"; b.file_path = "/b.jpg";
    queue.enqueue(b);
    if (!journal.write(queue.checkpoint(), &error)) return 3;
    std::cout << "READY\n" << std::flush;
    return app.exec(); // Parent kills this exact process, without destructors.
  }
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  QProcess child;
  child.start(app.applicationFilePath(), {"--crash-writer", root.path()});
  if (!child.waitForStarted(3000) || !child.waitForReadyRead(10000) ||
      child.readAllStandardOutput().trimmed() != "READY") return 1;
  child.kill();
  if (!child.waitForFinished(3000) || child.exitStatus() != QProcess::CrashExit) return 1;
  QString path, other_path, error;
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!require(journal.open(root.path(), "catalog-a", &recovered, &error), error.toUtf8().constData())) return 1;
    path = journal.path();
    EditorRecipeSaveJournal second;
    EditorRecipeSaveCheckpoint untouched;
    if (!require(!second.open(root.path(), "catalog-a", &untouched, &error), "A concurrent writer acquired the journal")) return 1;
    EditorRecipeSaveJournal other_catalog;
    if (!other_catalog.open(root.path(), "catalog-b", &untouched, &error) ||
        !untouched.pending.isEmpty() || untouched.active) return 1;
    other_path = other_catalog.path();
    EditorRecipeSaveQueue queue;
    queue.restore(recovered);
    auto* active = queue.begin();
    if (!require(active && active->state.exposure == 1 && active->migration_evidence.value("approvalTime") == "exact approval",
                 "Crash lost the exact active migration approval")) return 1;
    queue.complete(true);
    active = queue.begin();
    if (!active || active->asset_id != "a" || active->state.exposure != 2 || !active->migration_evidence.isEmpty()) return 1;
    queue.complete(true);
    active = queue.begin();
    if (!active || active->asset_id != "b") return 1;
    queue.complete(true);
    const auto previous = read(path);
    if (!independent_envelope_match(previous)) return 1;
    if (!legacy_checkpoint_roundtrip(previous)) return 1;
    const auto directory = QFileInfo(path).absolutePath();
    const auto held = directory + "-held";
    if (!QDir().rename(directory, held)) return 1;
    if (journal.write(queue.checkpoint(), &error) || error.isEmpty() ||
        read(QDir(held).filePath(QFileInfo(path).fileName())) != previous) return 1;
    if (!QDir().rename(held, directory) || !journal.write(queue.checkpoint(), &error)) return 1;
  }
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), "catalog-a", &recovered, &error) ||
        !recovered.pending.isEmpty() || recovered.active || recovered.retry_migration) return 1;
  }
  const auto valid = read(path);
  if (!independent_envelope_match(valid)) return 1;
  // Even if a file is moved to another catalog's namespace, its embedded
  // authority must reject replay rather than trusting the filename alone.
  if (!QFile::copy(path, other_path)) return 1;
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (journal.open(root.path(), "catalog-b", &recovered, &error) || read(other_path) != valid) return 1;
  }
  const auto reject_bytes = [&](const QByteArray& bytes) {
    {
      QSaveFile output(path);
      if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return false;
    }
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    return !journal.open(root.path(), "catalog-a", &recovered, &error) && !error.isEmpty() &&
        !journal.write({}, &error) && read(path) == bytes;
  };
  auto envelope = QJsonDocument::fromJson(valid).object();
  auto payload = envelope.value("payload").toObject();
  payload.insert("schema", "future-unsupported-version");
  envelope.insert("payload", payload);
  if (!reject_bytes(QJsonDocument(envelope).toJson(QJsonDocument::Compact))) return 1; // Invalid checksum.
  envelope.insert("sha256", QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument(payload).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()));
  if (!reject_bytes(QJsonDocument(envelope).toJson(QJsonDocument::Compact))) return 1; // Valid checksum, bad schema.
  {
    QSaveFile corrupt(path);
    if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("{broken") != 7 || !corrupt.commit()) return 1;
  }
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (journal.open(root.path(), "catalog-a", &recovered, &error) || error.isEmpty() ||
        journal.write({}, &error) || read(path) != "{broken") return 1;
  }
  if (valid.isEmpty()) return 1;
  std::cout << "Killed-writer recovery, exact migration ordering, catalog isolation, exclusive ownership, atomic failure retention, acknowledged removal and corrupt-file preservation passed.\n";
  return 0;
}

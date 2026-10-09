#include "job_output_transaction.h"
#include <QCoreApplication>
#include <iostream>

using photo_editor::desktop::JobOutputTransaction;
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

bool write(const QString& path, const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  CHECK(root.isValid(), "Temporary delivery root");
  const auto output = root.filePath("a.mov");
  {
    JobOutputTransaction delivery(output);
    CHECK(delivery.valid() && write(delivery.stagedPath("a.mov"), "movie") &&
        write(delivery.stagedPath("a-frames/001.png"), "frame"), "Stage a complete delivery");
    CHECK(!QFileInfo::exists(output), "No partial output is visible while work runs");
    CHECK(delivery.publish().value("ok").toBool() && read(output) == "movie", "Publish verified output without overwriting");
    CHECK(delivery.artifacts().size() == 2, "Capture file and image-sequence ownership receipts");
    CHECK(delivery.rollback().value("ok").toBool() && !QFileInfo::exists(output) &&
        !QFileInfo::exists(root.filePath("a-frames")), "Revoke the entire unmodified delivery");
  }
  {
    JobOutputTransaction delivery(output);
    CHECK(write(output, "manual") && write(delivery.stagedPath("a.mov"), "movie"), "Prepare collision");
    CHECK(!delivery.publish().value("ok").toBool() && read(output) == "manual", "Preserve existing delivery bytes");
    QFile::remove(output);
  }
  {
    JobOutputTransaction delivery(output);
    CHECK(write(delivery.stagedPath("a.mov"), "movie") && write(delivery.stagedPath("b.mov"), "extra"), "Stage cancellation fixture");
    CHECK(!delivery.publish([&]() { return QFileInfo::exists(output); }).value("ok").toBool() &&
        !QFileInfo::exists(output) && !QFileInfo::exists(root.filePath("b.mov")), "Cancel between publications and revoke completed artifacts");
  }
  {
    JobOutputTransaction delivery(output);
    CHECK(write(delivery.stagedPath("a.mov"), "movie") && write(delivery.stagedPath("b.mov"), "extra"), "Stage racing collision");
    CHECK(!delivery.publish([&]() {
      if (QFileInfo::exists(output)) write(root.filePath("b.mov"), "manual racing file");
      return false;
    }).value("ok").toBool() && !QFileInfo::exists(output) && read(root.filePath("b.mov")) == "manual racing file",
        "A collision after preflight never overwrites the new file");
    QFile::remove(root.filePath("b.mov"));
  }
  {
    JobOutputTransaction delivery(output);
    CHECK(write(delivery.stagedPath("a.mov"), "movie") && write(delivery.stagedPath("a-frames/001.png"), "frame"), "Stage manual-change fixture");
    CHECK(delivery.publish().value("ok").toBool(), "Publish before subsequent edits");
    CHECK(write(output, "later manual edit") && write(root.filePath("a-frames/002.png"), "later frame"), "Modify published artifacts independently");
    const auto reverted = delivery.rollback();
    CHECK(!reverted.value("ok").toBool() && reverted.value("preservedArtifacts").toStringList().size() == 2 &&
        read(output) == "later manual edit" && read(root.filePath("a-frames/002.png")) == "later frame",
        "Reversion preserves later file and directory edits");
  }
  std::cout << "Complete staging, collisions, cancellation and manual-change preservation passed.\n";
  return 0;
}

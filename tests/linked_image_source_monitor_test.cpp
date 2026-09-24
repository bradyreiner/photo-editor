#include "linked_image_source_monitor.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  QDir(root.path()).mkdir("sources");
  const auto path = root.filePath("sources/image.bin");
  auto write = [](const QString& target, const QByteArray& bytes) {
    QSaveFile file(target);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
  };
  if (!write(path, "before")) return 1;
  auto count = 0;
  QStringList changed;
  LinkedImageSourceMonitor monitor(nullptr, [&](const QStringList& paths) { ++count; changed = paths; });
  monitor.set_paths({path, path});
  auto wait = [&](const std::function<bool()>& done, int timeout = 4000) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < timeout) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
    return done();
  };
  auto settle = [&] { static_cast<void>(wait([] { return false; }, 250)); };
  for (auto i = 0; i < 8; ++i) {
    if (!write(path, QByteArray::number(i))) return 1;
  }
  if (!wait([&] { return count == 1; }) || changed != QStringList{path}) return 1;
  settle();
  if (count != 1 || monitor.paths().size() != 1) return 1;
  if (!write(root.filePath("sources/unrelated.bin"), "ignore")) return 1;
  settle();
  if (count != 1) return 1;
  if (!QFile::rename(path, root.filePath("sources/moved.bin"))) return 1;
  if (!wait([&] { return count == 2; })) return 1;
  if (!write(path, "restored")) return 1;
  if (!wait([&] { return count == 3; })) return 1;
  if (!QDir(root.path()).rename("sources", "held-sources")) return 1;
  if (!wait([&] { return count == 4; })) return 1;
  if (!QDir(root.path()).rename("held-sources", "sources")) return 1;
  if (!wait([&] { return count == 5; })) return 1;
  monitor.set_paths({});
  if (!write(path, "unwatched")) return 1;
  settle();
  if (count != 5 || !monitor.paths().isEmpty()) return 1;
  const auto absent = root.filePath("absent/deep/image.bin");
  monitor.set_paths({absent});
  if (!QDir().mkpath(root.filePath("absent/deep")) || !write(absent, "created")) return 1;
  if (!wait([&] { return count == 6 && changed == QStringList{absent}; })) return 1;
  // Resetting the document while a notification is pending must discard it.
  if (!write(absent, "pending")) return 1;
  monitor.set_paths({path});
  settle();
  if (count != 6) return 1;
  std::cout << "File replacement, coalescing, unrelated events, deletion/restoration, directory recovery and scope changes passed\n";
  return 0;
}

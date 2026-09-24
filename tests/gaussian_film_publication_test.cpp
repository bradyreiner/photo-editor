#include "gaussian_film_publication.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
bool write(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size() && file.flush();
}
QByteArray read(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!directory.isValid()) return 1;
  const QByteArray movie_bytes(1024 * 1024, 'm');
  const QByteArray receipt_bytes("{\"complete\":true}");
  for (int trial = 0; trial < 16; ++trial) {
    const int scenario = trial % 8;
    const bool photo = trial >= 8;
    const auto root = directory.filePath(QString::number(trial));
    if (!QDir().mkpath(root)) return 1;
    const auto movie = root + "/staged.mp4";
    const auto receipt = root + "/staged.json";
    const auto destination = root + "/final.mp4";
    const auto sidecar = destination + (photo ? ".camera.json" : ".camera-path.json");
    const auto sentinel = root + "/sentinel";
    if (!write(movie, movie_bytes) || !write(receipt, receipt_bytes) || !write(sentinel, "preserve")) return 1;
    // Cases 1-3 reproduce destinations appearing after export preflight and
    // force failure after receipt publication, exercising rollback.
    if (scenario == 1 && !write(destination, "existing movie")) return 1;
    if (scenario == 2 && !QFile::link(sentinel, destination)) return 1;
    if (scenario == 3 && !QDir().mkdir(destination)) return 1;
    if (scenario == 4 && !write(sidecar, "existing receipt")) return 1;
    if (scenario == 5 && !QFile::link(root + "/missing", sidecar)) return 1;
    if (scenario == 6 && !QFile::remove(movie)) return 1;
    if (scenario == 7) {
      if (!QFile::remove(receipt) || !write(receipt, {})) return 1;
    }
    const auto result = photo ? publish_gaussian_media_files(movie, receipt, destination, ".camera.json")
        : publish_gaussian_film_files(movie, receipt, destination);
    if (result.ok != (scenario == 0) || result.receipt_retained || read(sentinel) != "preserve") return 1;
    if (scenario == 0) {
      if (read(destination) != movie_bytes || read(sidecar) != receipt_bytes ||
          QFileInfo::exists(movie) || QFileInfo::exists(receipt)) return 1;
    } else {
      if (result.error.isEmpty() || (scenario != 6 && read(movie) != movie_bytes) ||
          (scenario != 7 && read(receipt) != receipt_bytes)) return 1;
      if (scenario == 1 && read(destination) != "existing movie") return 1;
      if (scenario == 2 && (!QFileInfo(destination).isSymLink() || read(destination) != "preserve")) return 1;
      if (scenario == 3 && !QFileInfo(destination).isDir()) return 1;
      if (scenario == 4 && read(sidecar) != "existing receipt") return 1;
      if (scenario == 5 && !QFileInfo(sidecar).isSymLink()) return 1;
      if (scenario != 4 && scenario != 5 && (QFileInfo::exists(sidecar) || QFileInfo(sidecar).isSymLink())) return 1;
      if (scenario >= 4 && (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink())) return 1;
    }
  }
  std::cout << "Sixteen photo/film complete-file publication, late collision, rollback and preservation cases pass\n";
}

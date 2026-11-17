#include "scene_library_controller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <iostream>
using namespace photo_editor::desktop;
namespace {
void write(const QString& path, const QByteArray& data) { QFile f(path); if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) std::abort(); }
QByteArray read(const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) std::abort(); return f.readAll(); }
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv); QTemporaryDir temporary;
  const auto library = temporary.filePath("saved-scenes"); qputenv("PHOTO_EDITOR_SCENE_DOCUMENTS_PATH", library.toUtf8());
  const auto depth = temporary.filePath("project.depthmotion.json"), gaussian = temporary.filePath("gaussian");
  QDir().mkpath(gaussian);
  write(depth, QJsonDocument(QJsonObject{{"version", 1}, {"settings", QJsonObject{}}, {"source", "video.mp4"}}).toJson());
  write(QDir(gaussian).filePath("scene.json"), QJsonDocument(QJsonObject{{"schema", "framehouse-gaussian-scene-v1"}}).toJson());
  SceneLibraryController first, second;
  if (!first.scenes().isEmpty() || !first.saveScene("depth-motion", depth, "Trip").value("ok").toBool() ||
      !second.saveScene("gaussian", gaussian, "Room", "view-1").value("ok").toBool() ||
      !first.saveScene("depth-motion", depth, "Trip edit").value("ok").toBool()) return fail("Both scene kinds must save across independent library instances");
  SceneLibraryController reopened;
  if (reopened.scenes().size() != 2) return fail("Saving an existing scene must update it without losing other windows' scenes");
  bool trip = false, room = false;
  for (const auto& value : reopened.scenes()) {
    const auto entry = value.toMap();
    trip |= entry.value("name") == "Trip edit" && entry.value("projectPath") == QFileInfo(depth).canonicalFilePath() && entry.value("kind") == "depth-motion";
    room |= entry.value("name") == "Room" && entry.value("viewId") == "view-1" && entry.value("projectPath") == QFileInfo(gaussian).canonicalFilePath();
    if (!entry.value("available").toBool()) return fail("Complete saved scenes must remain available when reopening");
  }
  if (!trip || !room) return fail("Reopening must retain scene identity, project path, name and saved camera view");
  const auto index = QDir(library).filePath("library.json"); const auto saved = read(index);
  QLockFile lock(QDir(library).filePath("library.lock"));
  if (!lock.tryLock(0) || first.saveScene("gaussian", gaussian, "Locked edit").value("ok").toBool() || read(index) != saved)
    return fail("A locked library must preserve its exact saved contents");
  lock.unlock();
  if (!first.saveScene("gaussian", gaussian, "Retry", "view-2").value("ok").toBool()) return fail("Saving must be retryable after another window releases its lock");
  write(index, "broken-json");
  if (first.saveScene("gaussian", gaussian, "Bad overwrite").value("ok").toBool() || read(index) != "broken-json")
    return fail("An invalid library index must never be replaced by an incomplete new one");
  write(index, saved); QFile::remove(depth); reopened.refresh();
  if (reopened.scenes().size() != 2) return fail("Missing media must not silently erase a saved scene");
  for (const auto& value : reopened.scenes()) {
    const auto entry = value.toMap();
    if (entry.value("kind") == "depth-motion" && entry.value("available").toBool()) return fail("Missing scenes must be marked unavailable for Library reopening");
  }
  std::cout << "Both scene types, named save/reopen, camera-view identity, independent windows, lock retry and invalid-index preservation passed\n";
}

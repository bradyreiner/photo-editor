#include "scene_library_controller.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QQuickItemGrabResult>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace photo_editor::desktop {
namespace {
QJsonArray entries(const QString& directory, QString* error) {
  QFile file(QDir(directory).filePath("library.json"));
  if (!file.exists()) return {};
  if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) { *error = "Could not read the saved scene library."; return {}; }
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parse).object();
  if (parse.error != QJsonParseError::NoError || document.value("version").toInt() != 1 || !document.value("scenes").isArray()) {
    *error = "The saved scene library is invalid. Its existing file was preserved."; return {};
  }
  return document.value("scenes").toArray();
}
bool available(const QString& kind, const QString& path) {
  return QFileInfo(kind == "depth-motion" ? path : QDir(path).filePath("scene.json")).isFile();
}
}
SceneLibraryController::SceneLibraryController(QObject* parent) : QObject(parent) {
  directory_ = qEnvironmentVariable("PHOTO_EDITOR_SCENE_DOCUMENTS_PATH").trimmed();
  if (directory_.isEmpty()) {
    const auto runtime = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR").trimmed();
    directory_ = QDir(runtime.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : runtime).filePath("scenes");
  }
  refresh();
}
QVariantMap SceneLibraryController::fail(const QString& message) { error_ = message; emit changed(); return {{"ok", false}, {"error", message}}; }
void SceneLibraryController::refresh() {
  error_.clear(); scenes_.clear();
  const auto saved = entries(directory_, &error_);
  for (const auto& value : saved) {
    auto entry = value.toObject().toVariantMap();
    const auto kind = entry.value("kind").toString(), path = entry.value("projectPath").toString();
    const auto id = entry.value("id").toString();
    if ((kind != "gaussian" && kind != "depth-motion") || id.size() != 24 ||
        !std::all_of(id.begin(), id.end(), [](QChar c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) continue;
    entry.insert("available", available(kind, path));
    const auto thumbnail = QDir(directory_).filePath("thumbnails/" + entry.value("id").toString() + ".png");
    if (QFileInfo(thumbnail).isFile()) entry.insert("previewSource", QUrl::fromLocalFile(thumbnail).toString() + "?v=" + QString::number(QFileInfo(thumbnail).lastModified().toMSecsSinceEpoch()));
    scenes_.push_back(entry);
  }
  std::sort(scenes_.begin(), scenes_.end(), [](const auto& a, const auto& b) { return a.toMap().value("updatedAt").toString() > b.toMap().value("updatedAt").toString(); });
  emit changed();
}
QVariantMap SceneLibraryController::saveScene(const QString& kind, const QString& input,
    const QString& name, const QString& view_id, QQuickItem* viewport) {
  if (kind != "gaussian" && kind != "depth-motion") return fail("Choose a supported scene project.");
  const QUrl url(input); const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : input);
  const auto project = info.canonicalFilePath();
  const auto title = name.trimmed().left(120);
  if (project.isEmpty() || title.isEmpty() || !available(kind, project)) return fail("Open a saved scene project and give it a name first.");
  QFile source(kind == "depth-motion" ? project : QDir(project).filePath("scene.json"));
  if (!source.open(QIODevice::ReadOnly) || source.size() > 4 * 1024 * 1024) return fail("The scene project could not be verified.");
  const auto metadata = QJsonDocument::fromJson(source.readAll()).object();
  if ((kind == "gaussian" && metadata.value("schema").toString() != "framehouse-gaussian-scene-v1") ||
      (kind == "depth-motion" && (metadata.value("version").toInt() != 1 || !metadata.value("settings").isObject())))
    return fail("The scene project format is not supported.");
  if (!QDir().mkpath(directory_)) return fail("Could not create the saved scene library.");
  QLockFile lock(QDir(directory_).filePath("library.lock"));
  if (!lock.tryLock(0)) return fail("The scene library is being saved by another window. Retry in a moment.");
  QString read_error; auto saved = entries(directory_, &read_error);
  if (!read_error.isEmpty()) return fail(read_error);
  const auto id = QString::fromLatin1(QCryptographicHash::hash((kind + '|' + project).toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
  const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  QJsonObject entry{{"id", id}, {"name", title}, {"kind", kind}, {"projectPath", project}, {"viewId", view_id}, {"updatedAt", now}};
  bool replaced = false;
  for (qsizetype index = 0; index < saved.size(); ++index)
    if (saved[index].toObject().value("id").toString() == id) { saved[index] = entry; replaced = true; break; }
  if (!replaced) saved.append(entry);
  QSaveFile output(QDir(directory_).filePath("library.json"));
  const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"scenes", saved}}).toJson();
  if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return fail("Could not save the scene library. Your project remains available.");
  lock.unlock(); refresh();
  if (viewport && viewport->window() && viewport->isVisible()) {
    QSize thumbnail_size(int(viewport->width()), int(viewport->height()));
    thumbnail_size.scale(480, 320, Qt::KeepAspectRatio);
    const auto capture = viewport->grabToImage(thumbnail_size);
    if (capture) connect(capture.data(), &QQuickItemGrabResult::ready, this, [this, capture, id] {
      if (capture->image().isNull() || !QDir().mkpath(QDir(directory_).filePath("thumbnails"))) return;
      QSaveFile thumbnail(QDir(directory_).filePath("thumbnails/" + id + ".png"));
      if (thumbnail.open(QIODevice::WriteOnly) && capture->image().save(&thumbnail, "PNG") && thumbnail.commit()) refresh();
    }, Qt::SingleShotConnection);
  }
  return {{"ok", true}, {"id", id}, {"name", title}};
}
}  // namespace photo_editor::desktop

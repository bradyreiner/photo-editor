#include "gaussian_saved_views.h"
#include "gaussian_camera_path.h"
#include "gaussian_physical_camera.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <QSet>
#include <cmath>

namespace photo_editor::desktop {
namespace {
constexpr qint64 maximum_bytes = 256 * 1024;
QString views_schema(const QVariantList& views) {
  for (const auto& view : views)
    if (view.toMap().value("camera").toMap().contains("physicalCalibration")) return "framehouse-gaussian-views-v3";
  for (const auto& view : views)
    if (view.toMap().value("camera").toMap().contains("photo")) return "framehouse-gaussian-views-v2";
  return "framehouse-gaussian-views-v1";
}
QVariantMap rejected(const QString& error) { return {{"ok", false}, {"error", error}}; }
QString fingerprint(const GaussianSceneData& scene) {
  QVariantList cameras;
  for (const auto& camera : scene.cameras) cameras.append(camera.description());
  const QVariantMap identity{{"artifactSha256", scene.artifact_sha256}, {"cameras", cameras},
      {"focusDistance", scene.focus_distance}};
  return QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument::fromVariant(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QString store_path(const QString& project) {
  const auto root = QFileInfo(project).canonicalFilePath();
  if (root.isEmpty() || !QFileInfo(root).isDir() || QDir::cleanPath(project) != root) return {};
  const auto manifest = QDir(root).filePath("scene.json");
  const auto path = QDir(root).filePath("camera-views.json");
  if (!QFileInfo(manifest).isFile() || QFileInfo(manifest).canonicalFilePath() != manifest ||
      QFileInfo(path).isSymLink() || (QFileInfo(path).exists() && !QFileInfo(path).isFile())) return {};
  return path;
}
bool scalar(const QJsonObject& value, const char* key, double minimum, double maximum, double* result) {
  const auto field = value.value(QString::fromLatin1(key));
  if (!field.isDouble()) return false;
  *result = field.toDouble();
  return std::isfinite(*result) && *result >= minimum && *result <= maximum;
}
bool vector(const QJsonObject& value, const char* key, QVector3D* result, double limit) {
  const auto field = value.value(QString::fromLatin1(key));
  if (!field.isArray() || field.toArray().size() != 3) return false;
  const auto array = field.toArray();
  float components[3];
  for (int i = 0; i < 3; ++i) {
    if (!array[i].isDouble() || !std::isfinite(array[i].toDouble()) || std::abs(array[i].toDouble()) > limit) return false;
    components[i] = static_cast<float>(array[i].toDouble());
  }
  *result = QVector3D(components[0], components[1], components[2]);
  return true;
}
QString normalized_name(const QString& name) {
  const auto result = name.trimmed().normalized(QString::NormalizationForm_C);
  if (result.isEmpty() || result.size() > 80) return {};
  for (const auto ch : result) if (ch.unicode() < 0x20 || ch.unicode() == 0x7f) return {};
  return result;
}
}

std::optional<GaussianCamera> gaussian_saved_camera(const QVariantMap& description) {
  const auto value = QJsonObject::fromVariantMap(description);
  if (value.value("convention") != "COLMAP-right-down-forward" || !value.value("name").isString()) return {};
  double width, height, fx, fy, cx, cy;
  if (!scalar(value, "width", 1, 65536, &width) || width != std::floor(width) ||
      !scalar(value, "height", 1, 65536, &height) || height != std::floor(height) ||
      !scalar(value, "fx", 1e-6, 1e9, &fx) || !scalar(value, "fy", 1e-6, 1e9, &fy) ||
      !scalar(value, "cx", -1e9, 1e9, &cx) || !scalar(value, "cy", -1e9, 1e9, &cy)) return {};
  GaussianCamera camera;
  if (value.contains("photo")) {
    if (!value.value("photo").isObject()) return {};
    const auto photo = gaussian_photo_settings(value.value("photo").toObject().toVariantMap());
    if (!photo) return {};
    camera.photo = *photo;
  }
  camera.name = value.value("name").toString();
  camera.image_size = QSize(static_cast<int>(width), static_cast<int>(height));
  camera.fx = static_cast<float>(fx); camera.fy = static_cast<float>(fy);
  camera.cx = static_cast<float>(cx); camera.cy = static_cast<float>(cy);
  if (value.contains("physicalCalibration")) {
    if (!value.value("physicalCalibration").isObject()) return {};
    camera.physical = gaussian_physical_calibration(value.value("physicalCalibration").toObject().toVariantMap());
    if (!camera.physical || !gaussian_physical_camera_consistent(camera)) return {};
  }
  if (!vector(value, "position", &camera.position, 1e12) || !vector(value, "right", &camera.right, 1.001) ||
      !vector(value, "down", &camera.down, 1.001) || !vector(value, "forward", &camera.forward, 1.001)) return {};
  for (const auto axis : {camera.right, camera.down, camera.forward})
    if (std::abs(axis.lengthSquared() - 1.0F) > 1e-4F) return {};
  if (std::abs(QVector3D::dotProduct(camera.right, camera.down)) > 1e-4F ||
      std::abs(QVector3D::dotProduct(camera.right, camera.forward)) > 1e-4F ||
      std::abs(QVector3D::dotProduct(camera.down, camera.forward)) > 1e-4F ||
      QVector3D::dotProduct(QVector3D::crossProduct(camera.right, camera.down), camera.forward) < 0.9999F) return {};
  return camera;
}

QVariantMap load_gaussian_saved_views(const QString& project, const GaussianSceneData& scene) {
  const auto path = store_path(project);
  if (path.isEmpty()) return rejected("Saved camera views must remain inside the scene project.");
  QFile file(path);
  if (!file.exists()) return {{"ok", true}, {"views", QVariantList{}}};
  if (!file.open(QIODevice::ReadOnly) || file.size() > maximum_bytes) return rejected("Cannot read saved camera views.");
  const auto document = QJsonDocument::fromJson(file.readAll());
  const auto root = document.object();
  if (!document.isObject() || (root.value("schema") != "framehouse-gaussian-views-v1" &&
      root.value("schema") != "framehouse-gaussian-views-v2" && root.value("schema") != "framehouse-gaussian-views-v3") ||
      root.value("sceneFingerprint") != fingerprint(scene) || !root.value("views").isArray())
    return rejected("Saved views are invalid or belong to a different scene/camera calibration. The file was not changed.");
  const auto views = root.value("views").toArray();
  if (views.size() > 128) return rejected("This scene exceeds the saved-view limit of 128.");
  QSet<QString> ids, names;
  for (const auto value : views) {
    if (!value.isObject()) return rejected("Invalid saved view.");
    const auto view = value.toObject();
    const auto id = view.value("id").toString();
    const auto name = normalized_name(view.value("name").toString());
    double index;
    if (QUuid(id).isNull() || id != QUuid(id).toString(QUuid::WithoutBraces) || ids.contains(id) ||
        name.isEmpty() || name != view.value("name").toString() || names.contains(name.toCaseFolded()) ||
        !scalar(view, "captureCameraIndex", 0, static_cast<double>(scene.cameras.size()) - 1, &index) || index != std::floor(index) ||
        !gaussian_saved_camera(view.value("camera").toObject().toVariantMap())) return rejected("Invalid or duplicate saved camera view.");
    ids.insert(id); names.insert(name.toCaseFolded());
  }
  const auto path_settings = root.value("cameraPath").toObject().toVariantMap();
  if (root.contains("cameraPath") && !gaussian_camera_path(path_settings, views.toVariantList()))
    return rejected("Invalid saved camera path. The file was not changed.");
  return {{"ok", true}, {"views", views.toVariantList()}, {"cameraPath", path_settings}};
}

QVariantMap save_gaussian_view(const QString& project, const GaussianSceneData& scene,
    const QString& requested_name, int capture_camera_index, const GaussianCamera& camera) {
  const auto path = store_path(project);
  const auto name = normalized_name(requested_name);
  if (path.isEmpty() || name.isEmpty() || capture_camera_index < 0 ||
      static_cast<std::size_t>(capture_camera_index) >= scene.cameras.size() || !gaussian_saved_camera(camera.description()))
    return rejected("Choose a valid camera and a unique view name of 1–80 characters.");
  QLockFile lock(path + ".lock");
  if (!lock.tryLock(0)) return rejected("Another window is saving views for this scene. Try again.");
  const auto loaded = load_gaussian_saved_views(project, scene);
  if (!loaded.value("ok").toBool()) return loaded;
  auto views = loaded.value("views").toList();
  if (views.size() >= 128) return rejected("This scene already has 128 saved views.");
  for (const auto& view : views) if (view.toMap().value("name").toString().toCaseFolded() == name.toCaseFolded())
    return rejected("That view name already exists. Choose a new name; saved shots are not overwritten.");
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  views.append(QVariantMap{{"id", id}, {"name", name}, {"captureCameraIndex", capture_camera_index}, {"camera", camera.description()}});
  QVariantMap root{{"schema", views_schema(views)}, {"sceneFingerprint", fingerprint(scene)}, {"views", views}};
  if (!loaded.value("cameraPath").toMap().isEmpty()) root.insert("cameraPath", loaded.value("cameraPath"));
  const auto bytes = QJsonDocument::fromVariant(root).toJson();
  if (bytes.size() > maximum_bytes) return rejected("Saved camera views exceed the storage limit.");
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    return rejected("Camera view could not be saved. Existing saved views are unchanged.");
  return {{"ok", true}, {"id", id}, {"views", views}};
}
QVariantMap save_gaussian_camera_path(const QString& project, const GaussianSceneData& scene, const QVariantMap& settings) {
  const auto path = store_path(project);
  if (path.isEmpty()) return rejected("Camera paths must remain inside the scene project.");
  QLockFile lock(path + ".lock");
  if (!lock.tryLock(0)) return rejected("Another window is saving this scene. Try again.");
  const auto loaded = load_gaussian_saved_views(project, scene);
  if (!loaded.value("ok").toBool()) return loaded;
  QString error;
  if (!gaussian_camera_path(settings, loaded.value("views").toList(), &error)) return rejected(error);
  const QVariantMap root{{"schema", views_schema(loaded.value("views").toList())}, {"sceneFingerprint", fingerprint(scene)},
      {"views", loaded.value("views")}, {"cameraPath", settings}};
  const auto bytes = QJsonDocument::fromVariant(root).toJson();
  if (bytes.size() > maximum_bytes) return rejected("Saved camera views exceed the storage limit.");
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return rejected("Cannot save the camera path.");
  return {{"ok", true}, {"cameraPath", settings}};
}
}  // namespace photo_editor::desktop

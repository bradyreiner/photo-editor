#include "sensor_dust_map_store.h"

#include "edit_recipe_mask_schema.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace photo_editor::desktop {

QString sensor_dust_map_key(
    const QString& camera_label,
    const QString& lens_model) {
  const auto camera = camera_label.simplified().isEmpty()
                          ? QStringLiteral("unknown-camera")
                          : camera_label.simplified();
  const auto lens = lens_model.simplified().isEmpty()
                        ? QStringLiteral("unknown-lens")
                        : lens_model.simplified();
  const auto identity = QStringLiteral("%1|%2").arg(camera, lens);
  return QString::fromLatin1(
      QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256)
          .toHex());
}

QString sensor_dust_map_path(
    const QString& storage_root,
    const QString& camera_label,
    const QString& lens_model) {
  const auto root =
      QDir(storage_root).filePath(QStringLiteral("sensor-dust-maps"));
  return QDir(root).filePath(
      QStringLiteral("%1.json").arg(
          sensor_dust_map_key(camera_label, lens_model)));
}

bool write_sensor_dust_map(
    const QString& storage_root,
    const QString& camera_label,
    const QString& lens_model,
    const QByteArray& segmentation_data,
    int width,
    int height,
    double remove_strength,
    double feather) {
  if (storage_root.trimmed().isEmpty() ||
      !valid_segmentation_payload(segmentation_data, width, height)) {
    return false;
  }

  const auto path = sensor_dust_map_path(
      storage_root, camera_label, lens_model);
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }

  QJsonObject object;
  object.insert(QStringLiteral("version"), 1);
  object.insert(QStringLiteral("cameraLabel"), camera_label);
  object.insert(QStringLiteral("lensModel"), lens_model);
  object.insert(QStringLiteral("width"), width);
  object.insert(QStringLiteral("height"), height);
  object.insert(QStringLiteral("removeStrength"), remove_strength);
  object.insert(QStringLiteral("feather"), feather);
  object.insert(
      QStringLiteral("segmentationData"),
      QString::fromLatin1(segmentation_data.toBase64()));

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
  return file.commit();
}

SensorDustMapPayload read_sensor_dust_map(
    const QString& storage_root,
    const QString& camera_label,
    const QString& lens_model) {
  SensorDustMapPayload payload;
  if (storage_root.trimmed().isEmpty()) {
    return payload;
  }
  QFile file(sensor_dust_map_path(storage_root, camera_label, lens_model));
  if (!file.open(QIODevice::ReadOnly)) {
    return payload;
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return payload;
  }

  const auto object = document.object();
  payload.width = object.value(QStringLiteral("width")).toInt();
  payload.height = object.value(QStringLiteral("height")).toInt();
  payload.remove_strength =
      object.value(QStringLiteral("removeStrength")).toDouble(74.0);
  payload.feather =
      object.value(QStringLiteral("feather")).toDouble(0.72);
  payload.segmentation_data = QByteArray::fromBase64(
      object.value(QStringLiteral("segmentationData"))
          .toString()
          .toLatin1());
  payload.valid = valid_segmentation_payload(
      payload.segmentation_data, payload.width, payload.height);
  return payload;
}

}  // namespace photo_editor::desktop

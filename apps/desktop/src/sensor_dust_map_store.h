#pragma once

#include <QByteArray>
#include <QString>

namespace photo_editor::desktop {

struct SensorDustMapPayload final {
  QByteArray segmentation_data;
  int width = 0;
  int height = 0;
  double remove_strength = 74.0;
  double feather = 0.72;
  bool valid = false;
};

[[nodiscard]] QString sensor_dust_map_key(
    const QString& camera_label,
    const QString& lens_model);

[[nodiscard]] QString sensor_dust_map_path(
    const QString& storage_root,
    const QString& camera_label,
    const QString& lens_model);

[[nodiscard]] bool write_sensor_dust_map(
    const QString& storage_root,
    const QString& camera_label,
    const QString& lens_model,
    const QByteArray& segmentation_data,
    int width,
    int height,
    double remove_strength,
    double feather);

[[nodiscard]] SensorDustMapPayload read_sensor_dust_map(
    const QString& storage_root,
    const QString& camera_label,
    const QString& lens_model);

}  // namespace photo_editor::desktop

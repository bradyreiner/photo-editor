#pragma once
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {
// Explicit user-supplied scale, never inferred from COLMAP coordinates.
// Focus is measured from the ideal lens plane, not the sensor plane.
struct GaussianPhysicalCameraSettings {
  double metres_per_scene_unit = 0;
  double sensor_width_mm = 36;
  double sensor_height_mm = 24;
  double focal_length_mm = 50;
  double f_number = 2.8;
  double focus_distance_m = 2;
  bool operator==(const GaussianPhysicalCameraSettings&) const = default;
  bool valid() const {
    return std::isfinite(metres_per_scene_unit) && metres_per_scene_unit >= 1e-6 && metres_per_scene_unit <= 1e6 &&
        std::isfinite(sensor_width_mm) && sensor_width_mm >= 1 && sensor_width_mm <= 300 &&
        std::isfinite(sensor_height_mm) && sensor_height_mm >= 1 && sensor_height_mm <= 300 &&
        std::isfinite(focal_length_mm) && focal_length_mm >= 1 && focal_length_mm <= 2000 &&
        std::isfinite(f_number) && f_number >= 0.5 && f_number <= 128 &&
        std::isfinite(focus_distance_m) && focus_distance_m > focal_length_mm / 1000 && focus_distance_m <= 1e8;
  }
  QVariantMap description() const {
    return {{"schema", "framehouse-physical-thin-lens-v1"}, {"metresPerSceneUnit", metres_per_scene_unit},
        {"sensorWidthMm", sensor_width_mm}, {"sensorHeightMm", sensor_height_mm},
        {"focalLengthMm", focal_length_mm}, {"fNumber", f_number}, {"focusDistanceM", focus_distance_m}};
  }
};
inline std::optional<GaussianPhysicalCameraSettings> gaussian_physical_camera_settings(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  if (json.size() != 7 || json.value("schema") != "framehouse-physical-thin-lens-v1") return {};
  for (const auto* key : {"metresPerSceneUnit", "sensorWidthMm", "sensorHeightMm", "focalLengthMm", "fNumber", "focusDistanceM"})
    if (!json.value(QLatin1String(key)).isDouble()) return {};
  const GaussianPhysicalCameraSettings result{json.value("metresPerSceneUnit").toDouble(),
      json.value("sensorWidthMm").toDouble(), json.value("sensorHeightMm").toDouble(),
      json.value("focalLengthMm").toDouble(), json.value("fNumber").toDouble(), json.value("focusDistanceM").toDouble()};
  return result.valid() ? std::optional(result) : std::nullopt;
}
struct GaussianPhysicalCalibration {
  GaussianPhysicalCameraSettings settings;
  // Freeze the cropped film gate when authoring a shot. Rounded preview/export
  // dimensions resample this gate; they must not recrop the field of view.
  double gate_width_mm = 0, gate_height_mm = 0;
  bool operator==(const GaussianPhysicalCalibration&) const = default;
  bool valid() const {
    if (!settings.valid() || !std::isfinite(gate_width_mm) || !std::isfinite(gate_height_mm) ||
        gate_width_mm <= 0 || gate_height_mm <= 0 || gate_width_mm > settings.sensor_width_mm + 1e-9 ||
        gate_height_mm > settings.sensor_height_mm + 1e-9) return false;
    return std::abs(gate_width_mm-settings.sensor_width_mm) < 1e-9 ||
        std::abs(gate_height_mm-settings.sensor_height_mm) < 1e-9;
  }
  QVariantMap description() const {
    return {{"schema","framehouse-physical-camera-calibration-v1"},{"settings",settings.description()},
        {"gateWidthMm",gate_width_mm},{"gateHeightMm",gate_height_mm},{"scaleSource","user-supplied"}};
  }
};
inline std::optional<GaussianPhysicalCalibration> gaussian_physical_calibration(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  if (json.size() != 5 || json.value("schema") != "framehouse-physical-camera-calibration-v1" ||
      json.value("scaleSource") != "user-supplied" || !json.value("settings").isObject() ||
      !json.value("gateWidthMm").isDouble() || !json.value("gateHeightMm").isDouble()) return {};
  const auto settings = gaussian_physical_camera_settings(json.value("settings").toObject().toVariantMap());
  if (!settings) return {};
  const GaussianPhysicalCalibration result{*settings,json.value("gateWidthMm").toDouble(),json.value("gateHeightMm").toDouble()};
  return result.valid() ? std::optional(result) : std::nullopt;
}
}  // namespace photo_editor::desktop

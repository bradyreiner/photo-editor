#pragma once
#include "gaussian_scene_data.h"
#include <algorithm>

namespace photo_editor::desktop {
inline bool gaussian_physical_camera_consistent(const GaussianCamera& camera) {
  if (!camera.physical) return true;
  const auto& c = *camera.physical;
  if (!c.valid() || !camera.photo.valid() || camera.image_size.isEmpty() || camera.photo.focal_scale != 1) return false;
  const auto& p = c.settings;
  const double focus_mm = p.focus_distance_m * 1000;
  const double v = p.focal_length_mm * focus_mm / (focus_mm-p.focal_length_mm);
  const auto near = [](double a, double b) { return std::isfinite(a) && std::isfinite(b) &&
      std::abs(a-b) <= 1e-6 * std::max(std::abs(b),1e-6); };
  return near(camera.fx,v*camera.image_size.width()/c.gate_width_mm) &&
      near(camera.fy,v*camera.image_size.height()/c.gate_height_mm) &&
      near(camera.cx,camera.image_size.width()/2.0) && near(camera.cy,camera.image_size.height()/2.0) &&
      near(camera.photo.lens.focus_distance,p.focus_distance_m/p.metres_per_scene_unit) &&
      near(camera.photo.lens.aperture_radius,p.focal_length_mm/(2000*p.f_number*p.metres_per_scene_unit));
}
struct GaussianPhysicalCameraResult {
  std::optional<GaussianCamera> camera;
  QVariantMap calibration;
  QString error;
};
// Convert an ideal physical lens to the renderer's off-axis aperture cameras.
// v = f*S/(S-f); D = f/N. Including v (rather than substituting f) preserves
// finite-distance magnification and ideal unit-focus breathing. The virtual
// upright projection mirrors the real inverted film image by convention.
// Center-crop the explicitly oriented sensor to output aspect; never stretch it.
inline GaussianPhysicalCameraResult make_gaussian_physical_camera(const GaussianCamera& pose, QSize output,
    const GaussianPhysicalCameraSettings& physical) {
  const auto fail = [](const QString& error) { return GaussianPhysicalCameraResult{{}, {}, error}; };
  if (!physical.valid()) return fail("Supply a valid measured scene scale, sensor gate, focal length, f-number and focus beyond the focal length.");
  if (output.width() < 1 || output.height() < 1 || output.width() > 8192 || output.height() > 8192 || !pose.photo.valid())
    return fail("Invalid output size or existing photo settings.");
  for (const auto axis : {pose.position, pose.right, pose.down, pose.forward})
    if (!std::isfinite(axis.x()) || !std::isfinite(axis.y()) || !std::isfinite(axis.z())) return fail("Camera pose must be finite.");
  if (std::abs(pose.right.lengthSquared()-1) > 1e-4F || std::abs(pose.down.lengthSquared()-1) > 1e-4F ||
      std::abs(pose.forward.lengthSquared()-1) > 1e-4F ||
      std::abs(QVector3D::dotProduct(pose.right,pose.down)) > 1e-4F ||
      (QVector3D::crossProduct(pose.right,pose.down)-pose.forward).length() > 1e-4F)
    return fail("Camera pose requires an orthonormal right-down-forward basis.");
  const double focus_mm = physical.focus_distance_m * 1000;
  const double image_distance_mm = physical.focal_length_mm * focus_mm / (focus_mm-physical.focal_length_mm);
  const double aspect = static_cast<double>(output.width()) / output.height();
  const double gate_width = std::min(physical.sensor_width_mm, physical.sensor_height_mm * aspect);
  const double gate_height = gate_width / aspect;
  const double focal_pixels = image_distance_mm * output.width() / gate_width;
  const double focus_units = physical.focus_distance_m / physical.metres_per_scene_unit;
  const double radius_units = physical.focal_length_mm / (2000 * physical.f_number * physical.metres_per_scene_unit);
  if (!std::isfinite(focal_pixels) || focal_pixels > 1e8 || focus_units < 0.01 || focus_units > 1e8 || radius_units > focus_units * 0.25)
    return fail("Physical camera exceeds the renderer's projection or scene-unit bounds.");
  auto camera = pose;
  camera.image_size = output;
  camera.fx = camera.fy = static_cast<float>(focal_pixels);
  camera.cx = static_cast<float>(output.width()) / 2;
  camera.cy = static_cast<float>(output.height()) / 2;
  camera.photo.focal_scale = 1;
  camera.photo.lens.focus_distance = static_cast<float>(focus_units);
  camera.photo.lens.aperture_radius = static_cast<float>(radius_units);
  camera.physical = GaussianPhysicalCalibration{physical,gate_width,gate_height};
  if (!gaussian_physical_camera_consistent(camera)) return fail("Physical camera cannot be represented by the current renderer.");
  auto calibration = physical.description();
  calibration.insert("effectiveGateWidthMm", gate_width);
  calibration.insert("effectiveGateHeightMm", gate_height);
  calibration.insert("imageDistanceMm", image_distance_mm);
  calibration.insert("exposureModel", "normalized-aperture-no-absolute-radiometry");
  return {camera, calibration, {}};
}
}  // namespace photo_editor::desktop

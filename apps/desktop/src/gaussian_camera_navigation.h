#pragma once

#include "gaussian_scene_data.h"
#include <QQuaternion>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace photo_editor::desktop {
namespace gaussian_camera_navigation_detail {
struct Basis {
  QVector3D right;
  QVector3D down;
  QVector3D forward;
};

inline bool finite(QVector3D value) {
  return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

inline bool make_basis(const GaussianCamera& camera, Basis* result) {
  if (!finite(camera.position) || !finite(camera.right) || !finite(camera.down) || !finite(camera.forward)) return false;
  auto forward = camera.forward;
  if (forward.lengthSquared() < 1e-12F) return false;
  forward.normalize();

  auto right = camera.right - forward * QVector3D::dotProduct(camera.right, forward);
  if (right.lengthSquared() < 1e-12F) {
    right = QVector3D::crossProduct(camera.down, forward);
    right -= forward * QVector3D::dotProduct(right, forward);
  }
  if (right.lengthSquared() < 1e-12F) return false;
  right.normalize();

  auto down = QVector3D::crossProduct(forward, right);
  if (down.lengthSquared() < 1e-12F) return false;
  down.normalize();
  *result = {right, down, forward};
  return true;
}

inline double effective_focal_length(float focal, float focal_scale) {
  if (!std::isfinite(focal) || !std::isfinite(focal_scale) || focal <= 0 || focal_scale <= 0) return 0;
  const double result = static_cast<double>(focal) * focal_scale;
  return std::isfinite(result) && result > 1e-6 ? result : 0;
}

inline float bounded_pan(float value) {
  if (!std::isfinite(value)) return 0;
  return std::clamp(value, -10.0F, 10.0F);
}

inline bool rotate_basis(const Basis& basis,float yaw_degrees,float pitch_degrees,float roll_degrees,Basis* result) {
  if (result == nullptr) return false;
  const float yaw = std::isfinite(yaw_degrees) ? std::remainder(yaw_degrees,360.0F) : 0.0F;
  const float pitch = std::isfinite(pitch_degrees) ? std::remainder(pitch_degrees,360.0F) : 0.0F;
  const float roll = std::isfinite(roll_degrees) ? std::remainder(roll_degrees,360.0F) : 0.0F;
  if (yaw == 0 && pitch == 0 && roll == 0) { *result = basis; return true; }
  const auto yaw_rotation = QQuaternion::fromAxisAndAngle(-basis.down,yaw);
  const auto pitch_rotation = QQuaternion::fromAxisAndAngle(basis.right,pitch);
  const auto roll_rotation = QQuaternion::fromAxisAndAngle(basis.forward,roll);
  auto rotation = yaw_rotation*pitch_rotation*roll_rotation;
  if (!std::isfinite(rotation.scalar()) || !std::isfinite(rotation.x()) ||
      !std::isfinite(rotation.y()) || !std::isfinite(rotation.z()) || rotation.lengthSquared() < 1e-12F) return false;
  rotation.normalize();
  const auto forward = rotation.rotatedVector(basis.forward).normalized();
  auto right = rotation.rotatedVector(basis.right);
  right -= forward*QVector3D::dotProduct(right,forward);
  if (forward.lengthSquared() < 1e-12F || right.lengthSquared() < 1e-12F) return false;
  right.normalize();
  const auto down = QVector3D::crossProduct(forward,right).normalized();
  if (!finite(forward) || !finite(right) || !finite(down)) return false;
  *result = {right,down,forward};
  return true;
}

inline QVector3D viewport_translation(const GaussianCamera& camera, const Basis& basis,
    double focus_depth, double dx, double dy) {
  if (camera.image_size.width() <= 0 || camera.image_size.height() <= 0 ||
      !std::isfinite(focus_depth) || focus_depth <= 0) return {};
  const double fx = effective_focal_length(camera.fx, camera.photo.focal_scale);
  const double fy = effective_focal_length(camera.fy, camera.photo.focal_scale);
  if (fx <= 0 || fy <= 0) return {};
  const auto bounded_delta = [](double value) {
    return std::isfinite(value) ? std::clamp(value, -10.0, 10.0) : 0.0;
  };
  const double horizontal = bounded_delta(dx) * focus_depth * camera.image_size.width() / fx;
  const double vertical = bounded_delta(dy) * focus_depth * camera.image_size.height() / fy;
  if (!std::isfinite(horizontal) || !std::isfinite(vertical)) return {};
  const auto component = [](double value) {
    return static_cast<float>(std::clamp(value, -1.0e6, 1.0e6));
  };
  return basis.right * component(horizontal) + basis.down * component(vertical);
}

inline double relative_roll_degrees(const Basis& reference,const Basis& pose) {
  auto reference_down = reference.down-pose.forward*QVector3D::dotProduct(reference.down,pose.forward);
  if (reference_down.lengthSquared() < 1e-10F) {
    const auto reference_right = reference.right-pose.forward*QVector3D::dotProduct(reference.right,pose.forward);
    if (reference_right.lengthSquared() < 1e-10F) return 0;
    reference_down = QVector3D::crossProduct(pose.forward,reference_right);
  }
  if (reference_down.lengthSquared() < 1e-10F) return 0;
  reference_down.normalize();
  const double cosine = std::clamp(static_cast<double>(QVector3D::dotProduct(reference_down,pose.down)),-1.0,1.0);
  const double sine = std::clamp(static_cast<double>(QVector3D::dotProduct(QVector3D::crossProduct(reference_down,pose.down),pose.forward)),-1.0,1.0);
  return std::atan2(sine,cosine)*180.0/std::numbers::pi;
}
}  // namespace gaussian_camera_navigation_detail

// dx/dy are fractions of the displayed image bounds. Scaling by width/focal
// converts a normalized drag to the visible focus-plane size, so the same
// pointer distance remains natural across camera focal lengths and aspect ratios.
inline QVector3D gaussian_camera_pan_translation(const GaussianCamera& camera, float focus_depth,
    double normalized_viewport_dx, double normalized_viewport_dy) {
  gaussian_camera_navigation_detail::Basis basis;
  if (!gaussian_camera_navigation_detail::make_basis(camera, &basis) ||
      !std::isfinite(focus_depth) || focus_depth <= 0) return {};
  return gaussian_camera_navigation_detail::viewport_translation(camera, basis, focus_depth,
      normalized_viewport_dx, normalized_viewport_dy);
}

// Apply a local-axis pose increment around a world-space subject point. Each
// input is relative to the current camera frame, including its existing roll.
// The selected point keeps its view-relative framing while the camera moves.
inline GaussianCamera navigate_gaussian_camera_increment(const GaussianCamera& base, QVector3D pivot,
    float yaw_degrees, float pitch_degrees, float roll_degrees, float dolly, float pan_x, float pan_y) {
  using namespace gaussian_camera_navigation_detail;
  Basis basis;
  if (!make_basis(base, &basis) || !finite(pivot)) return base;

  const auto pivot_offset = pivot - base.position;
  if (!finite(pivot_offset)) return base;
  const double focus_depth = static_cast<double>(pivot_offset.x()) * basis.forward.x() +
      static_cast<double>(pivot_offset.y()) * basis.forward.y() +
      static_cast<double>(pivot_offset.z()) * basis.forward.z();
  if (!std::isfinite(focus_depth) || focus_depth < 0.01 || focus_depth > 1.0e6) return base;

  const float yaw = std::isfinite(yaw_degrees) ? std::remainder(yaw_degrees, 360.0F) : 0.0F;
  const float pitch = std::isfinite(pitch_degrees) ? std::remainder(pitch_degrees, 360.0F) : 0.0F;
  const float roll = std::isfinite(roll_degrees) ? std::remainder(roll_degrees, 360.0F) : 0.0F;
  // A 20x dolly range keeps a picked pivot in front of the camera and avoids
  // the extreme jumps produced by an unbounded exponential wheel accumulator.
  constexpr float maximum_log_dolly = 2.995732274F;  // log(20)
  const float dolly_value = std::isfinite(dolly) ? std::clamp(dolly, -maximum_log_dolly, maximum_log_dolly) : 0.0F;
  if (yaw == 0 && pitch == 0 && roll == 0 && dolly_value == 0 && bounded_pan(pan_x) == 0 && bounded_pan(pan_y) == 0)
    return base;
  const float dolly_factor = std::exp(-dolly_value);
  Basis rotated_basis;
  if (!rotate_basis(basis,yaw,pitch,roll,&rotated_basis)) return base;
  const auto yaw_rotation = QQuaternion::fromAxisAndAngle(-basis.down,yaw);
  const auto pitch_rotation = QQuaternion::fromAxisAndAngle(basis.right,pitch);
  const auto roll_rotation = QQuaternion::fromAxisAndAngle(basis.forward,roll);
  auto rotation = yaw_rotation*pitch_rotation*roll_rotation;
  rotation.normalize();
  const auto translated_offset = rotation.rotatedVector(base.position-pivot);
  if (!finite(translated_offset)) return base;
  const auto pan = viewport_translation(base,
      rotated_basis, focus_depth,
      bounded_pan(pan_x), bounded_pan(pan_y));
  auto camera = base;
  camera.position = pivot + translated_offset + rotated_basis.forward *
      (static_cast<float>(focus_depth) * (1.0F - dolly_factor)) + pan;
  if (!finite(camera.position)) return base;
  camera.right = rotated_basis.right;
  camera.down = rotated_basis.down;
  camera.forward = rotated_basis.forward;
  return camera;
}

// Orbit around a world-space subject point while retaining the older API used
// by saved-view and export callers. Pitch is intentionally unbounded: crossing
// the previous vertical threshold is a normal camera rotation.
inline GaussianCamera navigate_gaussian_camera_around(const GaussianCamera& base, QVector3D pivot,
    float yaw_degrees, float pitch_degrees, float dolly, float pan_x, float pan_y) {
  return navigate_gaussian_camera_increment(base,pivot,yaw_degrees,pitch_degrees,0,dolly,pan_x,pan_y);
}

inline GaussianCamera rotate_gaussian_camera_around(const GaussianCamera& base, QVector3D pivot,
    float yaw_degrees, float pitch_degrees, float roll_degrees) {
  return navigate_gaussian_camera_increment(base,pivot,yaw_degrees,pitch_degrees,roll_degrees,0,0,0);
}

inline GaussianCamera look_gaussian_camera(const GaussianCamera& base,float yaw_degrees,float pitch_degrees) {
  using namespace gaussian_camera_navigation_detail;
  Basis basis, rotated;
  if (!make_basis(base,&basis) || !rotate_basis(basis,yaw_degrees,pitch_degrees,0,&rotated)) return base;
  auto camera = base;
  camera.right = rotated.right; camera.down = rotated.down; camera.forward = rotated.forward;
  return camera;
}

inline GaussianCamera roll_gaussian_camera(const GaussianCamera& base,float degrees) {
  using namespace gaussian_camera_navigation_detail;
  Basis basis, rotated;
  if (!make_basis(base,&basis) || !rotate_basis(basis,0,0,degrees,&rotated)) return base;
  auto camera = base;
  camera.right = rotated.right; camera.down = rotated.down; camera.forward = rotated.forward;
  return camera;
}

inline GaussianCamera move_gaussian_camera_local(const GaussianCamera& base,double right,double down,double forward) {
  using namespace gaussian_camera_navigation_detail;
  Basis basis;
  if (!make_basis(base,&basis)) return base;
  const auto component = [](double value) {
    if (!std::isfinite(value)) return 0.0F;
    return static_cast<float>(std::clamp(value,-1.0e6,1.0e6));
  };
  const auto translation = basis.right*component(right)+basis.down*component(down)+basis.forward*component(forward);
  auto camera = base;
  camera.position += translation;
  return finite(camera.position) ? camera : base;
}

// Display-only twist estimate relative to the selected capture basis. At the
// Euler singularity, use the capture right axis to keep a finite readout; this
// value never constrains or modifies camera orientation.
inline double gaussian_camera_relative_roll_degrees(const GaussianCamera& capture,const GaussianCamera& current) {
  using namespace gaussian_camera_navigation_detail;
  Basis reference,pose;
  if (!make_basis(capture,&reference) || !make_basis(current,&pose)) return 0;
  return relative_roll_degrees(reference,pose);
}
}  // namespace photo_editor::desktop

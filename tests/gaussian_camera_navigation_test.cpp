#include "gaussian_camera_navigation.h"
#include "gaussian_focus_pick.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;

namespace {
bool near(double actual, double expected, double tolerance = 1e-4) {
  return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

QVector3D point_in_camera(const GaussianCamera& camera, QVector3D point) {
  const auto offset = point - camera.position;
  return {QVector3D::dotProduct(offset,camera.right), QVector3D::dotProduct(offset,camera.down),
      QVector3D::dotProduct(offset,camera.forward)};
}

GaussianPoint splat_at(QVector3D center) {
  GaussianPoint point;
  point.center_opacity = {center.x(),center.y(),center.z(),0.95F};
  point.covariance_a = {0.01F,0,0,0.01F};
  point.covariance_b = {0,0.01F,0,0};
  return point;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto fail = [](const char* stage) { std::cerr << "Camera navigation: " << stage << '\n'; return 1; };

  GaussianCamera camera;
  camera.image_size = {1000,500}; camera.fx = camera.fy = 1000;
  camera.cx = 500.5F; camera.cy = 250.5F;
  camera.position = {12,-7,4};
  const QVector3D focus_point = camera.position + camera.forward*8.0F;

  const auto unmodified = navigate_gaussian_camera(camera,8,0,0,0,0,0);
  if (unmodified.position != camera.position || unmodified.right != camera.right ||
      unmodified.down != camera.down || unmodified.forward != camera.forward) return fail("zero-delta camera identity");

  const auto orbited = navigate_gaussian_camera(camera,8,90,32,0,0,0);
  const auto subject_in_view = point_in_camera(orbited,focus_point);
  const auto camera_to_subject = focus_point-orbited.position;
  if (!near(camera_to_subject.length(),8,1e-3) || std::abs(subject_in_view.x()) > 1e-4 ||
      std::abs(subject_in_view.y()) > 1e-4 || subject_in_view.z() < 7.99 ||
      !near(orbited.right.length(),1) || !near(orbited.down.length(),1) ||
      !near(orbited.forward.length(),1) || std::abs(QVector3D::dotProduct(orbited.right,orbited.down)) > 1e-5F)
    return fail("yaw and pitch orbit around the focus subject with an orthonormal frame");

  // Free look rotates the view around the current local axes while keeping
  // the camera position fixed. Its pitch deliberately crosses the former
  // +/-85 degree horizon clamp, and inverse single-axis turns recover pose.
  const auto look_yaw = look_gaussian_camera(camera,37,0);
  const auto look_yaw_back = look_gaussian_camera(look_yaw,-37,0);
  const auto look_pitch = look_gaussian_camera(camera,0,89);
  const auto look_pitch_back = look_gaussian_camera(look_pitch,0,-89);
  if (look_yaw.position != camera.position || look_pitch.position != camera.position ||
      !near(QVector3D::dotProduct(look_yaw_back.forward,camera.forward),1,2e-5) ||
      !near(QVector3D::dotProduct(look_pitch_back.forward,camera.forward),1,2e-5) ||
      QVector3D::dotProduct(look_pitch.forward,camera.forward) > 0.1F ||
      !near(look_pitch.right.length(),1) || !near(look_pitch.down.length(),1) ||
      !near(look_pitch.forward.length(),1) || std::abs(QVector3D::dotProduct(look_pitch.right,look_pitch.down)) > 1e-5F)
    return fail("unbounded fixed-position look and inverse local-axis rotations");
  const auto pitch_before_crossing = look_gaussian_camera(camera,0,84.9F);
  const auto pitch_after_crossing = look_gaussian_camera(pitch_before_crossing,0,1.0F);
  if (QVector3D::dotProduct(pitch_after_crossing.forward,camera.forward) > 0.08F)
    return fail("look remains continuous across the former vertical limit");

  auto repeated_look = camera;
  for (int i = 0; i < 2000; ++i) repeated_look = look_gaussian_camera(repeated_look,0.37F,0.29F);
  const auto repeated_right = repeated_look.right, repeated_down = repeated_look.down, repeated_forward = repeated_look.forward;
  if (repeated_look.position != camera.position || !std::isfinite(repeated_forward.length()) ||
      !near(repeated_right.length(),1,2e-5) || !near(repeated_down.length(),1,2e-5) || !near(repeated_forward.length(),1,2e-5) ||
      std::abs(QVector3D::dotProduct(repeated_right,repeated_down)) > 2e-5F ||
      std::abs(QVector3D::dotProduct(repeated_right,repeated_forward)) > 2e-5F ||
      std::abs(QVector3D::dotProduct(repeated_down,repeated_forward)) > 2e-5F)
    return fail("incremental quaternion look remains finite and orthonormal over long drags");

  const auto rolled = roll_gaussian_camera(camera,90);
  const auto rolled_back = roll_gaussian_camera(rolled,-90);
  const auto rolled_pan = gaussian_camera_pan_translation(rolled,10,0.1,0);
  if (rolled.position != camera.position || !near(gaussian_camera_relative_roll_degrees(camera,rolled),90,1e-3) ||
      !near(QVector3D::dotProduct(rolled_back.right,camera.right),1,2e-5) ||
      !near(QVector3D::dotProduct(rolled_back.down,camera.down),1,2e-5) ||
      !near(QVector3D::dotProduct(rolled_pan,rolled.right),1,1e-4) ||
      std::abs(QVector3D::dotProduct(rolled_pan,camera.right)) > 1e-4F)
    return fail("direct roll keeps position, reports capture-relative twist and rotates panning axes");

  const auto viewed_forward = look_gaussian_camera(camera,90,0);
  const auto moved_forward = move_gaussian_camera_local(viewed_forward,0,0,3);
  const auto move_delta = moved_forward.position-viewed_forward.position;
  if (!near(QVector3D::dotProduct(move_delta,viewed_forward.forward),3,1e-4) ||
      std::abs(QVector3D::dotProduct(move_delta,viewed_forward.right)) > 1e-4F ||
      std::abs(QVector3D::dotProduct(move_delta,viewed_forward.down)) > 1e-4F ||
      !near(QVector3D::dotProduct(move_delta,camera.forward),0,1e-4))
    return fail("scene-unit free-flight movement follows the current post-look forward axis");

  // The focus pick may be off-centre. Orbit must preserve its camera-relative
  // position instead of snapping it to the middle of the viewport.
  const QVector3D picked_subject = camera.position + camera.right*1.25F + camera.down*0.4F + camera.forward*6.0F;
  const auto picked_before = point_in_camera(camera,picked_subject);
  const auto picked_orbit = navigate_gaussian_camera_around(camera,picked_subject,73,-28,0,0,0);
  const auto picked_after = point_in_camera(picked_orbit,picked_subject);
  if (!near(picked_after.x(),picked_before.x(),2e-4) || !near(picked_after.y(),picked_before.y(),2e-4) ||
      !near(picked_after.z(),picked_before.z(),2e-4) ||
      !near((picked_subject-picked_orbit.position).length(),(picked_subject-camera.position).length(),2e-4))
    return fail("picked off-axis subject keeps its framing while orbiting");
  const QVector3D scene_shift{16384,-8192,4096};
  auto shifted_camera = camera; shifted_camera.position += scene_shift;
  const auto shifted_orbit = navigate_gaussian_camera_around(shifted_camera,picked_subject+scene_shift,73,-28,0,0,0);
  const auto local_translation = shifted_orbit.position-picked_orbit.position;
  if (!near(local_translation.x(),scene_shift.x(),0.01) || !near(local_translation.y(),scene_shift.y(),0.01) ||
      !near(local_translation.z(),scene_shift.z(),0.01))
    return fail("camera-relative orbit stays stable under a moderate scene-origin translation");

  // Pan tracks visible image size at the focus depth. Halving focal length
  // doubles movement; changing image dimensions changes each axis separately.
  const auto pan = gaussian_camera_pan_translation(camera,10,0.1,-0.1);
  if (!near(QVector3D::dotProduct(pan,camera.right),1) || !near(QVector3D::dotProduct(pan,camera.down),-0.5))
    return fail("viewport-normalized pan follows focus-plane FOV");
  auto wider_fov = camera; wider_fov.fx *= 0.5F; wider_fov.fy *= 0.5F;
  const auto wide_pan = gaussian_camera_pan_translation(wider_fov,10,0.1,-0.1);
  if (!near(QVector3D::dotProduct(wide_pan,camera.right),2) || !near(QVector3D::dotProduct(wide_pan,camera.down),-1))
    return fail("pan scales inversely with focal length");
  auto cropped = camera; cropped.photo.focal_scale = 2;
  const auto cropped_pan = gaussian_camera_pan_translation(cropped,10,0.1,-0.1);
  if (!near(QVector3D::dotProduct(cropped_pan,camera.right),0.5) || !near(QVector3D::dotProduct(cropped_pan,camera.down),-0.25))
    return fail("pan uses effective focal scale");

  const auto panned_orbit = navigate_gaussian_camera_around(camera,focus_point,45,20,0,0.1F,-0.05F);
  auto rotated_basis = camera;
  rotated_basis.right = panned_orbit.right; rotated_basis.down = panned_orbit.down;
  rotated_basis.forward = panned_orbit.forward;
  const auto expected_pan = gaussian_camera_pan_translation(rotated_basis,8,0.1,-0.05);
  const auto panned_center = navigate_gaussian_camera_around(camera,focus_point,45,20,0,0,0);
  if (!near((panned_orbit.position-panned_center.position).x(),expected_pan.x(),1e-4) ||
      !near((panned_orbit.position-panned_center.position).y(),expected_pan.y(),1e-4) ||
      !near((panned_orbit.position-panned_center.position).z(),expected_pan.z(),1e-4))
    return fail("pan follows the rotated camera frame");

  const auto rolled_pivot = rotate_gaussian_camera_around(camera,picked_subject,0,0,41);
  const auto rolled_pivot_view = point_in_camera(rolled_pivot,picked_subject);
  if (!near(rolled_pivot_view.x(),picked_before.x(),2e-4) || !near(rolled_pivot_view.y(),picked_before.y(),2e-4) ||
      !near(rolled_pivot_view.z(),picked_before.z(),2e-4))
    return fail("explicit pivot orbit preserves framing through roll");

  const auto close_dolly = navigate_gaussian_camera_around(camera,focus_point,0,0,100,0,0);
  const auto far_dolly = navigate_gaussian_camera_around(camera,focus_point,0,0,-100,0,0);
  const double close_depth = (focus_point-close_dolly.position).length();
  const double far_depth = (focus_point-far_dolly.position).length();
  if (!near(close_depth,0.4,1e-3) || !near(far_depth,160,1e-2) || close_depth <= 0)
    return fail("dolly is finite and bounded to a safe 20x range");
  const auto behind = navigate_gaussian_camera_around(camera,camera.position-camera.forward,10,5,1,0,0);
  if (behind.position != camera.position || behind.forward != camera.forward)
    return fail("invalid pivot is rejected without moving the camera");
  auto invalid_intrinsics = camera; invalid_intrinsics.fx = 0;
  if (!gaussian_camera_pan_translation(invalid_intrinsics,8,0.25,0).isNull())
    return fail("invalid focal length yields no pan translation");
  const auto bounded_translation = gaussian_camera_pan_translation(camera,1.0e6F,1e300,-1e300);
  if (!std::isfinite(bounded_translation.length()) || bounded_translation.length() > 1.5e6F)
    return fail("extreme drag input cannot create an unbounded translation");

  // Focus picking returns the selected point along the actual off-axis ray,
  // including axial-to-ray distance conversion needed to orbit around it.
  GaussianSceneData scene;
  scene.points = {splat_at({0.6F,0,3})};
  GaussianCamera pick_camera;
  pick_camera.image_size = {100,100}; pick_camera.fx = pick_camera.fy = 100;
  pick_camera.cx = pick_camera.cy = 50.5F;
  const auto pick = pick_gaussian_focus(scene,pick_camera,{70.5,50.5},{},{});
  const auto world_point = pick.value("worldPoint").toList();
  if (!pick.value("ok").toBool() || world_point.size() != 3 ||
      !near(world_point[0].toDouble(),0.6,1e-3) || !near(world_point[1].toDouble(),0,1e-3) ||
      !near(world_point[2].toDouble(),3,1e-3))
    return fail("focus pick returns the selected off-axis world point");

  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const auto invalid_input = navigate_gaussian_camera_around(camera,focus_point,nan,nan,nan,nan,nan);
  if (!std::isfinite(invalid_input.position.x()) || !std::isfinite(invalid_input.forward.z()))
    return fail("non-finite navigation input cannot poison camera state");

  std::cout << "Camera navigation: subject orbit, off-axis focus pivot, FOV-scaled pan, safe dolly, and world-point focus pass.\n";
  return 0;
}

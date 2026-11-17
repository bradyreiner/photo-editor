#include "gaussian_physical_camera.h"
#include "gaussian_lens_sampling.h"
#include "gaussian_camera_optics.h"
#include <QCoreApplication>
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto check = [](bool ok, const char* message) { if (!ok) std::cerr << message << '\n'; return ok; };
  const auto near = [](double a, double b) { return std::abs(a-b) <= 2e-5 * std::max(1.0,std::abs(b)); };
  GaussianCamera pose; pose.position = {2,3,4};
  pose.photo.focal_scale = 2; pose.photo.exposure_ev = -1;
  pose.photo.lens.blades = 7; pose.photo.lens.samples = 128;
  pose.photo.film = {0.3F,2,0.3F,4,0.8F,123};
  GaussianPhysicalCameraSettings p;
  if (!check(!make_gaussian_physical_camera(pose,{6000,4000},p).camera, "Missing scale accepted")) return 1;
  p.metres_per_scene_unit = 0.5;
  if (!check(gaussian_physical_camera_settings(p.description())->description() == p.description(), "Calibration roundtrip")) return 1;
  const auto built = make_gaussian_physical_camera(pose,{6000,4000},p);
  if (!built.camera) { std::cerr << built.error.toStdString(); return 1; }
  const auto c = *built.camera;
  if (!check(near(c.fx, 50.0*2000/1950*6000/36) && c.fx == c.fy && c.cx == 3000 && c.cy == 2000 &&
      c.position == pose.position && c.photo.focal_scale == 1 && c.photo.exposure_ev == -1 && c.photo.film == pose.photo.film &&
      c.photo.lens.blades == 7 && c.photo.lens.samples == 128 && near(c.photo.lens.focus_distance,4) &&
      near(c.photo.lens.aperture_radius,50.0/(2*2.8*1000*0.5)), "Physical conversion/preservation")) return 1;
  // Independent lens equation: signed diameter on the film is D*(1-v/v_z).
  // Compare projected rays at opposite pupil edges with this physical result.
  // Isolate optics from absolute-world-position float cancellation. The native
  // image proof separately checks equivalent scales; large world origins need
  // camera-relative rendering before claiming arbitrary-scale precision.
  auto optical_pose = pose; optical_pose.position = {};
  for (double focal : {28.0,35.0,50.0,85.0}) for (double stop : {1.4,2.0,2.8,8.0})
    for (double focus : {0.3,1.0,3.0,100.0}) for (double scale : {0.01,0.5,10.0}) {
      p.focal_length_mm = focal; p.f_number = stop; p.focus_distance_m = focus; p.metres_per_scene_unit = scale;
      const auto result = make_gaussian_physical_camera(optical_pose,{6000,4000},p);
      if (!result.camera) return 1;
      const auto& camera = *result.camera;
      for (double z : {focus*0.5,focus,focus*2}) {
        const auto left = gaussian_aperture_camera(camera,{-camera.photo.lens.aperture_radius,0});
        const auto right = gaussian_aperture_camera(camera,{camera.photo.lens.aperture_radius,0});
        const auto project = [&](const GaussianCamera& view) {
          // Point is on-axis relative to the original lens center.
          const double x = static_cast<double>(camera.position.x())-view.position.x();
          return view.fx*x/(z/scale)+view.cx;
        };
        const double v = focal*(focus*1000)/(focus*1000-focal);
        const double v_z = focal*(z*1000)/(z*1000-focal);
        // The upright virtual image reverses the physical film's sign.
        const double diameter_px = -focal/stop*(1-v/v_z)*6000/36;
        if (!check(std::abs((project(right)-project(left))-diameter_px) < 0.06, "Analytical signed circle of confusion")) {
          std::cerr << "f=" << focal << " N=" << stop << " S=" << focus << " scale=" << scale << " z=" << z
              << " actual=" << project(right)-project(left) << " expected=" << diameter_px << '\n';
          return 1;
        }
      }
    }
  p = {}; p.metres_per_scene_unit = 1;
  const auto square = make_gaussian_physical_camera(pose,{1000,1000},p);
  if (!square.camera || square.calibration.value("effectiveGateWidthMm") != 24.0 ||
      square.calibration.value("effectiveGateHeightMm") != 24.0) return 1;
  p.sensor_width_mm = 24; p.sensor_height_mm = 36;
  const auto portrait = make_gaussian_physical_camera(pose,{4000,6000},p);
  if (!portrait.camera || portrait.camera->fx != c.fx || portrait.calibration.value("effectiveGateHeightMm") != 36.0) return 1;
  const auto half = make_gaussian_physical_camera(pose,{2000,3000},p);
  if (!half.camera || half.camera->fx * 2 != portrait.camera->fx || half.camera->photo != portrait.camera->photo) return 1;
  GaussianPhysicalCameraSettings measured; measured.metres_per_scene_unit=1;
  const auto authored=make_gaussian_physical_camera(optical_pose,{1920,1080},measured);
  if (!authored.camera) return 1;
  const auto optics=gaussian_camera_optics(*authored.camera,30);
  if (!check(optics.value("ok").toBool() && optics.value("physical").toBool() &&
      near(optics.value("effectiveGateHeightMm").toDouble(),20.25) &&
      near(optics.value("focusFromSensorM").toDouble(),2.0+50.0*2000/1950/1000),"Sensor gate or focus reference readout differs")) return 1;
  // Check the displayed near/far limits against independently projected rays
  // from the opposite pupil edges. Both boundaries must produce a 30 µm blur.
  for (const char* key:{"depthOfFieldNearM","depthOfFieldFarM"}) {
    const double depth=optics.value(QLatin1String(key)).toDouble();
    const auto& camera=*authored.camera;
    const auto left=gaussian_aperture_camera(camera,{-camera.photo.lens.aperture_radius,0});
    const auto right=gaussian_aperture_camera(camera,{camera.photo.lens.aperture_radius,0});
    const double x_left=left.fx*(-left.position.x())/depth+left.cx;
    const double x_right=right.fx*(-right.position.x())/depth+right.cx;
    const double blur_um=std::abs(x_left-x_right)*camera.physical->gate_width_mm/camera.image_size.width()*1000;
    if (!check(std::abs(blur_um-30)<0.002,"Displayed depth-of-field limit disagrees with pupil ray geometry")) return 1;
  }
  measured.focus_distance_m=3;
  const auto full_refocus=make_gaussian_physical_camera(*authored.camera,{1920,1080},measured,true);
  const auto rounded_refocus=make_gaussian_physical_camera(authored.camera->resized({1279,719}),{1279,719},measured,true);
  if (!check(full_refocus.camera && rounded_refocus.camera &&
      full_refocus.camera->physical->gate_width_mm==rounded_refocus.camera->physical->gate_width_mm &&
      full_refocus.camera->physical->gate_height_mm==rounded_refocus.camera->physical->gate_height_mm,
      "Refocus recrops a frozen sensor gate at rounded preview dimensions")) return 1;
  const auto full_fov=gaussian_camera_optics(*full_refocus.camera),rounded_fov=gaussian_camera_optics(*rounded_refocus.camera);
  if (!check(std::abs(full_fov.value("horizontalFovDegrees").toDouble()-rounded_fov.value("horizontalFovDegrees").toDouble())<0.0001 &&
      std::abs(full_fov.value("verticalFovDegrees").toDouble()-rounded_fov.value("verticalFovDegrees").toDouble())<0.0001,
      "Focus/preview resampling changed the field of view")) return 1;
  if (!check(!gaussian_camera_optics(*authored.camera,0).value("ok").toBool(),"Invalid blur criterion accepted")) return 1;
  for (const double focal:{14.0,85.0,135.0,200.0,2000.0}) for (const double stop:{0.1,0.2,0.5,1.4,22.0,128.0}) {
    auto extreme=measured; extreme.focal_length_mm=focal; extreme.f_number=stop;
    extreme.focus_distance_m=3;
    const auto result=make_gaussian_physical_camera(*authored.camera,{1920,1080},extreme,true);
    if (!check(result.camera && gaussian_physical_camera_consistent(*result.camera) &&
        gaussian_lens_settings(result.camera->photo.lens.description()).has_value() &&
        near(result.camera->photo.lens.aperture_radius*2000*extreme.metres_per_scene_unit,focal/stop),
        "Extreme aperture/zoom failed conversion or persistence")) return 1;
  }
  auto below_minimum=measured.description(); below_minimum["fNumber"]=0.099;
  if (!check(!gaussian_physical_camera_settings(below_minimum),"Subminimum aperture accepted")) return 1;
  for (int fault = 0; fault < 11; ++fault) {
    auto bad = p.description();
    if (fault == 0) bad["metresPerSceneUnit"] = 0;
    if (fault == 1) bad["fNumber"] = 0;
    if (fault == 2) bad["focusDistanceM"] = 0.05;
    if (fault == 3) bad["sensorWidthMm"] = -1;
    if (fault == 4) bad["focalLengthMm"] = 2001;
    if (fault == 5) bad["fNumber"] = "2.8";
    if (fault == 6) bad["metresPerSceneUnit"] = std::numeric_limits<double>::infinity();
    if (fault == 7) bad["extra"] = 1;
    if (fault == 8) bad.remove("sensorHeightMm");
    if (fault == 9) bad["schema"] = "future";
    if (fault == 10) bad["focusDistanceM"] = std::numeric_limits<double>::quiet_NaN();
    if (!check(!gaussian_physical_camera_settings(bad), "Invalid physical calibration accepted")) return 1;
  }
  if (make_gaussian_physical_camera(pose,{0,100},p).camera || make_gaussian_physical_camera(pose,{8193,100},p).camera) return 1;
  pose.right = {0,0,0};
  if (make_gaussian_physical_camera(pose,{100,100},p).camera) return 1;
  std::cout << "Physical camera: calibrated scale, finite-focus projection, f-stops, crop and signed defocus pass\n";
  return 0;
}

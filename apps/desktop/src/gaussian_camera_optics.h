#pragma once
#include "gaussian_physical_camera.h"
#include "gaussian_lens_projection.h"
#include <numbers>

namespace photo_editor::desktop {
inline QVariantMap gaussian_camera_optics(const GaussianCamera& camera,double coc_microns=30) {
  if (!std::isfinite(coc_microns) || coc_microns<=0 || coc_microns>1000 ||
      camera.image_size.isEmpty() || !camera.photo.valid() || camera.fx<=0 || camera.fy<=0 ||
      !gaussian_physical_camera_consistent(camera)) return {{"ok",false},{"error","Invalid camera or circle-of-confusion criterion."}};
  const double width=camera.image_size.width(),height=camera.image_size.height();
  const auto field=[&](double x,double y) {
    return gaussian_lens_field(camera,gaussian_lens_chief_pixel(camera,{x,y}));
  };
  const auto left=field(0,height/2),right=field(width,height/2),top=field(width/2,0),bottom=field(width/2,height);
  constexpr double degrees=180/std::numbers::pi;
  QVariantMap result{{"ok",true},{"horizontalFovDegrees",(std::atan(right.x())-std::atan(left.x()))*degrees},
      {"verticalFovDegrees",(std::atan(bottom.y())-std::atan(top.y()))*degrees},
      {"fovDefinition","image-centre-line-chief-ray-angular-span"},
      {"physical",camera.physical.has_value()},{"circleOfConfusionMicrons",coc_microns}};
  if (!camera.physical) return result;
  const auto& calibration=*camera.physical; const auto& settings=calibration.settings;
  const double focus=settings.focus_distance_m*1000,focal=settings.focal_length_mm;
  const double image=focal*focus/(focus-focal),diameter=focal/settings.f_number;
  const double k=(coc_microns/1000)/(diameter*image);
  const double near=1/(1/focus+k),inverse_far=1/focus-k;
  result.insert("effectiveGateWidthMm",calibration.gate_width_mm);
  result.insert("effectiveGateHeightMm",calibration.gate_height_mm);
  result.insert("focalLengthMm",focal);
  result.insert("imageDistanceMm",image);
  result.insert("entrancePupilDiameterMm",diameter);
  result.insert("focusFromLensM",settings.focus_distance_m);
  result.insert("focusFromSensorM",(focus+image)/1000);
  result.insert("focusPlaneWidthM",calibration.gate_width_mm*focus/image/1000);
  result.insert("focusPlaneHeightM",calibration.gate_height_mm*focus/image/1000);
  result.insert("depthOfFieldNearM",near/1000);
  result.insert("depthOfFieldFarM",inverse_far>0 ? QVariant(1/inverse_far/1000) : QVariant{});
  result.insert("depthOfFieldFarInfinite",inverse_far<=0);
  result.insert("focusModel","ideal-thin-lens-plane-from-lens-principal-plane");
  result.insert("exposureModel","normalized-aperture-no-absolute-radiometry");
  const auto center=camera.position+camera.forward*camera.photo.lens.focus_distance;
  result.insert("focusPlaneCenterScene",QVariantList{center.x(),center.y(),center.z()});
  result.insert("focusPlaneNormal",QVariantList{camera.forward.x(),camera.forward.y(),camera.forward.z()});
  return result;
}
}  // namespace photo_editor::desktop

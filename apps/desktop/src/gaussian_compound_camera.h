#pragma once
#include "gaussian_compound_lens.h"
#include <numbers>

namespace photo_editor::desktop {

// The rig origin is the sensor centre. Focus is measured from the sensor plane,
// as on a physical camera body, rather than from a fictitious thin-lens centre.
struct CompoundCameraCalibration {
  CompoundLensPrescription lens;
  double metres_per_scene_unit=0;
  double sensor_width_mm=36,sensor_height_mm=24;
  double gate_width_mm=36,gate_height_mm=24;
  double focus_from_sensor_m=2;
  double film_distance_mm=0;
  double reference_wavelength_nm=587.5618;
  bool focus_at_infinity=false;
  bool valid() const {
    if (!lens.valid() || !std::isfinite(metres_per_scene_unit) || metres_per_scene_unit<1e-6 || metres_per_scene_unit>1e6 ||
        !std::isfinite(sensor_width_mm) || !std::isfinite(sensor_height_mm) || sensor_width_mm<1 || sensor_width_mm>300 ||
        sensor_height_mm<1 || sensor_height_mm>300 || !std::isfinite(gate_width_mm) || !std::isfinite(gate_height_mm) ||
        gate_width_mm<=0 || gate_height_mm<=0 || gate_width_mm>sensor_width_mm+1e-9 || gate_height_mm>sensor_height_mm+1e-9 ||
        !std::isfinite(film_distance_mm) || film_distance_mm<=0 || film_distance_mm>1e6 ||
        !std::isfinite(focus_from_sensor_m) || focus_from_sensor_m<=0 || focus_from_sensor_m>1e8 ||
        !std::isfinite(reference_wavelength_nm) || reference_wavelength_nm<360 || reference_wavelength_nm>830) return false;
    const auto matrix=compound_paraxial(lens,reference_wavelength_nm);
    if (!matrix || matrix->focal_length_mm()<=0) return false;
    const double object=focus_from_sensor_m*1000-matrix->track_mm-film_distance_mm;
    const double expected=focus_at_infinity ? matrix->back_focus_mm() : matrix->film_distance_mm(object);
    return (focus_at_infinity || object>0) && std::isfinite(expected) &&
        std::abs(expected-film_distance_mm)<1e-7*std::max(1.0,film_distance_mm);
  }
};

inline std::optional<double> compound_focus_from_sensor(const CompoundLensPrescription& lens,
    double focus_distance_mm,double wavelength_nm=587.5618) {
  const auto matrix=compound_paraxial(lens,wavelength_nm);
  if (!matrix || !std::isfinite(focus_distance_mm) || focus_distance_mm<=matrix->track_mm) return {};
  // Object/front distance S = target/sensor distance - lens track - film gap.
  // Enforce B=0 for T(film gap) * M(lens) * T(S), keeping the smaller positive
  // conjugate (the ordinary photographic, rather than reversed-macro, solution).
  const double target=focus_distance_mm-matrix->track_mm;
  const double a=matrix->c,b=-(matrix->c*target+matrix->d-matrix->a),c=-(matrix->a*target+matrix->b);
  const double discriminant=std::fma(b,b,-4*a*c);
  if (discriminant<0) return {};
  const double q=-0.5*(b+std::copysign(std::sqrt(discriminant),b));
  if (a==0 || q==0) return {};
  double best=std::numeric_limits<double>::infinity();
  for (double distance:{q/a,c/q}) {
    const double object=target-distance;
    if (distance>0 && object>0 && std::isfinite(distance) &&
        std::abs(matrix->film_distance_mm(object)-distance)<1e-6*std::max(1.0,distance)) best=std::min(best,distance);
  }
  return std::isfinite(best) ? std::optional(best) : std::nullopt;
}

inline std::optional<CompoundCameraCalibration> make_compound_camera(const CompoundLensPrescription& lens,
    double sensor_width_mm,double sensor_height_mm,int image_width,int image_height,
    double metres_per_scene_unit,double focus_from_sensor_m,bool focus_at_infinity=false) {
  if (image_width<1 || image_height<1 || image_width>8192 || image_height>8192) return {};
  const auto matrix=compound_paraxial(lens);
  const auto film=focus_at_infinity && matrix ? std::optional(matrix->back_focus_mm()) :
      compound_focus_from_sensor(lens,focus_from_sensor_m*1000);
  if (!film) return {};
  const double aspect=static_cast<double>(image_width)/image_height;
  const double gate_width=std::min(sensor_width_mm,sensor_height_mm*aspect);
  CompoundCameraCalibration result{lens,metres_per_scene_unit,sensor_width_mm,sensor_height_mm,
      gate_width,gate_width/aspect,focus_from_sensor_m,*film};
  result.focus_at_infinity=focus_at_infinity;
  return result.valid() ? std::optional(result) : std::nullopt;
}

struct CompoundCameraRay {
  OpticalRay ray;  // Sensor-relative origin in mm, direction toward the scene.
  double irradiance_weight=0;
  double glass_transmission=0;
};
inline std::optional<CompoundCameraRay> compound_camera_ray(const CompoundCameraCalibration& camera,
    double film_u,double film_v,double pupil_u,double pupil_v,double wavelength_nm=587.5618) {
  if (!camera.valid()) return {};
  for (double sample:{film_u,film_v,pupil_u,pupil_v})
    if (!std::isfinite(sample) || sample<0 || sample>1) return {};
  double track=0; for (const auto& surface:camera.lens.surfaces) track+=surface.thickness_mm;
  const double film_z=-track-camera.film_distance_mm;
  // Physical sensor images are inverted. Mirror both film coordinates so the
  // resulting photograph uses the renderer's upright right/down/forward basis.
  const OpticalVector film{(0.5-film_u)*camera.gate_width_mm,(0.5-film_v)*camera.gate_height_mm,film_z};
  const auto bounds=compound_rear_pupil_bounds(camera.lens,film);
  if (!bounds) return {};
  const OpticalVector rear{std::lerp(bounds->x_min,bounds->x_max,pupil_u),
      std::lerp(bounds->y_min,bounds->y_max,pupil_v),-track};
  const OpticalRay initial{film,(rear-film).normalized()};
  const auto trace=trace_compound_lens(camera.lens,initial,true,wavelength_nm);
  if (!trace) return CompoundCameraRay{};  // A blocked sample contributes zero, never renormalize by accepted rays.
  const double area=(bounds->x_max-bounds->x_min)*(bounds->y_max-bounds->y_min);
  const double cosine=initial.direction.z;
  const double weight=area*std::pow(cosine,4)/(camera.film_distance_mm*camera.film_distance_mm)*trace->transmission;
  auto ray=trace->ray; ray.origin.z-=film_z;
  return CompoundCameraRay{ray,weight,trace->transmission};
}

struct CompoundFieldOfView {
  std::optional<double> horizontal_degrees,vertical_degrees,diagonal_degrees;
};
inline CompoundFieldOfView compound_field_of_view(const CompoundCameraCalibration& camera) {
  CompoundFieldOfView result;
  if (!camera.valid()) return result;
  double track=0; for (const auto& surface:camera.lens.surfaces) track+=surface.thickness_mm;
  const double z=-track-camera.film_distance_mm;
  constexpr double degrees=180/std::numbers::pi;
  const auto horizontal=compound_chief_ray(camera.lens,{camera.gate_width_mm/2,0,z},camera.reference_wavelength_nm);
  const auto vertical=compound_chief_ray(camera.lens,{0,camera.gate_height_mm/2,z},camera.reference_wavelength_nm);
  const auto diagonal=compound_chief_ray(camera.lens,{camera.gate_width_mm/2,camera.gate_height_mm/2,z},camera.reference_wavelength_nm);
  if (horizontal) result.horizontal_degrees=2*std::atan2(std::abs(horizontal->ray.direction.x),horizontal->ray.direction.z)*degrees;
  if (vertical) result.vertical_degrees=2*std::atan2(std::abs(vertical->ray.direction.y),vertical->ray.direction.z)*degrees;
  if (diagonal) result.diagonal_degrees=2*std::atan2(std::hypot(diagonal->ray.direction.x,diagonal->ray.direction.y),diagonal->ray.direction.z)*degrees;
  return result;
}

inline std::optional<CompoundLensPrescription> compound_set_f_number(const CompoundLensPrescription& lens,
    double f_number,double wavelength_nm=587.5618) {
  if (!lens.valid() || lens.iris_surface<0 || !std::isfinite(f_number) || f_number<=0) return {};
  auto result=lens; result.iris_radius_mm=0;
  const auto cardinal=compound_paraxial(result,wavelength_nm);
  const auto pupil=compound_entrance_pupil_radius(result,wavelength_nm);
  if (!cardinal || !pupil || cardinal->focal_length_mm()<=0) return {};
  const double target=cardinal->focal_length_mm()/(2*f_number);
  if (target>*pupil*(1+1e-9)) return {};
  double low=0,high=result.surfaces[static_cast<size_t>(result.iris_surface)].aperture_radius_mm;
  for (int iteration=0;iteration<48;++iteration) {
    result.iris_radius_mm=(low+high)/2;
    const auto radius=compound_entrance_pupil_radius(result,wavelength_nm);
    if (!radius) return {};
    if (*radius<target) low=result.iris_radius_mm; else high=result.iris_radius_mm;
  }
  result.iris_radius_mm=(low+high)/2;
  const auto actual=compound_entrance_pupil_radius(result,wavelength_nm);
  if (!actual || std::abs(*actual-target)>std::max(1e-12,target*1e-7)) return {};
  return result;
}
}  // namespace photo_editor::desktop

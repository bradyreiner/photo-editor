#include "gaussian_compound_lens.h"
#include "gaussian_compound_camera.h"
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;
namespace {
bool check(bool value,const char* message) { if (!value) std::cerr<<message<<'\n'; return value; }
bool near(double a,double b,double tolerance=1e-9) { return std::isfinite(a) && std::abs(a-b)<=tolerance; }
}
int main() {
  constexpr double pi=3.14159265358979323846;
  const auto lens=double_gauss_50mm();
  if (!check(lens.valid(),"Published double-Gauss prescription is invalid")) return 1;
  if (!check(near(optical_fresnel(1,1,1.5),0.04) && optical_fresnel(0,1,1)==0,
      "Fresnel normal-incidence or equal-media limit differs")) return 1;
  const auto refracted=optical_refract({0.5,0,std::sqrt(0.75)},{0,0,-1},1,1.5);
  if (!check(refracted && near(refracted->x,1.0/3) && near(refracted->length(),1),"Snell refraction differs")) return 1;
  if (!check(!optical_refract({std::sqrt(0.75),0,0.5},{0,0,-1},1.5,1),"Total internal reflection transmitted")) return 1;
  for (double radius:{-1e8,-100.0,-20.0,20.0,100.0,1e8}) for (double x:{0.0,0.25,2.0}) {
    const auto hit=intersect_optical_surface({{x,0,-10},{0,0,1}},0,radius);
    const double expected=-std::copysign(x*x/(std::abs(radius)+std::sqrt(radius*radius-x*x)),radius);
    if (!check(hit && near(hit->position.x,x) && near(hit->position.z,expected,2e-8),"Spherical sag/cap root differs")) return 1;
    const auto reverse=intersect_optical_surface({{x,0,10},{0,0,-1}},0,radius);
    if (!check(reverse && near(reverse->position.z,expected,2e-8),"Reverse spherical cap selects wrong sphere side")) return 1;
  }
  if (!check(!intersect_optical_surface({{0,0,1},{1,0,0}},0,0),"Parallel ray intersects plane")) return 1;

  // Optical glass coefficients and d/F/C indices: SCHOTT N-BK7 datasheet.
  // https://media.schott.com/api/public/content/41e799d0bf874807a0bb8e702fbb75b5?v=54856406
  OpticalGlass bk7; bk7.sellmeier=true;
  bk7.b={1.03961212,0.231792344,1.01046945}; bk7.c={0.00600069867,0.0200179144,103.560653};
  if (!check(bk7.valid() && near(bk7.index(587.5618),1.51680,5e-6) &&
      near(bk7.index(486.1327),1.52238,5e-6) && near(bk7.index(656.2725),1.51432,5e-6),"Sellmeier does not match glass datasheet")) return 1;
  auto pole=bk7; pole.c[0]=0.55*0.55;
  if (!check(!pole.valid(),"Visible dispersion pole accepted")) return 1;

  // The planar plate must preserve the direction and cause the analytically
  // predicted lateral displacement and two bare-glass Fresnel losses.
  CompoundLensPrescription plate;
  CompoundLensSurface front; front.thickness_mm=5; front.aperture_radius_mm=100; front.glass_after.index_d=1.5;
  CompoundLensSurface back; back.aperture_radius_mm=100; plate.surfaces={front,back};
  const OpticalRay plate_ray{{0,0,10},{0.5,0,-std::sqrt(0.75)}};
  const auto through_plate=trace_compound_lens(plate,plate_ray,false);
  const double inside_angle=std::asin(1.0/3), external_angle=pi/6;
  const double expected_x=10*std::tan(external_angle)+5*std::tan(inside_angle);
  const double transmission=std::pow(1-optical_fresnel(std::cos(external_angle),1,1.5),2);
  if (!check(plate.valid() && through_plate && near(through_plate->ray.origin.x,expected_x) &&
      near(through_plate->ray.origin.z,-5) && (through_plate->ray.direction-plate_ray.direction).length()<1e-10 &&
      near(through_plate->transmission,transmission),"Parallel plate direction, displacement or Fresnel losses differ")) return 1;
  plate.surfaces[0].glass_after.absorption_per_mm=0.1;
  const auto absorbed=trace_compound_lens(plate,plate_ray,false);
  if (!check(absorbed && near(absorbed->transmission/transmission,std::exp(-0.1*5/std::cos(inside_angle))),
      "Beer-Lambert absorption uses wrong medium/path length")) return 1;

  const auto cardinal=compound_paraxial(lens);
  if (!check(cardinal && near(cardinal->focal_length_mm(),50.35816695831366,1e-9) &&
      near(cardinal->back_focus_mm(),36.10590523854215,1e-9) && near(cardinal->track_mm,32.04),
      "Reference paraxial cardinal values differ")) return 1;
  if (!check(near(cardinal->a*cardinal->d-cardinal->b*cardinal->c,1,1e-12),"Air-to-air transfer determinant differs")) return 1;
  // Independent geometric trace of an almost axial parallel ray must converge
  // at the matrix-predicted back focal plane.
  const auto parallel=trace_compound_lens(lens,{{0.001,0,10},{0,0,-1}},false);
  if (!check(parallel && std::abs(parallel->ray.direction.x)>1e-12,"Paraxial ray clipped")) return 1;
  const double crossing=parallel->ray.origin.z-parallel->ray.origin.x*parallel->ray.direction.z/parallel->ray.direction.x;
  if (!check(near(crossing,-cardinal->track_mm-cardinal->back_focus_mm(),2e-6),"Ray trace and independent matrix focus disagree")) return 1;
  int reversed=0;
  for (double object_distance:{300.0,1000.0,2000.0,100000.0}) {
    const double film_distance=cardinal->film_distance_mm(object_distance);
    const double film_z=-cardinal->track_mm-film_distance;
    for (double film_x:{-6.0,0.0,6.0}) for (double pupil_x:{-4.0,-1.0,1.0,4.0}) {
      const OpticalRay original{{film_x,0,film_z},(OpticalVector{pupil_x,0,-cardinal->track_mm}-OpticalVector{film_x,0,film_z}).normalized()};
      std::vector<OpticalVector> vertices;
      const auto forward=trace_compound_lens(lens,original,true,587.5618,&vertices);
      if (!forward) continue;
      const auto backward=trace_compound_lens(lens,{forward->ray.origin,forward->ray.direction*(-1)},false);
      if (!check(backward && forward->interfaces==11 && vertices.size()==12,"Reverse traversal or interface count differs")) return 1;
      const double t=(film_z-backward->ray.origin.z)/backward->ray.direction.z;
      const auto returned=backward->ray.origin+backward->ray.direction*t;
      if (!check((returned-original.origin).length()<1e-7 &&
          (backward->ray.direction+original.direction).length()<1e-9 &&
          near(backward->transmission,forward->transmission,1e-10),"Optical reversibility differs")) return 1;
      ++reversed;
    }
  }
  if (!check(reversed>=32,"Insufficient transmitted reversibility cases")) return 1;
  auto iris=lens; iris.iris_radius_mm=1;
  if (!check(trace_compound_lens(iris,{{0,0,-70},{0,0,1}},true).has_value() &&
      !trace_compound_lens(iris,{{20,0,-70},{0,0,1}},true),"Iris fails to pass centre or reject clipped ray")) return 1;
  iris.blades=5;
  if (!check(compound_iris_contains(iris,{0.98,0,0},1) &&
      !compound_iris_contains(iris,{0.98*std::cos(pi/5),0.98*std::sin(pi/5),0},1),"Odd polygon iris orientation differs")) return 1;
  iris.roundness=1;
  if (!check(compound_iris_contains(iris,{0.98*std::cos(pi/5),0.98*std::sin(pi/5),0},1),"Rounded iris fails circular limit")) return 1;
  const double infinity_film_z=-cardinal->track_mm-cardinal->back_focus_mm();
  const auto small_field=compound_chief_ray(lens,{0.001,0,infinity_film_z});
  if (!check(small_field && near(small_field->ray.direction.x/small_field->ray.direction.z,
      -0.001/cardinal->focal_length_mm(),2e-10),"Chief ray does not match infinitesimal field angle")) return 1;
  for (double film_x:{-12.0,-6.0,0.0,6.0,12.0}) {
    const auto chief=compound_chief_ray(lens,{film_x,0,infinity_film_z});
    if (!check(chief.has_value(),"Chief ray solver failed inside supported field")) return 1;
    std::vector<OpticalVector> path;
    const auto reverse=trace_compound_lens(lens,{chief->ray.origin,chief->ray.direction*(-1)},false,587.5618,&path);
    if (!check(reverse && path.size()==12 && std::hypot(path[6].x,path[6].y)<1e-8,
        "Chief ray does not pass the iris centre")) return 1;
    const auto rotated=compound_chief_ray(lens,{0,film_x,infinity_film_z});
    if (!check(rotated && near(rotated->ray.direction.y,chief->ray.direction.x,1e-10) &&
        near(rotated->ray.direction.z,chief->ray.direction.z,1e-10),"Chief rays violate axial symmetry")) return 1;
  }
  const auto pupil=compound_entrance_pupil_radius(lens);
  auto stopped=lens; stopped.iris_radius_mm=2;
  const auto narrow_pupil=compound_entrance_pupil_radius(stopped);
  if (!check(pupil && narrow_pupil && *pupil>*narrow_pupil && *pupil>lens.surfaces[5].aperture_radius_mm,
      "Entrance pupil incorrectly equated with physical iris opening")) return 1;
  const auto bounds=compound_rear_pupil_bounds(lens,{12,0,infinity_film_z});
  if (!check(bounds && bounds->x_min<=-lens.surfaces.back().aperture_radius_mm &&
      bounds->x_max>=lens.surfaces.back().aperture_radius_mm && bounds->y_min<bounds->y_max,
      "Rear pupil proposal does not contain conservative cap projection")) return 1;
  const auto physical=make_compound_camera(lens,36,24,1200,800,1,2);
  if (!check(physical && physical->valid(),"Sensor-referenced compound camera did not calibrate")) return 1;
  const auto focus_ray=compound_camera_ray(*physical,0.5,0.5,0.50005,0.5);
  if (!check(focus_ray && focus_ray->irradiance_weight>0 && std::abs(focus_ray->ray.direction.x)>1e-12,
      "Near-axis physical camera ray failed")) return 1;
  const double sensor_crossing=focus_ray->ray.origin.z-focus_ray->ray.origin.x*focus_ray->ray.direction.z/focus_ray->ray.direction.x;
  if (!check(near(sensor_crossing,2000,0.001),"Compound focus is not measured from the sensor plane")) return 1;
  const auto field=compound_field_of_view(*physical);
  if (!check(field.horizontal_degrees && field.vertical_degrees && *field.horizontal_degrees>30 &&
      *field.horizontal_degrees<50 && *field.vertical_degrees<*field.horizontal_degrees,"Traced field of view failed")) return 1;
  auto frozen=*physical; frozen.gate_width_mm=37;
  if (!check(!frozen.valid(),"Film gate larger than the sensor accepted")) return 1;
  auto cropped=*physical; cropped.gate_width_mm*=0.9; cropped.gate_height_mm*=0.9;
  const auto cropped_field=compound_field_of_view(cropped);
  if (!check(cropped.valid() && cropped_field.horizontal_degrees &&
      *cropped_field.horizontal_degrees<*field.horizontal_degrees && cropped.film_distance_mm==physical->film_distance_mm,
      "Active sensor crop changed focus or failed to narrow the traced field")) return 1;
  const auto infinity=make_compound_camera(lens,36,24,1200,800,1,2,true);
  if (!check(infinity && near(infinity->film_distance_mm,cardinal->back_focus_mm()),"Infinity focus does not use back focal plane")) return 1;
  const auto stopped_lens=compound_set_f_number(lens,4);
  if (!check(stopped_lens && near(cardinal->focal_length_mm()/(2*(*compound_entrance_pupil_radius(*stopped_lens))),4,1e-6) &&
      !compound_set_f_number(lens,1.4),"Physical iris calibration or maximum opening enforcement failed")) return 1;
  if (!check(!make_compound_camera(lens,36,24,1200,800,0,2) &&
      !make_compound_camera(lens,36,24,1200,800,1,0.05),"Unscaled or impossible-focus camera accepted")) return 1;
  for (int fault=0;fault<7;++fault) {
    auto bad=lens;
    if (fault==0) bad.surfaces[0].radius_mm=1;
    if (fault==1) bad.surfaces[0].thickness_mm=-1;
    if (fault==2) bad.surfaces.back().glass_after.index_d=1.5;
    if (fault==3) bad.iris_radius_mm=100;
    if (fault==4) bad.blades=2;
    if (fault==5) bad.surfaces[0].radius_mm=std::numeric_limits<double>::quiet_NaN();
    if (fault==6) bad.surfaces[0].glass_after.absorption_per_mm=-1;
    if (!check(!bad.valid(),"Invalid lens accepted")) return 1;
  }
  std::cout<<"Compound optics: Snell/Fresnel, spherical caps, planar plate, Sellmeier, absorption, paraxial focus, "
      <<reversed<<" reversible rays and iris clipping pass\n";
  std::cout<<"Double Gauss: EFL="<<cardinal->focal_length_mm()<<" mm; entrance pupil diameter="<<2*(*pupil)
      <<" mm; infinity f-number="<<cardinal->focal_length_mm()/(2*(*pupil))<<'\n';
  return 0;
}

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace photo_editor::desktop {

// Optical coordinates are millimetres, with the front vertex at z=0 and the
// scene at positive z. Prescription order is scene -> film. Positive radii have
// their curvature centre on the film side. Keep the reference in double precision.
struct OpticalVector {
  double x = 0, y = 0, z = 0;
  OpticalVector operator+(OpticalVector v) const { return {x+v.x,y+v.y,z+v.z}; }
  OpticalVector operator-(OpticalVector v) const { return {x-v.x,y-v.y,z-v.z}; }
  OpticalVector operator*(double s) const { return {x*s,y*s,z*s}; }
  double dot(OpticalVector v) const { return x*v.x+y*v.y+z*v.z; }
  double length() const { return std::sqrt(dot(*this)); }
  bool finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
  OpticalVector normalized() const { const double n=length(); return n>0 ? *this*(1/n) : OpticalVector{}; }
};
struct OpticalRay { OpticalVector origin, direction; };

struct OpticalGlass {
  double index_d = 1;
  bool sellmeier = false;
  std::array<double,3> b{}, c{};  // Sellmeier wavelength is in micrometres.
  double absorption_per_mm = 0;
  double index(double wavelength_nm) const {
    if (!std::isfinite(wavelength_nm) || wavelength_nm<360 || wavelength_nm>830)
      return std::numeric_limits<double>::quiet_NaN();
    if (!sellmeier) return index_d;
    const double squared=std::pow(wavelength_nm/1000,2);
    double n2=1;
    for (size_t i=0;i<3;++i) n2+=b[i]*squared/(squared-c[i]);
    return n2>0 ? std::sqrt(n2) : std::numeric_limits<double>::quiet_NaN();
  }
  bool valid() const {
    if (!std::isfinite(absorption_per_mm) || absorption_per_mm<0 || absorption_per_mm>100 ||
        !std::isfinite(index_d) || index_d<1 || index_d>3) return false;
    for (size_t i=0;i<3;++i) if (!std::isfinite(b[i]) || !std::isfinite(c[i])) return false;
    // Reject poles anywhere in the supported visible interval, not just at the
    // three validation wavelengths. Out-of-band poles are normal for Sellmeier.
    if (sellmeier) for (double pole:c) if (pole>=0.36*0.36 && pole<=0.83*0.83) return false;
    for (double wavelength:{360.0,486.1327,587.5618,656.2725,830.0}) {
      const double n=index(wavelength); if (!std::isfinite(n) || n<1 || n>3) return false;
    }
    return true;
  }
};
struct CompoundLensSurface {
  double radius_mm = 0, thickness_mm = 0, aperture_radius_mm = 1;
  OpticalGlass glass_after;
  bool stop = false;
};
struct CompoundLensPrescription {
  std::vector<CompoundLensSurface> surfaces;
  int iris_surface = -1;
  double iris_radius_mm = 0;  // Zero selects the prescribed maximum opening.
  int blades = 0;
  double roundness = 0, rotation_degrees = 0;
  bool valid() const {
    if (surfaces.size()<2 || surfaces.size()>32 || iris_surface<-1 ||
        iris_surface>=static_cast<int>(surfaces.size()) ||
        !std::isfinite(iris_radius_mm) || iris_radius_mm<0 ||
        !std::isfinite(roundness) || roundness<0 || roundness>1 ||
        !std::isfinite(rotation_degrees) || (blades!=0 && (blades<3 || blades>16))) return false;
    double track=0;
    for (size_t i=0;i<surfaces.size();++i) {
      const auto& s=surfaces[i];
      if (!std::isfinite(s.radius_mm) || std::abs(s.radius_mm)>1e9 ||
          !std::isfinite(s.thickness_mm) || s.thickness_mm<0 || s.thickness_mm>10000 ||
          (i+1<surfaces.size() && s.thickness_mm<=0) ||
          !std::isfinite(s.aperture_radius_mm) || s.aperture_radius_mm<=0 || s.aperture_radius_mm>1000 ||
          (s.radius_mm!=0 && std::abs(s.radius_mm)<s.aperture_radius_mm) ||
          (s.stop && s.radius_mm!=0) || !s.glass_after.valid()) return false;
      track+=s.thickness_mm;
    }
    if (track<=0 || track>10000 || surfaces.back().thickness_mm!=0 || surfaces.front().stop ||
        surfaces.back().stop || surfaces.back().glass_after.index_d!=1 || surfaces.back().glass_after.sellmeier) return false;
    return iris_surface<0 ? iris_radius_mm==0 : surfaces[static_cast<size_t>(iris_surface)].stop &&
        iris_radius_mm<=surfaces[static_cast<size_t>(iris_surface)].aperture_radius_mm;
  }
};

inline double optical_fresnel(double cos_i,double n_i,double n_t) {
  if (n_i==n_t) return 0;
  cos_i=std::clamp(std::abs(cos_i),0.0,1.0);
  const double sin2_t=std::pow(n_i/n_t,2)*(1-cos_i*cos_i);
  if (sin2_t>=1) return 1;
  const double cos_t=std::sqrt(1-sin2_t);
  const double s=(n_i*cos_i-n_t*cos_t)/(n_i*cos_i+n_t*cos_t);
  const double p=(n_t*cos_i-n_i*cos_t)/(n_t*cos_i+n_i*cos_t);
  return (s*s+p*p)*0.5;
}
inline std::optional<OpticalVector> optical_refract(OpticalVector direction,OpticalVector normal,
    double n_i,double n_t) {
  if (!direction.finite() || !normal.finite() || !std::isfinite(n_i) || !std::isfinite(n_t) || n_i<1 || n_t<1 ||
      direction.length()==0 || normal.length()==0) return {};
  direction=direction.normalized(); normal=normal.normalized();
  if (direction.dot(normal)>0) normal=normal*(-1);
  const double cos_i=std::clamp(-direction.dot(normal),0.0,1.0), eta=n_i/n_t;
  const double discriminant=1-eta*eta*(1-cos_i*cos_i);
  if (discriminant<0) return {};
  return (direction*eta+normal*(eta*cos_i-std::sqrt(discriminant))).normalized();
}

struct OpticalSurfaceHit { OpticalVector position, normal; double distance_mm=0; };
inline std::optional<OpticalSurfaceHit> intersect_optical_surface(const OpticalRay& ray,
    double vertex_z,double radius) {
  if (!ray.origin.finite() || !ray.direction.finite() || !std::isfinite(vertex_z) || !std::isfinite(radius) ||
      std::abs(ray.direction.length()-1)>1e-9) return {};
  if (radius==0) {
    if (std::abs(ray.direction.z)<1e-15) return {};
    const double t=(vertex_z-ray.origin.z)/ray.direction.z;
    if (t < -1e-9) return {};
    return OpticalSurfaceHit{ray.origin+ray.direction*std::max(0.0,t),{0,0,1},std::max(0.0,t)};
  }
  const auto relative=ray.origin-OpticalVector{0,0,vertex_z};
  // Expand around the vertex to avoid subtracting R^2 from an almost equal
  // centre-distance squared for weakly curved photographic surfaces.
  const double b=2*(relative.dot(ray.direction)+radius*ray.direction.z);
  const double c=relative.dot(relative)+2*radius*relative.z;
  const double discriminant=std::fma(b,b,-4*c);
  if (discriminant<0) return {};
  const double q=-0.5*(b+std::copysign(std::sqrt(discriminant),b));
  std::array<double,2> roots{q,q!=0 ? c/q : -b/2};
  std::sort(roots.begin(),roots.end());
  for (double t:roots) {
    if (t < -1e-9 || !std::isfinite(t)) continue;
    t=std::max(0.0,t);
    const auto p=ray.origin+ray.direction*t;
    const auto normal=(p-OpticalVector{0,0,vertex_z-radius}).normalized();
    // Select the cap containing the vertex, never the other side of the sphere.
    if (normal.z*radius < -1e-9) continue;
    return OpticalSurfaceHit{p,normal,t};
  }
  return {};
}

inline bool compound_iris_contains(const CompoundLensPrescription& lens,OpticalVector p,double radius) {
  const double r=std::hypot(p.x,p.y);
  if (r>radius*(1+1e-12)) return false;
  if (lens.blades==0 || lens.roundness==1 || r==0) return true;
  constexpr double pi=3.14159265358979323846;
  const double half=pi/lens.blades;
  double angle=std::fmod(std::atan2(p.y,p.x)-lens.rotation_degrees*pi/180,2*half);
  if (angle<0) angle+=2*half;
  const double boundary=std::cos(half)/std::cos(angle-half);
  return r<=radius*(boundary+lens.roundness*(1-boundary))*(1+1e-12);
}

struct CompoundLensTrace {
  OpticalRay ray;
  double transmission=1, optical_path_mm=0;
  int interfaces=0;
};
inline std::optional<CompoundLensTrace> trace_compound_lens_path(const CompoundLensPrescription& lens,
    OpticalRay ray,bool film_to_scene,double wavelength_nm,std::vector<OpticalVector>* vertices,
    int last_surface=-1,bool ignore_target_stop=false) {
  if (!lens.valid() || !ray.origin.finite() || !ray.direction.finite() || ray.direction.length()==0 ||
      !std::isfinite(wavelength_nm) || wavelength_nm<360 || wavelength_nm>830 ||
      (film_to_scene ? ray.direction.z<=0 : ray.direction.z>=0)) return {};
  ray.direction=ray.direction.normalized();
  std::array<double,32> positions{}; std::array<OpticalGlass,32> media_before{};
  OpticalGlass medium;
  double z=0;
  for (size_t i=0;i<lens.surfaces.size();++i) {
    positions[i]=z; media_before[i]=medium;
    if (!lens.surfaces[i].stop) medium=lens.surfaces[i].glass_after;
    z-=lens.surfaces[i].thickness_mm;
  }
  CompoundLensTrace result;
  medium=film_to_scene ? lens.surfaces.back().glass_after : OpticalGlass{};
  if (vertices) vertices->push_back(ray.origin);
  const int count=static_cast<int>(lens.surfaces.size());
  for (int step=0;step<count;++step) {
    const int selected=film_to_scene ? count-1-step : step;
    const auto i=static_cast<size_t>(selected); const auto& surface=lens.surfaces[i];
    const auto hit=intersect_optical_surface(ray,positions[i],surface.radius_mm);
    if (!hit) return {};
    const double radius=selected==lens.iris_surface && lens.iris_radius_mm>0 ? lens.iris_radius_mm : surface.aperture_radius_mm;
    const bool solve_stop=ignore_target_stop && surface.stop && selected==last_surface;
    if (!solve_stop && (std::hypot(hit->position.x,hit->position.y)>radius*(1+1e-12) ||
        (selected==lens.iris_surface && !compound_iris_contains(lens,hit->position,radius)))) return {};
    const double n_i=medium.index(wavelength_nm);
    result.transmission*=std::exp(-medium.absorption_per_mm*hit->distance_mm);
    result.optical_path_mm+=n_i*hit->distance_mm;
    ray.origin=hit->position;
    if (!surface.stop) {
      const auto next=film_to_scene ? media_before[i] : surface.glass_after;
      const double n_t=next.index(wavelength_nm);
      const auto direction=optical_refract(ray.direction,hit->normal,n_i,n_t);
      if (!direction) return {};
      result.transmission*=1-optical_fresnel(ray.direction.dot(hit->normal),n_i,n_t);
      ray.direction=*direction; medium=next;
    }
    if (vertices) vertices->push_back(ray.origin);
    ++result.interfaces;
    if (selected==last_surface) break;
  }
  result.ray=ray;
  return result;
}
inline std::optional<CompoundLensTrace> trace_compound_lens(const CompoundLensPrescription& lens,
    OpticalRay ray,bool film_to_scene,double wavelength_nm=587.5618,std::vector<OpticalVector>* vertices=nullptr) {
  return trace_compound_lens_path(lens,ray,film_to_scene,wavelength_nm,vertices);
}

struct CompoundParaxial {
  double a=1,b=0,c=0,d=1,track_mm=0;
  double focal_length_mm() const { return -1/c; }
  double back_focus_mm() const { return -a/c; }
  double film_distance_mm(double object_distance_mm) const {
    return -(a*object_distance_mm+b)/(c*object_distance_mm+d);
  }
};
inline std::optional<CompoundParaxial> compound_paraxial(const CompoundLensPrescription& lens,double wavelength_nm=587.5618) {
  if (!lens.valid()) return {};
  CompoundParaxial result; double index=1;
  for (const auto& surface:lens.surfaces) {
    if (!surface.stop) {
      const double next=surface.glass_after.index(wavelength_nm);
      if (!std::isfinite(next)) return {};
      const double power=surface.radius_mm==0 ? 0 : (next-index)/surface.radius_mm;
      result.c-=power*result.a; result.d-=power*result.b; index=next;
    }
    result.a+=surface.thickness_mm/index*result.c;
    result.b+=surface.thickness_mm/index*result.d;
    result.track_mm+=surface.thickness_mm;
  }
  return std::abs(result.c)>1e-15 ? std::optional(result) : std::nullopt;
}

// The chief ray passes through the centre of the physical iris. Aiming at the
// centre of the rear element is generally a different ray. Solve the radial
// shoot through the rear glass, then validate the complete transmitted path.
inline std::optional<CompoundLensTrace> compound_chief_ray(const CompoundLensPrescription& lens,
    OpticalVector film_position,double wavelength_nm=587.5618) {
  if (!lens.valid() || lens.iris_surface<0 || !film_position.finite()) return {};
  double rear_z=0;
  for (const auto& surface:lens.surfaces) rear_z-=surface.thickness_mm;
  if (film_position.z>=rear_z) return {};
  const double radial=std::hypot(film_position.x,film_position.y);
  const OpticalVector axis=radial>0 ? OpticalVector{film_position.x/radial,film_position.y/radial,0} : OpticalVector{1,0,0};
  const auto ray_at=[&](double coordinate) {
    return OpticalRay{film_position,(axis*coordinate+OpticalVector{0,0,rear_z}-film_position).normalized()};
  };
  const auto residual=[&](double coordinate) -> std::optional<double> {
    const auto trace=trace_compound_lens_path(lens,ray_at(coordinate),true,wavelength_nm,nullptr,lens.iris_surface,true);
    return trace ? std::optional(trace->ray.origin.dot(axis)) : std::nullopt;
  };
  const double rear_radius=lens.surfaces.back().aperture_radius_mm;
  double coordinate=0, best=std::numeric_limits<double>::infinity();
  for (int i=-12;i<=12;++i) {
    const double trial=rear_radius*static_cast<double>(i)/12;
    const auto value=residual(trial);
    if (value && std::abs(*value)<best) { coordinate=trial; best=std::abs(*value); }
  }
  if (!std::isfinite(best)) return {};
  for (int iteration=0;iteration<32;++iteration) {
    const auto value=residual(coordinate); if (!value) return {};
    if (std::abs(*value)<1e-9) return trace_compound_lens(lens,ray_at(coordinate),true,wavelength_nm);
    const double h=std::max(1e-7,rear_radius*1e-5);
    const auto plus=residual(coordinate+h),minus=residual(coordinate-h);
    if (!plus || !minus) return {};
    const double derivative=(*plus-*minus)/(2*h);
    if (std::abs(derivative)<1e-12) return {};
    double step=std::clamp(*value/derivative,-rear_radius*0.5,rear_radius*0.5);
    bool advanced=false;
    for (int damping=0;damping<16;++damping) {
      const double next=coordinate-step; const auto candidate=residual(next);
      if (candidate && std::abs(*candidate)<std::abs(*value)) { coordinate=next; advanced=true; break; }
      step*=0.5;
    }
    if (!advanced) return {};
  }
  return {};
}

inline std::optional<double> compound_entrance_pupil_radius(const CompoundLensPrescription& lens,
    double wavelength_nm=587.5618) {
  if (!lens.valid()) return {};
  // Nominal aperture uses the circular iris envelope. Rotating polygon blades
  // changes the bokeh outline, not the nominal f-number. Actual ray throughput
  // still integrates the prescribed blade shape, including its reduced area.
  auto circular=lens; circular.blades=0; circular.roundness=0;
  const auto passes=[&](double height) {
    return trace_compound_lens(circular,{{height,0,10},{0,0,-1}},false,wavelength_nm).has_value();
  };
  if (!passes(0)) return {};
  double low=0,high=lens.surfaces.front().aperture_radius_mm*(1+1e-6);
  for (int iteration=0;iteration<64;++iteration) {
    const double midpoint=(low+high)/2;
    if (passes(midpoint)) low=midpoint; else high=midpoint;
  }
  return low;
}

// Conservative rectangle on the rear vertex plane, obtained by projecting the
// entire rear spherical cap from this film point. Sampling only the physical
// rear aperture disk on that plane can incorrectly discard valid oblique rays.
struct CompoundPupilBounds { double x_min=0,x_max=0,y_min=0,y_max=0; };
inline std::optional<CompoundPupilBounds> compound_rear_pupil_bounds(const CompoundLensPrescription& lens,
    OpticalVector film_position) {
  if (!lens.valid() || !film_position.finite()) return {};
  double rear_z=0; for (const auto& surface:lens.surfaces) rear_z-=surface.thickness_mm;
  const double distance=rear_z-film_position.z;
  const auto& surface=lens.surfaces.back(); const double radius=surface.aperture_radius_mm;
  if (distance<=0) return {};
  const double r=surface.radius_mm;
  const double sag=r==0 ? 0 : -std::copysign(radius*radius/(std::abs(r)+std::sqrt(r*r-radius*radius)),r);
  if (distance+sag<=0) return {};
  const double t=distance/(distance+sag);
  CompoundPupilBounds bounds{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()};
  for (double factor:{1.0,t}) for (double edge:{-radius,radius}) {
    const double x=film_position.x+factor*(edge-film_position.x),y=film_position.y+factor*(edge-film_position.y);
    bounds.x_min=std::min(bounds.x_min,x); bounds.x_max=std::max(bounds.x_max,x);
    bounds.y_min=std::min(bounds.y_min,y); bounds.y_max=std::max(bounds.y_max,y);
  }
  return bounds;
}

// Numeric optical prescription, not copied renderer code. Provenance: Tronnier,
// US patent 2,673,491; the 50 mm table distributed with PBRT v4 scene examples:
// https://raw.githubusercontent.com/mmp/pbrt-v4-scenes/master/lenses/dgauss.50mm.dat
inline CompoundLensPrescription double_gauss_50mm() {
  constexpr std::array<std::array<double,4>,11> table{{
      {29.475,3.76,1.67,25.2},{84.83,0.12,1,25.2},{19.275,4.025,1.67,23},
      {40.77,3.275,1.699,23},{12.75,5.705,1,18},{0,4.5,0,17.1},
      {-14.495,1.18,1.603,17},{40.77,6.065,1.658,20},{-20.385,0.19,1,20},
      {437.065,3.22,1.717,20},{-39.73,0,1,20}}};
  CompoundLensPrescription result;
  for (const auto& row:table) {
    CompoundLensSurface surface; surface.radius_mm=row[0]; surface.thickness_mm=row[1];
    surface.glass_after.index_d=row[2]==0 ? 1 : row[2]; surface.aperture_radius_mm=row[3]/2;
    surface.stop=row[2]==0; result.surfaces.push_back(surface);
  }
  result.iris_surface=5;
  return result;
}
}  // namespace photo_editor::desktop

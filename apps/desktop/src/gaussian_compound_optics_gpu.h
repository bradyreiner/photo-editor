#pragma once
#include "gaussian_compound_camera.h"
#include <cstdint>

namespace photo_editor::desktop {
struct alignas(16) CompoundGpuSurface {
  std::array<float,4> shape{};  // Vertex z, signed curvature radius, clear radius, stop flag.
  std::array<float,4> medium{}; // Film-side index, scene-side index, film-side absorption/mm.
};
struct alignas(16) CompoundGpuCamera {
  std::array<uint32_t,4> counts{};
  std::array<float,4> gate{};  // Active width, height, film z, film/rear gap (mm).
  std::array<float,4> rear{};  // Rear vertex z, clear radius, edge sag, mm/scene-unit.
  std::array<float,4> iris{};  // Variable radius, roundness, rotation radians.
};
static_assert(sizeof(CompoundGpuSurface)==32 && sizeof(CompoundGpuCamera)==64);
struct CompoundGpuPrescription {
  CompoundGpuCamera camera;
  std::array<CompoundGpuSurface,32> surfaces{};
};
inline std::optional<CompoundGpuPrescription> pack_compound_optics(const CompoundCameraCalibration& calibration,
    double wavelength_nm=587.5618) {
  if (!calibration.valid() || !std::isfinite(wavelength_nm) || wavelength_nm<360 || wavelength_nm>830) return {};
  CompoundGpuPrescription result;
  auto& camera=result.camera;
  camera.counts={static_cast<uint32_t>(calibration.lens.surfaces.size()),
      calibration.lens.iris_surface<0 ? 0xffffffffU : static_cast<uint32_t>(calibration.lens.iris_surface),
      static_cast<uint32_t>(calibration.lens.blades),0};
  double z=0; OpticalGlass medium;
  for (size_t i=0;i<calibration.lens.surfaces.size();++i) {
    const auto& surface=calibration.lens.surfaces[i]; const auto before=medium;
    if (!surface.stop) medium=surface.glass_after;
    result.surfaces[i].shape={static_cast<float>(z),static_cast<float>(surface.radius_mm),
        static_cast<float>(surface.aperture_radius_mm),surface.stop ? 1.0F : 0.0F};
    result.surfaces[i].medium={static_cast<float>(medium.index(wavelength_nm)),
        static_cast<float>(before.index(wavelength_nm)),static_cast<float>(medium.absorption_per_mm),0};
    z-=surface.thickness_mm;
  }
  const auto& back=calibration.lens.surfaces.back();
  const double radius=back.radius_mm,clear=back.aperture_radius_mm;
  const double sag=radius==0 ? 0 : -std::copysign(clear*clear/(std::abs(radius)+std::sqrt(radius*radius-clear*clear)),radius);
  camera.gate={static_cast<float>(calibration.gate_width_mm),static_cast<float>(calibration.gate_height_mm),
      static_cast<float>(z-calibration.film_distance_mm),static_cast<float>(calibration.film_distance_mm)};
  camera.rear={static_cast<float>(z),static_cast<float>(clear),static_cast<float>(sag),
      static_cast<float>(calibration.metres_per_scene_unit*1000)};
  camera.iris={static_cast<float>(calibration.lens.iris_radius_mm),static_cast<float>(calibration.lens.roundness),
      static_cast<float>(calibration.lens.rotation_degrees*std::numbers::pi/180),0};
  return result;
}
}  // namespace photo_editor::desktop

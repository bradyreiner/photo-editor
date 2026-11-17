#pragma once
#include "gaussian_scene_data.h"
#include <QVector2D>
#include <numbers>

namespace photo_editor::desktop {
inline double gaussian_radical_inverse(unsigned index, unsigned base) {
  double value = 0, factor = 1.0 / base;
  while (index) { value += (index % base) * factor; index /= base; factor /= base; }
  return value;
}
// Importance sample the radial pupil response r^edge, rejected against a
// rounded polygon boundary. The zero-edge case keeps the original uniform-area
// sequence exactly. Rejection avoids polygon-corner bias; prefixes are stable.
inline std::vector<QVector2D> gaussian_aperture_samples(const GaussianLensSettings& lens) {
  if (!lens.valid()) return {};
  if (lens.aperture_radius == 0) return {{0, 0}};
  std::vector<QVector2D> result;
  const double pi = std::numbers::pi;
  const double rotation = lens.rotation_degrees * pi / 180;
  for (unsigned index = 1; result.size() < static_cast<size_t>(lens.samples) && index < 65536; ++index) {
    const double u = gaussian_radical_inverse(index, 2);
    const double radius = lens.aperture_edge == 0 ? std::sqrt(u) : std::pow(u,1.0/(2+lens.aperture_edge));
    const double angle = 2 * pi * gaussian_radical_inverse(index, 3);
    if (lens.blades != 0) {
      const double half_sector = pi / lens.blades;
      const double edge_angle = std::fmod(angle, 2 * half_sector) - half_sector;
      const double polygon_radius = std::cos(half_sector) / std::cos(edge_angle);
      if (radius > polygon_radius + lens.roundness * (1 - polygon_radius)) continue;
    }
    const double stretch = std::sqrt(lens.bokeh_aspect);
    result.emplace_back(static_cast<float>(radius * std::cos(angle + rotation) * lens.aperture_radius / stretch),
        static_cast<float>(radius * std::sin(angle + rotation) * lens.aperture_radius * stretch));
  }
  return result.size() == static_cast<size_t>(lens.samples) ? result : std::vector<QVector2D>{};
}
// Parallel off-axis views converge at the same plane: projected displacement
// is focal * apertureOffset * (1/focusDistance - 1/depth).
inline GaussianCamera gaussian_aperture_camera(const GaussianCamera& camera, QVector2D offset) {
  auto result = camera;
  // An internal pupil ray camera is no longer an authored physical shot.
  result.physical.reset();
  result.position += camera.right * offset.x() + camera.down * offset.y();
  result.cx += camera.fx * camera.photo.focal_scale * offset.x() / camera.photo.lens.focus_distance;
  result.cy += camera.fy * camera.photo.focal_scale * offset.y() / camera.photo.lens.focus_distance;
  return result;
}
}  // namespace photo_editor::desktop

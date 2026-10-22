#pragma once

#include <array>
#include <string_view>

namespace photo_editor::pipeline {

// Artistic stock families, not calibrated emulsion measurements. Size is in
// document pixels. The spectral/tonal coefficients remain separate from the
// editable starting values so equal slider values still have distinct structure.
struct FilmGrainProfile final {
  std::string_view id, name, description;
  double size, strength, color, roughness;
  double fine_weight, cluster_scale, shadow_response, highlight_response;
  std::array<double, 3> dye_scale;
};

inline constexpr std::array film_grain_profiles{
    FilmGrainProfile{"color_100", "Color negative 100",
        "Ektar-inspired: tight dye clouds and restrained color texture.",
        0.85, 0.028, -0.48, 0.22, 0.85, 1.8, 0.55, 0.55, {1.0, 0.86, 1.12}},
    FilmGrainProfile{"portrait_400", "Portrait negative 400",
        "Portra-inspired: soft, overlapping dye clouds and gentle highlights.",
        1.25, 0.042, -0.62, 0.38, 0.65, 2.4, 0.80, 0.45, {1.08, 0.90, 1.22}},
    FilmGrainProfile{"cinema_500t", "Cinema negative 500T",
        "Vision3-inspired: layered color texture with quieter deep shadows.",
        1.4, 0.045, -0.30, 0.42, 0.72, 2.8, 0.38, 0.70, {1.12, 0.90, 1.35}},
    FilmGrainProfile{"tabular_100", "Tabular silver 100",
        "T-Max-inspired: fine, even silver texture with little clumping.",
        0.8, 0.028, -1.0, 0.18, 0.94, 1.7, 0.55, 0.62, {1.0, 1.0, 1.0}},
    FilmGrainProfile{"classic_400", "Classic silver 400",
        "Tri-X-inspired: irregular silver clusters and a pronounced midtone bite.",
        1.65, 0.060, -1.0, 0.65, 0.48, 2.6, 0.95, 0.78, {1.0, 1.0, 1.0}},
    FilmGrainProfile{"pushed_3200", "Pushed silver 3200",
        "High-speed silver character: coarse clusters and stronger shadow texture.",
        2.3, 0.085, -1.0, 0.86, 0.36, 3.2, 1.35, 0.88, {1.0, 1.0, 1.0}}};

inline const FilmGrainProfile* find_film_grain_profile(std::string_view id) {
  for (const auto& profile : film_grain_profiles)
    if (profile.id == id) return &profile;
  return nullptr;
}

}  // namespace photo_editor::pipeline

#pragma once

#include "photo_editor/io/openexr_io.h"
#include "photo_editor/pipeline/color_transform.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {

inline constexpr auto kOpenExrSourceColorContract = "openexr-source-color:v2|chromaticities-to-linear-srgb|bradford-rgb:xyz-absolute|float-unbounded|interop-v1";

// EXR primaries may be imaginary (ACES) or lie on x+y=1 (ProPhoto).
// Construct from the stored chromaticities, not an optional descriptive name
// or QColorSpace's more restrictive primary validation.
inline std::optional<photo_editor::pipeline::ColorMatrix3> openexr_to_linear_srgb_matrix(
    const photo_editor::io::OpenExrChromaticities& c) {
  using namespace photo_editor::pipeline;
  const auto rgb_to_xyz = [](const photo_editor::io::OpenExrChromaticities& p)
      -> std::optional<ColorMatrix3> {
    for (const auto value : {p.red_x, p.red_y, p.green_x, p.green_y,
                            p.blue_x, p.blue_y, p.white_x, p.white_y}) {
      if (!std::isfinite(value)) return std::nullopt;
    }
    if (p.white_x <= 0 || p.white_y <= 0 ||
        static_cast<double>(p.white_x) + p.white_y >= 1.0) return std::nullopt;
    const auto base = matrix3_from_rows(
        p.red_x, p.green_x, p.blue_x,
        p.red_y, p.green_y, p.blue_y,
        1.0 - p.red_x - p.red_y, 1.0 - p.green_x - p.green_y, 1.0 - p.blue_x - p.blue_y);
    const auto& m = base.values;
    const auto determinant = m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-12) return std::nullopt;
    const auto inverse = invert_matrix3(base);
    const auto norm = [](const ColorMatrix3& matrix) {
      double result = 0;
      for (std::size_t row = 0; row < 3; ++row) {
        double sum = 0;
        for (std::size_t col = 0; col < 3; ++col) sum += std::abs(matrix.values[row * 3 + col]);
        result = std::max(result, sum);
      }
      return result;
    };
    if (norm(base) * norm(inverse) > 1e10) return std::nullopt;
    const auto white = xyz_from_chromaticity({p.white_x, p.white_y});
    const auto scales = multiply_matrix3_vector(inverse, {white.x, white.y, white.z});
    auto result = base;
    for (std::size_t col = 0; col < 3; ++col) {
      if (!std::isfinite(scales[col]) || std::abs(scales[col]) <= 1e-12) return std::nullopt;
      for (std::size_t row = 0; row < 3; ++row) result.values[row * 3 + col] *= scales[col];
    }
    return result;
  };
  const auto source = rgb_to_xyz(c);
  // Match Qt's linear-sRGB tag and OpenEXR's default chromaticities.
  const auto target_c = photo_editor::io::openexr_chromaticities_for_color_space(make_linear_srgb_color_space());
  const auto target = rgb_to_xyz(target_c);
  if (!source || !target) return std::nullopt;
  const auto source_white = xyz_from_chromaticity({c.white_x, c.white_y});
  const auto target_white = xyz_from_chromaticity({target_c.white_x, target_c.white_y});
  const auto response = multiply_matrix3_xyz(chromatic_adaptation_cone_matrix("Bradford"), source_white);
  if (response.x <= 1e-6 || response.y <= 1e-6 || response.z <= 1e-6) return std::nullopt;
  // Pure XYZ channel encodings use equal-energy "white" to define the basis,
  // not an adopted scene illuminant. Adapting E to D65 here changes the colors
  // in OpenEXR's equivalent XYZ/Rec709 reference images. RGB working spaces,
  // including ACES/ProPhoto, retain their reference-white adaptation contract.
  const auto near = [](double a, double b) { return std::abs(a - b) <= 1e-7; };
  const auto xyz_basis = near(c.red_x, 1) && near(c.red_y, 0) &&
      near(c.green_x, 0) && near(c.green_y, 1) && near(c.blue_x, 0) && near(c.blue_y, 0) &&
      near(c.white_x, 1.0 / 3.0) && near(c.white_y, 1.0 / 3.0);
  const auto adaptation = xyz_basis ? identity_matrix3() :
      chromatic_adaptation_matrix(source_white, target_white, "Bradford");
  auto result = multiply_matrix3(invert_matrix3(*target), multiply_matrix3(adaptation, *source));
  const auto identity = identity_matrix3();
  auto identity_error = 0.0;
  for (std::size_t i = 0; i < result.values.size(); ++i) {
    if (!std::isfinite(result.values[i])) return std::nullopt;
    identity_error = std::max(identity_error, std::abs(result.values[i] - identity.values[i]));
  }
  if (identity_error < 1e-12) result = identity;
  return result;
}

}  // namespace photo_editor::desktop

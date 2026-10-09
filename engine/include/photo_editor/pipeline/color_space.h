#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace photo_editor::pipeline {

enum class ColorPrimaries {
  Unknown,
  Srgb,
  DisplayP3,
  AdobeRgb,
  ProPhotoRgb,
  Rec709,
  Rec2020,
  SonySGamut3,
  SonySGamut3Cine,
  AcesAp0,
  AcesAp1,
};

enum class TransferFunction {
  Unknown,
  Linear,
  Srgb,
  Gamma18,
  Gamma22,
  Gamma24,
  Bt2020,
  Acescct,
  Bt709,
};

enum class WhitePoint {
  Unknown,
  D50,
  D65,
  AcesD60,
};

struct RgbColorSpace final {
  bool operator==(const RgbColorSpace&) const = default;
  std::string name;
  ColorPrimaries primaries = ColorPrimaries::Unknown;
  TransferFunction transfer_function = TransferFunction::Unknown;
  WhitePoint white_point = WhitePoint::Unknown;
  bool scene_referred = false;
  bool wide_gamut = false;
};

[[nodiscard]] RgbColorSpace make_linear_srgb_color_space();
[[nodiscard]] RgbColorSpace make_linear_display_p3_color_space();
[[nodiscard]] RgbColorSpace make_linear_prophoto_rgb_color_space();
[[nodiscard]] RgbColorSpace make_linear_rec2020_color_space();
[[nodiscard]] RgbColorSpace make_aces2065_1_color_space();
[[nodiscard]] RgbColorSpace make_acescg_color_space();
[[nodiscard]] RgbColorSpace make_acescct_color_space();
[[nodiscard]] RgbColorSpace make_srgb_color_space();
[[nodiscard]] RgbColorSpace make_display_p3_color_space();
[[nodiscard]] RgbColorSpace make_adobe_rgb_color_space();
[[nodiscard]] RgbColorSpace make_prophoto_rgb_color_space();
[[nodiscard]] RgbColorSpace make_rec709_color_space();
[[nodiscard]] RgbColorSpace make_linear_rec709_color_space();
[[nodiscard]] RgbColorSpace make_rec2020_color_space();
[[nodiscard]] RgbColorSpace make_linear_sony_s_gamut3_color_space();
[[nodiscard]] RgbColorSpace make_linear_sony_s_gamut3_cine_color_space();
// Resolves the canonical built-in RGB spaces accepted at declared color
// boundaries such as creative LUT inputs and outputs. Matching ignores ASCII
// punctuation, whitespace, and case so serialized aliases remain stable.
[[nodiscard]] std::optional<RgbColorSpace> resolve_builtin_rgb_color_space(
    std::string_view name);

}  // namespace photo_editor::pipeline

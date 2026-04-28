#pragma once

#include <string>

namespace photo_editor::pipeline {

enum class ColorPrimaries {
  Unknown,
  Srgb,
  DisplayP3,
  AdobeRgb,
  ProPhotoRgb,
  Rec709,
  Rec2020,
};

enum class TransferFunction {
  Unknown,
  Linear,
  Srgb,
  Gamma22,
  Gamma24,
  Bt2020,
};

enum class WhitePoint {
  Unknown,
  D50,
  D65,
};

struct RgbColorSpace final {
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
[[nodiscard]] RgbColorSpace make_srgb_color_space();
[[nodiscard]] RgbColorSpace make_display_p3_color_space();
[[nodiscard]] RgbColorSpace make_adobe_rgb_color_space();
[[nodiscard]] RgbColorSpace make_prophoto_rgb_color_space();
[[nodiscard]] RgbColorSpace make_rec709_color_space();
[[nodiscard]] RgbColorSpace make_rec2020_color_space();

}  // namespace photo_editor::pipeline

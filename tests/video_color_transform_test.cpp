#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "photo_editor/pipeline/video_color_transform.h"

namespace {

bool almost_equal(double left, double right, double tolerance = 1.0e-9) {
  return std::abs(left - right) <= tolerance;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using photo_editor::pipeline::VideoInputTransformProfile;
  constexpr auto code_maximum = 1023.0;

  // Sony's technical summary lists these full-range 10-bit S-Log3 anchors:
  // 0% reflection -> CV 95, 18% gray -> CV 420, 90% white -> CV 598.
  constexpr auto reference_vectors = std::array{
      std::array<double, 2>{0.0, 95.0},
      std::array<double, 2>{0.18, 420.0},
      std::array<double, 2>{0.90, 598.0},
  };
  for (const auto& [reflection, code_value] : reference_vectors) {
    const auto normalized_code = code_value / code_maximum;
    const auto encoded_code = photo_editor::pipeline::encode_sony_slog3(reflection) *
                              code_maximum;
    if (std::round(encoded_code) != code_value ||
        !almost_equal(
            photo_editor::pipeline::decode_sony_slog3(normalized_code),
            reflection,
            0.002)) {
      return fail("S-Log3 encoding and decoding should match Sony's 10-bit anchor vectors");
    }
  }

  constexpr auto breakpoint = 0.01125;
  const auto breakpoint_code = 171.2102946929 / code_maximum;
  if (!almost_equal(
          photo_editor::pipeline::encode_sony_slog3(breakpoint),
          breakpoint_code,
          1.0e-12) ||
      !almost_equal(
          photo_editor::pipeline::decode_sony_slog3(breakpoint_code),
          breakpoint,
          1.0e-12)) {
    return fail("S-Log3's linear and logarithmic branches should meet at Sony's breakpoint");
  }

  for (const auto reflection : {-0.005, 0.0, 0.005, breakpoint, 0.18, 0.90, 1.5}) {
    const auto round_trip = photo_editor::pipeline::decode_sony_slog3(
        photo_editor::pipeline::encode_sony_slog3(reflection));
    if (!almost_equal(round_trip, reflection, 1.0e-10)) {
      return fail("S-Log3's extended-range transfer should round-trip finite reflections");
    }
  }

  if (!almost_equal(photo_editor::pipeline::encode_rec709_oetf(0.0), 0.0) ||
      !almost_equal(photo_editor::pipeline::encode_rec709_oetf(0.018), 0.081) ||
      !almost_equal(photo_editor::pipeline::encode_rec709_oetf(1.0), 1.0)) {
    return fail("BT.709 output encoding should match its linear toe and unity anchors");
  }

  const auto working_space = photo_editor::pipeline::make_linear_srgb_color_space();
  for (const auto profile : {
           VideoInputTransformProfile::SonySLog3SGamut3,
           VideoInputTransformProfile::SonySLog3SGamut3Cine,
       }) {
    const auto input_transform =
        photo_editor::pipeline::compile_video_input_transform(profile, working_space);
    if (!input_transform.has_value()) {
      return fail("both Sony S-Log3 camera spaces should compile into the working space");
    }
    const auto gray = photo_editor::pipeline::apply_video_input_transform(
        *input_transform,
        photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(420.0 / code_maximum),
            .green = static_cast<float>(420.0 / code_maximum),
            .blue = static_cast<float>(420.0 / code_maximum),
            .alpha = 0.4F,
        });
    if (!almost_equal(gray.red, 0.18, 2.0e-6) ||
        !almost_equal(gray.green, 0.18, 2.0e-6) ||
        !almost_equal(gray.blue, 0.18, 2.0e-6) ||
        !almost_equal(gray.alpha, 0.4, 1.0e-7)) {
      return fail("both S-Gamut3 profiles should preserve neutral 18% gray and alpha");
    }

    const auto lut_settings = photo_editor::pipeline::VideoTransformLutSettings{};
    const auto lut = photo_editor::pipeline::build_sony_slog3_linear_rec709_lut(
        profile, lut_settings);
    if (!lut.ok() || lut.lut.size != 2 ||
        lut.lut.shaper_size != 16385 ||
        lut.lut.output_color_space != "Linear Rec.709" ||
        !std::isfinite(lut.linear_rec709_output_range[0]) ||
        !std::isfinite(lut.linear_rec709_output_range[1]) ||
        lut.linear_rec709_output_range[1] <= lut.linear_rec709_output_range[0] ||
        lut.validation_sample_count != 23U * 23U * 23U ||
        lut.measured_p99_absolute_error > lut_settings.p99_absolute_error_limit ||
        lut.measured_maximum_absolute_error > lut_settings.maximum_absolute_error_limit) {
      return fail("both Sony input transforms should pass the linear Rec.709 LUT error gates");
    }
  }

  const auto output_transform =
      photo_editor::pipeline::compile_video_rec709_output_transform(working_space);
  if (!output_transform.has_value()) {
    return fail("the BT.709 output transform should compile from the working space");
  }
  const auto encoded_gray = photo_editor::pipeline::apply_video_rec709_output_transform(
      *output_transform,
      photo_editor::pipeline::ColorPixel{0.18F, 0.18F, 0.18F, 0.4F});
  const auto expected_gray = 1.099 * std::pow(0.18, 0.45) - 0.099;
  if (!almost_equal(encoded_gray.red, expected_gray, 2.0e-6) ||
      !almost_equal(encoded_gray.green, expected_gray, 2.0e-6) ||
      !almost_equal(encoded_gray.blue, expected_gray, 2.0e-6) ||
      !almost_equal(encoded_gray.alpha, 0.4, 1.0e-7)) {
    return fail("the Rec.709 frame output should apply its OETF and preserve alpha");
  }

  return EXIT_SUCCESS;
}

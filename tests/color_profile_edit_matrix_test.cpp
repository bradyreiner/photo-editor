#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_metrics.h"
#include "photo_editor/pipeline/color_transform.h"

#include "../engine/src/pipeline/color_working_space_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(PHOTO_EDITOR_COLOR_MATRIX_TEST_HAS_LCMS2)
#include <lcms2.h>
#endif

namespace {

using namespace photo_editor::pipeline;

struct MatrixReference final {
  std::string_view name;
  RgbColorSpace space;
  std::array<double, 9> rgb_to_xyz;
};

using Matrix = std::array<double, 9>;
using Vector = std::array<double, 3>;

constexpr double kMatrixTolerance = 3.0e-4;
constexpr double kProfileEditXyzTolerance = 1.0e-3;

[[nodiscard]] Matrix multiply(const Matrix& left, const Matrix& right) {
  Matrix result{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      for (std::size_t inner = 0; inner < 3; ++inner) {
        result[row * 3 + column] +=
            left[row * 3 + inner] * right[inner * 3 + column];
      }
    }
  }
  return result;
}

[[nodiscard]] Vector multiply(const Matrix& matrix, const Vector& value) {
  return {
      matrix[0] * value[0] + matrix[1] * value[1] + matrix[2] * value[2],
      matrix[3] * value[0] + matrix[4] * value[1] + matrix[5] * value[2],
      matrix[6] * value[0] + matrix[7] * value[1] + matrix[8] * value[2],
  };
}

[[nodiscard]] Matrix inverse(const Matrix& m) {
  const auto determinant =
      m[0] * (m[4] * m[8] - m[5] * m[7]) -
      m[1] * (m[3] * m[8] - m[5] * m[6]) +
      m[2] * (m[3] * m[7] - m[4] * m[6]);
  if (std::abs(determinant) <= 1.0e-15) {
    return {};
  }
  const auto scale = 1.0 / determinant;
  return {
      (m[4] * m[8] - m[5] * m[7]) * scale,
      (m[2] * m[7] - m[1] * m[8]) * scale,
      (m[1] * m[5] - m[2] * m[4]) * scale,
      (m[5] * m[6] - m[3] * m[8]) * scale,
      (m[0] * m[8] - m[2] * m[6]) * scale,
      (m[2] * m[3] - m[0] * m[5]) * scale,
      (m[3] * m[7] - m[4] * m[6]) * scale,
      (m[1] * m[6] - m[0] * m[7]) * scale,
      (m[0] * m[4] - m[1] * m[3]) * scale,
  };
}

[[nodiscard]] Vector xyz_from_xy(double x, double y) {
  return {x / y, 1.0, (1.0 - x - y) / y};
}

[[nodiscard]] Matrix adaptation_reference(
    const Vector& source_white,
    const Vector& destination_white,
    std::string_view method) {
  // Published Bradford and CAT02 cone-response matrices. The adaptation is
  // calculated here independently of the engine's chromatic-adaptation code.
  const Matrix cone = method == "CAT02"
      ? Matrix{0.7328, 0.4296, -0.1624,
               -0.7036, 1.6975, 0.0061,
               0.0030, 0.0136, 0.9834}
      : Matrix{0.8951, 0.2664, -0.1614,
               -0.7502, 1.7135, 0.0367,
               0.0389, -0.0685, 1.0296};
  const auto source_response = multiply(cone, source_white);
  const auto destination_response = multiply(cone, destination_white);
  const auto scale = Matrix{
      destination_response[0] / source_response[0], 0.0, 0.0,
      0.0, destination_response[1] / source_response[1], 0.0,
      0.0, 0.0, destination_response[2] / source_response[2]};
  return multiply(inverse(cone), multiply(scale, cone));
}

[[nodiscard]] double max_component_difference(
    const Matrix& actual,
    const Matrix& expected) {
  auto maximum = 0.0;
  for (std::size_t index = 0; index < actual.size(); ++index) {
    maximum = std::max(maximum, std::abs(actual[index] - expected[index]));
  }
  return maximum;
}

[[nodiscard]] double max_component_difference(
    const Vector& actual,
    const Vector& expected) {
  auto maximum = 0.0;
  for (std::size_t index = 0; index < actual.size(); ++index) {
    maximum = std::max(maximum, std::abs(actual[index] - expected[index]));
  }
  return maximum;
}

[[nodiscard]] std::vector<MatrixReference> supported_matrix_references() {
  auto adobe_linear = make_adobe_rgb_color_space();
  adobe_linear.name = "Linear Adobe RGB";
  adobe_linear.transfer_function = TransferFunction::Linear;
  adobe_linear.scene_referred = true;

  return {
      {"IEC 61966-2-1 linear sRGB", make_linear_srgb_color_space(),
       {0.4123908, 0.3575843, 0.1804808,
        0.2126390, 0.7151687, 0.0721923,
        0.0193308, 0.1191948, 0.9505322}},
      {"Display P3 D65", make_linear_display_p3_color_space(),
       {0.48657095, 0.26566769, 0.19821729,
        0.22897456, 0.69173852, 0.07928691,
        0.00000000, 0.04511338, 1.04394437}},
      {"Adobe RGB D65", adobe_linear,
       {0.5767309, 0.1855540, 0.1881852,
        0.2973769, 0.6273491, 0.0752741,
        0.0270343, 0.0706872, 0.9911085}},
      {"ProPhoto RGB D50", make_linear_prophoto_rgb_color_space(),
       {0.7976749, 0.1351917, 0.0313534,
        0.2880402, 0.7118741, 0.0000857,
        0.0000000, 0.0000000, 0.8252100}},
      {"Rec.2020 D65", make_linear_rec2020_color_space(),
       {0.63695805, 0.14461690, 0.16888098,
        0.26270021, 0.67799807, 0.05930172,
        0.00000000, 0.02807269, 1.06098506}},
      {"ACEScg AP1 D60", make_acescg_color_space(),
       {0.66245418, 0.13400421, 0.15618769,
        0.27222872, 0.67408177, 0.05368952,
        -0.00557465, 0.00406073, 1.01033910}},
      {"ACES2065-1 AP0 D60", make_aces2065_1_color_space(),
       {0.95255240, 0.00000000, 0.00000000,
        0.34396645, 0.72816610, -0.07213255,
        0.00000000, 0.00000000, 1.00882518}},
  };
}

[[nodiscard]] bool verify_working_profile_name_roundtrips() {
  struct WorkingProfileNameReference final {
    std::string_view input;
    std::string_view canonical_name;
    WorkingTransformKind kind;
    RgbColorSpace color_space;
  };
  const std::array<WorkingProfileNameReference, 10> references{{
      {"Linear Adobe RGB", "Linear Adobe RGB", WorkingTransformKind::LinearAdobeRgb,
       make_linear_adobe_rgb_color_space()},
      {"Adobe RGB Linear", "Linear Adobe RGB", WorkingTransformKind::LinearAdobeRgb,
       make_linear_adobe_rgb_color_space()},
      {"Linear Adobe RGB 1998", "Linear Adobe RGB", WorkingTransformKind::LinearAdobeRgb,
       make_linear_adobe_rgb_color_space()},
      {"ACEScg", "ACEScg", WorkingTransformKind::LinearAcesCg,
       make_acescg_color_space()},
      {"Linear ACEScg", "ACEScg", WorkingTransformKind::LinearAcesCg,
       make_acescg_color_space()},
      {"ACEScg/AP1", "ACEScg", WorkingTransformKind::LinearAcesCg,
       make_acescg_color_space()},
      {"Linear ACEScg/AP1", "ACEScg", WorkingTransformKind::LinearAcesCg,
       make_acescg_color_space()},
      {"ACES2065-1", "ACES2065-1", WorkingTransformKind::LinearAces2065_1,
       make_aces2065_1_color_space()},
      {"Linear ACES2065-1", "ACES2065-1", WorkingTransformKind::LinearAces2065_1,
       make_aces2065_1_color_space()},
      {"Linear ACES2065-1/AP0", "ACES2065-1", WorkingTransformKind::LinearAces2065_1,
       make_aces2065_1_color_space()},
  }};
  for (const auto& reference : references) {
    const auto normalized = normalize_working_color_profile_name(reference.input);
    const auto profile = working_color_profile_from_name(reference.input);
    const auto resolved = resolve_builtin_rgb_color_space(reference.canonical_name);
    if (normalized != reference.canonical_name ||
        profile.profile_name != reference.canonical_name ||
        profile.kind != reference.kind || profile.color_space != reference.color_space ||
        profile.color_space.primaries == ColorPrimaries::ProPhotoRgb ||
        profile.color_space.transfer_function != TransferFunction::Linear ||
        !profile.color_space.scene_referred || !resolved.has_value() ||
        *resolved != reference.color_space) {
      std::cerr << "working-profile name failed canonical identity/matrix roundtrip: input="
                << reference.input << " normalized=" << normalized
                << " kind=" << static_cast<int>(profile.kind)
                << " primaries=" << static_cast<int>(profile.color_space.primaries) << '\n';
      return false;
    }
  }
  return true;
}

[[nodiscard]] Vector xy_white_for(const RgbColorSpace& space) {
  switch (space.white_point) {
    case WhitePoint::D50:
      return xyz_from_xy(0.34567, 0.35850);
    case WhitePoint::AcesD60:
      return xyz_from_xy(0.32168, 0.33767);
    case WhitePoint::D65:
    case WhitePoint::Unknown:
      return xyz_from_xy(0.31271, 0.32902);
  }
  return xyz_from_xy(0.31271, 0.32902);
}

[[nodiscard]] bool verify_published_matrix_vectors() {
  for (const auto& reference : supported_matrix_references()) {
    const auto actual = rgb_to_xyz_matrix(reference.space);
    const auto error = max_component_difference(actual.values, reference.rgb_to_xyz);
    if (error > kMatrixTolerance) {
      std::cerr << "published RGB-to-XYZ matrix mismatch: " << reference.name
                << " maxError=" << error << '\n';
      return false;
    }
  }

  // Rounded ICC CAT reference vectors for D65-to-D50, applied to XYZ basis
  // vectors. These values are independent of the engine's cone-response path.
  constexpr std::array<double, 9> expected_bradford{
      1.0478112, 0.0228866, -0.0501270,
      0.0295424, 0.9904844, -0.0170491,
      -0.0092345, 0.0150436, 0.7521316};
  constexpr std::array<double, 9> expected_cat02{
      1.0425739, 0.0308911, -0.0528126,
      0.0221935, 1.0018566, -0.0210738,
      -0.0011649, -0.0034205, 0.7617891};
  const Vector d65{0.95047, 1.0, 1.08883};
  const Vector d50{0.96422, 1.0, 0.82521};
  const ColorXyz engine_d65{.x = d65[0], .y = d65[1], .z = d65[2]};
  const ColorXyz engine_d50{.x = d50[0], .y = d50[1], .z = d50[2]};
  for (const auto method : {std::string_view("Bradford"), std::string_view("CAT02")}) {
    const auto actual = chromatic_adaptation_matrix(
        engine_d65, engine_d50, method).values;
    const auto& expected = method == "Bradford" ? expected_bradford : expected_cat02;
    const auto error = max_component_difference(actual, expected);
    const auto tolerance = method == "Bradford" ? 2.0e-5 : 3.0e-4;
    if (error > tolerance) {
      std::cerr << "published " << method << " D65-to-D50 adaptation mismatch maxError="
                << error << '\n';
      return false;
    }

    const auto independent = adaptation_reference(d65, d50, method);
    if (max_component_difference(actual, independent) > 3.0e-4) {
      std::cerr << method << " adaptation differs from independent cone reference\n";
      return false;
    }
  }
  return true;
}

[[nodiscard]] float srgb_decode(float value) {
  return value <= 0.04045F
      ? value / 12.92F
      : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] ColorPixel display_hue_patch(double hue, float intensity = 1.0F) {
  constexpr auto saturation = 0.70;
  constexpr auto value = 0.80;
  auto h = hue * 6.0;
  const auto sector = static_cast<int>(std::floor(h));
  const auto fraction = h - std::floor(h);
  const auto p = value * (1.0 - saturation);
  const auto q = value * (1.0 - saturation * fraction);
  const auto t = value * (1.0 - saturation * (1.0 - fraction));
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  switch (sector % 6) {
    case 0: red = value; green = t; blue = p; break;
    case 1: red = q; green = value; blue = p; break;
    case 2: red = p; green = value; blue = t; break;
    case 3: red = p; green = q; blue = value; break;
    case 4: red = t; green = p; blue = value; break;
    default: red = value; green = p; blue = q; break;
  }
  return {
      srgb_decode(static_cast<float>(red)) * intensity,
      srgb_decode(static_cast<float>(green)) * intensity,
      srgb_decode(static_cast<float>(blue)) * intensity,
      0.37F};
}

[[nodiscard]] Vector working_pixel_from_d65_xyz(
    const Vector& d65_xyz,
    const MatrixReference& profile,
    std::string_view method) {
  const auto d65_white = xyz_from_xy(0.31271, 0.32902);
  const auto native_white = xy_white_for(profile.space);
  const auto d65_to_native = adaptation_reference(d65_white, native_white, method);
  return multiply(inverse(profile.rgb_to_xyz), multiply(d65_to_native, d65_xyz));
}

[[nodiscard]] Vector working_pixel_to_d65_xyz(
    const ColorPixel& pixel,
    const MatrixReference& profile,
    std::string_view method) {
  const auto native_xyz = multiply(
      profile.rgb_to_xyz,
      Vector{static_cast<double>(pixel.red),
             static_cast<double>(pixel.green),
             static_cast<double>(pixel.blue)});
  const auto native_to_d65 = adaptation_reference(
      xy_white_for(profile.space), xyz_from_xy(0.31271, 0.32902), method);
  return multiply(native_to_d65, native_xyz);
}

[[nodiscard]] bool apply_one_pixel(
    ColorPixel input,
    const ColorAdjustmentState& state,
    ColorPixel* output) {
  std::vector<ColorPixel> pixels{input};
  if (!apply_color_look_stage(&pixels, 1, 1, state) || pixels.size() != 1U) {
    return false;
  }
  *output = pixels.front();
  return true;
}

[[nodiscard]] bool verify_profile_edit_matrix() {
  constexpr std::array<double, 8> hue_centers{0.0, 0.09, 0.15, 0.32, 0.50, 0.64, 0.78, 0.88};
  const auto profiles = supported_matrix_references();
  for (const auto method : {std::string_view("Bradford"), std::string_view("CAT02")}) {
    for (std::size_t hue_index = 0; hue_index < hue_centers.size(); ++hue_index) {
      const auto source = display_hue_patch(hue_centers[hue_index]);
      const auto source_xyz = multiply(
          profiles[0].rgb_to_xyz,
          Vector{static_cast<double>(source.red),
                 static_cast<double>(source.green),
                 static_cast<double>(source.blue)});
      ColorPixel reference_output;
      auto reference_state = ColorAdjustmentState{};
      reference_state.color_pipeline.working_profile.color_space = profiles[0].space;
      reference_state.input_transform.chromatic_adaptation_method = std::string(method);
      reference_state.color_mixer_channels[hue_index].hue = 40.0;
      if (!apply_one_pixel(source, reference_state, &reference_output)) {
        std::cerr << "sRGB reference edit failed for hue " << hue_index << '\n';
        return false;
      }
      const auto reference_xyz = working_pixel_to_d65_xyz(
          reference_output, profiles[0], method);

      for (const auto& profile : profiles) {
        const auto working_input = working_pixel_from_d65_xyz(
            source_xyz, profile, method);
        ColorPixel input{
            static_cast<float>(working_input[0]),
            static_cast<float>(working_input[1]),
            static_cast<float>(working_input[2]),
            source.alpha};
        auto state = ColorAdjustmentState{};
        state.color_pipeline.working_profile.color_space = profile.space;
        state.input_transform.chromatic_adaptation_method = std::string(method);
        state.color_mixer_channels[hue_index].hue = 40.0;
        ColorPixel output;
        if (!apply_one_pixel(input, state, &output)) {
          std::cerr << "profile edit failed: " << profile.name
                    << " method=" << method << " hueIndex=" << hue_index << '\n';
          return false;
        }
        const auto xyz = working_pixel_to_d65_xyz(output, profile, method);
        const auto error = max_component_difference(xyz, reference_xyz);
        if (error > kProfileEditXyzTolerance || output.alpha != input.alpha) {
          std::cerr << "profile-aware edit changed physical color: " << profile.name
                    << " method=" << method << " hueIndex=" << hue_index
                    << " xyzError=" << error << " alpha=" << output.alpha << '\n';
          return false;
        }
      }
    }
  }
  return true;
}

[[nodiscard]] bool verify_selector_boundary_continuity() {
  struct SelectorRegion final {
    double center;
    double inner;
    double outer;
  };
  // These are the documented display-hue selector regions. Keeping the
  // fixtures here makes the test independent of the runtime weight function.
  constexpr std::array<SelectorRegion, 8> regions{{
      {0.00, 0.05, 0.10}, {0.09, 0.04, 0.09},
      {0.15, 0.04, 0.08}, {0.32, 0.10, 0.18},
      {0.50, 0.06, 0.10}, {0.64, 0.08, 0.13},
      {0.78, 0.06, 0.10}, {0.88, 0.05, 0.10},
  }};
  constexpr Matrix srgb_to_xyz{
      0.4123908, 0.3575843, 0.1804808,
      0.2126390, 0.7151687, 0.0721923,
      0.0193308, 0.1191948, 0.9505322};
  constexpr auto hue_step = 1.0e-5;
  constexpr auto maximum_edit_delta_jump_xyz = 0.003;

  const auto edit_delta = [&](double hue, std::size_t channel) -> Vector {
    const auto input = display_hue_patch(hue);
    auto state = ColorAdjustmentState{};
    state.color_pipeline.working_profile.color_space = make_linear_srgb_color_space();
    state.color_mixer_channels[channel].hue = 70.0;
    ColorPixel output;
    if (!apply_one_pixel(input, state, &output)) {
      return {std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::quiet_NaN()};
    }
    const auto input_xyz = multiply(
        srgb_to_xyz,
        Vector{static_cast<double>(input.red),
               static_cast<double>(input.green),
               static_cast<double>(input.blue)});
    const auto output_xyz = multiply(
        srgb_to_xyz,
        Vector{static_cast<double>(output.red),
               static_cast<double>(output.green),
               static_cast<double>(output.blue)});
    return {output_xyz[0] - input_xyz[0],
            output_xyz[1] - input_xyz[1],
            output_xyz[2] - input_xyz[2]};
  };
  const auto max_jump = [&](double boundary, std::size_t channel) {
    const auto left = edit_delta(boundary - hue_step, channel);
    const auto right = edit_delta(boundary + hue_step, channel);
    return max_component_difference(left, right);
  };

  for (std::size_t channel = 0; channel < regions.size(); ++channel) {
    const auto region = regions[channel];
    for (const auto boundary : {
             region.center - region.inner,
             region.center + region.inner,
             region.center - region.outer,
             region.center + region.outer}) {
      const auto jump = max_jump(boundary, channel);
      if (!std::isfinite(jump) || jump > maximum_edit_delta_jump_xyz) {
        std::cerr << "selector edit is discontinuous at a region boundary: channel="
                  << channel << " hue=" << boundary << " xyzDeltaJump=" << jump << '\n';
        return false;
      }
    }
  }

  // The red selector wraps across hue 0/1. Verify both the selector response
  // and the display-hue conversion stay continuous at that seam.
  const auto seam_jump = max_jump(0.0, 0);
  if (!std::isfinite(seam_jump) || seam_jump > maximum_edit_delta_jump_xyz) {
    std::cerr << "red selector edit is discontinuous at the hue seam: xyzDeltaJump="
              << seam_jump << '\n';
    return false;
  }
  return true;
}

[[nodiscard]] bool verify_alpha_signed_hdr_and_faint_color() {
  const std::array<double, 8> hue_centers{0.0, 0.09, 0.15, 0.32, 0.50, 0.64, 0.78, 0.88};
  const auto profiles = supported_matrix_references();

  // The previous peak gate returned exactly zero at 1e-5. Test three
  // adjacent light levels around that boundary and compare physical XYZ, not
  // unlike working-space RGB values. The edit response must remain visible and
  // continuous for every selector hue and every supported working matrix.
  for (const auto method : {std::string_view("Bradford"), std::string_view("CAT02")}) {
    for (const auto& profile : profiles) {
      for (std::size_t hue = 0; hue < hue_centers.size(); ++hue) {
        std::array<double, 3> relative_changes{};
        for (std::size_t level = 0; level < relative_changes.size(); ++level) {
          const std::array<float, 3> intensities{5.0e-6F, 1.0e-5F, 2.0e-5F};
          const auto source = display_hue_patch(hue_centers[hue], intensities[level]);
          const auto source_xyz = multiply(
              profiles.front().rgb_to_xyz,
              Vector{static_cast<double>(source.red),
                     static_cast<double>(source.green),
                     static_cast<double>(source.blue)});
          const auto working = working_pixel_from_d65_xyz(source_xyz, profile, method);
          const ColorPixel input{
              static_cast<float>(working[0]),
              static_cast<float>(working[1]),
              static_cast<float>(working[2]),
              source.alpha};
          auto state = ColorAdjustmentState{};
          state.color_pipeline.working_profile.color_space = profile.space;
          state.input_transform.chromatic_adaptation_method = std::string(method);
          state.color_mixer_channels[hue].hue = 100.0;
          ColorPixel output;
          if (!apply_one_pixel(input, state, &output)) {
            std::cerr << "faint-color mixer failed: " << profile.name
                      << " method=" << method << " hue=" << hue << '\n';
            return false;
          }
          const auto input_xyz = working_pixel_to_d65_xyz(input, profile, method);
          const auto output_xyz = working_pixel_to_d65_xyz(output, profile, method);
          const auto change = max_component_difference(input_xyz, output_xyz);
          const auto input_peak = std::max({input.red, input.green, input.blue});
          relative_changes[level] = change / static_cast<double>(input_peak);
          if (!(relative_changes[level] > 0.01)) {
            std::cerr << "faint real color was suppressed: " << profile.name
                      << " method=" << method << " hue=" << hue
                      << " peak=" << input_peak << " xyzChange=" << change << '\n';
            return false;
          }
          if (output.alpha != input.alpha || !std::isfinite(output.red) ||
              !std::isfinite(output.green) || !std::isfinite(output.blue)) {
            std::cerr << "faint-color edit violated finite/alpha contract\n";
            return false;
          }
        }
        const auto response_range = std::max({
            relative_changes[0], relative_changes[1], relative_changes[2]}) -
            std::min({relative_changes[0], relative_changes[1], relative_changes[2]});
        if (response_range > 0.02) {
          std::cerr << "faint-color response has a brightness discontinuity: "
                    << profile.name << " method=" << method << " hue=" << hue
                    << " spread=" << response_range << '\n';
          return false;
        }
      }
    }
  }

  // Exact black carries no hue and must remain a finite identity for all
  // alpha values, even while every channel mixer is active.
  for (const auto& profile : profiles) {
    for (const auto alpha : {0.0F, 0.37F, 1.0F}) {
      auto state = ColorAdjustmentState{};
      state.color_pipeline.working_profile.color_space = profile.space;
      for (auto& channel : state.color_mixer_channels) {
        channel.hue = 100.0;
        channel.saturation = 100.0;
        channel.luminance = 100.0;
      }
      const ColorPixel black{0.0F, 0.0F, 0.0F, alpha};
      ColorPixel output;
      if (!apply_one_pixel(black, state, &output) || output.red != 0.0F ||
          output.green != 0.0F || output.blue != 0.0F || output.alpha != alpha) {
        std::cerr << "exact black failed finite/identity/alpha contract: "
                  << profile.name << " alpha=" << alpha << '\n';
        return false;
      }
    }
  }

  for (const auto alpha : {0.0F, 0.37F, 1.0F}) {
    const auto& profile = profiles.front();
    for (const auto& source : {
             ColorPixel{-0.04F, 0.35F, 1.6F, alpha},
             ColorPixel{0.72F, -0.025F, 0.14F, alpha}}) {
      auto state = ColorAdjustmentState{};
      state.color_pipeline.working_profile.color_space = profile.space;
      state.color_pipeline.output_headroom = 8.0;
      state.color_pipeline.output_gamut_mapping_enabled = false;
      state.color_mixer_channels[0].hue = 45.0;
      const auto compiled = compile_look_stage_program(state);
      const auto output = apply_compiled_look_stage_program(compiled, source);
      const auto input_has_hdr =
          std::max({source.red, source.green, source.blue}) > 1.0F;
      const auto input_has_negative =
          std::min({source.red, source.green, source.blue}) < 0.0F;
      const auto output_has_hdr =
          std::max({output.red, output.green, output.blue}) > 1.0F;
      const auto output_has_negative =
          std::min({output.red, output.green, output.blue}) < 0.0F;
      if (output.alpha != alpha || !std::isfinite(output.red) ||
          !std::isfinite(output.green) || !std::isfinite(output.blue) ||
          (input_has_hdr && !output_has_hdr) ||
          (input_has_negative && !output_has_negative)) {
        std::cerr << "signed/HDR mixer input lost scene range or alpha: alpha="
                  << alpha << " input=" << source.red << ',' << source.green << ','
                  << source.blue << " output=" << output.red << ',' << output.green
                  << ',' << output.blue << '\n';
        return false;
      }
    }
  }

  for (const auto method : {std::string_view("Bradford"), std::string_view("CAT02")}) {
    for (const auto& profile : profiles) {
      for (const auto value : {1.0e-5F, 1.0e-4F, 0.001F, 0.18F}) {
        for (int direction = 0; direction < 6; ++direction) {
          ColorPixel source_neutral{value, value, value, 0.37F};
          auto* component = direction % 3 == 0 ? &source_neutral.red
              : direction % 3 == 1 ? &source_neutral.green
                                   : &source_neutral.blue;
          *component += value * (direction < 3 ? 0.005F : -0.005F);
          const auto source_xyz = multiply(
              profiles.front().rgb_to_xyz,
              Vector{static_cast<double>(source_neutral.red),
                     static_cast<double>(source_neutral.green),
                     static_cast<double>(source_neutral.blue)});
          const auto working = working_pixel_from_d65_xyz(source_xyz, profile, method);
          const ColorPixel neutral{
              static_cast<float>(working[0]),
              static_cast<float>(working[1]),
              static_cast<float>(working[2]),
              source_neutral.alpha};
          auto state = ColorAdjustmentState{};
          state.color_pipeline.working_profile.color_space = profile.space;
          state.input_transform.chromatic_adaptation_method = std::string(method);
          for (auto& channel : state.color_mixer_channels) {
            channel.hue = 100.0;
            channel.saturation = 100.0;
            channel.luminance = 100.0;
          }
          ColorPixel output;
          if (!apply_one_pixel(neutral, state, &output) ||
              std::max({std::abs(output.red - neutral.red),
                        std::abs(output.green - neutral.green),
                        std::abs(output.blue - neutral.blue)}) > 3.0e-6F) {
            std::cerr << "near-neutral perturbation escaped selector protection: "
                      << profile.name << " method=" << method
                      << " peak=" << value << '\n';
            return false;
          }
        }
      }
    }
  }
  return true;
}

#if defined(PHOTO_EDITOR_COLOR_MATRIX_TEST_HAS_LCMS2)

[[nodiscard]] std::vector<std::byte> save_profile(cmsHPROFILE profile) {
  cmsUInt32Number bytes = 0;
  if (profile == nullptr || !cmsSaveProfileToMem(profile, nullptr, &bytes) || bytes == 0U) {
    return {};
  }
  std::vector<std::byte> payload(bytes);
  if (!cmsSaveProfileToMem(profile, payload.data(), &bytes) || bytes != payload.size()) {
    return {};
  }
  return payload;
}

[[nodiscard]] cmsHPROFILE make_custom_p3_profile(double gamma) {
  const cmsCIExyY white{.x = 0.31271, .y = 0.32902, .Y = 1.0};
  const cmsCIExyYTRIPLE primaries{
      .Red = {.x = 0.680, .y = 0.320, .Y = 1.0},
      .Green = {.x = 0.265, .y = 0.690, .Y = 1.0},
      .Blue = {.x = 0.150, .y = 0.060, .Y = 1.0}};
  std::array<cmsToneCurve*, 3> curves{};
  for (auto& curve : curves) {
    curve = cmsBuildGamma(nullptr, gamma);
  }
  if (std::any_of(curves.begin(), curves.end(), [](cmsToneCurve* curve) {
        return curve == nullptr;
      })) {
    for (auto* curve : curves) {
      if (curve != nullptr) cmsFreeToneCurve(curve);
    }
    return nullptr;
  }
  const auto profile = cmsCreateRGBProfile(&white, &primaries, curves.data());
  for (auto* curve : curves) {
    cmsFreeToneCurve(curve);
  }
  return profile;
}

[[nodiscard]] bool verify_custom_icc_against_lcms() {
  const auto custom_profile = make_custom_p3_profile(2.2);
  auto custom_payload = save_profile(custom_profile);
  if (custom_profile != nullptr) {
    cmsCloseProfile(custom_profile);
  }
  const auto changed_profile = make_custom_p3_profile(2.4);
  const auto changed_payload = save_profile(changed_profile);
  if (changed_profile != nullptr) {
    cmsCloseProfile(changed_profile);
  }
  const auto destination_payload = builtin_rgb_icc_profile_payload(make_srgb_color_space());
  if (custom_payload.empty() || changed_payload.empty() || destination_payload.empty()) {
    std::cerr << "could not build independent custom/display ICC profiles\n";
    return false;
  }

  const IccTransformOptions options{
      .rendering_intent = IccRenderingIntent::RelativeColorimetric,
      .black_point_compensation = false,
      .pixel_format = IccPixelFormat::RgbaFloat32,
      .target = "profile-edit-matrix-custom-p3-to-srgb"};
  const auto engine = compile_icc_transform(custom_payload, destination_payload, options);
  const auto changed_engine = compile_icc_transform(
      changed_payload, destination_payload, options);
  const auto source = cmsOpenProfileFromMem(custom_payload.data(),
      static_cast<cmsUInt32Number>(custom_payload.size()));
  const auto destination = cmsOpenProfileFromMem(destination_payload.data(),
      static_cast<cmsUInt32Number>(destination_payload.size()));
  const auto direct = source != nullptr && destination != nullptr
      ? cmsCreateTransform(source, TYPE_RGBA_FLT, destination, TYPE_RGBA_FLT,
                           INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA)
      : nullptr;
  if (!engine.ready() || !changed_engine.ready() ||
      engine.description().source_profile_sha256 ==
          changed_engine.description().source_profile_sha256 ||
      engine.description().cache_key == changed_engine.description().cache_key ||
      source == nullptr || destination == nullptr || direct == nullptr ||
      engine.description().source_profile_sha256 != color_payload_sha256(custom_payload) ||
      engine.description().destination_profile_sha256 != color_payload_sha256(destination_payload)) {
    if (direct != nullptr) cmsDeleteTransform(direct);
    if (source != nullptr) cmsCloseProfile(source);
    if (destination != nullptr) cmsCloseProfile(destination);
    std::cerr << "custom ICC profile failed to compile, track changed payload identity, or preserve destination identity\n";
    return false;
  }

  constexpr std::array<IccRgbaFloat, 6> source_pixels{{
      {.red = 0.18F, .green = 0.21F, .blue = 0.76F, .alpha = 0.0F},
      {.red = 0.61F, .green = 0.31F, .blue = 0.12F, .alpha = 0.25F},
      {.red = 0.08F, .green = 0.75F, .blue = 0.42F, .alpha = 0.37F},
      {.red = 0.47F, .green = 0.12F, .blue = 0.63F, .alpha = 0.50F},
      {.red = 0.82F, .green = 0.71F, .blue = 0.18F, .alpha = 0.75F},
      {.red = 0.26F, .green = 0.54F, .blue = 0.89F, .alpha = 1.0F},
  }};
  auto engine_pixels = source_pixels;
  auto reference_pixels = source_pixels;
  cmsDoTransform(direct, reference_pixels.data(), reference_pixels.data(),
                 static_cast<cmsUInt32Number>(reference_pixels.size()));
  if (!engine.apply(engine_pixels)) {
    cmsDeleteTransform(direct);
    cmsCloseProfile(source);
    cmsCloseProfile(destination);
    std::cerr << "engine rejected a valid payload-backed custom ICC transform\n";
    return false;
  }
  auto maximum_error = 0.0F;
  for (std::size_t index = 0; index < engine_pixels.size(); ++index) {
    maximum_error = std::max(maximum_error, std::abs(engine_pixels[index].red - reference_pixels[index].red));
    maximum_error = std::max(maximum_error, std::abs(engine_pixels[index].green - reference_pixels[index].green));
    maximum_error = std::max(maximum_error, std::abs(engine_pixels[index].blue - reference_pixels[index].blue));
    if (engine_pixels[index].alpha != source_pixels[index].alpha ||
        reference_pixels[index].alpha != source_pixels[index].alpha) {
      cmsDeleteTransform(direct);
      cmsCloseProfile(source);
      cmsCloseProfile(destination);
      std::cerr << "custom ICC transform changed alpha at sample " << index << '\n';
      return false;
    }
  }
  cmsDeleteTransform(direct);
  cmsCloseProfile(source);
  cmsCloseProfile(destination);
  if (maximum_error > 4.0e-4F) {
    std::cerr << "custom ICC output differs from independent Little CMS oracle maxError="
              << maximum_error << '\n';
    return false;
  }

  const std::array<std::byte, 7> malformed{
      std::byte{'n'}, std::byte{'o'}, std::byte{'t'}, std::byte{'-'},
      std::byte{'i'}, std::byte{'c'}, std::byte{'c'}};
  const auto rejected = compile_icc_transform(malformed, destination_payload, options);
  const auto missing = compile_icc_transform({}, destination_payload, options);
  if (rejected.ready() || missing.ready() ||
      rejected.description().status != IccTransformStatus::InvalidSourceProfile ||
      missing.description().status != IccTransformStatus::InvalidSourceProfile) {
    std::cerr << "invalid or missing custom ICC payload did not fail closed\n";
    return false;
  }
  return true;
}

#else

[[nodiscard]] bool verify_custom_icc_against_lcms() {
  const auto capability = icc_backend_capability();
  if (capability.available || capability.backend != "none" ||
      capability.failure_reason != "little_cms_not_compiled") {
    std::cerr << "no-LCMS build did not report its stable unavailable capability\n";
    return false;
  }
  const std::array<std::byte, 3> malformed{
      std::byte{'b'}, std::byte{'a'}, std::byte{'d'}};
  const auto transform = compile_icc_transform(malformed, malformed);
  if (transform.ready() || transform.description().status != IccTransformStatus::BackendUnavailable ||
      transform.description().failure_reason != "little_cms_not_compiled") {
    std::cerr << "no-LCMS build silently accepted or misreported an ICC transform\n";
    return false;
  }
  return true;
}

#endif

}  // namespace

int main() {
  if (!verify_working_profile_name_roundtrips() ||
      !verify_published_matrix_vectors() ||
      !verify_profile_edit_matrix() ||
      !verify_selector_boundary_continuity() ||
      !verify_alpha_signed_hdr_and_faint_color() ||
      !verify_custom_icc_against_lcms()) {
    return EXIT_FAILURE;
  }
  std::cout << "Color profile edit matrix: published matrix/CAT vectors, seven working profiles, "
               "two adaptations, eight selector regions, faint color, alpha, signed/HDR, and ICC fail-closed checks passed\n";
  return EXIT_SUCCESS;
}

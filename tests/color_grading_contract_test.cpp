#include "photo_editor/pipeline/color_adjustments.h"

#include "../engine/src/pipeline/color_look_compilation_runtime.h"
#include "../engine/src/pipeline/color_mixer_runtime.h"
#include "../engine/src/pipeline/color_working_space_runtime.h"
#include "../engine/src/pipeline/color_look_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace photo_editor::pipeline;

constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kTintScale = 0.18;

bool check(bool condition, std::string_view message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

double angular_distance(double left, double right) {
  auto distance = std::abs(std::remainder(left - right, kTwoPi));
  return std::min(distance, kTwoPi - distance);
}

double hue_to_rgb(double p, double q, double t) {
  if (t < 0.0) t += 1.0;
  if (t > 1.0) t -= 1.0;
  if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
  if (t < 0.5) return q;
  if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
  return p;
}

std::array<double, 3> display_srgb_from_hsl_hue(double hue) {
  hue -= std::floor(hue);
  constexpr auto saturation = 1.0;
  constexpr auto lightness = 0.5;
  const auto q = lightness < 0.5
                     ? lightness * (1.0 + saturation)
                     : lightness + saturation - lightness * saturation;
  const auto p = 2.0 * lightness - q;
  return {hue_to_rgb(p, q, hue + 1.0 / 3.0),
          hue_to_rgb(p, q, hue),
          hue_to_rgb(p, q, hue - 1.0 / 3.0)};
}

double srgb_decode(double value) {
  return value <= 0.04045 ? value / 12.92
                          : std::pow((value + 0.055) / 1.055, 2.4);
}

using Matrix3 = std::array<double, 9>;

Matrix3 inverse(Matrix3 m) {
  const auto determinant =
      m[0] * (m[4] * m[8] - m[5] * m[7]) -
      m[1] * (m[3] * m[8] - m[5] * m[6]) +
      m[2] * (m[3] * m[7] - m[4] * m[6]);
  return {(m[4] * m[8] - m[5] * m[7]) / determinant,
          (m[2] * m[7] - m[1] * m[8]) / determinant,
          (m[1] * m[5] - m[2] * m[4]) / determinant,
          (m[5] * m[6] - m[3] * m[8]) / determinant,
          (m[0] * m[8] - m[2] * m[6]) / determinant,
          (m[2] * m[3] - m[0] * m[5]) / determinant,
          (m[3] * m[7] - m[4] * m[6]) / determinant,
          (m[1] * m[6] - m[0] * m[7]) / determinant,
          (m[0] * m[4] - m[1] * m[3]) / determinant};
}

std::array<double, 3> multiply(const Matrix3& matrix,
                               const std::array<double, 3>& vector) {
  return {matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2],
          matrix[3] * vector[0] + matrix[4] * vector[1] + matrix[5] * vector[2],
          matrix[6] * vector[0] + matrix[7] * vector[1] + matrix[8] * vector[2]};
}

Matrix3 reference_srgb_to_xyz_d65() {
  const auto xyz_from_xy = [](double x, double y) {
    return std::array<double, 3>{x / y, 1.0, (1.0 - x - y) / y};
  };
  const auto red = xyz_from_xy(0.640, 0.330);
  const auto green = xyz_from_xy(0.300, 0.600);
  const auto blue = xyz_from_xy(0.150, 0.060);
  const auto white = xyz_from_xy(0.31271, 0.32902);
  const Matrix3 primary_columns{
      red[0], green[0], blue[0],
      red[1], green[1], blue[1],
      red[2], green[2], blue[2]};
  const auto scales = multiply(inverse(primary_columns), white);
  return {red[0] * scales[0], green[0] * scales[1], blue[0] * scales[2],
          red[1] * scales[0], green[1] * scales[1], blue[1] * scales[2],
          red[2] * scales[0], green[2] * scales[1], blue[2] * scales[2]};
}

// Independent reference: Qt's HSL display hue -> encoded sRGB -> linear sRGB -> OKLab.
double reference_oklab_hue_for_display_hue(double display_hue) {
  const auto encoded = display_srgb_from_hsl_hue(display_hue);
  const auto red = srgb_decode(encoded[0]);
  const auto green = srgb_decode(encoded[1]);
  const auto blue = srgb_decode(encoded[2]);
  const auto xyz = multiply(reference_srgb_to_xyz_d65(), {red, green, blue});
  const auto l = std::cbrt(0.8189330101 * xyz[0] + 0.3618667424 * xyz[1] -
                           0.1288597137 * xyz[2]);
  const auto m = std::cbrt(0.0329845436 * xyz[0] + 0.9293118715 * xyz[1] +
                           0.0361456387 * xyz[2]);
  const auto s = std::cbrt(0.0482003018 * xyz[0] + 0.2643662691 * xyz[1] +
                           0.6338517070 * xyz[2]);
  const auto a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
  const auto b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
  auto angle = std::atan2(b, a);
  return angle < 0.0 ? angle + kTwoPi : angle;
}

std::array<double, 3> reference_oklab_from_linear_srgb(const ColorPixel& pixel) {
  const auto xyz = multiply(reference_srgb_to_xyz_d65(),
                            {pixel.red, pixel.green, pixel.blue});
  const auto l = std::cbrt(0.8189330101 * xyz[0] + 0.3618667424 * xyz[1] -
                           0.1288597137 * xyz[2]);
  const auto m = std::cbrt(0.0329845436 * xyz[0] + 0.9293118715 * xyz[1] +
                           0.0361456387 * xyz[2]);
  const auto s = std::cbrt(0.0482003018 * xyz[0] + 0.2643662691 * xyz[1] +
                           0.6338517070 * xyz[2]);
  return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
          1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
          0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

double clamp_signed_percent_reference(double value) {
  return std::clamp(value, -100.0, 100.0);
}

double smoothstep_reference(double edge0, double edge1, double value) {
  const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

struct LegacyGradingReference final {
  double lightness;
  double a;
  double b;
};

// Independent transcription of the frozen renderer contract from the
// pre-alignment baseline: hue is a raw normalized turn, and saturation sign
// controls magnitude only. This intentionally does not call production math.
LegacyGradingReference apply_frozen_legacy_grading(
    const ColorAdjustmentState& state, double skin_scale,
    double input_lightness, double input_a, double input_b) {
  const auto balance_shift =
      clamp_signed_percent_reference(state.grading_balance) / 100.0 * 0.18;
  const auto selection_lightness = std::clamp(input_lightness, 0.0, 1.0);
  const auto shadow = 1.0 - smoothstep_reference(
      0.15 - balance_shift, 0.45 - balance_shift, selection_lightness);
  const auto highlight = smoothstep_reference(
      0.55 - balance_shift, 0.85 - balance_shift, selection_lightness);
  const std::array<double, 3> weights{
      shadow, std::max(0.0, 1.0 - shadow - highlight), highlight};
  const std::array<double, 3> hues{
      state.grading_shadows_hue, state.grading_midtones_hue,
      state.grading_highlights_hue};
  const std::array<double, 3> saturations{
      state.grading_shadows_saturation, state.grading_midtones_saturation,
      state.grading_highlights_saturation};
  const std::array<double, 3> luminances{
      state.grading_shadows_luminance, state.grading_midtones_luminance,
      state.grading_highlights_luminance};
  const auto amount = std::clamp(state.grading_amount / 100.0, 0.0, 1.0);
  auto result = LegacyGradingReference{input_lightness, input_a, input_b};
  for (std::size_t band = 0; band < weights.size(); ++band) {
    const auto scaled_weight = weights[band] * amount;
    const auto intensity = std::abs(
        clamp_signed_percent_reference(saturations[band]) / 100.0) * 0.18 *
        scaled_weight * skin_scale;
    const auto hue = (hues[band] - std::floor(hues[band])) * kTwoPi;
    result.a += intensity * std::cos(hue);
    result.b += intensity * std::sin(hue);
    result.lightness +=
        clamp_signed_percent_reference(luminances[band]) / 100.0 * 0.20 *
        scaled_weight * skin_scale;
  }
  return result;
}

ColorAdjustmentState grading_state() {
  ColorAdjustmentState state;
  state.grading_amount = 100.0;
  state.color_pipeline.working_profile = working_color_profile_from_name("Linear sRGB");
  return state;
}

CompiledLookStageProgram grading_program(const ColorAdjustmentState& state) {
  auto program = CompiledLookStageProgram{};
  compile_look_grading_program(&program, state);
  return program;
}

bool check_rgb(double expected_a, double expected_b, double actual_a,
              double actual_b, std::string_view message,
              double tolerance = 2.0e-6) {
  return check(std::abs(expected_a - actual_a) <= tolerance &&
                   std::abs(expected_b - actual_b) <= tolerance,
               message);
}

bool test_display_hue_contract() {
  bool ok = true;
  for (const auto hue : {0.0, 1.0 / 12.0, 0.15, 1.0 / 3.0, 0.5,
                         2.0 / 3.0, 0.83, 1.0 - 1.0e-9, 1.0}) {
    const auto expected = reference_oklab_hue_for_display_hue(hue);
    const auto actual = oklab_hue_for_rgb_hue(hue);
    if (angular_distance(expected, actual) >= 2.0e-6) {
      std::cerr << "display hue mismatch at " << hue << ": expected " << expected
                << " radians, actual " << actual << " radians\n";
      ok &= check(false,
                  "display hue did not map to its independent sRGB/OKLab reference");
    }
  }
  return ok;
}

bool test_signed_grading_formula() {
  bool ok = true;
  const auto display_hue = 0.0;  // Red in the wheel; deliberately not OKLab angle zero.
  const auto expected_angle = reference_oklab_hue_for_display_hue(display_hue);

  auto positive = grading_state();
  positive.grading_midtones_hue = display_hue;
  positive.grading_midtones_saturation = 100.0;
  auto positive_program = grading_program(positive);
  auto lightness = 0.5F;
  auto a = 0.0F;
  auto b = 0.0F;
  apply_compiled_look_grading(positive_program, 1.0F, &lightness, &a, &b);
  ok &= check_rgb(kTintScale * std::cos(expected_angle),
                  kTintScale * std::sin(expected_angle), a, b,
                  "positive grading did not tint toward the selected display hue");

  auto negative = grading_state();
  negative.grading_midtones_hue = display_hue;
  negative.grading_midtones_saturation = -100.0;
  auto negative_program = grading_program(negative);
  lightness = 0.5F;
  a = 0.12F;
  b = -0.07F;
  apply_compiled_look_grading(negative_program, 1.0F, &lightness, &a, &b);
  ok &= check_rgb(0.0, 0.0, a, b,
                  "negative grading did not fully desaturate its selected tonal band");

  auto identity = grading_state();
  auto identity_program = grading_program(identity);
  lightness = 0.37F;
  a = 0.10F;
  b = -0.04F;
  apply_compiled_look_grading(identity_program, 1.0F, &lightness, &a, &b);
  ok &= check_rgb(0.10, -0.04, a, b,
                  "zero grading saturation changed source chroma");

  auto partial_negative = grading_state();
  partial_negative.grading_midtones_saturation = -100.0;
  partial_negative.grading_amount = 25.0;
  auto partial_negative_program = grading_program(partial_negative);
  lightness = 0.5F;
  a = 0.10F;
  b = -0.04F;
  apply_compiled_look_grading(partial_negative_program, 0.5F,
                              &lightness, &a, &b);
  ok &= check_rgb(0.0875, -0.035, a, b,
                  "negative grading did not scale with amount and skin protection");

  for (const auto tone : {0.0F, 0.12F, 0.30F, 0.5F, 0.70F, 0.90F, 1.0F}) {
    auto all_negative = grading_state();
    all_negative.grading_shadows_saturation = -100.0;
    all_negative.grading_midtones_saturation = -100.0;
    all_negative.grading_highlights_saturation = -100.0;
    auto program = grading_program(all_negative);
    lightness = tone;
    a = 0.08F;
    b = -0.04F;
    apply_compiled_look_grading(program, 1.0F, &lightness, &a, &b);
    ok &= check_rgb(0.0, 0.0, a, b,
                    "all-band negative grading left chroma at a tonal boundary");
  }

  auto limited = grading_state();
  limited.grading_midtones_hue = display_hue;
  limited.grading_midtones_saturation = 100.0;
  limited.grading_amount = 25.0;
  auto limited_program = grading_program(limited);
  lightness = 0.5F;
  a = 0.0F;
  b = 0.0F;
  apply_compiled_look_grading(limited_program, 0.5F, &lightness, &a, &b);
  ok &= check_rgb(kTintScale * 0.25 * 0.5 * std::cos(expected_angle),
                  kTintScale * 0.25 * 0.5 * std::sin(expected_angle), a, b,
                  "grading amount or skin scale was not applied multiplicatively");

  auto mixed = grading_state();
  mixed.grading_shadows_hue = display_hue;
  mixed.grading_shadows_saturation = 100.0;
  mixed.grading_midtones_saturation = -50.0;
  auto mixed_program = grading_program(mixed);
  lightness = 0.30F;  // Smooth shadow and midtone weights are exactly one half each.
  a = 0.10F;
  b = -0.04F;
  apply_compiled_look_grading(mixed_program, 1.0F, &lightness, &a, &b);
  ok &= check_rgb(0.75 * 0.10 + 0.09 * std::cos(expected_angle),
                  0.75 * -0.04 + 0.09 * std::sin(expected_angle), a, b,
                  "mixed positive and negative bands did not combine independently");

  auto protected_state = grading_state();
  protected_state.grading_midtones_hue = display_hue;
  protected_state.grading_midtones_saturation = 100.0;
  auto protected_program = grading_program(protected_state);
  lightness = 0.5F;
  a = 0.10F;
  b = 0.03F;
  apply_compiled_look_grading(protected_program, 0.0F, &lightness, &a, &b);
  ok &= check_rgb(0.10, 0.03, a, b,
                  "zero skin scale changed the source chroma");

  return ok;
}

bool test_frozen_legacy_grading_contract() {
  bool ok = true;
  const auto legacy_flag = CompiledLookStageProgram::kLegacyCreativeSemantics;
  const std::array models{
      PhotographicRenderingModel::LegacyOklchV1,
      PhotographicRenderingModel::Aces2HellwigJmhV1};
  const std::array hues{0.0, 0.11, 0.37, 0.73, 0.999};
  const std::array tones{0.10, 0.30, 0.50, 0.70, 0.90};
  const std::array balances{-100.0, 0.0, 65.0};
  const std::array skin_scales{0.0, 0.4, 1.0};

  for (const auto model : models) {
    for (std::size_t example = 0; example < hues.size(); ++example) {
      for (const auto sign : {-1.0, 1.0}) {
        auto state = grading_state();
        state.color_pipeline.photographic_rendering_model = model;
        state.grading_amount = example % 2U == 0U ? 37.0 : 100.0;
        state.grading_balance = balances[example % balances.size()];
        state.grading_shadows_hue = hues[example];
        state.grading_midtones_hue = hues[(example + 1U) % hues.size()];
        state.grading_highlights_hue = hues[(example + 2U) % hues.size()];
        state.grading_shadows_saturation = sign * 83.0;
        state.grading_midtones_saturation = sign * 47.0;
        state.grading_highlights_saturation = sign * 91.0;
        state.grading_shadows_luminance = -24.0;
        state.grading_midtones_luminance = 32.0;
        state.grading_highlights_luminance = 12.0;

        const auto program = grading_program(state);
        ok &= check((program.active_mask & legacy_flag) != 0U,
                    "active legacy renderer grading did not carry its frozen-semantics bit");
        ok &= check((program.active_mask &
                     CompiledLookStageProgram::kColorGradingAddsChroma) != 0U,
                    "legacy signed tint no longer reports its frozen chroma addition");
        for (std::size_t band = 0; band < hues.size() && band < 3U; ++band) {
          const auto raw_hue = hues[(example + band) % hues.size()];
          const auto wrapped = raw_hue - std::floor(raw_hue);
          ok &= check(std::abs(program.grading_hue[band] - wrapped) < 2.0e-7,
                      "legacy grading hue was remapped away from its frozen raw-turn value");
        }

        const auto input_lightness = tones[example];
        constexpr auto input_a = 0.091;
        constexpr auto input_b = -0.063;
        for (const auto skin_scale : skin_scales) {
          const auto expected = apply_frozen_legacy_grading(
              state, skin_scale, input_lightness, input_a, input_b);
          auto actual_lightness = static_cast<float>(input_lightness);
          auto actual_a = static_cast<float>(input_a);
          auto actual_b = static_cast<float>(input_b);
          apply_compiled_look_grading(program, static_cast<float>(skin_scale),
                                      &actual_lightness, &actual_a, &actual_b);
          ok &= check(std::abs(actual_lightness - expected.lightness) < 3.0e-6 &&
                          std::abs(actual_a - expected.a) < 3.0e-6 &&
                          std::abs(actual_b - expected.b) < 3.0e-6,
                      "legacy grading output diverged from the frozen renderer reference");
        }
      }
    }

    auto inactive = grading_state();
    inactive.color_pipeline.photographic_rendering_model = model;
    inactive.grading_amount = 0.0;
    inactive.grading_shadows_saturation = 100.0;
    const auto inactive_program = grading_program(inactive);
    ok &= check((inactive_program.active_mask & legacy_flag) == 0U,
                "inactive legacy grading retained the frozen-semantics bit");
  }

  auto modern = grading_state();
  modern.grading_midtones_hue = 0.0;
  modern.grading_midtones_saturation = 100.0;
  const auto modern_program = grading_program(modern);
  ok &= check((modern_program.active_mask & legacy_flag) == 0U,
              "modern ACES 2 grading was incorrectly marked as legacy");
  ok &= check((modern_program.active_mask &
               CompiledLookStageProgram::kColorGradingAddsChroma) != 0U,
              "positive modern grading no longer reports chroma addition");

  auto modern_negative = modern;
  modern_negative.grading_midtones_saturation = -100.0;
  auto modern_positive = modern;
  const auto modern_negative_program = grading_program(modern_negative);
  const auto modern_positive_program = grading_program(modern_positive);
  ok &= check((modern_negative_program.active_mask &
               CompiledLookStageProgram::kColorGrading) != 0U &&
                  (modern_negative_program.active_mask &
                   CompiledLookStageProgram::kColorGradingAddsChroma) == 0U,
              "negative modern grading was not active without claiming positive tint");
  auto negative_lightness = 0.5F;
  auto negative_a = 0.12F;
  auto negative_b = -0.07F;
  auto positive_lightness = 0.5F;
  auto positive_a = 0.12F;
  auto positive_b = -0.07F;
  apply_compiled_look_grading(modern_negative_program, 1.0F,
                              &negative_lightness, &negative_a, &negative_b);
  apply_compiled_look_grading(modern_positive_program, 1.0F,
                              &positive_lightness, &positive_a, &positive_b);
  ok &= check(std::hypot(negative_a, negative_b) < 1.0e-6 &&
                  std::hypot(positive_a - 0.12F, positive_b + 0.07F) > 0.01F,
              "modern signed grading semantics changed while preserving legacy output");
  return ok;
}

bool test_reference_runtime_grading_contract() {
  bool ok = true;
  constexpr auto alpha = 0.63F;
  const auto gray = ColorPixel{0.125F, 0.125F, 0.125F, alpha};
  auto modern = grading_state();
  modern.grading_midtones_hue = 0.0;
  modern.grading_midtones_saturation = 100.0;
  const auto modern_context = make_pipeline_color_context(modern);
  const auto modern_output = apply_post_tone_color_adjustments(
      gray, modern_context, modern, 0.0, 0.0, 0.0, 1.0);
  const auto original_lab = reference_oklab_from_linear_srgb(gray);
  const auto modern_lab = reference_oklab_from_linear_srgb(modern_output);
  const auto red_angle = reference_oklab_hue_for_display_hue(0.0);
  ok &= check(std::abs(modern_lab[0] - original_lab[0]) < 4.0e-5 &&
                  std::abs(modern_lab[1] - (original_lab[1] +
                                            kTintScale * std::cos(red_angle))) < 4.0e-5 &&
                  std::abs(modern_lab[2] - (original_lab[2] +
                                            kTintScale * std::sin(red_angle))) < 4.0e-5 &&
                  modern_output.alpha == alpha,
              "reference renderer did not apply the canonical displayed hue as a signed tint");

  const auto chromatic = ColorPixel{0.25F, 0.10F, 0.04F, alpha};
  const auto chromatic_lab = reference_oklab_from_linear_srgb(chromatic);
  std::array<std::array<double, 3>, 2> legacy_outputs{};
  auto legacy_index = std::size_t{0};
  for (const auto sign : {-1.0, 1.0}) {
    auto legacy = grading_state();
    legacy.color_pipeline.photographic_rendering_model =
        PhotographicRenderingModel::LegacyOklchV1;
    legacy.grading_midtones_hue = 0.0;
    legacy.grading_midtones_saturation = sign * 100.0;
    const auto context = make_pipeline_color_context(legacy);
    const auto output = apply_post_tone_color_adjustments(
        chromatic, context, legacy, 0.0, 0.0, 0.0, 1.0);
    const auto actual_lab = reference_oklab_from_linear_srgb(output);
    const auto expected = apply_frozen_legacy_grading(
        legacy, 1.0, chromatic_lab[0], chromatic_lab[1], chromatic_lab[2]);
    ok &= check(std::abs(actual_lab[0] - expected.lightness) < 5.0e-5 &&
                    std::abs(actual_lab[1] - expected.a) < 5.0e-5 &&
                    std::abs(actual_lab[2] - expected.b) < 5.0e-5 &&
                    output.alpha == alpha,
                "reference renderer changed the frozen legacy grading appearance");
    legacy_outputs[legacy_index++] = actual_lab;
  }
  ok &= check(std::abs(legacy_outputs[0][1] - legacy_outputs[1][1]) < 3.0e-6 &&
                  std::abs(legacy_outputs[0][2] - legacy_outputs[1][2]) < 3.0e-6,
              "legacy positive and negative saturation no longer match their frozen same-direction behavior");
  return ok;
}

bool test_modern_hue_only_full_look_identity() {
  bool ok = true;
  constexpr auto alpha = 0.37F;
  const std::array<ColorPixel, 3> samples{{
      {4.0F, -0.25F, 1.25F, alpha},
      {-0.5F, 2.75F, 0.125F, 0.0F},
      {3.0F, 5.0F, -2.0F, 1.0F},
  }};
  const auto check_identity = [&](const ColorAdjustmentState& state,
                                  std::string_view case_name) {
    const auto compiled = compile_look_stage_program(state);
    ok &= check(compiled.active_mask == 0U && compiled.cache_identity == 0U &&
                    !compiled.ready(),
                std::string(case_name) +
                    ": hue-only or zero-amount grading activated the full look program");
    const auto context = make_pipeline_color_context(state);
    for (const auto& input : samples) {
      const auto compiled_output =
          apply_compiled_look_stage_program(compiled, input);
      const auto reference_output = apply_post_tone_color_adjustments(
          input, context, state, 0.0, 0.0, 0.0, 1.0);
      const auto exact_input = [&](const ColorPixel& output) {
        return output.red == input.red && output.green == input.green &&
            output.blue == input.blue && output.alpha == input.alpha;
      };
      ok &= check(exact_input(compiled_output),
                  std::string(case_name) +
                      ": inactive compiled look changed signed HDR RGB or alpha");
      ok &= check(exact_input(reference_output),
                  std::string(case_name) +
                      ": reference renderer changed a hue-only or zero-amount recipe");
    }
  };

  auto hue_only = grading_state();
  hue_only.grading_shadows_hue = 0.137;
  hue_only.grading_midtones_hue = 0.463;
  hue_only.grading_highlights_hue = 0.827;
  check_identity(hue_only, "modern hue-only grading");

  auto amount_zero = grading_state();
  amount_zero.grading_amount = 0.0;
  amount_zero.grading_shadows_hue = 0.137;
  amount_zero.grading_midtones_hue = 0.463;
  amount_zero.grading_highlights_hue = 0.827;
  amount_zero.grading_shadows_saturation = 73.0;
  amount_zero.grading_midtones_saturation = -52.0;
  amount_zero.grading_highlights_saturation = 91.0;
  amount_zero.grading_shadows_luminance = -20.0;
  amount_zero.grading_midtones_luminance = 17.0;
  amount_zero.grading_highlights_luminance = 30.0;
  check_identity(amount_zero, "modern zero-amount grading");
  return ok;
}

bool test_faint_color_hue_edit_contract() {
  auto modern = grading_state();
  modern.color_mixer_channels[0].hue = 100.0;
  const auto context = make_pipeline_color_context(modern);
  constexpr auto alpha = 0.4F;
  const auto faint = ColorPixel{1.2e-8F, 5.0e-9F, 2.0e-9F, alpha};
  const auto modern_output = apply_post_tone_color_adjustments(
      faint, context, modern, 0.0, 0.0, 0.0, 1.0);
  const auto changed = std::max({std::abs(modern_output.red - faint.red),
                                 std::abs(modern_output.green - faint.green),
                                 std::abs(modern_output.blue - faint.blue)});
  if (!check(changed > 1.0e-10F && modern_output.alpha == alpha,
             "modern reference runtime suppressed a genuine 1.2e-8-scale hue edit")) {
    const auto selection_rgb = pixel_to_linear_srgb_reference(faint, context);
    std::cerr << "faint selection RGB: " << selection_rgb.red << ", "
              << selection_rgb.green << ", " << selection_rgb.blue
              << "; confidence=" << color_selection_confidence(selection_rgb)
              << "; hue=" << rgb_hue_for_mixer_selection(selection_rgb)
              << "; red weight=" << color_mixer_channel_weight(
                     rgb_hue_for_mixer_selection(selection_rgb), 0U)
              << '\n';
    std::cerr << "faint edit max channel change: " << changed << '\n';
    return false;
  }

  auto legacy = modern;
  legacy.color_pipeline.photographic_rendering_model =
      PhotographicRenderingModel::LegacyOklchV1;
  const auto legacy_context = make_pipeline_color_context(legacy);
  const auto legacy_output = apply_post_tone_color_adjustments(
      faint, legacy_context, legacy, 0.0, 0.0, 0.0, 1.0);
  return check(legacy_output.red == faint.red &&
                   legacy_output.green == faint.green &&
                   legacy_output.blue == faint.blue &&
                   legacy_output.alpha == alpha,
               "legacy reference fast path no longer preserves frozen faint-color behavior");
}

bool test_global_and_band_desaturation_endpoint() {
  bool ok = true;
  auto state = grading_state();
  state.saturation = -100.0;
  state.grading_shadows_saturation = -100.0;
  state.grading_midtones_saturation = -100.0;
  state.grading_highlights_saturation = -100.0;
  state.grading_shadows_hue = 0.0;
  state.grading_midtones_hue = 1.0 / 3.0;
  state.grading_highlights_hue = 2.0 / 3.0;
  constexpr auto alpha = 0.37F;
  const auto source = ColorPixel{0.64F, 0.21F, 0.07F, alpha};

  const auto compiled = compile_look_stage_program(state);
  ok &= check((compiled.active_mask & CompiledLookStageProgram::kColorGrading) != 0U &&
                  (compiled.active_mask &
                   CompiledLookStageProgram::kColorGradingAddsChroma) == 0U,
              "negative grading blocked the modern full-desaturation endpoint");
  const auto output = apply_compiled_look_stage_program(compiled, source);
  ok &= check(std::abs(output.red - output.green) < 2.0e-6F &&
                  std::abs(output.green - output.blue) < 2.0e-6F &&
                  output.alpha == alpha,
              "combined global and band -100 controls did not produce neutral output");

  const auto context = make_pipeline_color_context(state);
  const auto reference_output = apply_post_tone_color_adjustments(
      source, context, state, -1.0, 0.0, 0.0, 1.0);
  ok &= check(std::abs(reference_output.red - reference_output.green) < 2.0e-6F &&
                  std::abs(reference_output.green - reference_output.blue) < 2.0e-6F &&
                  reference_output.alpha == alpha,
              "reference runtime did not preserve the combined neutral endpoint");
  return ok;
}

bool test_profile_and_alpha_invariance() {
  bool ok = true;
  const auto source = ColorPixel{0.24F, 0.11F, 0.055F, 0.37F};
  const auto reference_state = grading_state();
  const auto reference_context = make_pipeline_color_context(reference_state);
  auto reference_config = reference_state;
  reference_config.grading_highlights_hue = 0.0;
  reference_config.grading_highlights_saturation = 75.0;
  const auto reference_program = compile_look_stage_program(reference_config);
  const auto reference_output = apply_compiled_look_stage_program(reference_program, source);

  const auto check_profile = [&](const ColorAdjustmentState& state,
                                 std::string_view profile_name) {
    const auto context = make_pipeline_color_context(state);
    const auto input = apply_rgb_matrix(
        apply_rgb_matrix(source, reference_context.rgb_to_xyz_d65),
        context.xyz_d65_to_rgb);
    const auto output = apply_compiled_look_stage_program(
        compile_look_stage_program(state), input);
    const auto returned = apply_rgb_matrix(
        apply_rgb_matrix(output, context.rgb_to_xyz_d65),
        reference_context.xyz_d65_to_rgb);
    ok &= check(std::abs(returned.red - reference_output.red) < 4.0e-5F &&
                    std::abs(returned.green - reference_output.green) < 4.0e-5F &&
                    std::abs(returned.blue - reference_output.blue) < 4.0e-5F,
                std::string("grading appearance changed in ") + std::string(profile_name));
    ok &= check(output.alpha == input.alpha && input.alpha == source.alpha,
                std::string("grading changed alpha in ") + std::string(profile_name));
  };

  for (const auto* profile : {"Linear sRGB", "Linear Display P3",
                              "Linear ProPhoto RGB", "Linear Rec.2020",
                              "ACEScg", "ACES2065-1"}) {
    auto state = reference_config;
    state.color_pipeline.working_profile = working_color_profile_from_name(profile);
    check_profile(state, profile);
  }

  // The working-profile registry has no named Adobe RGB entry, but the
  // generic RGB working-space contract accepts a linearized Adobe RGB space.
  auto adobe_state = reference_config;
  auto adobe_profile = working_color_profile_from_name("Linear sRGB");
  adobe_profile.profile_name = "Custom Linear Adobe RGB";
  adobe_profile.color_space = make_adobe_rgb_color_space();
  adobe_profile.color_space.name = "Linear Adobe RGB";
  adobe_profile.color_space.transfer_function = TransferFunction::Linear;
  adobe_profile.color_space.scene_referred = true;
  adobe_state.color_pipeline.working_profile = adobe_profile;
  check_profile(adobe_state, "custom linear Adobe RGB");

  for (const auto alpha : {0.0F, 0.25F, 1.0F}) {
    auto state = reference_config;
    state.grading_highlights_saturation = -100.0;
    const auto result = apply_compiled_look_stage_program(
        compile_look_stage_program(state),
        ColorPixel{1.4F, 0.8F, 0.22F, alpha});
    ok &= check(result.alpha == alpha && std::isfinite(result.red) &&
                    std::isfinite(result.green) && std::isfinite(result.blue),
                "signed grading failed finite HDR or alpha preservation");
  }
  return ok;
}

}  // namespace

int main() {
  const auto hue_ok = test_display_hue_contract();
  const auto grading_ok = test_signed_grading_formula();
  const auto legacy_ok = test_frozen_legacy_grading_contract();
  const auto reference_ok = test_reference_runtime_grading_contract();
  const auto hue_only_identity_ok = test_modern_hue_only_full_look_identity();
  const auto faint_ok = test_faint_color_hue_edit_contract();
  const auto endpoint_ok = test_global_and_band_desaturation_endpoint();
  const auto profile_ok = test_profile_and_alpha_invariance();
  const auto ok = hue_ok && grading_ok && legacy_ok && reference_ok &&
      hue_only_identity_ok && faint_ok && endpoint_ok && profile_ok;
  if (!ok) return EXIT_FAILURE;
  std::cout << "Color grading contract: display hue, signed and legacy grading, faint color, profiles, HDR, and alpha passed\n";
  return EXIT_SUCCESS;
}

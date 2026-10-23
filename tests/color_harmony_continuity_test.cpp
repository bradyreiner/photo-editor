#include "color_look_compilation_runtime.h"
#include "color_look_harmony_math.h"
#include "color_look_runtime.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::CompiledLookStageProgram;
using photo_editor::pipeline::PhotographicRenderingModel;

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;

[[nodiscard]] float wrap_turn(float value) {
  return value - std::floor(value);
}

[[nodiscard]] float angular_distance(float left, float right) {
  return std::abs(std::remainder(left - right, kTwoPi));
}

[[nodiscard]] bool check(bool condition, std::string_view message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

[[nodiscard]] CompiledLookStageProgram compile_palette(
    std::string_view mode,
    double base_hue,
    double strength,
    std::string_view working_profile = "Linear ProPhoto RGB",
    PhotographicRenderingModel rendering_model =
        PhotographicRenderingModel::Aces2OcioV1) {
  ColorAdjustmentState state;
  state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name(working_profile);
  state.color_pipeline.photographic_rendering_model = rendering_model;
  state.color_harmony_enabled = true;
  state.color_harmony_mode = mode;
  state.color_harmony_base_hue = base_hue;
  state.color_harmony_strength = strength;
  CompiledLookStageProgram compiled;
  photo_editor::pipeline::compile_look_harmony_program(&compiled, state);
  return compiled;
}

[[nodiscard]] float output_hue(
    const CompiledLookStageProgram& compiled,
    float input_hue,
    float skin_scale = 1.0F,
    float chroma = 0.8F) {
  auto a = chroma * std::cos(input_hue);
  auto b = chroma * std::sin(input_hue);
  photo_editor::pipeline::apply_compiled_look_harmony(
      compiled, skin_scale, &a, &b);
  return std::atan2(b, a);
}

[[nodiscard]] float circular_force(
    float input_hue,
    const CompiledLookStageProgram& compiled) {
  return photo_editor_color_harmony_circular_force(
      input_hue, compiled.harmony_anchors.data(),
      compiled.harmony_anchor_count);
}

[[nodiscard]] bool test_canonical_anchor_mapping_and_profiles() {
  constexpr auto kModes = std::array<std::string_view, 5>{
      "Analogous", "Complementary", "Triadic", "Split Complementary",
      "Tetradic"};
  constexpr auto kProfiles = std::array<std::string_view, 7>{
      "Linear sRGB", "Linear Display P3", "Linear ProPhoto RGB",
      "Linear Rec.2020", "Linear Adobe RGB", "ACEScg", "ACES2065-1"};
  constexpr auto kBase = 0.173;

  for (const auto mode : kModes) {
    const auto reference = compile_palette(mode, kBase, 100.0);
    for (const auto profile : kProfiles) {
      const auto candidate = compile_palette(mode, kBase, 100.0, profile);
      if (!check(candidate.harmony_anchor_count == reference.harmony_anchor_count,
                 "working profile changed the harmony anchor count")) {
        return false;
      }
      for (auto index = 0U; index < 4U; ++index) {
        if (!check(std::abs(candidate.harmony_anchors[index] -
                            reference.harmony_anchors[index]) < 1.0e-7F,
                   "working profile changed canonical OKLab harmony anchors")) {
          return false;
        }
      }
      auto ref_a = -0.31F;
      auto ref_b = 0.47F;
      auto candidate_a = ref_a;
      auto candidate_b = ref_b;
      photo_editor::pipeline::apply_compiled_look_harmony(
          reference, 0.73F, &ref_a, &ref_b);
      photo_editor::pipeline::apply_compiled_look_harmony(
          candidate, 0.73F, &candidate_a, &candidate_b);
      if (!check(std::abs(ref_a - candidate_a) < 1.0e-7F &&
                     std::abs(ref_b - candidate_b) < 1.0e-7F,
                 "physically equivalent OKLab colors diverged across admitted working gamuts")) {
        return false;
      }
    }
  }

  const auto triadic = compile_palette("Triadic", kBase, 100.0);
  const auto display_anchors = std::array<float, 4>{
      wrap_turn(static_cast<float>(kBase)),
      wrap_turn(static_cast<float>(kBase + 1.0 / 3.0)),
      wrap_turn(static_cast<float>(kBase + 2.0 / 3.0)),
      wrap_turn(static_cast<float>(kBase))};
  for (auto index = 0U; index < display_anchors.size(); ++index) {
    const auto expected = wrap_turn(static_cast<float>(
        photo_editor::pipeline::oklab_hue_for_rgb_hue(
            static_cast<double>(display_anchors[index])) /
        static_cast<double>(kTwoPi)));
    if (!check(std::abs(triadic.harmony_anchors[index] - expected) < 1.0e-7F,
               "compiled palette anchor does not follow the canonical display-to-OKLab hue contract")) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool test_frozen_legacy_recipe_semantics() {
  struct LegacyPaletteCase final {
    std::string_view mode;
    std::array<float, 4> anchors;
    std::uint32_t count;
  };
  constexpr auto kLegacyPalettes = std::array<LegacyPaletteCase, 5>{{
      {"Analogous", {0.116666667F, 0.2F, 0.283333333F, 0.2F}, 3U},
      {"Complementary", {0.2F, 0.7F, 0.2F, 0.2F}, 2U},
      {"Triadic", {0.0F, 1.0F / 3.0F, 2.0F / 3.0F, 0.0F}, 3U},
      {"Split Complementary", {0.2F, 0.616666667F, 0.783333333F, 0.2F}, 3U},
      {"Tetradic", {0.2F, 0.45F, 0.7F, 0.95F}, 4U},
  }};
  const auto legacy = PhotographicRenderingModel::LegacyOklchV1;
  for (const auto& palette : kLegacyPalettes) {
    const auto compiled = compile_palette(
        palette.mode, palette.mode == "Triadic" ? 0.0 : 0.2, 100.0,
        "Linear ProPhoto RGB", legacy);
    if (!check(compiled.harmony_anchor_count == palette.count &&
                   (compiled.active_mask & CompiledLookStageProgram::kColorHarmony) != 0U &&
                   (compiled.active_mask & CompiledLookStageProgram::kLegacyCreativeSemantics) != 0U,
               "active legacy harmony did not retain its historical semantic flag and anchor count")) {
      return false;
    }
    for (auto index = 0U; index < palette.anchors.size(); ++index) {
      if (!check(std::abs(compiled.harmony_anchors[index] - palette.anchors[index]) <
                     5.0e-7F,
                 "legacy recipe palette anchor was remapped from its saved UI hue")) {
        return false;
      }
    }
  }

  const auto old_harmony = compile_palette("Triadic", 0.0, 100.0,
                                           "Linear ProPhoto RGB", legacy);
  const auto to_radians = [](float degrees) {
    return degrees * kPi / 180.0F;
  };
  const auto to_degrees = [](float radians) {
    return radians * 180.0F / kPi;
  };
  const auto before_boundary = to_degrees(
      output_hue(old_harmony, to_radians(59.99F)));
  const auto after_boundary = to_degrees(
      output_hue(old_harmony, to_radians(60.01F)));
  if (!check(std::abs(before_boundary - 20.9965F) < 0.003F &&
                 std::abs(after_boundary - 99.0035F) < 0.003F,
             "legacy Triadic recipe no longer matches its fixed historical nearest-anchor outputs")) {
    std::cerr << "legacy outputs: " << before_boundary << " and "
              << after_boundary << " degrees\n";
    return false;
  }
  const auto tie_output = to_degrees(
      output_hue(old_harmony, to_radians(60.0F)));
  if (!check(std::abs(tie_output - 21.0F) < 0.003F,
             "legacy palette tie no longer resolves to the first saved anchor")) {
    return false;
  }

  const auto alternate_legacy_model = compile_palette(
      "Triadic", 0.0, 100.0, "Linear ProPhoto RGB",
      PhotographicRenderingModel::Aces2HellwigJmhV1);
  if (!check((alternate_legacy_model.active_mask &
              CompiledLookStageProgram::kLegacyCreativeSemantics) != 0U,
             "pre-ACES2-OCIO renderer did not retain legacy harmony semantics")) {
    return false;
  }
  const auto modern = compile_palette("Triadic", 0.0, 100.0);
  if (!check((modern.active_mask &
              CompiledLookStageProgram::kLegacyCreativeSemantics) == 0U,
             "modern continuous harmony was marked with legacy semantics")) {
    return false;
  }
  const auto disabled = compile_palette("Triadic", 0.0, 0.0,
                                        "Linear ProPhoto RGB", legacy);
  if (!check((disabled.active_mask & CompiledLookStageProgram::kColorHarmony) == 0U &&
                 (disabled.active_mask &
                  CompiledLookStageProgram::kLegacyCreativeSemantics) == 0U,
             "inactive legacy harmony set a semantic bit or active stage")) {
    return false;
  }
  return true;
}

[[nodiscard]] bool test_compiled_harmony_grading_semantic_flag_ownership() {
  ColorAdjustmentState legacy_state;
  legacy_state.color_pipeline.photographic_rendering_model =
      PhotographicRenderingModel::LegacyOklchV1;
  legacy_state.color_harmony_enabled = true;
  legacy_state.color_harmony_mode = "Triadic";
  legacy_state.color_harmony_base_hue = 0.2;
  legacy_state.color_harmony_strength = 100.0;
  legacy_state.grading_amount = 100.0;
  legacy_state.grading_midtones_hue = 0.37;
  legacy_state.grading_midtones_saturation = 50.0;

  const auto legacy = photo_editor::pipeline::compile_look_stage_program(
      legacy_state);
  const auto required_bits = CompiledLookStageProgram::kColorHarmony |
                             CompiledLookStageProgram::kColorGrading |
                             CompiledLookStageProgram::kLegacyCreativeSemantics;
  if (!check((legacy.active_mask & required_bits) == required_bits,
             "full look compilation lost legacy harmony semantics when grading compiled afterward")) {
    return false;
  }
  if (!check(legacy.harmony_anchors[0] == 0.2F &&
                 legacy.grading_hue[1] == 0.37F,
             "combined legacy look remapped saved harmony or grading display hues")) {
    return false;
  }

  auto grade_only = legacy_state;
  grade_only.color_harmony_enabled = false;
  const auto legacy_grade_only =
      photo_editor::pipeline::compile_look_stage_program(grade_only);
  if (!check((legacy_grade_only.active_mask &
              CompiledLookStageProgram::kLegacyCreativeSemantics) != 0U &&
                 (legacy_grade_only.active_mask &
                  CompiledLookStageProgram::kColorHarmony) == 0U,
             "legacy grading did not independently own its semantic flag")) {
    return false;
  }

  auto modern_state = legacy_state;
  modern_state.color_pipeline.photographic_rendering_model =
      PhotographicRenderingModel::Aces2OcioV1;
  const auto modern = photo_editor::pipeline::compile_look_stage_program(
      modern_state);
  const auto modern_bits = CompiledLookStageProgram::kColorHarmony |
                           CompiledLookStageProgram::kColorGrading;
  if (!check((modern.active_mask & modern_bits) == modern_bits &&
                 (modern.active_mask &
                  CompiledLookStageProgram::kLegacyCreativeSemantics) == 0U,
             "modern combined harmony and grading inherited the legacy semantic flag")) {
    return false;
  }
  const auto expected_harmony_anchor = wrap_turn(static_cast<float>(
      photo_editor::pipeline::oklab_hue_for_rgb_hue(0.2) /
      static_cast<double>(kTwoPi)));
  const auto expected_grading_hue = wrap_turn(static_cast<float>(
      photo_editor::pipeline::oklab_hue_for_rgb_hue(0.37) /
      static_cast<double>(kTwoPi)));
  return check(std::abs(modern.harmony_anchors[0] - expected_harmony_anchor) <
                   1.0e-7F &&
                   std::abs(modern.grading_hue[1] - expected_grading_hue) <
                       1.0e-7F,
               "modern combined look did not apply the canonical hue contract to both stages");
}

[[nodiscard]] bool test_every_anchor_is_stable_and_every_bisector_is_repulsive() {
  constexpr auto kModes = std::array<std::string_view, 5>{
      "Analogous", "Complementary", "Triadic", "Split Complementary",
      "Tetradic"};
  for (const auto mode : kModes) {
    const auto compiled = compile_palette(mode, 0.317, 100.0);
    if (!check(compiled.harmony_anchor_count >= 2U &&
                   compiled.harmony_anchor_count <= 4U,
               "palette did not compile its supported anchor set")) {
      return false;
    }

    for (auto index = 0U; index < compiled.harmony_anchor_count; ++index) {
      const auto anchor_turn = compiled.harmony_anchors[index];
      auto previous_gap = 1.0F;
      auto next_gap = 1.0F;
      for (auto other = 0U; other < compiled.harmony_anchor_count; ++other) {
        if (other == index) continue;
        const auto clockwise =
            wrap_turn(compiled.harmony_anchors[other] - anchor_turn);
        const auto counterclockwise =
            wrap_turn(anchor_turn - compiled.harmony_anchors[other]);
        if (clockwise > 0.0F) next_gap = std::min(next_gap, clockwise);
        if (counterclockwise > 0.0F) {
          previous_gap = std::min(previous_gap, counterclockwise);
        }
      }
      const auto anchor_hue = anchor_turn * kTwoPi;
      const auto epsilon_turn = std::min(previous_gap, next_gap) * 0.01F;
      const auto epsilon = std::max(epsilon_turn * kTwoPi, 1.0e-4F);
      if (!check(angular_distance(output_hue(compiled, anchor_hue), anchor_hue) <
                     2.0e-5F,
                 "palette anchor moved under full-strength harmony")) {
        return false;
      }
      if (!check(circular_force(anchor_hue + epsilon, compiled) < 0.0F &&
                     circular_force(anchor_hue - epsilon, compiled) > 0.0F,
                 "a compiled palette anchor is not an attracting fixed point")) {
        return false;
      }

      const auto midpoint_turn = wrap_turn(anchor_turn + next_gap * 0.5F);
      const auto midpoint_hue = midpoint_turn * kTwoPi;
      const auto midpoint_epsilon =
          std::min(next_gap * 0.01F * kTwoPi, 1.0e-3F);
      const auto midpoint_force = circular_force(midpoint_hue, compiled);
      if (!check(std::abs(midpoint_force) < 2.0e-5F,
                 "palette bisector is not an analytic zero-force tie")) {
        return false;
      }
      if (!check(circular_force(midpoint_hue - midpoint_epsilon, compiled) < 0.0F &&
                     circular_force(midpoint_hue + midpoint_epsilon, compiled) > 0.0F,
                 "palette bisector does not repel toward its adjacent anchors")) {
        return false;
      }

      for (const auto strength : {20.0, 55.0, 100.0}) {
        const auto scaled = compile_palette(mode, 0.317, strength);
        const auto quarter_hue =
            wrap_turn(anchor_turn + next_gap * 0.25F) * kTwoPi;
        const auto input_offset = next_gap * kTwoPi * 0.25F;
        const auto displacement = std::abs(std::remainder(
            output_hue(scaled, quarter_hue) - quarter_hue, kTwoPi));
        const auto full_nearest_anchor_displacement =
            static_cast<float>(std::clamp(strength / 100.0, 0.0, 1.0) *
                               0.65) * input_offset;
        const auto retained_fraction =
            displacement / full_nearest_anchor_displacement;
        if (!check(displacement > 0.0F && retained_fraction > 0.88F &&
                       retained_fraction < 0.90F,
                   "quarter-gap palette guidance did not retain the documented 89% of the prior pull")) {
          return false;
        }
      }
    }
  }
  return true;
}

[[nodiscard]] bool test_analytic_single_anchor_wrap_and_ties() {
  CompiledLookStageProgram single_anchor;
  single_anchor.harmony_anchors = {0.125F, 0.0F, 0.0F, 0.0F};
  single_anchor.harmony_anchor_count = 1U;
  single_anchor.harmony_strength = 0.65F;

  const auto anchor = 0.125F * kTwoPi;
  const auto quarter_turn = anchor + 0.25F * kTwoPi;
  const auto expected_shift =
      -single_anchor.harmony_strength *
      kPhotoEditorColorHarmonyAttractionScale;
  const auto observed_shift = std::remainder(
      output_hue(single_anchor, quarter_turn) - quarter_turn, kTwoPi);
  if (!check(std::abs(observed_shift - expected_shift) < 2.0e-6F,
             "single-anchor quarter-circle attraction missed its analytic value")) {
    return false;
  }
  for (const auto skin_scale : {0.2F, 0.5F, 0.8F}) {
    const auto skin_scaled_shift = std::remainder(
        output_hue(single_anchor, quarter_turn, skin_scale) - quarter_turn,
        kTwoPi);
    const auto expected_skin_scaled_shift = expected_shift * skin_scale;
    if (!check(std::abs(skin_scaled_shift - expected_skin_scaled_shift) <
                   2.0e-6F,
               "harmony did not preserve the caller's skin-protection scale")) {
      return false;
    }
  }

  const auto antipode = anchor + kPi;
  if (!check(circular_force(antipode, single_anchor) == 0.0F &&
                 angular_distance(output_hue(single_anchor, antipode), antipode) <
                     2.0e-6F,
             "single-anchor antipode was not handled as an exact analytic tie")) {
    return false;
  }

  CompiledLookStageProgram wrapped_pair;
  wrapped_pair.harmony_anchors = {0.95F, 0.45F, 0.0F, 0.0F};
  wrapped_pair.harmony_anchor_count = 2U;
  wrapped_pair.harmony_strength = 0.65F;
  const auto seam_tie = 0.20F * kTwoPi;
  if (!check(std::abs(circular_force(seam_tie, wrapped_pair)) < 2.0e-6F &&
                 angular_distance(output_hue(wrapped_pair, seam_tie), seam_tie) <
                     2.0e-6F,
             "wrapped unequal-order bisector did not resolve to zero force")) {
    return false;
  }

  CompiledLookStageProgram ordered;
  ordered.harmony_anchors = {0.97F, 0.16F, 0.49F, 0.71F};
  ordered.harmony_anchor_count = 4U;
  CompiledLookStageProgram permuted = ordered;
  permuted.harmony_anchors = {0.49F, 0.97F, 0.71F, 0.16F};
  for (auto index = 0U; index < 360U; ++index) {
    const auto hue = static_cast<float>(index) / 360.0F * kTwoPi;
    if (!check(std::abs(circular_force(hue, ordered) -
                        circular_force(hue, permuted)) < 2.0e-6F,
               "circular palette force depends on anchor array order")) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool test_dense_monotone_lipschitz_sweeps() {
  constexpr auto kModes = std::array<std::string_view, 5>{
      "Analogous", "Complementary", "Triadic", "Split Complementary",
      "Tetradic"};
  constexpr auto kStrengths = std::array<double, 4>{0.0, 12.0, 48.0, 100.0};
  constexpr auto kSamples = 16384U;

  for (const auto mode : kModes) {
    for (const auto user_strength : kStrengths) {
      const auto compiled = compile_palette(mode, 0.827, user_strength);
      const auto alpha = compiled.harmony_strength;
      const auto slope_min =
          1.0F - alpha * kPhotoEditorColorHarmonyAttractionScale;
      const auto slope_max =
          1.0F + alpha * kPhotoEditorColorHarmonyAttractionScale;
      const auto step = kTwoPi / static_cast<float>(kSamples);
      auto previous_input = 0.0F;
      auto previous_output = output_hue(compiled, previous_input);
      auto unwrapped_output = previous_output;
      auto observed_min = 1000.0F;
      auto observed_max = -1000.0F;

      for (auto sample = 1U; sample <= kSamples; ++sample) {
        const auto input = step * static_cast<float>(sample);
        const auto output = output_hue(compiled, input);
        const auto output_step = std::remainder(output - previous_output, kTwoPi);
        const auto slope = output_step / (input - previous_input);
        observed_min = std::min(observed_min, slope);
        observed_max = std::max(observed_max, slope);
        if (!check(slope >= slope_min - 0.015F &&
                       slope <= slope_max + 0.015F,
                   "dense hue sweep exceeded the analytic derivative/Lipschitz bound")) {
          std::cerr << "mode=" << mode << " strength=" << user_strength
                    << " sample=" << sample << " slope=" << slope << '\n';
          return false;
        }
        if (!check(output_step > 0.0F,
                   "harmony reversed a dense monotonically increasing hue ramp")) {
          return false;
        }
        unwrapped_output += output_step;
        previous_input = input;
        previous_output = output;
      }
      if (!check(std::abs((unwrapped_output -
                           output_hue(compiled, 0.0F)) -
                          kTwoPi) < 2.0e-3F,
                 "harmony hue ramp failed periodicity after one full turn")) {
        return false;
      }
      if (!check(observed_min >= slope_min - 0.015F &&
                     observed_max <= slope_max + 0.015F,
                 "harmony's sampled slope range exceeded its closed-form bound")) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool test_identity_neutral_chroma_and_extended_values() {
  CompiledLookStageProgram active;
  active.harmony_anchors = {0.0F, 1.0F / 3.0F, 2.0F / 3.0F, 0.0F};
  active.harmony_anchor_count = 3U;
  active.harmony_strength = 0.65F;

  auto a = -0.137F;
  auto b = 0.219F;
  const auto original_a = a;
  const auto original_b = b;
  auto zero_strength = active;
  zero_strength.harmony_strength = 0.0F;
  photo_editor::pipeline::apply_compiled_look_harmony(
      zero_strength, 1.0F, &a, &b);
  if (!check(a == original_a && b == original_b,
             "zero-strength harmony did not preserve exact input identity")) {
    return false;
  }

  photo_editor::pipeline::apply_compiled_look_harmony(
      active, 0.0F, &a, &b);
  if (!check(a == original_a && b == original_b,
             "zero skin scale did not preserve exact input identity")) {
    return false;
  }

  a = 0.0F;
  b = 0.0F;
  photo_editor::pipeline::apply_compiled_look_harmony(
      active, 1.0F, &a, &b);
  if (!check(a == 0.0F && b == 0.0F,
             "exact neutral changed under hue-only harmony")) {
    return false;
  }

  constexpr auto kExtendedSamples =
      std::array<std::array<float, 2>, 5>{{
          {0.31F, -0.77F},
          {-2.0F, -3.0F},
          {4.5F, -1.25F},
          {-1.0e-5F, 2.0e-5F},
          {320.0F, -640.0F},
      }};
  for (const auto& sample : kExtendedSamples) {
    a = sample[0];
    b = sample[1];
    const auto chroma_before = std::hypot(a, b);
    photo_editor::pipeline::apply_compiled_look_harmony(
        active, 0.81F, &a, &b);
    const auto chroma_after = std::hypot(a, b);
    const auto tolerance = 2.0e-6F * std::max(1.0F, chroma_before);
    if (!check(std::isfinite(a) && std::isfinite(b) &&
                   std::abs(chroma_after - chroma_before) <= tolerance,
               "signed/HDR/faint OKLab chroma was clipped or altered by hue harmony")) {
      return false;
    }
  }

  a = 1.0e-5F * std::cos(0.2F);
  b = 1.0e-5F * std::sin(0.2F);
  const auto faint_hue_before = std::atan2(b, a);
  photo_editor::pipeline::apply_compiled_look_harmony(
      active, 1.0F, &a, &b);
  if (!check(angular_distance(std::atan2(b, a), faint_hue_before) > 1.0e-3F,
             "genuine faint color was incorrectly treated as neutral")) {
    return false;
  }
  return true;
}

[[nodiscard]] bool test_original_jump_regression() {
  CompiledLookStageProgram triadic;
  triadic.harmony_anchors = {0.0F, 1.0F / 3.0F, 2.0F / 3.0F, 0.0F};
  triadic.harmony_anchor_count = 3U;
  triadic.harmony_strength = 0.65F;

  const auto epsilon = 0.01F * kPi / 180.0F;
  const auto boundary = kPi / 3.0F;
  const auto before = output_hue(triadic, boundary - epsilon);
  const auto after = output_hue(triadic, boundary + epsilon);
  const auto output_gap = angular_distance(before, after);
  if (!check(output_gap < 1.91F * 2.0F * epsilon + 5.0e-6F,
             "Triadic bisector still has the old finite harmony jump")) {
    std::cerr << "0.02-degree input gap produced " << output_gap * 180.0F / kPi
              << " degrees of output gap\n";
    return false;
  }

  const auto seam_before = output_hue(triadic, kPi - 1.0e-4F);
  const auto seam_after = output_hue(triadic, -kPi + 1.0e-4F);
  if (!check(angular_distance(seam_before, seam_after) < 7.7e-4F,
             "hue wrap seam contains a finite output discontinuity")) {
    return false;
  }
  return true;
}

}  // namespace

int main() {
  if (!test_original_jump_regression() ||
      !test_canonical_anchor_mapping_and_profiles() ||
      !test_frozen_legacy_recipe_semantics() ||
      !test_compiled_harmony_grading_semantic_flag_ownership() ||
      !test_every_anchor_is_stable_and_every_bisector_is_repulsive() ||
      !test_analytic_single_anchor_wrap_and_ties() ||
      !test_dense_monotone_lipschitz_sweeps() ||
      !test_identity_neutral_chroma_and_extended_values()) {
    return EXIT_FAILURE;
  }
  std::cout << "continuous color harmony contracts passed\n";
  return EXIT_SUCCESS;
}

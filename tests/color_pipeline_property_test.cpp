#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"
#include "../engine/src/pipeline/color_input_runtime.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/output_rendering.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::IccRenderingIntent;
using photo_editor::pipeline::IccRgbaFloat;
using photo_editor::pipeline::IccTransformOptions;
using photo_editor::pipeline::RgbColorSpace;

[[nodiscard]] int fail(std::string_view property, std::string_view detail) {
  std::cerr << "color property failed: " << property << ": " << detail << '\n';
  return EXIT_FAILURE;
}

[[nodiscard]] bool finite_pixel(const ColorPixel& pixel) {
  return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
         std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
}

[[nodiscard]] double maximum_channel_error(
    const std::array<double, 3>& left,
    const std::array<double, 3>& right) {
  return std::max({
      std::abs(left[0] - right[0]),
      std::abs(left[1] - right[1]),
      std::abs(left[2] - right[2]),
  });
}

[[nodiscard]] double maximum_pixel_channel_error(
    const ColorPixel& left,
    const ColorPixel& right) {
  return std::max({
      std::abs(
          static_cast<double>(left.red) -
          static_cast<double>(right.red)),
      std::abs(
          static_cast<double>(left.green) -
          static_cast<double>(right.green)),
      std::abs(
          static_cast<double>(left.blue) -
          static_cast<double>(right.blue)),
  });
}

[[nodiscard]] double circular_hue_error(double left, double right) {
  auto error = std::fmod(std::abs(left - right), 360.0);
  return std::min(error, 360.0 - error);
}

[[nodiscard]] ColorAdjustmentState linear_adjustment_state(double exposure) {
  ColorAdjustmentState state;
  state.color_pipeline.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::
          LegacyOklchV1;
  state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  state.color_pipeline.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  state.color_pipeline.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  state.color_pipeline.output_headroom = 1.0;
  state.input_transform.exposure = exposure;
  return state;
}

[[nodiscard]] ColorAdjustmentState modern_scene_adjustment_state(
    double exposure) {
  auto state = linear_adjustment_state(exposure);
  state.color_pipeline.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1;
  state.color_pipeline.render_intent =
      photo_editor::pipeline::RenderIntent::Perceptual;
  return state;
}

[[nodiscard]] std::vector<ColorPixel> apply_exposure(
    std::vector<ColorPixel> pixels,
    double exposure) {
  photo_editor::pipeline::apply_color_adjustments(
      &pixels,
      static_cast<int>(pixels.size()),
      1,
      linear_adjustment_state(exposure));
  return pixels;
}

}  // namespace

int main() {
  // A bare engine state has no decoded RAW provenance. It must therefore
  // preserve generic signed working RGB rather than silently enabling RAW
  // black-floor and highlight-reconstruction behavior.
  ColorAdjustmentState default_non_raw_state;
  default_non_raw_state.color_pipeline.output_headroom = 16.0;
  std::vector<ColorPixel> default_non_raw_pixels = {
      {
          .red = -0.25F,
          .green = 0.40F,
          .blue = 2.00F,
          .alpha = 0.61F,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &default_non_raw_pixels,
      1,
      1,
      default_non_raw_state);
  if (!finite_pixel(default_non_raw_pixels.front()) ||
      default_non_raw_pixels.front().red >= -0.05F ||
      default_non_raw_pixels.front().blue <= 1.0F ||
      std::abs(default_non_raw_pixels.front().alpha - 0.61F) > 1.0e-7F) {
    return fail(
        "default-input-is-not-raw",
        "a metadata-free engine state applied RAW clipping to working RGB");
  }

  // The registry is the product-facing source of truth for edit semantics. It
  // must remain complete, unique, and in the same monotonic order as execution.
  const auto control_semantics =
      photo_editor::pipeline::core_color_control_semantics();
  if (control_semantics.size() != 43) {
    return fail("control-semantics-completeness",
                "the core color control registry size changed without review");
  }
  for (std::size_t index = 0; index < control_semantics.size(); ++index) {
    const auto& semantics = control_semantics[index];
    if (semantics.key.empty() || semantics.label.empty() ||
        semantics.description.empty() || semantics.order <= 0 ||
        photo_editor::pipeline::color_control_semantics_for_key(semantics.key) !=
            &semantics) {
      return fail("control-semantics-lookup",
                  "a control has incomplete or non-authoritative metadata");
    }
    if (index > 0 &&
        (semantics.order <= control_semantics[index - 1].order ||
         static_cast<int>(semantics.stage) <
             static_cast<int>(control_semantics[index - 1].stage))) {
      return fail("control-semantics-order",
                  "control execution metadata is not strictly ordered");
    }
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      if (control_semantics[earlier].key == semantics.key) {
        return fail("control-semantics-uniqueness",
                    "duplicate control key in semantics registry");
      }
    }
  }
  const auto require_semantics = [](
                                     std::string_view key,
                                     photo_editor::pipeline::ColorOperationDomain domain,
                                     photo_editor::pipeline::ColorPipelineStage stage,
                                     bool reversible) {
    const auto* semantics =
        photo_editor::pipeline::color_control_semantics_for_key(key);
    return semantics != nullptr && semantics->domain == domain &&
           semantics->stage == stage &&
           semantics->mathematically_reversible == reversible;
  };
  using photo_editor::pipeline::ColorOperationDomain;
  using photo_editor::pipeline::ColorPipelineStage;
  if (!require_semantics("exposure", ColorOperationDomain::SceneReferred,
                         ColorPipelineStage::InputSceneLinear, true) ||
      !require_semantics("contrast", ColorOperationDomain::SceneReferred,
                         ColorPipelineStage::SceneTone, false) ||
      !require_semantics("highlights", ColorOperationDomain::SceneReferred,
                         ColorPipelineStage::SceneTone, false) ||
      !require_semantics("masterCurvePoints",
                         ColorOperationDomain::DisplayReferred,
                         ColorPipelineStage::DisplayCurve, false) ||
      !require_semantics("vibrance", ColorOperationDomain::LookSpace,
                         ColorPipelineStage::PerceptualLook, false) ||
      !require_semantics("colorGrading", ColorOperationDomain::LookSpace,
                         ColorPipelineStage::PerceptualLook, false) ||
      !require_semantics("saturation", ColorOperationDomain::LookSpace,
                         ColorPipelineStage::PerceptualLook, false) ||
      !require_semantics("displayEncoding",
                         ColorOperationDomain::DisplayReferred,
                         ColorPipelineStage::OutputRendering, true) ||
      !require_semantics("softProof", ColorOperationDomain::DisplayReferred,
                         ColorPipelineStage::ProofSimulation, false) ||
      photo_editor::pipeline::color_control_semantics_for_key(
          "not-a-color-control") != nullptr) {
    return fail("control-semantics-classification",
                "a core control moved to the wrong domain or stage");
  }
  for (const auto key :
       {"contrast", "highlights", "shadows", "whites", "blacks"}) {
    const auto* semantics =
        photo_editor::pipeline::color_control_semantics_for_key(key);
    if (semantics == nullptr || semantics->image_dependent) {
      return fail(
          "control-semantics-image-dependence",
          "modern monotone tone controls must not depend on unrelated image extrema");
    }
  }

  // Lock the executable stage contract: scene-linear exposure feeds the
  // display-style master curve. The reverse composition is intentionally
  // different for this nonlinear curve.
  auto ordered_state = linear_adjustment_state(1.0);
  ordered_state.master_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 0.5, .y = 0.24},
      {.x = 1.0, .y = 1.0},
  };
  const auto ordered_lut = photo_editor::pipeline::build_color_lut(ordered_state);
  const auto ordered_pixel = photo_editor::pipeline::apply_lut(
      ordered_lut,
      {.red = 0.125F, .green = 0.125F, .blue = 0.125F, .alpha = 0.7F});
  const auto curve_response = photo_editor::pipeline::build_perceptual_curve_lut(
      ordered_state.master_curve_points,
      257);
  const auto exposure_then_curve =
      static_cast<double>(curve_response[64]);
  const auto curve_then_exposure =
      static_cast<double>(curve_response[32]) * 2.0;
  if (maximum_channel_error(
          {ordered_pixel.red, ordered_pixel.green, ordered_pixel.blue},
          {exposure_then_curve, exposure_then_curve, exposure_then_curve}) >
          2.0e-5 ||
      std::abs(exposure_then_curve - curve_then_exposure) < 1.0e-2 ||
      std::abs(ordered_pixel.alpha - 0.7F) > 1.0e-6F) {
    return fail("control-stage-execution-order",
                "exposure no longer executes before the master curve");
  }

  // Exposure is scene-linear: a stop is exactly a factor of two, and two
  // sequential one-stop edits compose to one two-stop edit below the shoulder.
  const std::vector<ColorPixel> exposure_inputs = {
      {.red = 0.010F, .green = 0.020F, .blue = 0.030F, .alpha = 0.15F},
      {.red = 0.080F, .green = 0.040F, .blue = 0.015F, .alpha = 0.50F},
      {.red = 0.035F, .green = 0.120F, .blue = 0.070F, .alpha = 0.85F},
      {.red = 0.150F, .green = 0.110F, .blue = 0.045F, .alpha = 1.00F},
  };
  for (const double ev : {-5.0, -3.0, 3.0, 5.0, 0.01}) {
    auto state = linear_adjustment_state(ev);
    const auto context = photo_editor::pipeline::make_pipeline_color_context(state);
    const auto parameters = photo_editor::pipeline::make_input_stage_parameters(state, context);
    auto output = exposure_inputs;
    for (auto& pixel : output)
      pixel = photo_editor::pipeline::apply_scene_adjustments_stage(pixel, parameters);
    const auto gain = std::exp2(ev);
    for (std::size_t index = 0; index < exposure_inputs.size(); ++index) {
      const auto& source = exposure_inputs[index];
      const auto& pixel = output[index];
      if (maximum_channel_error({pixel.red, pixel.green, pixel.blue},
                                {source.red * gain, source.green * gain, source.blue * gain}) > 2e-5 ||
          pixel.alpha != source.alpha)
      {
        return fail("extended-exposure", "EV must scale linear RGB by 2^EV and preserve alpha");
      }
    }
  }
  const auto plus_one = apply_exposure(exposure_inputs, 1.0);
  const auto plus_two = apply_exposure(exposure_inputs, 2.0);
  const auto composed_plus_two = apply_exposure(plus_one, 1.0);
  for (std::size_t index = 0; index < exposure_inputs.size(); ++index) {
    const auto& source = exposure_inputs[index];
    const auto& one = plus_one[index];
    const auto& two = plus_two[index];
    const auto& composed = composed_plus_two[index];
    const std::array<double, 3> source_rgb = {source.red, source.green, source.blue};
    const std::array<double, 3> one_rgb = {one.red, one.green, one.blue};
    const std::array<double, 3> two_rgb = {two.red, two.green, two.blue};
    const std::array<double, 3> composed_rgb = {
        composed.red,
        composed.green,
        composed.blue,
    };
    if (maximum_channel_error(
            one_rgb,
            {source_rgb[0] * 2.0, source_rgb[1] * 2.0, source_rgb[2] * 2.0}) >
            3.0e-6 ||
        maximum_channel_error(
            two_rgb,
            {source_rgb[0] * 4.0, source_rgb[1] * 4.0, source_rgb[2] * 4.0}) >
            3.0e-6 ||
        maximum_channel_error(two_rgb, composed_rgb) > 3.0e-6 ||
        std::abs(one.alpha - source.alpha) > 1.0e-7F ||
        std::abs(two.alpha - source.alpha) > 1.0e-7F) {
      return fail("exposure-linearity", "one-stop gain or exposure composition drifted");
    }
  }

  // Modern ACES recipes must preserve scene-linear RAW range until output
  // rendering. A one-stop exposure therefore remains a factor of two above
  // diffuse white, while legacy recipes keep their historical SDR shoulder.
  const auto extended_source =
      ColorPixel{.red = 0.75F, .green = 0.60F, .blue = 0.40F, .alpha = 0.42F};
  auto modern_extended = std::vector<ColorPixel>{extended_source};
  photo_editor::pipeline::apply_color_adjustments(
      &modern_extended,
      1,
      1,
      modern_scene_adjustment_state(1.0));
  const auto& modern_extended_pixel = modern_extended.front();
  if (maximum_channel_error(
          {modern_extended_pixel.red,
           modern_extended_pixel.green,
           modern_extended_pixel.blue},
          {1.50, 1.20, 0.80}) >
          4.0e-6 ||
      std::abs(modern_extended_pixel.alpha - extended_source.alpha) > 1.0e-7F) {
    return fail(
        "aces-scene-exposure-headroom",
        "modern exposure clipped or reshaped RGB before output rendering");
  }
  auto legacy_extended = std::vector<ColorPixel>{extended_source};
  photo_editor::pipeline::apply_color_adjustments(
      &legacy_extended,
      1,
      1,
      linear_adjustment_state(1.0));
  if (std::max({
          legacy_extended.front().red,
          legacy_extended.front().green,
          legacy_extended.front().blue,
      }) >
      1.000001F) {
    return fail(
        "legacy-headroom-compatibility",
        "legacy recipes no longer retain their historical SDR headroom");
  }

  // Adjustment layers share the same scene headroom. Repeated exposure and
  // tone stages must compose without each layer forcing highlights back to 1.
  auto modern_layered_state = modern_scene_adjustment_state(0.0);
  auto scene_layer = photo_editor::pipeline::PhotoLayer{};
  scene_layer.exposure = 0.5;
  scene_layer.brightness = 1.0;
  modern_layered_state.photo_layers = {scene_layer, scene_layer};
  auto modern_layered =
      std::vector<ColorPixel>{{.red = 0.75F,
                               .green = 0.75F,
                               .blue = 0.75F,
                               .alpha = 0.64F}};
  photo_editor::pipeline::apply_color_adjustments(
      &modern_layered,
      1,
      1,
      modern_layered_state);
  const auto& layered_pixel = modern_layered.front();
  if (layered_pixel.red <= 1.45F ||
      std::max({
          std::abs(layered_pixel.red - layered_pixel.green),
          std::abs(layered_pixel.green - layered_pixel.blue),
      }) >
          2.0e-6F ||
      std::abs(layered_pixel.alpha - 0.64F) > 1.0e-7F) {
    return fail(
        "aces-adjustment-layer-headroom",
        "stacked scene adjustments destructively clipped highlight detail");
  }

  // Strong highlight recovery must preserve ordering instead of flattening a
  // bright RAW ramp into a single clipped value.
  auto highlight_state = modern_scene_adjustment_state(2.0);
  highlight_state.highlights = -60.0;
  auto highlight_ramp = std::vector<ColorPixel>{
      {.red = 0.40F, .green = 0.40F, .blue = 0.40F, .alpha = 1.0F},
      {.red = 0.60F, .green = 0.60F, .blue = 0.60F, .alpha = 1.0F},
      {.red = 0.80F, .green = 0.80F, .blue = 0.80F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &highlight_ramp,
      static_cast<int>(highlight_ramp.size()),
      1,
      highlight_state);
  if (!(highlight_ramp[0].red + 1.0e-4F < highlight_ramp[1].red &&
        highlight_ramp[1].red + 1.0e-4F < highlight_ramp[2].red &&
        highlight_ramp[2].red > 1.0F)) {
    return fail(
        "aces-highlight-detail-ordering",
        "highlight recovery flattened the extended-range scene ramp");
  }

  auto chromatic_tone_state = modern_scene_adjustment_state(1.0);
  chromatic_tone_state.contrast = 45.0;
  chromatic_tone_state.highlights = -55.0;
  chromatic_tone_state.shadows = 30.0;
  chromatic_tone_state.whites = 25.0;
  const auto chromatic_tone_source =
      ColorPixel{.red = 1.20F, .green = 0.60F, .blue = 0.30F, .alpha = 0.63F};
  auto chromatic_tone_output =
      std::vector<ColorPixel>{chromatic_tone_source};
  photo_editor::pipeline::apply_color_adjustments(
      &chromatic_tone_output, 1, 1, chromatic_tone_state);
  const auto& chromatic_tone_pixel = chromatic_tone_output.front();
  if (std::abs(
          static_cast<double>(chromatic_tone_pixel.red) /
              chromatic_tone_pixel.green -
          2.0) >
          4.0e-6 ||
      std::abs(
          static_cast<double>(chromatic_tone_pixel.green) /
              chromatic_tone_pixel.blue -
          2.0) >
          4.0e-6 ||
      std::abs(chromatic_tone_pixel.alpha - chromatic_tone_source.alpha) >
          1.0e-7F) {
    return fail(
        "tone-preserves-chromatic-ratios",
        "scene-tone luminance shaping changed hue/chroma before a declared gamut or creative operation");
  }

  auto shadow_recovery_state = modern_scene_adjustment_state(3.0);
  shadow_recovery_state.shadows = 70.0;
  shadow_recovery_state.blacks = 20.0;
  auto neutral_shadow_ramp = std::vector<ColorPixel>{
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F},
      {.red = 0.001F, .green = 0.001F, .blue = 0.001F},
      {.red = 0.004F, .green = 0.004F, .blue = 0.004F},
      {.red = 0.016F, .green = 0.016F, .blue = 0.016F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &neutral_shadow_ramp,
      static_cast<int>(neutral_shadow_ramp.size()),
      1,
      shadow_recovery_state);
  for (std::size_t index = 0; index < neutral_shadow_ramp.size(); ++index) {
    const auto& pixel = neutral_shadow_ramp[index];
    if (!finite_pixel(pixel) || pixel.red < 0.0F ||
        std::abs(pixel.red - pixel.green) > 1.0e-7F ||
        std::abs(pixel.green - pixel.blue) > 1.0e-7F ||
        (index > 0 &&
         !(neutral_shadow_ramp[index - 1].red < pixel.red))) {
      return fail(
          "shadow-black-floor-neutrality",
          "shadow recovery changed neutral hue, ordering, or finite black-floor behavior");
    }
  }
  if (neutral_shadow_ramp.front().red > 1.0e-7F) {
    return fail(
        "shadow-black-floor-zero",
        "creative shadow recovery lifted mathematical black");
  }

  const auto contaminated_shadow_source =
      ColorPixel{.red = 0.004F, .green = 0.003F, .blue = 0.005F};
  auto contaminated_shadow =
      std::vector<ColorPixel>{contaminated_shadow_source};
  photo_editor::pipeline::apply_color_adjustments(
      &contaminated_shadow, 1, 1, shadow_recovery_state);
  const auto relative_spread = [](const ColorPixel& pixel) {
    const auto maximum =
        std::max({pixel.red, pixel.green, pixel.blue});
    const auto minimum =
        std::min({pixel.red, pixel.green, pixel.blue});
    const auto mean =
        (static_cast<double>(pixel.red) + pixel.green + pixel.blue) /
        3.0;
    return static_cast<double>(maximum - minimum) /
           std::max(1.0e-9, mean);
  };
  if (relative_spread(contaminated_shadow.front()) >
      relative_spread(contaminated_shadow_source) + 1.0e-6) {
    return fail(
        "shadow-contamination-stability",
        "lifting a near-neutral sensor shadow amplified false chroma");
  }

  const auto colored_shadow_source =
      ColorPixel{.red = 0.18F, .green = 0.04F, .blue = 0.01F};
  auto colored_shadow = std::vector<ColorPixel>{colored_shadow_source};
  photo_editor::pipeline::apply_color_adjustments(
      &colored_shadow, 1, 1, shadow_recovery_state);
  if (std::abs(
          static_cast<double>(colored_shadow.front().red) /
              colored_shadow.front().green -
          4.5) >
          4.0e-6 ||
      std::abs(
          static_cast<double>(colored_shadow.front().green) /
              colored_shadow.front().blue -
          4.0) >
          4.0e-6) {
    return fail(
        "shadow-colored-subject-hue",
        "shadow recovery neutralized a deliberately colored low-light subject");
  }

  struct ToneControlProbe final {
    std::string_view name;
    double ColorAdjustmentState::*member = nullptr;
  };
  constexpr std::array<ToneControlProbe, 6> kToneControls = {
      ToneControlProbe{"contrast", &ColorAdjustmentState::contrast},
      ToneControlProbe{"shadows", &ColorAdjustmentState::shadows},
      ToneControlProbe{"highlights", &ColorAdjustmentState::highlights},
      ToneControlProbe{"blacks", &ColorAdjustmentState::blacks},
      ToneControlProbe{"whites", &ColorAdjustmentState::whites},
      ToneControlProbe{"brightness", &ColorAdjustmentState::brightness},
  };
  for (const auto& control : kToneControls) {
    for (const auto amount : {-100.0, 100.0}) {
      auto state = modern_scene_adjustment_state(0.0);
      state.*(control.member) = amount;
      std::vector<ColorPixel> ramp;
      ramp.reserve(101);
      for (int index = 0; index <= 100; ++index) {
        const auto stops =
            -8.0 + static_cast<double>(index) * 0.125;
        const auto value =
            static_cast<float>(0.18 * std::pow(2.0, stops));
        ramp.push_back(ColorPixel{
            .red = value,
            .green = value,
            .blue = value,
        });
      }
      photo_editor::pipeline::apply_color_adjustments(
          &ramp,
          static_cast<int>(ramp.size()),
          1,
          state);
      auto previous_slope = std::numeric_limits<double>::quiet_NaN();
      for (std::size_t index = 1; index < ramp.size(); ++index) {
        if (!finite_pixel(ramp[index]) ||
            !(ramp[index - 1].red < ramp[index].red)) {
          std::cerr << "tone monotonic failure control=" << control.name
                    << " amount=" << amount
                    << " index=" << index
                    << " previous=" << ramp[index - 1].red
                    << " current=" << ramp[index].red << '\n';
          return fail(
              "aces-monotone-log-tone",
              control.name);
        }
        const auto previous_stops = std::log2(
            static_cast<double>(ramp[index - 1].red) / 0.18);
        const auto current_stops = std::log2(
            static_cast<double>(ramp[index].red) / 0.18);
        const auto slope =
            (current_stops - previous_stops) / 0.125;
        if (std::isfinite(previous_slope) &&
            std::abs(slope - previous_slope) > 0.20) {
          std::cerr << "tone derivative jump control=" << control.name
                    << " amount=" << amount
                    << " index=" << index
                    << " previousSlope=" << previous_slope
                    << " slope=" << slope << '\n';
          return fail(
              "aces-tone-derivative-continuity",
              control.name);
        }
        previous_slope = slope;
      }
    }
  }

  const auto require_local_influence =
      [&](std::string_view name,
          double ColorAdjustmentState::*member,
          double target_stops,
          double remote_stops) {
        auto state = modern_scene_adjustment_state(0.0);
        state.*member = 100.0;
        const auto target =
            static_cast<float>(
                0.18 * std::pow(2.0, target_stops));
        const auto remote =
            static_cast<float>(
                0.18 * std::pow(2.0, remote_stops));
        std::vector<ColorPixel> pixels{
            ColorPixel{.red = target, .green = target, .blue = target},
            ColorPixel{.red = remote, .green = remote, .blue = remote},
        };
        photo_editor::pipeline::apply_color_adjustments(
            &pixels, 2, 1, state);
        const auto target_delta =
            std::abs(std::log2(
                static_cast<double>(pixels[0].red) /
                static_cast<double>(target)));
        const auto remote_delta =
            std::abs(std::log2(
                static_cast<double>(pixels[1].red) /
                static_cast<double>(remote)));
        if (!(target_delta > 0.25 &&
              target_delta > remote_delta + 0.20)) {
          return fail("aces-tone-influence-band", name);
        }
        return EXIT_SUCCESS;
      };
  if (require_local_influence(
          "shadows",
          &ColorAdjustmentState::shadows,
          -1.85,
          4.5) != EXIT_SUCCESS ||
      require_local_influence(
          "highlights",
          &ColorAdjustmentState::highlights,
          1.95,
          -4.5) != EXIT_SUCCESS ||
      require_local_influence(
          "blacks",
          &ColorAdjustmentState::blacks,
          -4.85,
          1.5) != EXIT_SUCCESS ||
      require_local_influence(
          "whites",
          &ColorAdjustmentState::whites,
          4.85,
          -1.5) != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }

  const auto apply_deep_stack =
      [](int edit_count) {
        std::vector<ColorPixel> pixels{
            ColorPixel{
                .red = 0.015F,
                .green = 0.022F,
                .blue = 0.035F,
                .alpha = 0.35F,
            },
            ColorPixel{
                .red = 0.42F,
                .green = 0.31F,
                .blue = 0.18F,
                .alpha = 0.75F,
            },
            ColorPixel{
                .red = 2.4F,
                .green = 1.6F,
                .blue = 0.8F,
                .alpha = 1.0F,
            },
        };
        for (int index = 0; index < edit_count; ++index) {
          auto state = modern_scene_adjustment_state(
              static_cast<double>((index % 5) - 2) * 0.01);
          state.contrast =
              static_cast<double>((index % 7) - 3) * 0.35;
          state.highlights =
              static_cast<double>((index % 9) - 4) * 0.22;
          state.shadows =
              static_cast<double>((index % 11) - 5) * 0.18;
          state.saturation =
              static_cast<double>((index % 13) - 6) * 0.12;
          photo_editor::pipeline::apply_color_adjustments(
              &pixels,
              static_cast<int>(pixels.size()),
              1,
              state);
        }
        return pixels;
      };
  for (const auto edit_count : {10, 50, 100}) {
    const auto first = apply_deep_stack(edit_count);
    const auto repeated = apply_deep_stack(edit_count);
    const auto identical =
        std::ranges::equal(
            first,
            repeated,
            [](const auto& left, const auto& right) {
              return left.red == right.red &&
                     left.green == right.green &&
                     left.blue == right.blue &&
                     left.alpha == right.alpha;
            });
    if (!identical ||
        std::ranges::any_of(
            first,
            [](const auto& pixel) {
              return !finite_pixel(pixel) ||
                     pixel.red < 0.0F ||
                     pixel.green < 0.0F ||
                     pixel.blue < 0.0F;
            })) {
      return fail(
          "deep-edit-stack-stability",
          "10, 50, and 100 edit stacks must remain finite and deterministic");
    }
  }

  const auto chroma_span = [](const ColorPixel& pixel) {
    return static_cast<double>(
        std::max({pixel.red, pixel.green, pixel.blue}) -
        std::min({pixel.red, pixel.green, pixel.blue}));
  };
  const auto skin_like_source =
      ColorPixel{.red = 0.42F, .green = 0.24F, .blue = 0.16F};
  auto generic_vibrance = std::vector<ColorPixel>{skin_like_source};
  auto generic_vibrance_state = modern_scene_adjustment_state(0.0);
  generic_vibrance_state.vibrance = 100.0;
  photo_editor::pipeline::apply_color_adjustments(
      &generic_vibrance, 1, 1, generic_vibrance_state);
  auto protected_vibrance = std::vector<ColorPixel>{skin_like_source};
  auto protected_vibrance_state = generic_vibrance_state;
  protected_vibrance_state.skin_tone_protection_enabled = true;
  protected_vibrance_state.skin_tone_line_hue = 0.08;
  protected_vibrance_state.skin_tone_protection_range = 100.0;
  protected_vibrance_state.skin_tone_protection_strength = 100.0;
  photo_editor::pipeline::apply_color_adjustments(
      &protected_vibrance, 1, 1, protected_vibrance_state);
  const auto generic_chroma_gain =
      chroma_span(generic_vibrance.front()) -
      chroma_span(skin_like_source);
  const auto protected_chroma_gain =
      chroma_span(protected_vibrance.front()) -
      chroma_span(skin_like_source);
  if (!(generic_chroma_gain > 0.01 &&
        protected_chroma_gain < generic_chroma_gain * 0.80)) {
    std::cerr << "generic_chroma_gain=" << generic_chroma_gain
              << " protected_chroma_gain=" << protected_chroma_gain
              << '\n';
    return fail(
        "explicit-skin-protection-only",
        "generic vibrance should use the full declared range and only the explicit protection control may suppress it");
  }

  // Compile the exact reversible output subchain for built-in targets. Each
  // sample is constructed inside the destination gamut, run through the real
  // forward facade, optionally display-encoded, then recovered to working RGB.
  struct InverseProfileCase final {
    const char* output_profile;
    std::array<double, 3> target_rgb;
  };
  for (const auto& profile_case : std::array<InverseProfileCase, 4>{
           InverseProfileCase{"sRGB", {0.12, 0.37, 0.68}},
           InverseProfileCase{"Display P3", {0.72, 0.16, 0.41}},
           InverseProfileCase{"Rec.2020", {0.08, 0.62, 0.31}},
           InverseProfileCase{"ProPhoto RGB", {0.83, 0.21, 0.09}},
       }) {
    auto config = photo_editor::pipeline::default_color_pipeline_config();
    config.working_profile =
        photo_editor::pipeline::working_color_profile_from_name(
            "Linear ProPhoto RGB");
    config.preview_output_profile =
        photo_editor::pipeline::output_color_profile_from_name(
            profile_case.output_profile);
    config.preview_display_target =
        photo_editor::pipeline::make_sdr_display_target(
            config.preview_output_profile);
    config.render_intent =
        photo_editor::pipeline::RenderIntent::RelativeColorimetric;
    config.black_point_compensation_enabled = false;
    const auto target_to_working =
        photo_editor::pipeline::compile_builtin_color_transform(
            config.preview_output_profile.color_space,
            config.working_profile.color_space);
    const auto working_rgb = photo_editor::pipeline::multiply_matrix3_vector(
        target_to_working.matrix,
        profile_case.target_rgb);
    const auto source = ColorPixel{
        .red = static_cast<float>(working_rgb[0]),
        .green = static_cast<float>(working_rgb[1]),
        .blue = static_cast<float>(working_rgb[2]),
        .alpha = 0.63F,
    };
    const auto forward = photo_editor::pipeline::apply_output_transform(
        source,
        config,
        photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    const auto inverse =
        photo_editor::pipeline::compile_output_inverse_transform(
            config,
            photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    const auto provenance =
        photo_editor::pipeline::output_sample_provenance(source, forward);
    const auto linear_round_trip =
        photo_editor::pipeline::apply_output_inverse_transform(
            inverse,
            forward.pixel,
            photo_editor::pipeline::OutputSampleEncoding::LinearDisplay,
            provenance);
    auto encoded_pixel = forward.pixel;
    encoded_pixel.red = static_cast<float>(
        photo_editor::pipeline::encode_display_eotf(
            forward.pixel.red,
            config.preview_display_target));
    encoded_pixel.green = static_cast<float>(
        photo_editor::pipeline::encode_display_eotf(
            forward.pixel.green,
            config.preview_display_target));
    encoded_pixel.blue = static_cast<float>(
        photo_editor::pipeline::encode_display_eotf(
            forward.pixel.blue,
            config.preview_display_target));
    const auto encoded_round_trip =
        photo_editor::pipeline::apply_output_inverse_transform(
            inverse,
            encoded_pixel,
            photo_editor::pipeline::OutputSampleEncoding::DisplayEncoded,
            provenance);
    const std::array<double, 3> expected = {
        source.red,
        source.green,
        source.blue,
    };
    const std::array<double, 3> linear_actual = {
        linear_round_trip.pixel.red,
        linear_round_trip.pixel.green,
        linear_round_trip.pixel.blue,
    };
    const std::array<double, 3> encoded_actual = {
        encoded_round_trip.pixel.red,
        encoded_round_trip.pixel.green,
        encoded_round_trip.pixel.blue,
    };
    if (!inverse.ready() ||
        inverse.provenance.find("inverse-output-matrix:v1") != 0U ||
        !inverse.requires_unmapped_sample ||
        !inverse.supports_display_encoded_samples ||
        forward.source_out_of_gamut || !linear_round_trip.exact ||
        !encoded_round_trip.exact ||
        linear_round_trip.status !=
            photo_editor::pipeline::OutputInverseStatus::Ready ||
        encoded_round_trip.status !=
            photo_editor::pipeline::OutputInverseStatus::Ready ||
        maximum_channel_error(expected, linear_actual) > 2.0e-6 ||
        maximum_channel_error(expected, encoded_actual) > 2.0e-6 ||
        std::abs(linear_round_trip.pixel.alpha - source.alpha) > 1.0e-7F ||
        std::abs(encoded_round_trip.pixel.alpha - source.alpha) > 1.0e-7F) {
      return fail(
          "output-inverse-round-trip",
          "a built-in linear or display-encoded target failed exact recovery");
    }
  }

  // Explicit HDR EOTFs participate in the same typed inverse contract.
  for (const auto eotf : {
           photo_editor::pipeline::DisplayEotf::Pq,
           photo_editor::pipeline::DisplayEotf::Hlg,
       }) {
    auto config = photo_editor::pipeline::default_color_pipeline_config();
    config.working_profile =
        photo_editor::pipeline::working_color_profile_from_name(
            "Linear Rec.2020");
    config.export_output_profile =
        photo_editor::pipeline::output_color_profile_from_name("Rec.2020");
    config.export_display_target = photo_editor::pipeline::DisplayTarget{
        .target_name = eotf == photo_editor::pipeline::DisplayEotf::Pq
                           ? "Rec.2100 PQ inverse property"
                           : "Rec.2100 HLG inverse property",
        .color_space = photo_editor::pipeline::make_linear_rec2020_color_space(),
        .eotf = eotf,
        .peak_luminance_nits = 1000.0,
        .black_luminance_nits = 0.0005,
        .reference_white_nits = 203.0,
        .surround_luminance_nits = 5.0,
        .surround = photo_editor::pipeline::DisplaySurround::Dim,
        .mastering_mode =
            photo_editor::pipeline::DisplayMasteringMode::HdrDisplayReferred,
    };
    config.render_intent =
        photo_editor::pipeline::RenderIntent::RelativeColorimetric;
    config.black_point_compensation_enabled = false;
    config.output_headroom =
        photo_editor::pipeline::display_target_linear_headroom(
            config.export_display_target);
    const auto source = ColorPixel{1.75F, 0.42F, 0.08F, 0.47F};
    const auto forward = photo_editor::pipeline::apply_output_transform(
        source,
        config,
        photo_editor::pipeline::GamutWarningTarget::ExportOutput);
    auto encoded = forward.pixel;
    for (auto* channel : {&encoded.red, &encoded.green, &encoded.blue}) {
      *channel = static_cast<float>(photo_editor::pipeline::encode_display_eotf(
          *channel,
          config.export_display_target));
    }
    const auto inverse =
        photo_editor::pipeline::compile_output_inverse_transform(
            config,
            photo_editor::pipeline::GamutWarningTarget::ExportOutput);
    const auto round_trip =
        photo_editor::pipeline::apply_output_inverse_transform(
            inverse,
            encoded,
            photo_editor::pipeline::OutputSampleEncoding::DisplayEncoded,
            photo_editor::pipeline::output_sample_provenance(source, forward));
    if (!round_trip.exact ||
        std::abs(round_trip.pixel.red - source.red) > 2.0e-5F ||
        std::abs(round_trip.pixel.green - source.green) > 2.0e-5F ||
        std::abs(round_trip.pixel.blue - source.blue) > 2.0e-5F ||
        std::abs(round_trip.pixel.alpha - source.alpha) > 1.0e-7F) {
      std::cerr << "HDR inverse eotf="
                << photo_editor::pipeline::display_eotf_name(eotf)
                << " status="
                << photo_editor::pipeline::output_inverse_status_name(
                       round_trip.status)
                << " expected=" << source.red << ',' << source.green << ','
                << source.blue << " actual=" << round_trip.pixel.red << ','
                << round_trip.pixel.green << ',' << round_trip.pixel.blue
                << '\n';
      return fail(
          "output-inverse-hdr-eotf",
          "PQ or HLG display encoding failed exact scene-linear recovery");
    }
  }

  // Non-invertible global operations and destructive per-sample history must
  // fail with stable, machine-readable reasons instead of returning guesses.
  auto rejection_config = photo_editor::pipeline::default_color_pipeline_config();
  rejection_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::
          LegacyOklchV1;
  rejection_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  rejection_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  rejection_config.preview_display_target =
      photo_editor::pipeline::make_sdr_display_target(
          rejection_config.preview_output_profile);
  rejection_config.black_point_compensation_enabled = false;
  const auto ready_inverse =
      photo_editor::pipeline::compile_output_inverse_transform(
          rejection_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  const auto clipped_source = ColorPixel{-0.25F, 0.4F, 1.4F, 0.8F};
  const auto clipped_forward = photo_editor::pipeline::apply_output_transform(
      clipped_source,
      rejection_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  const auto clipped_inverse =
      photo_editor::pipeline::apply_output_inverse_transform(
          ready_inverse,
          clipped_forward.pixel,
          photo_editor::pipeline::OutputSampleEncoding::LinearDisplay,
          photo_editor::pipeline::output_sample_provenance(
              clipped_source,
              clipped_forward));
  const auto alpha_source = ColorPixel{0.2F, 0.3F, 0.4F, 1.2F};
  const auto alpha_forward = photo_editor::pipeline::apply_output_transform(
      alpha_source,
      rejection_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  const auto alpha_inverse = photo_editor::pipeline::apply_output_inverse_transform(
      ready_inverse,
      alpha_forward.pixel,
      photo_editor::pipeline::OutputSampleEncoding::LinearDisplay,
      photo_editor::pipeline::output_sample_provenance(alpha_source, alpha_forward));
  auto mismatch_provenance =
      photo_editor::pipeline::output_sample_provenance(alpha_source, alpha_forward);
  mismatch_provenance.alpha_was_clamped = false;
  mismatch_provenance.output_profile_name = "Display P3";
  const auto mismatch_inverse =
      photo_editor::pipeline::apply_output_inverse_transform(
          ready_inverse,
          alpha_forward.pixel,
          photo_editor::pipeline::OutputSampleEncoding::LinearDisplay,
          mismatch_provenance);
  auto non_finite_pixel = alpha_forward.pixel;
  non_finite_pixel.red = std::numeric_limits<float>::quiet_NaN();
  auto finite_provenance = mismatch_provenance;
  finite_provenance.output_profile_name = "sRGB";
  const auto non_finite_inverse =
      photo_editor::pipeline::apply_output_inverse_transform(
          ready_inverse,
          non_finite_pixel,
          photo_editor::pipeline::OutputSampleEncoding::LinearDisplay,
          finite_provenance);
  auto invalid_encoded = alpha_forward.pixel;
  invalid_encoded.red = -0.01F;
  const auto invalid_encoded_inverse =
      photo_editor::pipeline::apply_output_inverse_transform(
          ready_inverse,
          invalid_encoded,
          photo_editor::pipeline::OutputSampleEncoding::DisplayEncoded,
          finite_provenance);
  if (clipped_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::
              NonInvertibleClippedOrMappedSample ||
      alpha_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::NonInvertibleAlphaClamp ||
      mismatch_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::OutputProfileMismatch ||
      non_finite_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::NonFiniteSample ||
      invalid_encoded_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::
              EncodedSignalOutsideInvertibleDomain) {
    return fail(
        "output-inverse-rejection",
        "destructive sample history did not fail with its exact reason");
  }

  auto noninvertible_config = rejection_config;
  noninvertible_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1;
  const auto photographic_inverse =
      photo_editor::pipeline::compile_output_inverse_transform(
          noninvertible_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  noninvertible_config = rejection_config;
  noninvertible_config.soft_proof_enabled = true;
  const auto proof_inverse =
      photo_editor::pipeline::compile_output_inverse_transform(
          noninvertible_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  noninvertible_config = rejection_config;
  noninvertible_config.preview_icc_profile_asset.status =
      photo_editor::pipeline::IccProfileAssetStatus::Ready;
  const auto custom_icc_inverse =
      photo_editor::pipeline::compile_output_inverse_transform(
          noninvertible_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  noninvertible_config = rejection_config;
  noninvertible_config.black_point_compensation_enabled = true;
  noninvertible_config.source_black_point = 0.01;
  noninvertible_config.output_black_point = 0.02;
  const auto black_point_inverse =
      photo_editor::pipeline::compile_output_inverse_transform(
          noninvertible_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  noninvertible_config = rejection_config;
  noninvertible_config.preview_display_target.target_name.clear();
  const auto invalid_target_inverse =
      photo_editor::pipeline::compile_output_inverse_transform(
          noninvertible_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (photographic_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::
              NonInvertiblePhotographicRendering ||
      proof_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::NonInvertibleSoftProof ||
      custom_icc_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::NonInvertibleCustomIcc ||
      black_point_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::
              NonInvertibleBlackPointCompensation ||
      invalid_target_inverse.status !=
          photo_editor::pipeline::OutputInverseStatus::InvalidDisplayTarget ||
      std::string(photo_editor::pipeline::output_inverse_status_name(
          photographic_inverse.status)) !=
          "non-invertible-photographic-rendering") {
    return fail(
        "output-inverse-capability",
        "a non-invertible global transform was not rejected explicitly");
  }

  const std::array<RgbColorSpace, 5> linear_spaces = {
      photo_editor::pipeline::make_linear_srgb_color_space(),
      photo_editor::pipeline::make_linear_display_p3_color_space(),
      photo_editor::pipeline::make_linear_rec2020_color_space(),
      photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
      photo_editor::pipeline::make_acescg_color_space(),
  };

  // Bradford-adapted built-in transforms must keep every neutral neutral,
  // including D50/D60/D65 crossings and extended-range scene values.
  for (const auto& source : linear_spaces) {
    for (const auto& destination : linear_spaces) {
      const auto transform = photo_editor::pipeline::compile_builtin_color_transform(
          source, destination);
      for (int step = -8; step <= 64; ++step) {
        const auto neutral = static_cast<double>(step) / 8.0;
        const auto output = photo_editor::pipeline::multiply_matrix3_vector(
            transform.matrix,
            {neutral, neutral, neutral});
        if (!std::isfinite(output[0]) || !std::isfinite(output[1]) ||
            !std::isfinite(output[2]) ||
            std::max({output[0], output[1], output[2]}) -
                    std::min({output[0], output[1], output[2]}) >
                2.0e-5) {
          return fail(
              "neutral-preservation",
              "a built-in white-point or primaries transform introduced chroma");
        }
      }
    }
  }

  // A->B->C must equal A->C. This is exercised on a signed, extended-range
  // lattice rather than on a single in-gamut swatch.
  const auto a_to_b = photo_editor::pipeline::compile_builtin_color_transform(
      photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
      photo_editor::pipeline::make_acescg_color_space());
  const auto b_to_c = photo_editor::pipeline::compile_builtin_color_transform(
      photo_editor::pipeline::make_acescg_color_space(),
      photo_editor::pipeline::make_linear_srgb_color_space());
  const auto a_to_c = photo_editor::pipeline::compile_builtin_color_transform(
      photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
      photo_editor::pipeline::make_linear_srgb_color_space());
  constexpr std::array<double, 6> kCompositionSamples = {
      -0.25, 0.0, 0.003, 0.18, 1.0, 4.0,
  };
  for (const auto red : kCompositionSamples) {
    for (const auto green : kCompositionSamples) {
      for (const auto blue : kCompositionSamples) {
        const std::array<double, 3> input = {red, green, blue};
        const auto direct = photo_editor::pipeline::multiply_matrix3_vector(
            a_to_c.matrix, input);
        const auto composed = photo_editor::pipeline::multiply_matrix3_vector(
            b_to_c.matrix,
            photo_editor::pipeline::multiply_matrix3_vector(a_to_b.matrix, input));
        if (maximum_channel_error(direct, composed) > 2.0e-12) {
          return fail(
              "transform-composition",
              "ProPhoto to ACEScg to sRGB diverged from the direct transform");
        }
      }
    }
  }

  // The cylindrical appearance model must remain continuous through the
  // 359/0-degree seam and invert each sampled hue without a discontinuity.
  const auto appearance =
      photo_editor::pipeline::make_aces2_hellwig_jmh_parameters(
          photo_editor::pipeline::make_linear_srgb_color_space());
  std::array<double, 3> first_hue_rgb{};
  std::array<double, 3> previous_hue_rgb{};
  auto maximum_hue_step = 0.0;
  constexpr auto kHueSamples = 720;
  for (int index = 0; index < kHueSamples; ++index) {
    const auto hue = 360.0 * static_cast<double>(index) /
                     static_cast<double>(kHueSamples);
    const auto rgb = photo_editor::pipeline::aces2_hellwig_jmh_to_linear_rgb(
        {.j = 55.0, .m = 20.0, .h_degrees = hue},
        appearance);
    const auto round_trip =
        photo_editor::pipeline::aces2_linear_rgb_to_hellwig_jmh(rgb, appearance);
    if (!std::isfinite(rgb[0]) || !std::isfinite(rgb[1]) ||
        !std::isfinite(rgb[2]) ||
        circular_hue_error(round_trip.h_degrees, hue) > 2.0e-8 ||
        std::abs(round_trip.j - 55.0) > 2.0e-8 ||
        std::abs(round_trip.m - 20.0) > 2.0e-8) {
      return fail("hue-continuity", "Hellwig JMh failed a hue round trip");
    }
    if (index == 0) {
      first_hue_rgb = rgb;
    } else {
      maximum_hue_step = std::max(
          maximum_hue_step,
          std::sqrt(
              std::pow(rgb[0] - previous_hue_rgb[0], 2.0) +
              std::pow(rgb[1] - previous_hue_rgb[1], 2.0) +
              std::pow(rgb[2] - previous_hue_rgb[2], 2.0)));
    }
    previous_hue_rgb = rgb;
  }
  const auto wrap_step = std::sqrt(
      std::pow(first_hue_rgb[0] - previous_hue_rgb[0], 2.0) +
      std::pow(first_hue_rgb[1] - previous_hue_rgb[1], 2.0) +
      std::pow(first_hue_rgb[2] - previous_hue_rgb[2], 2.0));
  if (!std::isfinite(maximum_hue_step) || maximum_hue_step <= 0.0 ||
      wrap_step > maximum_hue_step * 1.02 + 1.0e-12) {
    return fail("hue-continuity", "the 359/0-degree seam exceeds adjacent hue steps");
  }

  // ICC round trips exercise the production CMM wrapper when present. The
  // deterministic no-LCMS build remains valid and proves fail-closed behavior
  // separately in color_transform_reference_test.
  const auto icc_capability = photo_editor::pipeline::icc_backend_capability();
  if (icc_capability.available) {
    const auto linear_srgb_profile =
        photo_editor::pipeline::builtin_rgb_icc_profile_payload(
            photo_editor::pipeline::make_linear_srgb_color_space());
    const auto linear_p3_profile =
        photo_editor::pipeline::builtin_rgb_icc_profile_payload(
            photo_editor::pipeline::make_linear_display_p3_color_space());
    const auto forward = photo_editor::pipeline::compile_icc_transform(
        linear_srgb_profile,
        linear_p3_profile,
        IccTransformOptions{
            .rendering_intent = IccRenderingIntent::RelativeColorimetric,
            .black_point_compensation = false,
            .target = "property-round-trip-forward",
        });
    const auto reverse = photo_editor::pipeline::compile_icc_transform(
        linear_p3_profile,
        linear_srgb_profile,
        IccTransformOptions{
            .rendering_intent = IccRenderingIntent::RelativeColorimetric,
            .black_point_compensation = false,
            .target = "property-round-trip-reverse",
        });
    std::vector<IccRgbaFloat> samples;
    for (int red = 0; red <= 4; ++red) {
      for (int green = 0; green <= 4; ++green) {
        for (int blue = 0; blue <= 4; ++blue) {
          samples.push_back({
              .red = static_cast<float>(red) / 4.0F,
              .green = static_cast<float>(green) / 4.0F,
              .blue = static_cast<float>(blue) / 4.0F,
              .alpha = static_cast<float>(red + green + blue + 1) / 13.0F,
          });
        }
      }
    }
    const auto original = samples;
    if (!forward.ready() || !reverse.ready() || !forward.apply(samples) ||
        !reverse.apply(samples)) {
      return fail("profile-round-trip", "the production ICC transforms did not execute");
    }
    for (std::size_t index = 0; index < samples.size(); ++index) {
      const std::array<double, 3> expected = {
          original[index].red,
          original[index].green,
          original[index].blue,
      };
      const std::array<double, 3> actual = {
          samples[index].red,
          samples[index].green,
          samples[index].blue,
      };
      if (maximum_channel_error(expected, actual) > 2.5e-4 ||
          std::abs(samples[index].alpha - original[index].alpha) > 1.0e-7F) {
        return fail(
            "profile-round-trip",
            "linear sRGB to Display P3 to sRGB exceeded its float tolerance");
      }
    }
  }

  // Finite, signed scene values are legal between stages. A deterministic
  // stress corpus ensures the general adjustment path never emits NaN/Inf or
  // mutates unassociated alpha when it contains large excursions.
  std::vector<ColorPixel> extended_samples;
  extended_samples.reserve(1024);
  auto state = std::uint32_t{0x6d2b79f5U};
  const auto next_sample = [&state]() {
    state = state * 1664525U + 1013904223U;
    return -32.0F + 96.0F *
                        static_cast<float>(state >> 8U) /
                        static_cast<float>(0x00ffffffU);
  };
  for (std::size_t index = 0; index < 1024; ++index) {
    extended_samples.push_back({
        .red = next_sample(),
        .green = next_sample(),
        .blue = next_sample(),
        .alpha = static_cast<float>(index % 17U) / 16.0F,
    });
  }
  const auto extended_alpha = extended_samples;
  auto extended_state = linear_adjustment_state(3.5);
  extended_state.input_transform.temperature = 65.0;
  extended_state.input_transform.tint = -45.0;
  extended_state.contrast = 35.0;
  extended_state.highlights = -40.0;
  extended_state.shadows = 30.0;
  extended_state.saturation = 20.0;
  photo_editor::pipeline::apply_color_adjustments(
      &extended_samples,
      32,
      32,
      extended_state);
  for (std::size_t index = 0; index < extended_samples.size(); ++index) {
    if (!finite_pixel(extended_samples[index]) ||
        std::abs(extended_samples[index].alpha - extended_alpha[index].alpha) > 1.0e-7F) {
      return fail(
          "finite-extended-range",
          "a signed scene-linear stress sample became non-finite or changed alpha");
    }
  }

  auto prefix_stage_state =
      modern_scene_adjustment_state(1.0);
  prefix_stage_state.input_transform.raw_input_transform_enabled =
      true;
  prefix_stage_state.input_transform.raw_black_level = 0.02;
  prefix_stage_state.input_transform.raw_white_level = 1.25;
  prefix_stage_state.color_pipeline.input_transform_recipe
      .baseline_exposure = 0.35;
  auto prefix_stage_source = std::vector<ColorPixel>{
      {.red = 0.03F, .green = 0.04F, .blue = 0.05F, .alpha = 0.8F},
      {.red = 0.18F, .green = 0.12F, .blue = 0.08F, .alpha = 0.9F},
      {.red = 0.75F, .green = 0.52F, .blue = 0.30F, .alpha = 1.0F},
      {.red = 1.10F, .green = 0.88F, .blue = 0.64F, .alpha = 0.7F},
  };
  auto canonical_prefix = prefix_stage_source;
  photo_editor::pipeline::
      apply_color_adjustments_through_look_base(
          &canonical_prefix,
          4,
          1,
          prefix_stage_state,
          photo_editor::pipeline::RemovalQuality::Final);
  auto split_prefix = prefix_stage_source;
  auto prefix_context =
      photo_editor::pipeline::
          prepare_color_prefix_stage_context(
              split_prefix,
              4,
              1,
              prefix_stage_state);
  photo_editor::pipeline::ColorAdjustmentTelemetry
      prefix_telemetry;
  if (!photo_editor::pipeline::
          apply_color_normalize_raw_stage(
              &split_prefix,
              4,
              1,
              prefix_stage_state,
              &prefix_context,
              {},
              &prefix_telemetry) ||
      !photo_editor::pipeline::
          apply_color_camera_characterization_stage(
              &split_prefix,
              4,
              1,
              prefix_stage_state,
              prefix_context,
              {},
              &prefix_telemetry) ||
      !photo_editor::pipeline::
          apply_color_scene_adjustments_stage(
              &split_prefix,
              4,
              1,
              prefix_stage_state,
              {},
              &prefix_telemetry)) {
    return fail(
        "input-prefix-stage-execution",
        "a valid NormalizeRaw, CameraCharacterization, or SceneAdjustments surface was rejected");
  }
  for (std::size_t index = 0;
       index < split_prefix.size();
       ++index) {
    if (maximum_pixel_channel_error(
            split_prefix[index],
            canonical_prefix[index]) >
        2.0e-6) {
      return fail(
          "input-prefix-stage-boundary",
          "split input stages differ from the canonical neutral through-Look result");
    }
  }

  auto identity_look_state =
      modern_scene_adjustment_state(0.0);
  auto identity_curve_pixels = prefix_stage_source;
  const auto identity_curve_reference = identity_curve_pixels;
  photo_editor::pipeline::ColorAdjustmentTelemetry identity_curve_telemetry;
  if (!photo_editor::pipeline::apply_color_curves_stage(
          &identity_curve_pixels,
          4,
          1,
          identity_look_state,
          {},
          &identity_curve_telemetry) ||
      identity_curve_telemetry.curves_ms != 0) {
    return fail(
        "identity-curve-stage-admission",
        "a recipe without curves did not take the exact identity path");
  }
  for (std::size_t index = 0; index < identity_curve_pixels.size(); ++index) {
    if (maximum_pixel_channel_error(
            identity_curve_pixels[index], identity_curve_reference[index]) != 0.0) {
      return fail(
          "identity-curve-stage-pixels",
          "an identity Curves stage changed working pixels");
    }
  }
  identity_curve_pixels.front().red =
      std::numeric_limits<float>::quiet_NaN();
  if (photo_editor::pipeline::apply_color_curves_stage(
          &identity_curve_pixels, 4, 1, identity_look_state)) {
    return fail(
        "identity-curve-stage-finite-contract",
        "the identity Curves path accepted a non-finite working surface");
  }
  auto identity_tone_pixels = std::vector<ColorPixel>{
      {.red = -4.0F, .green = 0.25F, .blue = 12.0F, .alpha = 0.25F},
      {.red = 8191.0F, .green = -0.5F, .blue = 1.5F, .alpha = 0.5F},
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.75F},
      {.red = 1.0e-8F, .green = -1.0e-8F, .blue = 64.0F, .alpha = 1.0F},
  };
  const auto identity_tone_reference = identity_tone_pixels;
  photo_editor::pipeline::ColorAdjustmentTelemetry identity_tone_telemetry;
  if (!photo_editor::pipeline::apply_color_tone_stage(
          &identity_tone_pixels,
          4,
          1,
          identity_look_state,
          {},
          &identity_tone_telemetry) ||
      identity_tone_telemetry.tone_ms != 0 ||
      identity_tone_telemetry.tone_mask_ms != 0) {
    return fail(
        "identity-tone-stage-admission",
        "a neutral modern-ACES Tone stage did not take the exact identity path");
  }
  for (std::size_t index = 0; index < identity_tone_pixels.size(); ++index) {
    if (maximum_pixel_channel_error(
            identity_tone_pixels[index], identity_tone_reference[index]) != 0.0) {
      return fail(
          "identity-tone-stage-pixels",
          "a neutral modern-ACES Tone stage changed signed or extended working pixels");
    }
  }
  identity_tone_pixels.front().green =
      std::numeric_limits<float>::infinity();
  if (photo_editor::pipeline::apply_color_tone_stage(
          &identity_tone_pixels, 4, 1, identity_look_state)) {
    return fail(
        "identity-tone-stage-finite-contract",
        "the identity Tone path accepted a non-finite working surface");
  }
  auto identity_look_pixels = prefix_stage_source;
  const auto identity_look_reference = identity_look_pixels;
  photo_editor::pipeline::ColorAdjustmentTelemetry identity_look_telemetry;
  if (!photo_editor::pipeline::apply_color_look_stage(
          &identity_look_pixels,
          4,
          1,
          identity_look_state,
          {},
          &identity_look_telemetry) ||
      identity_look_telemetry.look_ms != 0) {
    return fail(
        "identity-look-stage-admission",
        "an ACES look stage without active controls did not take its identity path");
  }
  for (std::size_t index = 0; index < identity_look_pixels.size(); ++index) {
    if (maximum_pixel_channel_error(
            identity_look_pixels[index], identity_look_reference[index]) != 0.0) {
      return fail(
          "identity-look-stage-pixels",
          "an ACES identity Look stage changed working pixels");
    }
  }
  identity_look_pixels.front().red =
      std::numeric_limits<float>::quiet_NaN();
  if (photo_editor::pipeline::apply_color_look_stage(
          &identity_look_pixels, 4, 1, identity_look_state)) {
    return fail(
        "identity-look-stage-finite-contract",
        "the identity Look path accepted a non-finite working surface");
  }
  auto full_prefix_state = prefix_stage_state;
  full_prefix_state.contrast = 18.0;
  full_prefix_state.highlights = -22.0;
  full_prefix_state.shadows = 16.0;
  full_prefix_state.curve_shadows = 8.0;
  full_prefix_state.curve_lights = -6.0;
  full_prefix_state.saturation = 12.0;
  full_prefix_state.vibrance = 18.0;
  auto canonical_full_prefix = prefix_stage_source;
  photo_editor::pipeline::
      apply_color_adjustments_through_look_base(
          &canonical_full_prefix,
          4,
          1,
          full_prefix_state,
          photo_editor::pipeline::RemovalQuality::Final);
  auto split_full_prefix = prefix_stage_source;
  auto full_prefix_context =
      photo_editor::pipeline::
          prepare_color_prefix_stage_context(
              split_full_prefix,
              4,
              1,
              full_prefix_state);
  if (!photo_editor::pipeline::
          apply_color_normalize_raw_stage(
              &split_full_prefix,
              4,
              1,
              full_prefix_state,
              &full_prefix_context) ||
      !photo_editor::pipeline::
          apply_color_camera_characterization_stage(
              &split_full_prefix,
              4,
              1,
              full_prefix_state,
              full_prefix_context) ||
      !photo_editor::pipeline::
          apply_color_scene_adjustments_stage(
              &split_full_prefix,
              4,
              1,
              full_prefix_state) ||
      !photo_editor::pipeline::apply_color_tone_stage(
          &split_full_prefix,
          4,
          1,
          full_prefix_state) ||
      !photo_editor::pipeline::apply_color_curves_stage(
          &split_full_prefix,
          4,
          1,
          full_prefix_state) ||
      !photo_editor::pipeline::apply_color_look_stage(
          &split_full_prefix,
          4,
          1,
          full_prefix_state)) {
    return fail(
        "full-prefix-stage-execution",
        "a valid typed color-prefix stage rejected its input");
  }
  for (std::size_t index = 0;
       index < split_full_prefix.size();
       ++index) {
    if (maximum_pixel_channel_error(
            split_full_prefix[index],
            canonical_full_prefix[index]) >
        3.0e-5) {
      return fail(
          "full-prefix-stage-boundary",
          "split NormalizeRaw through Look differs from the canonical fused prefix");
    }
  }

  // Detail is a working-space spatial stage, not an output gamut boundary.
  // A strong chromatic edge legitimately produces signed sharpening
  // excursions. They must remain available to later stages rather than being
  // gamut-fitted back into a bounded RGB cube here.
  auto detail_boundary_state =
      modern_scene_adjustment_state(0.0);
  detail_boundary_state.texture = 24.0;
  detail_boundary_state.sharpening = 32.0;
  photo_editor::pipeline::LocalMask
      detail_boundary_mask;
  detail_boundary_mask.kind = "brush";
  detail_boundary_mask.segmentation_width = 5;
  detail_boundary_mask.segmentation_height = 5;
  detail_boundary_mask.segmentation_mask.assign(
      25U, 0xff);
  detail_boundary_mask.exposure = 0.5;
  detail_boundary_state.local_masks = {
      detail_boundary_mask};
  detail_boundary_state.photo_layers = {
      photo_editor::pipeline::PhotoLayer{
          .id = "detail-boundary-layer",
          .name = "Detail boundary layer",
          .exposure = 0.25,
      },
  };
  auto detail_boundary_source =
      std::vector<ColorPixel>{};
  detail_boundary_source.reserve(25U);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      detail_boundary_source.push_back(
          x < 2
              ? ColorPixel{
                    .red = 0.82F,
                    .green = 0.16F,
                    .blue = 0.05F,
                    .alpha = 0.74F,
                }
              : ColorPixel{
                    .red = 0.03F,
                    .green = 0.28F,
                    .blue = 0.91F,
                    .alpha = 0.74F,
                });
    }
  }
  auto through_layers = detail_boundary_source;
  photo_editor::pipeline::ColorAdjustmentTelemetry
      layers_telemetry;
  photo_editor::pipeline::
      apply_color_adjustments_through_layers(
          &through_layers,
          5,
          5,
          detail_boundary_state,
          photo_editor::pipeline::RemovalQuality::
              Final,
          {},
          &layers_telemetry);
  auto through_look_base = detail_boundary_source;
  photo_editor::pipeline::
      apply_color_adjustments_through_look_base(
          &through_look_base,
          5,
          5,
          detail_boundary_state,
          photo_editor::pipeline::RemovalQuality::
              Final);
  auto layer_free_state = detail_boundary_state;
  layer_free_state.local_masks.clear();
  layer_free_state.local_mask_sets.clear();
  layer_free_state.retouch_masks.clear();
  layer_free_state.photo_layers.clear();
  layer_free_state.linear_mask_enabled = false;
  layer_free_state.radial_mask_enabled = false;
  auto explicit_layer_free = detail_boundary_source;
  photo_editor::pipeline::
      apply_color_adjustments_through_layers(
          &explicit_layer_free,
          5,
          5,
          layer_free_state,
          photo_editor::pipeline::RemovalQuality::
              Final);
  auto look_base_delta = 0.0;
  auto omitted_layers_delta = 0.0;
  for (std::size_t index = 0;
       index < through_look_base.size();
       ++index) {
    look_base_delta = std::max(
        look_base_delta,
        maximum_pixel_channel_error(
            through_look_base[index],
            explicit_layer_free[index]));
    omitted_layers_delta = std::max(
        omitted_layers_delta,
        maximum_pixel_channel_error(
            through_look_base[index],
            through_layers[index]));
  }
  if (look_base_delta != 0.0 ||
      omitted_layers_delta <= 1.0e-5) {
    return fail(
        "look-base-boundary",
        "the base-through-Look boundary read local masks/photo layers or failed to omit real Layers work");
  }
  auto split_layers = through_look_base;
  photo_editor::pipeline::ColorAdjustmentTelemetry
      split_layers_telemetry;
  if (!photo_editor::pipeline::
          apply_color_layers_stage(
              &split_layers,
              5,
              5,
              detail_boundary_state,
              photo_editor::pipeline::
                  RemovalQuality::Final,
              {},
              &split_layers_telemetry)) {
    return fail(
        "layers-stage-execution",
        "the pixel-only Layers stage rejected a valid Look surface");
  }
  for (std::size_t index = 0;
       index < through_layers.size();
       ++index) {
    if (maximum_pixel_channel_error(
            split_layers[index],
            through_layers[index]) != 0.0) {
      return fail(
          "layers-stage-boundary",
          "split Look plus Layers execution differed from the canonical through-Layers boundary");
    }
  }
  auto through_detail = detail_boundary_source;
  photo_editor::pipeline::
      apply_color_adjustments_through_detail(
          &through_detail,
          5,
          5,
          detail_boundary_state,
          photo_editor::pipeline::RemovalQuality::
              Final);
  auto split_detail = through_layers;
  photo_editor::pipeline::ColorAdjustmentTelemetry
      split_detail_telemetry;
  if (!photo_editor::pipeline::
          apply_color_detail_stage(
              &split_detail,
              5,
              5,
              detail_boundary_state,
              photo_editor::pipeline::
                  RemovalQuality::Final,
              {},
              &split_detail_telemetry)) {
    return fail(
        "detail-stage-split",
        "the extracted Detail stage rejected a valid Layers surface");
  }
  auto detail_boundary_delta = 0.0;
  auto split_detail_delta = 0.0;
  for (std::size_t index = 0;
       index < through_layers.size();
       ++index) {
    detail_boundary_delta = std::max(
        detail_boundary_delta,
        maximum_pixel_channel_error(
            through_layers[index],
            through_detail[index]));
    split_detail_delta = std::max(
        split_detail_delta,
        maximum_pixel_channel_error(
            split_detail[index],
            through_detail[index]));
  }
  if (layers_telemetry.spatial_ms != 0 ||
      layers_telemetry.detail_ms != 0 ||
      detail_boundary_delta <= 1.0e-5) {
    return fail(
        "layers-detail-boundary",
        "the Layers boundary executed Detail prematurely or failed to leave real Detail work");
  }
  if (split_detail_delta != 0.0) {
    return fail(
        "detail-stage-split",
        "the extracted Detail stage does not reproduce the monolithic through-Detail result bit exactly");
  }

  auto detail_range_state = modern_scene_adjustment_state(0.0);
  detail_range_state.color_pipeline.output_headroom = 16.0;
  std::vector<ColorPixel> detail_range_output;
  detail_range_output.reserve(25U);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      detail_range_output.push_back(
          x < 2
              ? ColorPixel{
                    .red = 0.02F,
                    .green = 0.90F,
                    .blue = 2.00F,
                    .alpha = 0.61F,
                }
              : ColorPixel{
                    .red = 0.90F,
                    .green = 0.02F,
                    .blue = 0.10F,
                    .alpha = 0.61F,
                });
    }
  }
  detail_range_state.sharpening = 75.0;
  detail_range_state.sharpen_radius = 2.0;
  detail_range_state.sharpen_detail = 80.0;
  photo_editor::pipeline::apply_color_adjustments(
      &detail_range_output,
      5,
      5,
      detail_range_state);
  auto detail_has_signed_excursion = false;
  auto detail_preserves_over_range = false;
  for (const auto& detailed : detail_range_output) {
    detail_has_signed_excursion =
        detail_has_signed_excursion ||
        detailed.red < -1.0e-4F ||
        detailed.green < -1.0e-4F ||
        detailed.blue < -1.0e-4F;
    detail_preserves_over_range =
        detail_preserves_over_range ||
        detailed.red > 1.0F ||
        detailed.green > 1.0F ||
        detailed.blue > 1.0F;
    if (!finite_pixel(detailed) ||
        std::abs(detailed.alpha - 0.61F) > 1.0e-7F) {
      return fail(
          "detail-preserves-working-range",
          "detail produced a non-finite sample or changed alpha");
    }
  }
  if (!detail_has_signed_excursion || !detail_preserves_over_range) {
    return fail(
        "detail-preserves-working-range",
        "detail clipped signed or over-range working excursions before output");
  }

  // Geometry is a real stage after Detail, not a label on an early optical
  // helper. The combined render must match explicit detail-then-geometry
  // execution and differ from the reversed order on an asymmetric edge.
  std::vector<ColorPixel> geometry_order_source;
  geometry_order_source.reserve(49U);
  for (int y = 0; y < 7; ++y) {
    for (int x = 0; x < 7; ++x) {
      geometry_order_source.push_back(ColorPixel{
          .red = static_cast<float>(
              0.04 + 0.08 * static_cast<double>(x) +
              (x == 2 && y >= 2 ? 0.32 : 0.0)),
          .green = static_cast<float>(
              0.06 + 0.05 * static_cast<double>(y)),
          .blue = static_cast<float>(
              0.03 +
              0.02 * static_cast<double>((x + 2 * y) % 5)),
          .alpha = 0.82F,
      });
    }
  }
  auto combined_geometry_state =
      modern_scene_adjustment_state(0.0);
  combined_geometry_state.color_pipeline.output_headroom = 16.0;
  combined_geometry_state.sharpening = 58.0;
  combined_geometry_state.sharpen_radius = 1.8;
  combined_geometry_state.sharpen_detail = 64.0;
  combined_geometry_state.lens_distortion = 52.0;
  auto combined_geometry = geometry_order_source;
  photo_editor::pipeline::apply_color_adjustments(
      &combined_geometry,
      7,
      7,
      combined_geometry_state);

  auto detail_only_state = combined_geometry_state;
  detail_only_state.lens_distortion = 0.0;
  auto geometry_only_state = combined_geometry_state;
  geometry_only_state.sharpening = 0.0;
  geometry_only_state.sharpen_detail = 0.0;
  auto explicit_detail_then_geometry = geometry_order_source;
  photo_editor::pipeline::apply_color_adjustments(
      &explicit_detail_then_geometry,
      7,
      7,
      detail_only_state);
  if (!photo_editor::pipeline::apply_color_geometry_stage(
      &explicit_detail_then_geometry,
      7,
      7,
      geometry_only_state)) {
    return fail(
        "geometry-stage-contract",
        "the explicit Geometry stage rejected valid working pixels");
  }
  auto reversed_geometry_then_detail = geometry_order_source;
  if (!photo_editor::pipeline::apply_color_geometry_stage(
      &reversed_geometry_then_detail,
      7,
      7,
      geometry_only_state)) {
    return fail(
        "geometry-stage-contract",
        "the explicit Geometry stage rejected the reverse-order fixture");
  }
  photo_editor::pipeline::apply_color_adjustments(
      &reversed_geometry_then_detail,
      7,
      7,
      detail_only_state);
  auto combined_to_declared_order_error = 0.0;
  auto combined_to_reversed_order_error = 0.0;
  for (std::size_t index = 0;
       index < combined_geometry.size();
       ++index) {
    const auto channel_error =
        [](const ColorPixel& left, const ColorPixel& right) {
          return std::max({
              std::abs(
                  static_cast<double>(left.red) -
                  static_cast<double>(right.red)),
              std::abs(
                  static_cast<double>(left.green) -
                  static_cast<double>(right.green)),
              std::abs(
                  static_cast<double>(left.blue) -
                  static_cast<double>(right.blue)),
              std::abs(
                  static_cast<double>(left.alpha) -
                  static_cast<double>(right.alpha)),
          });
        };
    combined_to_declared_order_error = std::max(
        combined_to_declared_order_error,
        channel_error(
            combined_geometry[index],
            explicit_detail_then_geometry[index]));
    combined_to_reversed_order_error = std::max(
        combined_to_reversed_order_error,
        channel_error(
            combined_geometry[index],
            reversed_geometry_then_detail[index]));
  }
  if (combined_to_declared_order_error > 2.0e-6 ||
      combined_to_reversed_order_error < 1.0e-5) {
    return fail(
        "detail-before-geometry-stage-order",
        "combined execution did not match Detail -> Geometry or could not distinguish the reversed order");
  }

  auto executor_suffix_surface = geometry_order_source;
  photo_editor::pipeline::ColorAdjustmentTelemetry
      executor_suffix_telemetry;
  photo_editor::pipeline::apply_color_adjustments_through_detail(
      &executor_suffix_surface,
      7,
      7,
      combined_geometry_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      {},
      &executor_suffix_telemetry);
  if (executor_suffix_telemetry.geometry_ms != 0 ||
      executor_suffix_telemetry.final_output_ms != 0 ||
      !photo_editor::pipeline::apply_color_geometry_stage(
          &executor_suffix_surface,
          7,
          7,
          combined_geometry_state,
          {},
          &executor_suffix_telemetry)) {
    return fail(
        "detail-surface-executor-suffix-contract",
        "the Detail boundary did not defer Geometry to its callable stage");
  }
  auto executor_suffix_error = 0.0;
  for (std::size_t index = 0;
       index < combined_geometry.size();
       ++index) {
    executor_suffix_error = std::max(
        executor_suffix_error,
        std::max({
            std::abs(
                static_cast<double>(
                    executor_suffix_surface[index].red) -
                static_cast<double>(
                    combined_geometry[index].red)),
            std::abs(
                static_cast<double>(
                    executor_suffix_surface[index].green) -
                static_cast<double>(
                    combined_geometry[index].green)),
            std::abs(
                static_cast<double>(
                    executor_suffix_surface[index].blue) -
                static_cast<double>(
                    combined_geometry[index].blue)),
            std::abs(
                static_cast<double>(
                    executor_suffix_surface[index].alpha) -
                static_cast<double>(
                    combined_geometry[index].alpha)),
        }));
  }
  if (executor_suffix_error != 0.0 ||
      executor_suffix_telemetry.geometry_ms < 0 ||
      executor_suffix_telemetry.final_output_ms != 0) {
    return fail(
        "detail-surface-executor-suffix-parity",
        "Detail plus explicit Geometry was not bit-identical to complete canonical ACES scene execution");
  }

  auto look_stage_state = modern_scene_adjustment_state(0.0);
  look_stage_state.color_pipeline.output_headroom = 16.0;
  look_stage_state.vibrance = 8.0;
  auto layer_stage_state = modern_scene_adjustment_state(0.0);
  layer_stage_state.color_pipeline.output_headroom = 16.0;
  photo_editor::pipeline::PhotoLayer stage_order_layer;
  stage_order_layer.kind = "adjustment";
  stage_order_layer.blend_mode = "normal";
  stage_order_layer.exposure = 0.12;
  layer_stage_state.photo_layers.push_back(stage_order_layer);
  auto detail_stage_state = modern_scene_adjustment_state(0.0);
  detail_stage_state.color_pipeline.output_headroom = 16.0;
  detail_stage_state.texture = 14.0;
  detail_stage_state.clarity = 11.0;
  auto combined_stack_state = look_stage_state;
  combined_stack_state.photo_layers =
      layer_stage_state.photo_layers;
  combined_stack_state.texture = detail_stage_state.texture;
  combined_stack_state.clarity = detail_stage_state.clarity;

  auto combined_stack_order = geometry_order_source;
  photo_editor::pipeline::apply_color_adjustments(
      &combined_stack_order,
      7,
      7,
      combined_stack_state);
  auto explicit_look_layers_detail = geometry_order_source;
  photo_editor::pipeline::apply_color_adjustments(
      &explicit_look_layers_detail,
      7,
      7,
      look_stage_state);
  photo_editor::pipeline::apply_color_adjustments(
      &explicit_look_layers_detail,
      7,
      7,
      layer_stage_state);
  photo_editor::pipeline::apply_color_adjustments(
      &explicit_look_layers_detail,
      7,
      7,
      detail_stage_state);
  auto reversed_detail_layers_look = geometry_order_source;
  photo_editor::pipeline::apply_color_adjustments(
      &reversed_detail_layers_look,
      7,
      7,
      detail_stage_state);
  photo_editor::pipeline::apply_color_adjustments(
      &reversed_detail_layers_look,
      7,
      7,
      layer_stage_state);
  photo_editor::pipeline::apply_color_adjustments(
      &reversed_detail_layers_look,
      7,
      7,
      look_stage_state);
  auto declared_stack_order_error = 0.0;
  auto reversed_stack_order_error = 0.0;
  for (std::size_t index = 0;
       index < combined_stack_order.size();
       ++index) {
    const auto channel_error =
        [](const ColorPixel& left, const ColorPixel& right) {
          return std::max({
              std::abs(
                  static_cast<double>(left.red) -
                  static_cast<double>(right.red)),
              std::abs(
                  static_cast<double>(left.green) -
                  static_cast<double>(right.green)),
              std::abs(
                  static_cast<double>(left.blue) -
                  static_cast<double>(right.blue)),
          });
        };
    declared_stack_order_error = std::max(
        declared_stack_order_error,
        channel_error(
            combined_stack_order[index],
            explicit_look_layers_detail[index]));
    reversed_stack_order_error = std::max(
        reversed_stack_order_error,
        channel_error(
            combined_stack_order[index],
            reversed_detail_layers_look[index]));
  }
  if (declared_stack_order_error > 2.0e-5 ||
      reversed_stack_order_error < 1.0e-5) {
    return fail(
        "look-layers-detail-stage-order",
        "combined execution did not match Look -> Layers -> Detail or could not distinguish the reversed order");
  }

  // Exact tone evaluation is compiled once per bounded tone-state key. Preview
  // and final renders of the same recipe must reuse the immutable table, while
  // a changed tone control must not observe stale compiled data.
  auto cached_tone_state =
      modern_scene_adjustment_state(0.0);
  cached_tone_state.color_pipeline.output_headroom = 16.0;
  cached_tone_state.contrast = 17.23456789;
  cached_tone_state.highlights = -23.45678901;
  cached_tone_state.zone_midtones = 8.765432109;
  const auto cached_tone_source = geometry_order_source;
  auto first_cached_tone_render = cached_tone_source;
  photo_editor::pipeline::ColorAdjustmentTelemetry
      first_cached_tone_telemetry;
  photo_editor::pipeline::apply_color_adjustments(
      &first_cached_tone_render,
      7,
      7,
      cached_tone_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      {},
      &first_cached_tone_telemetry);
  auto second_cached_tone_render = cached_tone_source;
  photo_editor::pipeline::ColorAdjustmentTelemetry
      second_cached_tone_telemetry;
  photo_editor::pipeline::apply_color_adjustments(
      &second_cached_tone_render,
      7,
      7,
      cached_tone_state,
      photo_editor::pipeline::RemovalQuality::Final,
      {},
      &second_cached_tone_telemetry);
  const auto exact_pixel_vectors_equal =
      [](std::span<const ColorPixel> left,
         std::span<const ColorPixel> right) {
        return left.size() == right.size() &&
               std::equal(
                   left.begin(),
                   left.end(),
                   right.begin(),
                   [](const ColorPixel& a, const ColorPixel& b) {
                     return a.red == b.red &&
                            a.green == b.green &&
                            a.blue == b.blue &&
                            a.alpha == b.alpha;
                   });
      };
  if (first_cached_tone_telemetry.tone_table_cache_miss_count != 1 ||
      first_cached_tone_telemetry.tone_table_cache_hit_count != 0 ||
      second_cached_tone_telemetry.tone_table_cache_hit_count != 1 ||
      second_cached_tone_telemetry.tone_table_cache_miss_count != 0 ||
      !exact_pixel_vectors_equal(
          first_cached_tone_render,
          second_cached_tone_render)) {
    return fail(
        "bounded-tone-table-cache-reuse",
        "identical preview/final tone states did not reuse one deterministic compiled table");
  }
  auto changed_tone_state = cached_tone_state;
  changed_tone_state.zone_midtones += 0.125;
  auto changed_tone_render = cached_tone_source;
  photo_editor::pipeline::ColorAdjustmentTelemetry
      changed_tone_telemetry;
  photo_editor::pipeline::apply_color_adjustments(
      &changed_tone_render,
      7,
      7,
      changed_tone_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      {},
      &changed_tone_telemetry);
  if (changed_tone_telemetry.tone_table_cache_miss_count != 1 ||
      changed_tone_telemetry.tone_table_cache_hit_count != 0 ||
      exact_pixel_vectors_equal(
          changed_tone_render,
          first_cached_tone_render)) {
    return fail(
        "bounded-tone-table-cache-invalidation",
        "a changed tone control reused stale compiled tone data");
  }

  // Global, local, and layer look operators may all expand working-space
  // chroma, but a complete recipe owns one gamut-fit boundary. Reintroducing
  // an operator-local fit would make stacked color edits repeatedly compress
  // the result before later edits can see it.
  auto stacked_look_state = linear_adjustment_state(0.0);
  stacked_look_state.color_pipeline.output_headroom = 16.0;
  stacked_look_state.vibrance = 36.0;
  photo_editor::pipeline::LocalMask full_color_mask;
  full_color_mask.kind = "brush";
  full_color_mask.segmentation_width = 1;
  full_color_mask.segmentation_height = 1;
  full_color_mask.segmentation_mask = {0xff};
  full_color_mask.saturation = 24.0;
  stacked_look_state.local_masks.push_back(full_color_mask);
  photo_editor::pipeline::PhotoLayer color_layer;
  color_layer.kind = "adjustment";
  color_layer.blend_mode = "normal";
  color_layer.vibrance = 18.0;
  stacked_look_state.photo_layers.push_back(color_layer);
  auto stacked_look_pixels = std::vector<ColorPixel>{
      ColorPixel{
          .red = 0.88F,
          .green = 0.09F,
          .blue = 0.03F,
          .alpha = 0.73F,
      },
  };
  photo_editor::pipeline::ColorAdjustmentTelemetry stacked_look_telemetry;
  photo_editor::pipeline::apply_color_adjustments(
      &stacked_look_pixels,
      1,
      1,
      stacked_look_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      {},
      &stacked_look_telemetry);
  if (stacked_look_telemetry.final_output_gamut_fit_pass_count != 1 ||
      !finite_pixel(stacked_look_pixels.front()) ||
      std::abs(stacked_look_pixels.front().alpha - 0.73F) > 1.0e-7F) {
    return fail(
        "single-look-gamut-boundary",
        "a global/local/layer look recipe must execute exactly one final gamut-fit pass");
  }

  auto aces_stacked_look_state = stacked_look_state;
  aces_stacked_look_state.color_pipeline.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1;
  aces_stacked_look_state.color_pipeline.render_intent =
      photo_editor::pipeline::RenderIntent::Perceptual;
  auto aces_stacked_look_pixels = std::vector<ColorPixel>{
      ColorPixel{
          .red = 0.88F,
          .green = 0.09F,
          .blue = 0.03F,
          .alpha = 0.73F,
      },
  };
  photo_editor::pipeline::ColorAdjustmentTelemetry
      aces_stacked_look_telemetry;
  photo_editor::pipeline::apply_color_adjustments(
      &aces_stacked_look_pixels,
      1,
      1,
      aces_stacked_look_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      {},
      &aces_stacked_look_telemetry);
  if (aces_stacked_look_telemetry
          .final_output_gamut_fit_pass_count != 0 ||
      !finite_pixel(aces_stacked_look_pixels.front()) ||
      std::abs(
          aces_stacked_look_pixels.front().alpha - 0.73F) >
          1.0e-7F) {
    return fail(
        "aces-defers-gamut-boundary",
        "perceptual ACES look edits must defer gamut mapping to photographic output");
  }

  auto aces_output_ownership_state =
      modern_scene_adjustment_state(0.0);
  aces_output_ownership_state.color_pipeline.output_headroom =
      1.0;
  aces_output_ownership_state.color_pipeline
      .black_point_compensation_enabled = true;
  aces_output_ownership_state.color_pipeline
      .source_black_point = 0.0;
  aces_output_ownership_state.color_pipeline
      .output_black_point = 0.04;
  auto aces_scene_output_ownership_pixels =
      std::vector<ColorPixel>{
          ColorPixel{
              .red = 0.0F,
              .green = 0.0F,
              .blue = 0.0F,
              .alpha = 1.0F,
          },
          ColorPixel{
              .red = 24.0F,
              .green = 12.0F,
              .blue = 6.0F,
              .alpha = 0.8F,
          },
      };
  photo_editor::pipeline::ColorAdjustmentTelemetry
      aces_scene_output_ownership_telemetry;
  photo_editor::pipeline::apply_color_adjustments(
      &aces_scene_output_ownership_pixels,
      2,
      1,
      aces_output_ownership_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      {},
      &aces_scene_output_ownership_telemetry);
  if (aces_scene_output_ownership_telemetry
          .final_output_ms != 0 ||
      std::abs(
          aces_scene_output_ownership_pixels.front().red) >
          1.0e-7F ||
      aces_scene_output_ownership_pixels.back().red <
          23.9F) {
    return fail(
        "aces-photographic-output-ownership",
        "the scene pipeline applied black-point or display-headroom compression before ACES output");
  }
  auto aces_display_output_ownership_pixels =
      aces_scene_output_ownership_pixels;
  photo_editor::pipeline::apply_output_transform(
      aces_display_output_ownership_pixels,
      aces_output_ownership_state.color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::
          PreviewDisplay);
  if (!finite_pixel(
          aces_display_output_ownership_pixels.front()) ||
      !finite_pixel(
          aces_display_output_ownership_pixels.back()) ||
      aces_display_output_ownership_pixels.front().red <
          0.039F ||
      aces_display_output_ownership_pixels.back().red >
          1.001F) {
    return fail(
        "aces-photographic-output-boundary",
        "ACES PhotographicOutput did not own one black-point and display-range mapping");
  }

  return EXIT_SUCCESS;
}

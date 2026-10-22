#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#include "../engine/src/pipeline/color_look_protected_program.h"
#include "../engine/src/pipeline/color_working_space_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

namespace {
using namespace photo_editor::pipeline;
constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;

double error(const ColorPixel& left, const ColorPixel& right) {
  return std::max({std::abs(double(left.red) - right.red),
                   std::abs(double(left.green) - right.green),
                   std::abs(double(left.blue) - right.blue)});
}

ColorPixel hue_patch(double hue) {
  const auto sector = hue * 6.0;
  const auto x = static_cast<float>(1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
  ColorPixel p;
  switch (static_cast<int>(sector) % 6) {
    case 0: p = {1, x, 0, .37F}; break;
    case 1: p = {x, 1, 0, .37F}; break;
    case 2: p = {0, 1, x, .37F}; break;
    case 3: p = {0, x, 1, .37F}; break;
    case 4: p = {x, 0, 1, .37F}; break;
    default: p = {1, 0, x, .37F}; break;
  }
  const auto decode = [](double v) {
    return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
  };
  p.red = static_cast<float>(decode(p.red));
  p.green = static_cast<float>(decode(p.green));
  p.blue = static_cast<float>(decode(p.blue));
  return p;
}

// Independent published linear-sRGB -> OKLab reference (Ottosson matrices).
double reference_hue(const ColorPixel& p) {
  const auto l = std::cbrt(.4122214708*p.red + .5363325363*p.green + .0514459929*p.blue);
  const auto m = std::cbrt(.2119034982*p.red + .6806995451*p.green + .1073969566*p.blue);
  const auto s = std::cbrt(.0883024619*p.red + .2817188376*p.green + .6299787005*p.blue);
  const auto a = 1.9779984951*l - 2.4285922050*m + .4505937099*s;
  const auto b = .0259040371*l + .7827717662*m - .8086757660*s;
  auto h = std::atan2(b, a) / kTwoPi;
  return h < 0 ? h + 1 : h;
}

bool protection_contract() {
  for (const auto* space : {"Linear sRGB", "Linear Display P3", "Linear ProPhoto RGB", "Linear Rec.2020", "ACEScg", "ACES2065-1"}) {
    for (const auto display_hue : {.0, .04, .08, .12, .25, .5, .64, .9}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(space);
      state.grading_midtones_saturation = 50;
      state.skin_tone_protection_enabled = true;
      state.skin_tone_line_hue = display_hue;
      state.skin_tone_protection_range = 10;
      state.skin_tone_protection_strength = 100;
      const auto expected_hue = reference_hue(hue_patch(display_hue));
      const auto compiled = compile_look_stage_program(state);
      if (std::abs(std::remainder(double(compiled.skin_hue) - expected_hue, 1.0)) > 1.0e-4) {
        std::cerr << "protection display hue disagrees with independent OKLab oracle: " << space << '\n';
        return false;
      }
      for (const auto strength : {0.0, 25.0, 100.0}) {
        state.skin_tone_protection_strength = strength;
        const auto program = compile_look_stage_program(state);
        const auto scale = compiled_look_skin_protection_scale(program,
            static_cast<float>(.18*std::cos(expected_hue*kTwoPi)),
            static_cast<float>(.18*std::sin(expected_hue*kTwoPi)));
        if (std::abs(scale - (1.0 - .95*strength/100.0)) > 2.0e-5 ||
            compiled_look_skin_protection_scale(program, 0, 0) != 1.0F ||
            std::abs(compiled_look_skin_protection_scale(program,
                static_cast<float>(.18*std::cos((expected_hue+.25)*kTwoPi)),
                static_cast<float>(.18*std::sin((expected_hue+.25)*kTwoPi))) - 1.0F) > 1.0e-6F) {
          std::cerr << "protection center/strength/neutral/off-range contract failed\n";
          return false;
        }
      }
    }
  }
  return true;
}

int run(bool use_metal) {
  metal::MetalPipelineBackend backend;
  if (use_metal && !backend.available()) {
    std::cerr << backend.unavailable_reason() << '\n';
    return backend.unavailable_reason() == "no_system_metal_device" ||
                   backend.unavailable_reason() == "metal_not_compiled" ? 77 : EXIT_FAILURE;
  }
  ColorAdjustmentState srgb_state;
  srgb_state.color_pipeline.working_profile = working_color_profile_from_name("Linear sRGB");
  const auto srgb = make_pipeline_color_context(srgb_state);
  auto dispatches = 0;
  for (const auto* space : {"Linear sRGB", "Linear Display P3", "Linear ProPhoto RGB", "Linear Rec.2020", "ACEScg", "ACES2065-1", "Linear Adobe RGB"}) {
    for (int renderer = 0; renderer < 3; ++renderer) {
      for (const auto headroom : {1.0, 4.0}) {
      for (int effect = 0; effect < 14; ++effect) {
        ColorAdjustmentState state;
        state.input_transform.raw_input_transform_enabled = false;
        state.color_pipeline.output_headroom = headroom;
        state.color_pipeline.working_profile = working_color_profile_from_name(space);
        state.color_pipeline.photographic_rendering_model = static_cast<PhotographicRenderingModel>(renderer);
        state.grading_shadows_hue = .02;
        state.grading_midtones_hue = .35;
        state.grading_highlights_hue = .68;
        if (effect < 4) {
          const auto sign = effect % 2 == 0 ? 1.0 : -1.0;
          state.grading_shadows_saturation = sign*100;
          state.grading_midtones_saturation = sign*75;
          state.grading_highlights_saturation = sign*50;
          state.grading_balance = effect < 2 ? -100 : 100;
          state.grading_midtones_luminance = effect < 2 ? -15 : 15;
        } else if (effect < 9) {
          const std::array modes{"Complementary", "Analogous", "Triadic", "Split Complementary", "Tetradic"};
          state.color_harmony_enabled = true;
          state.color_harmony_strength = 100;
          state.color_harmony_base_hue = .08;
          state.color_harmony_mode = modes[static_cast<std::size_t>(effect - 4)];
        }
        if (effect == 9) {
          state.color_harmony_enabled = true;
          state.color_harmony_mode = "Triadic";
          state.color_harmony_strength = 80;
          state.grading_midtones_saturation = -65;
          state.grading_shadows_saturation = 50;
          state.grading_highlights_saturation = 25;
          state.color_mixer_channels[0] = {.hue = 40, .saturation = 60, .luminance = 20};
          state.color_replacement_enabled = true;
          state.color_replacement_strength = 65;
          state.color_replacement_source_hue = .08;
          state.color_replacement_target_hue = .64;
        }
        if (effect >= 10) {
          state.grading_highlights_saturation = 50;
          if (effect == 10) state.input_transform.exposure = .5;
          if (effect == 11) state.filmic_shoulder = 20;
          if (effect == 12) state.hdr_local_tone_mapping = 20;
          if (effect == 13) state.color_pipeline.render_intent = RenderIntent::RelativeColorimetric;
        }
        state.skin_tone_protection_enabled = true;
        state.skin_tone_line_hue = .08;
        state.skin_tone_protection_range = 30;
        state.skin_tone_protection_strength = 80;
        const auto context = make_pipeline_color_context(state);
        std::vector<ColorPixel> source;
        for (int sample = 0; sample < 384; ++sample) {
          auto p = hue_patch(double(sample % 128) / 128.0);
          const auto scale = sample < 128 ? 1.0F : sample < 256 ? 0.000001F : 3.0F;
          p.red *= scale; p.green *= scale; p.blue *= scale;
          if (sample % 17 == 0) p = {.18F, .18F, .18F, 0};
          if (sample % 19 == 0) p = {-.1F, .2F, .4F, 1.3F};
          if (sample % 23 == 0) p = {0, 0, 0, .5F};
          source.push_back(apply_rgb_matrix(apply_rgb_matrix(p, srgb.rgb_to_xyz_d65), context.xyz_d65_to_rgb));
        }
        auto reference = source;
        if (!apply_color_look_stage(&reference, static_cast<int>(source.size()), 1, state)) return EXIT_FAILURE;
        const auto program = compile_look_stage_program(state);
        std::vector<ColorPixel> actual;
        if (use_metal) {
          RenderSurfaceDescriptor descriptor{
              .pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
              .color_domain = RenderColorDomain::WorkingScene,
              .range = RenderRangeModel::SceneExtended,
              .alpha = RenderAlphaModel::Straight,
              .color_space_identity = space, .transfer_identity = "linear",
              .width = static_cast<std::uint32_t>(source.size()), .height = 1};
          ColorAdjustmentTelemetry telemetry;
          // This dispatch isolates Look on already tone-processed pixels. The
          // compiled program retains the HDR-tone eligibility bit; asking this
          // API to run HDR tone too would require its separate tone LUT and
          // would compare a different stage to the reference above.
          auto dispatch_state = state;
          dispatch_state.hdr_local_tone_mapping = 0;
          const auto surface = backend.publish_masks_and_color_lut_surface(
              source, static_cast<int>(source.size()), 1, dispatch_state, {}, descriptor,
              &telemetry, {}, 1, false, nullptr, nullptr, &program);
          const auto copied = surface && surface->copy_pixels(&actual);
          if (!surface || !copied || telemetry.gpu_kernel_dispatch_count < 1) {
            std::cerr << "creative GPU dispatch failed: " << space << " renderer=" << renderer
                      << " effect=" << effect << " headroom=" << headroom
                      << " surface=" << bool(surface) << " copied=" << copied
                      << " complete=" << (surface && surface->producer_completed_successfully())
                      << " dispatches=" << telemetry.gpu_kernel_dispatch_count
                      << " reason=" << telemetry.gpu_fallback_reason << '\n';
            return EXIT_FAILURE;
          }
          ++dispatches;
        } else {
          for (const auto& p : source) actual.push_back(apply_compiled_look_stage_program(program, p));
        }
        if (actual.size() != source.size()) return EXIT_FAILURE;
        for (std::size_t i = 0; i < source.size(); ++i) {
          if (!std::isfinite(actual[i].red) || !std::isfinite(actual[i].green) || !std::isfinite(actual[i].blue) ||
              actual[i].alpha != source[i].alpha || reference[i].alpha != source[i].alpha ||
              error(actual[i], reference[i]) > (use_metal ? 8.0e-5 : 4.0e-5)) {
            std::cerr << "creative parity failed: " << space << " renderer=" << renderer << " effect=" << effect
                      << " headroom=" << headroom << " sample=" << i << " error=" << error(actual[i], reference[i]) << '\n';
            return EXIT_FAILURE;
          }
        }
      }
      }
    }
  }
  std::cout << "Creative contracts: 7 profiles, 3 renderers, 2 headrooms, 14 effect cases, 384 samples; GPU dispatches=" << dispatches << '\n';
  return EXIT_SUCCESS;
}
}

int main(int argc, char** argv) {
  if (!protection_contract()) return EXIT_FAILURE;
  return run(argc == 2 && std::string(argv[1]) == "--metal");
}

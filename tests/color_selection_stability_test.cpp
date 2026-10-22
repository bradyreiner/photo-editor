#include "photo_editor/pipeline/color_adjustments.h"
#include "../engine/src/pipeline/color_working_space_runtime.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace photo_editor::pipeline;

double error(const ColorPixel& a, const ColorPixel& b) {
  return std::max({std::abs(double(a.red) - b.red),
                   std::abs(double(a.green) - b.green),
                   std::abs(double(a.blue) - b.blue)});
}

ColorAdjustmentState state_for(const char* space) {
  ColorAdjustmentState state;
  state.input_transform.raw_input_transform_enabled = false;
  state.color_pipeline.working_profile = working_color_profile_from_name(space);
  return state;
}

ColorPixel convert(
    const ColorPixel& pixel, const PipelineColorContext& from,
    const PipelineColorContext& to) {
  return apply_rgb_matrix(apply_rgb_matrix(pixel, from.rgb_to_xyz_d65), to.xyz_d65_to_rgb);
}

bool render(
    const ColorAdjustmentState& state, const std::vector<ColorPixel>& source,
    std::vector<ColorPixel>* reference, std::vector<ColorPixel>* compiled) {
  *reference = source;
  if (!apply_color_look_stage(reference, static_cast<int>(source.size()), 1, state)) return false;
  const auto program = compile_look_stage_program(state);
  compiled->clear();
  for (std::size_t i = 0; i < source.size(); ++i) {
    const auto result = apply_compiled_look_stage_program(program, source[i]);
    if (!std::isfinite(result.red) || !std::isfinite(result.green) ||
        !std::isfinite(result.blue) || result.alpha != source[i].alpha ||
        (*reference)[i].alpha != source[i].alpha || error(result, (*reference)[i]) > 4.0e-5) {
      std::cerr << "reference/compiled selection mismatch at " << i
                << " error=" << error(result, (*reference)[i]) << '\n';
      return false;
    }
    compiled->push_back(result);
  }
  return true;
}

ColorPixel hue_patch(double hue) {
  const auto h = hue * 6.0;
  const auto x = static_cast<float>(1.0 - std::abs(std::fmod(h, 2.0) - 1.0));
  ColorPixel result;
  switch (static_cast<int>(h) % 6) {
    case 0: result = {1, x, 0, .37F}; break;
    case 1: result = {x, 1, 0, .37F}; break;
    case 2: result = {0, 1, x, .37F}; break;
    case 3: result = {0, x, 1, .37F}; break;
    case 4: result = {x, 0, 1, .37F}; break;
    default: result = {1, 0, x, .37F}; break;
  }
  result.red = static_cast<float>(srgb_to_linear(result.red));
  result.green = static_cast<float>(srgb_to_linear(result.green));
  result.blue = static_cast<float>(srgb_to_linear(result.blue));
  return result;
}

int verify_metal() {
  metal::MetalPipelineBackend backend;
  if (!backend.available()) {
    std::cerr << "Metal selection test: " << backend.unavailable_reason() << '\n';
    return backend.unavailable_reason() == "no_system_metal_device" ||
                   backend.unavailable_reason() == "metal_not_compiled" ? 77 : EXIT_FAILURE;
  }
  const auto srgb = make_pipeline_color_context(state_for("Linear sRGB"));
  for (const auto* space : {"Linear sRGB", "Linear Display P3", "Linear ProPhoto RGB", "Linear Rec.2020"}) {
    for (const auto* model : {"HSL", "HSV", "OKLab", "LCH"}) {
      for (int tool = 0; tool < 4; ++tool) {
        auto state = state_for(space);
        state.color_mixer_model = model;
        state.color_mixer_channels[0] = {
            .hue = 50, .saturation = 70, .luminance = 30,
            .selective_cyan = -30, .selective_magenta = 20,
            .selective_yellow = 15, .selective_black = 25};
        if (tool == 1) {
          state.black_and_white_enabled = true;
          state.black_and_white_mixer.fill(65);
        }
        if (tool == 2) {
          state.color_replacement_enabled = true;
          state.color_replacement_strength = 100;
          state.color_replacement_range = 80;
          state.color_replacement_target_hue = .64;
          state.color_replacement_preserve_saturation = false;
        }
        if (tool == 3) {
          state.color_harmony_enabled = true;
          state.color_harmony_strength = 85;
        }
        const auto context = make_pipeline_color_context(state);
        std::vector<ColorPixel> source;
        for (int i = 0; i < 512; ++i) {
          // Neutral-noise patches, both falloff boundaries, black floor and real hues.
          const auto value = i < 128 ? .18F : i < 256 ? .0001F : i < 384 ? .82F : .018F;
          auto p = i % 4 == 0 ? hue_patch(double(i % 128) / 128.0) :
              ColorPixel{value * (1.F + float(i % 64) / 256.F), value, value, .37F};
          source.push_back(convert(p, srgb, context));
        }
        std::vector<ColorPixel> expected, compiled;
        if (!render(state, source, &expected, &compiled)) return EXIT_FAILURE;
        const auto program = compile_look_stage_program(state);
        RenderSurfaceDescriptor descriptor{
            .pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
            .color_domain = RenderColorDomain::WorkingScene,
            .range = RenderRangeModel::SceneExtended,
            .alpha = RenderAlphaModel::Straight,
            .color_space_identity = space,
            .transfer_identity = "linear", .width = 512, .height = 1};
        ColorAdjustmentTelemetry telemetry;
        const auto surface = backend.publish_masks_and_color_lut_surface(
            source, 512, 1, state, {}, descriptor, &telemetry, {}, 1.0, false,
            nullptr, nullptr, &program);
        std::vector<ColorPixel> actual;
        if (!surface || !surface->copy_pixels(&actual) || actual.size() != source.size() ||
            telemetry.gpu_kernel_dispatch_count < 1) {
          std::cerr << "Metal selection dispatch failed: " << telemetry.gpu_fallback_reason << '\n';
          return EXIT_FAILURE;
        }
        for (std::size_t i = 0; i < actual.size(); ++i) {
          if (!std::isfinite(actual[i].red) || !std::isfinite(actual[i].green) ||
              !std::isfinite(actual[i].blue) || error(actual[i], expected[i]) > 8.0e-5 ||
              actual[i].alpha != source[i].alpha) {
            std::cerr << "Metal selection parity failed: " << space << ' ' << model
                      << " tool=" << tool << " sample=" << i
                      << " error=" << error(actual[i], expected[i]) << '\n';
            return EXIT_FAILURE;
          }
        }
      }
    }
  }
  std::cout << "Metal color selection: 64 real dispatches match the CPU reference\n";
  return EXIT_SUCCESS;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--metal") return verify_metal();
  const auto srgb = make_pipeline_color_context(state_for("Linear sRGB"));
  constexpr auto models = std::array{"HSL", "HSV", "OKLab", "LCH"};
  constexpr auto spaces = std::array{
      "Linear sRGB", "Linear Display P3", "Linear ProPhoto RGB", "Linear Rec.2020"};
  constexpr std::array<double, 8> hues{0.0, .09, .15, .32, .50, .64, .78, .88};
  std::vector<ColorPixel> reference, compiled;
  for (const auto* space : spaces) {
    auto state = state_for(space);
    const auto context = make_pipeline_color_context(state);
    std::vector<ColorPixel> neutral_noise;
    // Every direction around gray, across shadows, midtones, highlights and HDR.
    for (const auto value : {.0001F, .003F, .018F, .18F, .82F, 1.5F}) {
      for (int direction = 0; direction < 6; ++direction) {
        auto p = ColorPixel{value, value, value, .37F};
        auto* component = direction % 3 == 0 ? &p.red : direction % 3 == 1 ? &p.green : &p.blue;
        *component += value * (direction < 3 ? .005F : -.005F);
        neutral_noise.push_back(convert(p, srgb, context));
      }
    }
    for (const auto* model : models) {
      for (std::size_t channel = 0; channel < hues.size(); ++channel) {
        for (int control = 0; control < 7; ++control) {
          state = state_for(space);
          state.color_mixer_model = model;
          auto& c = state.color_mixer_channels[channel];
          const std::array<double*, 7> controls{
              &c.hue, &c.saturation, &c.luminance, &c.selective_cyan,
              &c.selective_magenta, &c.selective_yellow, &c.selective_black};
          *controls[static_cast<std::size_t>(control)] = control % 2 == 0 ? 100.0 : -100.0;
          if (!render(state, neutral_noise, &reference, &compiled)) return EXIT_FAILURE;
          for (std::size_t i = 0; i < neutral_noise.size(); ++i) {
            if (error(reference[i], neutral_noise[i]) > 3.0e-6 ||
                error(compiled[i], neutral_noise[i]) != 0.0) {
              std::cerr << "near-neutral noise changed: " << space << ' ' << model
                        << " channel=" << channel << " control=" << control << '\n';
              return EXIT_FAILURE;
            }
          }
        }
        // A named color must still respond, including in a different working gamut.
        state = state_for(space);
        state.color_mixer_model = model;
        state.color_mixer_channels[channel].hue = 35.0;
        const auto input = convert(hue_patch(hues[channel]), srgb, context);
        if (!render(state, {input}, &reference, &compiled) || error(input, reference[0]) < .005) {
          std::cerr << "named color did not respond: " << space << ' ' << model << ' ' << channel << '\n';
          return EXIT_FAILURE;
        }
        // Identical physical colors must receive identical edits across working spaces.
        auto s = state;
        s.color_pipeline.working_profile = working_color_profile_from_name("Linear sRGB");
        const auto expected = apply_compiled_look_stage_program(compile_look_stage_program(s), hue_patch(hues[channel]));
        if (error(convert(compiled[0], context, srgb), expected) > 8.0e-5) {
          std::cerr << "working-gamut-dependent color selection: " << space << ' ' << model << ' ' << channel << '\n';
          return EXIT_FAILURE;
        }
      }
      state = state_for(space);
      state.color_mixer_model = model;
      state.color_mixer_channels[0].hue = 100;
      state.color_mixer_channels[0].saturation = 100;
      state.color_mixer_channels[0].luminance = 100;
      const std::vector<ColorPixel> unrelated{
          convert({0, .4F, 0, .37F}, srgb, context),
          convert({0, 0, .4F, .37F}, srgb, context)};
      if (!render(state, unrelated, &reference, &compiled)) return EXIT_FAILURE;
      for (std::size_t i = 0; i < unrelated.size(); ++i) {
        if (error(unrelated[i], reference[i]) > 3.0e-6) {
          std::cerr << "Reds adjustment changed green/blue in " << model << '\n';
          return EXIT_FAILURE;
        }
      }
    }
    // Related hue-selective tools must use the same neutral protection.
    for (int tool = 0; tool < 3; ++tool) {
      state = state_for(space);
      if (tool == 0) {
        state.black_and_white_enabled = true;
        state.black_and_white_mixer.fill(100);
      } else if (tool == 1) {
        state.color_replacement_enabled = true;
        state.color_replacement_range = 100;
        state.color_replacement_strength = 100;
        state.color_replacement_preserve_saturation = false;
      } else {
        state.color_harmony_enabled = true;
        state.color_harmony_strength = 100;
      }
      if (!render(state, neutral_noise, &reference, &compiled)) return EXIT_FAILURE;
      auto baseline = neutral_noise;
      auto neutral_state = state_for(space);
      neutral_state.black_and_white_enabled = tool == 0;
      if (!apply_color_look_stage(&baseline, static_cast<int>(baseline.size()), 1, neutral_state)) return EXIT_FAILURE;
      for (std::size_t i = 0; i < baseline.size(); ++i) {
        if (error(baseline[i], reference[i]) > 3.0e-6) {
          std::cerr << "related selector amplified neutral noise: tool=" << tool << '\n';
          return EXIT_FAILURE;
        }
      }
    }
  }
  // Continuous falloff: crossing the neutral-protection band must not pop brightness.
  auto state = state_for("Linear sRGB");
  state.color_mixer_channels[0].luminance = 30;
  const auto program = compile_look_stage_program(state);
  auto previous = apply_compiled_look_stage_program(program, {.18F, .18F, .18F, 1});
  for (int step = 1; step <= 2000; ++step) {
    const auto input = ColorPixel{.18F + .04F * float(step) / 2000.F, .18F, .18F, 1};
    const auto output = apply_compiled_look_stage_program(program, input);
    if (error(output, previous) > .001) {
      std::cerr << "color selection has a visible discontinuity near gray\n";
      return EXIT_FAILURE;
    }
    previous = output;
  }
  std::cout << "color selection: neutral noise, named colors, gamut invariance, related tools, CPU parity and continuity passed\n";
  return EXIT_SUCCESS;
}

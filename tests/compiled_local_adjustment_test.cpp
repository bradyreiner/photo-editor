#include "color_adjustment_kernel_runtime.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <vector>

using namespace photo_editor::pipeline;

namespace {
ColorPixel evaluate(ColorPixel pixel, const PipelineColorContext& context,
    PhotographicRenderingModel model, const LocalAdjustmentSettings& settings,
    double weight, const CompiledLocalAdjustment* compiled) {
  apply_local_adjustment_mask(&pixel, context, model, weight,
      settings.exposure, settings.contrast, settings.highlights, settings.shadows,
      settings.whites, settings.blacks, settings.temperature, settings.tint,
      settings.vibrance, settings.saturation, settings.hue_shift,
      settings.saturation_shift, settings.colorize_hue, settings.colorize_strength, compiled);
  return pixel;
}

bool identical(const ColorPixel& a, const ColorPixel& b) {
  return std::bit_cast<std::uint32_t>(a.red) == std::bit_cast<std::uint32_t>(b.red) &&
      std::bit_cast<std::uint32_t>(a.green) == std::bit_cast<std::uint32_t>(b.green) &&
      std::bit_cast<std::uint32_t>(a.blue) == std::bit_cast<std::uint32_t>(b.blue) &&
      std::bit_cast<std::uint32_t>(a.alpha) == std::bit_cast<std::uint32_t>(b.alpha);
}
}

int main() {
  std::vector<LocalAdjustmentSettings> cases(1);
  const std::array fields{&LocalAdjustmentSettings::exposure, &LocalAdjustmentSettings::contrast,
      &LocalAdjustmentSettings::highlights, &LocalAdjustmentSettings::shadows,
      &LocalAdjustmentSettings::whites, &LocalAdjustmentSettings::blacks,
      &LocalAdjustmentSettings::temperature, &LocalAdjustmentSettings::tint,
      &LocalAdjustmentSettings::vibrance, &LocalAdjustmentSettings::saturation};
  for (const auto field : fields) for (const auto direction : {-1.0, 1.0}) {
    LocalAdjustmentSettings settings;
    settings.*field = direction * (field == &LocalAdjustmentSettings::exposure ? 1.25 : 35.0);
    cases.push_back(settings);
  }
  cases.push_back(LocalAdjustmentSettings{.exposure = 0.75, .contrast = 23.0,
      .highlights = -17.0, .shadows = 29.0, .whites = 12.0, .blacks = -11.0,
      .temperature = 18.0, .tint = -9.0, .vibrance = 22.0, .saturation = -14.0});
  auto specialized = cases.back();
  specialized.hue_shift = 24.0;
  cases.push_back(specialized);
  specialized.hue_shift = 0.0;
  specialized.saturation_shift = 15.0;
  cases.push_back(specialized);
  specialized.saturation_shift = 0.0;
  specialized.colorize_hue = 0.4;
  specialized.colorize_strength = 30.0;
  cases.push_back(specialized);
  std::vector pixels{ColorPixel{0.0F, 0.0F, 0.0F, 0.0F},
      ColorPixel{0.001F, 0.005F, 0.002F, 0.5F}, ColorPixel{0.18F, 0.18F, 0.18F, 1.0F},
      ColorPixel{0.6F, 0.12F, 0.025F, 0.7F}, ColorPixel{-0.04F, 0.9F, 0.03F, 1.0F},
      ColorPixel{2.5F, 1.2F, 0.8F, 1.0F}, ColorPixel{8.0F, 6.0F, 3.0F, 1.0F}};
  std::uint32_t seed = 0x5392a81U;
  const auto sample = [&] {
    seed = seed * 1664525U + 1013904223U;
    const auto value = std::exp2(-20.0 + double(seed & 65535U) / 65535.0 * 25.0);
    return static_cast<float>((seed & 0x10000U) ? value : -value);
  };
  for (int i = 0; i < 128; ++i) pixels.push_back({sample(), sample(), sample(), float(i % 5) / 4.0F});
  pixels.push_back({-0.0F, 0.0F, -0.0F, -0.0F});
  for (const auto value : {1e-10, 1e-8, 1e-6, 1.1e-6}) {
    LocalAdjustmentSettings settings;
    settings.temperature = 12.0;
    settings.contrast = value;
    settings.saturation = value;
    cases.push_back(settings);
  }
  std::size_t comparisons = 0;
  for (const auto profile : {"Linear sRGB", "Linear Display P3", "Linear ProPhoto RGB", "Linear Rec.2020"})
    for (const auto model : {PhotographicRenderingModel::LegacyOklchV1,
        PhotographicRenderingModel::Aces2HellwigJmhV1, PhotographicRenderingModel::Aces2OcioV1})
      for (const auto headroom : {1.0, 4.0})
        for (const auto adaptation : {"Bradford", "CAT16"}) {
          ColorAdjustmentState state;
          state.color_pipeline.working_profile = working_color_profile_from_name(profile);
          state.color_pipeline.photographic_rendering_model = model;
          state.color_pipeline.output_headroom = headroom;
          state.input_transform.chromatic_adaptation_method = adaptation;
          const auto context = make_pipeline_color_context(state);
          for (const auto& settings : cases) {
            const auto compiled = compile_local_adjustment(context, model, settings);
            const bool specialized_color = settings.hue_shift != 0.0 ||
                settings.saturation_shift != 0.0 || settings.colorize_strength != 0.0;
            if (compiled.has_value() == specialized_color) {
              std::cerr << "Specialized local color must retain its original path\n"; return 1;
            }
            for (const auto& pixel : pixels) for (const auto weight : {0.0, 0.001, 0.2, 0.5, 0.85, 1.0}) {
              const auto reference = evaluate(pixel, context, model, settings, weight, nullptr);
              const auto actual = evaluate(pixel, context, model, settings, weight, compiled ? &*compiled : nullptr);
              if (!identical(reference, actual)) {
                std::cerr << "Compiled local adjustment differs in " << profile << " at weight " << weight << '\n';
                return 1;
              }
              ++comparisons;
            }
          }
        }
  std::cout << comparisons << " bit-exact local adjustment comparisons passed\n";
  return 0;
}

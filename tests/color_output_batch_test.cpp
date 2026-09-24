#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace photo_editor::pipeline;

bool identical(const ColorPixel& a, const ColorPixel& b) {
  return std::bit_cast<std::array<std::uint32_t, 4>>(a) ==
         std::bit_cast<std::array<std::uint32_t, 4>>(b);
}

int main() {
  // The unmodified scalar API is the reference. Cover both serial and parallel
  // batches, signed/HDR samples, clipping, perceptual gamut mapping and alpha.
  std::vector<ColorPixel> source{{0, 0, 0, 0}, {1, 1, 1, 1}, {-1, 2, 8, -0.5F},
      {0.18F, 0.18F, 0.18F, 1.5F}, {0.0F, 1.0F, 0.0F, 0.5F}};
  std::uint32_t rng = 42;
  const auto channel = [&rng]() {
    rng = rng * 1664525U + 1013904223U;
    return static_cast<float>(rng >> 8U) / 16777215.0F * 5.0F - 0.5F;
  };
  for (int i = 0; i < 257; ++i)
    source.push_back({channel(), channel(), channel(), channel()});
  std::size_t compared = 0;
  for (const auto working : {"Linear ProPhoto RGB", "Linear sRGB"})
    for (const auto output : {"sRGB", "Display P3", "Adobe RGB"})
      for (const auto intent : {RenderIntent::Perceptual, RenderIntent::RelativeColorimetric})
        for (const auto policy : {GamutMappingPolicy::Perceptual, GamutMappingPolicy::Clip,
                                  GamutMappingPolicy::PreserveSaturation})
          for (const auto headroom : {1.0, 4.0})
            for (const bool bpc : {false, true})
              for (const auto adaptation : {"Bradford", "CAT16"}) {
                ColorPipelineConfig config;
                config.photographic_rendering_model = PhotographicRenderingModel::LegacyOklchV1;
                config.working_profile = working_color_profile_from_name(working);
                config.preview_output_profile = output_color_profile_from_name(output);
                config.export_output_profile = output_color_profile_from_name(output);
                config.render_intent = intent;
                config.output_gamut_mapping_policy = policy;
                config.output_headroom = headroom;
                config.black_point_compensation_enabled = bpc;
                config.source_black_point = 0.01;
                config.output_black_point = 0.03;
                for (const auto target : {GamutWarningTarget::PreviewDisplay, GamutWarningTarget::ExportOutput}) {
                  auto actual = source;
                  apply_output_transform(actual, config, target, adaptation);
                  for (std::size_t i = 0; i < source.size(); ++i) {
                    const auto expected = apply_output_transform(source[i], config, target, adaptation).pixel;
                    if (!identical(actual[i], expected)) {
                      std::cerr << "Batch output differs from scalar: " << working << '/' << output
                                << " sample=" << i << " headroom=" << headroom << '\n';
                      return 1;
                    }
                    ++compared;
                  }
                  std::vector<ColorPixel> small{source.front(), source.back()};
                  apply_output_transform(small, config, target, adaptation);
                  if (!identical(small.front(), actual.front()) || !identical(small.back(), actual.back())) return 1;
                  std::vector<ColorPixel> empty;
                  apply_output_transform(empty, config, target, adaptation);
                }
              }
  std::cout << "Exact scalar/batch output comparisons: " << compared << '\n';
}

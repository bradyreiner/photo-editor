#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace photo_editor::pipeline;

int main() {
  const auto identity = std::vector<CurvePoint>{{0, 0}, {0.5, 0.5}, {1, 1}};
  for (int channel = 0; channel < 5; ++channel) {
    ColorAdjustmentState state;
    state.color_pipeline = default_color_pipeline_config();
    state.color_pipeline.output_headroom = 16.0;
    auto* points = channel == 0 ? &state.master_curve_points
                  : channel == 1 ? &state.luma_curve_points
                  : channel == 2 ? &state.red_curve_points
                  : channel == 3 ? &state.green_curve_points
                                 : &state.blue_curve_points;
    *points = identity;
    const std::vector<ColorPixel> input{
        {0, 0, 0, 0.25F}, {0.18F, 0.18F, 0.18F, 1},
        {-0.03F, 0.1F, 0.08F, 1}, {1, 1, 1, 1}, {2, 3, 4, 0.5F}, {4, 6, 8, 1}};
    auto exact = input;
    if (!apply_color_curves_stage(&exact, static_cast<int>(exact.size()), 1, state))
      return EXIT_FAILURE;
    const auto compiled = compile_curve_stage_luts(state);
    for (std::size_t i = 0; i < input.size(); ++i) {
      const auto fast = apply_compiled_curve_stage_luts(compiled, input[i]);
      for (const auto output : {exact[i], fast}) {
        if (std::max({std::abs(output.red - input[i].red),
                      std::abs(output.green - input[i].green),
                      std::abs(output.blue - input[i].blue)}) > 2e-5F ||
            output.alpha != input[i].alpha) {
          std::cerr << "Identity curve clipped channel " << channel << " sample " << i << '\n';
          return EXIT_FAILURE;
        }
      }
    }
    // A small midtone edit must not flatten the brighter half of the image.
    (*points)[1].y = 0.52;
    auto edited = input;
    if (!apply_color_curves_stage(&edited, static_cast<int>(edited.size()), 1, state) ||
        edited.back().red <= edited[4].red || edited.back().green <= edited[4].green ||
        edited.back().blue <= edited[4].blue) {
      std::cerr << "Small point edit lost highlight separation\n";
      return EXIT_FAILURE;
    }
  }
  {
    ColorAdjustmentState layer_state;
    layer_state.color_pipeline = default_color_pipeline_config();
    PhotoLayer layer;
    layer.kind = "adjustment";
    layer.master_curve_points = identity;
    layer_state.photo_layers.push_back(layer);
    std::vector<ColorPixel> pixels{{2, 3, 4, 1}, {4, 6, 8, 1}};
    const auto input = pixels;
    if (!apply_color_layers_stage(&pixels, 2, 1, layer_state)) return EXIT_FAILURE;
    for (std::size_t i = 0; i < pixels.size(); ++i) {
      if (std::max({std::abs(pixels[i].red - input[i].red),
                    std::abs(pixels[i].green - input[i].green),
                    std::abs(pixels[i].blue - input[i].blue)}) > 2e-5F) {
        std::cerr << "Adjustment-layer identity curve clipped highlights\n";
        return EXIT_FAILURE;
      }
    }
  }
  // Moving the white endpoint retains relative highlight differences and is
  // continuous on either side of the graph boundary, in CPU and LUT paths.
  ColorAdjustmentState state;
  state.master_curve_points = {{0, 0}, {0.5, 0.52}, {1, 0.9}};
  std::vector<ColorPixel> pixels{{0.99999F, 1, 1.00001F, 1}, {2, 4, 8, 0.7F}};
  const auto input = pixels;
  if (!apply_color_curves_stage(&pixels, 2, 1, state)) return EXIT_FAILURE;
  const auto compiled = compile_curve_stage_luts(state);
  const auto fast = apply_compiled_curve_stage_luts(compiled, input[1]);
  const auto white = static_cast<float>(srgb_to_linear(0.9));
  if (std::abs(pixels[0].red - pixels[0].blue) > 0.0001F ||
      std::abs(pixels[1].blue - 8 * white) > 1e-5F ||
      std::abs(fast.blue - pixels[1].blue) > 1e-5F) {
    std::cerr << "White endpoint extension is discontinuous or differs between renderers\n";
    return EXIT_FAILURE;
  }
  std::cout << "Curve identity, small-edit headroom and endpoint continuity passed\n";
  return EXIT_SUCCESS;
}

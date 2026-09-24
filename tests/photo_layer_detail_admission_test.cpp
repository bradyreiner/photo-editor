#include "photo_editor/pipeline/color_fill_layer_program.h"
#include <array>
#include <bit>
#include <iostream>
#include <limits>

using namespace photo_editor::pipeline;
int main() {
  const std::array fields{&PhotoLayer::texture, &PhotoLayer::clarity,
      &PhotoLayer::sharpening, &PhotoLayer::luminance_nr, &PhotoLayer::color_nr};
  std::uint64_t checksum = 14695981039346656037ULL;
  int valid_cases = 0, invalid_accepted = 0;
  for (const auto* profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"}) {
    ColorAdjustmentState state;
    state.color_pipeline.working_profile = working_color_profile_from_name(profile);
    PhotoLayer layer; layer.id = "detail-admission"; layer.kind = "adjustment";
    for (auto field : fields) for (double value : {0.0, 12.0, 50.0, 100.0}) {
      auto adjusted = layer; adjusted.*field = value; state.photo_layers = {adjusted};
      const auto program = compile_fill_layer_program(state, 16, 16);
      if (!retained_simple_layers_supported(state) || !program.ready()) return 1;
      const auto& compiled = program.layers[0];
      const bool spatial = field == &PhotoLayer::texture || field == &PhotoLayer::clarity;
      if (value > 0 && ((spatial && !compiled.has_spatial_detail) || (!spatial && !compiled.has_fine_detail))) return 1;
      std::vector<ColorPixel> pixels;
      for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
        pixels.push_back({0.1F+static_cast<float>(x)/20, 0.1F+static_cast<float>(y)/20,
            static_cast<float>((x+y)%3)/4, 0.5F});
      if (!apply_compiled_fill_layer_program(&pixels, program)) return 1;
      for (const auto& pixel : pixels) for (float channel : {pixel.red,pixel.green,pixel.blue,pixel.alpha}) {
        checksum ^= std::bit_cast<std::uint32_t>(channel); checksum *= 1099511628211ULL;
      }
      ++valid_cases;
    }
    for (auto field : fields) for (double value : {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
      auto invalid = layer; invalid.*field = value; state.photo_layers = {invalid};
      if (compile_fill_layer_program(state,16,16).ready()) ++invalid_accepted;
    }
  }
  std::cout << "Finite detail cases=" << valid_cases << " rendered_checksum=" << checksum
            << " nonfinite_admitted=" << invalid_accepted << '\n';
  // Exercise long vectorizable spans and an odd tail, not just tiny rasters.
  ColorAdjustmentState raster_state;
  PhotoLayer raster; raster.id="validation-tail"; raster.kind="pixel";
  raster.pixel_width=257; raster.pixel_height=1;
  raster.pixels.assign(257, ColorPixel{-0.25F,4.0F,0.5F,1.0F});
  raster_state.photo_layers={raster};
  if (!retained_simple_layers_supported(raster_state)) return 1;
  int rejected=0;
  for (int index : {0,3,8,127,128,256}) for (int channel=0;channel<4;++channel) {
    auto& pixel=raster_state.photo_layers[0].pixels[static_cast<std::size_t>(index)];
    auto* component=channel==0?&pixel.red:channel==1?&pixel.green:channel==2?&pixel.blue:&pixel.alpha;
    const auto previous=*component;
    for (float value : {std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()}) {
      *component=value;
      if (retained_simple_layers_supported(raster_state) || compile_fill_layer_program(raster_state,8,8).ready()) return 1;
      ++rejected;
    }
    *component=previous;
  }
  std::cout << "Nonfinite raster channel/block/tail rejections=" << rejected << '\n';
  return invalid_accepted ? 1 : 0;
}

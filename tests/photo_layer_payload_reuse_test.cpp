#include "photo_editor/pipeline/color_fill_layer_program.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>

using namespace photo_editor::pipeline;
namespace {
bool equivalent(const CompiledFillLayerProgram& a, const CompiledFillLayerProgram& b) {
  if (!a.ready() || !b.ready() || a.pixel_identity != b.pixel_identity) return false;
  std::vector<ColorPixel> x(64, ColorPixel{0.2F,0.1F,0.3F,0.5F}), y=x;
  return apply_compiled_fill_layer_program(&x,a) && apply_compiled_fill_layer_program(&y,b) &&
      std::memcmp(x.data(),y.data(),x.size()*sizeof(ColorPixel)) == 0;
}
}
int main() {
  ColorAdjustmentState state;
  PhotoLayer layer; layer.id="payload"; layer.kind="pixel"; layer.pixel_width=2; layer.pixel_height=2;
  layer.pixels={{0.2F,0.7F,0.1F,1},{0.8F,0.2F,0.5F,0.3F},{-0.2F,1.4F,0.8F,0.8F},{9,-7,8,0}};
  state.photo_layers={layer,layer}; state.photo_layers[1].x=3;
  const auto cold=compile_fill_layer_program(state,8,8);
  if (!cold.ready() || cold.pixel_data.size()!=8) return 1;
  auto storage=std::make_shared<const std::vector<ColorPixel>>(cold.pixel_data);
  std::weak_ptr<const std::vector<ColorPixel>> lifetime=storage;
  auto cache=std::make_shared<CachedFillLayerPixels>(CachedFillLayerPixels{.pixels=*storage,.owner=storage,.identity=cold.pixel_identity});
  auto warm=compile_fill_layer_program(state,8,8,cache);
  if (!warm.cached_pixels || !warm.pixel_data.empty() || !equivalent(cold,warm)) return 1;
  // Appearance/geometry/masks can change without rebuilding immutable pixels.
  for (const auto* profile : {"Linear sRGB","Linear Display P3","Linear Rec.2020","Linear ProPhoto RGB"}) {
    state.color_pipeline.working_profile=working_color_profile_from_name(profile);
    state.photo_layers[0].opacity=0.6; state.photo_layers[1].transform_scale=0.65;
    state.photo_layers[1].transform_rotation=17; state.photo_layers[1].mask_width=1;
    state.photo_layers[1].mask_height=1; state.photo_layers[1].mask_data={128};
    auto reused=compile_fill_layer_program(state,8,8,cache);
    if (!reused.cached_pixels || !equivalent(reused,compile_fill_layer_program(state,8,8))) return 1;
  }
  // Same allocation, different source bytes must miss, including the last pixel
  // and alpha/hidden RGB. Restore each mutation so the next check is independent.
  for (int index : {0,3}) for (int channel=0;channel<4;++channel) {
    auto& pixel=state.photo_layers[1].pixels[static_cast<std::size_t>(index)];
    auto* value=channel==0?&pixel.red:channel==1?&pixel.green:channel==2?&pixel.blue:&pixel.alpha;
    const float previous=*value; *value+=0.125F;
    auto changed=compile_fill_layer_program(state,8,8,cache);
    if (changed.cached_pixels || changed.pixel_identity==cold.pixel_identity ||
        !equivalent(changed,compile_fill_layer_program(state,8,8))) return 1;
    *value=previous;
  }
  state.photo_layers[0].enabled=false;
  if (compile_fill_layer_program(state,8,8,cache).cached_pixels) return 1;
  state.photo_layers[0].enabled=true;
  for (int index : {0,3}) for (int channel=0;channel<4;++channel) {
    auto& pixel=state.photo_layers[0].pixels[static_cast<std::size_t>(index)];
    auto* value=channel==0?&pixel.red:channel==1?&pixel.green:channel==2?&pixel.blue:&pixel.alpha;
    const float previous=*value;
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()}) {
      *value=invalid;
      if (retained_simple_layers_supported(state) || compile_fill_layer_program(state,8,8,cache).ready()) return 1;
    }
    for (float finite : {-0.0F, std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()}) {
      *value=finite;
      if (!retained_simple_layers_supported(state) || !compile_fill_layer_program(state,8,8).ready()) return 1;
    }
    *value=previous;
  }
  // Repartition the same stream: offsets and dimensions are fresh metadata.
  state.photo_layers[0].pixel_width=1; state.photo_layers[0].pixel_height=4;
  auto reshaped=compile_fill_layer_program(state,8,8,cache);
  if (!reshaped.cached_pixels || !equivalent(reshaped,compile_fill_layer_program(state,8,8))) return 1;
  reshaped={};
  auto unowned=std::make_shared<CachedFillLayerPixels>(*cache); unowned->owner.reset();
  if (compile_fill_layer_program(state,8,8,unowned).cached_pixels) return 1;
  unowned.reset(); cache.reset(); storage.reset();
  if (lifetime.expired() || !equivalent(warm,cold)) return 1;
  warm={};
  if (!lifetime.expired()) return 1;
  std::cout << "Pinned layer payload reuse, mutation invalidation, metadata refresh and lifetime pass\n";
  return 0;
}

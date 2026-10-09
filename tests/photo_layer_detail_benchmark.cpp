#include "photo_editor/pipeline/color_adjustments.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

using namespace photo_editor::pipeline;

int main(int argc, char** argv) {
  if (argc != 7) {
    std::cerr << "usage: photo_layer_detail_benchmark output.json width height repeats mode masked\n";
    return 1;
  }
  const int width = std::stoi(argv[2]), height = std::stoi(argv[3]);
  const int repeats = std::stoi(argv[4]);
  const std::string requested_mode = argv[5];
  const bool content = requested_mode.starts_with("content-");
  const std::string mode = content ? requested_mode.substr(8) : requested_mode;
  const bool compositing_only = content && (mode == "plain" || mode == "rotated" ||
      mode == "emboss" || mode == "glass" || mode == "glass-rotated");
  const int mask_mode = std::stoi(argv[6]);
  const bool masked = mask_mode == 1;
  if (width < 1 || height < 1 || width > 10000 || height > 10000 || repeats < 1 || repeats > 10 ||
      mask_mode < 0 || mask_mode > 7)
    return 1;
  const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<ColorPixel> source(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto value = static_cast<float>((i * 37U) % 1021U) / 1020.0F;
    source[i] = {value * 3.7F - .2F, value * .7F, 1.4F - value,
        static_cast<float>(i % 17U) / 16.0F};
  }
  PhotoLayer layer;
  layer.id = "detail-plane"; layer.kind = "adjustment";
  PhotoEffectInstance effect;
  effect.id = "creative"; effect.input_scope = PhotoEffectInputScope::CompositeBelow;
  if (mode == "diffusion" || mode.starts_with("gaussian") || mode == "spatial-diffusion") {
    effect.type_id = mode.starts_with("gaussian") ? "gaussian_blur" : "optical_diffusion";
    effect.diffusion.radius = 24; effect.gaussian.sigma = 3; effect.amount = .73;
    if (mode == "gaussian-extend") effect.gaussian.edge_mode = PhotoEffectEdgeMode::Extend;
    layer.effects = {effect};
  } else if (mode == "vignette") layer.lens_vignette = .3;
  else if (mode == "dehaze") layer.dehaze = .25;
  else if (mode == "sharpen") layer.sharpening = .4;
  else if (mode == "noise") { layer.luminance_nr = .25; layer.color_nr = .3; }
  else if (mode == "texture") layer.texture = .3;
  else if (mode == "clarity") layer.clarity = -.3;
  else if (mode == "subthreshold") { layer.texture = 1e-10; layer.clarity = -1e-10; layer.dehaze = .25; }
  else if (mode == "threshold") { layer.texture = 1.1e-9; layer.clarity = -1.1e-9; layer.dehaze = .25; }
  else if (!compositing_only) { std::cerr << "Unknown mode\n"; return 1; }
  if (mode == "spatial-diffusion") { layer.texture = -.3; layer.clarity = .25; }
  // Keep every case on the general layer compositor exercised by creative tools,
  // including active Texture/Clarity guards that otherwise use the retained path.
  if (layer.effects.empty() && !compositing_only) {
    effect.type_id = "invert_linear_rgb";
    effect.amount = .01;
    layer.effects = {effect};
  }
  if (content) {
    layer.kind = "pixel";
    layer.pixel_width = width; layer.pixel_height = height; layer.pixels = source;
    for (auto& content_effect : layer.effects)
      content_effect.input_scope = PhotoEffectInputScope::LayerContent;
    if (mode == "gaussian-rotated" || mode == "rotated" || mode == "glass-rotated") {
      layer.transform_scale = .73;
      layer.transform_rotation = 17;
    }
    if (mode == "gaussian-stack") {
      auto invert = layer.effects.front();
      invert.id = "invert"; invert.type_id = "invert_linear_rgb";
      layer.effects.push_back(invert);
    }
    if (mode == "emboss" || mode == "glass" || mode == "glass-rotated") {
      layer.kind = "text";
      layer.text_material = mode == "emboss" ? "soft_emboss" : "frosted_glass";
      layer.text_material_blur = 3;
    }
  }
  ColorAdjustmentState state;
  if (masked) {
    layer.opacity = .63; layer.blend_mode = "screen"; layer.clipping_mask = true;
    layer.mask_width = width; layer.mask_height = height; layer.mask_data.resize(count);
    for (std::size_t i = 0; i < count; ++i) layer.mask_data[i] = static_cast<std::uint8_t>(i % 256U);
    PhotoLayer backing;
    backing.kind = "fill"; backing.fill_color = {.4F, .5F, .6F, .35F};
    state.photo_layers.push_back(std::move(backing));
  }
  state.photo_layers.push_back(std::move(layer));
  // 0: no masks; 1: layer mask; 2: legacy local masks; 3: mask sets;
  // 4: both local representations; 5/6: legacy gradients; 7: clone retouch.
  if (mask_mode >= 2 && mask_mode <= 4) {
    LocalMask mask;
    mask.id = "edge-source"; mask.kind = "background"; mask.exposure = .35;
    mask.segmentation_width = 9; mask.segmentation_height = 7;
    for (int i = 0; i < 63; ++i)
      mask.segmentation_mask.push_back(static_cast<std::uint8_t>((i * 47) % 256));
    mask.selection_fine_edge = 15; mask.selection_smooth = 8;
    mask.selection_decontaminate = .25;
    if (mask_mode == 2 || mask_mode == 4) state.local_masks = {mask};
    if (mask_mode == 3 || mask_mode == 4) {
      LocalMaskSet set;
      set.id = "edge-set"; set.exposure = .35; set.sources = {mask};
      state.local_mask_sets = {set};
    }
  }
  if (mask_mode == 5) { state.linear_mask_enabled = true; state.linear_mask_exposure = .35; }
  if (mask_mode == 6) { state.radial_mask_enabled = true; state.radial_mask_exposure = .35; }
  if (mask_mode == 7) {
    LocalMask clone;
    clone.id = "clone"; clone.kind = "brush"; clone.retouch_mode = "clone";
    clone.clone_source_set = true; clone.clone_source_x = .2; clone.clone_source_y = .2;
    clone.clone_offset_x = -.4; clone.clone_offset_y = -.4;
    clone.brush_points = {{.6, .6, .15, 1, 1}};
    clone.segmentation_width = 9; clone.segmentation_height = 7;
    clone.segmentation_mask.assign(63, 0); clone.segmentation_mask[41] = 255;
    state.retouch_masks = {clone};
  }
  std::vector<double> times;
  std::uint64_t expected = 0;
  for (int repeat = 0; repeat < repeats; ++repeat) {
    auto pixels = source;
    const auto start = std::chrono::steady_clock::now();
    if (!apply_color_layers_stage(&pixels, width, height, state)) return 2;
    times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& pixel : pixels)
      for (const auto channel : {pixel.red, pixel.green, pixel.blue, pixel.alpha}) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &channel, sizeof(bits));
        for (int byte = 0; byte < 4; ++byte) {
          hash ^= (bits >> (byte * 8)) & 255U;
          hash *= 1099511628211ULL;
        }
      }
    if (repeat > 0 && hash != expected) return 3;
    expected = hash;
  }
  long rss = 0;
#if defined(__unix__) || defined(__APPLE__)
  struct rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) rss = usage.ru_maxrss;
#if !defined(__APPLE__)
  rss *= 1024;
#endif
#endif
  std::ofstream output(argv[1]);
  output << "{\"mode\":\"" << requested_mode << "\",\"width\":" << width << ",\"height\":" << height
         << ",\"masked\":" << (masked ? "true" : "false") << ",\"hash\":\"" << expected
         << "\",\"mask_mode\":" << mask_mode
         << ",\"peak_rss_bytes\":" << rss << ",\"ms\":[";
  for (std::size_t i = 0; i < times.size(); ++i) output << (i ? "," : "") << times[i];
  output << "]}\n";
  return output.good() ? 0 : 1;
}

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>

using namespace photo_editor::pipeline;
namespace {
ColorPixel reference_over(ColorPixel base, ColorPixel source, double opacity, bool multiply) {
  const double sa = source.alpha * opacity, ba = base.alpha;
  if (sa == 0) return base;
  const auto alpha = sa + ba * (1 - sa);
  const auto channel = [&](double b, double s) {
    const double blend = multiply ? b * s : s;
    return static_cast<float>((sa * (1 - ba) * s + sa * ba * blend + (1 - sa) * ba * b) / alpha);
  };
  return {channel(base.red, source.red), channel(base.green, source.green),
      channel(base.blue, source.blue), static_cast<float>(alpha)};
}
double difference(ColorPixel a, ColorPixel b) {
  return std::max({std::abs(a.red - b.red), std::abs(a.green - b.green),
      std::abs(a.blue - b.blue), std::abs(a.alpha - b.alpha)});
}
}
int main(int argc, char** argv) {
  const bool native = argc == 2 && std::string_view(argv[1]) == "--metal";
  if (argc > 1 && !native) return 1;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  std::unique_ptr<metal::MetalPipelineBackend> backend;
  if (native) {
    backend = std::make_unique<metal::MetalPipelineBackend>();
    if (!backend->available()) { std::cerr << backend->unavailable_reason(); return 1; }
  }
#else
  if (native) return 1;
#endif
  const auto check_gpu = [&](const ColorAdjustmentState& state, ColorPixel base, ColorPixel expected) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    if (backend) {
      const auto descriptor = RenderSurfaceDescriptor{.pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
          .color_domain = RenderColorDomain::WorkingScene, .range = RenderRangeModel::SceneExtended,
          .alpha = RenderAlphaModel::Straight, .color_space_identity = state.color_pipeline.working_profile.color_space.name,
          .transfer_identity = "linear", .width = 1, .height = 1};
      const std::vector<ColorPixel> input{base};
      ColorAdjustmentTelemetry telemetry;
      const auto surface = backend->publish_masks_and_color_lut_surface(input, 1, 1, state, {}, descriptor,
          &telemetry, {}, 1.0, false);
      std::vector<ColorPixel> actual;
      if (!surface || !surface->copy_pixels(&actual) || actual.size() != 1 || difference(actual[0], expected) > 2e-6) {
        std::cerr << "Native alpha composite mismatch: " << telemetry.gpu_fallback_reason << '\n'; return false;
      }
    }
#else
    (void)state; (void)base; (void)expected;
#endif
    return true;
  };
  bool ok = true;
  int cases = 0, failures = 0;
  double worst = 0;
  for (const auto* profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"}) {
    // No hidden backdrop RGB may contaminate any blend mode when alpha is zero.
    for (const auto* mode : {"normal", "multiply", "screen", "difference", "lighten", "darken", "overlay",
        "soft-light", "hard-light", "linear-light", "hue", "saturation", "color", "luminosity"}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      PhotoLayer layer; layer.kind = "fill"; layer.id = "transparent-backdrop";
      layer.fill_color = {0.8F, 0.4F, 0.2F, 0.5F}; layer.opacity = 0.5; layer.blend_mode = mode;
      state.photo_layers = {layer};
      const ColorPixel base{4.0F, -1.0F, 7.0F, 0}, expected{0.8F, 0.4F, 0.2F, 0.25F};
      auto pixels = std::vector<ColorPixel>{base}, retained = pixels;
      const auto program = compile_fill_layer_program(state, 1, 1);
      if (!apply_color_layers_stage(&pixels, 1, 1, state) || !program.ready() ||
          !apply_compiled_fill_layer_program(&retained, program) || difference(pixels[0], expected) > 2e-6 ||
          difference(retained[0], expected) > 2e-6 || !check_gpu(state, base, expected)) return 1;
    }
    // Alpha composition must not clamp scene-linear negative or HDR channels.
    for (float ba : {0.0F, 0.25F, 1.0F}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      PhotoLayer layer; layer.kind = "fill"; layer.id = "hdr-alpha"; layer.fill_color = {-0.25F, 2.0F, 4.0F, 0.5F};
      state.photo_layers = {layer};
      const ColorPixel base{0.5F, 0.25F, 0.125F, ba};
      const auto expected = reference_over(base, layer.fill_color, 1, false);
      auto pixels = std::vector<ColorPixel>{base}, retained = pixels;
      const auto program = compile_fill_layer_program(state, 1, 1);
      if (!apply_color_layers_stage(&pixels, 1, 1, state) || !program.ready() ||
          !apply_compiled_fill_layer_program(&retained, program) || difference(pixels[0], expected) > 2e-6 ||
          difference(retained[0], expected) > 2e-6 || !check_gpu(state, base, expected)) return 1;
    }
    for (const auto* kind : {"fill", "pixel"}) for (bool multiply : {false, true})
      for (float ba : {0.0F, 0.25F, 0.75F, 1.0F}) for (float sa : {0.0F, 0.25F, 0.5F, 1.0F})
        for (double opacity : {0.0, 0.25, 0.5, 1.0}) for (int mask : {-1, 0, 128, 255}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      PhotoLayer layer; layer.kind = kind; layer.id = "alpha"; layer.opacity = opacity;
      layer.blend_mode = multiply ? "multiply" : "normal";
      const ColorPixel base{0.15F, 0.35F, 0.65F, ba}, source{0.8F, 0.4F, 0.2F, sa};
      layer.fill_color = source; layer.pixel_width = layer.pixel_height = 1; layer.pixels = {source};
      double mask_weight = 1;
      if (mask >= 0) {
        layer.mask_width = layer.mask_height = 1; layer.mask_data = {static_cast<std::uint8_t>(mask)};
        layer.mask_density = 0.8; layer.mask_opacity = 0.75;
        mask_weight = std::min(mask / 255.0, 0.8) * 0.75;
      }
      state.photo_layers = {layer};
      const auto expected = reference_over(base, source, opacity * mask_weight, multiply);
      auto legacy = std::vector<ColorPixel>{base}, retained = legacy;
      const auto program = compile_fill_layer_program(state, 1, 1);
      if (!apply_color_layers_stage(&legacy, 1, 1, state) || !program.ready() ||
          !apply_compiled_fill_layer_program(&retained, program)) return 1;
      const auto delta = std::max(difference(legacy[0], expected), difference(retained[0], expected));
      worst = std::max(worst, delta); ++cases;
      if (delta > 2e-6) { ++failures; ok = false; }
      if (!check_gpu(state, base, expected)) return 1;
    }
    for (const auto* mode : {"normal", "multiply", "screen", "overlay", "color"}) {
      ColorAdjustmentState state;
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      PhotoLayer adjustment; adjustment.kind = "adjustment"; adjustment.id = "adjustment";
      adjustment.opacity = 0.5; adjustment.exposure = 1; adjustment.blend_mode = mode;
      state.photo_layers = {adjustment};
      auto pixels = std::vector<ColorPixel>{{0.15F, 0.35F, 0.65F, 0.25F}};
      auto retained = pixels;
      const auto program = compile_fill_layer_program(state, 1, 1);
      if (!apply_color_layers_stage(&pixels, 1, 1, state) || !program.ready() ||
          !apply_compiled_fill_layer_program(&retained, program)) return 1;
      if (pixels[0].alpha != 0.25F || retained[0].alpha != 0.25F) {
        std::cerr << profile << '/' << mode << " adjustment altered alpha: " << pixels[0].alpha << '/' << retained[0].alpha << '\n';
        ok = false;
      }
      if (!check_gpu(state, {0.15F, 0.35F, 0.65F, 0.25F}, retained[0])) return 1;
    }
  }
  std::cout << "Independent straight-alpha layer cases=" << cases << " failures=" << failures << " worst=" << worst
            << " native=" << native << '\n';
  return ok ? 0 : 1;
}

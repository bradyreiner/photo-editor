#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <string_view>

using namespace photo_editor::pipeline;
namespace {
// Independent separable tent reconstruction: sum every source texel's
// associated contribution. Outside the finite layer is transparent, not clamped.
ColorPixel reference(const PhotoLayer& layer, int x, int y, ColorPixel base) {
  const double radians = -layer.transform_rotation * std::numbers::pi / 180.0;
  const double dx = x + 0.5 - layer.x - layer.pixel_width * layer.transform_scale / 2;
  const double dy = y + 0.5 - layer.y - layer.pixel_height * layer.transform_scale / 2;
  const double sx = (dx * std::cos(radians) - dy * std::sin(radians)) / layer.transform_scale + layer.pixel_width / 2.0 - 0.5;
  const double sy = (dx * std::sin(radians) + dy * std::cos(radians)) / layer.transform_scale + layer.pixel_height / 2.0 - 0.5;
  const double radius = std::max(1.0, 1.0 / layer.transform_scale);
  double nx = 0, ny = 0;
  for (int i = static_cast<int>(std::ceil(sx-radius)); i <= static_cast<int>(std::floor(sx+radius)); ++i)
    nx += std::max(0.0, 1-std::abs(sx-i)/radius);
  for (int i = static_cast<int>(std::ceil(sy-radius)); i <= static_cast<int>(std::floor(sy+radius)); ++i)
    ny += std::max(0.0, 1-std::abs(sy-i)/radius);
  double r = 0, g = 0, b = 0, a = 0;
  for (int iy = 0; iy < layer.pixel_height; ++iy) for (int ix = 0; ix < layer.pixel_width; ++ix) {
    const auto& p = layer.pixels[static_cast<std::size_t>(iy * layer.pixel_width + ix)];
    const double w = std::max(0.0, 1 - std::abs(sx - ix)/radius) *
        std::max(0.0, 1 - std::abs(sy - iy)/radius) * p.alpha / (nx*ny);
    r += w * p.red; g += w * p.green; b += w * p.blue; a += w;
  }
  double opacity = layer.opacity;
  if (!layer.mask_data.empty()) opacity *= 128.0 / 255.0;
  const double sa = a * opacity;
  if (sa <= 1e-6) return base;
  const double ba = base.alpha * (1 - sa), alpha = sa + ba;
  return {static_cast<float>((r * opacity + base.red * ba) / alpha),
      static_cast<float>((g * opacity + base.green * ba) / alpha),
      static_cast<float>((b * opacity + base.blue * ba) / alpha), static_cast<float>(alpha)};
}
double error(ColorPixel a, ColorPixel b) {
  // RGB is meaningful only where covered. Keep a separate strict alpha check.
  return std::max({std::abs(a.red * a.alpha - b.red * b.alpha),
      std::abs(a.green * a.alpha - b.green * b.alpha),
      std::abs(a.blue * a.alpha - b.blue * b.alpha), std::abs(a.alpha - b.alpha)});
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
  constexpr int width = 28, height = 24;
  int cases = 0, failures = 0;
  double worst = 0;
  for (const auto* profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"})
    for (const auto* kind : {"pixel", "source", "text"})
      for (double scale : {0.05, 0.12, 0.25, 0.65, 1.0, 1.3, 2.4, 7.0, 32.0})
        for (double rotation : {0.0, 13.0, -37.0, 90.0, 180.0})
          for (float alpha : {0.0F, 0.35F, 1.0F}) for (bool mask : {false, true}) {
    ColorAdjustmentState state;
    state.color_pipeline.working_profile = working_color_profile_from_name(profile);
    PhotoLayer layer;
    layer.kind = kind; layer.id = "sampling"; layer.pixel_width = 3; layer.pixel_height = 2;
    layer.pixels = {{-0.2F, 2.0F, 0.3F, 1.0F}, {40, -20, 70, 0}, {0.3F, 0.2F, 1.8F, 0.5F},
                    {0.8F, 0.3F, 0.1F, 0.25F}, {0.2F, 0.9F, 0.5F, 0.8F}, {-30, 50, 20, 0}};
    layer.x = -1; layer.y = 3; layer.transform_scale = scale; layer.transform_rotation = rotation;
    layer.opacity = 0.7;
    if (mask) { layer.mask_width = 1; layer.mask_height = 1; layer.mask_data = {128}; }
    state.photo_layers = {layer};
    const ColorPixel base{0.15F, 0.25F, 0.1F, alpha};
    const std::vector<ColorPixel> input(static_cast<std::size_t>(width * height), base);
    auto cpu = input, compiled = input;
    const auto program = compile_fill_layer_program(state, width, height);
    if (!apply_color_layers_stage(&cpu, width, height, state) || !program.ready() ||
        !apply_compiled_fill_layer_program(&compiled, program)) return 1;
    std::vector<ColorPixel> gpu;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    if (backend) {
      const auto descriptor = RenderSurfaceDescriptor{.pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
          .color_domain = RenderColorDomain::WorkingScene, .range = RenderRangeModel::SceneExtended,
          .alpha = RenderAlphaModel::Straight, .color_space_identity = state.color_pipeline.working_profile.color_space.name,
          .transfer_identity = "linear", .width = width, .height = height};
      ColorAdjustmentTelemetry telemetry;
      const auto surface = backend->publish_masks_and_color_lut_surface(input, width, height, state, {}, descriptor,
          &telemetry, {}, 1.0, false);
      if (!surface || !surface->copy_pixels(&gpu) || gpu.size() != input.size()) {
        std::cerr << "Native sampling failed: " << telemetry.gpu_fallback_reason << '\n'; return 1;
      }
    }
#endif
    double case_error = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
      const auto i = static_cast<std::size_t>(y * width + x);
      const auto expected = reference(layer, x, y, base);
      case_error = std::max({case_error, error(cpu[i], expected), error(compiled[i], expected)});
      if (!gpu.empty()) case_error = std::max(case_error, error(gpu[i], expected));
    }
    if (case_error > 2e-5) ++failures;
    worst = std::max(worst, case_error); ++cases;
  }
  std::cout << "Independent layer reconstruction cases=" << cases << " failures=" << failures
            << " max_associated_or_alpha_error=" << worst << " native=" << native << '\n';
  // An even-width tent footprint cancels a one-pixel checker exactly in the
  // interior, at any subpixel phase. Compare the old four-tap reconstruction
  // explicitly to this independent analytic gray target, including rotations.
  double old_squared = 0, new_squared = 0;
  int checked = 0;
  for (double scale : {0.05, 0.1, 0.25}) for (double rotation : {0.0, 13.0, -37.0}) {
    PhotoLayer layer; layer.id = "checker"; layer.kind = "pixel";
    layer.pixel_width = 128; layer.pixel_height = 128; layer.x = 8; layer.y = 8;
    layer.transform_scale = scale; layer.transform_rotation = rotation;
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
      const auto v = static_cast<float>((x+y)%2); layer.pixels.push_back({v,v,v,1});
    }
    ColorAdjustmentState state; state.photo_layers = {layer};
    std::vector<ColorPixel> pixels(96*96, ColorPixel{0,0,0,0});
    if (!apply_color_layers_stage(&pixels, 96, 96, state)) return 1;
    const double radians = -rotation*std::numbers::pi/180, radius = 1/scale;
    for (int y = 0; y < 96; ++y) for (int x = 0; x < 96; ++x) {
      const double dx = x+0.5-8-64*scale, dy = y+0.5-8-64*scale;
      const double sx = (dx*std::cos(radians)-dy*std::sin(radians))/scale+63.5;
      const double sy = (dx*std::sin(radians)+dy*std::cos(radians))/scale+63.5;
      if (sx < radius || sy < radius || sx > 127-radius || sy > 127-radius) continue;
      const int ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
      const double fx = sx-ix, fy = sy-iy;
      const double old = ((ix+iy)%2)*(1-fx)*(1-fy) + ((ix+iy+1)%2)*(fx*(1-fy)+(1-fx)*fy)
          + ((ix+iy+2)%2)*fx*fy;
      const auto p = pixels[static_cast<std::size_t>(y*96+x)];
      old_squared += (old-0.5)*(old-0.5); new_squared += (p.red-0.5)*(p.red-0.5); ++checked;
      if (std::abs(p.alpha-1) > 2e-6) return 1;
    }
  }
  const auto old_rmse = std::sqrt(old_squared/std::max(1,checked));
  const auto new_rmse = std::sqrt(new_squared/std::max(1,checked));
  std::cout << "Checker interior pixels=" << checked << " old_bilinear_RMSE=" << old_rmse
            << " filtered_RMSE=" << new_rmse << '\n';
  if (checked < 100 || old_rmse < 0.02 || new_rmse > 1e-5) return 1;
  return failures ? 1 : 0;
}

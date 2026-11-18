#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"

#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
  using namespace photo_editor::pipeline;
  metal::MetalPipelineBackend backend;
  if (backend.available() || backend.unavailable_reason() != "metal_not_compiled" ||
      metal::shared_metal_pipeline_backend().available()) return EXIT_FAILURE;
  const std::vector<ColorPixel> original{{0.25F, -0.5F, 3.0F, 0.37F}};
  auto pixels = original;
  ColorAdjustmentTelemetry telemetry;
  ColorAdjustmentState state;
  ColorLut3D lut;
  if (backend.apply_masks_and_color_lut(&pixels, 1, 1, state, lut, &telemetry) ||
      pixels.size() != original.size() || pixels[0].red != original[0].red ||
      pixels[0].green != original[0].green || pixels[0].blue != original[0].blue ||
      pixels[0].alpha != original[0].alpha || telemetry.gpu_kernel_dispatch_count != 0 ||
      telemetry.gpu_fallback_reason != "metal_not_compiled") return EXIT_FAILURE;
  RenderSurfaceDescriptor descriptor;
  descriptor.width = 1;
  descriptor.height = 1;
  if (backend.upload_working_surface(pixels, descriptor, &telemetry) ||
      backend.publish_masks_and_color_lut_surface(pixels, 1, 1, state, lut, descriptor, &telemetry) ||
      !backend.gaussian_approximate_blur({1.0F}, 1, 1, 1).empty() ||
      backend.color_lut_cache_stats().entries != 0 || backend.color_lut_cache_stats().bytes != 0)
    return EXIT_FAILURE;
  std::cout << "Unavailable Metal backend declines work without modifying CPU pixels\n";
  return EXIT_SUCCESS;
}

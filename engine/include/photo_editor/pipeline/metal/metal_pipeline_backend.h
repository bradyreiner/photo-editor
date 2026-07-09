#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <memory>
#include <string>
#include <vector>

namespace photo_editor::pipeline::metal {

class MetalPipelineBackend final {
 public:
  MetalPipelineBackend();
  ~MetalPipelineBackend();

  MetalPipelineBackend(const MetalPipelineBackend&) = delete;
  MetalPipelineBackend& operator=(const MetalPipelineBackend&) = delete;

  [[nodiscard]] bool available() const;
  [[nodiscard]] std::string unavailable_reason() const;

  void apply_color_lut(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      const ColorLut3D& lut);

  void apply_masks(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      const ColorAdjustmentState& state);

  void apply_masks_and_color_lut(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      const ColorAdjustmentState& state,
      const ColorLut3D& lut);

  [[nodiscard]] std::vector<float> gaussian_approximate_blur(
      const std::vector<float>& input,
      int width,
      int height,
      int radius);

  [[nodiscard]] bool apply_detail_effects(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      double sharpen_strength,
      double sharpen_radius,
      double sharpen_detail,
      double sharpen_mask_strength,
      double luma_nr_strength,
      double color_nr_strength,
      ColorAdjustmentTelemetry* telemetry = nullptr);

  [[nodiscard]] bool apply_spatial_tone_effects(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      double texture_strength,
      double clarity_strength,
      double dehaze_strength,
      ColorAdjustmentTelemetry* telemetry = nullptr,
      CancellationToken cancellation = {});

  [[nodiscard]] bool apply_post_color_effects(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      const ColorAdjustmentState& state);

  [[nodiscard]] bool apply_pre_tone_effects(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      const ColorAdjustmentState& state,
      const std::vector<float>* local_mask_weights = nullptr,
      double local_mask_exposure = 0.0,
      double local_mask_temperature = 0.0,
      CancellationToken cancellation = {});

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace photo_editor::pipeline::metal

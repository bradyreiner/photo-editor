#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <memory>
#include <string>
#include <vector>

namespace photo_editor::pipeline::cuda {

// CUDA acceleration for pixel-parallel preview stages. Operations are
// transactional: callers retain their original pixels when a CUDA operation
// fails, so the CPU reference implementation can always take over.
class CudaPipelineBackend final {
 public:
  CudaPipelineBackend();
  ~CudaPipelineBackend();

  CudaPipelineBackend(const CudaPipelineBackend&) = delete;
  CudaPipelineBackend& operator=(const CudaPipelineBackend&) = delete;

  [[nodiscard]] bool available() const;
  [[nodiscard]] std::string unavailable_reason() const;
  [[nodiscard]] std::string device_name() const;

  [[nodiscard]] bool apply_scene_exposure(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      double exposure_gain,
      ColorAdjustmentTelemetry* telemetry = nullptr,
      CancellationToken cancellation = {});

  [[nodiscard]] bool apply_aces2_output_lut_target(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      const CompiledAces2OutputLutApplication& application,
      ColorAdjustmentTelemetry* telemetry = nullptr,
      CancellationToken cancellation = {});

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] CudaPipelineBackend& shared_cuda_pipeline_backend();

}  // namespace photo_editor::pipeline::cuda

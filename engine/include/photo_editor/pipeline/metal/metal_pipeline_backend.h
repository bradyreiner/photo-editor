#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_lut_program.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/ocio_project_config.h"
#include "photo_editor/pipeline/output_rendering.h"
#include "photo_editor/pipeline/render_executor.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace photo_editor::pipeline::metal {

struct MetalColorLutCacheStats final {
  std::size_t entries = 0U;
  std::uint64_t bytes = 0U;
  std::size_t maximum_entries = 0U;
  std::uint64_t maximum_bytes = 0U;
};

enum class MetalOutputSurfaceFormat {
  Bgra8UnormSrgbEncoded,
};

class MetalWorkingSurface final : public RenderSurfaceBacking {
 public:
  ~MetalWorkingSurface() override;

  MetalWorkingSurface(const MetalWorkingSurface&) = delete;
  MetalWorkingSurface& operator=(const MetalWorkingSurface&) = delete;

  [[nodiscard]] std::string_view backend_identity() const noexcept override;
  [[nodiscard]] std::uint64_t byte_size() const noexcept override;
  [[nodiscard]] bool valid_for(
      const RenderSurfaceDescriptor& descriptor) const noexcept override;
  [[nodiscard]] bool copy_pixels(
      std::vector<ColorPixel>* destination,
      CancellationToken cancellation = {}) const override;
  [[nodiscard]] RenderSurfaceDescriptor descriptor() const;
  [[nodiscard]] std::uintptr_t native_buffer_handle() const;
  [[nodiscard]] std::uint64_t device_registry_id() const;
  [[nodiscard]] std::uintptr_t native_ready_event_handle() const;
  [[nodiscard]] std::uint64_t ready_event_value() const;
  [[nodiscard]] bool producer_completed_successfully() const;

 private:
  struct Impl;
  explicit MetalWorkingSurface(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;

  friend class MetalPipelineBackend;
};

class MetalOutputSurface final : public RenderSurfaceBacking {
 public:
  ~MetalOutputSurface();

  MetalOutputSurface(const MetalOutputSurface&) = delete;
  MetalOutputSurface& operator=(const MetalOutputSurface&) = delete;

  [[nodiscard]] std::string_view backend_identity() const noexcept override;
  [[nodiscard]] std::uint64_t byte_size() const noexcept override;
  [[nodiscard]] bool valid_for(
      const RenderSurfaceDescriptor& descriptor) const noexcept override;
  [[nodiscard]] bool copy_pixels(
      std::vector<ColorPixel>* destination,
      CancellationToken cancellation = {}) const override;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] MetalOutputSurfaceFormat format() const;
  [[nodiscard]] std::uintptr_t native_texture_handle() const;
  [[nodiscard]] std::uint64_t device_registry_id() const;
  [[nodiscard]] std::uintptr_t native_ready_event_handle() const;
  [[nodiscard]] std::uint64_t ready_event_value() const;
  [[nodiscard]] bool producer_completed_successfully() const;
  [[nodiscard]] bool copy_bgra8_pixels_for_testing(
      std::span<std::uint8_t> destination) const;

 private:
  struct Impl;
  explicit MetalOutputSurface(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;

  friend class MetalPipelineBackend;
};

class MetalPipelineBackend final {
 public:
  MetalPipelineBackend();
  ~MetalPipelineBackend();

  MetalPipelineBackend(const MetalPipelineBackend&) = delete;
  MetalPipelineBackend& operator=(const MetalPipelineBackend&) = delete;

  [[nodiscard]] bool available() const;
  [[nodiscard]] std::string unavailable_reason() const;
  [[nodiscard]] std::string device_name() const;
  [[nodiscard]] MetalColorLutCacheStats
  color_lut_cache_stats() const;
  [[nodiscard]] MetalColorLutCacheStats
  tone_lut_cache_stats() const;
  [[nodiscard]] MetalColorLutCacheStats
  curve_lut_cache_stats() const;

  [[nodiscard]] bool apply_aces2_output_lut_target(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      const CompiledAces2OutputLutApplication& application,
      ColorAdjustmentTelemetry* telemetry = nullptr);

  [[nodiscard]] std::shared_ptr<const MetalOutputSurface>
  publish_aces2_output_lut_srgb8_surface(
      std::span<const ColorPixel> pixels,
      int width,
      int height,
      const CompiledAces2OutputLutApplication& application,
      ColorAdjustmentTelemetry* telemetry = nullptr,
      CancellationToken cancellation = {},
      bool defer_completion = false);

  [[nodiscard]] std::shared_ptr<const MetalOutputSurface>
  publish_aces2_output_lut_srgb8_surface(
      const std::shared_ptr<const MetalWorkingSurface>& working_surface,
      const CompiledAces2OutputLutApplication& application,
      ColorAdjustmentTelemetry* telemetry = nullptr,
      CancellationToken cancellation = {},
      bool defer_completion = true);

  [[nodiscard]] std::shared_ptr<const MetalOutputSurface>
  publish_aces2_output_lut_srgb8_surface(
      const std::shared_ptr<const MetalWorkingSurface>& working_surface,
      int source_x,
      int source_y,
      int width,
      int height,
      const CompiledAces2OutputLutApplication& application,
      ColorAdjustmentTelemetry* telemetry = nullptr,
      CancellationToken cancellation = {},
      bool defer_completion = true);

  [[nodiscard]] std::shared_ptr<const MetalOutputSurface>
  publish_aces2_output_lut_icc_bgra8_surface(
      const std::shared_ptr<const MetalWorkingSurface>& working_surface,
      const CompiledAces2OutputLutApplication& application,
      const IccTransformLut3D& device_lut,
      const CompiledIccTransform& exact_device_transform,
      const RenderSurfaceDescriptor& publish_output,
      ColorAdjustmentTelemetry* telemetry = nullptr,
      CancellationToken cancellation = {},
      bool defer_completion = true);

  [[nodiscard]] bool apply_icc_transform_lut(
      std::span<IccRgbaFloat> pixels,
      int width,
      int height,
      const IccTransformLut3D& lut,
      const CompiledIccTransform& exact_transform,
      IccTransformLutApplyStats* apply_stats = nullptr,
      ColorAdjustmentTelemetry* telemetry = nullptr);

  [[nodiscard]] bool apply_ocio_project_gpu_program(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      const OcioProjectGpuProgram& program,
      std::string* failure_reason = nullptr,
      ColorAdjustmentTelemetry* telemetry = nullptr);

  [[nodiscard]] bool apply_masks_and_color_lut(
      std::vector<ColorPixel>* pixels,
      int width,
      int height,
      const ColorAdjustmentState& state,
      const ColorLut3D& lut,
      ColorAdjustmentTelemetry* telemetry = nullptr,
      CancellationToken cancellation = {});

  [[nodiscard]] std::shared_ptr<const MetalWorkingSurface>
  publish_masks_and_color_lut_surface(
      std::span<const ColorPixel> pixels,
      int width,
      int height,
      const ColorAdjustmentState& state,
      const ColorLut3D& lut,
      const RenderSurfaceDescriptor& descriptor,
      ColorAdjustmentTelemetry* telemetry = nullptr,
      CancellationToken cancellation = {},
      double scene_exposure_gain = 1.0,
      bool apply_color_lut = true,
      const CompiledToneStageLut* tone_lut = nullptr,
      const CompiledCurveStageLuts* curve_luts = nullptr,
      const CompiledLookStageProgram* look_program = nullptr,
      const CompiledUserLutStageProgram* user_lut_program = nullptr);

  [[nodiscard]] std::vector<float> gaussian_approximate_blur(
      const std::vector<float>& input,
      int width,
      int height,
      int radius);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] MetalPipelineBackend& shared_metal_pipeline_backend();

}  // namespace photo_editor::pipeline::metal

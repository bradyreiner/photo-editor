#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"

#include <utility>

namespace photo_editor::pipeline::metal {
namespace {
void mark_unavailable(ColorAdjustmentTelemetry* telemetry) {
  if (telemetry != nullptr) telemetry->gpu_fallback_reason = "metal_not_compiled";
}
}  // namespace

// Keep the public backend API linkable on platforms/builds without Metal.
// These methods decline work; they never claim a GPU dispatch or mutate pixels.
struct MetalWorkingSurface::Impl {};
struct MetalOutputSurface::Impl {};
struct MetalPipelineBackend::Impl {};

MetalWorkingSurface::MetalWorkingSurface(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
MetalWorkingSurface::~MetalWorkingSurface() = default;
std::string_view MetalWorkingSurface::backend_identity() const noexcept { return "metal"; }
std::uint64_t MetalWorkingSurface::byte_size() const noexcept { return 0; }
bool MetalWorkingSurface::valid_for(const RenderSurfaceDescriptor&) const noexcept { return false; }
bool MetalWorkingSurface::copy_pixels(std::vector<ColorPixel>*, CancellationToken) const { return false; }
bool MetalWorkingSurface::copy_pixels_validates_finite() const noexcept { return false; }
RenderSurfaceDescriptor MetalWorkingSurface::descriptor() const { return {}; }
bool MetalWorkingSurface::copy_sampled_pixels(int, int, std::vector<ColorPixel>*, CancellationToken) const { return false; }
std::uintptr_t MetalWorkingSurface::native_buffer_handle() const { return 0; }
std::uint64_t MetalWorkingSurface::device_registry_id() const { return 0; }
std::uintptr_t MetalWorkingSurface::native_ready_event_handle() const { return 0; }
std::uint64_t MetalWorkingSurface::ready_event_value() const { return 0; }
bool MetalWorkingSurface::producer_completed_successfully() const { return false; }

MetalOutputSurface::MetalOutputSurface(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
MetalOutputSurface::~MetalOutputSurface() = default;
std::string_view MetalOutputSurface::backend_identity() const noexcept { return "metal"; }
std::uint64_t MetalOutputSurface::byte_size() const noexcept { return 0; }
bool MetalOutputSurface::valid_for(const RenderSurfaceDescriptor&) const noexcept { return false; }
bool MetalOutputSurface::copy_pixels(std::vector<ColorPixel>*, CancellationToken) const { return false; }
bool MetalOutputSurface::copy_pixels_validates_finite() const noexcept { return false; }
int MetalOutputSurface::width() const { return 0; }
int MetalOutputSurface::height() const { return 0; }
MetalOutputSurfaceFormat MetalOutputSurface::format() const { return MetalOutputSurfaceFormat::Bgra8UnormSrgbEncoded; }
std::uintptr_t MetalOutputSurface::native_texture_handle() const { return 0; }
std::uint64_t MetalOutputSurface::device_registry_id() const { return 0; }
std::uintptr_t MetalOutputSurface::native_ready_event_handle() const { return 0; }
std::uint64_t MetalOutputSurface::ready_event_value() const { return 0; }
bool MetalOutputSurface::producer_completed_successfully() const { return false; }
bool MetalOutputSurface::copy_bgra8_region(int, int, int, int, std::span<std::uint8_t>, CancellationToken) const { return false; }
bool MetalOutputSurface::copy_bgra8_pixels_for_testing(std::span<std::uint8_t>) const { return false; }

MetalPipelineBackend::MetalPipelineBackend() = default;
MetalPipelineBackend::~MetalPipelineBackend() = default;
bool MetalPipelineBackend::available() const { return false; }
std::string MetalPipelineBackend::unavailable_reason() const { return "metal_not_compiled"; }
std::string MetalPipelineBackend::device_name() const { return {}; }
MetalColorLutCacheStats MetalPipelineBackend::color_lut_cache_stats() const { return {}; }
MetalColorLutCacheStats MetalPipelineBackend::tone_lut_cache_stats() const { return {}; }
MetalColorLutCacheStats MetalPipelineBackend::curve_lut_cache_stats() const { return {}; }

std::shared_ptr<const MetalWorkingSurface> MetalPipelineBackend::upload_working_surface(
    std::span<const ColorPixel>, const RenderSurfaceDescriptor&, ColorAdjustmentTelemetry* telemetry, CancellationToken) {
  mark_unavailable(telemetry);
  return {};
}
bool MetalPipelineBackend::apply_aces2_output_lut_target(
    std::vector<ColorPixel>*, int, int, const CompiledAces2OutputLutApplication&, ColorAdjustmentTelemetry* telemetry) {
  mark_unavailable(telemetry);
  return false;
}
std::shared_ptr<const MetalOutputSurface> MetalPipelineBackend::publish_aces2_output_lut_srgb8_surface(
    std::span<const ColorPixel>, int, int, const CompiledAces2OutputLutApplication&,
    ColorAdjustmentTelemetry* telemetry, CancellationToken, bool) {
  mark_unavailable(telemetry);
  return {};
}
std::shared_ptr<const MetalOutputSurface> MetalPipelineBackend::publish_aces2_output_lut_srgb8_surface(
    const std::shared_ptr<const MetalWorkingSurface>&, const CompiledAces2OutputLutApplication&,
    ColorAdjustmentTelemetry* telemetry, CancellationToken, bool) {
  mark_unavailable(telemetry);
  return {};
}
std::shared_ptr<const MetalOutputSurface> MetalPipelineBackend::publish_aces2_output_lut_srgb8_surface(
    const std::shared_ptr<const MetalWorkingSurface>&, int, int, int, int, const CompiledAces2OutputLutApplication&,
    ColorAdjustmentTelemetry* telemetry, CancellationToken, bool) {
  mark_unavailable(telemetry);
  return {};
}
std::shared_ptr<const MetalOutputSurface> MetalPipelineBackend::publish_aces2_output_lut_icc_bgra8_surface(
    const std::shared_ptr<const MetalWorkingSurface>&, const CompiledAces2OutputLutApplication&,
    const IccTransformLut3D&, const CompiledIccTransform&, const RenderSurfaceDescriptor&,
    ColorAdjustmentTelemetry* telemetry, CancellationToken, bool) {
  mark_unavailable(telemetry);
  return {};
}
bool MetalPipelineBackend::apply_icc_transform_lut(
    std::span<IccRgbaFloat>, int, int, const IccTransformLut3D&, const CompiledIccTransform&,
    IccTransformLutApplyStats*, ColorAdjustmentTelemetry* telemetry) {
  mark_unavailable(telemetry);
  return false;
}
bool MetalPipelineBackend::apply_ocio_project_gpu_program(
    std::vector<ColorPixel>*, int, int, const OcioProjectGpuProgram&, std::string* failure_reason,
    ColorAdjustmentTelemetry* telemetry) {
  mark_unavailable(telemetry);
  if (failure_reason != nullptr) *failure_reason = "metal_not_compiled";
  return false;
}
bool MetalPipelineBackend::apply_masks_and_color_lut(
    std::vector<ColorPixel>*, int, int, const ColorAdjustmentState&, const ColorLut3D&,
    ColorAdjustmentTelemetry* telemetry, CancellationToken) {
  mark_unavailable(telemetry);
  return false;
}
std::shared_ptr<const MetalWorkingSurface> MetalPipelineBackend::publish_masks_and_color_lut_surface(
    std::span<const ColorPixel>, int, int, const ColorAdjustmentState&, const ColorLut3D&,
    const RenderSurfaceDescriptor&, ColorAdjustmentTelemetry* telemetry, CancellationToken,
    double, bool, const CompiledToneStageLut*, const CompiledCurveStageLuts*,
    const CompiledLookStageProgram*, const CompiledUserLutStageProgram*) {
  mark_unavailable(telemetry);
  return {};
}
std::vector<float> MetalPipelineBackend::gaussian_approximate_blur(const std::vector<float>&, int, int, int) { return {}; }

MetalPipelineBackend& shared_metal_pipeline_backend() {
  static MetalPipelineBackend backend;
  return backend;
}
}  // namespace photo_editor::pipeline::metal

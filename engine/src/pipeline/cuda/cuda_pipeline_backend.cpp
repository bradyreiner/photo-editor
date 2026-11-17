#include "photo_editor/pipeline/cuda/cuda_pipeline_backend.h"

#include "cuda_pipeline_kernels.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace photo_editor::pipeline::cuda {

namespace {

constexpr auto kCudaMessageCapacity = std::size_t{512U};

[[nodiscard]] PhotoEditorCudaAces2Uniforms make_uniforms(
    const CompiledAces2OutputLutApplication& application) {
  const auto& lut = *application.lut;
  const auto safe_black_headroom =
      std::clamp(application.black_point_headroom, 1.0, 16.0);
  const auto safe_display_headroom =
      std::clamp(application.display_headroom, 1.0, 100.0);
  const auto maximum_black =
      std::max(0.0, safe_black_headroom - 1.0e-6);
  const auto safe_source_black = std::clamp(
      application.source_black_point, 0.0, maximum_black);
  const auto safe_output_black = std::clamp(
      application.output_black_point, 0.0, maximum_black);
  auto uniforms = PhotoEditorCudaAces2Uniforms{};
  for (std::size_t index = 0U; index < 9U; ++index) {
    uniforms.working_to_acescg[index] =
        static_cast<float>(lut.working_to_acescg.values[index]);
  }
  uniforms.shaper_maximum = static_cast<float>(lut.shaper_maximum);
  uniforms.shaper_offset = static_cast<float>(lut.settings.shaper_offset);
  uniforms.maximum_physical_rgb =
      static_cast<float>(lut.maximum_physical_rgb);
  uniforms.physical_to_reference_white =
      static_cast<float>(lut.physical_to_reference_white);
  uniforms.source_black_point = static_cast<float>(safe_source_black);
  uniforms.output_black_point = static_cast<float>(safe_output_black);
  uniforms.black_point_headroom = static_cast<float>(safe_black_headroom);
  uniforms.display_headroom = static_cast<float>(safe_display_headroom);
  uniforms.black_point_compensation_enabled =
      application.black_point_compensation_enabled &&
              std::abs(safe_source_black - safe_output_black) > 1.0e-9
          ? 1
          : 0;
  uniforms.lut_size = lut.settings.cube_size;
  return uniforms;
}

}  // namespace

struct CudaPipelineBackend::Impl final {
  void* context = nullptr;
  std::string unavailable_reason;
  std::string device_name;

  ~Impl() {
    photo_editor_cuda_destroy(context);
  }
};

CudaPipelineBackend::CudaPipelineBackend()
    : impl_(std::make_unique<Impl>()) {
  auto error = std::array<char, kCudaMessageCapacity>{};
  auto device = std::array<char, kCudaMessageCapacity>{};
  impl_->context = photo_editor_cuda_create(
      error.data(), error.size(), device.data(), device.size());
  impl_->unavailable_reason = error.data();
  impl_->device_name = device.data();
  if (impl_->context == nullptr && impl_->unavailable_reason.empty()) {
    impl_->unavailable_reason = "cuda_backend_initialization_failed";
  }
}

CudaPipelineBackend::~CudaPipelineBackend() = default;

bool CudaPipelineBackend::available() const {
  return impl_ != nullptr && impl_->context != nullptr;
}

std::string CudaPipelineBackend::unavailable_reason() const {
  return impl_ != nullptr ? impl_->unavailable_reason
                          : "cuda_backend_not_initialized";
}

std::string CudaPipelineBackend::device_name() const {
  return impl_ != nullptr ? impl_->device_name : std::string{};
}

bool CudaPipelineBackend::apply_scene_exposure(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    double exposure_gain,
    ColorAdjustmentTelemetry* telemetry,
    CancellationToken cancellation) {
  const auto fail = [&](std::string reason) {
    if (telemetry != nullptr) {
      telemetry->gpu_fallback_reason = std::move(reason);
    }
    return false;
  };
  if (!available()) {
    return fail(unavailable_reason());
  }
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      !std::isfinite(exposure_gain) || exposure_gain <= 0.0 ||
      cancellation.cancelled()) {
    return fail(cancellation.cancelled()
                    ? "cuda_scene_exposure_cancelled"
                    : "invalid_cuda_scene_exposure_request");
  }
  const auto pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (pixels->size() != pixel_count) {
    return fail("cuda_scene_exposure_pixel_count_mismatch");
  }
  auto output = *pixels;
  auto error = std::array<char, kCudaMessageCapacity>{};
  const auto stage_start = std::chrono::steady_clock::now();
  if (photo_editor_cuda_apply_scene_exposure(
          impl_->context,
          reinterpret_cast<float*>(output.data()),
          output.size(),
          static_cast<float>(exposure_gain),
          error.data(),
          error.size()) == 0) {
    return fail(error.data()[0] != '\0'
                    ? std::string{error.data()}
                    : "cuda_scene_exposure_failed");
  }
  if (cancellation.cancelled() ||
      !std::all_of(output.cbegin(), output.cend(), [](const auto& pixel) {
        return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
               std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
      })) {
    return fail(cancellation.cancelled()
                    ? "cuda_scene_exposure_cancelled"
                    : "cuda_scene_exposure_validation_failed");
  }
  *pixels = std::move(output);
  if (telemetry != nullptr) {
    const auto pixel_bytes = static_cast<std::int64_t>(
        pixel_count * sizeof(ColorPixel));
    telemetry->gpu_fallback_reason.clear();
    telemetry->scene_adjustments_ms +=
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - stage_start)
            .count();
    telemetry->gpu_cpu_to_gpu_bytes += pixel_bytes;
    telemetry->gpu_gpu_to_cpu_bytes += pixel_bytes;
    telemetry->gpu_command_buffer_count += 1;
    telemetry->gpu_kernel_dispatch_count += 1;
  }
  return true;
}

bool CudaPipelineBackend::apply_aces2_output_lut_target(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const CompiledAces2OutputLutApplication& application,
    ColorAdjustmentTelemetry* telemetry,
    CancellationToken cancellation) {
  const auto fail = [&](std::string reason) {
    if (telemetry != nullptr) {
      telemetry->gpu_fallback_reason = std::move(reason);
    }
    return false;
  };
  if (!available()) {
    return fail(unavailable_reason());
  }
  if (pixels == nullptr || !application.ready() || width <= 0 || height <= 0 ||
      cancellation.cancelled()) {
    return fail(cancellation.cancelled()
                    ? "cuda_aces2_output_cancelled"
                    : "invalid_cuda_aces2_output_lut_request");
  }
  const auto pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (pixels->size() != pixel_count) {
    return fail("cuda_aces2_output_lut_pixel_count_mismatch");
  }
  if (!std::all_of(pixels->cbegin(), pixels->cend(), [](const auto& pixel) {
        return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
               std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
      })) {
    return fail("cuda_aces2_output_source_non_finite");
  }
  static_assert(sizeof(ColorPixel) == sizeof(float) * 4U);
  static_assert(sizeof(application.lut->table.front()) == sizeof(float) * 4U);
  auto output = *pixels;
  auto error = std::array<char, kCudaMessageCapacity>{};
  auto uploaded_lut_bytes = std::size_t{0U};
  const auto stage_start = std::chrono::steady_clock::now();
  const auto& lut = *application.lut;
  const auto lut_identity = static_cast<std::uint64_t>(
      std::hash<std::string>{}(lut.cache_key));
  const auto uniforms = make_uniforms(application);
  if (photo_editor_cuda_apply_aces2_output_lut(
          impl_->context,
          reinterpret_cast<float*>(output.data()),
          output.size(),
          reinterpret_cast<const float*>(lut.table.data()),
          lut.table.size(),
          lut_identity,
          &uniforms,
          &uploaded_lut_bytes,
          error.data(),
          error.size()) == 0) {
    return fail(error.data()[0] != '\0'
                    ? std::string{error.data()}
                    : "cuda_aces2_output_failed");
  }
  if (cancellation.cancelled()) {
    return fail("cuda_aces2_output_cancelled");
  }
  for (std::size_t index = 0U; index < pixel_count; ++index) {
    const auto& pixel = output[index];
    const auto expected_alpha = std::clamp(
        static_cast<double>((*pixels)[index].alpha), 0.0, 1.0);
    if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) ||
        !std::isfinite(pixel.blue) || !std::isfinite(pixel.alpha) ||
        std::abs(static_cast<double>(pixel.alpha) - expected_alpha) > 1.0e-6) {
      return fail("cuda_aces2_output_validation_failed");
    }
  }
  *pixels = std::move(output);
  if (telemetry != nullptr) {
    telemetry->gpu_fallback_reason.clear();
    telemetry->final_output_ms +=
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - stage_start)
            .count();
    telemetry->gpu_cpu_to_gpu_bytes += static_cast<std::int64_t>(
        pixel_count * sizeof(ColorPixel) + uploaded_lut_bytes);
    telemetry->gpu_gpu_to_cpu_bytes +=
        static_cast<std::int64_t>(pixel_count * sizeof(ColorPixel));
    telemetry->gpu_command_buffer_count += 1;
    telemetry->gpu_kernel_dispatch_count += 1;
  }
  return true;
}

CudaPipelineBackend& shared_cuda_pipeline_backend() {
  static CudaPipelineBackend backend;
  return backend;
}

}  // namespace photo_editor::pipeline::cuda

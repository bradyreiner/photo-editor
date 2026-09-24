#pragma once

#include "canonical_native_preview_suffix.h"

#include <string>
#include <utility>
#include <vector>

namespace photo_editor::desktop {

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
[[nodiscard]] inline photo_editor::pipeline::RenderFusionEvidence
canonical_suffix_fusion_evidence(
    std::string reference_identity,
    bool includes_device_transform) {
  auto stages = std::vector<photo_editor::pipeline::RenderStageKind>{
      photo_editor::pipeline::RenderStageKind::PhotographicOutput,
  };
  if (includes_device_transform) {
    stages.push_back(photo_editor::pipeline::RenderStageKind::DeviceTransform);
  }
  stages.push_back(photo_editor::pipeline::RenderStageKind::Publish);
  return {
      .unfused_reference_passed = true,
      .unfused_reference_identity = std::move(reference_identity),
      .logical_stages_covered = std::move(stages),
  };
}

[[nodiscard]] inline photo_editor::pipeline::RenderStageExecution
publish_canonical_native_suffix(
    const photo_editor::pipeline::RenderStageInvocation& invocation,
    const photo_editor::pipeline::RenderSurfaceDescriptor& publish_output,
    std::optional<photo_editor::pipeline::RenderSurface> input,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    photo_editor::pipeline::Aces2OutputLutQuality quality,
    photo_editor::pipeline::ColorAdjustmentTelemetry* telemetry,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (!input.has_value() || cancellation.cancelled()) {
    return {.failure_reason = "canonical native suffix input is unavailable"};
  }
  const auto application = photo_editor::pipeline::compile_aces2_output_lut_application(
      color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
      quality);
  auto& metal = photo_editor::pipeline::metal::shared_metal_pipeline_backend();
  const auto retained = std::dynamic_pointer_cast<const
      photo_editor::pipeline::metal::MetalWorkingSurface>(input->backing);
  auto surface = retained != nullptr
                     ? metal.publish_aces2_output_lut_srgb8_surface(
                           retained, application, telemetry, cancellation, true)
                     : std::shared_ptr<const
                           photo_editor::pipeline::metal::MetalOutputSurface>{};
  if (surface == nullptr && input->ensure_cpu_pixels(cancellation)) {
    surface = metal.publish_aces2_output_lut_srgb8_surface(
        input->pixels,
        static_cast<int>(publish_output.width),
        static_cast<int>(publish_output.height),
        application,
        telemetry,
        cancellation,
        true);
  }
  if (surface != nullptr && surface->valid_for(publish_output)) {
    return {
        .output = photo_editor::pipeline::RenderSurface{
            .descriptor = publish_output,
            .backing = std::move(surface),
            .identity =
                "surface:canonical-native-publish:" + invocation.stage_cache_key,
        },
        .backend = "metal-aces2-publish-fused",
        .fusion_evidence = canonical_suffix_fusion_evidence(
            "metal-aces2-output-lut-parity:v1", false),
    };
  }

  const auto fallback_reason =
      telemetry != nullptr ? telemetry->gpu_fallback_reason : std::string{};
  if (!input->ensure_cpu_pixels(cancellation)) {
    return {.failure_reason =
                "canonical native PhotographicOutput-Publish fusion failed"};
  }
  auto pixels = std::move(input->pixels);
  photo_editor::pipeline::apply_aces2_output_lut_target_application(
      pixels, application);
  if (!photo_editor::pipeline::apply_color_publish_stage(
          &pixels,
          static_cast<int>(publish_output.width),
          static_cast<int>(publish_output.height),
          color_pipeline.preview_display_target,
          cancellation,
          telemetry)) {
    return {.failure_reason =
                "canonical PhotographicOutput-Publish CPU fallback failed"};
  }
  return {
      .output = photo_editor::pipeline::RenderSurface{
          .descriptor = publish_output,
          .pixels = std::move(pixels),
          .identity = "surface:canonical-cpu-publish:" + invocation.stage_cache_key,
      },
      .backend = "cpu-reference-fused-fallback",
      .fallback_reason = fallback_reason.empty()
                             ? "metal-native-publication-failed"
                             : fallback_reason,
      .fusion_evidence =
          canonical_suffix_fusion_evidence("cpu-reference-color-plan:v1", false),
  };
}

[[nodiscard]] inline photo_editor::pipeline::RenderStageExecution
publish_canonical_native_device_suffix(
    const photo_editor::pipeline::RenderStageInvocation& invocation,
    const photo_editor::pipeline::RenderSurfaceDescriptor& publish_output,
    std::optional<photo_editor::pipeline::RenderSurface> input,
    const photo_editor::pipeline::ColorPipelineConfig& output_pipeline,
    photo_editor::pipeline::Aces2OutputLutQuality quality,
    const std::shared_ptr<const photo_editor::pipeline::IccTransformLut3D>& device_lut,
    const photo_editor::pipeline::CompiledIccTransform& exact_device_transform,
    photo_editor::pipeline::ColorAdjustmentTelemetry* telemetry,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (!input.has_value() || device_lut == nullptr || cancellation.cancelled()) {
    return {.failure_reason = "canonical native device suffix input is unavailable"};
  }
  const auto application = photo_editor::pipeline::compile_aces2_output_lut_application(
      output_pipeline,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
      quality);
  const auto retained = std::dynamic_pointer_cast<const
      photo_editor::pipeline::metal::MetalWorkingSurface>(input->backing);
  auto& metal = photo_editor::pipeline::metal::shared_metal_pipeline_backend();
  auto surface = retained != nullptr
                     ? metal.publish_aces2_output_lut_icc_bgra8_surface(
                           retained,
                           application,
                           *device_lut,
                           exact_device_transform,
                           publish_output,
                           telemetry,
                           cancellation,
                           true)
                     : std::shared_ptr<const
                           photo_editor::pipeline::metal::MetalOutputSurface>{};
  if (surface != nullptr && surface->valid_for(publish_output)) {
    return {
        .output = photo_editor::pipeline::RenderSurface{
            .descriptor = publish_output,
            .backing = std::move(surface),
            .identity = "surface:canonical-native-device-publish:" +
                        invocation.stage_cache_key,
        },
        .backend = "metal-aces2-icc-publish-fused",
        .fusion_evidence =
            canonical_suffix_fusion_evidence("metal-icc-output-parity:v1", true),
    };
  }

  const auto fallback_reason =
      telemetry != nullptr ? telemetry->gpu_fallback_reason : std::string{};
  if (!input->ensure_cpu_pixels(cancellation)) {
    return {.failure_reason = "canonical native device suffix materialization failed"};
  }
  auto pixels = std::move(input->pixels);
  photo_editor::pipeline::apply_aces2_output_lut_target_application(
      pixels, application);
  auto device_pixels = std::vector<photo_editor::pipeline::IccRgbaFloat>{};
  device_pixels.reserve(pixels.size());
  for (const auto& pixel : pixels) {
    device_pixels.push_back({
        .red = pixel.red,
        .green = pixel.green,
        .blue = pixel.blue,
        .alpha = pixel.alpha,
    });
  }
  if (!exact_device_transform.apply(device_pixels) || cancellation.cancelled()) {
    return {.failure_reason = "canonical device CPU fallback transform failed"};
  }
  for (auto index = std::size_t{0}; index < pixels.size(); ++index) {
    pixels[index] = photo_editor::pipeline::ColorPixel{
        .red = device_pixels[index].red,
        .green = device_pixels[index].green,
        .blue = device_pixels[index].blue,
        .alpha = device_pixels[index].alpha,
    };
  }
  return {
      .output = photo_editor::pipeline::RenderSurface{
          .descriptor = publish_output,
          .pixels = std::move(pixels),
          .identity =
              "surface:canonical-cpu-device-publish:" + invocation.stage_cache_key,
      },
      .backend = "cpu-reference-fused-fallback",
      .fallback_reason = fallback_reason.empty()
                             ? "metal-native-device-publication-failed"
                             : fallback_reason,
      .fusion_evidence =
          canonical_suffix_fusion_evidence("cpu-reference-color-plan:v1", true),
  };
}
#endif

}  // namespace photo_editor::desktop

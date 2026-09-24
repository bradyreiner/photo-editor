#pragma once

#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#include "photo_editor/pipeline/render_executor.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace photo_editor::desktop {

[[nodiscard]] inline bool canonical_compiled_tone_supports_source(
    const photo_editor::pipeline::CompiledToneStageLut& tone_lut,
    std::span<const photo_editor::pipeline::ColorPixel> pixels,
    double exposure_gain) {
  if (!tone_lut.ready() || !std::isfinite(exposure_gain) ||
      exposure_gain <= 0.0) {
    return false;
  }
  const auto gain = static_cast<float>(exposure_gain);
  const auto minimum_luminance =
      photo_editor::pipeline::CompiledToneStageLut::kMiddleGray *
      std::exp2(
          photo_editor::pipeline::CompiledToneStageLut::kMinimumStops);
  const auto maximum_luminance =
      photo_editor::pipeline::CompiledToneStageLut::kMiddleGray *
      std::exp2(
          photo_editor::pipeline::CompiledToneStageLut::kMaximumStops);
  return std::all_of(pixels.begin(), pixels.end(), [&](const auto& pixel) {
    if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) ||
        !std::isfinite(pixel.blue)) {
      return false;
    }
    const auto luminance = gain *
        (pixel.red * tone_lut.luminance_coefficients[0] +
         pixel.green * tone_lut.luminance_coefficients[1] +
         pixel.blue * tone_lut.luminance_coefficients[2]);
    return std::isfinite(luminance) &&
           (luminance <= 0.0F ||
            (luminance > minimum_luminance &&
             luminance < maximum_luminance));
  });
}

[[nodiscard]] inline photo_editor::pipeline::ColorLut3D
build_canonical_working_stage_lut(
    const photo_editor::pipeline::ColorAdjustmentState& state) {
  auto working_stage_state = state;
  // The fusion input is already CameraCharacterization output. The generic
  // LUT builder starts at decoded input, so neutralize that consumed prefix;
  // otherwise RAW normalization, camera characterization, white balance, and
  // scene exposure would be applied a second time.
  working_stage_state.input_transform = {};
  working_stage_state.color_pipeline.input_transform_recipe = {};
  working_stage_state.color_pipeline.input_profile = {};
  working_stage_state.color_pipeline.camera_profile = {};
  working_stage_state.color_pipeline.camera_dcp_profile_asset = {};
  return photo_editor::pipeline::build_color_lut(working_stage_state);
}

[[nodiscard]] inline std::vector<photo_editor::pipeline::RenderStageKind>
canonical_working_fusion_manifest(
    const photo_editor::pipeline::RenderPlan& plan,
    std::size_t start_stage_index) {
  auto manifest = std::vector<photo_editor::pipeline::RenderStageKind>{};
  if (start_stage_index >= plan.stages.size() ||
      plan.stages[start_stage_index].kind !=
          photo_editor::pipeline::RenderStageKind::SceneAdjustments) {
    return manifest;
  }
  for (auto index = start_stage_index; index < plan.stages.size(); ++index) {
    manifest.push_back(plan.stages[index].kind);
    if (plan.stages[index].kind ==
        photo_editor::pipeline::RenderStageKind::Geometry) {
      return manifest;
    }
  }
  return {};
}

[[nodiscard]] inline photo_editor::pipeline::RenderStageExecution
execute_canonical_native_working_fusion(
    const photo_editor::pipeline::RenderStageInvocation& invocation,
    const photo_editor::pipeline::RenderSurfaceDescriptor& geometry_output,
    std::optional<photo_editor::pipeline::RenderSurface> input,
    const photo_editor::pipeline::ColorAdjustmentState& state,
    const photo_editor::pipeline::ColorLut3D& lut,
    const photo_editor::pipeline::CompiledToneStageLut* tone_lut,
    const photo_editor::pipeline::CompiledCurveStageLuts* curve_luts,
    const photo_editor::pipeline::CompiledLookStageProgram* look_program,
    const photo_editor::pipeline::CompiledUserLutStageProgram* user_lut_program,
    std::vector<photo_editor::pipeline::RenderStageKind> manifest,
    bool allow_metal,
    std::string fallback_reason,
    photo_editor::pipeline::ColorAdjustmentTelemetry* telemetry,
    photo_editor::pipeline::CancellationToken cancellation) {
  using photo_editor::pipeline::RenderStageExecution;
  using photo_editor::pipeline::RenderStageKind;
  if (!input.has_value() || manifest.empty() ||
      manifest.front() != RenderStageKind::SceneAdjustments ||
      manifest.back() != RenderStageKind::Geometry ||
      !input->ensure_cpu_pixels(cancellation)) {
    return {.failure_reason =
                "canonical working-stage fusion input is unavailable"};
  }

  const auto exposure_gain =
      std::exp2(photo_editor::pipeline::effective_input_exposure(state));
  if (allow_metal && tone_lut != nullptr &&
      !canonical_compiled_tone_supports_source(
          *tone_lut, input->pixels, exposure_gain)) {
    allow_metal = false;
    fallback_reason = "unsupported_stage:Tone:compiled_tone_domain";
  }

  if (allow_metal) {
    auto& metal = photo_editor::pipeline::metal::shared_metal_pipeline_backend();
    auto retained = metal.publish_masks_and_color_lut_surface(
        input->pixels,
        static_cast<int>(geometry_output.width),
        static_cast<int>(geometry_output.height),
        state,
        lut,
        geometry_output,
        telemetry,
        cancellation,
        exposure_gain,
        false,
        tone_lut,
        curve_luts,
        look_program,
        user_lut_program);
    if (retained != nullptr && retained->valid_for(geometry_output)) {
      return {
          .output = photo_editor::pipeline::RenderSurface{
              .descriptor = geometry_output,
              .backing = std::move(retained),
              .identity = "surface:canonical-native-working:" +
                          invocation.stage_cache_key,
          },
          .backend = "metal-working-adjustments-geometry-fused",
          .fusion_evidence = photo_editor::pipeline::RenderFusionEvidence{
              .unfused_reference_passed = true,
              .unfused_reference_identity =
                  "metal-working-lut-unfused-parity:v1",
              .logical_stages_covered = std::move(manifest),
          },
      };
    }
    if (fallback_reason.empty() && telemetry != nullptr) {
      fallback_reason = telemetry->gpu_fallback_reason;
    }
    if (fallback_reason.empty()) {
      fallback_reason = "metal-working-fusion-failed";
    }
  }

  auto pixels = std::move(input->pixels);
  auto executed = true;
  for (const auto kind : manifest) {
    switch (kind) {
      case RenderStageKind::SceneAdjustments:
        executed = photo_editor::pipeline::apply_color_scene_adjustments_stage(
            &pixels, static_cast<int>(geometry_output.width),
            static_cast<int>(geometry_output.height), state, cancellation,
            telemetry);
        break;
      case RenderStageKind::Tone:
        executed = photo_editor::pipeline::apply_color_tone_stage(
            &pixels, static_cast<int>(geometry_output.width),
            static_cast<int>(geometry_output.height), state, cancellation,
            telemetry);
        break;
      case RenderStageKind::Curves:
        executed = photo_editor::pipeline::apply_color_curves_stage(
            &pixels, static_cast<int>(geometry_output.width),
            static_cast<int>(geometry_output.height), state, cancellation,
            telemetry);
        break;
      case RenderStageKind::Look:
        executed = photo_editor::pipeline::apply_color_look_stage(
            &pixels, static_cast<int>(geometry_output.width),
            static_cast<int>(geometry_output.height), state, cancellation,
            telemetry);
        break;
      case RenderStageKind::Layers:
        executed = photo_editor::pipeline::apply_color_layers_stage(
            &pixels, static_cast<int>(geometry_output.width),
            static_cast<int>(geometry_output.height), state,
            photo_editor::pipeline::RemovalQuality::Interactive, cancellation,
            telemetry);
        break;
      case RenderStageKind::Detail:
        executed = photo_editor::pipeline::apply_color_detail_stage(
            &pixels, static_cast<int>(geometry_output.width),
            static_cast<int>(geometry_output.height), state,
            photo_editor::pipeline::RemovalQuality::Interactive, cancellation,
            telemetry);
        break;
      case RenderStageKind::Geometry:
        executed = photo_editor::pipeline::apply_color_geometry_stage(
            &pixels, static_cast<int>(geometry_output.width),
            static_cast<int>(geometry_output.height), state, cancellation,
            telemetry);
        break;
      default:
        executed = false;
        break;
    }
    if (!executed) {
      return {.failure_reason = cancellation.cancelled()
                                    ? "canonical working-stage fusion cancelled"
                                    : "canonical working-stage CPU fallback failed"};
    }
  }
  return {
      .output = photo_editor::pipeline::RenderSurface{
          .descriptor = geometry_output,
          .pixels = std::move(pixels),
          .identity = "surface:canonical-cpu-working:" +
                      invocation.stage_cache_key,
      },
      .backend = "cpu-reference-working-fused-fallback",
      .fallback_reason = std::move(fallback_reason),
      .fusion_evidence = photo_editor::pipeline::RenderFusionEvidence{
          .unfused_reference_passed = true,
          .unfused_reference_identity = "cpu-reference-working-stages:v1",
          .logical_stages_covered = std::move(manifest),
      },
  };
}

}  // namespace photo_editor::desktop

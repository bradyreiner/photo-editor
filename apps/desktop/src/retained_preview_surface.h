#pragma once

#include "editor_preview_controller.h"
#include "native_output_surface_publication.h"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace photo_editor::desktop {

template <typename Telemetry>
inline void record_retained_surface_materialization(
    WorkingImage* working,
    Telemetry* telemetry) {
  if (working != nullptr && telemetry != nullptr) {
    telemetry->gpu_gpu_to_cpu_bytes += static_cast<qint64>(
        working->takeMaterializedGpuToCpuBytes());
  }
}

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
[[nodiscard]] inline std::optional<photo_editor::pipeline::RenderSurface>
publish_retained_adjusted_surface(
    photo_editor::pipeline::metal::MetalPipelineBackend& metal,
    std::span<const photo_editor::pipeline::ColorPixel> pixels,
    int width,
    int height,
    const photo_editor::pipeline::ColorAdjustmentState& state,
    const photo_editor::pipeline::ColorLut3D& lut,
    std::string working_color_space_identity,
    photo_editor::pipeline::ColorAdjustmentTelemetry* telemetry,
    photo_editor::pipeline::CancellationToken cancellation) {
  auto descriptor = photo_editor::pipeline::RenderSurfaceDescriptor{
      .pixel_encoding = photo_editor::pipeline::RenderPixelEncoding::SceneLinearFloat,
      .color_domain = photo_editor::pipeline::RenderColorDomain::WorkingScene,
      .range = photo_editor::pipeline::RenderRangeModel::SceneExtended,
      .alpha = photo_editor::pipeline::RenderAlphaModel::Straight,
      .color_space_identity = std::move(working_color_space_identity),
      .transfer_identity = "linear",
      .width = static_cast<std::uint32_t>(std::max(width, 0)),
      .height = static_cast<std::uint32_t>(std::max(height, 0)),
  };
  auto backing = metal.publish_masks_and_color_lut_surface(
      pixels, width, height, state, lut, descriptor, telemetry, cancellation);
  if (backing == nullptr) {
    return std::nullopt;
  }
  return photo_editor::pipeline::RenderSurface{
      .descriptor = std::move(descriptor),
      .backing = std::move(backing),
      .identity = "surface:adjusted:metal-retained",
  };
}
#endif

[[nodiscard]] inline bool retained_preview_surface_is_eligible(
    const WorkingImage& working,
    const RenderRequest* request) {
  return request != nullptr && working.hasRetainedSurface() &&
         request->use_in_memory_preview && request->allow_native_srgb_surface &&
         !request->require_cpu_preview_image && !request->full_resolution &&
         !request->require_cpu_display_image &&
         request->tile_size <= 0 && !request->compare_mode &&
         !request->generate_proof_gamut_warning_mask && !request->scopes_visible &&
         !request->canonical_render_plan.has_value();
}

template <typename Result>
[[nodiscard]] Result publish_retained_preview_surface(
    const WorkingImage& working,
    const RenderRequest* request,
    const EditState& state,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (!retained_preview_surface_is_eligible(working, request)) {
    return {};
  }
  const auto output_transform = preview_working_output_transform(
      &color_pipeline,
      nullptr,
      false,
      request->draft_interactive
          ? photo_editor::pipeline::Aces2OutputLutQuality::Interactive
          : photo_editor::pipeline::Aces2OutputLutQuality::Final);
  return publish_full_working_image<Result>(
      working,
      QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32,
      state.working_color_profile,
      output_transform,
      true,
      cancellation);
}

}  // namespace photo_editor::desktop

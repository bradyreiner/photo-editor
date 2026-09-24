#pragma once

#include "editor_preview_controller.h"
#include "edit_recipe_pipeline_bridge.h"
#include "native_output_surface_publication.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace photo_editor::desktop {

[[nodiscard]] inline bool canonical_working_surface_request_is_eligible(
    const RenderRequest& request) {
  // Scopes may materialize the completed working surface. Only the zero-readback
  // display suffix requires them to be hidden; color-stage fusion does not.
  return request.use_in_memory_preview && !request.require_cpu_preview_image &&
         !request.full_resolution && request.tile_size <= 0 &&
         !request.compare_mode && !request.generate_proof_gamut_warning_mask &&
         request.canonical_render_plan.has_value();
}

[[nodiscard]] inline bool canonical_native_surface_request_is_eligible(
    const RenderRequest& request) {
  return canonical_working_surface_request_is_eligible(request) &&
         !request.scopes_visible;
}

[[nodiscard]] inline bool canonical_native_suffix_is_eligible(
    const RenderRequest& request,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    bool has_proof_stage,
    bool has_device_stage,
    bool backend_available) {
  return backend_available && canonical_native_surface_request_is_eligible(request) &&
         request.allow_native_srgb_surface && !has_proof_stage &&
         !has_device_stage && !color_pipeline.soft_proof_enabled &&
         !request.canonical_render_plan->device_icc_profile.has_value() &&
         color_pipeline.preview_icc_profile_asset.status ==
             photo_editor::pipeline::IccProfileAssetStatus::Unconfigured &&
         photo_editor::pipeline::photographic_rendering_model_uses_aces2(
             color_pipeline.photographic_rendering_model) &&
         color_pipeline.render_intent == photo_editor::pipeline::RenderIntent::Perceptual &&
         color_pipeline.preview_output_profile.color_space.primaries ==
             photo_editor::pipeline::ColorPrimaries::Srgb &&
         color_pipeline.preview_display_target.eotf ==
             photo_editor::pipeline::DisplayEotf::Srgb;
}

[[nodiscard]] inline bool canonical_native_device_suffix_may_be_eligible(
    const RenderRequest& request,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    bool has_proof_stage,
    bool has_device_stage,
    bool backend_available) {
  return backend_available && canonical_native_surface_request_is_eligible(request) &&
         request.allow_native_device_surface && !has_proof_stage && has_device_stage &&
         !color_pipeline.soft_proof_enabled &&
         request.canonical_render_plan->device_icc_profile.has_value() &&
         color_pipeline.preview_icc_profile_asset.ready() &&
         photo_editor::pipeline::photographic_rendering_model_uses_aces2(
             color_pipeline.photographic_rendering_model) &&
         color_pipeline.render_intent == photo_editor::pipeline::RenderIntent::Perceptual &&
         color_pipeline.preview_display_target.eotf ==
             photo_editor::pipeline::DisplayEotf::Srgb;
}

[[nodiscard]] inline bool canonical_native_device_suffix_is_eligible(
    const RenderRequest& request,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    bool has_proof_stage,
    bool has_device_stage,
    const std::shared_ptr<const photo_editor::pipeline::IccTransformLut3D>& device_lut,
    bool backend_available) {
  return canonical_native_device_suffix_may_be_eligible(
             request,
             color_pipeline,
             has_proof_stage,
         has_device_stage,
         backend_available) &&
         device_lut != nullptr && device_lut->valid();
}

[[nodiscard]] inline bool canonical_native_suffix_backend_available() {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  return photo_editor::pipeline::metal::shared_metal_pipeline_backend().available();
#else
  return false;
#endif
}

[[nodiscard]] inline std::optional<photo_editor::pipeline::RenderSurface>
canonical_geometry_surface(
    const WorkingImage& working,
    photo_editor::pipeline::RenderSurfaceDescriptor descriptor,
    std::string identity) {
  if (working.width <= 0 || working.height <= 0 ||
      descriptor.width != static_cast<std::uint32_t>(working.width) ||
      descriptor.height != static_cast<std::uint32_t>(working.height)) {
    return std::nullopt;
  }
  if (working.hasRetainedSurface()) {
    auto surface = photo_editor::pipeline::RenderSurface{
        .descriptor = std::move(descriptor),
        .backing = working.retainedBacking(),
        .identity = std::move(identity),
    };
    return surface.valid()
               ? std::optional<photo_editor::pipeline::RenderSurface>{
                     std::move(surface)}
               : std::nullopt;
  }
  if (working.isNull()) {
    return std::nullopt;
  }
  return photo_editor::pipeline::RenderSurface{
      .descriptor = std::move(descriptor),
      .pixels = working.pixels,
      .identity = std::move(identity),
  };
}

}  // namespace photo_editor::desktop

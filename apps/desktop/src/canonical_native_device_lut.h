#pragma once

#include "canonical_native_preview_suffix.h"

#include <memory>

namespace photo_editor::desktop {

struct CanonicalNativeDeviceLut final {
  std::shared_ptr<const photo_editor::pipeline::IccTransformLut3D> lut;
  bool eligible = false;
};

[[nodiscard]] inline CanonicalNativeDeviceLut select_canonical_native_device_lut(
    const RenderRequest& request,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    bool has_proof_stage,
    bool has_device_stage,
    const photo_editor::pipeline::CompiledIccTransform* device_transform,
    bool backend_available) {
  auto selection = CanonicalNativeDeviceLut{};
  if (device_transform != nullptr && device_transform->ready()) {
    const auto settings = photo_editor::pipeline::icc_transform_lut_settings(
        request.draft_interactive
            ? photo_editor::pipeline::IccTransformLutQuality::Interactive
            : photo_editor::pipeline::IccTransformLutQuality::Final);
    selection.lut = photo_editor::pipeline::find_cached_icc_transform_lut(
        *device_transform, settings);
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    if (selection.lut == nullptr) {
      schedule_icc_transform_lut_prewarm(*device_transform, settings);
    }
#endif
  }
  selection.eligible = canonical_native_device_suffix_is_eligible(
      request,
      color_pipeline,
      has_proof_stage,
      has_device_stage,
      selection.lut,
      backend_available);
  return selection;
}

}  // namespace photo_editor::desktop

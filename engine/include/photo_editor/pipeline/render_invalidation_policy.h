#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

enum class PreviewInvalidationStage : std::uint8_t {
  SourceDecode,
  RawDemosaic,
  InputColorTransform,
  LinearBase,
  GeometryTransform,
  GlobalToneColor,
  MaskRasterization,
  MaskComposition,
  LocalAdjustments,
  RetouchHeal,
  RetouchPatchApplication,
  DetailSpatial,
  LayersComposite,
  LutPostColor,
  DisplayTransform,
  OverlayComposite,
  PublishTexture,
  EncodeDisk,
  HistogramScope,
};

[[nodiscard]] std::string_view preview_invalidation_stage_name(
    PreviewInvalidationStage stage) noexcept;
[[nodiscard]] const std::vector<PreviewInvalidationStage>&
preview_invalidation_stage_order();
[[nodiscard]] std::string normalize_preview_dirty_domain(
    std::string_view dirty_domain);
[[nodiscard]] std::vector<PreviewInvalidationStage>
preview_dirty_stages_for_domain(std::string_view dirty_domain);
[[nodiscard]] std::string preview_dirty_domain_for_reason(
    std::string_view reason);

}  // namespace photo_editor::pipeline

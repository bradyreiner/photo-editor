#include "photo_editor/pipeline/render_invalidation_policy.h"

#include <algorithm>
#include <cctype>

namespace photo_editor::pipeline {

std::string_view preview_invalidation_stage_name(
    PreviewInvalidationStage stage) noexcept {
  switch (stage) {
    case PreviewInvalidationStage::SourceDecode: return "source_decode";
    case PreviewInvalidationStage::RawDemosaic: return "raw_demosaic";
    case PreviewInvalidationStage::InputColorTransform: return "input_color_transform";
    case PreviewInvalidationStage::LinearBase: return "linear_base";
    case PreviewInvalidationStage::GeometryTransform: return "geometry_transform";
    case PreviewInvalidationStage::GlobalToneColor: return "global_tone_color";
    case PreviewInvalidationStage::MaskRasterization: return "mask_rasterization";
    case PreviewInvalidationStage::MaskComposition: return "mask_composition";
    case PreviewInvalidationStage::LocalAdjustments: return "local_adjustments";
    case PreviewInvalidationStage::RetouchHeal: return "retouch_heal";
    case PreviewInvalidationStage::RetouchPatchApplication: return "retouch_patch_application";
    case PreviewInvalidationStage::DetailSpatial: return "detail_spatial";
    case PreviewInvalidationStage::LayersComposite: return "layers_composite";
    case PreviewInvalidationStage::LutPostColor: return "lut_post_color";
    case PreviewInvalidationStage::DisplayTransform: return "display_transform";
    case PreviewInvalidationStage::OverlayComposite: return "overlay_composite";
    case PreviewInvalidationStage::PublishTexture: return "publish_texture";
    case PreviewInvalidationStage::EncodeDisk: return "encode_disk";
    case PreviewInvalidationStage::HistogramScope: return "histogram_scope";
  }
  return "source_decode";
}

const std::vector<PreviewInvalidationStage>&
preview_invalidation_stage_order() {
  static const std::vector<PreviewInvalidationStage> stages = {
      PreviewInvalidationStage::SourceDecode,
      PreviewInvalidationStage::RawDemosaic,
      PreviewInvalidationStage::InputColorTransform,
      PreviewInvalidationStage::LinearBase,
      PreviewInvalidationStage::GeometryTransform,
      PreviewInvalidationStage::GlobalToneColor,
      PreviewInvalidationStage::MaskRasterization,
      PreviewInvalidationStage::MaskComposition,
      PreviewInvalidationStage::LocalAdjustments,
      PreviewInvalidationStage::RetouchHeal,
      PreviewInvalidationStage::RetouchPatchApplication,
      PreviewInvalidationStage::DetailSpatial,
      PreviewInvalidationStage::LayersComposite,
      PreviewInvalidationStage::LutPostColor,
      PreviewInvalidationStage::DisplayTransform,
      PreviewInvalidationStage::OverlayComposite,
      PreviewInvalidationStage::PublishTexture,
      PreviewInvalidationStage::EncodeDisk,
      PreviewInvalidationStage::HistogramScope,
  };
  return stages;
}

std::string normalize_preview_dirty_domain(
    std::string_view dirty_domain) {
  std::string normalized(dirty_domain);
  std::ranges::transform(normalized, normalized.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  for (auto& value : normalized) {
    if (value == '-' || value == ' ') {
      value = '_';
    }
  }
  const auto first = normalized.find_first_not_of("_\t\r\n");
  const auto last = normalized.find_last_not_of("_\t\r\n");
  normalized =
      first == std::string::npos
          ? std::string{}
          : normalized.substr(first, last - first + 1U);
  if (normalized == "global_color" || normalized == "tone_color") return "global_tone_color";
  if (normalized == "display") return "display_transform";
  if (normalized == "geometry") return "geometry_transform";
  if (normalized == "mask") return "mask_rasterization";
  if (normalized == "mask_compose" || normalized == "mask_composite") return "mask_composition";
  if (normalized == "detail") return "detail_spatial";
  if (normalized == "layer") return "layers_composite";
  if (normalized == "retouch") return "retouch_heal";
  if (normalized == "retouch_patch" || normalized == "inpaint_patch") return "retouch_patch_application";
  if (normalized == "overlay") return "overlay_composite";
  return normalized.empty() ? "full_pipeline" : normalized;
}

std::vector<PreviewInvalidationStage>
preview_dirty_stages_for_domain(std::string_view dirty_domain) {
  const auto& stages = preview_invalidation_stage_order();
  const auto normalized = normalize_preview_dirty_domain(dirty_domain);
  if (normalized == "full_pipeline" || normalized == "decode" ||
      normalized == "source_decode") {
    return stages;
  }
  auto begin = stages.cbegin();
  if (normalized == "raw") {
    ++begin;
  } else {
    begin = std::ranges::find_if(stages, [&](const auto stage) {
      return preview_invalidation_stage_name(stage) == normalized;
    });
    if (begin == stages.cend()) {
      begin = stages.cbegin();
    }
  }
  return {begin, stages.cend()};
}

std::string preview_dirty_domain_for_reason(std::string_view reason) {
  auto normalized = normalize_preview_dirty_domain(reason);
  if (normalized == "output_profile_change") return "display_transform";
  if (normalized == "crop_change") return "geometry_transform";
  if (normalized == "mask_preview_change") return "mask_rasterization";
  if (normalized == "preview_retry") return "publish_texture";
  return "full_pipeline";
}

}  // namespace photo_editor::pipeline

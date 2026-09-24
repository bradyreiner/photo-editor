#pragma once

#include "color_working_space_runtime.h"
#include "photo_editor/pipeline/color_adjustments.h"

#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

struct EdgeAwareMatteContext final {
  const std::vector<ColorPixel>* pixels = nullptr;
  int width = 0;
  int height = 0;
};

struct LocalAdjustmentSettings final {
  double exposure = 0.0;
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  double vibrance = 0.0;
  double saturation = 0.0;
  double hue_shift = 0.0;
  double saturation_shift = 0.0;
  double colorize_hue = 0.0;
  double colorize_strength = 0.0;
  double remove_strength = 0.0;
};

struct HealComposite final {
  double weight = 0.0;
  double texture_blend = 0.68;
  double color_blend = 0.46;
};

[[nodiscard]] double radial_mask_weight(double normalized_x, double normalized_y, const ColorAdjustmentState& state);
[[nodiscard]] double brush_mask_weight(double normalized_x, double normalized_y, const LocalMask& mask);
[[nodiscard]] double local_mask_weight(double normalized_x, double normalized_y, const ColorPixel& pixel, const PipelineColorContext& context, const LocalMask& mask, const EdgeAwareMatteContext* source_context = nullptr);
[[nodiscard]] double nested_local_mask_weight(double normalized_x, double normalized_y, const ColorPixel& pixel, const PipelineColorContext& context, const LocalMask& mask, const std::vector<LocalMask>& siblings, const EdgeAwareMatteContext* source_context = nullptr);
[[nodiscard]] bool local_mask_set_has_adjustments(const LocalMaskSet& mask_set);
[[nodiscard]] double local_mask_set_weight(double normalized_x, double normalized_y, const ColorPixel& pixel, const PipelineColorContext& context, const LocalMaskSet& mask_set, const EdgeAwareMatteContext* source_context = nullptr);
[[nodiscard]] bool has_selection_decontaminate(const std::vector<LocalMask>& masks);
[[nodiscard]] bool has_selection_decontaminate(const LocalMaskSet& mask_set);
void apply_selection_decontaminate_stages(ColorPixel* pixel, double normalized_x, double normalized_y, const PipelineColorContext& context, const std::vector<LocalMask>& masks, const EdgeAwareMatteContext* source_context = nullptr);
void apply_selection_decontaminate_stages(ColorPixel* pixel, double normalized_x, double normalized_y, const PipelineColorContext& context, const LocalMaskSet& mask_set, const EdgeAwareMatteContext* source_context = nullptr);
[[nodiscard]] bool local_masks_share_adjustments(const std::vector<LocalMask>& masks, LocalAdjustmentSettings* shared_adjustments);
[[nodiscard]] bool local_masks_all_additive(const std::vector<LocalMask>& masks);
[[nodiscard]] bool primary_local_mask_adjustments(const std::vector<LocalMask>& masks, LocalAdjustmentSettings* adjustments);
[[nodiscard]] double composite_local_mask_weight(double normalized_x, double normalized_y, const ColorPixel& pixel, const PipelineColorContext& context, const std::vector<LocalMask>& masks, const EdgeAwareMatteContext* source_context = nullptr);
[[nodiscard]] bool has_local_mask_removal(const std::vector<LocalMask>& masks);
[[nodiscard]] bool has_local_mask_heal(const std::vector<LocalMask>& masks);
[[nodiscard]] bool has_local_mask_clone(const std::vector<LocalMask>& masks);
[[nodiscard]] bool has_local_mask_red_eye(const std::vector<LocalMask>& masks);
[[nodiscard]] bool has_local_mask_skin_cleanup(const std::vector<LocalMask>& masks);
[[nodiscard]] bool has_local_mask_frequency_smoothing(const std::vector<LocalMask>& masks);
[[nodiscard]] bool has_local_inpaint_patch(const std::vector<LocalMask>& masks);
[[nodiscard]] double composite_local_removal_weight(double normalized_x, double normalized_y, const ColorPixel& pixel, const PipelineColorContext& context, const std::vector<LocalMask>& masks, std::string_view retouch_mode);
[[nodiscard]] HealComposite composite_local_heal(double normalized_x, double normalized_y, const ColorPixel& pixel, const PipelineColorContext& context, const std::vector<LocalMask>& masks);
void apply_local_removal(std::vector<ColorPixel>* pixels, int width, int height, const std::vector<double>& removal_mask, double detail_preservation, RemovalQuality removal_quality, double texture_blend = 1.0, double color_blend = 1.0, CancellationToken cancellation = {});
void apply_local_clone(std::vector<ColorPixel>* pixels, int width, int height, const PipelineColorContext& context, const std::vector<LocalMask>& masks);
void apply_local_red_eye(std::vector<ColorPixel>* pixels, int width, int height, const PipelineColorContext& context, const std::vector<LocalMask>& masks);
void apply_local_skin_cleanup(std::vector<ColorPixel>* pixels, int width, int height, const PipelineColorContext& context, const std::vector<LocalMask>& masks);
void apply_local_frequency_smoothing(std::vector<ColorPixel>* pixels, int width, int height, const PipelineColorContext& context, const std::vector<LocalMask>& masks);
void apply_local_inpaint_patches(std::vector<ColorPixel>* pixels, int width, int height, const PipelineColorContext& context, const std::vector<LocalMask>& masks);

}  // namespace photo_editor::pipeline


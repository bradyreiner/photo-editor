#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <array>

#include <QVariantMap>

namespace photo_editor::desktop {

[[nodiscard]] double wrap_unit(double value);
[[nodiscard]] double circular_weight(double value, double center, double radius);
[[nodiscard]] double circular_feathered_weight(
    double value, double center, double radius, double softness);
[[nodiscard]] double preview_luminance_for_pixel(
    const photo_editor::pipeline::ColorPixel& pixel);
void hue_saturation_value_for_preview_pixel(
    const photo_editor::pipeline::ColorPixel& pixel,
    double* hue,
    double* saturation,
    double* value);
[[nodiscard]] double skin_candidate_weight(
    double hue, double saturation, double value);
[[nodiscard]] double circular_distance_unit(double left, double right);

void update_color_range_constraints_from_samples(QVariantMap* mask);
void update_luminance_range_constraints_from_samples(QVariantMap* mask);
[[nodiscard]] bool mask_has_valid_depth_payload(const QVariantMap& mask);
void update_depth_range_constraints_from_samples(QVariantMap* mask);
[[nodiscard]] QVariantMap luminance_sample_preview_map(
    double start_x, double start_y, double end_x, double end_y);
[[nodiscard]] std::array<double, 3> preview_white_balance_multipliers(
    double temperature, double tint);

[[nodiscard]] double apply_selection_expand_to_preview_weight(
    double base_weight, const photo_editor::pipeline::LocalMask& mask);
[[nodiscard]] double apply_selection_edge_contrast_to_preview_weight(
    double base_weight, const photo_editor::pipeline::LocalMask& mask);
[[nodiscard]] double apply_selection_smooth_to_preview_weight(
    double base_weight, const photo_editor::pipeline::LocalMask& mask);
[[nodiscard]] double apply_selection_fine_edge_to_preview_weight(
    double base_weight, const photo_editor::pipeline::LocalMask& mask);
[[nodiscard]] double apply_selection_refine_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask,
    bool spatial_expand_smooth_applied = false);
[[nodiscard]] double depth_refine_value_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask);
[[nodiscard]] double unit_range_refine_weight(
    double value, double min_value, double max_value, double feather);
[[nodiscard]] double apply_range_refinement_to_preview_weight(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask);
[[nodiscard]] double apply_full_refine_to_preview_weight(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask,
    bool spatial_expand_smooth_applied = false);

}  // namespace photo_editor::desktop

#pragma once

#include <QSize>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {

struct OutlineNoiseFilterStats final {
  int input_path_count = 0;
  int output_path_count = 0;
  double largest_area = 0.0;
  double minimum_secondary_area = 0.0;
  bool applied = false;
};

[[nodiscard]] bool mask_selection_refinement_active(const QVariantMap& mask);
[[nodiscard]] QSize high_resolution_brush_outline_size(const QVariantMap& mask);
[[nodiscard]] QVariantList traced_outline_paths_for_brush_strokes(
    const QVariantMap& mask);
[[nodiscard]] QVariantList outline_paths_for_mask(const QVariantMap& mask);
[[nodiscard]] QVariantList clean_canvas_outline_paths_for_mask(
    const QVariantMap& mask);
[[nodiscard]] bool brush_mask_has_source_dependent_outline(
    const QVariantMap& mask);
[[nodiscard]] bool clean_canvas_outline_matches_final_single_mask(
    const QVariantMap& mask);
[[nodiscard]] QVariantList first_outline_path_only(
    const QVariantList& outline_paths);
[[nodiscard]] double normalized_outline_path_area(const QVariantList& path);
[[nodiscard]] QVariantList filter_minor_composite_outline_paths(
    const QVariantList& outline_paths, OutlineNoiseFilterStats* stats);
[[nodiscard]] QVariantList outline_points_for_mask(const QVariantMap& mask);

}  // namespace photo_editor::desktop

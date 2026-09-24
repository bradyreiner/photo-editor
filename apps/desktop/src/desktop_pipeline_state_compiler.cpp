#include "desktop_pipeline_state_compiler.h"

#include "edit_recipe_mask_schema.h"
#include "edit_recipe_pipeline_bridge.h"
#include "photo_layer_compiler.h"
#include "local_mask_set_settings.h"

#include <QHash>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace photo_editor::desktop {
namespace {

double map_double_value(
    const QVariantMap& map,
    const QString& key,
    double fallback) {
  return map.contains(key) ? map.value(key).toDouble() : fallback;
}

QString pipeline_mask_tool_role(const QVariantMap& mask) {
  if (mask.contains(QStringLiteral("toolRole"))) {
    return normalized_mask_tool_role(
        mask.value(QStringLiteral("toolRole")).toString());
  }
  if (!mask.value(QStringLiteral("retouchMode"))
           .toString()
           .trimmed()
           .isEmpty()) {
    return QStringLiteral("retouch");
  }

  constexpr std::array selection_adjustments{
      "exposure", "contrast", "highlights", "shadows", "whites", "blacks",
      "temperature", "tint", "vibrance", "saturation", "hueShift",
      "saturationShift", "colorizeHue", "colorizeStrength"};
  const auto has_selection_adjustments = std::ranges::any_of(
      selection_adjustments,
      [&mask](const char* key) {
        return std::abs(map_double_value(
                   mask, QString::fromLatin1(key), 0.0)) > 1e-6;
      });
  if (!has_selection_adjustments &&
      std::abs(map_double_value(
          mask, QStringLiteral("removeStrength"), 0.0)) > 1e-6) {
    return QStringLiteral("retouch");
  }
  return QStringLiteral("selection");
}

bool preview_bypasses_mask(
    const QVariantMap& mask,
    DesktopRenderPurpose purpose) {
  return purpose == DesktopRenderPurpose::Preview &&
         mask.value(QStringLiteral("maskSetPreviewBypassed")).toBool();
}

}  // namespace

std::vector<photo_editor::pipeline::LocalMaskSet>
compile_pipeline_local_mask_sets(
    const QVariantList& masks,
    DesktopRenderPurpose purpose,
    LocalMaskRasterPolicy raster_policy,
    const QSize& output_size) {
  std::vector<photo_editor::pipeline::LocalMaskSet> mask_sets;
  QHash<QString, int> indices_by_id;
  auto selection_solo_active = false;
  auto solo_ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), solo_ordinal);
    ++solo_ordinal;
    if (mask_definition_affects_render(mask, solo_ordinal - 1) &&
        pipeline_mask_tool_role(mask) == QStringLiteral("selection") &&
        !preview_bypasses_mask(mask, purpose) &&
        mask.value(QStringLiteral("solo")).toBool()) {
      selection_solo_active = true;
      break;
    }
  }

  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("selection") ||
        preview_bypasses_mask(mask, purpose) ||
        (selection_solo_active &&
         !mask.value(QStringLiteral("solo")).toBool())) {
      continue;
    }

    auto source = compile_pipeline_local_mask(
        mask, raster_policy, output_size);
    source.exposure = 0.0;
    source.contrast = 0.0;
    source.highlights = 0.0;
    source.shadows = 0.0;
    source.whites = 0.0;
    source.blacks = 0.0;
    source.temperature = 0.0;
    source.tint = 0.0;
    source.vibrance = 0.0;
    source.saturation = 0.0;
    source.hue_shift = 0.0;
    source.saturation_shift = 0.0;
    source.colorize_hue = 0.0;
    source.colorize_strength = 0.0;
    source.remove_strength = 0.0;
    source.retouch_mode.clear();

    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
    auto set_index = indices_by_id.value(set_id, -1);
    if (set_index < 0) {
      auto set = compile_local_mask_set_settings(mask);
      mask_sets.push_back(std::move(set));
      set_index = static_cast<int>(mask_sets.size()) - 1;
      indices_by_id.insert(set_id, set_index);
    }
    mask_sets[static_cast<std::size_t>(set_index)].sources.push_back(
        std::move(source));
  }
  return mask_sets;
}

std::vector<photo_editor::pipeline::LocalMask>
compile_pipeline_retouch_masks(
    const QVariantList& masks,
    LocalMaskRasterPolicy raster_policy,
    const QSize& output_size) {
  std::vector<photo_editor::pipeline::LocalMask> retouch_masks;
  retouch_masks.reserve(static_cast<std::size_t>(masks.size()));
  auto retouch_solo_active = false;
  auto solo_ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), solo_ordinal);
    ++solo_ordinal;
    if (mask_definition_affects_render(mask, solo_ordinal - 1) &&
        pipeline_mask_tool_role(mask) == QStringLiteral("retouch") &&
        mask.value(QStringLiteral("solo")).toBool()) {
      retouch_solo_active = true;
      break;
    }
  }
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("retouch") ||
        (retouch_solo_active &&
         !mask.value(QStringLiteral("solo")).toBool())) {
      continue;
    }
    retouch_masks.push_back(compile_pipeline_local_mask(
        mask, raster_policy, output_size));
  }
  std::stable_sort(
      retouch_masks.begin(), retouch_masks.end(),
      [](const auto& left, const auto& right) {
        return left.retouch_order < right.retouch_order;
      });
  return retouch_masks;
}

photo_editor::pipeline::ColorAdjustmentState
compile_desktop_pipeline_state(
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline_config,
    DesktopRenderPurpose purpose,
    LocalMaskRasterPolicy raster_policy,
    const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels) {
  auto pipeline_state = compile_base_color_adjustment_state(
      state, metadata, &color_pipeline_config);
  pipeline_state.local_mask_sets = compile_pipeline_local_mask_sets(
      state.mask_stack, purpose, raster_policy, output_size);
  pipeline_state.retouch_masks = compile_pipeline_retouch_masks(
      state.mask_stack, raster_policy, output_size);
  pipeline_state.photo_layers = compile_pipeline_photo_layers(
      state.layers, state.mask_stack, raster_policy, output_size,
      source_pixels, color_pipeline_config, purpose);
  return pipeline_state;
}

}  // namespace photo_editor::desktop

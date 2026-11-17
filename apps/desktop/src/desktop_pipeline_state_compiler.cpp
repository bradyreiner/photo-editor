#include "desktop_pipeline_state_compiler.h"
#include "mask_workflow_ownership.h"

#include "edit_recipe_mask_schema.h"
#include "edit_recipe_pipeline_bridge.h"
#include "photo_layer_compiler.h"
#include "photo_layer_effect_codec.h"
#include "photo_layer_style_codec.h"
#include "local_mask_set_settings.h"
#include "photo_text_style.h"
#include "photo_smart_document_expansion.h"
#include "photo_render_profile_for_tests.h"
#include "recipe_mask_sampling.h"
#include "working_image.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <optional>
#include <utility>

namespace photo_editor::desktop {
namespace {

bool has_active_layer_stacks(const QVariantMap& layer) {
  const auto effects = decode_photo_layer_effects(layer);
  if (!effects) return true;
  photo_editor::pipeline::PhotoLayer candidate;
  candidate.effects = *effects;
  if (layer.contains(QStringLiteral("layerStyleStack"))) {
    const auto value = layer.value(QStringLiteral("layerStyleStack"));
    if (value.metaType().id() != QMetaType::QString ||
        !layer_style_codec::decode_stack(value.toString(), &candidate.style_stack, nullptr)) return true;
  }
  return photo_editor::pipeline::photo_layer_has_active_effects(candidate);
}

double map_double_value(
    const QVariantMap& map,
    const QString& key,
    double fallback) {
  return map.contains(key) ? map.value(key).toDouble() : fallback;
}

bool full_canvas_mask_raster(
    const QByteArray& mask_data,
    int mask_width,
    int mask_height,
    const QSize& output_size) {
  return !mask_data.isEmpty() && mask_width > 0 && mask_height > 0 &&
      output_size.width() > 0 && output_size.height() > 0 &&
      static_cast<qint64>(mask_data.size()) ==
          static_cast<qint64>(mask_width) * mask_height &&
      static_cast<qint64>(mask_width) * output_size.height() ==
          static_cast<qint64>(mask_height) * output_size.width();
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

bool plain_opaque_text_ink(const QVariantMap& layer) {
  const auto typography = layer.value(QStringLiteral("textTypography")).toMap();
  if (!typography.isEmpty()) {
    const auto style = decode_photo_text_style(
        QJsonObject::fromVariantMap(typography));
    return style.has_value() && style->color.alpha() == 255 &&
        style->stroke_width == 0.0 && style->shadow_x == 0.0 &&
        style->shadow_y == 0.0 && !style->underline &&
        style->path_points.isEmpty() && style->path_contours.isEmpty() &&
        !style->path_exclusion && style->material == QStringLiteral("none");
  }
  auto color = QColor(layer.value(QStringLiteral("textColor"),
                                  QStringLiteral("#ffffff")).toString());
  return color.isValid() && color.alpha() == 255;
}

bool plain_display_text_layer(const QVariantMap& layer, const QSize& output_size) {
  const auto kind = layer.value(QStringLiteral("kind")).toString()
                        .trimmed().toLower();
  if (kind != QStringLiteral("text") ||
      !layer.value(QStringLiteral("enabled"), true).toBool() ||
      layer.value(QStringLiteral("solo")).toBool() ||
      layer.value(QStringLiteral("clippingMask")).toBool() ||
      layer.value(QStringLiteral("copyBaseImage")).toBool() ||
      layer.value(QStringLiteral("eraseBaseImage")).toBool()) {
    return false;
  }
  const auto blend_mode = layer.value(QStringLiteral("blendMode"))
                              .toString().trimmed().toLower();
  if (!blend_mode.isEmpty() && blend_mode != QStringLiteral("normal")) {
    return false;
  }
  const auto effects = layer.value(QStringLiteral("effects")).toList();
  if (!effects.isEmpty() || has_active_layer_stacks(layer) ||
      !layer.value(QStringLiteral("adjustments")).toMap().isEmpty()) {
    return false;
  }
  const auto mask_data = layer.value(QStringLiteral("maskData")).toByteArray();
  const auto mask_width = layer.value(QStringLiteral("maskWidth")).toInt();
  const auto mask_height = layer.value(QStringLiteral("maskHeight")).toInt();
  const auto has_mask = !mask_data.isEmpty() || mask_width > 0 || mask_height > 0;
  if (layer.value(QStringLiteral("selectionScoped")).toBool() ||
      (has_mask && !full_canvas_mask_raster(
          mask_data, mask_width, mask_height, output_size)) ||
      !layer.value(QStringLiteral("maskId")).toString().trimmed().isEmpty() ||
      !layer.value(QStringLiteral("maskRef")).toString().trimmed().isEmpty()) {
    return false;
  }
  if (std::abs(layer.value(QStringLiteral("textPerspectiveHorizontal")).toDouble()) > 1e-9 ||
      std::abs(layer.value(QStringLiteral("textPerspectiveVertical")).toDouble()) > 1e-9) {
    return false;
  }

  // Text adjustment recipes and layer effects alter glyph color or depend on
  // the working-scene backdrop, so they stay on the established layer path.
  constexpr std::array adjustment_keys{
      "exposure", "contrast", "highlights", "shadows", "whites", "blacks",
      "brightness", "filmicShoulder", "filmicToe", "adaptiveContrast",
      "levelsBlackPoint", "levelsGamma", "levelsWhitePoint", "zoneDeepShadows",
      "zoneShadows", "zoneMidtones", "zoneLights", "zoneHighlights", "texture",
      "clarity", "dehaze", "sharpening", "luminanceNr", "colorNr", "vibrance",
      "saturation", "hueShift", "saturationShift", "colorizeStrength",
      "lensDistortion", "lensVignette", "chromaticAberration", "perspectiveVertical",
      "perspectiveHorizontal", "transformSkewX", "transformSkewY", "liquifyStrength"};
  for (const auto* key : adjustment_keys) {
    if (std::abs(layer.value(QString::fromLatin1(key)).toDouble()) > 1e-9) {
      return false;
    }
  }
  return plain_opaque_text_ink(layer);
}

bool neutral_display_text_group(const QVariantMap& layer) {
  const auto kind = layer.value(QStringLiteral("kind")).toString()
                        .trimmed().toLower();
  if (kind != QStringLiteral("group") ||
      !layer.value(QStringLiteral("_smartDocumentPostCompositeRetouchMasks"))
           .toList().isEmpty() ||
      !layer.value(QStringLiteral("enabled"), true).toBool() ||
      (layer.contains(QStringLiteral("visible")) &&
       !layer.value(QStringLiteral("visible")).toBool()) ||
      layer.value(QStringLiteral("solo")).toBool() ||
      layer.value(QStringLiteral("clippingMask")).toBool() ||
      layer.value(QStringLiteral("copyBaseImage")).toBool() ||
      layer.value(QStringLiteral("eraseBaseImage")).toBool()) {
    return false;
  }
  bool opacity_ok = true;
  const auto opacity = layer.contains(QStringLiteral("opacity"))
      ? layer.value(QStringLiteral("opacity")).toDouble(&opacity_ok) : 1.0;
  if (!opacity_ok || !std::isfinite(opacity) || std::abs(opacity - 1.0) > 1e-9) {
    return false;
  }
  const auto unit_value_is_neutral = [&layer](const QString& key) {
    if (!layer.contains(key)) return true;
    bool ok = false;
    const auto value = layer.value(key).toDouble(&ok);
    return ok && std::isfinite(value) && std::abs(value - 1.0) <= 1e-9;
  };
  const auto blend_mode = layer.value(QStringLiteral("blendMode"))
                              .toString().trimmed().toLower();
  if (!blend_mode.isEmpty() && blend_mode != QStringLiteral("normal")) {
    return false;
  }
  const auto effects = layer.value(QStringLiteral("effects"));
  if (layer.value(QStringLiteral("blendIf")).toMap()
          .value(QStringLiteral("enabled")).toBool() ||
      !effects.toList().isEmpty() || !effects.toMap().isEmpty() ||
      !effects.toString().trimmed().isEmpty() ||
      has_active_layer_stacks(layer) ||
      !layer.value(QStringLiteral("adjustments")).toMap().isEmpty()) {
    return false;
  }
  const auto mask_data = layer.value(QStringLiteral("maskData")).toByteArray();
  if (!mask_data.isEmpty() ||
      layer.value(QStringLiteral("maskWidth")).toInt() > 0 ||
      layer.value(QStringLiteral("maskHeight")).toInt() > 0 ||
      layer.value(QStringLiteral("maskInverted")).toBool() ||
      !unit_value_is_neutral(QStringLiteral("maskOpacity")) ||
      !unit_value_is_neutral(QStringLiteral("maskDensity")) ||
      !layer.value(QStringLiteral("maskId")).toString().trimmed().isEmpty() ||
      !layer.value(QStringLiteral("maskRef")).toString().trimmed().isEmpty() ||
      !layer.value(QStringLiteral("maskGroupId")).toString().trimmed().isEmpty() ||
      !layer.value(QStringLiteral("maskSetId")).toString().trimmed().isEmpty() ||
      !layer.value(QStringLiteral("maskSetName")).toString().trimmed().isEmpty() ||
      !layer.value(QStringLiteral("selectionMaskSetId")).toString().trimmed().isEmpty() ||
      !layer.value(QStringLiteral("selectionMaskSetName")).toString().trimmed().isEmpty() ||
      !layer.value(QStringLiteral("selectionMaskSetIds")).toList().isEmpty() ||
      !layer.value(QStringLiteral("selectionMaskSetNames")).toList().isEmpty() ||
      !layer.value(QStringLiteral("selectionIntersectionMaskSetIds")).toList().isEmpty() ||
      !layer.value(QStringLiteral("selectionIntersectionMaskSetNames")).toList().isEmpty() ||
      layer.value(QStringLiteral("selectionScoped")).toBool() ||
      !layer.value(QStringLiteral("vectorMask")).toMap().isEmpty()) {
    return false;
  }
  constexpr std::array adjustment_keys{
      "exposure", "contrast", "highlights", "shadows", "whites", "blacks",
      "brightness", "filmicShoulder", "filmicToe", "adaptiveContrast",
      "levelsBlackPoint", "levelsGamma", "levelsWhitePoint", "zoneDeepShadows",
      "zoneShadows", "zoneMidtones", "zoneLights", "zoneHighlights", "texture",
      "clarity", "dehaze", "sharpening", "luminanceNr", "colorNr", "vibrance",
      "saturation", "hueShift", "saturationShift", "colorizeStrength",
      "lensDistortion", "lensVignette", "chromaticAberration", "perspectiveVertical",
      "perspectiveHorizontal", "transformSkewX", "transformSkewY", "liquifyStrength"};
  for (const auto* key : adjustment_keys) {
    const auto value = layer.value(QString::fromLatin1(key)).toDouble();
    if (!std::isfinite(value) || std::abs(value) > 1e-9) {
      return false;
    }
  }
  return true;
}

struct DisplayTextSuffix final {
  QStringList text_ids;
  QStringList group_ids;
  bool reached_end = false;
};

DisplayTextSuffix collect_display_text_suffix(
    const QVariantList& siblings,
    const QHash<QString, QVariantList>& children_by_parent,
    const QSize& output_size,
    const QVariantList& masks,
    int depth = 0) {
  DisplayTextSuffix result;
  if (depth > 64) return result;
  for (const auto& value : siblings) {
    const auto layer = value.toMap();
    const auto kind = layer.value(QStringLiteral("kind")).toString()
                          .trimmed().toLower();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) return result;
    if (kind == QStringLiteral("text")) {
      if (!plain_display_text_layer(layer, output_size)) return result;
      result.text_ids.push_back(id);
      continue;
    }
    if (kind != QStringLiteral("group") || !neutral_display_text_group(layer)) {
      return result;
    }
    const auto group_has_attached_mask = std::any_of(
        masks.cbegin(), masks.cend(), [&id](const QVariant& mask_value) {
          return mask_value.toMap().value(QStringLiteral("parentId"))
              .toString().trimmed() == id;
        });
    if (group_has_attached_mask) return result;
    auto group_children = collect_display_text_suffix(
        children_by_parent.value(id), children_by_parent, output_size, masks,
        depth + 1);
    if (group_children.text_ids.isEmpty()) return result;
    result.group_ids.push_back(id);
    result.group_ids.append(group_children.group_ids);
    result.text_ids.append(group_children.text_ids);
    if (!group_children.reached_end) return result;
  }
  result.reached_end = true;
  return result;
}

bool identity_post_layer_geometry_and_detail(const EditRecipeState& state) {
  const auto near = [](double value, double expected = 0.0) {
    return std::isfinite(value) && std::abs(value - expected) <= 1e-9;
  };
  const auto crop_preset = state.crop_preset.trimmed().toLower();
  return near(state.texture) && near(state.clarity) && near(state.dehaze) &&
      near(state.sharpening) && near(state.luminance_nr) && near(state.color_nr) &&
      near(state.lens_distortion) && near(state.lens_vignette) &&
      near(state.chromatic_aberration) && near(state.perspective_vertical) &&
      near(state.perspective_horizontal) && near(state.transform_rotation) &&
      near(state.transform_scale, 1.0) && near(state.transform_skew_x) &&
      near(state.transform_skew_y) && near(state.liquify_strength) &&
      !state.content_aware_crop_fill && state.upright_mode.compare(
          QStringLiteral("Off"), Qt::CaseInsensitive) == 0 &&
      near(state.crop_rect_x) && near(state.crop_rect_y) &&
      near(state.crop_rect_width) && near(state.crop_rect_height) &&
      near(state.crop_zoom, 1.0) &&
      (crop_preset.isEmpty() || crop_preset == QStringLiteral("original"));
}

bool preview_bypasses_mask(
    const QVariantMap& mask,
    DesktopRenderPurpose purpose) {
  return purpose == DesktopRenderPurpose::Preview &&
         mask.value(QStringLiteral("maskSetPreviewBypassed")).toBool();
}

}  // namespace

DisplayReferredTextPolicySnapshot classify_display_referred_text_layers(
    const EditRecipeState& state,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline_config,
    DesktopRenderPurpose purpose,
    const QSize& output_size) {
  using namespace photo_editor::pipeline;
  DisplayReferredTextPolicySnapshot result;
  const auto fail = [&result](const QString& reason) {
    result.reason = reason;
    result.eligible_layer_ids.clear();
    result.render_color_domain = QStringLiteral("working-scene-linear");
    result.eligibility = QStringLiteral("legacy-working-scene");
    return result;
  };
  if (!output_size.isValid() || output_size.width() <= 0 || output_size.height() <= 0) {
    return fail(QStringLiteral("output-size-unavailable"));
  }
  if (!photographic_rendering_model_uses_aces2(
          color_pipeline_config.photographic_rendering_model) ||
      color_pipeline_config.render_intent != RenderIntent::Perceptual) {
    return fail(QStringLiteral("aces2-perceptual-output-required"));
  }
  const auto& target = purpose == DesktopRenderPurpose::Preview
      ? color_pipeline_config.preview_display_target
      : color_pipeline_config.export_display_target;
  if (target.eotf != DisplayEotf::Srgb ||
      target.mastering_mode != DisplayMasteringMode::Sdr ||
      target.color_space.primaries != ColorPrimaries::Srgb) {
    return fail(QStringLiteral("sdr-srgb-target-required"));
  }
  if (color_pipeline_config.soft_proof_enabled) {
    return fail(QStringLiteral("soft-proof-requires-working-scene-composition"));
  }
  if (purpose == DesktopRenderPurpose::Preview &&
      color_pipeline_config.preview_icc_profile_asset.status !=
          IccProfileAssetStatus::Unconfigured &&
      !color_pipeline_config.preview_icc_profile_asset.ready()) {
    return fail(QStringLiteral("device-icc-preview-profile-not-ready"));
  }
  if (purpose == DesktopRenderPurpose::Output &&
      color_pipeline_config.export_icc_profile_asset.status !=
          IccProfileAssetStatus::Unconfigured) {
    return fail(QStringLiteral("device-icc-output-requires-working-scene-composition"));
  }
  if (!identity_post_layer_geometry_and_detail(state)) {
    return fail(QStringLiteral("non-identity-post-layer-geometry-or-detail"));
  }
  const auto expanded = expand_smart_documents(state.layers, state.mask_stack,
      state.document_layer_style_light_angle);
  if (!expanded.error.isEmpty()) {
    return fail(QStringLiteral("component-layer-expansion-failed"));
  }
  if (expanded.layers.isEmpty()) {
    return fail(QStringLiteral("top-layer-is-not-text"));
  }
  QHash<QString, QVariantMap> layers_by_id;
  QHash<QString, QVariantList> children_by_parent;
  QVariantList roots;
  for (const auto& value : expanded.layers) {
    const auto layer = value.toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    if (!id.isEmpty() && layers_by_id.contains(id)) {
      return fail(QStringLiteral("component-layer-identities-are-not-unique"));
    }
    if (!id.isEmpty()) layers_by_id.insert(id, layer);
    const auto parent_id = layer.value(QStringLiteral("parentId")).toString().trimmed();
    if (parent_id.isEmpty()) roots.push_back(layer);
    else children_by_parent[parent_id].push_back(layer);
  }
  for (auto it = children_by_parent.cbegin(); it != children_by_parent.cend(); ++it) {
    const auto parent = layers_by_id.value(it.key());
    if (parent.isEmpty() || parent.value(QStringLiteral("kind")).toString()
            .trimmed().toLower() != QStringLiteral("group")) {
      return fail(QStringLiteral("component-layer-parent-is-not-a-group"));
    }
  }
  if (roots.isEmpty()) {
    return fail(QStringLiteral("top-layer-is-not-text"));
  }
  const auto first_kind = roots.constFirst().toMap()
                              .value(QStringLiteral("kind")).toString()
                              .trimmed().toLower();
  if (first_kind != QStringLiteral("text") &&
      first_kind != QStringLiteral("group")) {
    return fail(QStringLiteral("top-layer-is-not-text"));
  }
  const auto suffix = collect_display_text_suffix(
      roots, children_by_parent, output_size, expanded.masks);
  if (suffix.text_ids.isEmpty()) {
    return fail(QStringLiteral("no-topmost-plain-text-suffix"));
  }
  // A component repair samples and modifies the visible child composition in
  // the Layers stage. Keep text in that same working-scene composition so the
  // source used by Clone/Heal/Patch and the rendered repair see identical
  // pixels. The normal display-referred suffix remains available for documents
  // without component post-composite retouch.
  for (const auto& value : state.mask_stack) {
    if (value.toMap()
            .value(QStringLiteral("componentPostCompositeRetouchVersion"))
            .toInt() == 1) {
      return fail(QStringLiteral("component-post-composite-retouch-requires-working-scene-text"));
    }
  }
  result.render_color_domain = QStringLiteral("display-linear");
  result.eligibility = QStringLiteral("eligible-sdr-top-text-suffix");
  result.reason = QStringLiteral("eligible");
  result.eligible_layer_ids = suffix.text_ids;
  if (!suffix.group_ids.isEmpty()) {
    result.limits = QStringLiteral(
        "Opaque solid topmost text only; neutral normal groups with full opacity and no masks, effects, blend conditions, clipping, or adjustments may carry the suffix. Group transforms are retained. Direct own-mask rasters must cover the full canvas with matching aspect ratio and valid bytes. Preview ICC requires the exact PhotographicOutput->DeviceTransform->Publish suffix with a ready SDR profile; soft proof, export ICC, HDR targets, selection masks, effects, clipping, and non-identity detail/geometry remain in the working-scene path.");
  }
  return result;
}

std::vector<photo_editor::pipeline::LocalMaskSet>
compile_pipeline_local_mask_sets(
    const QVariantList& masks,
    DesktopRenderPurpose purpose,
    LocalMaskRasterPolicy raster_policy,
    const QSize& output_size,
    const QRectF& source_area,
    const QSizeF& original_basis_pixel_extent) {
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
        mask, raster_policy, output_size, source_area,original_basis_pixel_extent);
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
    const QSize& output_size,
    const QRectF& source_area,
    const QSizeF& original_basis_pixel_extent,
    std::vector<photo_editor::pipeline::LocalMask>* post_composite_masks) {
  std::vector<photo_editor::pipeline::LocalMask> retouch_masks;
  retouch_masks.reserve(static_cast<std::size_t>(masks.size()));
  auto retouch_solo_active = false;
  auto solo_ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), solo_ordinal);
    ++solo_ordinal;
    if (pipeline_mask_tool_role(mask) == QStringLiteral("retouch") &&
        !mask.value(QStringLiteral("committed")).toBool() && accepted_retouch_mask(mask).isEmpty()) continue;
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
    auto render_mask = mask;
    if (!mask.value(QStringLiteral("committed")).toBool()) {
      render_mask = accepted_retouch_mask(mask);
      if (render_mask.isEmpty()) continue;
    }
    auto mask_basis_pixel_extent = original_basis_pixel_extent;
    if (render_mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() == 1) {
      const auto component_width =
          render_mask.value(QStringLiteral("componentRetouchCanvasWidth")).toInt();
      const auto component_height =
          render_mask.value(QStringLiteral("componentRetouchCanvasHeight")).toInt();
      if (component_width <= 0 || component_height <= 0) continue;
      mask_basis_pixel_extent = QSizeF(component_width - 1, component_height - 1);
    }
    auto compiled_mask = compile_pipeline_local_mask(
        render_mask, raster_policy, output_size, source_area, mask_basis_pixel_extent);
    if (compiled_mask.component_post_composite_retouch) {
      if (post_composite_masks != nullptr) {
        post_composite_masks->push_back(std::move(compiled_mask));
      }
    } else {
      retouch_masks.push_back(std::move(compiled_mask));
    }
  }
  std::stable_sort(
      retouch_masks.begin(), retouch_masks.end(),
      [](const auto& left, const auto& right) {
        return left.retouch_order < right.retouch_order;
      });
  if (post_composite_masks != nullptr) {
    std::stable_sort(
        post_composite_masks->begin(), post_composite_masks->end(),
        [](const auto& left, const auto& right) {
          return left.retouch_order < right.retouch_order;
        });
  }
  return retouch_masks;
}

photo_editor::pipeline::ColorAdjustmentState
compile_desktop_source_prefix_state(
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata) {
  return compile_base_color_adjustment_state(state, metadata);
}

photo_editor::pipeline::ColorAdjustmentState
compile_desktop_pipeline_state(
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline_config,
    DesktopRenderPurpose purpose,
    LocalMaskRasterPolicy raster_policy,
    const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels,
    const DesktopSourceGeometry& source_geometry,
    const QImage* full_source,
    photo_editor::pipeline::CancellationToken cancellation) {
  auto pipeline_state = compile_base_color_adjustment_state(
      state, metadata, &color_pipeline_config);
  const auto mask_source_area = source_geometry.mask_area();
  const auto mask_basis_pixel_extent=source_geometry.valid()
      ? QSizeF(source_geometry.source_size.width()-1,source_geometry.source_size.height()-1) : QSizeF{};
  const auto edge_area = source_geometry.edge_area();
  pipeline_state.input_transform.source_area_x = edge_area.x();
  pipeline_state.input_transform.source_area_y = edge_area.y();
  pipeline_state.input_transform.source_area_width = edge_area.width();
  pipeline_state.input_transform.source_area_height = edge_area.height();
  pipeline_state.local_mask_sets = compile_pipeline_local_mask_sets(
      state.mask_stack, purpose, raster_policy, output_size, mask_source_area,mask_basis_pixel_extent);
  pipeline_state.retouch_masks = compile_pipeline_retouch_masks(
      state.mask_stack, raster_policy, output_size, mask_source_area, mask_basis_pixel_extent,
      &pipeline_state.post_composite_retouch_masks);
  auto display_text_policy = classify_display_referred_text_layers(
      state, color_pipeline_config, purpose, output_size);
  pipeline_state.display_referred_text_color_domain =
      display_text_policy.render_color_domain.toStdString();
  pipeline_state.display_referred_text_eligibility =
      display_text_policy.eligibility.toStdString();
  pipeline_state.display_referred_text_eligibility_reason =
      display_text_policy.reason.toStdString();
  pipeline_state.display_referred_text_policy_version =
      display_text_policy.version.toStdString();
  PhotoLayerSelectionSourceProvider selection_source_provider;
  std::optional<WorkingImage> full_working;
  std::optional<RecipeMaskSamplingContext> cached_sampling;
  QString cached_set_id;
  QString cached_set_name;
  QSize cached_raster_size;
  auto sampling_geometry = DesktopSourceGeometry{};
  if (full_source != nullptr && !full_source->isNull()) {
    const auto source_pixel_count =
        static_cast<qint64>(full_source->width()) * full_source->height();
    sampling_geometry = source_geometry.valid()
        ? source_geometry
        : DesktopSourceGeometry{full_source->size(), full_source->rect()};
    if (source_pixel_count > 0 && source_pixel_count <= 64LL * 1024LL * 1024LL &&
        sampling_geometry.source_size == full_source->size()) {
      const auto sampling_metadata = metadata != nullptr
          ? *metadata
          : photo_editor::pipeline::DecodedImageMetadata{};
      selection_source_provider =
          [&, sampling_metadata](const QString& mask_set_id,
                                 const QString& mask_set_name,
                                 const QSize& raster_size) {
            if (cancellation.cancelled()) return PhotoLayerSelectionSamplingSource{};
            if (cached_set_id != mask_set_id || cached_set_name != mask_set_name ||
                cached_raster_size != raster_size || !cached_sampling.has_value()) {
              if (!full_working.has_value()) {
                full_working = working_image_from_qimage(
                    *full_source,
                    state.working_color_profile,
                    &color_pipeline_config.input_icc_profile_asset,
                    metadata != nullptr &&
                        photo_editor::pipeline::decoded_image_requires_engine_input_transform(
                            *metadata));
                if (full_working->isNull()) return PhotoLayerSelectionSamplingSource{};
              }
              QVariantMap group_definition;
              auto ordinal = 1;
              for (const auto& value : state.mask_stack) {
                const auto normalized = normalized_mask_definition(value.toMap(), ordinal++);
                const auto candidate_id =
                    normalized.value(QStringLiteral("maskSetId")).toString().simplified();
                const auto candidate_name =
                    normalized.value(QStringLiteral("maskSetName")).toString().simplified();
                if ((!mask_set_id.isEmpty() && candidate_id == mask_set_id) ||
                    (mask_set_id.isEmpty() && !mask_set_name.isEmpty() &&
                     candidate_name == mask_set_name)) {
                  group_definition = normalized;
                  break;
                }
              }
              if (group_definition.isEmpty()) return PhotoLayerSelectionSamplingSource{};
              cached_sampling = prepare_recipe_mask_sampling(
                  *full_working, state, sampling_metadata, group_definition,
                  sampling_geometry, raster_size, cancellation, purpose, raster_policy);
              cached_set_id = mask_set_id;
              cached_set_name = mask_set_name;
              cached_raster_size = raster_size;
            }
            if (!cached_sampling->valid || cancellation.cancelled()) {
              return PhotoLayerSelectionSamplingSource{};
            }
            return PhotoLayerSelectionSamplingSource{
                .range_pixels = cached_sampling->range.pixels,
                .edge_pixels = cached_sampling->edge_pixels,
            };
          };
    }
  }
  pipeline_state.photo_layers = compile_pipeline_photo_layers(
      state.layers, state.mask_stack, raster_policy, output_size,
      source_pixels, color_pipeline_config, purpose, source_geometry,
      selection_source_provider, state.document_layer_style_light_angle);
  if (!display_text_policy.eligible_layer_ids.isEmpty()) {
    if (photo_render_profile_for_tests.enabled)
      ++photo_render_profile_for_tests.expansion_for_display_text_calls;
    const auto expanded = expand_smart_documents(state.layers, state.mask_stack,
        state.document_layer_style_light_angle);
    QSet<QString> display_group_ids;
    QSet<QString> display_text_ids;
    QHash<QString, QVariantMap> expanded_by_id;
    auto valid_selection = expanded.error.isEmpty();
    if (valid_selection) {
      for (const auto& value : expanded.layers) {
        const auto layer = value.toMap();
        expanded_by_id.insert(
            layer.value(QStringLiteral("id")).toString().trimmed(), layer);
      }
      for (const auto& id : display_text_policy.eligible_layer_ids) {
        if (!expanded_by_id.contains(id)) {
          valid_selection = false;
          break;
        }
        display_text_ids.insert(id);
        auto parent_id = expanded_by_id.value(id)
                             .value(QStringLiteral("parentId")).toString().trimmed();
        int depth = 0;
        while (!parent_id.isEmpty()) {
          if (++depth > 64 || !expanded_by_id.contains(parent_id)) {
            valid_selection = false;
            break;
          }
          const auto parent = expanded_by_id.value(parent_id);
          if (parent.value(QStringLiteral("kind")).toString().trimmed().toLower() !=
              QStringLiteral("group")) {
            valid_selection = false;
            break;
          }
          display_group_ids.insert(parent_id);
          parent_id = parent.value(QStringLiteral("parentId")).toString().trimmed();
        }
        if (!valid_selection) break;
      }
    }
    const auto is_display_group = [&display_group_ids](
        const photo_editor::pipeline::PhotoLayer& layer) {
      return layer.kind == "group" &&
          display_group_ids.contains(QString::fromStdString(layer.id));
    };
    const auto is_display_text = [&display_text_ids](
        const photo_editor::pipeline::PhotoLayer& layer) {
      return layer.kind == "text" &&
          display_text_ids.contains(QString::fromStdString(layer.id));
    };
    QSet<QString> seen_group_ids;
    QSet<QString> seen_text_ids;
    for (const auto& compiled : pipeline_state.photo_layers) {
      if (is_display_group(compiled)) {
        seen_group_ids.insert(QString::fromStdString(compiled.id));
        if (!compiled.pixels.empty() || !compiled.mask_data.empty() ||
            !compiled.effects.empty() || !compiled.effect_masks.empty()) {
          valid_selection = false;
          break;
        }
        continue;
      }
      if (is_display_text(compiled)) {
        const bool valid_text = !compiled.pixels.empty() &&
            compiled.pixel_width > 0 && compiled.pixel_height > 0 &&
            ((compiled.mask_width <= 0 && compiled.mask_height <= 0 &&
              compiled.mask_data.empty()) ||
             (compiled.mask_width > 0 && compiled.mask_height > 0 &&
              compiled.mask_data.size() ==
                  static_cast<std::size_t>(compiled.mask_width) *
                      static_cast<std::size_t>(compiled.mask_height) &&
              static_cast<qint64>(compiled.mask_width) * output_size.height() ==
                  static_cast<qint64>(compiled.mask_height) * output_size.width())) &&
            compiled.effects.empty() && compiled.effect_masks.empty() &&
            (compiled.text_material.empty() || compiled.text_material == "none");
        if (!valid_text) {
          valid_selection = false;
          break;
        }
        seen_text_ids.insert(QString::fromStdString(compiled.id));
      }
    }
    for (const auto& id : display_group_ids) {
      const auto group = std::find_if(
          pipeline_state.photo_layers.cbegin(), pipeline_state.photo_layers.cend(),
          [&id](const auto& layer) {
            return layer.kind == "group" && QString::fromStdString(layer.id) == id;
          });
      if (group == pipeline_state.photo_layers.cend()) valid_selection = false;
    }
    if (seen_text_ids != display_text_ids || seen_group_ids != display_group_ids) {
      valid_selection = false;
    }
    if (valid_selection) {
      std::vector<photo_editor::pipeline::PhotoLayer> scene_layers;
      std::vector<photo_editor::pipeline::PhotoLayer> display_layers;
      scene_layers.reserve(pipeline_state.photo_layers.size());
      display_layers.reserve(static_cast<std::size_t>(
          display_group_ids.size() + display_text_ids.size()));
      for (auto& compiled : pipeline_state.photo_layers) {
        if (is_display_group(compiled)) {
          // Groups are metadata-only rows, so retain one copy in the working
          // scene and one in the extracted text stack without duplicating any
          // raster payloads.
          display_layers.push_back(compiled);
          scene_layers.push_back(std::move(compiled));
        } else if (is_display_text(compiled)) {
          display_layers.push_back(std::move(compiled));
        } else {
          scene_layers.push_back(std::move(compiled));
        }
      }
      pipeline_state.display_referred_text_layers = std::move(display_layers);
      pipeline_state.photo_layers = std::move(scene_layers);
    } else {
      pipeline_state.display_referred_text_eligibility =
          "legacy-working-scene";
      pipeline_state.display_referred_text_color_domain =
          "working-scene-linear";
      pipeline_state.display_referred_text_eligibility_reason =
          "compiled-layer-content-does-not-match-policy";
    }
  }
  return pipeline_state;
}

}  // namespace photo_editor::desktop

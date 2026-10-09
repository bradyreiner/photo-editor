#include "desktop_pipeline_state_compiler.h"

#include "edit_recipe_mask_schema.h"
#include "edit_recipe_pipeline_bridge.h"
#include "photo_layer_compiler.h"
#include "local_mask_set_settings.h"
#include "photo_text_style.h"

#include <QHash>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <utility>

namespace photo_editor::desktop {
namespace {

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

bool plain_root_text_layer(const QVariantMap& layer, const QSize& output_size) {
  const auto kind = layer.value(QStringLiteral("kind")).toString()
                        .trimmed().toLower();
  if (kind != QStringLiteral("text") ||
      !layer.value(QStringLiteral("parentId")).toString().trimmed().isEmpty() ||
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
  if (!effects.isEmpty() || !layer.value(QStringLiteral("adjustments")).toMap().isEmpty()) {
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
  if (state.layers.isEmpty() ||
      state.layers.constFirst().toMap().value(QStringLiteral("kind"))
          .toString().trimmed().toLower() != QStringLiteral("text")) {
    return fail(QStringLiteral("top-layer-is-not-text"));
  }
  QStringList ids;
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString().trimmed().toLower() !=
        QStringLiteral("text")) {
      break;
    }
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty() || !plain_root_text_layer(layer, output_size)) {
      return fail(QStringLiteral("topmost-text-suffix-is-not-plain-opaque-root-text"));
    }
    ids.push_back(id);
  }
  if (ids.isEmpty()) {
    return fail(QStringLiteral("no-topmost-plain-text-suffix"));
  }
  result.render_color_domain = QStringLiteral("display-linear");
  result.eligibility = QStringLiteral("eligible-sdr-top-text-suffix");
  result.reason = QStringLiteral("eligible");
  result.eligible_layer_ids = std::move(ids);
  return result;
}

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
  pipeline_state.photo_layers = compile_pipeline_photo_layers(
      state.layers, state.mask_stack, raster_policy, output_size,
      source_pixels, color_pipeline_config, purpose);
  const auto candidate_count = static_cast<std::size_t>(
      display_text_policy.eligible_layer_ids.size());
  if (candidate_count > 0U && candidate_count <= pipeline_state.photo_layers.size()) {
    const auto suffix_begin = pipeline_state.photo_layers.size() - candidate_count;
    auto suffix_matches = true;
    for (std::size_t index = 0; index < candidate_count; ++index) {
      const auto expected_id = display_text_policy.eligible_layer_ids.at(
          static_cast<qsizetype>(candidate_count - index - 1U)).toStdString();
      const auto& compiled = pipeline_state.photo_layers[suffix_begin + index];
      if (compiled.id != expected_id || compiled.kind != "text" ||
          compiled.pixels.empty() || compiled.pixel_width <= 0 ||
          compiled.pixel_height <= 0 ||
          ((compiled.mask_width > 0 || compiled.mask_height > 0 ||
            !compiled.mask_data.empty()) &&
           (compiled.mask_width <= 0 || compiled.mask_height <= 0 ||
            compiled.mask_data.size() !=
                static_cast<std::size_t>(compiled.mask_width) *
                    static_cast<std::size_t>(compiled.mask_height) ||
            static_cast<qint64>(compiled.mask_width) * output_size.height() !=
                static_cast<qint64>(compiled.mask_height) * output_size.width())) ||
          !compiled.effects.empty() || !compiled.effect_masks.empty() ||
          (!compiled.text_material.empty() &&
           compiled.text_material != "none")) {
        suffix_matches = false;
        break;
      }
    }
    if (suffix_matches) {
      pipeline_state.display_referred_text_layers.assign(
          std::make_move_iterator(pipeline_state.photo_layers.begin() +
                                  static_cast<std::ptrdiff_t>(suffix_begin)),
          std::make_move_iterator(pipeline_state.photo_layers.end()));
      pipeline_state.photo_layers.erase(
          pipeline_state.photo_layers.begin() + static_cast<std::ptrdiff_t>(suffix_begin),
          pipeline_state.photo_layers.end());
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

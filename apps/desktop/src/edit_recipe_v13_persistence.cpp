#include "edit_recipe_v13_persistence.h"
#include "photo_layer_effect_codec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "photo_editor/pipeline/edit_recipe_v13.h"

#include <QVariantList>
#include <QVariantMap>

#include <array>

namespace photo_editor::desktop {
namespace {

constexpr auto kCanonicalRecipeProvenanceSchema =
    "photo-editor-edit-recipe-provenance:v2";

QString serialize_recipe_provenance(
    const EditRecipeState& state) {
  return QString::fromUtf8(
      QJsonDocument(
          QJsonObject{
              {QStringLiteral("schema"),
               QString::fromLatin1(
                   kCanonicalRecipeProvenanceSchema)},
              {QStringLiteral("presetApplications"),
               QJsonArray::fromVariantList(
                   state.preset_applications)},
              {QStringLiteral("history"), QJsonObject{
                  {QStringLiteral("activeBranchId"), normalized_history_branch_id(state.active_history_branch)},
                  {QStringLiteral("snapshots"), QJsonArray::fromVariantList(state.recipe_snapshots)},
              }},
          })
          .toJson(QJsonDocument::Compact));
}

bool restore_recipe_provenance(
    const QString& payload,
    EditRecipeState* state,
    QString* error) {
  if (state == nullptr) {
    return false;
  }
  if (payload.trimmed().isEmpty()) {
    state->preset_applications.clear();
    state->active_history_branch = QStringLiteral("main");
    state->recipe_snapshots.clear();
    return true;
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(
      payload.toUtf8(),
      &parse_error);
  if (parse_error.error != QJsonParseError::NoError ||
      !document.isObject()) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "canonical recipe provenance is not a JSON object");
    }
    return false;
  }
  const auto object = document.object();
  const auto applications =
      object.value(QStringLiteral("presetApplications"));
  const auto schema = object.value(QStringLiteral("schema")).toString();
  const bool legacy = schema == QStringLiteral("photo-editor-edit-recipe-provenance:v1");
  const auto history = object.value(QStringLiteral("history")).toObject();
  const auto branch = history.value(QStringLiteral("activeBranchId"));
  const auto snapshots = history.value(QStringLiteral("snapshots"));
  const bool valid_history = history.size() == 2 && branch.isString() &&
      !branch.toString().trimmed().isEmpty() && snapshots.isArray();
  if (!applications.isArray() ||
      (legacy ? object.size() != 2 :
          (schema != QString::fromLatin1(kCanonicalRecipeProvenanceSchema) ||
           object.size() != 3 || !valid_history))) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "canonical recipe provenance does not match the v1 or v2 schema");
    }
    return false;
  }
  state->preset_applications =
      applications.toArray().toVariantList();
  state->active_history_branch = legacy ? QStringLiteral("main") : branch.toString();
  state->recipe_snapshots = legacy ? QVariantList{} : snapshots.toArray().toVariantList();
  return true;
}

double percent(double value) {
  return value * 100.0;
}

QVariantList curve_points(
    const std::vector<
        photo_editor::pipeline::CurvePointV13>& points) {
  QVariantList result;
  result.reserve(
      static_cast<qsizetype>(points.size()));
  for (const auto& point : points) {
    result.push_back(
        QVariantMap{
            {QStringLiteral("x"), point.x},
            {QStringLiteral("y"), point.y},
        });
  }
  return result;
}

QString demosaic_quality(
    photo_editor::pipeline::RawDemosaicQualityV13 value) {
  using enum photo_editor::pipeline::RawDemosaicQualityV13;
  switch (value) {
    case Preview:
      return QStringLiteral("Preview");
    case Balanced:
      return QStringLiteral("Balanced");
    case Full:
      return QStringLiteral("Full");
  }
  return {};
}

QString highlight_reconstruction(
    photo_editor::pipeline::HighlightReconstructionModeV13 value) {
  using enum photo_editor::pipeline::HighlightReconstructionModeV13;
  switch (value) {
    case Disabled:
      return QStringLiteral("Disabled");
    case CameraDefault:
      return QStringLiteral("Camera Default");
    case Enabled:
      return QStringLiteral("Enabled");
  }
  return {};
}

QString chromatic_adaptation(
    photo_editor::pipeline::ChromaticAdaptationMethodV13 value) {
  using enum photo_editor::pipeline::ChromaticAdaptationMethodV13;
  switch (value) {
    case Bradford:
      return QStringLiteral("Bradford");
    case Cat02:
      return QStringLiteral("CAT02");
  }
  return {};
}

QString camera_look(
    photo_editor::pipeline::CameraMatchingLookV13 value) {
  using enum photo_editor::pipeline::CameraMatchingLookV13;
  switch (value) {
    case Standard:
      return QStringLiteral("Standard");
    case Neutral:
      return QStringLiteral("Neutral");
    case Vivid:
      return QStringLiteral("Vivid");
    case Portrait:
      return QStringLiteral("Portrait");
    case Landscape:
      return QStringLiteral("Landscape");
    case Monochrome:
      return QStringLiteral("Monochrome");
  }
  return {};
}

QString mixer_model(
    photo_editor::pipeline::ColorMixerModelV13 value) {
  using enum photo_editor::pipeline::ColorMixerModelV13;
  switch (value) {
    case Oklab:
      return QStringLiteral("OKLab");
    case Hsl:
      return QStringLiteral("HSL");
    case Hsv:
      return QStringLiteral("HSV");
    case Lch:
      return QStringLiteral("LCH");
  }
  return {};
}

QString harmony_mode(
    photo_editor::pipeline::ColorHarmonyModeV13 value) {
  using enum photo_editor::pipeline::ColorHarmonyModeV13;
  switch (value) {
    case Complementary:
      return QStringLiteral("Complementary");
    case Analogous:
      return QStringLiteral("Analogous");
    case Triadic:
      return QStringLiteral("Triadic");
    case SplitComplementary:
      return QStringLiteral("Split Complementary");
    case Tetradic:
      return QStringLiteral("Tetradic");
  }
  return {};
}

QString upright_mode(
    photo_editor::pipeline::UprightMode value) {
  using enum photo_editor::pipeline::UprightMode;
  switch (value) {
    case Off:
      return QStringLiteral("Off");
    case Vertical:
      return QStringLiteral("Vertical");
    case Full:
      return QStringLiteral("Full");
    case Guided:
      return QStringLiteral("Guided");
  }
  return {};
}

QString render_intent(
    photo_editor::pipeline::OutputRenderIntent value) {
  using enum photo_editor::pipeline::OutputRenderIntent;
  switch (value) {
    case Perceptual:
      return QStringLiteral("Perceptual");
    case RelativeColorimetric:
      return QStringLiteral("Relative Colorimetric");
    case AbsoluteColorimetric:
      return QStringLiteral("Absolute Colorimetric");
    case Saturation:
      return QStringLiteral("Saturation");
  }
  return {};
}

QString gamut_policy(
    photo_editor::pipeline::OutputGamutMappingPolicy value) {
  using enum photo_editor::pipeline::OutputGamutMappingPolicy;
  switch (value) {
    case Perceptual:
      return QStringLiteral("Perceptual");
    case Clip:
      return QStringLiteral("Clip");
    case PreserveSaturation:
      return QStringLiteral("Preserve Saturation");
  }
  return {};
}

}  // namespace

CanonicalRecipeV13WriteResult
serialize_desktop_recipe_as_canonical_v13(
    const EditRecipeState& state,
    const DesktopRecipeV13Context& context) {
  CanonicalRecipeV13WriteResult result;
  auto adapted = adapt_desktop_recipe_to_v13(state, context);
  result.errors = std::move(adapted.errors);
  if (!adapted.recipe.has_value() || !result.errors.empty()) {
    return result;
  }

  const auto normalized =
      photo_editor::pipeline::normalize_edit_recipe_v13(
          *adapted.recipe);
  const auto validation =
      photo_editor::pipeline::validate_edit_recipe_v13(
          normalized);
  if (!validation.ok()) {
    result.errors.reserve(
        result.errors.size() + validation.errors.size());
    for (const auto& error : validation.errors) {
      result.errors.push_back(
          DesktopRecipeAdapterError{
              .code =
                  DesktopRecipeAdapterErrorCode::
                      InvalidV13Recipe,
              .path = error.path,
              .message = error.message,
          });
    }
    return result;
  }

  result.document.recipe_payload =
      QString::fromStdString(
          photo_editor::pipeline::
              serialize_edit_recipe_v13(normalized));
  result.document.cache_key =
      QString::fromStdString(
          photo_editor::pipeline::
              edit_recipe_v13_cache_key(normalized));
  result.document.provenance_payload =
      serialize_recipe_provenance(state);
  return result;
}

CanonicalRecipeV13AssetWriteResult
serialize_desktop_recipe_as_canonical_v13_with_assets(
    const EditRecipeState& state,
    const DesktopRecipeV13Context& base_context,
    const RecipeAssetStore& asset_store) {
  CanonicalRecipeV13AssetWriteResult result;
  auto context = base_context;
  const auto add_store_error =
      [&](const QString& path, const QString& error) {
        result.errors.push_back(
            DesktopRecipeAdapterError{
                .code =
                    DesktopRecipeAdapterErrorCode::
                        InvalidV13Recipe,
                .path = path.toStdString(),
                .message = error.toStdString(),
            });
      };

  const auto masks = normalize_mask_stack(state.mask_stack);
  for (qsizetype index = 0; index < masks.size();
       ++index) {
    const auto mask = masks.at(index).toMap();
    const auto source_id =
        mask.value(QStringLiteral("id"))
            .toString()
            .trimmed();
    if (source_id.isEmpty()) {
      add_store_error(
          QStringLiteral("masks[%1].id").arg(index),
          QStringLiteral(
              "Mask payload requires a stable source id."));
      continue;
    }
    const auto stored =
        asset_store.putVariant(mask);
    if (!stored.ok()) {
      add_store_error(
          QStringLiteral("masks[%1].payload")
              .arg(index),
          stored.error);
      continue;
    }
    context.mask_payload_assets.push_back(
        DesktopRecipeV13Context::MaskPayloadBinding{
            .source_id = source_id,
            .payload_asset = *stored.asset,
        });
    ++result.stored_asset_count;
  }

  for (qsizetype layer_index = 0;
       layer_index < state.layers.size();
       ++layer_index) {
    const auto layer =
        state.layers.at(layer_index).toMap();
    const auto layer_id =
        layer.value(QStringLiteral("id"))
            .toString()
            .trimmed();
    if (layer_id.isEmpty()) {
      add_store_error(
          QStringLiteral("layers[%1].id")
              .arg(layer_index),
          QStringLiteral(
              "Layer content requires a stable layer id."));
      continue;
    }
    const auto stored =
        asset_store.putVariant(layer);
    if (!stored.ok()) {
      add_store_error(
          QStringLiteral("layers[%1].content")
              .arg(layer_index),
          stored.error);
    } else {
      context.layer_content_assets.push_back(
          DesktopRecipeV13Context::LayerContentBinding{
              .layer_id = layer_id,
              .content_asset = *stored.asset,
          });
      ++result.stored_asset_count;
    }

    const auto adjustments =
        layer.value(QStringLiteral("adjustments"))
            .toMap();
    auto luts =
        adjustments.value(QStringLiteral("lutStack"))
            .toList();
    if (luts.isEmpty() &&
        adjustments
            .value(QStringLiteral("lut"))
            .canConvert<QVariantMap>()) {
      luts.push_back(
          adjustments.value(QStringLiteral("lut")));
    }
    for (qsizetype lut_index = 0;
         lut_index < luts.size();
         ++lut_index) {
      const auto stored =
          asset_store.putVariant(luts.at(lut_index));
      if (!stored.ok()) {
        add_store_error(
            QStringLiteral(
                "layers[%1].look.luts[%2]")
                .arg(layer_index)
                .arg(lut_index),
            stored.error);
        continue;
      }
      context.lut_payload_assets.push_back(
          DesktopRecipeV13Context::LutPayloadBinding{
              .layer_id = layer_id,
              .index =
                  static_cast<std::size_t>(lut_index),
              .payload_asset = *stored.asset,
          });
      ++result.stored_asset_count;
    }
  }
  if (!result.errors.empty()) {
    return result;
  }

  const auto written =
      serialize_desktop_recipe_as_canonical_v13(
          state,
          context);
  result.document = written.document;
  result.errors = written.errors;
  return result;
}

CanonicalRecipeV13ProjectionResult
project_canonical_v13_to_desktop_state(
    const photo_editor::pipeline::EditRecipeV13& recipe) {
  CanonicalRecipeV13ProjectionResult result;
  const auto normalized =
      photo_editor::pipeline::normalize_edit_recipe_v13(
          recipe);
  const auto validation =
      photo_editor::pipeline::validate_edit_recipe_v13(
          normalized);
  for (const auto& error : validation.errors) {
    result.errors.push_back(
        DesktopRecipeAdapterError{
            .code =
                DesktopRecipeAdapterErrorCode::
                    InvalidV13Recipe,
            .path = error.path,
            .message = error.message,
        });
  }
  if (!normalized.masks.empty() ||
      !normalized.layers.empty() ||
      !normalized.look.luts.empty()) {
    result.errors.push_back(
        DesktopRecipeAdapterError{
            .code =
                DesktopRecipeAdapterErrorCode::
                    UnsupportedLegacyIntent,
            .path = "asset_bindings",
            .message =
                "v13 mask, layer, and LUT projection requires an external asset resolver",
        });
  }
  if (!result.errors.empty()) {
    return result;
  }

  EditRecipeState state;
  state.photographic_rendering_model =
      QStringLiteral("aces2-ocio-v1");
  state.renderer_transition_look =
      normalized.output.renderer_transition_look ==
              photo_editor::pipeline::RendererTransitionLookV13::
                  PreserveLegacyDirectLuminanceV1
          ? QStringLiteral("preserve-legacy-direct-luminance-v1")
          : QStringLiteral("none");
  state.raw_input_transform_enabled =
      normalized.input.input_transform_enabled;
  state.raw_demosaic_quality =
      demosaic_quality(normalized.input.demosaic_quality);
  state.highlight_reconstruction_mode =
      highlight_reconstruction(
          normalized.input.highlight_reconstruction);
  state.chromatic_adaptation_method =
      chromatic_adaptation(
          normalized.input.chromatic_adaptation);
  state.raw_camera_matching_look =
      camera_look(normalized.input.camera_matching_look);
  state.raw_hot_dead_pixel_cleanup =
      normalized.input.hot_dead_pixel_cleanup;
  state.raw_false_color_suppression =
      normalized.input.false_color_suppression;
  state.raw_highlight_recovery =
      normalized.input.highlight_recovery_enabled;
  state.raw_white_balance_override =
      normalized.input.white_balance_temperature_kelvin
          .has_value() ||
      normalized.input.white_balance_tint.has_value();
  state.raw_white_balance_temperature =
      normalized.input.white_balance_temperature_kelvin
          .value_or(
              state.raw_white_balance_temperature);
  state.raw_white_balance_tint =
      percent(
          normalized.input.white_balance_tint.value_or(
              0.0));
  state.raw_black_level =
      normalized.input.black_level_override.value_or(0.0);
  state.raw_white_level =
      normalized.input.white_level_override.value_or(1.0);
  state.raw_calibration_hue =
      percent(normalized.input.calibration_hue);
  state.raw_calibration_saturation =
      percent(normalized.input.calibration_saturation);
  state.raw_red_primary_hue =
      percent(normalized.input.red_primary_hue);
  state.raw_red_primary_saturation =
      percent(normalized.input.red_primary_saturation);
  state.raw_green_primary_hue =
      percent(normalized.input.green_primary_hue);
  state.raw_green_primary_saturation =
      percent(normalized.input.green_primary_saturation);
  state.raw_blue_primary_hue =
      percent(normalized.input.blue_primary_hue);
  state.raw_blue_primary_saturation =
      percent(normalized.input.blue_primary_saturation);
  if (normalized.input.camera_profile.has_value()) {
    const auto asset_id = QString::fromStdString(
        normalized.input.camera_profile->value);
    if (normalized.input.camera_profile_mode ==
        photo_editor::pipeline::CameraProfileMode::Dcp) {
      state.camera_dcp_profile_asset.asset_id =
          asset_id;
    } else if (
        normalized.input.camera_profile_mode ==
        photo_editor::pipeline::CameraProfileMode::Icc) {
      state.input_icc_profile_asset.asset_id =
          asset_id;
    }
  }

  state.exposure = normalized.scene.exposure_ev;
  state.contrast = percent(normalized.tone.contrast);
  state.highlights = percent(normalized.tone.highlights);
  state.shadows = percent(normalized.tone.shadows);
  state.whites = percent(normalized.tone.whites);
  state.blacks = percent(normalized.tone.blacks);
  state.brightness = percent(normalized.tone.brightness);
  state.hdr_local_tone_mapping =
      percent(normalized.tone.hdr_local_tone_mapping);
  state.filmic_shoulder =
      percent(normalized.tone.filmic_shoulder);
  state.filmic_toe = percent(normalized.tone.filmic_toe);
  state.adaptive_contrast =
      percent(normalized.tone.adaptive_contrast);
  state.levels_black_point =
      percent(normalized.tone.levels_black_point);
  state.levels_gamma =
      percent(normalized.tone.levels_gamma);
  state.levels_white_point =
      percent(normalized.tone.levels_white_point);
  state.zone_deep_shadows =
      percent(normalized.tone.zone_deep_shadows);
  state.zone_shadows =
      percent(normalized.tone.zone_shadows);
  state.zone_midtones =
      percent(normalized.tone.zone_midtones);
  state.zone_lights =
      percent(normalized.tone.zone_lights);
  state.zone_highlights =
      percent(normalized.tone.zone_highlights);

  state.master_curve_points =
      curve_points(normalized.curves.master);
  state.luma_curve_points =
      curve_points(normalized.curves.luma);
  state.red_curve_points =
      curve_points(normalized.curves.red);
  state.green_curve_points =
      curve_points(normalized.curves.green);
  state.blue_curve_points =
      curve_points(normalized.curves.blue);

  state.temperature = percent(normalized.look.temperature);
  state.tint = percent(normalized.look.tint);
  state.vibrance = percent(normalized.look.vibrance);
  state.saturation = percent(normalized.look.saturation);
  state.color_mixer_model =
      mixer_model(normalized.look.mixer_model);
  for (std::size_t index = 0;
       index < state.mixer_channels.size();
       ++index) {
    const auto& source =
        normalized.look.mixer_channels[index];
    auto& target = state.mixer_channels[index];
    target.hue = percent(source.hue);
    target.saturation = percent(source.saturation);
    target.luminance = percent(source.luminance);
    target.selective_cyan =
        percent(source.selective_cyan);
    target.selective_magenta =
        percent(source.selective_magenta);
    target.selective_yellow =
        percent(source.selective_yellow);
    target.selective_black =
        percent(source.selective_black);
  }
  const auto& source_rgb =
      normalized.look.rgb_channel_mixer;
  auto& target_rgb = state.rgb_channel_mixer;
  target_rgb.red_from_red =
      percent(source_rgb.red_from_red);
  target_rgb.red_from_green =
      percent(source_rgb.red_from_green);
  target_rgb.red_from_blue =
      percent(source_rgb.red_from_blue);
  target_rgb.green_from_red =
      percent(source_rgb.green_from_red);
  target_rgb.green_from_green =
      percent(source_rgb.green_from_green);
  target_rgb.green_from_blue =
      percent(source_rgb.green_from_blue);
  target_rgb.blue_from_red =
      percent(source_rgb.blue_from_red);
  target_rgb.blue_from_green =
      percent(source_rgb.blue_from_green);
  target_rgb.blue_from_blue =
      percent(source_rgb.blue_from_blue);
  state.black_and_white_enabled =
      normalized.look.black_and_white_enabled;
  for (std::size_t index = 0;
       index < state.black_and_white_mixer.size();
       ++index) {
    state.black_and_white_mixer[index] = percent(
        normalized.look.black_and_white_mixer[index]);
  }
  state.color_replacement_enabled =
      normalized.look.color_replacement_enabled;
  state.color_replacement_source_hue =
      normalized.look.color_replacement_source_hue;
  state.color_replacement_target_hue =
      normalized.look.color_replacement_target_hue;
  state.color_replacement_range =
      percent(normalized.look.color_replacement_range);
  state.color_replacement_softness =
      percent(normalized.look.color_replacement_softness);
  state.color_replacement_strength =
      percent(normalized.look.color_replacement_strength);
  state.color_replacement_preserve_luminance =
      normalized.look
          .color_replacement_preserve_luminance;
  state.color_replacement_preserve_saturation =
      normalized.look
          .color_replacement_preserve_saturation;
  state.skin_tone_protection_enabled =
      normalized.look.skin_tone_protection_enabled;
  state.skin_tone_line_hue =
      normalized.look.skin_tone_line_hue;
  state.skin_tone_protection_range =
      percent(normalized.look.skin_tone_protection_range);
  state.skin_tone_protection_strength =
      percent(
          normalized.look.skin_tone_protection_strength);
  state.color_harmony_enabled =
      normalized.look.color_harmony_enabled;
  state.color_harmony_mode =
      harmony_mode(normalized.look.color_harmony_mode);
  state.color_harmony_base_hue =
      normalized.look.color_harmony_base_hue;
  state.color_harmony_strength =
      percent(normalized.look.color_harmony_strength);
  state.grading_shadows_hue =
      normalized.look.grading_shadows.hue;
  state.grading_shadows_saturation =
      percent(normalized.look.grading_shadows.saturation);
  state.grading_shadows_luminance =
      percent(normalized.look.grading_shadows.luminance);
  state.grading_midtones_hue =
      normalized.look.grading_midtones.hue;
  state.grading_midtones_saturation =
      percent(normalized.look.grading_midtones.saturation);
  state.grading_midtones_luminance =
      percent(normalized.look.grading_midtones.luminance);
  state.grading_highlights_hue =
      normalized.look.grading_highlights.hue;
  state.grading_highlights_saturation =
      percent(normalized.look.grading_highlights.saturation);
  state.grading_highlights_luminance =
      percent(normalized.look.grading_highlights.luminance);
  state.grading_amount =
      percent(normalized.look.grading_amount);
  state.grading_balance =
      percent(normalized.look.grading_balance);

  state.texture = percent(normalized.detail.texture);
  state.clarity = percent(normalized.detail.clarity);
  state.dehaze = percent(normalized.detail.dehaze);
  state.sharpening =
      percent(normalized.detail.sharpening);
  state.sharpen_radius =
      normalized.detail.sharpen_radius;
  state.sharpen_detail =
      percent(normalized.detail.sharpen_detail);
  state.sharpen_masking =
      percent(normalized.detail.sharpen_masking);
  state.luminance_nr =
      percent(
          normalized.detail.luminance_noise_reduction);
  state.color_nr =
      percent(
          normalized.detail.chroma_noise_reduction);

  state.crop_rect_x = normalized.geometry.crop_x;
  state.crop_rect_y = normalized.geometry.crop_y;
  state.crop_rect_width =
      normalized.geometry.crop_width;
  state.crop_rect_height =
      normalized.geometry.crop_height;
  // Canonical geometry represents an uncropped image as the unit rectangle.
  // Restore the desktop's neutral sentinel instead of inventing a crop edit.
  // Only do this at neutral zoom: explicit rectangles bypass legacy crop zoom.
  if (state.crop_rect_x == 0.0 && state.crop_rect_y == 0.0 &&
      state.crop_rect_width == 1.0 && state.crop_rect_height == 1.0 &&
      normalized.geometry.crop_zoom == 1.0) {
    state.crop_rect_width = 0.0;
    state.crop_rect_height = 0.0;
  }
  state.transform_rotation =
      normalized.geometry.rotation_degrees;
  state.lens_distortion =
      percent(normalized.geometry.lens_distortion);
  state.lens_vignette =
      percent(normalized.geometry.lens_vignette);
  state.chromatic_aberration =
      percent(normalized.geometry.chromatic_aberration);
  state.perspective_vertical =
      percent(normalized.geometry.perspective_vertical);
  state.perspective_horizontal =
      percent(normalized.geometry.perspective_horizontal);
  state.transform_scale = normalized.geometry.scale;
  state.transform_skew_x =
      percent(normalized.geometry.skew_x);
  state.transform_skew_y =
      percent(normalized.geometry.skew_y);
  state.crop_zoom = normalized.geometry.crop_zoom;
  state.upright_mode =
      upright_mode(normalized.geometry.upright_mode);
  state.liquify_center_x =
      normalized.geometry.liquify_center_x;
  state.liquify_center_y =
      normalized.geometry.liquify_center_y;
  state.liquify_radius =
      normalized.geometry.liquify_radius;
  state.liquify_strength =
      percent(normalized.geometry.liquify_strength);
  state.content_aware_crop_fill =
      normalized.geometry.content_aware_crop_fill;

  state.output_headroom =
      normalized.output.peak_luminance_nits / 100.0;
  switch (normalized.output.export_color_space) {
    using enum photo_editor::pipeline::ExportColorSpaceV13;
    case Srgb: state.output_color_space = QStringLiteral("sRGB"); break;
    case DisplayP3: state.output_color_space = QStringLiteral("Display P3"); break;
    case AdobeRgb: state.output_color_space = QStringLiteral("Adobe RGB"); break;
    case ProPhotoRgb: state.output_color_space = QStringLiteral("ProPhoto RGB"); break;
    case Rec709: state.output_color_space = QStringLiteral("Rec.709"); break;
    case Rec2020: state.output_color_space = QStringLiteral("Rec.2020"); break;
  }
  state.render_intent =
      render_intent(normalized.output.render_intent);
  state.output_gamut_mapping_enabled =
      normalized.output.gamut_mapping_enabled;
  state.output_gamut_mapping_policy =
      gamut_policy(
          normalized.output.gamut_mapping_policy);
  state.black_point_compensation_enabled =
      normalized.output.black_point_compensation;
  state.source_black_point =
      normalized.output.source_black_point;
  state.output_black_point =
      normalized.output.output_black_point;
  state.soft_proof_enabled =
      normalized.proof.mode ==
      photo_editor::pipeline::ProofMode::Icc;
  state.proof_simulate_paper_white =
      normalized.proof.simulate_paper;
  state.proof_simulate_ink_black =
      normalized.proof.simulate_ink_black;
  if (normalized.proof.profile.has_value()) {
    const auto profile = QString::fromStdString(
        normalized.proof.profile->value);
    if (profile.startsWith(
            QStringLiteral("builtin:icc:"))) {
      state.soft_proof_profile =
          profile.mid(
              QStringLiteral("builtin:icc:").size());
    } else {
      state.proof_icc_profile_asset.asset_id =
          profile;
    }
  }

  result.state = std::move(state);
  return result;
}

CanonicalRecipeV13ProjectionResult
project_canonical_v13_to_desktop_state_with_assets(
    const photo_editor::pipeline::EditRecipeV13& recipe,
    const RecipeAssetStore& asset_store) {
  CanonicalRecipeV13ProjectionResult result;
  auto structural_recipe =
      photo_editor::pipeline::normalize_edit_recipe_v13(
          recipe);
  structural_recipe.masks.clear();
  structural_recipe.layers.clear();
  structural_recipe.look.luts.clear();
  auto projected =
      project_canonical_v13_to_desktop_state(
          structural_recipe);
  if (!projected.ok()) {
    return projected;
  }
  auto state = std::move(*projected.state);

  for (std::size_t set_index = 0;
       set_index < recipe.masks.size();
       ++set_index) {
    const auto& mask_set = recipe.masks[set_index];
    for (std::size_t source_index = 0;
         source_index < mask_set.sources.size();
         ++source_index) {
      const auto& source =
          mask_set.sources[source_index];
      if (!source.payload_asset.has_value()) {
        result.errors.push_back(
            DesktopRecipeAdapterError{
                .code =
                    DesktopRecipeAdapterErrorCode::
                        MissingContext,
                .path =
                    "masks[" +
                    std::to_string(set_index) +
                    "].sources[" +
                    std::to_string(source_index) +
                    "].payload_asset",
                .message =
                    "asset-backed desktop projection requires a stored mask payload",
            });
        continue;
      }
      QString asset_error;
      const auto restored =
          asset_store.getVariant(
              *source.payload_asset,
              &asset_error);
      if (!restored.has_value() ||
          !restored->canConvert<QVariantMap>()) {
        result.errors.push_back(
            DesktopRecipeAdapterError{
                .code =
                    DesktopRecipeAdapterErrorCode::
                        MissingContext,
                .path =
                    "masks[" +
                    std::to_string(set_index) +
                    "].sources[" +
                    std::to_string(source_index) +
                    "].payload_asset",
                .message =
                    asset_error.isEmpty()
                        ? "stored mask payload is not a map"
                        : asset_error.toStdString(),
            });
        continue;
      }
      auto mask = restored->toMap();
      const auto restored_id =
          mask.value(QStringLiteral("id"))
              .toString()
              .trimmed();
      if (restored_id.toStdString() !=
          source.id.value) {
        result.errors.push_back(
            DesktopRecipeAdapterError{
                .code =
                    DesktopRecipeAdapterErrorCode::
                        InvalidV13Recipe,
                .path =
                    "masks[" +
                    std::to_string(set_index) +
                    "].sources[" +
                    std::to_string(source_index) +
                    "].id",
                .message =
                    "stored mask payload identity does not match the canonical source",
            });
        continue;
      }
      mask.insert(
          QStringLiteral("maskSetId"),
          QString::fromStdString(mask_set.id.value));
      state.mask_stack.push_back(mask);
    }
  }

  for (std::size_t layer_index = 0;
       layer_index < recipe.layers.size();
       ++layer_index) {
    const auto& layer = recipe.layers[layer_index];
    if (layer.id.value.starts_with(
            "desktop-mask-source-layer:")) {
      continue;
    }
    if (!layer.content_asset.has_value()) {
      result.errors.push_back(
          DesktopRecipeAdapterError{
              .code =
                  DesktopRecipeAdapterErrorCode::
                      MissingContext,
              .path =
                  "layers[" +
                  std::to_string(layer_index) +
                  "].content_asset",
              .message =
                  "adjustment and group layer projection is not implemented without a stored presentation asset",
          });
      continue;
    }
    QString asset_error;
    const auto restored =
        asset_store.getVariant(
            *layer.content_asset,
            &asset_error);
    if (!restored.has_value() ||
        !restored->canConvert<QVariantMap>()) {
      result.errors.push_back(
          DesktopRecipeAdapterError{
              .code =
                  DesktopRecipeAdapterErrorCode::
                      MissingContext,
              .path =
                  "layers[" +
                  std::to_string(layer_index) +
                  "].content_asset",
              .message =
                  asset_error.isEmpty()
                      ? "stored layer content is not a map"
                      : asset_error.toStdString(),
          });
      continue;
    }
    const auto restored_layer = restored->toMap();
    if (restored_layer
            .value(QStringLiteral("id"))
            .toString()
            .trimmed()
            .toStdString() != layer.id.value) {
      result.errors.push_back(
          DesktopRecipeAdapterError{
              .code =
                  DesktopRecipeAdapterErrorCode::
                      InvalidV13Recipe,
              .path =
                  "layers[" +
                  std::to_string(layer_index) +
                  "].id",
              .message =
                  "stored layer content identity does not match the canonical layer",
          });
      continue;
    }
    QString effect_error;
    const auto effects = decode_photo_layer_effects(restored_layer, &effect_error);
    if (!effects || *effects != layer.effects) {
      result.errors.push_back(DesktopRecipeAdapterError{
          .code = DesktopRecipeAdapterErrorCode::InvalidV13Recipe,
          .path = "layers[" + std::to_string(layer_index) + "].effects",
          .message = effects ? "stored layer effects do not match the canonical recipe" : effect_error.toStdString(),
      });
      continue;
    }
    state.layers.push_back(restored_layer);
  }
  if (!result.errors.empty()) {
    return result;
  }

  DesktopRecipeV13Context context{
      .recipe_id =
          QString::fromStdString(recipe.recipe_id.value),
      .source_id =
          QString::fromStdString(recipe.input.source.value),
      .decoder_provenance =
          QString::fromStdString(
              recipe.input.decoder_provenance),
      .raw_source =
          recipe.input.decoder ==
          photo_editor::pipeline::RawDecoder::LibRaw,
      .rendering_target = recipe.output.rendering_target,
      .device_icc_profile =
          recipe.output.device_icc_profile,
  };
  const auto rewritten =
      serialize_desktop_recipe_as_canonical_v13_with_assets(
          state,
          context,
          asset_store);
  const auto canonical_payload =
      QString::fromStdString(
          photo_editor::pipeline::
              serialize_edit_recipe_v13(recipe));
  if (!rewritten.ok() ||
      rewritten.document.recipe_payload !=
          canonical_payload) {
    result.errors.push_back(
        DesktopRecipeAdapterError{
            .code =
                DesktopRecipeAdapterErrorCode::
                    InvalidV13Recipe,
            .path = "projection",
            .message =
                "resolved desktop projection does not reproduce the canonical v13 payload",
        });
    return result;
  }

  result.state = std::move(state);
  return result;
}

CanonicalRecipeV13ProjectionResult
project_canonical_v13_document_to_desktop_state_with_assets(
    const CanonicalRecipeV13Document& document,
    const RecipeAssetStore& asset_store) {
  CanonicalRecipeV13ProjectionResult result;
  const auto decoded =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          document.recipe_payload.toStdString());
  if (!decoded.has_value()) {
    result.errors.push_back(
        DesktopRecipeAdapterError{
            .code =
                DesktopRecipeAdapterErrorCode::
                    InvalidV13Recipe,
            .path = "recipe_payload",
            .message =
                "canonical v13 payload cannot be decoded",
        });
    return result;
  }
  const auto computed_cache_key =
      QString::fromStdString(
          photo_editor::pipeline::
              edit_recipe_v13_cache_key(*decoded));
  if (computed_cache_key != document.cache_key) {
    result.errors.push_back(
        DesktopRecipeAdapterError{
            .code =
                DesktopRecipeAdapterErrorCode::
                    InvalidV13Recipe,
            .path = "cache_key",
            .message =
                "canonical v13 cache key does not match its payload",
        });
    return result;
  }
  auto projection =
      project_canonical_v13_to_desktop_state_with_assets(
      *decoded,
      asset_store);
  if (!projection.ok()) {
    return projection;
  }
  QString provenance_error;
  if (!restore_recipe_provenance(
          document.provenance_payload,
          &*projection.state,
          &provenance_error)) {
    projection.state.reset();
    projection.errors.push_back(
        DesktopRecipeAdapterError{
            .code =
                DesktopRecipeAdapterErrorCode::
                    InvalidV13Recipe,
            .path = "provenance_payload",
            .message = provenance_error.toStdString(),
        });
  }
  return projection;
}

CanonicalRecipeV13SidecarResult
write_canonical_v13_recipe_sidecar(
    const QString& sidecar_path,
    const EditRecipeState& state,
    const DesktopRecipeV13Context& context) {
  CanonicalRecipeV13SidecarResult result;
  result.sidecar_path = sidecar_path.trimmed();
  if (result.sidecar_path.isEmpty()) {
    result.error =
        QStringLiteral("Canonical v13 sidecar path is empty.");
    return result;
  }
  const RecipeAssetStore asset_store(
      result.sidecar_path +
      QStringLiteral(".assets"));
  const auto written =
      serialize_desktop_recipe_as_canonical_v13_with_assets(
          state,
          context,
          asset_store);
  if (!written.ok()) {
    result.error =
        written.errors.empty()
            ? QStringLiteral("Canonical v13 sidecar serialization failed.")
            : QString::fromStdString(
                  written.errors.front().message);
    return result;
  }
  const QFileInfo sidecar_info(result.sidecar_path);
  if (!QDir().mkpath(
          sidecar_info.dir().absolutePath())) {
    result.error =
        QStringLiteral("Could not create canonical v13 sidecar directory.");
    return result;
  }
  const QJsonObject envelope{
      {QStringLiteral("format"),
       QStringLiteral(
           "photo-editor-canonical-recipe-v13")},
      {QStringLiteral("recipePayload"),
       written.document.recipe_payload},
      {QStringLiteral("cacheKey"),
       written.document.cache_key},
      {QStringLiteral("provenancePayload"),
       written.document.provenance_payload},
  };
  QSaveFile file(result.sidecar_path);
  if (!file.open(
          QIODevice::WriteOnly |
          QIODevice::Text)) {
    result.error =
        QStringLiteral("Could not open canonical v13 sidecar for writing.");
    return result;
  }
  file.write(
      QJsonDocument(envelope)
          .toJson(QJsonDocument::Compact));
  file.write("\n");
  if (!file.commit()) {
    result.error =
        QStringLiteral("Could not atomically commit canonical v13 sidecar.");
    return result;
  }
  result.document = written.document;
  return result;
}

CanonicalRecipeV13SidecarResult
read_canonical_v13_recipe_sidecar(
    const QString& sidecar_path) {
  CanonicalRecipeV13SidecarResult result;
  result.sidecar_path = sidecar_path.trimmed();
  QFile file(result.sidecar_path);
  if (result.sidecar_path.isEmpty() ||
      !file.open(
          QIODevice::ReadOnly |
          QIODevice::Text)) {
    result.error =
        QStringLiteral("Could not open canonical v13 sidecar for reading.");
    return result;
  }
  QJsonParseError parse_error;
  const auto envelope =
      QJsonDocument::fromJson(
          file.readAll(),
          &parse_error);
  if (parse_error.error !=
          QJsonParseError::NoError ||
      !envelope.isObject() ||
      envelope.object()
              .value(QStringLiteral("format"))
              .toString() !=
          QStringLiteral(
              "photo-editor-canonical-recipe-v13")) {
    result.error =
        QStringLiteral("Sidecar is not a canonical v13 recipe envelope.");
    return result;
  }
  result.document = CanonicalRecipeV13Document{
      .recipe_payload =
          envelope.object()
              .value(
                  QStringLiteral(
                      "recipePayload"))
              .toString(),
      .cache_key =
          envelope.object()
              .value(
                  QStringLiteral(
                      "cacheKey"))
              .toString(),
      .provenance_payload =
          envelope.object()
              .value(
                  QStringLiteral(
                      "provenancePayload"))
              .toString(),
  };
  const RecipeAssetStore asset_store(
      result.sidecar_path +
      QStringLiteral(".assets"));
  const auto projected =
      project_canonical_v13_document_to_desktop_state_with_assets(
          result.document,
          asset_store);
  if (!projected.ok()) {
    result.error =
        projected.errors.empty()
            ? QStringLiteral("Canonical v13 sidecar projection failed.")
            : QString::fromStdString(
                  projected.errors.front().message);
    return result;
  }
  result.state = projected.state;
  return result;
}

QStringList canonical_v13_profile_identities(
    const CanonicalRecipeV13Document& document) {
  photo_editor::pipeline::RecipeError decode_error;
  const auto recipe =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          document.recipe_payload.toStdString(),
          &decode_error);
  if (!recipe.has_value()) {
    return {};
  }
  QStringList identities;
  const auto append = [&identities](const auto& profile) {
    if (profile.has_value()) {
      const auto identity =
          QString::fromStdString(profile->value);
      if (!identities.contains(identity)) {
        identities.append(identity);
      }
    }
  };
  append(recipe->input.camera_profile);
  append(recipe->output.device_icc_profile);
  append(recipe->proof.profile);
  return identities;
}

}  // namespace photo_editor::desktop

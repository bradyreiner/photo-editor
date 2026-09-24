#include "photo_editor/pipeline/recipe_compiler.h"
#include "photo_editor/pipeline/photo_effect_codec.h"
#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/color_space.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace photo_editor::pipeline {
namespace {

template <typename Enum>
void write_enum(std::ostream& stream, Enum value) {
  stream << static_cast<int>(
                static_cast<std::underlying_type_t<Enum>>(value))
         << ' ';
}

void write_string(std::ostream& stream, const std::string& value) {
  stream << std::quoted(value) << ' ';
}

void write_optional_identifier(
    std::ostream& stream,
    const std::optional<AssetIdentifier>& identifier) {
  stream << (identifier.has_value() ? 1 : 0) << ' ';
  if (identifier.has_value()) {
    write_string(stream, identifier->value);
  }
}

void write_look_payload(std::ostream& stream, const LookRecipeV13& look) {
  write_enum(stream, look.renderer);
  stream << look.temperature << ' ' << look.tint << ' ' << look.vibrance
         << ' ' << look.saturation << ' ' << look.hue_shift << ' '
         << look.saturation_shift << ' ' << look.colorize_hue << ' '
         << look.colorize_strength << ' ';
  write_enum(stream, look.mixer_model);
  for (const auto& channel : look.mixer_channels) {
    stream << channel.hue << ' ' << channel.saturation << ' '
           << channel.luminance << ' ' << channel.selective_cyan << ' '
           << channel.selective_magenta << ' ' << channel.selective_yellow
           << ' ' << channel.selective_black << ' ';
  }
  const auto& rgb = look.rgb_channel_mixer;
  stream << rgb.red_from_red << ' ' << rgb.red_from_green << ' '
         << rgb.red_from_blue << ' ' << rgb.green_from_red << ' '
         << rgb.green_from_green << ' ' << rgb.green_from_blue << ' '
         << rgb.blue_from_red << ' ' << rgb.blue_from_green << ' '
         << rgb.blue_from_blue << ' ' << look.black_and_white_enabled << ' ';
  for (const auto value : look.black_and_white_mixer) {
    stream << value << ' ';
  }
  stream << look.color_replacement_enabled << ' '
         << look.color_replacement_source_hue << ' '
         << look.color_replacement_target_hue << ' '
         << look.color_replacement_range << ' '
         << look.color_replacement_softness << ' '
         << look.color_replacement_strength << ' '
         << look.color_replacement_preserve_luminance << ' '
         << look.color_replacement_preserve_saturation << ' '
         << look.skin_tone_protection_enabled << ' '
         << look.skin_tone_line_hue << ' '
         << look.skin_tone_protection_range << ' '
         << look.skin_tone_protection_strength << ' '
         << look.color_harmony_enabled << ' ';
  write_enum(stream, look.color_harmony_mode);
  stream << look.color_harmony_base_hue << ' '
         << look.color_harmony_strength << ' ';
  const auto write_grade = [&](const ColorGradingZoneV13& grade) {
    stream << grade.hue << ' ' << grade.saturation << ' '
           << grade.luminance << ' ';
  };
  write_grade(look.grading_shadows);
  write_grade(look.grading_midtones);
  write_grade(look.grading_highlights);
  stream << look.grading_amount << ' ' << look.grading_balance << ' ';
  stream << look.luts.size() << ' ';
  for (const auto& lut : look.luts) {
    write_string(stream, lut.payload_asset.value);
    stream << lut.enabled << ' ' << lut.strength << ' ';
    write_string(stream, lut.name);
    for (const auto value : lut.domain_min) {
      stream << value << ' ';
    }
    for (const auto value : lut.domain_max) {
      stream << value << ' ';
    }
    write_string(stream, lut.input_color_space);
    write_string(stream, lut.output_color_space);
  }
}

bool profile_is_available(
    const RecipeCompileContext& context,
    const std::optional<AssetIdentifier>& profile) {
  if (!profile.has_value()) {
    return true;
  }
  return std::find(
             context.available_profile_assets.begin(),
             context.available_profile_assets.end(),
             *profile) != context.available_profile_assets.end();
}

bool content_is_available(
    const RecipeCompileContext& context,
    const std::optional<AssetIdentifier>& asset) {
  if (!asset.has_value()) {
    return true;
  }
  return std::find(
             context.available_content_assets.begin(),
             context.available_content_assets.end(),
             *asset) != context.available_content_assets.end();
}

std::string stable_hash(std::string_view value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char character : value) {
    hash ^= static_cast<std::uint64_t>(
        static_cast<unsigned char>(character));
    hash *= 1099511628211ULL;
  }
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::hex << std::setfill('0') << std::setw(16) << hash;
  return stream.str();
}

std::string stage_payload(
    const std::function<void(std::ostream&)>& write) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  write(stream);
  return stream.str();
}

std::string append_stage(
    RenderPlan* plan,
    RenderStageKind kind,
    std::string_view stage_version,
    std::string_view upstream_identity,
    std::string local_payload) {
  const auto identity_source =
      std::string(stage_version) + "|" + std::string(upstream_identity) +
      "|" + std::move(local_payload);
  const auto identity =
      std::string(stage_version) + "-fnv1a64-" + stable_hash(identity_source);
  plan->stages.push_back(RenderStagePlan{
      .kind = kind,
      .cache_identity = identity,
  });
  return identity;
}

void append_validation_errors(
    const RecipeValidationResult& validation,
    RecipeCompileResult* result) {
  for (const auto& error : validation.errors) {
    result->errors.push_back(RecipeCompileError{
        .code = RecipeCompileErrorCode::InvalidRecipe,
        .path = error.path,
        .message = error.message,
    });
  }
}

void append_lut_color_space_errors(
    const LookRecipeV13& look,
    std::string_view path_prefix,
    RecipeCompileResult* result) {
  for (std::size_t index = 0; index < look.luts.size(); ++index) {
    const auto& lut = look.luts[index];
    const auto lut_path =
        std::string(path_prefix) + ".luts[" + std::to_string(index) + "]";
    if (!resolve_builtin_rgb_color_space(lut.input_color_space).has_value()) {
      result->errors.push_back(RecipeCompileError{
          .code = RecipeCompileErrorCode::UnsupportedColorSpace,
          .path = lut_path + ".input_color_space",
          .message =
              "creative LUT input color space is unsupported by the canonical engine",
      });
    }
    if (!resolve_builtin_rgb_color_space(lut.output_color_space).has_value()) {
      result->errors.push_back(RecipeCompileError{
          .code = RecipeCompileErrorCode::UnsupportedColorSpace,
          .path = lut_path + ".output_color_space",
          .message =
              "creative LUT output color space is unsupported by the canonical engine",
      });
    }
  }
}

}  // namespace

RecipeCompileResult RecipeCompiler::compile(
    const EditRecipeV13& recipe,
    const RecipeCompileContext& context) const {
  RecipeCompileResult result;
  const auto validation = validate_edit_recipe_v13(recipe);
  append_validation_errors(validation, &result);
  if (context.source_width == 0 || context.source_height == 0) {
    result.errors.push_back(RecipeCompileError{
        .code = RecipeCompileErrorCode::InvalidSourceGeometry,
        .path = "source_geometry",
        .message = "source dimensions must be non-zero",
    });
  }
  if (!profile_is_available(context, recipe.input.camera_profile)) {
    result.errors.push_back(RecipeCompileError{
        .code = RecipeCompileErrorCode::ProfileUnavailable,
        .path = "input.camera_profile",
        .message = "the identified camera profile asset is unavailable",
    });
  }
  if (!profile_is_available(context, recipe.output.device_icc_profile)) {
    result.errors.push_back(RecipeCompileError{
        .code = RecipeCompileErrorCode::ProfileUnavailable,
        .path = "output.device_icc_profile",
        .message = "the identified device profile asset is unavailable",
    });
  }
  if (!profile_is_available(context, recipe.proof.profile)) {
    result.errors.push_back(RecipeCompileError{
        .code = RecipeCompileErrorCode::ProfileUnavailable,
        .path = "proof.profile",
        .message = "the identified proof profile asset is unavailable",
    });
  }
  append_lut_color_space_errors(recipe.look, "look", &result);
  for (std::size_t index = 0; index < recipe.look.luts.size(); ++index) {
    if (!content_is_available(
            context,
            std::optional<AssetIdentifier>{
                recipe.look.luts[index].payload_asset})) {
      result.errors.push_back(RecipeCompileError{
          .code = RecipeCompileErrorCode::AssetUnavailable,
          .path = "look.luts[" + std::to_string(index) + "].payload_asset",
          .message = "the identified creative LUT payload asset is unavailable",
      });
    }
  }
  for (std::size_t set_index = 0;
       set_index < recipe.masks.size();
       ++set_index) {
    const auto& mask_set = recipe.masks[set_index];
    for (std::size_t source_index = 0;
         source_index < mask_set.sources.size();
         ++source_index) {
      const auto& source = mask_set.sources[source_index];
      if (!content_is_available(context, source.payload_asset)) {
        result.errors.push_back(RecipeCompileError{
            .code = RecipeCompileErrorCode::AssetUnavailable,
            .path =
                "masks[" + std::to_string(set_index) + "].sources[" +
                std::to_string(source_index) + "].payload_asset",
            .message = "the identified mask payload asset is unavailable",
        });
      }
    }
  }
  for (std::size_t index = 0; index < recipe.layers.size(); ++index) {
    const auto& effect_layer = recipe.layers[index];
    if (effect_layer.enabled && effect_layer.opacity > 0) {
      for (std::size_t effect_index = 0; effect_index < effect_layer.effects.size(); ++effect_index) {
        const auto& effect = effect_layer.effects[effect_index];
        PhotoEffectSupport support;
        const auto admission = photo_effect_support(effect, {}, &support);
        if (admission.status == PhotoEffectStatus::Identity) continue;
        const auto algorithm = effect.type_id + ":" + std::to_string(effect.algorithm_version);
        const auto path = "layers[" + std::to_string(index) + "].effects[" + std::to_string(effect_index) + "]";
        const auto expected_scope = effect_layer.kind == LayerKind::Adjustment ?
            PhotoEffectInputScope::CompositeBelow : PhotoEffectInputScope::LayerContent;
        if (effect_layer.kind == LayerKind::Group || effect.input_scope != expected_scope) {
          result.errors.push_back({RecipeCompileErrorCode::UnsupportedEffect, path + ".input_scope",
              "photo effect input scope is incompatible with its layer"});
        }
        if (!admission.ok() || std::find(context.available_photo_effect_algorithms.begin(),
                context.available_photo_effect_algorithms.end(), algorithm) == context.available_photo_effect_algorithms.end()) {
          result.errors.push_back({RecipeCompileErrorCode::UnsupportedEffect, path,
              admission.ok() ? "photo effect is not supported by this render executor" : admission.error});
        }
        if (!effect.mask_ref.empty() && std::none_of(recipe.masks.begin(), recipe.masks.end(),
                [&](const auto& set) { return set.id.value == effect.mask_ref; })) {
          result.errors.push_back({RecipeCompileErrorCode::AssetUnavailable, path + ".mask_ref",
              "photo effect mask set is unavailable"});
        }
      }
    }
    append_lut_color_space_errors(
        recipe.layers[index].look,
        "layers[" + std::to_string(index) + "].look",
        &result);
    if (!content_is_available(
            context,
            recipe.layers[index].content_asset)) {
      result.errors.push_back(RecipeCompileError{
          .code = RecipeCompileErrorCode::AssetUnavailable,
          .path =
              "layers[" + std::to_string(index) + "].content_asset",
          .message = "the identified layer content asset is unavailable",
      });
    }
    for (std::size_t lut_index = 0;
         lut_index < recipe.layers[index].look.luts.size();
         ++lut_index) {
      if (!content_is_available(
              context,
              std::optional<AssetIdentifier>{
                  recipe.layers[index].look.luts[lut_index].payload_asset})) {
        result.errors.push_back(RecipeCompileError{
            .code = RecipeCompileErrorCode::AssetUnavailable,
            .path =
                "layers[" + std::to_string(index) + "].look.luts[" +
                std::to_string(lut_index) + "].payload_asset",
            .message =
                "the identified layer creative LUT payload asset is unavailable",
        });
      }
    }
  }
  if (!result.errors.empty()) {
    return result;
  }
  if (context.purpose == RenderPurpose::Scope &&
      context.scope_domain == RenderScopeDomain::Proof &&
      recipe.proof.mode == ProofMode::Disabled) {
    result.errors.push_back(RecipeCompileError{
        .code = RecipeCompileErrorCode::InvalidRecipe,
        .path = "scope.domain",
        .message =
            "proof scope requires an enabled proof recipe",
    });
    return result;
  }

  RenderPlan plan{
      .recipe_cache_key = edit_recipe_v13_cache_key(recipe),
      .source = recipe.input.source,
      .decoder = recipe.input.decoder,
      .camera_profile_mode = recipe.input.camera_profile_mode,
      .camera_profile = recipe.input.camera_profile,
      .renderer = recipe.look.renderer,
      .renderer_transition_look =
          recipe.output.renderer_transition_look,
      .rendering_target = recipe.output.rendering_target,
      .device_icc_profile = recipe.output.device_icc_profile,
      .proof_mode = recipe.proof.mode,
      .proof_profile = recipe.proof.profile,
      .backend_requirement = context.backend_requirement,
      .purpose = context.purpose,
      .scope_domain = context.scope_domain,
      .source_width = context.source_width,
      .source_height = context.source_height,
  };
  auto upstream_identity = append_stage(
      &plan,
      RenderStageKind::Decode,
      "decode-v2",
      {},
      stage_payload([&](std::ostream& stream) {
        write_string(stream, recipe.input.source.value);
        write_enum(stream, recipe.input.decoder);
        write_string(stream, recipe.input.decoder_provenance);
        stream << context.source_width << ' ' << context.source_height << ' ';
        write_enum(stream, recipe.input.demosaic_quality);
        stream << recipe.input.hot_dead_pixel_cleanup << ' '
               << recipe.input.false_color_suppression << ' '
               << recipe.input.highlight_recovery_enabled << ' ';
      }));
  if (recipe.input.decoder == RawDecoder::LibRaw) {
    upstream_identity = append_stage(
        &plan,
        RenderStageKind::NormalizeRaw,
        "normalize-raw-v3",
        upstream_identity,
        stage_payload([&](std::ostream& stream) {
          const auto write_optional_double =
              [&](const std::optional<double>& value) {
                stream << (value.has_value() ? 1 : 0) << ' ';
                if (value.has_value()) {
                  stream << *value << ' ';
                }
              };
          write_optional_double(
              recipe.input.white_balance_temperature_kelvin);
          write_optional_double(recipe.input.white_balance_tint);
          write_optional_double(recipe.input.black_level_override);
          write_optional_double(recipe.input.white_level_override);
          stream << recipe.input.input_transform_enabled << ' ';
          write_enum(stream, recipe.input.highlight_reconstruction);
        }));
  }
  upstream_identity = append_stage(
      &plan,
      RenderStageKind::CameraCharacterization,
      "camera-characterization-v5",
      upstream_identity,
      stage_payload([&](std::ostream& stream) {
        const auto write_optional_double =
            [&](const std::optional<double>& value) {
              stream << (value.has_value() ? 1 : 0) << ' ';
              if (value.has_value()) {
                stream << *value << ' ';
              }
            };
        write_enum(stream, recipe.input.camera_profile_mode);
        write_optional_identifier(stream, recipe.input.camera_profile);
        write_optional_double(
            recipe.input.white_balance_temperature_kelvin);
        write_optional_double(recipe.input.white_balance_tint);
        // These global controls live in the serialized Look recipe, but the
        // executor applies their white-balance matrix during characterization.
        // Reusing the old characterized surface drops the edit from previews.
        stream << recipe.look.temperature << ' ' << recipe.look.tint << ' ';
        stream << recipe.input.input_transform_enabled << ' ';
        write_enum(stream, recipe.input.chromatic_adaptation);
        write_enum(stream, recipe.input.camera_matching_look);
        stream << recipe.input.calibration_hue << ' '
               << recipe.input.calibration_saturation << ' '
               << recipe.input.red_primary_hue << ' '
               << recipe.input.red_primary_saturation << ' '
               << recipe.input.green_primary_hue << ' '
               << recipe.input.green_primary_saturation << ' '
               << recipe.input.blue_primary_hue << ' '
               << recipe.input.blue_primary_saturation << ' '
               << recipe.geometry.lens_vignette << ' ';
      }));
  upstream_identity = append_stage(
      &plan,
      RenderStageKind::SceneAdjustments,
      "scene-v3",
      upstream_identity,
      stage_payload([&](std::ostream& stream) {
        stream << recipe.scene.exposure_ev << ' ';
      }));
  upstream_identity = append_stage(
      &plan,
      RenderStageKind::Tone,
      "tone-v3",
      upstream_identity,
      stage_payload([&](std::ostream& stream) {
        stream << recipe.tone.contrast << ' ' << recipe.tone.highlights << ' '
               << recipe.tone.shadows << ' ' << recipe.tone.whites << ' '
               << recipe.tone.blacks << ' ' << recipe.tone.brightness << ' '
               << recipe.tone.hdr_local_tone_mapping << ' '
               << recipe.tone.filmic_shoulder << ' '
               << recipe.tone.filmic_toe << ' '
               << recipe.tone.adaptive_contrast << ' '
               << recipe.tone.levels_black_point << ' '
               << recipe.tone.levels_gamma << ' '
               << recipe.tone.levels_white_point << ' '
               << recipe.tone.zone_deep_shadows << ' '
               << recipe.tone.zone_shadows << ' '
               << recipe.tone.zone_midtones << ' '
               << recipe.tone.zone_lights << ' '
               << recipe.tone.zone_highlights << ' ';
      }));
  if (!recipe.curves.master.empty() || !recipe.curves.luma.empty() ||
      !recipe.curves.red.empty() || !recipe.curves.green.empty() ||
      !recipe.curves.blue.empty()) {
    upstream_identity = append_stage(
        &plan,
        RenderStageKind::Curves,
        "curves-v4-extended-range",
        upstream_identity,
        stage_payload([&](std::ostream& stream) {
          write_enum(stream, recipe.curves.interpolation);
          const auto write_curve =
              [&](const std::vector<CurvePointV13>& points) {
                stream << points.size() << ' ';
                for (const auto& point : points) {
                  stream << point.x << ' ' << point.y << ' ';
                }
              };
          write_curve(recipe.curves.master);
          write_curve(recipe.curves.luma);
          write_curve(recipe.curves.red);
          write_curve(recipe.curves.green);
          write_curve(recipe.curves.blue);
        }));
  }
  upstream_identity = append_stage(
      &plan,
      RenderStageKind::Look,
      "look-v3",
      upstream_identity,
      stage_payload([&](std::ostream& stream) {
        write_look_payload(stream, recipe.look);
      }));
  if (!recipe.layers.empty()) {
    upstream_identity = append_stage(
        &plan,
        RenderStageKind::Layers,
        "layers-v3-extended-curves",
        upstream_identity,
        stage_payload([&](std::ostream& stream) {
          stream << recipe.masks.size() << ' ';
          for (const auto& mask_set : recipe.masks) {
            write_string(stream, mask_set.id.value);
            stream << (mask_set.enabled ? 1 : 0) << ' '
                   << mask_set.sources.size() << ' ';
            for (const auto& source : mask_set.sources) {
              write_string(stream, source.id.value);
              write_enum(stream, source.kind);
              write_enum(stream, source.operation);
              write_optional_identifier(stream, source.parent);
              stream << (source.enabled ? 1 : 0) << ' ';
              write_enum(stream, source.blend_mode);
              stream << source.opacity
                     << ' ' << source.density << ' '
                     << (source.inverted ? 1 : 0) << ' ';
              write_optional_identifier(stream, source.output);
              stream << (source.reusable_output ? 1 : 0) << ' ';
              write_enum(stream, source.coordinate_space);
              write_enum(stream, source.sampling);
              write_optional_identifier(stream, source.source_revision);
              write_optional_identifier(stream, source.payload_asset);
              stream << source.geometry.feather << ' '
                     << source.geometry.position << ' '
                     << source.geometry.center_x << ' '
                     << source.geometry.center_y << ' '
                     << source.geometry.radius_x << ' '
                     << source.geometry.radius_y << ' '
                     << source.geometry.rotation_degrees << ' '
                     << source.geometry.range_min << ' '
                     << source.geometry.range_max << ' ';
            }
          }
          stream << recipe.layers.size() << ' ';
          for (const auto& layer : recipe.layers) {
            write_string(stream, layer.id.value);
            write_enum(stream, layer.kind);
            stream << (layer.enabled ? 1 : 0) << ' ' << layer.opacity << ' ';
            write_enum(stream, layer.blend_mode);
            write_optional_identifier(stream, layer.mask);
            write_optional_identifier(stream, layer.parent);
            write_optional_identifier(stream, layer.content_asset);
            if (!layer.effects.empty()) {
              // Keep existing empty-stack stage identities byte-stable.
              write_string(stream, "photo-effects:v1");
              write_string(stream, *serialize_photo_effect_stack(layer.effects));
            }
            stream << layer.scene.exposure_ev << ' '
                   << layer.tone.contrast << ' ' << layer.tone.highlights << ' '
                   << layer.tone.shadows << ' ' << layer.tone.whites << ' '
                   << layer.tone.blacks << ' ' << layer.tone.brightness << ' '
                   << layer.tone.hdr_local_tone_mapping << ' '
                   << layer.tone.filmic_shoulder << ' '
                   << layer.tone.filmic_toe << ' '
                   << layer.tone.adaptive_contrast << ' '
                   << layer.tone.levels_black_point << ' '
                   << layer.tone.levels_gamma << ' '
                   << layer.tone.levels_white_point << ' '
                   << layer.tone.zone_deep_shadows << ' '
                   << layer.tone.zone_shadows << ' '
                   << layer.tone.zone_midtones << ' '
                   << layer.tone.zone_lights << ' '
                   << layer.tone.zone_highlights << ' ';
            write_enum(stream, layer.curves.interpolation);
            const auto write_curve =
                [&](const std::vector<CurvePointV13>& points) {
                  stream << points.size() << ' ';
                  for (const auto& point : points) {
                    stream << point.x << ' ' << point.y << ' ';
                  }
                };
            write_curve(layer.curves.master);
            write_curve(layer.curves.luma);
            write_curve(layer.curves.red);
            write_curve(layer.curves.green);
            write_curve(layer.curves.blue);
            write_look_payload(stream, layer.look);
            stream << layer.detail.texture << ' ' << layer.detail.clarity
                   << ' ' << layer.detail.dehaze << ' '
                   << layer.detail.sharpening << ' '
                   << layer.detail.sharpen_radius << ' '
                   << layer.detail.sharpen_detail << ' '
                   << layer.detail.sharpen_masking << ' '
                   << layer.detail.luminance_noise_reduction << ' '
                   << layer.detail.chroma_noise_reduction << ' ';
            stream << layer.geometry.crop_x << ' '
                   << layer.geometry.crop_y << ' '
                   << layer.geometry.crop_width << ' '
                   << layer.geometry.crop_height << ' '
                   << layer.geometry.rotation_degrees << ' '
                   << layer.geometry.lens_distortion << ' '
                   << layer.geometry.lens_vignette << ' '
                   << layer.geometry.chromatic_aberration << ' '
                   << layer.geometry.perspective_vertical << ' '
                   << layer.geometry.perspective_horizontal << ' '
                   << layer.geometry.scale << ' '
                   << layer.geometry.skew_x << ' '
                   << layer.geometry.skew_y << ' '
                   << layer.geometry.crop_zoom << ' ';
            write_enum(stream, layer.geometry.upright_mode);
            stream << layer.geometry.liquify_center_x << ' '
                   << layer.geometry.liquify_center_y << ' '
                   << layer.geometry.liquify_radius << ' '
                   << layer.geometry.liquify_strength << ' '
                   << layer.geometry.content_aware_crop_fill << ' ';
          }
        }));
  }
  upstream_identity = append_stage(
      &plan,
      RenderStageKind::Detail,
      "detail-v2",
      upstream_identity,
      stage_payload([&](std::ostream& stream) {
        stream << recipe.detail.texture << ' ' << recipe.detail.clarity << ' '
               << recipe.detail.dehaze << ' ' << recipe.detail.sharpening << ' '
               << recipe.detail.sharpen_radius << ' '
               << recipe.detail.sharpen_detail << ' '
               << recipe.detail.sharpen_masking << ' '
               << recipe.detail.luminance_noise_reduction << ' '
               << recipe.detail.chroma_noise_reduction << ' ';
      }));
  upstream_identity = append_stage(
      &plan,
      RenderStageKind::Geometry,
      "geometry-v3",
      upstream_identity,
      stage_payload([&](std::ostream& stream) {
        stream << recipe.geometry.crop_x << ' ' << recipe.geometry.crop_y << ' '
               << recipe.geometry.crop_width << ' '
               << recipe.geometry.crop_height << ' '
               << recipe.geometry.rotation_degrees << ' '
               << recipe.geometry.lens_distortion << ' '
               << recipe.geometry.chromatic_aberration << ' '
               << recipe.geometry.perspective_vertical << ' '
               << recipe.geometry.perspective_horizontal << ' '
               << recipe.geometry.scale << ' ' << recipe.geometry.skew_x << ' '
               << recipe.geometry.skew_y << ' ' << recipe.geometry.crop_zoom
               << ' ';
        write_enum(stream, recipe.geometry.upright_mode);
        stream << recipe.geometry.liquify_center_x << ' '
               << recipe.geometry.liquify_center_y << ' '
               << recipe.geometry.liquify_radius << ' '
               << recipe.geometry.liquify_strength << ' '
               << recipe.geometry.content_aware_crop_fill << ' ';
      }));
  upstream_identity = append_stage(
      &plan,
      RenderStageKind::PhotographicOutput,
      "photographic-output-aces2-v4",
      upstream_identity,
      stage_payload([&](std::ostream& stream) {
        write_enum(stream, recipe.look.renderer);
        write_enum(stream, recipe.output.renderer_transition_look);
        write_enum(stream, recipe.output.rendering_target);
        stream << recipe.output.peak_luminance_nits << ' '
               << recipe.output.black_luminance_nits << ' ';
        write_enum(stream, recipe.output.render_intent);
        stream << recipe.output.gamut_mapping_enabled << ' ';
        write_enum(stream, recipe.output.gamut_mapping_policy);
        stream << recipe.output.black_point_compensation << ' '
               << recipe.output.source_black_point << ' '
               << recipe.output.output_black_point << ' ';
        if (recipe.output.export_color_space != ExportColorSpaceV13::Srgb) {
          write_string(stream, "export-color-space-v1");
          write_enum(stream, recipe.output.export_color_space);
        }
      }));
  if (recipe.proof.mode != ProofMode::Disabled) {
    upstream_identity = append_stage(
        &plan,
        RenderStageKind::Proof,
        "proof-v2",
        upstream_identity,
        stage_payload([&](std::ostream& stream) {
          write_enum(stream, recipe.proof.mode);
          write_optional_identifier(stream, recipe.proof.profile);
          stream << (recipe.proof.simulate_paper ? 1 : 0) << ' '
                 << (recipe.proof.simulate_ink_black ? 1 : 0) << ' '
                 << (recipe.proof.black_point_compensation ? 1 : 0) << ' ';
        }));
  }
  if (recipe.output.device_icc_profile.has_value()) {
    upstream_identity = append_stage(
        &plan,
        RenderStageKind::DeviceTransform,
        "device-transform-v2",
        upstream_identity,
        stage_payload([&](std::ostream& stream) {
          write_optional_identifier(
              stream,
              recipe.output.device_icc_profile);
        }));
  }
  append_stage(
      &plan,
      RenderStageKind::Publish,
      "publish-v2",
      upstream_identity,
      stage_payload([&](std::ostream& stream) {
        write_enum(stream, context.purpose);
        write_enum(stream, context.backend_requirement);
      }));

  if (context.purpose == RenderPurpose::Scope) {
    const auto stop_kind =
        render_scope_domain_stop_stage(
            context.scope_domain);
    const auto stop = std::find_if(
        plan.stages.begin(),
        plan.stages.end(),
        [stop_kind](const auto& stage) {
          return stage.kind == stop_kind;
        });
    if (stop == plan.stages.end()) {
      result.errors.push_back(RecipeCompileError{
          .code = RecipeCompileErrorCode::InvalidRecipe,
          .path = "scope.domain",
          .message =
              "scope domain has no executable stopping stage",
      });
      return result;
    }
    plan.stages.erase(
        std::next(stop),
        plan.stages.end());
  }

  const auto serialized_without_key = serialize_render_plan(plan);
  plan.plan_cache_key =
      "render-plan-v2-fnv1a64-" +
      stable_hash(serialized_without_key);
  result.plan = std::move(plan);
  return result;
}

std::string serialize_render_plan(const RenderPlan& plan) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  write_string(stream, "photo-editor-render-plan-v2");
  stream << plan.plan_version << ' ';
  write_string(stream, plan.recipe_cache_key);
  write_string(stream, plan.source.value);
  write_enum(stream, plan.decoder);
  write_enum(stream, plan.camera_profile_mode);
  write_optional_identifier(stream, plan.camera_profile);
  write_enum(stream, plan.renderer);
  write_enum(stream, plan.rendering_target);
  write_optional_identifier(stream, plan.device_icc_profile);
  write_enum(stream, plan.proof_mode);
  write_optional_identifier(stream, plan.proof_profile);
  write_enum(stream, plan.backend_requirement);
  write_enum(stream, plan.purpose);
  write_enum(stream, plan.scope_domain);
  stream << plan.source_width << ' ' << plan.source_height << ' ';
  stream << plan.stages.size() << ' ';
  for (const auto& stage : plan.stages) {
    write_enum(stream, stage.kind);
    write_string(stream, stage.cache_identity);
  }
  write_string(stream, plan.plan_cache_key);
  return stream.str();
}

std::string_view render_scope_domain_name(
    RenderScopeDomain domain) noexcept {
  switch (domain) {
    case RenderScopeDomain::SceneLinear:
      return "scene-linear";
    case RenderScopeDomain::Working:
      return "working";
    case RenderScopeDomain::PreOutput:
      return "pre-output";
    case RenderScopeDomain::Proof:
      return "proof";
    case RenderScopeDomain::FinalDisplay:
      return "final-display";
  }
  return "final-display";
}

RenderStageKind render_scope_domain_stop_stage(
    RenderScopeDomain domain) noexcept {
  switch (domain) {
    case RenderScopeDomain::SceneLinear:
      return RenderStageKind::CameraCharacterization;
    case RenderScopeDomain::Working:
      return RenderStageKind::Geometry;
    case RenderScopeDomain::PreOutput:
      return RenderStageKind::PhotographicOutput;
    case RenderScopeDomain::Proof:
      return RenderStageKind::Proof;
    case RenderScopeDomain::FinalDisplay:
      return RenderStageKind::Publish;
  }
  return RenderStageKind::Publish;
}

}  // namespace photo_editor::pipeline

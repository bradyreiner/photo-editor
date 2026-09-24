#include "photo_editor/pipeline/edit_recipe_v13.h"
#include "photo_editor/pipeline/recipe_compiler.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <set>
#include <string>
#include <string_view>

namespace {

using photo_editor::pipeline::AssetIdentifier;
using photo_editor::pipeline::CameraProfileMode;
using photo_editor::pipeline::DisplayRenderingTarget;
using photo_editor::pipeline::EditRecipeV13;
using photo_editor::pipeline::MaskSetRecipeV13;
using photo_editor::pipeline::MaskSourceRecipeV13;
using photo_editor::pipeline::ProofMode;
using photo_editor::pipeline::RecipeCompileContext;
using photo_editor::pipeline::RecipeCompileErrorCode;
using photo_editor::pipeline::RecipeCompiler;
using photo_editor::pipeline::RecipeError;
using photo_editor::pipeline::RecipeErrorCode;
using photo_editor::pipeline::RenderStageKind;
using photo_editor::pipeline::RenderPurpose;
using photo_editor::pipeline::RenderScopeDomain;

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

EditRecipeV13 representative_recipe() {
  EditRecipeV13 recipe;
  recipe.recipe_id = AssetIdentifier{"recipe-001"};
  recipe.input.source = AssetIdentifier{"source-raw-001"};
  recipe.input.decoder_provenance = "libraw:0.21.5:camera-linear";
  recipe.input.camera_profile_mode = CameraProfileMode::Dcp;
  recipe.input.camera_profile = AssetIdentifier{"profile-camera-dcp"};
  recipe.input.white_balance_temperature_kelvin = 5275.0;
  recipe.input.white_balance_tint = 0.07;
  recipe.input.demosaic_quality =
      photo_editor::pipeline::RawDemosaicQualityV13::Full;
  recipe.input.highlight_reconstruction =
      photo_editor::pipeline::HighlightReconstructionModeV13::CameraDefault;
  recipe.input.chromatic_adaptation =
      photo_editor::pipeline::ChromaticAdaptationMethodV13::Cat02;
  recipe.input.camera_matching_look =
      photo_editor::pipeline::CameraMatchingLookV13::Vivid;
  recipe.input.hot_dead_pixel_cleanup = 0.08;
  recipe.input.false_color_suppression = 0.1;
  recipe.input.highlight_recovery_enabled = false;
  recipe.input.calibration_hue = 0.08;
  recipe.input.blue_primary_saturation = -0.07;
  recipe.scene.exposure_ev = 0.65;
  recipe.tone.contrast = 0.18;
  recipe.tone.highlights = -0.22;
  recipe.tone.shadows = 0.16;
  recipe.tone.brightness = 0.08;
  recipe.tone.hdr_local_tone_mapping = 0.12;
  recipe.tone.filmic_shoulder = -0.07;
  recipe.tone.filmic_toe = 0.05;
  recipe.tone.adaptive_contrast = 0.09;
  recipe.tone.levels_black_point = 0.02;
  recipe.tone.levels_gamma = -0.03;
  recipe.tone.levels_white_point = 0.04;
  recipe.tone.zone_deep_shadows = 0.11;
  recipe.tone.zone_shadows = 0.07;
  recipe.tone.zone_midtones = -0.05;
  recipe.tone.zone_lights = 0.06;
  recipe.tone.zone_highlights = -0.08;
  recipe.detail.dehaze = -0.14;
  recipe.detail.sharpen_radius = 1.6;
  recipe.detail.sharpen_detail = 0.35;
  recipe.detail.sharpen_masking = 0.24;
  recipe.geometry.lens_distortion = -0.12;
  recipe.geometry.lens_vignette = 0.08;
  recipe.geometry.chromatic_aberration = 0.04;
  recipe.geometry.perspective_vertical = -0.07;
  recipe.geometry.perspective_horizontal = 0.05;
  recipe.geometry.scale = 1.15;
  recipe.geometry.skew_x = 0.06;
  recipe.geometry.skew_y = -0.03;
  recipe.geometry.crop_zoom = 1.08;
  recipe.geometry.upright_mode =
      photo_editor::pipeline::UprightMode::Vertical;
  recipe.geometry.liquify_center_x = 0.45;
  recipe.geometry.liquify_center_y = 0.55;
  recipe.geometry.liquify_radius = 0.3;
  recipe.geometry.liquify_strength = 0.14;
  recipe.geometry.content_aware_crop_fill = true;
  recipe.output.render_intent =
      photo_editor::pipeline::OutputRenderIntent::RelativeColorimetric;
  recipe.output.gamut_mapping_enabled = false;
  recipe.output.gamut_mapping_policy =
      photo_editor::pipeline::OutputGamutMappingPolicy::PreserveSaturation;
  recipe.output.black_point_compensation = false;
  recipe.output.source_black_point = 0.02;
  recipe.output.output_black_point = 0.04;
  recipe.look.mixer_model =
      photo_editor::pipeline::ColorMixerModelV13::Hsv;
  recipe.look.mixer_channels[2].hue = 0.14;
  recipe.look.mixer_channels[2].selective_yellow = -0.09;
  recipe.look.rgb_channel_mixer.blue_from_red = 0.18;
  recipe.look.black_and_white_enabled = true;
  recipe.look.black_and_white_mixer[3] = 0.24;
  recipe.look.color_replacement_enabled = true;
  recipe.look.color_replacement_source_hue = 0.18;
  recipe.look.color_replacement_strength = 0.4;
  recipe.look.skin_tone_protection_enabled = true;
  recipe.look.skin_tone_protection_strength = 0.3;
  recipe.look.color_harmony_enabled = true;
  recipe.look.color_harmony_mode =
      photo_editor::pipeline::ColorHarmonyModeV13::Tetradic;
  recipe.look.color_harmony_strength = 0.22;
  recipe.look.grading_shadows = {
      .hue = 0.72,
      .saturation = 0.2,
      .luminance = -0.08,
  };
  recipe.look.grading_amount = 0.84;
  recipe.look.grading_balance = -0.12;
  recipe.look.luts.push_back(
      photo_editor::pipeline::CreativeLutRecipeV13{
          .payload_asset = AssetIdentifier{"creative-lut-payload"},
          .enabled = true,
          .strength = 0.72,
          .name = "Editorial look",
          .domain_min = {-0.1, -0.1, -0.1},
          .domain_max = {1.2, 1.2, 1.2},
          .input_color_space = "ACEScg",
          .output_color_space = "ACEScg",
      });
  recipe.curves.master = {{0.0, 0.0}, {0.4, 0.36}, {1.0, 1.0}};
  recipe.curves.red = {{0.0, 0.0}, {0.5, 0.56}, {1.0, 1.0}};
  recipe.look.vibrance = 0.12;
  recipe.look.saturation = -0.03;
  MaskSetRecipeV13 mask_set{
      .id = AssetIdentifier{"mask-set-sky"},
      .enabled = true,
  };
  mask_set.sources.push_back(MaskSourceRecipeV13{
      .id = AssetIdentifier{"mask-sky"},
      .kind = photo_editor::pipeline::MaskSourceKind::LinearGradient,
      .operation = photo_editor::pipeline::MaskCombineOperation::Add,
      .enabled = true,
      .opacity = 0.9,
      .density = 0.8,
      .inverted = false,
      .geometry =
          {
              .feather = 0.25,
              .position = 0.62,
              .rotation_degrees = 12.0,
          },
  });
  recipe.masks.push_back(mask_set);
  photo_editor::pipeline::LayerRecipeV13 layer;
  layer.id = AssetIdentifier{"layer-sky"};
  layer.opacity = 0.72;
  layer.mask = AssetIdentifier{"mask-set-sky"};
  layer.scene.exposure_ev = -0.2;
  layer.curves.master = {{0.0, 0.0}, {0.5, 0.48}, {1.0, 1.0}};
  layer.look.saturation = 0.08;
  layer.detail.texture = 0.12;
  recipe.layers.push_back(layer);
  recipe.detail.texture = 0.1;
  recipe.detail.sharpening = 0.35;
  recipe.geometry.crop_x = 0.05;
  recipe.geometry.crop_y = 0.1;
  recipe.geometry.crop_width = 0.9;
  recipe.geometry.crop_height = 0.8;
  recipe.output.rendering_target = DisplayRenderingTarget::DisplayP3Simulation;
  recipe.output.device_icc_profile = AssetIdentifier{"profile-display-icc"};
  recipe.proof.mode = ProofMode::Icc;
  recipe.proof.profile = AssetIdentifier{"profile-proof-icc"};
  recipe.proof.simulate_paper = true;
  return recipe;
}

RecipeCompileContext representative_context() {
  return RecipeCompileContext{
      .source_width = 8256,
      .source_height = 5504,
      .available_profile_assets = {
          AssetIdentifier{"profile-camera-dcp"},
          AssetIdentifier{"profile-display-icc"},
          AssetIdentifier{"profile-proof-icc"},
      },
      .available_content_assets = {
          AssetIdentifier{"creative-lut-payload"},
      },
  };
}

bool has_stage(
    const photo_editor::pipeline::RenderPlan& plan,
    RenderStageKind kind) {
  return std::any_of(
      plan.stages.begin(),
      plan.stages.end(),
      [kind](const auto& stage) {
        return stage.kind == kind;
      });
}

std::string stage_identity(
    const photo_editor::pipeline::RenderPlan& plan,
    RenderStageKind kind) {
  const auto stage = std::find_if(
      plan.stages.begin(),
      plan.stages.end(),
      [kind](const auto& candidate) {
        return candidate.kind == kind;
      });
  return stage == plan.stages.end() ? std::string{} : stage->cache_identity;
}

}  // namespace

int main() {
  const auto recipe = representative_recipe();
  const auto validation =
      photo_editor::pipeline::validate_edit_recipe_v13(recipe);
  if (!validation.ok()) {
    return fail("representative v13 recipe should validate");
  }

  const auto serialized =
      photo_editor::pipeline::serialize_edit_recipe_v13(recipe);
  if (serialized !=
      photo_editor::pipeline::serialize_edit_recipe_v13(recipe)) {
    return fail("v13 recipe serialization should be deterministic");
  }
  {
    using namespace photo_editor::pipeline;
    auto with_effect = recipe;
    PhotoEffectInstance blur;
    blur.id = "blur-1";
    blur.input_scope = PhotoEffectInputScope::CompositeBelow;
    blur.mask_ref = "mask-set-sky";
    with_effect.layers[0].effects.push_back(blur);
    const auto effect_bytes = serialize_edit_recipe_v13(with_effect);
    const auto effect_read = deserialize_edit_recipe_v13(effect_bytes);
    if (effect_bytes.find("photo-editor-edit-recipe-v13.4") == std::string::npos ||
        !effect_read || *effect_read != with_effect)
      return fail("effects must round-trip in the explicitly versioned v13.4 envelope");
    auto effect_context = representative_context();
    if (RecipeCompiler{}.compile(with_effect, effect_context).ok())
      return fail("an executor without effect admission must not silently omit active effects");
    effect_context.available_photo_effect_algorithms = {"gaussian_blur:1"};
    const auto first = RecipeCompiler{}.compile(with_effect, effect_context);
    if (!first.ok()) return fail("known effect with an admitted executor should compile");
    with_effect.layers[0].effects[0].gaussian.sigma += 1;
    const auto second = RecipeCompiler{}.compile(with_effect, effect_context);
    if (!second.ok() || first.plan->recipe_cache_key == second.plan->recipe_cache_key ||
        stage_identity(*first.plan, RenderStageKind::Layers) == stage_identity(*second.plan, RenderStageKind::Layers) ||
        stage_identity(*first.plan, RenderStageKind::Decode) != stage_identity(*second.plan, RenderStageKind::Decode))
      return fail("effect changes must invalidate layers/downstream while retaining source decode identity");
    with_effect.layers[0].effects[0].mask_ref = "absent-mask";
    if (RecipeCompiler{}.compile(with_effect, effect_context).ok()) return fail("unresolved effect mask admitted");
    with_effect.layers[0].effects[0].enabled = false;
    if (!RecipeCompiler{}.compile(with_effect, representative_context()).ok())
      return fail("disabled effect must not require an executor or mask");
    with_effect.layers[0].effects[0].type_id = "future-algorithm";
    with_effect.layers[0].effects[0].opaque_parameters = "future-parameter-payload";
    const auto unknown_read = deserialize_edit_recipe_v13(serialize_edit_recipe_v13(with_effect));
    if (!unknown_read || *unknown_read != with_effect)
      return fail("canonical recipe lost opaque future effect parameters");
    with_effect.layers[0].effects[0].enabled = true;
    effect_context.available_photo_effect_algorithms.push_back("future-algorithm:1");
    if (RecipeCompiler{}.compile(with_effect, effect_context).ok())
      return fail("a declared capability cannot make an unimplemented operator renderable");
    with_effect.layers[0].effects.clear();
    if (serialize_edit_recipe_v13(with_effect) != serialized)
      return fail("removing the final effect must recover original document bytes");
    with_effect.layers[0].effects.push_back(blur);
    with_effect.output.export_color_space = ExportColorSpaceV13::DisplayP3;
    const auto wide_read = deserialize_edit_recipe_v13(serialize_edit_recipe_v13(with_effect));
    if (!wide_read || *wide_read != with_effect) return fail("v13.4 lost output color space");
  }
  RecipeError decode_error;
  const auto decoded =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          serialized,
          &decode_error);
  if (!decoded.has_value()) {
    return fail(
        "v13 recipe should decode: " + decode_error.path + ": " +
        decode_error.message);
  }
  if (*decoded != recipe) {
    return fail("v13 recipe should round-trip byte-exact scalar values");
  }
  if (photo_editor::pipeline::serialize_edit_recipe_v13(*decoded) !=
      serialized) {
    return fail("decoded v13 recipe should reproduce canonical bytes");
  }
  auto previous_serialized = serialized;
  for (std::uint8_t value = 0; value <= 5; ++value) {
    auto delivery = recipe;
    delivery.output.export_color_space = static_cast<photo_editor::pipeline::ExportColorSpaceV13>(value);
    const auto delivery_bytes = photo_editor::pipeline::serialize_edit_recipe_v13(delivery);
    const auto restored_delivery = photo_editor::pipeline::deserialize_edit_recipe_v13(delivery_bytes, &decode_error);
    if (!restored_delivery || *restored_delivery != delivery ||
        (value == 0 && delivery_bytes != serialized) ||
        (value != 0 && photo_editor::pipeline::edit_recipe_v13_cache_key(delivery) ==
            photo_editor::pipeline::edit_recipe_v13_cache_key(recipe))) {
      return fail("delivery space must round-trip, invalidate non-sRGB cache keys and preserve old sRGB bytes");
    }
    if (value != 0) {
      auto malformed = delivery_bytes;
      malformed.replace(malformed.find_last_not_of(' '), 1, "9");
      if (photo_editor::pipeline::deserialize_edit_recipe_v13(malformed, &decode_error))
        return fail("unsupported serialized delivery space must be rejected");
    }
  }
  const auto current_magic = std::string{"photo-editor-edit-recipe-v13.2"};
  const auto previous_magic = std::string{"photo-editor-edit-recipe-v13.1"};
  const auto current_magic_offset = previous_serialized.find(current_magic);
  const auto transition_marker =
      std::string{" 2 0 1 \"profile-display-icc\" "};
  const auto transition_offset = previous_serialized.find(transition_marker);
  if (current_magic_offset == std::string::npos ||
      transition_offset == std::string::npos) {
    return fail("test fixture should expose the canonical v13.2 transition field");
  }
  previous_serialized.replace(
      transition_offset,
      transition_marker.size(),
      " 2 1 \"profile-display-icc\" ");
  previous_serialized.replace(
      current_magic_offset,
      current_magic.size(),
      previous_magic);
  const auto previous_decoded =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          previous_serialized,
          &decode_error);
  if (!previous_decoded.has_value() || *previous_decoded != recipe) {
    return fail("canonical v13.1 serialization should remain readable with no transition look");
  }
  auto legacy_stream = std::istringstream{previous_serialized};
  std::string serialized_magic;
  std::string serialized_recipe_id;
  std::string serialized_source_id;
  std::string serialized_decoder_provenance;
  std::string serialized_camera_profile;
  int serialized_recipe_version = 0;
  int serialized_decoder = 0;
  int serialized_camera_profile_mode = 0;
  int serialized_camera_profile_present = 0;
  int serialized_white_balance_present = 0;
  int serialized_tint_present = 0;
  int serialized_black_level_present = 0;
  int serialized_white_level_present = 0;
  int serialized_input_transform_enabled = 0;
  int serialized_highlight_mode = 0;
  int serialized_adaptation = 0;
  int serialized_demosaic = 0;
  double serialized_white_balance = 0.0;
  double serialized_tint = 0.0;
  if (!(legacy_stream >> std::quoted(serialized_magic) >>
        serialized_recipe_version >> std::quoted(serialized_recipe_id) >>
        std::quoted(serialized_source_id) >> serialized_decoder >>
        std::quoted(serialized_decoder_provenance) >>
        serialized_camera_profile_mode >>
        serialized_camera_profile_present >>
        std::quoted(serialized_camera_profile) >>
        serialized_white_balance_present >> serialized_white_balance >>
        serialized_tint_present >> serialized_tint >>
        serialized_black_level_present >> serialized_white_level_present >>
        serialized_input_transform_enabled >> serialized_highlight_mode >>
        serialized_adaptation >> serialized_demosaic)) {
    return fail("test fixture should expose the canonical v13.1 input prefix");
  }
  const auto before_decode_controls = legacy_stream.tellg();
  double serialized_hot_dead_cleanup = 0.0;
  double serialized_false_color = 0.0;
  int serialized_highlight_recovery = 0;
  if (!(legacy_stream >> serialized_hot_dead_cleanup >>
        serialized_false_color >> serialized_highlight_recovery)) {
    return fail("test fixture should expose the canonical v13.1 decode controls");
  }
  const auto after_decode_controls = legacy_stream.tellg();
  auto neutral_legacy_serialized = previous_serialized;
  neutral_legacy_serialized.insert(
      static_cast<std::size_t>(after_decode_controls),
      " 0 1");
  neutral_legacy_serialized.insert(
      static_cast<std::size_t>(before_decode_controls),
      " 0 1 0 0 0");
  neutral_legacy_serialized.replace(
      neutral_legacy_serialized.find("photo-editor-edit-recipe-v13.1"),
      std::string("photo-editor-edit-recipe-v13.1").size(),
      "photo-editor-edit-recipe-v13");
  const auto neutral_legacy_decoded =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          neutral_legacy_serialized,
          &decode_error);
  if (!neutral_legacy_decoded.has_value() ||
      *neutral_legacy_decoded != recipe) {
    return fail(
        "neutral legacy-v13 serialization should migrate through the read-only compatibility parser");
  }
  auto nonneutral_legacy_serialized = neutral_legacy_serialized;
  const auto retired_controls_offset =
      static_cast<std::size_t>(before_decode_controls) - 2U;
  nonneutral_legacy_serialized.replace(
      retired_controls_offset + 1,
      1,
      "0.25");
  if (photo_editor::pipeline::deserialize_edit_recipe_v13(
          nonneutral_legacy_serialized,
          &decode_error)
          .has_value()) {
    return fail(
        "legacy-v13 serialization with retired no-op RAW intent must fail closed");
  }
  auto negative_zero_recipe = recipe;
  negative_zero_recipe.scene.exposure_ev = -0.0;
  auto positive_zero_recipe = recipe;
  positive_zero_recipe.scene.exposure_ev = 0.0;
  if (photo_editor::pipeline::serialize_edit_recipe_v13(
          negative_zero_recipe) !=
      photo_editor::pipeline::serialize_edit_recipe_v13(
          positive_zero_recipe)) {
    return fail("v13 normalization should canonicalize signed zero");
  }

  auto modified_recipe = recipe;
  modified_recipe.scene.exposure_ev += 0.01;
  if (photo_editor::pipeline::edit_recipe_v13_cache_key(modified_recipe) ==
      photo_editor::pipeline::edit_recipe_v13_cache_key(recipe)) {
    return fail("recipe cache identity should change with edit intent");
  }

  auto invalid_recipe = recipe;
  invalid_recipe.scene.exposure_ev =
      std::numeric_limits<double>::quiet_NaN();
  invalid_recipe.layers.front().mask = AssetIdentifier{"missing-mask"};
  const auto invalid_validation =
      photo_editor::pipeline::validate_edit_recipe_v13(invalid_recipe);
  if (invalid_validation.ok() ||
      !std::any_of(
          invalid_validation.errors.begin(),
          invalid_validation.errors.end(),
          [](const auto& error) {
            return error.code == RecipeErrorCode::NonFiniteValue;
          }) ||
      !std::any_of(
          invalid_validation.errors.begin(),
          invalid_validation.errors.end(),
          [](const auto& error) {
            return error.path == "layers[0].mask";
          })) {
    return fail("v13 validation should return structured invalid-state errors");
  }
  if (photo_editor::pipeline::deserialize_edit_recipe_v13(
          serialized + " unexpected",
          &decode_error)
          .has_value() ||
      decode_error.code != RecipeErrorCode::MalformedSerialization) {
    return fail("v13 decoder should reject non-canonical trailing data");
  }

  const RecipeCompiler compiler;
  const auto context = representative_context();
  const auto first_compile = compiler.compile(recipe, context);
  const auto second_compile = compiler.compile(recipe, context);
  if (!first_compile.ok() || !second_compile.ok() ||
      first_compile.plan != second_compile.plan) {
    return fail("recipe compiler should produce a deterministic render plan");
  }
  const auto& plan = *first_compile.plan;
  const auto expected_stage_order = std::vector<RenderStageKind>{
      RenderStageKind::Decode,
      RenderStageKind::NormalizeRaw,
      RenderStageKind::CameraCharacterization,
      RenderStageKind::SceneAdjustments,
      RenderStageKind::Tone,
      RenderStageKind::Curves,
      RenderStageKind::Look,
      RenderStageKind::Layers,
      RenderStageKind::Detail,
      RenderStageKind::Geometry,
      RenderStageKind::PhotographicOutput,
      RenderStageKind::Proof,
      RenderStageKind::DeviceTransform,
      RenderStageKind::Publish,
  };
  auto actual_stage_order = std::vector<RenderStageKind>{};
  actual_stage_order.reserve(plan.stages.size());
  std::transform(
      plan.stages.cbegin(),
      plan.stages.cend(),
      std::back_inserter(actual_stage_order),
      [](const auto& stage) { return stage.kind; });
  if (plan.plan_cache_key.empty() ||
      photo_editor::pipeline::serialize_render_plan(plan) !=
          photo_editor::pipeline::serialize_render_plan(*second_compile.plan) ||
      actual_stage_order != expected_stage_order ||
      !has_stage(plan, RenderStageKind::NormalizeRaw) ||
      !has_stage(plan, RenderStageKind::CameraCharacterization) ||
      !has_stage(plan, RenderStageKind::Tone) ||
      !has_stage(plan, RenderStageKind::Layers) ||
      !has_stage(plan, RenderStageKind::PhotographicOutput) ||
      !has_stage(plan, RenderStageKind::Proof) ||
      !has_stage(plan, RenderStageKind::DeviceTransform) ||
      !has_stage(plan, RenderStageKind::Publish)) {
    return fail("compiled plan should identify the canonical color stages");
  }

  auto output_changed_recipe = recipe;
  output_changed_recipe.output.rendering_target =
      DisplayRenderingTarget::SrgbSimulation;
  const auto output_changed_compile =
      compiler.compile(output_changed_recipe, context);
  if (!output_changed_compile.ok() ||
      stage_identity(plan, RenderStageKind::Decode) !=
          stage_identity(*output_changed_compile.plan, RenderStageKind::Decode) ||
      stage_identity(plan, RenderStageKind::Geometry) !=
          stage_identity(*output_changed_compile.plan, RenderStageKind::Geometry) ||
      stage_identity(plan, RenderStageKind::PhotographicOutput) ==
          stage_identity(
              *output_changed_compile.plan,
              RenderStageKind::PhotographicOutput) ||
      stage_identity(plan, RenderStageKind::Publish) ==
          stage_identity(*output_changed_compile.plan, RenderStageKind::Publish)) {
    return fail("output changes should invalidate output stages without invalidating scene work");
  }

  auto transition_recipe = recipe;
  auto delivery_recipe = recipe;
  delivery_recipe.output.export_color_space = photo_editor::pipeline::ExportColorSpaceV13::DisplayP3;
  const auto delivery_compile = compiler.compile(delivery_recipe, context);
  if (!delivery_compile.ok() ||
      stage_identity(plan, RenderStageKind::Geometry) !=
          stage_identity(*delivery_compile.plan, RenderStageKind::Geometry) ||
      stage_identity(plan, RenderStageKind::PhotographicOutput) ==
          stage_identity(*delivery_compile.plan, RenderStageKind::PhotographicOutput) ||
      stage_identity(plan, RenderStageKind::Publish) ==
          stage_identity(*delivery_compile.plan, RenderStageKind::Publish)) {
    return fail("delivery color space must invalidate output stages while retaining scene work");
  }
  transition_recipe.output.renderer_transition_look =
      photo_editor::pipeline::RendererTransitionLookV13::
          PreserveLegacyDirectLuminanceV1;
  const auto transition_serialized =
      photo_editor::pipeline::serialize_edit_recipe_v13(
          transition_recipe);
  const auto transition_decoded =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          transition_serialized,
          &decode_error);
  const auto transition_compile =
      compiler.compile(transition_recipe, context);
  if (!transition_decoded.has_value() ||
      *transition_decoded != transition_recipe ||
      !transition_compile.ok() ||
      transition_compile.plan->renderer_transition_look !=
          transition_recipe.output.renderer_transition_look ||
      stage_identity(plan, RenderStageKind::Geometry) !=
          stage_identity(
              *transition_compile.plan,
              RenderStageKind::Geometry) ||
      stage_identity(plan, RenderStageKind::PhotographicOutput) ==
          stage_identity(
              *transition_compile.plan,
              RenderStageKind::PhotographicOutput)) {
    return fail(
        "renderer-transition intent should round-trip canonically and invalidate only the output suffix");
  }

  for (const bool temperature : {true, false}) {
    auto white_balance_recipe = recipe;
    if (temperature) white_balance_recipe.look.temperature += 0.1;
    else white_balance_recipe.look.tint += 0.1;
    const auto white_balance_compile = compiler.compile(white_balance_recipe, context);
    if (!white_balance_compile.ok() ||
        stage_identity(plan, RenderStageKind::Decode) !=
            stage_identity(*white_balance_compile.plan, RenderStageKind::Decode) ||
        stage_identity(plan, RenderStageKind::NormalizeRaw) !=
            stage_identity(*white_balance_compile.plan, RenderStageKind::NormalizeRaw) ||
        stage_identity(plan, RenderStageKind::CameraCharacterization) ==
            stage_identity(*white_balance_compile.plan, RenderStageKind::CameraCharacterization) ||
        stage_identity(plan, RenderStageKind::Publish) ==
            stage_identity(*white_balance_compile.plan, RenderStageKind::Publish)) {
      return fail("temperature/tint must invalidate characterization and downstream pixels while reusing decode");
    }
  }

  auto exposure_changed_recipe = recipe;
  exposure_changed_recipe.scene.exposure_ev += 0.5;
  const auto exposure_changed_compile =
      compiler.compile(exposure_changed_recipe, context);
  if (!exposure_changed_compile.ok() ||
      stage_identity(plan, RenderStageKind::Decode) !=
          stage_identity(*exposure_changed_compile.plan, RenderStageKind::Decode) ||
      stage_identity(plan, RenderStageKind::CameraCharacterization) !=
          stage_identity(
              *exposure_changed_compile.plan,
              RenderStageKind::CameraCharacterization) ||
      stage_identity(plan, RenderStageKind::SceneAdjustments) ==
          stage_identity(
              *exposure_changed_compile.plan,
              RenderStageKind::SceneAdjustments) ||
      stage_identity(plan, RenderStageKind::Publish) ==
          stage_identity(*exposure_changed_compile.plan, RenderStageKind::Publish)) {
    return fail("exposure changes should preserve decode/profile caches and invalidate downstream work");
  }

  auto export_context = context;
  export_context.purpose = RenderPurpose::Export;
  const auto export_compile = compiler.compile(recipe, export_context);
  if (!export_compile.ok() ||
      export_compile.plan->purpose != RenderPurpose::Export ||
      stage_identity(plan, RenderStageKind::DeviceTransform) !=
          stage_identity(*export_compile.plan, RenderStageKind::DeviceTransform) ||
      stage_identity(plan, RenderStageKind::Publish) ==
          stage_identity(*export_compile.plan, RenderStageKind::Publish)) {
    return fail("render purpose should alter publication identity without rebuilding color stages");
  }

  const auto scope_domains =
      std::vector<std::pair<
          RenderScopeDomain,
          RenderStageKind>>{
          {RenderScopeDomain::SceneLinear,
           RenderStageKind::CameraCharacterization},
          {RenderScopeDomain::Working,
           RenderStageKind::Geometry},
          {RenderScopeDomain::PreOutput,
           RenderStageKind::PhotographicOutput},
          {RenderScopeDomain::Proof,
           RenderStageKind::Proof},
          {RenderScopeDomain::FinalDisplay,
           RenderStageKind::Publish},
      };
  auto scope_plan_keys = std::set<std::string>{};
  for (const auto& [domain, stop_stage] :
       scope_domains) {
    auto scope_context = context;
    scope_context.purpose = RenderPurpose::Scope;
    scope_context.scope_domain = domain;
    const auto scope_compile =
        compiler.compile(recipe, scope_context);
    if (!scope_compile.ok() ||
        scope_compile.plan->purpose !=
            RenderPurpose::Scope ||
        scope_compile.plan->scope_domain != domain ||
        scope_compile.plan->stages.empty() ||
        scope_compile.plan->stages.back().kind !=
            stop_stage ||
        !scope_plan_keys
             .insert(
                 scope_compile.plan->plan_cache_key)
             .second ||
        photo_editor::pipeline::
                render_scope_domain_stop_stage(domain) !=
            stop_stage ||
        photo_editor::pipeline::
            render_scope_domain_name(domain)
                .empty()) {
      return fail(
          "scope domains should compile distinct canonical stopping plans");
    }
  }
  auto no_proof_recipe = recipe;
  no_proof_recipe.proof = {};
  auto proof_scope_context = context;
  proof_scope_context.purpose =
      RenderPurpose::Scope;
  proof_scope_context.scope_domain =
      RenderScopeDomain::Proof;
  const auto unavailable_proof_scope =
      compiler.compile(
          no_proof_recipe,
          proof_scope_context);
  if (unavailable_proof_scope.ok() ||
      unavailable_proof_scope.plan.has_value() ||
      !std::any_of(
          unavailable_proof_scope.errors.begin(),
          unavailable_proof_scope.errors.end(),
          [](const auto& error) {
            return error.path == "scope.domain";
          })) {
    return fail(
        "proof scope should fail closed without an enabled proof recipe");
  }

  auto missing_profile_context = context;
  missing_profile_context.available_profile_assets.erase(
      missing_profile_context.available_profile_assets.begin());
  const auto missing_profile_compile =
      compiler.compile(recipe, missing_profile_context);
  if (missing_profile_compile.ok() ||
      missing_profile_compile.plan.has_value() ||
      !std::any_of(
          missing_profile_compile.errors.begin(),
          missing_profile_compile.errors.end(),
          [](const auto& error) {
            return error.code ==
                       RecipeCompileErrorCode::ProfileUnavailable &&
                   error.path == "input.camera_profile";
          })) {
    return fail("compiler must fail closed when an identified profile is unavailable");
  }

  auto unsupported_lut_space_recipe = recipe;
  unsupported_lut_space_recipe.look.luts.front().input_color_space =
      "Unknown Camera Log";
  unsupported_lut_space_recipe.layers.front().look.luts.push_back(
      recipe.look.luts.front());
  unsupported_lut_space_recipe.layers.front()
      .look.luts.front()
      .output_color_space = "Unknown Display Encoding";
  const auto unsupported_lut_space_compile =
      compiler.compile(unsupported_lut_space_recipe, context);
  const auto has_compile_error = [&](std::string_view path) {
    return std::any_of(
        unsupported_lut_space_compile.errors.begin(),
        unsupported_lut_space_compile.errors.end(),
        [&](const auto& error) {
          return error.code ==
                     RecipeCompileErrorCode::UnsupportedColorSpace &&
                 error.path == path;
        });
  };
  if (unsupported_lut_space_compile.ok() ||
      unsupported_lut_space_compile.plan.has_value() ||
      !has_compile_error("look.luts[0].input_color_space") ||
      !has_compile_error(
          "layers[0].look.luts[0].output_color_space")) {
    return fail(
        "compiler must reject unsupported global and layer LUT color spaces with structured paths");
  }

  auto content_recipe = recipe;
  content_recipe.masks.front().sources.push_back(
      MaskSourceRecipeV13{
          .id = AssetIdentifier{"brush-source"},
          .kind = photo_editor::pipeline::MaskSourceKind::BrushRaster,
          .operation =
              photo_editor::pipeline::MaskCombineOperation::Add,
          .enabled = true,
          .coordinate_space =
              photo_editor::pipeline::MaskCoordinateSpace::SourcePixels,
          .sampling =
              photo_editor::pipeline::MaskSamplingMode::Nearest,
          .payload_asset = AssetIdentifier{"brush-payload"},
      });
  photo_editor::pipeline::LayerRecipeV13 pixel_layer;
  pixel_layer.id = AssetIdentifier{"pixel-layer"};
  pixel_layer.kind = photo_editor::pipeline::LayerKind::Pixel;
  pixel_layer.content_asset = AssetIdentifier{"pixel-content"};
  content_recipe.layers.push_back(pixel_layer);
  auto content_context = context;
  content_context.available_content_assets = {
      AssetIdentifier{"brush-payload"},
      AssetIdentifier{"creative-lut-payload"},
      AssetIdentifier{"pixel-content"},
  };
  if (!compiler.compile(content_recipe, content_context).ok()) {
    return fail("compiler should resolve identified mask and layer content assets");
  }
  content_context.available_content_assets.pop_back();
  const auto unavailable_content_compile =
      compiler.compile(content_recipe, content_context);
  if (unavailable_content_compile.ok() ||
      !std::any_of(
          unavailable_content_compile.errors.begin(),
          unavailable_content_compile.errors.end(),
          [](const auto& error) {
            return error.code ==
                       RecipeCompileErrorCode::AssetUnavailable &&
                   error.path == "layers[1].content_asset";
          })) {
    return fail("compiler must fail closed when identified layer content is unavailable");
  }

  auto invalid_geometry_context = context;
  invalid_geometry_context.source_width = 0;
  const auto invalid_geometry_compile =
      compiler.compile(recipe, invalid_geometry_context);
  if (invalid_geometry_compile.ok() ||
      invalid_geometry_compile.plan.has_value()) {
    return fail("compiler must reject missing source geometry");
  }

  std::uint64_t random_state = 0x9e3779b97f4a7c15ULL;
  auto next_unit = [&]() {
    random_state =
        random_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>(random_state >> 11U) /
           static_cast<double>(std::uint64_t{1} << 53U);
  };
  std::set<std::string> property_cache_keys;
  for (int iteration = 0; iteration < 512; ++iteration) {
    auto property_recipe = recipe;
    property_recipe.scene.exposure_ev = next_unit() * 10.0 - 5.0;
    property_recipe.tone.contrast = next_unit() * 2.0 - 1.0;
    property_recipe.tone.highlights = next_unit() * 2.0 - 1.0;
    property_recipe.tone.shadows = next_unit() * 2.0 - 1.0;
    property_recipe.look.vibrance = next_unit() * 2.0 - 1.0;
    property_recipe.look.saturation = next_unit() * 2.0 - 1.0;
    property_recipe.layers.front().opacity = next_unit();
    property_recipe.geometry.crop_x = next_unit() * 0.2;
    property_recipe.geometry.crop_y = next_unit() * 0.2;
    property_recipe.geometry.crop_width =
        0.5 + next_unit() * (0.5 - property_recipe.geometry.crop_x);
    property_recipe.geometry.crop_height =
        0.5 + next_unit() * (0.5 - property_recipe.geometry.crop_y);
    const auto property_validation =
        photo_editor::pipeline::validate_edit_recipe_v13(property_recipe);
    const auto property_serialized =
        photo_editor::pipeline::serialize_edit_recipe_v13(property_recipe);
    const auto property_decoded =
        photo_editor::pipeline::deserialize_edit_recipe_v13(
            property_serialized);
    const auto property_compile =
        compiler.compile(property_recipe, context);
    if (!property_validation.ok() || !property_decoded.has_value() ||
        *property_decoded != property_recipe || !property_compile.ok() ||
        compiler.compile(property_recipe, context).plan !=
            property_compile.plan) {
      return fail("v13 property sweep should validate, round-trip, and compile deterministically");
    }
    property_cache_keys.insert(
        photo_editor::pipeline::edit_recipe_v13_cache_key(property_recipe));
  }
  if (property_cache_keys.size() != 512U) {
    return fail("v13 property sweep should produce distinct cache identities");
  }

  std::set<std::string> stack_cache_keys;
  for (const auto layer_count : {10, 50, 100}) {
    auto stacked_recipe = recipe;
    stacked_recipe.layers.clear();
    stacked_recipe.masks.clear();
    stacked_recipe.look.luts.clear();
    for (int index = 0; index < layer_count; ++index) {
      photo_editor::pipeline::LayerRecipeV13 layer;
      layer.id = photo_editor::pipeline::AssetIdentifier{
          "stack-layer-" + std::to_string(index)};
      layer.opacity =
          0.35 + static_cast<double>(index % 7) * 0.08;
      layer.scene.exposure_ev =
          (static_cast<double>(index % 9) - 4.0) * 0.04;
      layer.tone.contrast =
          (static_cast<double>(index % 11) - 5.0) * 0.015;
      layer.tone.highlights =
          (static_cast<double>(index % 5) - 2.0) * 0.025;
      layer.tone.shadows =
          (static_cast<double>(index % 7) - 3.0) * 0.02;
      layer.look.saturation =
          (static_cast<double>(index % 13) - 6.0) * 0.01;
      stacked_recipe.layers.push_back(std::move(layer));
    }
    const auto serialized_stack =
        photo_editor::pipeline::serialize_edit_recipe_v13(
            stacked_recipe);
    const auto decoded_stack =
        photo_editor::pipeline::deserialize_edit_recipe_v13(
            serialized_stack);
    const auto compiled_stack =
        compiler.compile(stacked_recipe, context);
    if (serialized_stack.size() > 1024U * 1024U ||
        !decoded_stack.has_value() ||
        *decoded_stack != stacked_recipe ||
        !compiled_stack.ok() ||
        compiler.compile(stacked_recipe, context).plan !=
            compiled_stack.plan) {
      return fail("deep v13 adjustment stacks should remain bounded, serializable, and deterministic");
    }
    stack_cache_keys.insert(
        photo_editor::pipeline::edit_recipe_v13_cache_key(
            stacked_recipe));
  }
  if (stack_cache_keys.size() != 3U) {
    return fail("10, 50, and 100 layer recipes must have distinct cache identities");
  }

  return EXIT_SUCCESS;
}

#include "edit_recipe_v13_adapter.h"
#include "photo_layer_effect_codec.h"

#include "photo_editor/pipeline/recipe_compiler.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::DesktopRecipeV13Context context() {
  return photo_editor::desktop::DesktopRecipeV13Context{
      .recipe_id = QStringLiteral("desktop-recipe-1"),
      .source_id = QStringLiteral("raw-source-1"),
      .decoder_provenance =
          QStringLiteral("libraw:0.21.5:camera-linear"),
      .raw_source = true,
      .rendering_target =
          photo_editor::pipeline::DisplayRenderingTarget::ActiveDisplay,
      .device_icc_profile =
          photo_editor::pipeline::AssetIdentifier{"display-profile"},
  };
}

bool has_error_path(
    const photo_editor::desktop::DesktopRecipeV13AdapterResult& result,
    const std::string& path) {
  return std::any_of(
      result.errors.begin(),
      result.errors.end(),
      [&](const auto& error) {
        return error.path == path;
      });
}

}  // namespace

int main() {
  {
    photo_editor::desktop::EditRecipeState rectangle;
    rectangle.mask_stack = {QVariantMap{{"id", "rectangle-selection"}, {"kind", "rectangle"},
        {"operation", "add"}, {"blendMode", "normal"}, {"maskSetId", "rectangle-set"}}};
    photo_editor::desktop::DesktopRenderPlanRequest request;
    request.source_id = QStringLiteral("rectangle-photo");
    request.recipe_id = QStringLiteral("rectangle-recipe");
    request.decoder_provenance = QStringLiteral("qt-image-reader:srgb");
    request.source_width = 640;
    request.source_height = 400;
    const auto plan = photo_editor::desktop::compile_desktop_render_plan_with_derived_assets(rectangle, request);
    if (!plan.ok()) return fail("rectangle selection must be admitted for preview and export");
  }
  photo_editor::desktop::EditRecipeState state;
  state.photographic_rendering_model =
      QStringLiteral("aces2-ocio-v1");
  state.renderer_transition_look =
      QStringLiteral("preserve-legacy-direct-luminance-v1");
  state.exposure = 0.75;
  state.contrast = 25.0;
  state.highlights = -30.0;
  state.brightness = 8.0;
  state.hdr_local_tone_mapping = 11.0;
  state.filmic_shoulder = -6.0;
  state.filmic_toe = 4.0;
  state.adaptive_contrast = 9.0;
  state.levels_black_point = 2.0;
  state.levels_gamma = -3.0;
  state.levels_white_point = 5.0;
  state.zone_deep_shadows = 7.0;
  state.zone_shadows = 6.0;
  state.zone_midtones = -4.0;
  state.zone_lights = 3.0;
  state.zone_highlights = -8.0;
  state.vibrance = 15.0;
  state.texture = 10.0;
  state.dehaze = -12.0;
  state.sharpening = 40.0;
  state.sharpen_radius = 1.7;
  state.sharpen_detail = 35.0;
  state.sharpen_masking = 22.0;
  state.crop_rect_x = 0.1;
  state.crop_rect_y = 0.1;
  state.crop_rect_width = 0.8;
  state.crop_rect_height = 0.8;
  state.lens_distortion = -14.0;
  state.lens_vignette = 9.0;
  state.chromatic_aberration = 6.0;
  state.perspective_vertical = -8.0;
  state.perspective_horizontal = 5.0;
  state.transform_scale = 1.2;
  state.transform_skew_x = 7.0;
  state.transform_skew_y = -4.0;
  state.crop_zoom = 1.1;
  state.upright_mode = QStringLiteral("Guided");
  state.liquify_center_x = 0.4;
  state.liquify_center_y = 0.6;
  state.liquify_radius = 0.3;
  state.liquify_strength = 12.0;
  state.content_aware_crop_fill = true;
  state.render_intent = QStringLiteral("Relative Colorimetric");
  state.output_gamut_mapping_enabled = false;
  state.output_gamut_mapping_policy =
      QStringLiteral("Preserve Saturation");
  state.black_point_compensation_enabled = false;
  state.source_black_point = 0.03;
  state.output_black_point = 0.05;
  state.color_mixer_model = QStringLiteral("LCH");
  state.mixer_channels[0].hue = 12.0;
  state.mixer_channels[0].selective_cyan = -8.0;
  state.rgb_channel_mixer.red_from_green = 14.0;
  state.black_and_white_enabled = true;
  state.black_and_white_mixer[1] = 22.0;
  state.color_replacement_enabled = true;
  state.color_replacement_source_hue = 0.12;
  state.color_replacement_strength = 45.0;
  state.skin_tone_protection_enabled = true;
  state.skin_tone_protection_strength = 35.0;
  state.color_harmony_enabled = true;
  state.color_harmony_mode = QStringLiteral("Triadic");
  state.color_harmony_strength = 28.0;
  state.grading_shadows_hue = 0.7;
  state.grading_shadows_saturation = 18.0;
  state.grading_amount = 82.0;
  state.grading_balance = -16.0;
  state.raw_demosaic_quality = QStringLiteral("Full");
  state.highlight_reconstruction_mode =
      QStringLiteral("Camera Default");
  state.chromatic_adaptation_method = QStringLiteral("CAT02");
  state.raw_camera_matching_look = QStringLiteral("Portrait");
  state.raw_hot_dead_pixel_cleanup = 0.1;
  state.raw_false_color_suppression = 0.12;
  state.raw_highlight_recovery = false;
  state.raw_gamut_mapping = false;
  state.raw_lens_correction = false;
  state.raw_calibration_hue = 8.0;
  state.raw_red_primary_saturation = -9.0;
  state.camera_dcp_profile_asset.asset_id =
      QStringLiteral("camera-profile");

  const auto adapted =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          state,
          context());
  if (!adapted.ok() ||
      adapted.recipe->scene.exposure_ev != 0.75 ||
      adapted.recipe->tone.contrast != 0.25 ||
      adapted.recipe->tone.highlights != -0.3 ||
      adapted.recipe->tone.brightness != 0.08 ||
      adapted.recipe->tone.hdr_local_tone_mapping != 0.11 ||
      adapted.recipe->tone.filmic_shoulder != -0.06 ||
      adapted.recipe->tone.filmic_toe != 0.04 ||
      adapted.recipe->tone.adaptive_contrast != 0.09 ||
      adapted.recipe->tone.levels_black_point != 0.02 ||
      adapted.recipe->tone.levels_gamma != -0.03 ||
      adapted.recipe->tone.levels_white_point != 0.05 ||
      adapted.recipe->tone.zone_deep_shadows != 0.07 ||
      adapted.recipe->tone.zone_shadows != 0.06 ||
      adapted.recipe->tone.zone_midtones != -0.04 ||
      adapted.recipe->tone.zone_lights != 0.03 ||
      adapted.recipe->tone.zone_highlights != -0.08 ||
      adapted.recipe->look.vibrance != 0.15 ||
      adapted.recipe->detail.texture != 0.1 ||
      adapted.recipe->detail.dehaze != -0.12 ||
      adapted.recipe->detail.sharpening != 0.4 ||
      adapted.recipe->detail.sharpen_radius != 1.7 ||
      adapted.recipe->detail.sharpen_detail != 0.35 ||
      adapted.recipe->detail.sharpen_masking != 0.22 ||
      adapted.recipe->geometry.crop_width != 0.8 ||
      adapted.recipe->geometry.lens_distortion != -0.14 ||
      adapted.recipe->geometry.lens_vignette != 0.09 ||
      adapted.recipe->geometry.chromatic_aberration != 0.06 ||
      adapted.recipe->geometry.perspective_vertical != -0.08 ||
      adapted.recipe->geometry.perspective_horizontal != 0.05 ||
      adapted.recipe->geometry.scale != 1.2 ||
      adapted.recipe->geometry.skew_x != 0.07 ||
      adapted.recipe->geometry.skew_y != -0.04 ||
      adapted.recipe->geometry.crop_zoom != 1.1 ||
      adapted.recipe->geometry.upright_mode !=
          photo_editor::pipeline::UprightMode::Guided ||
      adapted.recipe->geometry.liquify_center_x != 0.4 ||
      adapted.recipe->geometry.liquify_center_y != 0.6 ||
      adapted.recipe->geometry.liquify_radius != 0.3 ||
      adapted.recipe->geometry.liquify_strength != 0.12 ||
      !adapted.recipe->geometry.content_aware_crop_fill ||
      adapted.recipe->output.render_intent !=
          photo_editor::pipeline::OutputRenderIntent::RelativeColorimetric ||
      adapted.recipe->output.renderer_transition_look !=
          photo_editor::pipeline::RendererTransitionLookV13::
              PreserveLegacyDirectLuminanceV1 ||
      adapted.recipe->output.gamut_mapping_enabled ||
      adapted.recipe->output.gamut_mapping_policy !=
          photo_editor::pipeline::OutputGamutMappingPolicy::PreserveSaturation ||
      adapted.recipe->output.black_point_compensation ||
      adapted.recipe->output.source_black_point != 0.03 ||
      adapted.recipe->output.output_black_point != 0.05 ||
      adapted.recipe->look.mixer_model !=
          photo_editor::pipeline::ColorMixerModelV13::Lch ||
      adapted.recipe->look.mixer_channels[0].hue != 0.12 ||
      adapted.recipe->look.mixer_channels[0].selective_cyan != -0.08 ||
      adapted.recipe->look.rgb_channel_mixer.red_from_green != 0.14 ||
      !adapted.recipe->look.black_and_white_enabled ||
      adapted.recipe->look.black_and_white_mixer[1] != 0.22 ||
      !adapted.recipe->look.color_replacement_enabled ||
      adapted.recipe->look.color_replacement_source_hue != 0.12 ||
      adapted.recipe->look.color_replacement_strength != 0.45 ||
      !adapted.recipe->look.skin_tone_protection_enabled ||
      adapted.recipe->look.skin_tone_protection_strength != 0.35 ||
      !adapted.recipe->look.color_harmony_enabled ||
      adapted.recipe->look.color_harmony_mode !=
          photo_editor::pipeline::ColorHarmonyModeV13::Triadic ||
      adapted.recipe->look.color_harmony_strength != 0.28 ||
      adapted.recipe->look.grading_shadows.hue != 0.7 ||
      adapted.recipe->look.grading_shadows.saturation != 0.18 ||
      adapted.recipe->look.grading_amount != 0.82 ||
      adapted.recipe->look.grading_balance != -0.16 ||
      adapted.recipe->input.demosaic_quality !=
          photo_editor::pipeline::RawDemosaicQualityV13::Full ||
      adapted.recipe->input.highlight_reconstruction !=
          photo_editor::pipeline::HighlightReconstructionModeV13::CameraDefault ||
      adapted.recipe->input.chromatic_adaptation !=
          photo_editor::pipeline::ChromaticAdaptationMethodV13::Cat02 ||
      adapted.recipe->input.camera_matching_look !=
          photo_editor::pipeline::CameraMatchingLookV13::Portrait ||
      adapted.recipe->input.hot_dead_pixel_cleanup != 0.1 ||
      adapted.recipe->input.false_color_suppression != 0.12 ||
      adapted.recipe->input.highlight_recovery_enabled ||
      adapted.recipe->input.calibration_hue != 0.08 ||
      adapted.recipe->input.red_primary_saturation != -0.09 ||
      !adapted.recipe->input.camera_profile.has_value() ||
      adapted.recipe->input.camera_profile->value != "camera-profile") {
    return fail("representable desktop v12 intent should map exactly to v13");
  }

  auto retired_raw_control_state = state;
  retired_raw_control_state.raw_boost = 0.1;
  const auto retired_raw_control_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          retired_raw_control_state,
          context());
  if (retired_raw_control_result.ok() ||
      !has_error_path(
          retired_raw_control_result,
          "input.retired_raw_controls")) {
    return fail(
        "desktop adapter must reject legacy RAW fields that never had an executable operator");
  }

  photo_editor::pipeline::RecipeCompileContext compile_context{
      .source_width = 6000,
      .source_height = 4000,
      .available_profile_assets = {
          photo_editor::pipeline::AssetIdentifier{"camera-profile"},
          photo_editor::pipeline::AssetIdentifier{"display-profile"},
      },
  };
  const photo_editor::pipeline::RecipeCompiler compiler;
  const auto compiled =
      compiler.compile(*adapted.recipe, compile_context);
  if (!compiled.ok()) {
    return fail("adapted desktop recipe should compile through the sole v13 compiler");
  }
  const auto preview_plan =
      photo_editor::desktop::compile_desktop_render_plan(
          state, context(), compile_context);
  const auto export_plan =
      photo_editor::desktop::compile_desktop_render_plan(
          state, context(), compile_context);
  if (!preview_plan.ok() || !export_plan.ok() ||
      photo_editor::pipeline::serialize_render_plan(*preview_plan.plan) !=
          photo_editor::pipeline::serialize_render_plan(
              *export_plan.plan)) {
    return fail(
        "desktop preview and export boundaries should receive one byte-identical compiled plan");
  }
  const auto production_request =
      photo_editor::desktop::DesktopRenderPlanRequest{
          .recipe_id = QStringLiteral("desktop-recipe-1"),
          .source_id = QStringLiteral("raw-source-1"),
          .decoder_provenance =
              QStringLiteral("libraw:0.21.5:camera-linear"),
          .raw_source = true,
          .source_width = 6000,
          .source_height = 4000,
          .device_icc_profile =
              photo_editor::pipeline::AssetIdentifier{"display-profile"},
      };
  const auto derived_plan_a =
      photo_editor::desktop::compile_desktop_render_plan_with_derived_assets(
          state, production_request);
  const auto derived_plan_b =
      photo_editor::desktop::compile_desktop_render_plan_with_derived_assets(
          state, production_request);
  if (!derived_plan_a.ok() || !derived_plan_b.ok() ||
      photo_editor::pipeline::serialize_render_plan(*derived_plan_a.plan) !=
          photo_editor::pipeline::serialize_render_plan(
              *derived_plan_b.plan)) {
    return fail(
        "production plan context should derive stable profile/content assets and a byte-identical plan");
  }

  auto builtin_proof_state = state;
  builtin_proof_state.soft_proof_enabled = true;
  builtin_proof_state.soft_proof_profile = QStringLiteral("sRGB");
  const auto builtin_proof_recipe =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          builtin_proof_state,
          context());
  const auto builtin_proof_plan =
      photo_editor::desktop::compile_desktop_render_plan_with_derived_assets(
          builtin_proof_state,
          production_request);
  if (!builtin_proof_recipe.ok() ||
      builtin_proof_recipe.recipe->proof.mode !=
          photo_editor::pipeline::ProofMode::Icc ||
      !builtin_proof_recipe.recipe->proof.profile.has_value() ||
      builtin_proof_recipe.recipe->proof.profile->value !=
          "builtin:icc:srgb" ||
      !builtin_proof_plan.ok()) {
    return fail(
        "built-in proof spaces should compile through stable immutable v13 profile identities");
  }

  auto unavailable_context = compile_context;
  unavailable_context.available_profile_assets.clear();
  const auto unavailable_plan =
      photo_editor::desktop::compile_desktop_render_plan(
          state, context(), unavailable_context);
  if (unavailable_plan.ok() || unavailable_plan.plan.has_value() ||
      unavailable_plan.compiler_errors.empty()) {
    return fail(
        "desktop plan compilation must preserve fail-closed compiler diagnostics");
  }

  auto legacy_renderer = state;
  legacy_renderer.photographic_rendering_model =
      QStringLiteral("legacy-oklch-v1");
  const auto legacy_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          legacy_renderer,
          context());
  if (legacy_result.ok() || legacy_result.recipe.has_value() ||
      !has_error_path(legacy_result, "look.renderer")) {
    return fail("desktop adapter must not relabel the legacy renderer as ACES 2");
  }

  auto ambiguous_exposure = state;
  ambiguous_exposure.raw_exposure = 0.5;
  const auto ambiguous_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          ambiguous_exposure,
          context());
  if (ambiguous_result.ok() ||
      !has_error_path(ambiguous_result, "scene.exposure_ev")) {
    return fail("desktop adapter must reject duplicate exposure ownership");
  }

  auto unsupported_mask = state;
  unsupported_mask.linear_mask_enabled = true;
  unsupported_mask.linear_mask_exposure = 0.6;
  unsupported_mask.linear_mask_temperature = -20.0;
  unsupported_mask.linear_mask_feather = 0.3;
  unsupported_mask.linear_mask_position = 0.65;
  unsupported_mask.linear_mask_rotation = 14.0;
  const auto mask_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          unsupported_mask,
          context());
  if (!mask_result.ok() || mask_result.recipe->masks.size() != 1U ||
      mask_result.recipe->layers.size() != 1U ||
      mask_result.recipe->masks.front().sources.size() != 1U ||
      mask_result.recipe->masks.front().sources.front().kind !=
          photo_editor::pipeline::MaskSourceKind::LinearGradient ||
      mask_result.recipe->masks.front().sources.front().geometry.position !=
          0.65 ||
      mask_result.recipe->layers.front().scene.exposure_ev != 0.6 ||
      mask_result.recipe->layers.front().look.temperature != -0.2) {
    return fail("desktop analytic masks should become mask-backed v13 adjustment layers");
  }
  auto unsupported_mask_stack = state;
  unsupported_mask_stack.mask_stack = {
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("brush-mask")},
          {QStringLiteral("kind"), QStringLiteral("brush")},
      },
  };
  const auto mask_stack_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          unsupported_mask_stack,
          context());
  if (mask_stack_result.ok() || mask_stack_result.recipe.has_value() ||
      !has_error_path(
          mask_stack_result,
          "masks[0].sources[0].payload_asset")) {
    return fail("desktop adapter must fail closed on mask payloads it cannot identify");
  }
  auto mask_payload_context = context();
  mask_payload_context.mask_payload_assets.push_back(
      photo_editor::desktop::DesktopRecipeV13Context::MaskPayloadBinding{
          .source_id = QStringLiteral("brush-mask"),
          .payload_asset =
              photo_editor::pipeline::AssetIdentifier{
                  "mask-payload-sha256-001"},
      });
  const auto bound_mask_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          unsupported_mask_stack,
          mask_payload_context);
  if (!bound_mask_result.ok() ||
      bound_mask_result.recipe->masks.front()
              .sources.front()
              .payload_asset->value != "mask-payload-sha256-001") {
    return fail("desktop raster masks should migrate only through immutable payload bindings");
  }
  auto background_state = unsupported_mask_stack;
  auto background_mask = background_state.mask_stack.first().toMap();
  background_mask.insert(QStringLiteral("kind"), QStringLiteral("background"));
  background_state.mask_stack[0] = background_mask;
  const auto background_result = photo_editor::desktop::adapt_desktop_recipe_to_v13(background_state, mask_payload_context);
  if (!background_result.ok() || background_result.recipe->masks.front().sources.front().kind !=
          photo_editor::pipeline::MaskSourceKind::Object ||
      background_result.recipe->masks.front().sources.front().inverted ||
      background_result.recipe->masks.front().sources.front().payload_asset->value != "mask-payload-sha256-001")
    return fail("pre-inverted background alpha must reach the canonical renderer as an unchanged raster payload");
  auto imported_alpha_state = unsupported_mask_stack;
  auto imported_alpha = imported_alpha_state.mask_stack.first().toMap();
  imported_alpha.insert(QStringLiteral("kind"), QStringLiteral("alpha"));
  imported_alpha.insert(QStringLiteral("invert"), true);
  imported_alpha_state.mask_stack[0] = imported_alpha;
  const auto imported_alpha_result = photo_editor::desktop::adapt_desktop_recipe_to_v13(
      imported_alpha_state, mask_payload_context);
  if (!imported_alpha_result.ok() ||
      imported_alpha_result.recipe->masks.front().sources.front().kind !=
          photo_editor::pipeline::MaskSourceKind::Object ||
      !imported_alpha_result.recipe->masks.front().sources.front().inverted ||
      imported_alpha_result.recipe->masks.front().sources.front().payload_asset->value != "mask-payload-sha256-001")
    return fail("imported alpha must retain its immutable raster payload and explicit inversion");
  const auto unbound_alpha_result = photo_editor::desktop::adapt_desktop_recipe_to_v13(
      imported_alpha_state, context());
  if (unbound_alpha_result.ok() || !has_error_path(unbound_alpha_result, "masks[0].sources[0].payload_asset"))
    return fail("imported alpha without an immutable payload must remain rejected");
  auto combined_mask_state = state;
  combined_mask_state.mask_stack = {
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("linear-source")},
          {QStringLiteral("maskSetId"), QStringLiteral("combined-set")},
          {QStringLiteral("kind"), QStringLiteral("linear")},
          {QStringLiteral("operation"), QStringLiteral("add")},
          {QStringLiteral("exposure"), 0.3},
          {QStringLiteral("contrast"), 10.0},
          {QStringLiteral("position"), 0.62},
          {QStringLiteral("outputId"), QStringLiteral("combined-alpha")},
          {QStringLiteral("reusableOutput"), true},
      },
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("radial-source")},
          {QStringLiteral("parentId"), QStringLiteral("linear-source")},
          {QStringLiteral("maskSetId"), QStringLiteral("combined-set")},
          {QStringLiteral("kind"), QStringLiteral("radial")},
          {QStringLiteral("operation"), QStringLiteral("subtract")},
          {QStringLiteral("blendMode"), QStringLiteral("screen")},
          {QStringLiteral("exposure"), 0.3},
          {QStringLiteral("contrast"), 10.0},
          {QStringLiteral("centerX"), 0.4},
          {QStringLiteral("centerY"), 0.6},
      },
  };
  const auto combined_mask_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          combined_mask_state,
          context());
  if (!combined_mask_result.ok() ||
      combined_mask_result.recipe->masks.size() != 1U ||
      combined_mask_result.recipe->masks.front().sources.size() != 2U ||
      combined_mask_result.recipe->masks.front().sources[1].operation !=
          photo_editor::pipeline::MaskCombineOperation::Subtract ||
      !combined_mask_result.recipe->masks.front().sources[1].parent.has_value() ||
      combined_mask_result.recipe->masks.front().sources[1].blend_mode !=
          photo_editor::pipeline::BlendMode::Screen ||
      combined_mask_result.recipe->layers.front().tone.contrast != 0.1) {
    return fail("desktop mask sets should preserve ordered combine and shared adjustment semantics");
  }
  auto conflicting_source =
      combined_mask_state.mask_stack[1].toMap();
  conflicting_source.insert(
      QStringLiteral("adjustmentOverrides"),
      QVariantMap{
          {QStringLiteral("exposure"), 0.8},
          {QStringLiteral("contrast"), -25.0},
      });
  combined_mask_state.mask_stack[1] = conflicting_source;
  const auto conflicting_mask_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          combined_mask_state,
          context());
  if (!conflicting_mask_result.ok() ||
      conflicting_mask_result.recipe->layers.size() < 2U ||
      !conflicting_mask_result.recipe->layers[1].mask.has_value() ||
      conflicting_mask_result.recipe->layers[1].mask->value !=
          "radial-source" ||
      conflicting_mask_result.recipe->layers[1].scene.exposure_ev !=
          0.8 ||
      conflicting_mask_result.recipe->layers[1].tone.contrast !=
          -0.25) {
    if (!conflicting_mask_result.ok()) {
      for (const auto& error : conflicting_mask_result.errors) {
        std::cerr << error.path << ": " << error.message << '\n';
      }
    } else {
      std::cerr << "override layers="
                << conflicting_mask_result.recipe->layers.size();
      for (const auto& layer : conflicting_mask_result.recipe->layers) {
        std::cerr << " [" << layer.id.value << " mask="
                  << (layer.mask.has_value() ? layer.mask->value : "none")
                  << " exposure=" << layer.scene.exposure_ev
                  << " contrast=" << layer.tone.contrast << "]";
      }
      std::cerr << '\n';
    }
    return fail("per-source adjustment overrides should become explicit source-targeted v13 layers");
  }

  auto unsupported_curve = state;
  unsupported_curve.master_curve_points = {
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  };
  unsupported_curve.red_curve_points = {
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.6}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  };
  const auto curve_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          unsupported_curve,
          context());
  if (!curve_result.ok() ||
      curve_result.recipe->curves.master.size() != 2U ||
      curve_result.recipe->curves.red.size() != 3U ||
      curve_result.recipe->curves.master.front().x != 0.0 ||
      curve_result.recipe->curves.master.back().y != 1.0 ||
      curve_result.recipe->curves.red[1].y != 0.6) {
    return fail("desktop master and channel point curves should map exactly to v13");
  }
  auto malformed_curve = state;
  malformed_curve.master_curve_points = {
      QVariantMap{{QStringLiteral("x"), QStringLiteral("bad")}},
  };
  const auto malformed_curve_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          malformed_curve,
          context());
  if (malformed_curve_result.ok() ||
      !has_error_path(malformed_curve_result, "curves")) {
    return fail("malformed desktop point curves should fail closed");
  }
  auto parametric_curve = state;
  parametric_curve.curve_shadows = 20.0;
  parametric_curve.curve_highlights = -30.0;
  const auto parametric_curve_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          parametric_curve,
          context());
  if (!parametric_curve_result.ok() ||
      parametric_curve_result.recipe->curves.master.size() != 6U ||
      parametric_curve_result.recipe->curves.master[1].y != 0.268 ||
      parametric_curve_result.recipe->curves.master[4].y != 0.748) {
    return fail("desktop parametric curves should migrate to their exact canonical control points");
  }

  auto adjustment_layer_state = state;
  adjustment_layer_state.layers = {
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("adjustment-layer")},
          {QStringLiteral("kind"), QStringLiteral("adjustment")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("opacity"), 0.7},
          {QStringLiteral("blendMode"), QStringLiteral("luminosity")},
          {QStringLiteral("exposure"), 0.45},
          {QStringLiteral("contrast"), 20.0},
          {QStringLiteral("temperature"), -15.0},
          {QStringLiteral("texture"), 12.0},
          {QStringLiteral("adjustments"),
           QVariantMap{
               {QStringLiteral("brightness"), 18.0},
               {QStringLiteral("hueShift"), -22.0},
               {QStringLiteral("saturationShift"), 13.0},
               {QStringLiteral("colorizeHue"), 0.42},
               {QStringLiteral("colorizeStrength"), 31.0},
               {QStringLiteral("lensDistortion"), -18.0},
               {QStringLiteral("perspectiveVertical"), 12.0},
               {QStringLiteral("transformScale"), 1.3},
               {QStringLiteral("transformSkewX"), -7.0},
               {QStringLiteral("liquifyCenterX"), 0.35},
               {QStringLiteral("liquifyStrength"), 16.0},
               {QStringLiteral("contentAwareCropFill"), true},
               {QStringLiteral("colorMixer"),
                QVariantMap{
                    {QStringLiteral("model"), QStringLiteral("HSV")},
                    {QStringLiteral("channel"), QStringLiteral("Blues")},
                    {QStringLiteral("channels"),
                     QVariantMap{
                         {QStringLiteral("Blues"),
                          QVariantMap{
                              {QStringLiteral("hue"), 26.0},
                              {QStringLiteral("saturation"), -17.0},
                              {QStringLiteral("selectiveCyan"), 9.0},
                          }},
                     }},
                    {QStringLiteral("rgbChannelMixer"),
                     QVariantMap{
                         {QStringLiteral("redFromGreen"), 14.0},
                     }},
                    {QStringLiteral("blackAndWhite"),
                     QVariantMap{
                         {QStringLiteral("enabled"), true},
                         {QStringLiteral("channels"),
                          QVariantMap{
                              {QStringLiteral("Reds"), 19.0},
                          }},
                     }},
                    {QStringLiteral("colorHarmony"),
                     QVariantMap{
                         {QStringLiteral("enabled"), true},
                         {QStringLiteral("mode"), QStringLiteral("Triadic")},
                         {QStringLiteral("strength"), 27.0},
                     }},
                }},
               {QStringLiteral("colorGrading"),
                QVariantMap{
                    {QStringLiteral("shadows"),
                     QVariantMap{
                         {QStringLiteral("hue"), 0.7},
                         {QStringLiteral("saturation"), 24.0},
                     }},
                    {QStringLiteral("amount"), 81.0},
                    {QStringLiteral("balance"), -12.0},
                }},
           }},
          {QStringLiteral("masterCurvePoints"),
           QVariantList{
               QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
               QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
           }},
      },
  };
  const auto adjustment_layer_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          adjustment_layer_state,
          context());
  if (!adjustment_layer_result.ok() ||
      adjustment_layer_result.recipe->layers.size() != 1U ||
      adjustment_layer_result.recipe->layers.front().scene.exposure_ev !=
          0.45 ||
      adjustment_layer_result.recipe->layers.front().tone.contrast != 0.2 ||
      adjustment_layer_result.recipe->layers.front().tone.brightness != 0.18 ||
      adjustment_layer_result.recipe->layers.front().look.temperature !=
          -0.15 ||
      adjustment_layer_result.recipe->layers.front().look.hue_shift != -0.22 ||
      adjustment_layer_result.recipe->layers.front().look.saturation_shift !=
          0.13 ||
      adjustment_layer_result.recipe->layers.front().look.colorize_hue !=
          0.42 ||
      adjustment_layer_result.recipe->layers.front().look.colorize_strength !=
          0.31 ||
      adjustment_layer_result.recipe->layers.front().look.mixer_model !=
          photo_editor::pipeline::ColorMixerModelV13::Hsv ||
      adjustment_layer_result.recipe->layers.front()
              .look.mixer_channels[5]
              .hue != 0.26 ||
      adjustment_layer_result.recipe->layers.front()
              .look.mixer_channels[5]
              .selective_cyan != 0.09 ||
      adjustment_layer_result.recipe->layers.front()
              .look.rgb_channel_mixer.red_from_green != 0.14 ||
      !adjustment_layer_result.recipe->layers.front()
           .look.black_and_white_enabled ||
      adjustment_layer_result.recipe->layers.front()
              .look.black_and_white_mixer[0] != 0.19 ||
      !adjustment_layer_result.recipe->layers.front()
           .look.color_harmony_enabled ||
      adjustment_layer_result.recipe->layers.front().look.color_harmony_mode !=
          photo_editor::pipeline::ColorHarmonyModeV13::Triadic ||
      adjustment_layer_result.recipe->layers.front()
              .look.grading_shadows.saturation != 0.24 ||
      adjustment_layer_result.recipe->layers.front().look.grading_amount !=
          0.81 ||
      adjustment_layer_result.recipe->layers.front().detail.texture !=
          0.12 ||
      adjustment_layer_result.recipe->layers.front()
              .geometry.lens_distortion != -0.18 ||
      adjustment_layer_result.recipe->layers.front()
              .geometry.perspective_vertical != 0.12 ||
      adjustment_layer_result.recipe->layers.front().geometry.scale != 1.3 ||
      adjustment_layer_result.recipe->layers.front().geometry.skew_x !=
          -0.07 ||
      adjustment_layer_result.recipe->layers.front()
              .geometry.liquify_center_x != 0.35 ||
      adjustment_layer_result.recipe->layers.front()
              .geometry.liquify_strength != 0.16 ||
      !adjustment_layer_result.recipe->layers.front()
           .geometry.content_aware_crop_fill ||
      adjustment_layer_result.recipe->layers.front().curves.master.size() !=
          2U) {
    return fail(
        "representable desktop adjustment layers should reuse all v13 operator sections");
  }

  auto lut_layer_state = state;
  lut_layer_state.layers = {
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("lut-layer")},
          {QStringLiteral("kind"), QStringLiteral("adjustment")},
          {QStringLiteral("adjustments"),
           QVariantMap{
               {QStringLiteral("lutStack"),
                QVariantList{
                    QVariantMap{
                        {QStringLiteral("enabled"), true},
                        {QStringLiteral("strength"), 68.0},
                        {QStringLiteral("name"),
                         QStringLiteral("Canonical layer LUT")},
                        {QStringLiteral("table"),
                         QVariantList{
                             QVariantMap{
                                 {QStringLiteral("red"), 0.0},
                                 {QStringLiteral("green"), 0.0},
                                 {QStringLiteral("blue"), 0.0},
                                 {QStringLiteral("alpha"), 1.0},
                             },
                         }},
                        {QStringLiteral("domainMin"),
                         QVariantList{-0.1, -0.1, -0.1}},
                        {QStringLiteral("domainMax"),
                         QVariantList{1.2, 1.2, 1.2}},
                        {QStringLiteral("inputColorSpace"),
                         QStringLiteral("ACEScg")},
                        {QStringLiteral("outputColorSpace"),
                         QStringLiteral("ACEScg")},
                    },
                }},
           }},
      },
  };
  auto lut_context = context();
  lut_context.lut_payload_assets.push_back(
      photo_editor::desktop::DesktopRecipeV13Context::LutPayloadBinding{
          .layer_id = QStringLiteral("lut-layer"),
          .index = 0,
          .payload_asset =
              photo_editor::pipeline::AssetIdentifier{
                  "lut-payload-sha256-001"},
      });
  const auto lut_layer_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          lut_layer_state,
          lut_context);
  if (!lut_layer_result.ok() ||
      lut_layer_result.recipe->layers.front().look.luts.size() != 1U ||
      lut_layer_result.recipe->layers.front()
              .look.luts.front()
              .payload_asset.value != "lut-payload-sha256-001" ||
      lut_layer_result.recipe->layers.front().look.luts.front().strength !=
          0.68 ||
      lut_layer_result.recipe->layers.front()
              .look.luts.front()
              .domain_min[0] != -0.1 ||
      lut_layer_result.recipe->layers.front()
              .look.luts.front()
              .input_color_space != "ACEScg") {
    return fail(
        "layer LUT intent should map through an immutable v13 payload identity");
  }
  auto lut_plan_request = production_request;
  lut_plan_request.recipe_id = QStringLiteral("lut-recipe");
  const auto lut_plan =
      photo_editor::desktop::compile_desktop_render_plan_with_derived_assets(
          lut_layer_state,
          lut_plan_request);
  if (!lut_plan.ok()) {
    return fail(
        "production plan compilation should derive immutable identities for inline layer LUT payloads");
  }
  auto unsupported_lut_space_state = lut_layer_state;
  auto unsupported_layer = unsupported_lut_space_state.layers.front().toMap();
  auto unsupported_adjustments =
      unsupported_layer.value(QStringLiteral("adjustments")).toMap();
  auto unsupported_stack =
      unsupported_adjustments.value(QStringLiteral("lutStack")).toList();
  auto unsupported_lut = unsupported_stack.front().toMap();
  unsupported_lut.insert(
      QStringLiteral("inputColorSpace"),
      QStringLiteral("Unknown Camera Log"));
  unsupported_stack[0] = unsupported_lut;
  unsupported_adjustments.insert(
      QStringLiteral("lutStack"), unsupported_stack);
  unsupported_layer.insert(
      QStringLiteral("adjustments"), unsupported_adjustments);
  unsupported_lut_space_state.layers[0] = unsupported_layer;
  const auto unsupported_lut_plan =
      photo_editor::desktop::compile_desktop_render_plan_with_derived_assets(
          unsupported_lut_space_state,
          lut_plan_request);
  if (unsupported_lut_plan.ok() ||
      unsupported_lut_plan.plan.has_value() ||
      !std::any_of(
          unsupported_lut_plan.compiler_errors.begin(),
          unsupported_lut_plan.compiler_errors.end(),
          [](const auto& error) {
            return error.code ==
                       photo_editor::pipeline::RecipeCompileErrorCode::
                           UnsupportedColorSpace &&
                   error.path ==
                       "layers[0].look.luts[0].input_color_space";
          })) {
    return fail(
        "desktop plan admission must expose structured unsupported layer LUT color-space errors");
  }

  auto pixel_layer_state = state;
  pixel_layer_state.layers = {
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("pixel-layer")},
          {QStringLiteral("kind"), QStringLiteral("pixel")},
          {QStringLiteral("enabled"), true},
      },
  };
  const auto unbound_pixel_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          pixel_layer_state,
          context());
  if (unbound_pixel_result.ok() ||
      !has_error_path(
          unbound_pixel_result,
          "layers[0].content_asset")) {
    return fail("pixel layers must not migrate without immutable content identity");
  }
  auto pixel_context = context();
  pixel_context.layer_content_assets.push_back(
      photo_editor::desktop::DesktopRecipeV13Context::LayerContentBinding{
          .layer_id = QStringLiteral("pixel-layer"),
          .content_asset =
              photo_editor::pipeline::AssetIdentifier{
                  "pixel-layer-sha256-001"},
      });
  const auto bound_pixel_result =
      photo_editor::desktop::adapt_desktop_recipe_to_v13(
          pixel_layer_state,
          pixel_context);
  if (!bound_pixel_result.ok() ||
      bound_pixel_result.recipe->layers.front().content_asset->value !=
          "pixel-layer-sha256-001") {
    return fail("pixel layers should migrate through explicit immutable content bindings");
  }

  {
    photo_editor::pipeline::PhotoEffectInstance effect;
    effect.id = "layer-blur";
    const std::vector effects{effect};
    auto effect_layer = pixel_layer_state.layers[0].toMap();
    effect_layer.insert(QStringLiteral("effectStack"), *photo_editor::desktop::encode_photo_layer_effects(effects));
    pixel_layer_state.layers[0] = effect_layer;
    const auto adapted = photo_editor::desktop::adapt_desktop_recipe_to_v13(pixel_layer_state, pixel_context);
    if (!adapted.ok() || adapted.recipe->layers.front().effects != effects)
      return fail("desktop adapter must retain typed layer effects in the canonical recipe");

    auto canonical_export_request = production_request;
    canonical_export_request.purpose = photo_editor::pipeline::RenderPurpose::Export;
    canonical_export_request.scope_domain = photo_editor::pipeline::RenderScopeDomain::FinalDisplay;
    for (const auto* type : {"film_grain", "light_leak", "split_tone", "vignette", "duotone", "soft_fade"}) {
      auto typed_effect = photo_editor::pipeline::PhotoEffectInstance{};
      typed_effect.id = std::string("canonical-") + type;
      typed_effect.type_id = type;
      auto typed_layer = pixel_layer_state.layers[0].toMap();
      const std::vector typed_effects{typed_effect};
      const auto typed_stack = photo_editor::desktop::encode_photo_layer_effects(
          typed_effects);
      if (!typed_stack) return fail("new photographic operator should encode before canonical export admission");
      typed_layer.insert(QStringLiteral("effectStack"), *typed_stack);
      auto typed_state = pixel_layer_state;
      typed_state.layers[0] = typed_layer;
      const auto typed_plan = photo_editor::desktop::compile_desktop_render_plan_with_derived_assets(
          typed_state, canonical_export_request);
      if (!typed_plan.ok())
        return fail(std::string("new photographic operator should pass canonical export admission: ") + type);
    }

    effect_layer.insert(QStringLiteral("effectStack"), QStringLiteral("unrecognized future envelope"));
    pixel_layer_state.layers[0] = effect_layer;
    const auto rejected = photo_editor::desktop::adapt_desktop_recipe_to_v13(pixel_layer_state, pixel_context);
    if (rejected.ok() || !has_error_path(rejected, "layers[0].effectStack"))
      return fail("malformed desktop effect stack must not be omitted by adaptation");
  }
  return EXIT_SUCCESS;
}

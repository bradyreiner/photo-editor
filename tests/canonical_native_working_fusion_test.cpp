#include "canonical_native_working_fusion.h"
#include "preview_gpu_fallback_policy.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"
#include "photo_editor/pipeline/color_geometry_program.h"
#include "photo_editor/pipeline/color_hdr_local_tone_program.h"
#include "photo_editor/pipeline/color_spatial_detail_program.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool pixels_equal(
    const std::vector<photo_editor::pipeline::ColorPixel>& left,
    const std::vector<photo_editor::pipeline::ColorPixel>& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (auto index = std::size_t{0}; index < left.size(); ++index) {
    if (left[index].red != right[index].red ||
        left[index].green != right[index].green ||
        left[index].blue != right[index].blue ||
        left[index].alpha != right[index].alpha) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  using namespace photo_editor::pipeline;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  {
    photo_editor::desktop::EditRecipeState recipe;
    ColorAdjustmentState raw;
    raw.color_pipeline = default_color_pipeline_config();
    raw.input_transform.raw_input_transform_enabled = true;
    raw.color_pipeline.camera_profile.valid = true;
    const std::vector<ColorPixel> decoded{{0.15F, 0.25F, 0.35F, 1.0F}};
    const auto reason = photo_editor::desktop::preview_gpu_fallback_reason(
        recipe, raw, decoded, true, {});
    if (reason != "unsupported_stage:CameraCharacterization:camera_linear_lut_fidelity")
      return fail("Uncharacterized RAW preview must not sample camera characterization into the compatibility LUT");
    if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
            recipe, raw, decoded, true, {}).isEmpty())
      return fail("An already characterized RAW surface must retain canonical Metal eligibility");
    raw.input_transform.raw_input_transform_enabled = false;
    raw.color_pipeline.camera_profile.valid = false;
    if (!photo_editor::desktop::preview_gpu_fallback_reason(recipe, raw, decoded, true, {}).isEmpty())
      return fail("The camera fidelity gate must not disable ordinary raster previews");
  }
#endif
  {
    auto tone_state = ColorAdjustmentState{};
    tone_state.color_pipeline = default_color_pipeline_config();
    tone_state.contrast = 31.0;
    tone_state.highlights = -38.0;
    tone_state.shadows = 42.0;
    tone_state.whites = 19.0;
    tone_state.blacks = -14.0;
    tone_state.brightness = 11.0;
    tone_state.filmic_toe = 17.0;
    tone_state.adaptive_contrast = 22.0;
    tone_state.levels_black_point = 3.0;
    tone_state.levels_gamma = -8.0;
    tone_state.levels_white_point = 6.0;
    tone_state.zone_deep_shadows = 13.0;
    tone_state.zone_shadows = -9.0;
    tone_state.zone_midtones = 7.0;
    tone_state.zone_lights = -12.0;
    tone_state.zone_highlights = 16.0;

    auto source_ramp = std::vector<ColorPixel>{};
    source_ramp.reserve(769U);
    source_ramp.push_back(
        {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.25F});
    for (auto index = 0; index < 768; ++index) {
      const auto stops =
          -30.0 + 42.0 * static_cast<double>(index) / 767.0;
      const auto value = static_cast<float>(0.18 * std::exp2(stops));
      const auto mode = index % 4;
      source_ramp.push_back(ColorPixel{
          .red = value * (mode == 1 ? 1.35F : mode == 2 ? 0.62F : 1.0F),
          .green = value * (mode == 2 ? 1.28F : mode == 3 ? 0.73F : 1.0F),
          .blue = value * (mode == 3 ? 1.42F : mode == 1 ? 0.71F : 1.0F),
          .alpha = static_cast<float>((index % 9) + 1) / 9.0F,
      });
    }
    auto exact = source_ramp;
    auto tone_telemetry = ColorAdjustmentTelemetry{};
    if (!apply_color_tone_stage(
            &exact,
            static_cast<int>(exact.size()),
            1,
            tone_state,
            {},
            &tone_telemetry)) {
      return fail("canonical tone reference failed");
    }
    const auto compiled =
        compile_tone_stage_lut(tone_state, &tone_telemetry);
    const auto compiled_again = compile_tone_stage_lut(tone_state, nullptr);
    if (!compiled.ready() ||
        compiled.cache_identity != compiled_again.cache_identity ||
        tone_telemetry.tone_table_cache_hit_count < 1) {
      std::cerr << "compiled_ready=" << compiled.ready()
                << " identity_match="
                << (compiled.cache_identity == compiled_again.cache_identity)
                << " cache_hits=" << tone_telemetry.tone_table_cache_hit_count
                << " cache_misses=" << tone_telemetry.tone_table_cache_miss_count
                << '\n';
      return fail("compiled tone stage must be valid, stable, and cache-backed");
    }
    auto maximum_error = 0.0F;
    for (auto index = std::size_t{0}; index < source_ramp.size(); ++index) {
      const auto actual =
          apply_compiled_tone_stage_lut(compiled, source_ramp[index]);
      maximum_error = std::max({
          maximum_error,
          std::abs(actual.red - exact[index].red),
          std::abs(actual.green - exact[index].green),
          std::abs(actual.blue - exact[index].blue),
      });
      if (actual.alpha != exact[index].alpha) {
        return fail("compiled tone stage must preserve alpha exactly");
      }
    }
    if (maximum_error > 0.004F) {
      std::cerr << "compiled tone maximum linear error: " << maximum_error
                << '\n';
      return fail("compiled float tone stage exceeds CPU parity tolerance");
    }

    auto previous = -1.0F;
    for (auto index = 0; index < 1024; ++index) {
      const auto stops =
          -30.0 + 42.0 * static_cast<double>(index) / 1023.0;
      const auto value = static_cast<float>(0.18 * std::exp2(stops));
      const auto output = apply_compiled_tone_stage_lut(
          compiled,
          {.red = value, .green = value, .blue = value, .alpha = 1.0F});
      if (output.red + 1.0e-6F < previous ||
          output.red != output.green || output.red != output.blue) {
        return fail("compiled tone ramp must remain neutral and monotone");
      }
      previous = output.red;
    }
  }
  {
    auto curve_state = ColorAdjustmentState{};
    curve_state.color_pipeline = default_color_pipeline_config();
    curve_state.curve_shadows = 23.0;
    curve_state.curve_darks = -17.0;
    curve_state.curve_lights = 14.0;
    curve_state.curve_highlights = -11.0;
    curve_state.master_curve_points = {
        {0.0, 0.0}, {0.22, 0.16}, {0.63, 0.76}, {1.0, 1.0}};
    curve_state.luma_curve_points = {
        {0.0, 0.0}, {0.31, 0.38}, {0.74, 0.69}, {1.0, 1.0}};
    curve_state.red_curve_points = {
        {0.0, 0.0}, {0.35, 0.42}, {1.0, 1.0}};
    curve_state.green_curve_points = {
        {0.0, 0.0}, {0.48, 0.43}, {1.0, 1.0}};
    curve_state.blue_curve_points = {
        {0.0, 0.0}, {0.58, 0.66}, {1.0, 1.0}};
    auto curve_source = std::vector<ColorPixel>{};
    curve_source.reserve(1025U);
    curve_source.push_back(
        {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.4F});
    for (auto index = 0; index < 1024; ++index) {
      const auto value = 4.0F * static_cast<float>(index) / 1023.0F;
      curve_source.push_back({
          .red = value * (index % 3 == 0 ? 0.61F : 1.0F),
          .green = value * (index % 3 == 1 ? 0.73F : 1.0F),
          .blue = value * (index % 3 == 2 ? 0.82F : 1.0F),
          .alpha = static_cast<float>((index % 7) + 1) / 7.0F,
      });
    }
    auto exact_curves = curve_source;
    if (!apply_color_curves_stage(
            &exact_curves,
            static_cast<int>(exact_curves.size()),
            1,
            curve_state)) {
      return fail("canonical curves reference failed");
    }
    const auto compiled_curves = compile_curve_stage_luts(curve_state);
    const auto compiled_curves_again = compile_curve_stage_luts(curve_state);
    if (!compiled_curves.ready() ||
        compiled_curves.cache_identity != compiled_curves_again.cache_identity) {
      return fail("compiled curve stage must be complete and stable");
    }
    auto maximum_curve_error = 0.0F;
    for (auto index = std::size_t{0}; index < curve_source.size(); ++index) {
      const auto actual =
          apply_compiled_curve_stage_luts(compiled_curves, curve_source[index]);
      maximum_curve_error = std::max({
          maximum_curve_error,
          std::abs(actual.red - exact_curves[index].red),
          std::abs(actual.green - exact_curves[index].green),
          std::abs(actual.blue - exact_curves[index].blue),
      });
      if (actual.alpha != exact_curves[index].alpha) {
        return fail("compiled curve stage must preserve alpha exactly");
      }
    }
    if (maximum_curve_error > 0.004F) {
      std::cerr << "compiled curve maximum linear error: "
                << maximum_curve_error << '\n';
      return fail("compiled curve stage exceeds CPU parity tolerance");
    }
  }
  {
    auto look_source = std::vector<ColorPixel>{};
    look_source.reserve(1025U);
    look_source.push_back(
        {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.3F});
    for (auto index = 0; index < 1024; ++index) {
      const auto value = 32.0F * static_cast<float>(index) / 1023.0F;
      look_source.push_back({
          .red = value * (index % 3 == 0 ? 0.37F : 1.0F),
          .green = value * (index % 3 == 1 ? 0.59F : 1.0F),
          .blue = value * (index % 3 == 2 ? 0.76F : 1.0F),
          .alpha = static_cast<float>((index % 11) + 1) / 11.0F,
      });
    }
    auto color_look_state = ColorAdjustmentState{};
    color_look_state.color_pipeline = default_color_pipeline_config();
    color_look_state.saturation = 34.0;
    color_look_state.vibrance = 46.0;
    color_look_state.rgb_channel_mixer = {
        .red_from_red = 91.0,
        .red_from_green = 13.0,
        .red_from_blue = -4.0,
        .green_from_red = 5.0,
        .green_from_green = 89.0,
        .green_from_blue = 6.0,
        .blue_from_red = -3.0,
        .blue_from_green = 11.0,
        .blue_from_blue = 92.0,
    };
    auto black_and_white_state = ColorAdjustmentState{};
    black_and_white_state.color_pipeline = default_color_pipeline_config();
    black_and_white_state.black_and_white_enabled = true;
    black_and_white_state.black_and_white_mixer = {
        18.0, -12.0, 24.0, -8.0, 11.0, -19.0, 7.0, 14.0};
    auto desaturate_state = ColorAdjustmentState{};
    desaturate_state.color_pipeline = default_color_pipeline_config();
    desaturate_state.saturation = -100.0;
    auto grading_state = ColorAdjustmentState{};
    grading_state.color_pipeline = default_color_pipeline_config();
    grading_state.saturation = 17.0;
    grading_state.vibrance = 29.0;
    grading_state.grading_shadows_hue = 0.91;
    grading_state.grading_shadows_saturation = 24.0;
    grading_state.grading_shadows_luminance = -11.0;
    grading_state.grading_midtones_hue = 0.12;
    grading_state.grading_midtones_saturation = -18.0;
    grading_state.grading_midtones_luminance = 8.0;
    grading_state.grading_highlights_hue = 0.57;
    grading_state.grading_highlights_saturation = 31.0;
    grading_state.grading_highlights_luminance = 13.0;
    grading_state.grading_amount = 73.0;
    grading_state.grading_balance = -26.0;
    auto mixer_state = ColorAdjustmentState{};
    mixer_state.color_pipeline = default_color_pipeline_config();
    mixer_state.color_mixer_model = "HSL";
    mixer_state.color_mixer_channels[0] = {
        .hue = 17.0, .saturation = -22.0, .luminance = 11.0,
        .selective_cyan = 14.0, .selective_magenta = -9.0,
        .selective_yellow = 18.0, .selective_black = -7.0};
    mixer_state.color_mixer_channels[3] = {
        .hue = -13.0, .saturation = 19.0, .luminance = -8.0,
        .selective_cyan = -11.0, .selective_magenta = 16.0,
        .selective_yellow = -6.0, .selective_black = 9.0};
    auto hsv_mixer_state = mixer_state;
    hsv_mixer_state.color_mixer_model = "HSV";
    auto oklab_mixer_state = mixer_state;
    oklab_mixer_state.color_mixer_model = "OKLab";
    auto legacy_mixer_state = ColorAdjustmentState{};
    legacy_mixer_state.color_pipeline = default_color_pipeline_config();
    legacy_mixer_state.color_mixer_model = "LCH";
    legacy_mixer_state.color_mixer_channel = "Blues";
    legacy_mixer_state.hue_adjustment = 21.0;
    legacy_mixer_state.saturation_adjustment = 27.0;
    legacy_mixer_state.luminance_adjustment = -12.0;
    auto harmony_state = ColorAdjustmentState{};
    harmony_state.color_pipeline = default_color_pipeline_config();
    harmony_state.color_harmony_enabled = true;
    harmony_state.color_harmony_base_hue = 0.17;
    harmony_state.color_harmony_strength = 68.0;
    harmony_state.color_harmony_mode = "Analogous";
    auto triadic_harmony_state = harmony_state;
    triadic_harmony_state.color_harmony_mode = "Triadic";
    auto split_harmony_state = harmony_state;
    split_harmony_state.color_harmony_mode = "Split Complementary";
    auto tetradic_harmony_state = harmony_state;
    tetradic_harmony_state.color_harmony_mode = "Tetradic";
    auto replacement_state = ColorAdjustmentState{};
    replacement_state.color_pipeline = default_color_pipeline_config();
    replacement_state.color_replacement_enabled = true;
    replacement_state.color_replacement_source_hue = 0.08;
    replacement_state.color_replacement_target_hue = 0.61;
    replacement_state.color_replacement_range = 82.0;
    replacement_state.color_replacement_softness = 57.0;
    replacement_state.color_replacement_strength = 74.0;
    auto replacement_chroma_state = replacement_state;
    replacement_chroma_state.color_replacement_preserve_luminance = false;
    replacement_chroma_state.color_replacement_preserve_saturation = false;
    auto protected_state = mixer_state;
    protected_state.saturation = 18.0;
    protected_state.vibrance = 27.0;
    protected_state.grading_shadows_hue = 0.93;
    protected_state.grading_shadows_saturation = 21.0;
    protected_state.grading_midtones_luminance = 7.0;
    protected_state.color_harmony_enabled = true;
    protected_state.color_harmony_mode = "Triadic";
    protected_state.color_harmony_base_hue = 0.16;
    protected_state.color_harmony_strength = 52.0;
    protected_state.color_replacement_enabled = true;
    protected_state.color_replacement_source_hue = 0.08;
    protected_state.color_replacement_target_hue = 0.58;
    protected_state.color_replacement_range = 76.0;
    protected_state.color_replacement_softness = 64.0;
    protected_state.color_replacement_strength = 69.0;
    protected_state.skin_tone_protection_enabled = true;
    protected_state.skin_tone_line_hue = 0.08;
    protected_state.skin_tone_protection_range = 38.0;
    protected_state.skin_tone_protection_strength = 81.0;
    auto look_case = 0;
    for (const auto* look_state : {
             &color_look_state, &black_and_white_state, &desaturate_state,
             &grading_state, &mixer_state, &hsv_mixer_state,
             &oklab_mixer_state, &legacy_mixer_state, &harmony_state,
             &triadic_harmony_state, &split_harmony_state,
             &tetradic_harmony_state, &replacement_state,
             &replacement_chroma_state, &protected_state}) {
      auto exact_look = look_source;
      if (!apply_color_look_stage(
              &exact_look,
              static_cast<int>(exact_look.size()),
              1,
              *look_state)) {
        return fail("canonical common Look reference failed");
      }
      const auto compiled_look = compile_look_stage_program(*look_state);
      const auto compiled_look_again = compile_look_stage_program(*look_state);
      if (!compiled_look.ready() ||
          compiled_look.cache_identity != compiled_look_again.cache_identity) {
        return fail("compiled common Look program must be complete and stable");
      }
      auto maximum_look_error = 0.0F;
      for (auto index = std::size_t{0}; index < look_source.size(); ++index) {
        const auto actual = apply_compiled_look_stage_program(
            compiled_look, look_source[index]);
        maximum_look_error = std::max({
            maximum_look_error,
            std::abs(actual.red - exact_look[index].red),
            std::abs(actual.green - exact_look[index].green),
            std::abs(actual.blue - exact_look[index].blue),
        });
        if (actual.alpha != exact_look[index].alpha) {
          return fail("compiled common Look program must preserve alpha exactly");
        }
      }
      if (maximum_look_error > 0.004F) {
        std::cerr << "compiled Look case " << look_case
                  << " maximum linear error: "
                  << maximum_look_error << '\n';
        return fail("compiled common Look program exceeds CPU parity tolerance");
      }
      ++look_case;
    }
  }
  auto request = RenderExecutionRequest{
      .plan = RenderPlan{
          .recipe_cache_key = "recipe:working-fusion",
          .source = AssetIdentifier{"asset:working-fusion"},
          .stages = {
              {.kind = RenderStageKind::Decode, .cache_identity = "decode"},
              {.kind = RenderStageKind::CameraCharacterization,
               .cache_identity = "camera"},
              {.kind = RenderStageKind::SceneAdjustments,
               .cache_identity = "scene"},
              {.kind = RenderStageKind::Tone, .cache_identity = "tone"},
              {.kind = RenderStageKind::Look, .cache_identity = "look"},
              {.kind = RenderStageKind::Layers, .cache_identity = "layers"},
              {.kind = RenderStageKind::Detail, .cache_identity = "detail"},
              {.kind = RenderStageKind::Geometry, .cache_identity = "geometry"},
              {.kind = RenderStageKind::PhotographicOutput,
               .cache_identity = "output"},
              {.kind = RenderStageKind::Publish, .cache_identity = "publish"},
          },
          .plan_cache_key = "plan:working-fusion",
      },
      .backend_version = "test",
      .target_identity = "target:test",
      .profile_identity = "profile:none",
      .source_color_space_identity = "camera:test",
      .working_color_space_identity = "Linear sRGB",
      .rendering_target_color_space_identity = "Linear sRGB",
      .device_color_space_identity = "sRGB",
      .device_transfer_identity = "srgb",
      .output_width = 2,
      .output_height = 1,
  };
  auto state = ColorAdjustmentState{};
  state.color_pipeline = default_color_pipeline_config();
  state.input_transform.exposure = 0.75;
  state.input_transform.temperature = 12.0;
  state.input_transform.tint = -8.0;
  auto policy_state = photo_editor::desktop::EditRecipeState{};
  policy_state.exposure = 0.75;
  const auto source = std::vector<ColorPixel>{
      {.red = 0.08F, .green = 0.18F, .blue = 0.42F, .alpha = 1.0F},
      {.red = 0.72F, .green = 0.43F, .blue = 0.21F, .alpha = 0.6F},
  };
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty()) {
    return fail("exact scene exposure must be admitted by working fusion");
  }
  const auto extended_working_source = std::vector<ColorPixel>{
      {.red = 1.42F, .green = 0.68F, .blue = -0.04F, .alpha = 1.0F},
      {.red = 0.16F, .green = 1.18F, .blue = 0.39F, .alpha = 0.7F},
  };
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, extended_working_source, true, {}).isEmpty()) {
    return fail(
        "characterized extended-range RAW working pixels must reach retained fusion");
  }
  const auto extended_tone_lut = compile_tone_stage_lut(state);
  if (!photo_editor::desktop::canonical_compiled_tone_supports_source(
          extended_tone_lut,
          extended_working_source,
          std::exp2(effective_input_exposure(state)))) {
    return fail("compiled Tone domain must admit finite extended working pixels");
  }
  auto outside_tone_domain = extended_working_source;
  outside_tone_domain.front().red = 1.0e9F;
  if (photo_editor::desktop::canonical_compiled_tone_supports_source(
          extended_tone_lut,
          outside_tone_domain,
          std::exp2(effective_input_exposure(state)))) {
    return fail("compiled Tone must fail closed outside its sampled stop domain");
  }
  policy_state.lens_vignette = 36.0;
  state.lens_vignette = 36.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty()) {
    return fail(
        "camera-characterization lens vignette must not block downstream fusion");
  }
  policy_state.lens_vignette = 0.0;
  state.lens_vignette = 0.0;
  policy_state.perspective_vertical = 9.0;
  policy_state.perspective_horizontal = -7.0;
  policy_state.transform_rotation = 2.0;
  policy_state.transform_scale = 1.08;
  policy_state.transform_skew_x = 4.0;
  policy_state.transform_skew_y = -3.0;
  policy_state.liquify_strength = 12.0;
  policy_state.lens_distortion = 14.0;
  policy_state.chromatic_aberration = 18.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty()) {
    return fail("exact retained Geometry controls must be admitted");
  }
  policy_state.perspective_vertical = 0.0;
  policy_state.perspective_horizontal = 0.0;
  policy_state.transform_rotation = 0.0;
  policy_state.transform_scale = 1.0;
  policy_state.transform_skew_x = 0.0;
  policy_state.transform_skew_y = 0.0;
  policy_state.liquify_strength = 0.0;
  policy_state.lens_distortion = 0.0;
  policy_state.chromatic_aberration = 0.0;
  policy_state.texture = 23.0;
  policy_state.clarity = -17.0;
  policy_state.dehaze = 29.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty()) {
    return fail("exact retained spatial Detail controls must be admitted");
  }
  auto spatial_detail_state = ColorAdjustmentState{};
  spatial_detail_state.color_pipeline = default_color_pipeline_config();
  spatial_detail_state.texture = policy_state.texture;
  spatial_detail_state.clarity = policy_state.clarity;
  spatial_detail_state.dehaze = policy_state.dehaze;
  const auto spatial_detail_program =
      compile_interactive_spatial_detail_program(
          spatial_detail_state, 9, 7);
  if (!spatial_detail_program.ready() ||
      spatial_detail_program.active_mask !=
          (CompiledSpatialDetailProgram::kTexture |
           CompiledSpatialDetailProgram::kClarity |
           CompiledSpatialDetailProgram::kDehaze) ||
      spatial_detail_program.luminance_coefficients[0] <= 0.0F ||
      spatial_detail_program.luminance_coefficients[1] <= 0.0F ||
      spatial_detail_program.luminance_coefficients[2] <= 0.0F) {
    return fail("retained spatial Detail program must compile exact context");
  }
  auto spatial_source = std::vector<ColorPixel>{};
  spatial_source.reserve(13U * 11U);
  for (auto y = 0; y < 11; ++y) {
    for (auto x = 0; x < 13; ++x) {
      const auto checker = ((x + y) % 3) == 0 ? 0.11F : 0.0F;
      spatial_source.push_back({
          .red = 0.01F + static_cast<float>(x) / 16.0F + checker,
          .green = 0.02F + static_cast<float>(y) / 14.0F,
          .blue = 0.03F + static_cast<float>(x + y) / 28.0F,
          .alpha = 0.25F + static_cast<float>(x) / 16.0F,
      });
    }
  }
  auto canonical_spatial = spatial_source;
  auto compiled_spatial = spatial_source;
  const auto spatial_pixel_program =
      compile_interactive_spatial_detail_program(
          spatial_detail_state, 13, 11);
  if (!apply_color_detail_stage(
          &canonical_spatial, 13, 11, spatial_detail_state,
          RemovalQuality::Interactive) ||
      !apply_compiled_interactive_spatial_detail_program(
          &compiled_spatial, spatial_pixel_program)) {
    return fail("retained spatial Detail reference must execute");
  }
  for (auto index = std::size_t{0U}; index < canonical_spatial.size();
       ++index) {
    const auto& expected = canonical_spatial[index];
    const auto& actual = compiled_spatial[index];
    if (std::max({
            std::abs(expected.red - actual.red),
            std::abs(expected.green - actual.green),
            std::abs(expected.blue - actual.blue),
            std::abs(expected.alpha - actual.alpha),
        }) > 5.0e-5F) {
      return fail("retained spatial Detail exceeds CPU parity tolerance");
    }
  }
  spatial_detail_state.dehaze =
      std::numeric_limits<double>::quiet_NaN();
  if (compile_interactive_spatial_detail_program(
          spatial_detail_state, 9, 7).ready()) {
    return fail("non-finite retained spatial Detail must fail closed");
  }
  policy_state.texture = 0.0;
  policy_state.clarity = 0.0;
  policy_state.dehaze = 0.0;
  policy_state.sharpening = 42.0;
  policy_state.sharpen_radius = 2.3;
  policy_state.sharpen_detail = 37.0;
  policy_state.sharpen_masking = 54.0;
  policy_state.luminance_nr = 36.0;
  policy_state.color_nr = 41.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty()) {
    return fail("exact retained fine Detail controls must be admitted");
  }
  auto fine_detail_state = ColorAdjustmentState{};
  fine_detail_state.color_pipeline = default_color_pipeline_config();
  fine_detail_state.sharpening = policy_state.sharpening;
  fine_detail_state.sharpen_radius = policy_state.sharpen_radius;
  fine_detail_state.sharpen_detail = policy_state.sharpen_detail;
  fine_detail_state.sharpen_masking = policy_state.sharpen_masking;
  fine_detail_state.luminance_nr = policy_state.luminance_nr;
  fine_detail_state.color_nr = policy_state.color_nr;
  const auto fine_detail_program = compile_fine_detail_program(
      fine_detail_state, 13, 11);
  if (!fine_detail_program.ready() ||
      fine_detail_program.active_mask !=
          (CompiledFineDetailProgram::kSharpen |
           CompiledFineDetailProgram::kLuminanceNoiseReduction |
           CompiledFineDetailProgram::kColorNoiseReduction) ||
      fine_detail_program.sharpen_radius != 2U ||
      fine_detail_program.sharpen_edge_radius != 7U) {
    return fail("retained fine Detail program must compile exact controls");
  }
  auto canonical_fine_detail = spatial_source;
  auto compiled_fine_detail = spatial_source;
  if (!apply_color_detail_stage(
          &canonical_fine_detail, 13, 11, fine_detail_state,
          RemovalQuality::Interactive) ||
      !apply_compiled_fine_detail_program(
          &compiled_fine_detail, fine_detail_program)) {
    return fail("retained fine Detail reference must execute");
  }
  for (auto index = std::size_t{0U}; index < canonical_fine_detail.size();
       ++index) {
    const auto& expected = canonical_fine_detail[index];
    const auto& actual = compiled_fine_detail[index];
    if (std::max({
            std::abs(expected.red - actual.red),
            std::abs(expected.green - actual.green),
            std::abs(expected.blue - actual.blue),
            std::abs(expected.alpha - actual.alpha),
        }) > 5.0e-4F) {
      return fail("retained fine Detail exceeds CPU parity tolerance");
    }
  }
  fine_detail_state.sharpen_radius =
      std::numeric_limits<double>::infinity();
  if (compile_fine_detail_program(fine_detail_state, 13, 11).ready()) {
    return fail("non-finite retained fine Detail must fail closed");
  }
  policy_state.sharpening = 0.0;
  policy_state.sharpen_radius = 1.0;
  policy_state.sharpen_detail = 0.0;
  policy_state.sharpen_masking = 0.0;
  policy_state.luminance_nr = 0.0;
  policy_state.color_nr = 0.0;
  policy_state.hdr_local_tone_mapping = 64.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty()) {
    return fail("exact retained HDR local tone mapping must be admitted");
  }
  auto hdr_state = ColorAdjustmentState{};
  hdr_state.color_pipeline = default_color_pipeline_config();
  hdr_state.contrast = 22.0;
  hdr_state.highlights = -18.0;
  hdr_state.hdr_local_tone_mapping = policy_state.hdr_local_tone_mapping;
  const auto hdr_program = compile_hdr_local_tone_program(hdr_state, 13, 11);
  const auto hdr_tone_lut = compile_tone_stage_lut(hdr_state);
  if (!hdr_program.ready() || !hdr_program.active() || hdr_program.radius != 1U ||
      !hdr_tone_lut.ready()) {
    return fail("retained HDR local-tone program must compile exact controls");
  }
  auto hdr_source = spatial_source;
  for (auto& pixel : hdr_source) {
    pixel.red *= 3.2F;
    pixel.green *= 2.7F;
    pixel.blue *= 3.8F;
  }
  auto canonical_hdr = hdr_source;
  auto compiled_hdr = hdr_source;
  if (!apply_color_tone_stage(&canonical_hdr, 13, 11, hdr_state)) {
    return fail("canonical HDR local tone stage must execute");
  }
  for (auto& pixel : compiled_hdr) {
    pixel = apply_compiled_tone_stage_lut(hdr_tone_lut, pixel);
  }
  if (!apply_compiled_hdr_local_tone_program(&compiled_hdr, hdr_program)) {
    return fail("retained HDR local-tone reference must execute");
  }
  for (auto index = std::size_t{0U}; index < canonical_hdr.size(); ++index) {
    const auto& expected = canonical_hdr[index];
    const auto& actual = compiled_hdr[index];
    if (std::max({
            std::abs(expected.red - actual.red),
            std::abs(expected.green - actual.green),
            std::abs(expected.blue - actual.blue),
            std::abs(expected.alpha - actual.alpha),
        }) > 1.0e-3F) {
      return fail("retained HDR local tone exceeds CPU parity tolerance");
    }
  }
  hdr_state.hdr_local_tone_mapping =
      std::numeric_limits<double>::quiet_NaN();
  if (compile_hdr_local_tone_program(hdr_state, 13, 11).ready()) {
    return fail("non-finite retained HDR local tone must fail closed");
  }
  policy_state.hdr_local_tone_mapping = 0.0;
  auto fill_state = ColorAdjustmentState{};
  fill_state.color_pipeline = default_color_pipeline_config();
  fill_state.photo_layers = {
      PhotoLayer{
          .kind = "fill",
          .blend_mode = "normal",
          .opacity = 0.43,
          .fill_color = {0.72F, 0.18F, 0.31F, 0.65F},
          .mask_data = {0U, 64U, 255U, 255U, 128U, 32U},
          .mask_width = 3,
          .mask_height = 2,
          .mask_inverted = true,
          .mask_opacity = 0.81,
          .mask_density = 0.73,
      },
      PhotoLayer{
          .kind = "fill",
          .blend_mode = "screen",
          .opacity = 0.37,
          .clipping_mask = true,
          .fill_color = {0.14F, 0.66F, 0.28F, 0.52F},
      },
      PhotoLayer{
          .kind = "fill",
          .blend_mode = "soft-light",
          .opacity = 0.29,
          .fill_color = {0.81F, 0.23F, 0.57F, 0.48F},
      },
      PhotoLayer{
          .kind = "adjustment",
          .blend_mode = "normal",
          .opacity = 0.41,
          .clipping_mask = true,
          .exposure = 0.72,
          .contrast = 18.0,
          .highlights = -12.0,
          .shadows = 22.0,
          .whites = 7.0,
          .blacks = -5.0,
          .temperature = 14.0,
          .tint = -9.0,
          .texture = 19.0,
          .clarity = -13.0,
          .dehaze = -27.0,
          .sharpening = 24.0,
          .sharpen_radius = 1.7,
          .sharpen_detail = 33.0,
          .sharpen_masking = 41.0,
          .luminance_nr = 17.0,
          .color_nr = 21.0,
          .lens_distortion = 12.0,
          .lens_vignette = 38.0,
          .chromatic_aberration = 9.0,
          .perspective_vertical = 7.0,
          .perspective_horizontal = -5.0,
          .global_transform_rotation = 3.0,
          .global_transform_scale = 1.04,
          .transform_skew_x = 4.0,
          .transform_skew_y = -3.0,
          .liquify_center_x = 0.38,
          .liquify_center_y = 0.61,
          .liquify_radius = 0.36,
          .liquify_strength = 8.0,
          .content_aware_crop_fill = true,
          .vibrance = 21.0,
          .saturation = -8.0,
          .hue_shift = 13.0,
          .saturation_shift = -17.0,
          .colorize_hue = 0.63,
          .colorize_strength = 28.0,
          .brightness = 6.0,
          .filmic_shoulder = 17.0,
          .filmic_toe = 11.0,
          .adaptive_contrast = 9.0,
          .levels_black_point = 2.0,
          .levels_gamma = 4.0,
          .levels_white_point = -3.0,
          .zone_deep_shadows = 8.0,
          .zone_shadows = -6.0,
          .zone_midtones = 5.0,
          .zone_lights = -4.0,
          .zone_highlights = 7.0,
          .curve_shadows = 4.0,
          .curve_darks = -3.0,
          .curve_lights = 3.0,
          .curve_highlights = -2.0,
          .master_curve_points = {{0.0, 0.0}, {0.50, 0.49}, {1.0, 1.0}},
          .luma_curve_points = {{0.0, 0.0}, {0.50, 0.51}, {1.0, 1.0}},
          .red_curve_points = {{0.0, 0.0}, {0.50, 0.51}, {1.0, 1.0}},
          .green_curve_points = {{0.0, 0.0}, {0.50, 0.49}, {1.0, 1.0}},
          .blue_curve_points = {{0.0, 0.0}, {0.50, 0.51}, {1.0, 1.0}},
          .color_mixer_model = "HSL",
          .color_mixer_channel = "Blues",
          .hue_adjustment = 8.0,
          .saturation_adjustment = 6.0,
          .luminance_adjustment = -4.0,
          .color_mixer_channels = {{
              {.hue = 7.0, .saturation = -6.0, .luminance = 4.0,
               .selective_cyan = 5.0, .selective_magenta = -3.0,
               .selective_yellow = 4.0, .selective_black = -2.0},
              {}, {}, {},
              {.hue = -5.0, .saturation = 7.0, .luminance = -3.0,
               .selective_cyan = -4.0, .selective_magenta = 5.0,
               .selective_yellow = -3.0, .selective_black = 2.0},
              {}, {}, {},
          }},
          .rgb_channel_mixer = {
              .red_from_red = 96.0, .red_from_green = 6.0, .red_from_blue = -2.0,
              .green_from_red = 2.0, .green_from_green = 96.0, .green_from_blue = 2.0,
              .blue_from_red = -1.0, .blue_from_green = 5.0, .blue_from_blue = 96.0,
          },
          .color_replacement_enabled = true,
          .color_replacement_source_hue = 0.08,
          .color_replacement_target_hue = 0.61,
          .color_replacement_range = 72.0,
          .color_replacement_softness = 58.0,
          .color_replacement_strength = 34.0,
          .skin_tone_protection_enabled = true,
          .skin_tone_line_hue = 0.08,
          .skin_tone_protection_range = 36.0,
          .skin_tone_protection_strength = 61.0,
          .color_harmony_enabled = true,
          .color_harmony_mode = "Triadic",
          .color_harmony_base_hue = 0.17,
          .color_harmony_strength = 31.0,
          .grading_shadows_hue = 0.91,
          .grading_shadows_saturation = 12.0,
          .grading_shadows_luminance = -5.0,
          .grading_midtones_hue = 0.12,
          .grading_midtones_saturation = -9.0,
          .grading_midtones_luminance = 4.0,
          .grading_highlights_hue = 0.57,
          .grading_highlights_saturation = 15.0,
          .grading_highlights_luminance = 6.0,
          .grading_amount = 64.0,
          .grading_balance = -18.0,
      },
      PhotoLayer{
          .kind = "adjustment",
          .blend_mode = "normal",
          .opacity = 0.23,
          .black_and_white_enabled = true,
          .black_and_white_mixer = {
              9.0, -6.0, 11.0, -4.0, 5.0, -8.0, 3.0, 7.0},
      },
      PhotoLayer{
          .kind = "pixel",
          .blend_mode = "multiply",
          .opacity = 0.55,
          .pixels = {
              {0.91F, 0.24F, 0.13F, 0.80F},
              {0.18F, 0.82F, 0.31F, 0.62F},
              {0.27F, 0.33F, 0.94F, 0.71F},
              {0.75F, 0.58F, 0.12F, 0.49F},
              {0.42F, 0.16F, 0.69F, 0.88F},
              {0.11F, 0.72F, 0.77F, 0.57F},
          },
          .pixel_width = 3,
          .pixel_height = 2,
          .x = 4,
          .y = 3,
          .transform_scale = 1.35,
          .transform_rotation = 17.0,
      },
  };
  auto detail_fill_state = fill_state;
  detail_fill_state.photo_layers[3].lut_enabled = true;
  detail_fill_state.photo_layers[3].lut_strength = 62.0;
  detail_fill_state.photo_layers[3].lut_size = 2;
  detail_fill_state.photo_layers[3].lut_input_color_space = "sRGB";
  detail_fill_state.photo_layers[3].lut_output_color_space = "Display P3";
  detail_fill_state.photo_layers[3].lut_table = {
      {0.00F, 0.00F, 0.00F, 1.0F}, {0.06F, 0.02F, 0.94F, 1.0F},
      {0.04F, 0.96F, 0.03F, 1.0F}, {0.10F, 0.92F, 0.91F, 1.0F},
      {0.95F, 0.03F, 0.02F, 1.0F}, {0.91F, 0.08F, 0.96F, 1.0F},
      {0.98F, 0.94F, 0.07F, 1.0F}, {1.00F, 1.00F, 1.00F, 1.0F},
  };
  fill_state.photo_layers[3].texture = 0.0;
  fill_state.photo_layers[3].clarity = 0.0;
  fill_state.photo_layers[3].sharpening = 0.0;
  fill_state.photo_layers[3].sharpen_radius = 1.0;
  fill_state.photo_layers[3].sharpen_detail = 0.0;
  fill_state.photo_layers[3].sharpen_masking = 0.0;
  fill_state.photo_layers[3].luminance_nr = 0.0;
  fill_state.photo_layers[3].color_nr = 0.0;
  policy_state.layers = QVariantList{QVariantMap{{QStringLiteral("kind"),
                                                  QStringLiteral("fill")}}};
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, fill_state, source, true, {}).isEmpty()) {
    return fail("exact retained fill layers must be admitted");
  }
  auto spatial_fill_state = fill_state;
  spatial_fill_state.photo_layers[3].texture = 19.0;
  spatial_fill_state.photo_layers[3].clarity = -13.0;
  if (!retained_simple_layers_supported(spatial_fill_state) ||
      !photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, spatial_fill_state, source, true, {}).isEmpty()) {
    return fail("exact retained layer spatial Detail must be admitted");
  }
  if (!retained_simple_layers_supported(detail_fill_state) ||
      !photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, detail_fill_state, source, true, {}).isEmpty()) {
    return fail("exact retained layer spatial and fine Detail must be admitted");
  }
  const auto fill_program = compile_fill_layer_program(detail_fill_state, 13, 11);
  auto canonical_fill = spatial_source;
  auto compiled_fill = spatial_source;
  if (!fill_program.ready() || fill_program.layer_count != 6U ||
      !fill_program.layers[3].has_geometry ||
      !fill_program.layers[3].has_user_lut ||
      !fill_program.layers[3].has_spatial_detail ||
      !fill_program.layers[3].has_fine_detail ||
      !apply_color_layers_stage(
          &canonical_fill, 13, 11, detail_fill_state, RemovalQuality::Interactive) ||
      !apply_compiled_fill_layer_program(&compiled_fill, fill_program)) {
    return fail("retained fill-layer reference must execute");
  }
  auto maximum_fill_error = 0.0F;
  for (auto index = std::size_t{0U}; index < canonical_fill.size(); ++index) {
    const auto& expected = canonical_fill[index];
    const auto& actual = compiled_fill[index];
    maximum_fill_error = std::max({
        maximum_fill_error, std::abs(expected.red - actual.red),
        std::abs(expected.green - actual.green),
        std::abs(expected.blue - actual.blue),
        std::abs(expected.alpha - actual.alpha)});
  }
  if (maximum_fill_error > 0.004F) {
    std::cerr << "retained fill maximum error: " << maximum_fill_error << '\n';
    return fail("retained fill layers exceed CPU parity tolerance");
  }
  for (const auto* blend_mode : {
           "hue", "saturation", "color", "luminosity"}) {
    auto perceptual_blend_state = ColorAdjustmentState{};
    perceptual_blend_state.color_pipeline = default_color_pipeline_config();
    perceptual_blend_state.color_pipeline.output_headroom = 2.0;
    perceptual_blend_state.photo_layers = {PhotoLayer{
        .kind = "fill",
        .blend_mode = blend_mode,
        .opacity = 0.76,
        .fill_color = {0.10F, 0.30F, 1.55F, 0.82F},
    }};
    auto expected = std::vector<ColorPixel>{
        {1.30F, 1.18F, 0.96F, 0.73F}};
    auto actual = expected;
    const auto perceptual_program = compile_fill_layer_program(
        perceptual_blend_state, 1, 1);
    if (!perceptual_program.ready() ||
        !apply_color_layers_stage(
            &expected, 1, 1, perceptual_blend_state,
            RemovalQuality::Interactive) ||
        !apply_compiled_fill_layer_program(&actual, perceptual_program) ||
        std::max({
            std::abs(expected[0].red - actual[0].red),
            std::abs(expected[0].green - actual[0].green),
            std::abs(expected[0].blue - actual[0].blue),
            std::abs(expected[0].alpha - actual[0].alpha),
        }) > 3.0e-6F) {
      return fail("retained perceptual layer blend exceeds CPU parity tolerance");
    }
  }
  auto invalid_mask_fill_state = fill_state;
  invalid_mask_fill_state.photo_layers.front().mask_data.pop_back();
  if (retained_simple_layers_supported(invalid_mask_fill_state) ||
      compile_fill_layer_program(invalid_mask_fill_state, 13, 11).ready()) {
    return fail("malformed retained fill-layer masks must fail closed");
  }
  auto invalid_transform_state = fill_state;
  invalid_transform_state.photo_layers.back().transform_scale =
      std::numeric_limits<double>::quiet_NaN();
  if (retained_simple_layers_supported(invalid_transform_state)) {
    return fail("non-finite raster transforms must fail closed");
  }
  auto invalid_exposure_state = fill_state;
  invalid_exposure_state.photo_layers[3].exposure = 10000.0;
  if (retained_simple_layers_supported(invalid_exposure_state) ||
      compile_fill_layer_program(invalid_exposure_state, 13, 11).ready()) {
    return fail("non-representable adjustment exposure must fail closed");
  }
  auto invalid_tone_state = fill_state;
  invalid_tone_state.photo_layers[3].contrast =
      std::numeric_limits<double>::quiet_NaN();
  if (retained_simple_layers_supported(invalid_tone_state) ||
      compile_fill_layer_program(invalid_tone_state, 13, 11).ready()) {
    return fail("non-finite adjustment tone must fail closed");
  }
  auto invalid_look_state = fill_state;
  invalid_look_state.photo_layers[3].grading_amount =
      std::numeric_limits<double>::quiet_NaN();
  if (retained_simple_layers_supported(invalid_look_state) ||
      compile_fill_layer_program(invalid_look_state, 13, 11).ready()) {
    return fail("non-finite adjustment Look controls must fail closed");
  }
  auto invalid_point_effect_state = fill_state;
  invalid_point_effect_state.photo_layers[3].lens_vignette =
      std::numeric_limits<double>::infinity();
  if (retained_simple_layers_supported(invalid_point_effect_state) ||
      compile_fill_layer_program(invalid_point_effect_state, 13, 11).ready()) {
    return fail("non-finite adjustment point effects must fail closed");
  }
  invalid_point_effect_state = fill_state;
  invalid_point_effect_state.photo_layers[3].dehaze =
      std::numeric_limits<double>::quiet_NaN();
  if (retained_simple_layers_supported(invalid_point_effect_state) ||
      compile_fill_layer_program(invalid_point_effect_state, 13, 11).ready()) {
    return fail("non-finite adjustment dehaze must fail closed");
  }
  auto invalid_detail_state = detail_fill_state;
  invalid_detail_state.photo_layers[3].sharpen_radius =
      std::numeric_limits<double>::quiet_NaN();
  if (compile_fill_layer_program(invalid_detail_state, 13, 11).ready()) {
    return fail("non-finite adjustment layer Detail must fail closed");
  }
  auto invalid_layer_geometry_state = detail_fill_state;
  invalid_layer_geometry_state.photo_layers[3].liquify_center_x =
      std::numeric_limits<double>::quiet_NaN();
  if (retained_simple_layers_supported(invalid_layer_geometry_state) ||
      compile_fill_layer_program(invalid_layer_geometry_state, 13, 11).ready()) {
    return fail("non-finite adjustment layer Geometry must fail closed");
  }
  auto over_cap_layer_state = ColorAdjustmentState{};
  over_cap_layer_state.color_pipeline = default_color_pipeline_config();
  over_cap_layer_state.photo_layers.assign(
      CompiledFillLayerProgram::kMaximumLayers + 1U,
      PhotoLayer{.kind = "fill"});
  if (retained_simple_layers_supported(over_cap_layer_state) ||
      photo_editor::desktop::preview_working_fusion_fallback_reason(
          policy_state, over_cap_layer_state, source, true, {}) !=
          QStringLiteral("unsupported_stage:Layers:photo_layers")) {
    return fail("over-cap retained layer stacks must remain fail closed");
  }
  auto invalid_curve_state = fill_state;
  invalid_curve_state.photo_layers[3].master_curve_points[1].x =
      std::numeric_limits<double>::quiet_NaN();
  if (retained_simple_layers_supported(invalid_curve_state) ||
      compile_fill_layer_program(invalid_curve_state, 13, 11).ready()) {
    return fail("non-finite adjustment curves must fail closed");
  }
  policy_state.layers.clear();
  {
    constexpr auto kGeometryWidth = 9;
    constexpr auto kGeometryHeight = 7;
    auto geometry_state = ColorAdjustmentState{};
    geometry_state.lens_distortion = 23.0;
    geometry_state.chromatic_aberration = 31.0;
    geometry_state.perspective_vertical = 14.0;
    geometry_state.perspective_horizontal = -11.0;
    geometry_state.transform_rotation = 7.0;
    geometry_state.transform_scale = 1.13;
    geometry_state.transform_skew_x = 9.0;
    geometry_state.transform_skew_y = -8.0;
    geometry_state.liquify_strength = 27.0;
    geometry_state.liquify_center_x = 0.31;
    geometry_state.liquify_center_y = 0.68;
    geometry_state.liquify_radius = 0.44;
    geometry_state.content_aware_crop_fill = true;
    auto geometry_source = std::vector<ColorPixel>{};
    geometry_source.reserve(kGeometryWidth * kGeometryHeight);
    for (auto y = 0; y < kGeometryHeight; ++y) {
      for (auto x = 0; x < kGeometryWidth; ++x) {
        geometry_source.push_back({
            .red = static_cast<float>(x) / (kGeometryWidth - 1),
            .green = static_cast<float>(y) / (kGeometryHeight - 1),
            .blue = static_cast<float>(x + y) /
                    (kGeometryWidth + kGeometryHeight - 2),
            .alpha = 0.2F + 0.9F * static_cast<float>(x) /
                                 (kGeometryWidth - 1),
        });
      }
    }
    auto canonical_geometry = geometry_source;
    auto compiled_geometry = geometry_source;
    const auto geometry_program = compile_geometry_stage_program(
        geometry_state, kGeometryWidth, kGeometryHeight);
    if (!geometry_program.ready() || !geometry_program.active ||
        !apply_compiled_geometry_stage_program(
            &compiled_geometry, geometry_program) ||
        !apply_color_geometry_stage(
            &canonical_geometry, kGeometryWidth, kGeometryHeight,
            geometry_state)) {
      return fail("compiled retained Geometry reference must execute");
    }
    for (auto index = std::size_t{0}; index < canonical_geometry.size();
         ++index) {
      const auto& expected = canonical_geometry[index];
      const auto& actual = compiled_geometry[index];
      if (std::max({
              std::abs(expected.red - actual.red),
              std::abs(expected.green - actual.green),
              std::abs(expected.blue - actual.blue),
              std::abs(expected.alpha - actual.alpha),
          }) > 2.0e-5F) {
        return fail("compiled retained Geometry exceeds CPU parity tolerance");
      }
    }
    geometry_state.transform_rotation =
        std::numeric_limits<double>::quiet_NaN();
    if (compile_geometry_stage_program(
            geometry_state, kGeometryWidth, kGeometryHeight).ready()) {
      return fail("non-finite retained Geometry must fail closed");
    }
  }
  policy_state.contrast = 1.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty() ||
      !photo_editor::desktop::preview_uses_compiled_working_tone(
          policy_state)) {
    return fail("compiled per-pixel tone controls must be admitted");
  }
  policy_state.filmic_shoulder = 1.0;
  if (photo_editor::desktop::preview_working_fusion_fallback_reason(
          policy_state, state, source, true, {}) !=
      QStringLiteral("unsupported_stage:Tone:exact_global_tone")) {
    return fail("image-dependent shoulder must remain on exact CPU fallback");
  }
  policy_state.filmic_shoulder = 0.0;
  policy_state.red_curve_points = {
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.8}},
  };
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty() ||
      !photo_editor::desktop::preview_uses_compiled_working_curves(
          policy_state)) {
    return fail("compiled channel curves must be admitted by working fusion");
  }
  policy_state.red_curve_points.clear();
  state.lut_enabled = true;
  state.lut_strength = 67.0;
  state.lut_size = 2;
  state.lut_input_color_space = "sRGB";
  state.lut_output_color_space = "Display P3";
  for (auto red = 0; red < 2; ++red) {
    for (auto green = 0; green < 2; ++green) {
      for (auto blue = 0; blue < 2; ++blue) {
        state.lut_table.push_back({
            .red = static_cast<float>(blue),
            .green = static_cast<float>(red),
            .blue = static_cast<float>(green),
            .alpha = 1.0F,
        });
      }
    }
  }
  auto stacked_lut = LutAdjustment{
      .enabled = true,
      .strength = 43.0,
      .name = "acescct-stack",
      .size = 2,
      .domain_min = {-0.1, -0.1, -0.1},
      .domain_max = {1.1, 1.1, 1.1},
      .input_color_space = "ACEScct",
      .output_color_space = "ACEScg",
  };
  for (auto red = 0; red < 2; ++red) {
    for (auto green = 0; green < 2; ++green) {
      for (auto blue = 0; blue < 2; ++blue) {
        stacked_lut.table.push_back({
            .red = static_cast<float>(red),
            .green = static_cast<float>(blue),
            .blue = static_cast<float>(green),
            .alpha = 1.0F,
        });
      }
    }
  }
  state.lut_stack = {stacked_lut};
  const auto compiled_user_luts = compile_user_lut_stage_program(state);
  const auto compiled_user_luts_again = compile_user_lut_stage_program(state);
  auto changed_lut_state = state;
  changed_lut_state.lut_stack[0].strength += 1.0;
  const auto changed_user_luts =
      compile_user_lut_stage_program(changed_lut_state);
  if (!compiled_user_luts.ready() || compiled_user_luts.luts.size() != 2U ||
      compiled_user_luts.cache_identity !=
          compiled_user_luts_again.cache_identity ||
      !changed_user_luts.ready() ||
      changed_user_luts.cache_identity == compiled_user_luts.cache_identity ||
      !photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty() ||
      !photo_editor::desktop::preview_uses_compiled_user_luts(state)) {
    return fail("color-space-aware user LUT stacks must compile and be admitted");
  }
  auto exact_user_luts = source;
  if (!apply_color_look_stage(
          &exact_user_luts, static_cast<int>(exact_user_luts.size()), 1,
          state)) {
    return fail("canonical user LUT stack reference failed");
  }
  for (auto index = std::size_t{0U}; index < source.size(); ++index) {
    const auto compiled = apply_compiled_user_lut_stage_program(
        compiled_user_luts, source[index]);
    if (std::abs(compiled.red - exact_user_luts[index].red) > 1.0e-6F ||
        std::abs(compiled.green - exact_user_luts[index].green) > 1.0e-6F ||
        std::abs(compiled.blue - exact_user_luts[index].blue) > 1.0e-6F ||
        compiled.alpha != exact_user_luts[index].alpha) {
      return fail("compiled user LUT stack must match canonical CPU execution");
    }
  }
  const auto valid_lut_table = state.lut_table;
  state.lut_table.clear();
  if (photo_editor::desktop::preview_working_fusion_fallback_reason(
          policy_state, state, source, true, {}) !=
      QStringLiteral("unsupported_stage:Look:user_lut")) {
    return fail("invalid user LUT payloads must remain fail-closed");
  }
  state.lut_enabled = false;
  state.lut_table = valid_lut_table;
  state.lut_stack.clear();
  policy_state.saturation = 22.0;
  policy_state.vibrance = 31.0;
  policy_state.rgb_channel_mixer.red_from_green = 8.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty() ||
      !photo_editor::desktop::preview_uses_compiled_working_look(
          policy_state)) {
    return fail("compiled common Look controls must be admitted");
  }
  policy_state.grading_shadows_hue = 0.92;
  policy_state.grading_shadows_saturation = 24.0;
  policy_state.grading_midtones_luminance = -8.0;
  policy_state.grading_highlights_hue = 0.56;
  policy_state.grading_highlights_saturation = 31.0;
  policy_state.grading_amount = 73.0;
  policy_state.grading_balance = -26.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty() ||
      !photo_editor::desktop::preview_uses_compiled_working_look(
          policy_state)) {
    return fail("compiled color grading must be admitted by working fusion");
  }
  policy_state.mixer_channels[0].hue = 1.0;
  policy_state.mixer_channels[0].selective_cyan = -12.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty() ||
      !photo_editor::desktop::preview_uses_compiled_working_look(
          policy_state)) {
    return fail("compiled per-channel mixer must be admitted");
  }
  policy_state.color_harmony_enabled = true;
  policy_state.color_harmony_strength = 62.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty() ||
      !photo_editor::desktop::preview_uses_compiled_working_look(
          policy_state)) {
    return fail("compiled color harmony must be admitted");
  }
  policy_state.color_replacement_enabled = true;
  policy_state.color_replacement_strength = 1.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty() ||
      !photo_editor::desktop::preview_uses_compiled_working_look(
          policy_state)) {
    return fail("compiled color replacement must be admitted");
  }
  policy_state.skin_tone_protection_enabled = true;
  policy_state.skin_tone_protection_strength = 75.0;
  if (!photo_editor::desktop::preview_working_fusion_fallback_reason(
           policy_state, state, source, true, {}).isEmpty() ||
      !photo_editor::desktop::preview_uses_compiled_working_look(
          policy_state)) {
    return fail("compiled skin-protected Look controls must be admitted");
  }
  policy_state.color_replacement_enabled = false;
  policy_state.color_replacement_strength = 0.0;
  policy_state.skin_tone_protection_enabled = false;
  policy_state.skin_tone_protection_strength = 0.0;
  policy_state.color_harmony_enabled = false;
  policy_state.color_harmony_strength = 0.0;
  policy_state.saturation = 0.0;
  policy_state.vibrance = 0.0;
  policy_state.rgb_channel_mixer = {};
  policy_state.mixer_channels[0].hue = 0.0;
  policy_state.mixer_channels[0].selective_cyan = 0.0;
  const auto working_lut =
      photo_editor::desktop::build_canonical_working_stage_lut(state);
  for (const auto& pixel : source) {
    const auto sampled = apply_lut(working_lut, pixel);
    if (std::abs(sampled.red - pixel.red) > 1.0e-6F ||
        std::abs(sampled.green - pixel.green) > 1.0e-6F ||
        std::abs(sampled.blue - pixel.blue) > 1.0e-6F ||
        sampled.alpha != pixel.alpha) {
      return fail(
          "working-stage LUT must not reapply the consumed camera/input prefix");
    }
  }
  state.contrast = 24.0;
  state.highlights = -28.0;
  state.shadows = 31.0;
  state.whites = 12.0;
  state.blacks = -9.0;
  const auto compiled_working_tone = compile_tone_stage_lut(state);
  if (!photo_editor::desktop::canonical_compiled_tone_supports_source(
          compiled_working_tone,
          source,
          std::exp2(effective_input_exposure(state)))) {
    return fail("bounded characterized pixels must fit the compiled tone domain");
  }
  auto unsupported_tone_source = source;
  unsupported_tone_source.front().red = 1.01F;
  if (!photo_editor::desktop::canonical_compiled_tone_supports_source(
          compiled_working_tone,
          unsupported_tone_source,
          std::exp2(effective_input_exposure(state)))) {
    return fail("compiled tone admission must preserve extended RAW headroom");
  }
  unsupported_tone_source.front().red = 1.0e9F;
  if (photo_editor::desktop::canonical_compiled_tone_supports_source(
          compiled_working_tone,
          unsupported_tone_source,
          std::exp2(effective_input_exposure(state)))) {
    return fail("compiled tone admission must reject its true stop-domain overflow");
  }
  auto reference = source;
  auto telemetry = ColorAdjustmentTelemetry{};
  if (!apply_color_scene_adjustments_stage(
          &reference, 2, 1, state, {}, &telemetry) ||
      !apply_color_tone_stage(&reference, 2, 1, state, {}, &telemetry) ||
      !apply_color_look_stage(&reference, 2, 1, state, {}, &telemetry) ||
      !apply_color_layers_stage(
          &reference, 2, 1, state, RemovalQuality::Interactive, {}, &telemetry) ||
      !apply_color_detail_stage(
          &reference, 2, 1, state, RemovalQuality::Interactive, {}, &telemetry) ||
      !apply_color_geometry_stage(&reference, 2, 1, state, {}, &telemetry)) {
    return fail("exact working-stage reference failed");
  }

  const auto scene_index = std::size_t{2};
  const auto manifest = photo_editor::desktop::canonical_working_fusion_manifest(
      request.plan, scene_index);
  auto input = RenderSurface{
      .descriptor = render_stage_output_descriptor(
          request, RenderStageKind::CameraCharacterization),
      .pixels = source,
      .identity = "surface:camera:working-fusion",
  };
  const auto invocation = RenderStageInvocation{
      .stage = request.plan.stages[scene_index],
      .stage_index = scene_index,
      .stage_cache_key = "stage:working-fusion",
      .expected_output = render_stage_output_descriptor(
          request, RenderStageKind::SceneAdjustments),
  };
  auto fallback_telemetry = ColorAdjustmentTelemetry{};
  const auto result =
      photo_editor::desktop::execute_canonical_native_working_fusion(
          invocation,
          render_stage_output_descriptor(request, RenderStageKind::Geometry),
          std::move(input),
          state,
          working_lut,
          &compiled_working_tone,
          nullptr,
          nullptr,
          nullptr,
          manifest,
          false,
          "metal-disabled-for-test",
          &fallback_telemetry,
          {});
  if (!result.output.has_value() || result.output->backing != nullptr ||
      result.backend != "cpu-reference-working-fused-fallback" ||
      result.fallback_reason != "metal-disabled-for-test" ||
      !result.fusion_evidence.has_value() ||
      result.fusion_evidence->logical_stages_covered != manifest ||
      !pixels_equal(result.output->pixels, reference)) {
    return fail(
        "working-stage fusion must preserve the exact CPU reference and complete manifest when Metal is not admitted");
  }

  auto malformed_input = RenderSurface{
      .descriptor = render_stage_output_descriptor(
          request, RenderStageKind::CameraCharacterization),
      .pixels = source,
      .identity = "surface:camera:malformed-working-fusion",
  };
  const auto malformed =
      photo_editor::desktop::execute_canonical_native_working_fusion(
          invocation,
          render_stage_output_descriptor(request, RenderStageKind::Geometry),
          std::move(malformed_input),
          state,
          working_lut,
          &compiled_working_tone,
          nullptr,
          nullptr,
          nullptr,
          {},
          false,
          {},
          nullptr,
          {});
  if (malformed.output.has_value() || malformed.failure_reason.empty()) {
    return fail("working-stage fusion must reject an incomplete manifest");
  }
  return EXIT_SUCCESS;
}

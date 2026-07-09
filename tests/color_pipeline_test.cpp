#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/render_graph.h"

int main() {
  const auto default_config = photo_editor::pipeline::default_color_pipeline_config();
  if (default_config.input_profile.profile_name != "Embedded") {
    std::cerr << "default input profile should be Embedded\n";
    return EXIT_FAILURE;
  }
  if (default_config.working_profile.profile_name != "Linear ProPhoto RGB") {
    std::cerr << "default working profile should be Linear ProPhoto RGB\n";
    return EXIT_FAILURE;
  }
  if (default_config.preview_output_profile.profile_name != "sRGB" ||
      default_config.export_output_profile.profile_name != "sRGB") {
    std::cerr << "default output profiles should be sRGB\n";
    return EXIT_FAILURE;
  }
  if (std::abs(default_config.output_headroom - 1.0) > 1.0e-9) {
    std::cerr << "default output headroom should target SDR\n";
    return EXIT_FAILURE;
  }
  if (!default_config.output_gamut_mapping_enabled ||
      default_config.output_gamut_mapping_policy !=
          photo_editor::pipeline::GamutMappingPolicy::Perceptual) {
    std::cerr << "default output gamut mapping should use perceptual compression\n";
    return EXIT_FAILURE;
  }
  if (!default_config.black_point_compensation_enabled ||
      std::abs(default_config.source_black_point) > 1.0e-9 ||
      std::abs(default_config.output_black_point) > 1.0e-9) {
    std::cerr << "default black-point compensation should be enabled but neutral\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_output_color_profile_name("display-p3") != "Display P3") {
    std::cerr << "output profile normalization should canonicalize Display P3\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_render_intent_name("relative-colorimetric") !=
      "Relative Colorimetric") {
    std::cerr << "render intent normalization should canonicalize relative colorimetric\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_gamut_mapping_policy_name("clipping") != "Clip" ||
      photo_editor::pipeline::normalize_gamut_mapping_policy_name("preserve-saturation") !=
          "Preserve Saturation") {
    std::cerr << "gamut mapping policy normalization should canonicalize supported policies\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_working_color_profile_name("linear-rec2020") !=
      "Linear Rec.2020") {
    std::cerr << "working profile normalization should canonicalize Linear Rec.2020\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_output_color_profile_name("rec2020") != "Rec.2020") {
    std::cerr << "output profile normalization should canonicalize Rec.2020\n";
    return EXIT_FAILURE;
  }

  struct ProfileExpectation final {
    const char* input_name;
    const char* canonical_name;
    photo_editor::pipeline::OutputTransformKind output_kind;
    photo_editor::pipeline::ColorPrimaries primaries;
  };
  const std::vector<ProfileExpectation> rgb_profile_expectations = {
      {"srgb", "sRGB", photo_editor::pipeline::OutputTransformKind::Srgb,
       photo_editor::pipeline::ColorPrimaries::Srgb},
      {"display-p3", "Display P3", photo_editor::pipeline::OutputTransformKind::DisplayP3,
       photo_editor::pipeline::ColorPrimaries::DisplayP3},
      {"adobe-rgb", "Adobe RGB", photo_editor::pipeline::OutputTransformKind::AdobeRgb,
       photo_editor::pipeline::ColorPrimaries::AdobeRgb},
      {"prophoto", "ProPhoto RGB", photo_editor::pipeline::OutputTransformKind::ProPhotoRgb,
       photo_editor::pipeline::ColorPrimaries::ProPhotoRgb},
      {"rec709", "Rec.709", photo_editor::pipeline::OutputTransformKind::Rec709,
       photo_editor::pipeline::ColorPrimaries::Rec709},
      {"rec2020", "Rec.2020", photo_editor::pipeline::OutputTransformKind::Rec2020,
       photo_editor::pipeline::ColorPrimaries::Rec2020},
  };
  for (const auto& expectation : rgb_profile_expectations) {
    const auto input_profile =
        photo_editor::pipeline::input_color_profile_from_name(expectation.input_name);
    const auto output_profile =
        photo_editor::pipeline::output_color_profile_from_name(expectation.input_name);
    if (input_profile.profile_name != expectation.canonical_name ||
        input_profile.kind != photo_editor::pipeline::InputTransformKind::EmbeddedProfile ||
        input_profile.color_space.primaries != expectation.primaries) {
      std::cerr << "input profile should support " << expectation.canonical_name << '\n';
      return EXIT_FAILURE;
    }
    if (output_profile.profile_name != expectation.canonical_name ||
        output_profile.kind != expectation.output_kind ||
        output_profile.color_space.primaries != expectation.primaries) {
      std::cerr << "output profile should support " << expectation.canonical_name << '\n';
      return EXIT_FAILURE;
    }
  }
  if (photo_editor::pipeline::working_color_profile_from_name("linear-display-p3")
          .color_space.primaries != photo_editor::pipeline::ColorPrimaries::DisplayP3 ||
      photo_editor::pipeline::working_color_profile_from_name("linear-prophoto-rgb")
          .color_space.primaries != photo_editor::pipeline::ColorPrimaries::ProPhotoRgb ||
      photo_editor::pipeline::working_color_profile_from_name("linear-rec2020")
          .color_space.primaries != photo_editor::pipeline::ColorPrimaries::Rec2020) {
    std::cerr << "working profiles should support wide-gamut RGB working spaces\n";
    return EXIT_FAILURE;
  }

  const auto resolved_profile = photo_editor::pipeline::resolve_camera_profile(
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .input_profile_name = "Camera Native",
          .camera_make = "Canon",
          .camera_model = "EOS R5",
          .as_shot_temperature = 4850.0,
          .as_shot_tint = 8.0,
          .baseline_exposure = 0.25,
          .highlight_headroom = 0.9,
      });
  if (!resolved_profile.valid || resolved_profile.profile_name != "Canon EOS R5") {
    std::cerr << "camera profile resolution should preserve make and model identity\n";
    return EXIT_FAILURE;
  }

  const auto dcp_input_profile =
      photo_editor::pipeline::input_color_profile_from_name("/profiles/Canon EOS R5 Adobe Standard.dcp");
  if (dcp_input_profile.profile_name != "Camera DCP: Canon EOS R5 Adobe Standard.dcp" ||
      dcp_input_profile.kind != photo_editor::pipeline::InputTransformKind::CameraDcpProfile) {
    std::cerr << "DCP camera profiles should normalize as camera input profiles\n";
    return EXIT_FAILURE;
  }
  const auto icc_input_profile =
      photo_editor::pipeline::input_color_profile_from_name("icc: Canon EOS R5 Manufacturer");
  if (icc_input_profile.profile_name != "Camera ICC: Canon EOS R5 Manufacturer" ||
      icc_input_profile.kind != photo_editor::pipeline::InputTransformKind::CameraIccProfile) {
    std::cerr << "ICC camera profiles should normalize as camera input profiles\n";
    return EXIT_FAILURE;
  }

  const auto dcp_resolved_profile = photo_editor::pipeline::resolve_camera_profile(
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .input_profile_name = dcp_input_profile.profile_name,
          .camera_make = "Canon",
          .camera_model = "EOS R5",
          .as_shot_temperature = 4850.0,
          .as_shot_tint = 8.0,
      },
      dcp_input_profile);
  if (!dcp_resolved_profile.valid || dcp_resolved_profile.profile_format != "DCP" ||
      dcp_resolved_profile.profile_name != dcp_input_profile.profile_name ||
      !dcp_resolved_profile.dual_illuminant ||
      std::abs(dcp_resolved_profile.camera_to_xyz_d50_daylight[0] - 1.04) > 1.0e-9) {
    std::cerr << "DCP camera profiles should resolve into a camera transform\n";
    return EXIT_FAILURE;
  }

  const auto resolved_recipe = photo_editor::pipeline::resolve_input_transform_recipe(
      photo_editor::pipeline::input_color_profile_from_name("Camera Native"),
      resolved_profile,
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .input_profile_name = "Camera Native",
          .camera_make = "Canon",
          .camera_model = "EOS R5",
          .as_shot_temperature = 4850.0,
          .as_shot_tint = 8.0,
          .baseline_exposure = 0.25,
          .highlight_headroom = 0.9,
      });
  if (!resolved_recipe.has_source_white_balance ||
      std::abs(resolved_recipe.source_temperature - 4850.0) > 1.0e-6 ||
      std::abs(resolved_recipe.baseline_exposure - 0.25) > 1.0e-6) {
    std::cerr << "input transform recipe should keep decoded RAW normalization metadata\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState state;
  state.exposure = 1.25;
  if (photo_editor::pipeline::effective_input_exposure(state) != 1.25) {
    std::cerr << "input exposure helper should fall back to legacy exposure\n";
    return EXIT_FAILURE;
  }
  state.input_transform.exposure = 0.5;
  state.input_transform.temperature = 12.0;
  state.input_transform.tint = -8.0;
  if (photo_editor::pipeline::effective_input_exposure(state) != 0.5 ||
      photo_editor::pipeline::effective_input_temperature(state) != 12.0 ||
      photo_editor::pipeline::effective_input_tint(state) != -8.0) {
    std::cerr << "input transform helpers should prefer explicit input transform values\n";
    return EXIT_FAILURE;
  }

  const photo_editor::pipeline::RenderGraph graph;
  const auto& stages = graph.stages();
  if (stages.size() < 7) {
    std::cerr << "expected render graph to include the input transform stage\n";
    return EXIT_FAILURE;
  }
  if (stages[1].name != "input_transform") {
    std::cerr << "expected input_transform to follow raw_decode\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState white_balance_bradford;
  white_balance_bradford.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  white_balance_bradford.input_transform.temperature = 55.0;
  white_balance_bradford.input_transform.tint = 30.0;
  white_balance_bradford.input_transform.chromatic_adaptation_method = "Bradford";
  std::vector<photo_editor::pipeline::ColorPixel> bradford_pixels = {
      {.red = 0.5F, .green = 0.5F, .blue = 0.5F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&bradford_pixels, 1, 1, white_balance_bradford);

  auto white_balance_cat02 = white_balance_bradford;
  white_balance_cat02.input_transform.chromatic_adaptation_method = "CAT02";
  std::vector<photo_editor::pipeline::ColorPixel> cat02_pixels = {
      {.red = 0.5F, .green = 0.5F, .blue = 0.5F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&cat02_pixels, 1, 1, white_balance_cat02);
  const auto adaptation_delta = std::max({
      std::abs(bradford_pixels[0].red - cat02_pixels[0].red),
      std::abs(bradford_pixels[0].green - cat02_pixels[0].green),
      std::abs(bradford_pixels[0].blue - cat02_pixels[0].blue),
  });
  if (adaptation_delta <= 1.0e-5F) {
    std::cerr << "chromatic adaptation method should affect white balance in wide-gamut working space\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState input_stage_white_balance_state;
  input_stage_white_balance_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  input_stage_white_balance_state.color_pipeline.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  input_stage_white_balance_state.color_pipeline.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("Display P3");
  input_stage_white_balance_state.input_transform.raw_input_transform_enabled = true;
  input_stage_white_balance_state.input_transform.temperature = 46.0;
  input_stage_white_balance_state.input_transform.tint = -24.0;
  input_stage_white_balance_state.input_transform.chromatic_adaptation_method = "Bradford";
  input_stage_white_balance_state.input_transform.raw_black_level = 0.04;
  input_stage_white_balance_state.input_transform.raw_white_level = 1.18;
  input_stage_white_balance_state.input_transform.calibration_hue = 18.0;
  input_stage_white_balance_state.input_transform.red_primary_saturation = 14.0;
  input_stage_white_balance_state.exposure = 0.38;
  input_stage_white_balance_state.contrast = 24.0;
  input_stage_white_balance_state.color_pipeline.output_headroom = 1.0;
  std::vector<photo_editor::pipeline::ColorPixel> input_stage_white_balance_pixels = {
      {.red = 0.68F, .green = 0.37F, .blue = 0.16F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &input_stage_white_balance_pixels,
      1,
      1,
      input_stage_white_balance_state);

  auto late_white_balance_state = input_stage_white_balance_state;
  late_white_balance_state.input_transform.temperature = 0.0;
  late_white_balance_state.input_transform.tint = 0.0;
  photo_editor::pipeline::PhotoLayer late_white_balance_layer;
  late_white_balance_layer.enabled = true;
  late_white_balance_layer.kind = "adjustment";
  late_white_balance_layer.blend_mode = "normal";
  late_white_balance_layer.opacity = 1.0;
  late_white_balance_layer.temperature = 46.0;
  late_white_balance_layer.tint = -24.0;
  late_white_balance_state.photo_layers.push_back(late_white_balance_layer);
  std::vector<photo_editor::pipeline::ColorPixel> late_white_balance_pixels = {
      {.red = 0.68F, .green = 0.37F, .blue = 0.16F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &late_white_balance_pixels,
      1,
      1,
      late_white_balance_state);
  const auto input_vs_late_white_balance_delta = std::max({
      std::abs(input_stage_white_balance_pixels[0].red - late_white_balance_pixels[0].red),
      std::abs(input_stage_white_balance_pixels[0].green - late_white_balance_pixels[0].green),
      std::abs(input_stage_white_balance_pixels[0].blue - late_white_balance_pixels[0].blue),
  });
  if (input_vs_late_white_balance_delta <= 0.01F) {
    std::cerr << "RAW white balance should run in the input transform, not as late RGB tinting\n";
    return EXIT_FAILURE;
  }

  const auto preview_transform = photo_editor::pipeline::apply_output_transform(
      input_stage_white_balance_pixels[0],
      input_stage_white_balance_state.color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
      input_stage_white_balance_state.input_transform.chromatic_adaptation_method);
  const auto export_transform = photo_editor::pipeline::apply_output_transform(
      input_stage_white_balance_pixels[0],
      input_stage_white_balance_state.color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::ExportOutput,
      input_stage_white_balance_state.input_transform.chromatic_adaptation_method);
  if (preview_transform.output_profile_name != "sRGB" ||
      export_transform.output_profile_name != "Display P3" ||
      !std::isfinite(preview_transform.pixel.red) ||
      !std::isfinite(preview_transform.pixel.green) ||
      !std::isfinite(preview_transform.pixel.blue) ||
      !std::isfinite(export_transform.pixel.red) ||
      !std::isfinite(export_transform.pixel.green) ||
      !std::isfinite(export_transform.pixel.blue) ||
      preview_transform.pixel.red < -1.0e-6F ||
      preview_transform.pixel.green < -1.0e-6F ||
      preview_transform.pixel.blue < -1.0e-6F ||
      export_transform.pixel.red < -1.0e-6F ||
      export_transform.pixel.green < -1.0e-6F ||
      export_transform.pixel.blue < -1.0e-6F ||
      preview_transform.pixel.red > 1.00001F ||
      preview_transform.pixel.green > 1.00001F ||
      preview_transform.pixel.blue > 1.00001F ||
      export_transform.pixel.red > 1.00001F ||
      export_transform.pixel.green > 1.00001F ||
      export_transform.pixel.blue > 1.00001F) {
    std::cerr << "input-stage white balance should feed bounded preview and export transforms\n";
    return EXIT_FAILURE;
  }
  std::vector<photo_editor::pipeline::ColorPixel> preview_transform_pixels = {
      input_stage_white_balance_pixels[0],
  };
  std::vector<photo_editor::pipeline::ColorPixel> export_transform_pixels = {
      input_stage_white_balance_pixels[0],
  };
  photo_editor::pipeline::apply_output_transform(
      preview_transform_pixels,
      input_stage_white_balance_state.color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
      input_stage_white_balance_state.input_transform.chromatic_adaptation_method);
  photo_editor::pipeline::apply_output_transform(
      export_transform_pixels,
      input_stage_white_balance_state.color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::ExportOutput,
      input_stage_white_balance_state.input_transform.chromatic_adaptation_method);
  if (std::abs(preview_transform_pixels[0].red - preview_transform.pixel.red) > 1.0e-6F ||
      std::abs(preview_transform_pixels[0].green - preview_transform.pixel.green) > 1.0e-6F ||
      std::abs(preview_transform_pixels[0].blue - preview_transform.pixel.blue) > 1.0e-6F ||
      std::abs(export_transform_pixels[0].red - export_transform.pixel.red) > 1.0e-6F ||
      std::abs(export_transform_pixels[0].green - export_transform.pixel.green) > 1.0e-6F ||
      std::abs(export_transform_pixels[0].blue - export_transform.pixel.blue) > 1.0e-6F) {
    std::cerr << "preview/export output transforms should use the same input-stage white-balance result\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState calibration_state;
  calibration_state.input_transform.calibration_hue = 24.0;
  calibration_state.input_transform.calibration_saturation = 35.0;
  calibration_state.input_transform.red_primary_hue = 40.0;
  calibration_state.input_transform.red_primary_saturation = 30.0;
  std::vector<photo_editor::pipeline::ColorPixel> calibrated_pixels = {
      {.red = 0.72F, .green = 0.18F, .blue = 0.08F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&calibrated_pixels, 1, 1, calibration_state);
  std::vector<photo_editor::pipeline::ColorPixel> neutral_calibration_pixels = {
      {.red = 0.72F, .green = 0.18F, .blue = 0.08F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&neutral_calibration_pixels, 1, 1, {});
  const auto calibration_delta = std::max({
      std::abs(calibrated_pixels[0].red - neutral_calibration_pixels[0].red),
      std::abs(calibrated_pixels[0].green - neutral_calibration_pixels[0].green),
      std::abs(calibrated_pixels[0].blue - neutral_calibration_pixels[0].blue),
  });
  if (calibration_delta <= 1.0e-4F || calibrated_pixels[0].green <= neutral_calibration_pixels[0].green) {
    std::cerr << "RAW calibration controls should affect input-stage hue, saturation, and primary shifts\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState camera_look_state;
  camera_look_state.input_transform.camera_matching_look = "Vivid";
  std::vector<photo_editor::pipeline::ColorPixel> vivid_look_pixels = {
      {.red = 0.58F, .green = 0.30F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&vivid_look_pixels, 1, 1, camera_look_state);
  std::vector<photo_editor::pipeline::ColorPixel> standard_look_pixels = {
      {.red = 0.58F, .green = 0.30F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&standard_look_pixels, 1, 1, {});
  const auto camera_look_delta = std::max({
      std::abs(vivid_look_pixels[0].red - standard_look_pixels[0].red),
      std::abs(vivid_look_pixels[0].green - standard_look_pixels[0].green),
      std::abs(vivid_look_pixels[0].blue - standard_look_pixels[0].blue),
  });
  if (camera_look_delta <= 1.0e-4F || vivid_look_pixels[0].red <= standard_look_pixels[0].red) {
    std::cerr << "camera matching looks should affect the RAW input-stage rendering\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState dcp_profile_state;
  dcp_profile_state.color_pipeline.input_profile = dcp_input_profile;
  dcp_profile_state.color_pipeline.camera_profile = dcp_resolved_profile;
  std::vector<photo_editor::pipeline::ColorPixel> dcp_profile_pixels = {
      {.red = 0.42F, .green = 0.30F, .blue = 0.18F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&dcp_profile_pixels, 1, 1, dcp_profile_state);
  std::vector<photo_editor::pipeline::ColorPixel> embedded_profile_pixels = {
      {.red = 0.42F, .green = 0.30F, .blue = 0.18F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&embedded_profile_pixels, 1, 1, {});
  const auto camera_profile_delta = std::max({
      std::abs(dcp_profile_pixels[0].red - embedded_profile_pixels[0].red),
      std::abs(dcp_profile_pixels[0].green - embedded_profile_pixels[0].green),
      std::abs(dcp_profile_pixels[0].blue - embedded_profile_pixels[0].blue),
  });
  if (camera_profile_delta <= 1.0e-4F) {
    std::cerr << "DCP/ICC camera profiles should affect the input-stage camera transform\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState raw_normalization_state;
  raw_normalization_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  raw_normalization_state.color_pipeline.input_profile = dcp_input_profile;
  raw_normalization_state.color_pipeline.camera_profile = dcp_resolved_profile;
  raw_normalization_state.color_pipeline.input_transform_recipe.has_source_white_balance = true;
  raw_normalization_state.color_pipeline.input_transform_recipe.source_temperature = 4200.0;
  raw_normalization_state.color_pipeline.input_transform_recipe.source_tint = -10.0;
  raw_normalization_state.color_pipeline.input_transform_recipe.baseline_exposure = 0.55;
  raw_normalization_state.color_pipeline.input_transform_recipe.highlight_headroom = 0.72;
  raw_normalization_state.input_transform.raw_input_transform_enabled = true;
  raw_normalization_state.input_transform.raw_white_balance_override_enabled = true;
  raw_normalization_state.input_transform.raw_white_balance_temperature = 6400.0;
  raw_normalization_state.input_transform.raw_white_balance_tint = 16.0;
  raw_normalization_state.input_transform.raw_black_level = 0.08;
  raw_normalization_state.input_transform.raw_white_level = 1.45;
  raw_normalization_state.input_transform.highlight_recovery_enabled = true;
  raw_normalization_state.input_transform.highlight_reconstruction_mode = "Enabled";
  raw_normalization_state.input_transform.chromatic_adaptation_method = "CAT02";
  raw_normalization_state.input_transform.camera_matching_look = "Landscape";
  raw_normalization_state.input_transform.calibration_hue = -18.0;
  raw_normalization_state.input_transform.calibration_saturation = 24.0;
  raw_normalization_state.input_transform.red_primary_hue = 15.0;
  raw_normalization_state.input_transform.red_primary_saturation = 20.0;
  raw_normalization_state.input_transform.green_primary_hue = -12.0;
  raw_normalization_state.input_transform.green_primary_saturation = -18.0;
  raw_normalization_state.input_transform.blue_primary_hue = 10.0;
  raw_normalization_state.input_transform.blue_primary_saturation = 14.0;
  std::vector<photo_editor::pipeline::ColorPixel> raw_normalization_pixels = {
      {.red = 0.16F, .green = 0.38F, .blue = 0.92F, .alpha = 1.0F},
      {.red = 1.70F, .green = 0.26F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &raw_normalization_pixels,
      2,
      1,
      raw_normalization_state);

  auto disabled_raw_normalization_state = raw_normalization_state;
  disabled_raw_normalization_state.input_transform.raw_input_transform_enabled = false;
  std::vector<photo_editor::pipeline::ColorPixel> disabled_raw_normalization_pixels = {
      {.red = 0.16F, .green = 0.38F, .blue = 0.92F, .alpha = 1.0F},
      {.red = 1.70F, .green = 0.26F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &disabled_raw_normalization_pixels,
      2,
      1,
      disabled_raw_normalization_state);

  photo_editor::pipeline::ColorAdjustmentState non_raw_reference_state;
  non_raw_reference_state.color_pipeline.working_profile =
      raw_normalization_state.color_pipeline.working_profile;
  non_raw_reference_state.input_transform.raw_input_transform_enabled = false;
  non_raw_reference_state.input_transform.chromatic_adaptation_method = "CAT02";
  std::vector<photo_editor::pipeline::ColorPixel> non_raw_reference_pixels = {
      {.red = 0.16F, .green = 0.38F, .blue = 0.92F, .alpha = 1.0F},
      {.red = 1.70F, .green = 0.26F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &non_raw_reference_pixels,
      2,
      1,
      non_raw_reference_state);
  auto max_pixel_delta = [](const std::vector<photo_editor::pipeline::ColorPixel>& left,
                            const std::vector<photo_editor::pipeline::ColorPixel>& right) {
    double delta = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
      delta = std::max(delta, static_cast<double>(std::abs(left[index].red - right[index].red)));
      delta = std::max(delta, static_cast<double>(std::abs(left[index].green - right[index].green)));
      delta = std::max(delta, static_cast<double>(std::abs(left[index].blue - right[index].blue)));
    }
    return delta;
  };
  if (max_pixel_delta(raw_normalization_pixels, disabled_raw_normalization_pixels) <= 0.05) {
    std::cerr << "camera normalization fields should affect RAW-like input when enabled\n";
    return EXIT_FAILURE;
  }
  if (max_pixel_delta(disabled_raw_normalization_pixels, non_raw_reference_pixels) > 1.0e-5) {
    std::cerr << "camera normalization fields should be inert when the input transform is disabled\n";
    return EXIT_FAILURE;
  }

  auto tungsten_profile_state = dcp_profile_state;
  tungsten_profile_state.color_pipeline.input_transform_recipe.has_source_white_balance = true;
  tungsten_profile_state.color_pipeline.input_transform_recipe.source_temperature = 2850.0;
  auto daylight_profile_state = dcp_profile_state;
  daylight_profile_state.color_pipeline.input_transform_recipe.has_source_white_balance = true;
  daylight_profile_state.color_pipeline.input_transform_recipe.source_temperature = 6500.0;
  std::vector<photo_editor::pipeline::ColorPixel> tungsten_profile_pixels = {
      {.red = 0.42F, .green = 0.30F, .blue = 0.18F, .alpha = 1.0F},
  };
  std::vector<photo_editor::pipeline::ColorPixel> daylight_profile_pixels = tungsten_profile_pixels;
  photo_editor::pipeline::apply_color_adjustments(
      &tungsten_profile_pixels,
      1,
      1,
      tungsten_profile_state);
  photo_editor::pipeline::apply_color_adjustments(
      &daylight_profile_pixels,
      1,
      1,
      daylight_profile_state);
  const auto dual_illuminant_delta = std::max({
      std::abs(tungsten_profile_pixels[0].red - daylight_profile_pixels[0].red),
      std::abs(tungsten_profile_pixels[0].green - daylight_profile_pixels[0].green),
      std::abs(tungsten_profile_pixels[0].blue - daylight_profile_pixels[0].blue),
  });
  if (dual_illuminant_delta <= 1.0e-4F) {
    std::cerr << "dual-illuminant DCP profiles should interpolate by RAW source temperature\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState perceptual_state;
  perceptual_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  perceptual_state.color_pipeline.render_intent = photo_editor::pipeline::RenderIntent::Perceptual;
  perceptual_state.saturation = 100.0;
  perceptual_state.vibrance = 100.0;
  std::vector<photo_editor::pipeline::ColorPixel> perceptual_pixels = {
      {.red = 1.0F, .green = 0.15F, .blue = 0.02F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&perceptual_pixels, 1, 1, perceptual_state);

  auto relative_state = perceptual_state;
  relative_state.color_pipeline.render_intent = photo_editor::pipeline::RenderIntent::RelativeColorimetric;
  std::vector<photo_editor::pipeline::ColorPixel> relative_pixels = {
      {.red = 1.0F, .green = 0.15F, .blue = 0.02F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&relative_pixels, 1, 1, relative_state);
  const auto intent_delta = std::max({
      std::abs(perceptual_pixels[0].red - relative_pixels[0].red),
      std::abs(perceptual_pixels[0].green - relative_pixels[0].green),
      std::abs(perceptual_pixels[0].blue - relative_pixels[0].blue),
  });
  if (intent_delta <= 1.0e-5F) {
    std::cerr << "render intent should affect gamut fitting for saturated wide-gamut edits\n";
    return EXIT_FAILURE;
  }
  const auto perceptual_intent_delta = photo_editor::pipeline::perceptual_delta(
      perceptual_pixels[0],
      relative_pixels[0],
      perceptual_state.color_pipeline.working_profile.color_space);
  if (perceptual_intent_delta <= 1.0e-4) {
    std::cerr << "render intent differences should be visible in perceptual delta space\n";
    return EXIT_FAILURE;
  }
  auto clip_policy_state = perceptual_state;
  clip_policy_state.color_pipeline.output_gamut_mapping_policy =
      photo_editor::pipeline::GamutMappingPolicy::Clip;
  std::vector<photo_editor::pipeline::ColorPixel> clip_policy_pixels = {
      {.red = 1.0F, .green = 0.15F, .blue = 0.02F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&clip_policy_pixels, 1, 1, clip_policy_state);
  const auto clip_policy_delta = photo_editor::pipeline::perceptual_delta(
      perceptual_pixels[0],
      clip_policy_pixels[0],
      perceptual_state.color_pipeline.working_profile.color_space);
  if (clip_policy_delta <= 1.0e-4) {
    std::cerr << "output gamut mapping policy should affect saturated wide-gamut edits\n";
    return EXIT_FAILURE;
  }
  auto disabled_gamut_state = perceptual_state;
  disabled_gamut_state.color_pipeline.output_gamut_mapping_enabled = false;
  std::vector<photo_editor::pipeline::ColorPixel> disabled_gamut_pixels = {
      {.red = 1.0F, .green = 0.15F, .blue = 0.02F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &disabled_gamut_pixels,
      1,
      1,
      disabled_gamut_state);
  if (photo_editor::pipeline::perceptual_delta(
          clip_policy_pixels[0],
          disabled_gamut_pixels[0],
          perceptual_state.color_pipeline.working_profile.color_space) > 1.0e-6) {
    std::cerr << "disabled output gamut mapping should match explicit clipping\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::perceptual_delta(
          perceptual_pixels[0],
          perceptual_pixels[0],
          perceptual_state.color_pipeline.working_profile.color_space) > 1.0e-9) {
    std::cerr << "perceptual delta should be zero for identical colors\n";
    return EXIT_FAILURE;
  }

  auto output_transform_config = photo_editor::pipeline::default_color_pipeline_config();
  output_transform_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear Display P3");
  output_transform_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  output_transform_config.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("Display P3");
  const photo_editor::pipeline::ColorPixel p3_green{
      .red = 0.0F,
      .green = 1.0F,
      .blue = 0.0F,
      .alpha = 1.0F,
  };
  const auto srgb_output_transform = photo_editor::pipeline::apply_output_transform(
      p3_green,
      output_transform_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  const auto display_p3_output_transform = photo_editor::pipeline::apply_output_transform(
      p3_green,
      output_transform_config,
      photo_editor::pipeline::GamutWarningTarget::ExportOutput);
  if (srgb_output_transform.output_profile_name != "sRGB" ||
      !srgb_output_transform.source_out_of_gamut ||
      srgb_output_transform.max_channel_excursion <= 1.0e-4 ||
      srgb_output_transform.pixel.green < 0.99F ||
      display_p3_output_transform.output_profile_name != "Display P3" ||
      display_p3_output_transform.source_out_of_gamut ||
      std::abs(display_p3_output_transform.pixel.green - 1.0F) > 1.0e-5F) {
    std::cerr << "output transform should convert working pixels into explicit sRGB and Display P3 targets\n";
    return EXIT_FAILURE;
  }

  struct ProfileRoundTripCase final {
    const char* working_profile;
    const char* output_profile;
  };
  for (const auto& profile_case : std::vector<ProfileRoundTripCase>{
           {"Linear sRGB", "sRGB"},
           {"Linear Display P3", "Display P3"},
           {"Linear Rec.2020", "Rec.2020"},
           {"Linear ProPhoto RGB", "ProPhoto RGB"},
       }) {
    auto identity_transform_config = photo_editor::pipeline::default_color_pipeline_config();
    identity_transform_config.working_profile =
        photo_editor::pipeline::working_color_profile_from_name(profile_case.working_profile);
    identity_transform_config.export_output_profile =
        photo_editor::pipeline::output_color_profile_from_name(profile_case.output_profile);
    identity_transform_config.render_intent =
        photo_editor::pipeline::RenderIntent::RelativeColorimetric;
    identity_transform_config.output_gamut_mapping_enabled = false;
    identity_transform_config.black_point_compensation_enabled = false;
    identity_transform_config.output_headroom = 1.0;
    const photo_editor::pipeline::ColorPixel profile_sample = {
        .red = 0.18F,
        .green = 0.42F,
        .blue = 0.74F,
        .alpha = 0.65F,
    };
    const auto round_trip = photo_editor::pipeline::apply_output_transform(
        profile_sample,
        identity_transform_config,
        photo_editor::pipeline::GamutWarningTarget::ExportOutput);
    if (round_trip.output_profile_name != profile_case.output_profile ||
        round_trip.source_out_of_gamut ||
        std::abs(round_trip.pixel.red - profile_sample.red) > 1.0e-5F ||
        std::abs(round_trip.pixel.green - profile_sample.green) > 1.0e-5F ||
        std::abs(round_trip.pixel.blue - profile_sample.blue) > 1.0e-5F ||
        std::abs(round_trip.pixel.alpha - profile_sample.alpha) > 1.0e-6F) {
      std::cerr << "same-profile output transform should round-trip within 1e-5 for "
                << profile_case.working_profile << " -> "
                << profile_case.output_profile << '\n';
      return EXIT_FAILURE;
    }
  }

  for (const auto* profile_name : {"Rec.2020", "ProPhoto RGB"}) {
    auto profile_transform_config = output_transform_config;
    profile_transform_config.export_output_profile =
        photo_editor::pipeline::output_color_profile_from_name(profile_name);
    const auto profile_output = photo_editor::pipeline::apply_output_transform(
        p3_green,
        profile_transform_config,
        photo_editor::pipeline::GamutWarningTarget::ExportOutput);
    if (profile_output.output_profile_name != profile_name ||
        !std::isfinite(profile_output.pixel.red) ||
        !std::isfinite(profile_output.pixel.green) ||
        !std::isfinite(profile_output.pixel.blue) ||
        profile_output.pixel.red < -1.0e-6F ||
        profile_output.pixel.green < -1.0e-6F ||
        profile_output.pixel.blue < -1.0e-6F ||
        profile_output.pixel.red > 1.0001F ||
        profile_output.pixel.green > 1.0001F ||
        profile_output.pixel.blue > 1.0001F) {
      std::cerr << "output transform should support bounded Rec.2020 and ProPhoto RGB targets\n";
      return EXIT_FAILURE;
    }
  }

  auto output_intent_config = photo_editor::pipeline::default_color_pipeline_config();
  output_intent_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  output_intent_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  output_intent_config.output_headroom = 1.0;
  output_intent_config.render_intent = photo_editor::pipeline::RenderIntent::Perceptual;
  const photo_editor::pipeline::ColorPixel overrange_srgb{
      .red = 1.40F,
      .green = 0.70F,
      .blue = 0.35F,
      .alpha = 1.0F,
  };
  const auto perceptual_output_transform = photo_editor::pipeline::apply_output_transform(
      overrange_srgb,
      output_intent_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  output_intent_config.render_intent =
      photo_editor::pipeline::RenderIntent::RelativeColorimetric;
  const auto relative_output_transform = photo_editor::pipeline::apply_output_transform(
      overrange_srgb,
      output_intent_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (perceptual_output_transform.pixel.red > 1.0001F ||
      relative_output_transform.pixel.red > 1.0001F ||
      std::abs(perceptual_output_transform.pixel.green - relative_output_transform.pixel.green) <=
          0.05F ||
      perceptual_output_transform.pixel.green >= relative_output_transform.pixel.green) {
    std::cerr << "output transform should distinguish perceptual compression from relative colorimetric clipping\n";
    return EXIT_FAILURE;
  }

  auto output_bpc_config = photo_editor::pipeline::default_color_pipeline_config();
  output_bpc_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  output_bpc_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  output_bpc_config.black_point_compensation_enabled = true;
  output_bpc_config.source_black_point = 0.0;
  output_bpc_config.output_black_point = 0.04;
  const auto compensated_output_transform = photo_editor::pipeline::apply_output_transform(
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.5F},
      output_bpc_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (compensated_output_transform.pixel.red < 0.039F ||
      compensated_output_transform.pixel.green < 0.039F ||
      compensated_output_transform.pixel.blue < 0.039F ||
      std::abs(compensated_output_transform.pixel.alpha - 0.5F) > 1.0e-6F) {
    std::cerr << "output transform should apply black-point compensation in the final target stage\n";
    return EXIT_FAILURE;
  }

  const auto hue_for_rgb = [](
      const photo_editor::pipeline::ColorPixel& pixel) {
    const auto red = std::max(0.0, static_cast<double>(pixel.red));
    const auto green = std::max(0.0, static_cast<double>(pixel.green));
    const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
    const auto maximum = std::max({red, green, blue});
    const auto minimum = std::min({red, green, blue});
    const auto delta = maximum - minimum;
    if (delta <= 1.0e-9) {
      return -1.0;
    }
    auto hue = 0.0;
    if (maximum == red) {
      hue = (green - blue) / delta + (green < blue ? 6.0 : 0.0);
    } else if (maximum == green) {
      hue = ((blue - red) / delta) + 2.0;
    } else {
      hue = ((red - green) / delta) + 4.0;
    }
    return hue / 6.0;
  };
  const auto hue_distance = [](double left, double right) {
    auto distance = std::abs(left - right);
    return std::min(distance, 1.0 - distance);
  };
  const auto normalized_chroma = [](
      const photo_editor::pipeline::ColorPixel& pixel) {
    const auto red = std::max(0.0, static_cast<double>(pixel.red));
    const auto green = std::max(0.0, static_cast<double>(pixel.green));
    const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
    const auto maximum = std::max({red, green, blue});
    const auto minimum = std::min({red, green, blue});
    return maximum <= 1.0e-9 ? 0.0 : (maximum - minimum) / maximum;
  };
  auto compression_config = photo_editor::pipeline::default_color_pipeline_config();
  compression_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  compression_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  compression_config.output_headroom = 1.0;
  compression_config.render_intent = photo_editor::pipeline::RenderIntent::Perceptual;
  const std::vector<photo_editor::pipeline::ColorPixel> saturated_patches = {
      {.red = 1.50F, .green = 0.42F, .blue = 0.12F, .alpha = 1.0F},
      {.red = 0.20F, .green = 1.42F, .blue = 0.52F, .alpha = 1.0F},
      {.red = 0.16F, .green = 0.48F, .blue = 1.36F, .alpha = 1.0F},
      {.red = 1.35F, .green = 0.24F, .blue = 1.48F, .alpha = 1.0F},
  };
  for (const auto& patch : saturated_patches) {
    const auto source_hue = hue_for_rgb(patch);
    const auto source_chroma = normalized_chroma(patch);
    const auto compressed = photo_editor::pipeline::apply_output_transform(
        patch,
        compression_config,
        photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    auto clipped_config = compression_config;
    clipped_config.render_intent =
        photo_editor::pipeline::RenderIntent::RelativeColorimetric;
    const auto clipped = photo_editor::pipeline::apply_output_transform(
        patch,
        clipped_config,
        photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    if (!compressed.source_out_of_gamut ||
        hue_distance(source_hue, hue_for_rgb(compressed.pixel)) > 0.015 ||
        normalized_chroma(compressed.pixel) + 0.02 < source_chroma ||
        normalized_chroma(compressed.pixel) < normalized_chroma(clipped.pixel)) {
      std::cerr << "perceptual output compression should preserve saturated-patch hue and chroma better than clipping\n";
      return EXIT_FAILURE;
    }
  }

  const auto small_delta = photo_editor::pipeline::perceptual_delta(
      {.red = 0.50F, .green = 0.50F, .blue = 0.50F, .alpha = 1.0F},
      {.red = 0.505F, .green = 0.50F, .blue = 0.50F, .alpha = 1.0F},
      photo_editor::pipeline::make_linear_srgb_color_space());
  const auto large_delta = photo_editor::pipeline::perceptual_delta(
      {.red = 0.50F, .green = 0.50F, .blue = 0.50F, .alpha = 1.0F},
      {.red = 0.80F, .green = 0.25F, .blue = 0.10F, .alpha = 1.0F},
      photo_editor::pipeline::make_linear_srgb_color_space());
  if (!(small_delta > 0.0 && large_delta > small_delta * 8.0)) {
    std::cerr << "perceptual delta should scale with visible color differences\n";
    return EXIT_FAILURE;
  }

  auto gamut_config = photo_editor::pipeline::default_color_pipeline_config();
  gamut_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear Display P3");
  gamut_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  gamut_config.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("Display P3");
  const std::vector<photo_editor::pipeline::ColorPixel> display_p3_pixels = {
      {.red = 0.0F, .green = 1.0F, .blue = 0.0F, .alpha = 1.0F},
      {.red = 0.4F, .green = 0.5F, .blue = 0.2F, .alpha = 0.0F},
  };
  const auto display_warning = photo_editor::pipeline::analyze_gamut_warnings(
      display_p3_pixels,
      gamut_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (!display_warning.has_warnings() || display_warning.tested_pixels != 1 ||
      display_warning.out_of_gamut_pixels != 1 ||
      display_warning.out_of_gamut_fraction < 0.99 ||
      display_warning.max_channel_excursion <= 1.0e-4) {
    std::cerr << "display gamut warning analysis should flag Display P3 colors outside sRGB\n";
    return EXIT_FAILURE;
  }
  auto transform_warning_count = std::size_t{0};
  auto transform_max_excursion = 0.0;
  auto transform_tested_pixels = std::size_t{0};
  for (const auto& pixel : display_p3_pixels) {
    if (pixel.alpha <= 1.0e-6F) {
      continue;
    }
    ++transform_tested_pixels;
    const auto output = photo_editor::pipeline::apply_output_transform(
        pixel,
        gamut_config,
        photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    if (output.source_out_of_gamut) {
      ++transform_warning_count;
      transform_max_excursion =
          std::max(transform_max_excursion, output.max_channel_excursion);
    }
  }
  if (display_warning.tested_pixels != transform_tested_pixels ||
      display_warning.out_of_gamut_pixels != transform_warning_count ||
      std::abs(display_warning.max_channel_excursion - transform_max_excursion) > 1.0e-9) {
    std::cerr << "gamut diagnostics should match final output-transform decisions pixel for pixel\n";
    return EXIT_FAILURE;
  }
  const auto export_warning = photo_editor::pipeline::analyze_gamut_warnings(
      display_p3_pixels,
      gamut_config,
      photo_editor::pipeline::GamutWarningTarget::ExportOutput);
  if (export_warning.has_warnings()) {
    std::cerr << "export gamut warning analysis should not flag pixels inside the export profile\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState sdr_tone_map_state;
  sdr_tone_map_state.color_pipeline.output_headroom = 1.0;
  std::vector<photo_editor::pipeline::ColorPixel> sdr_hdr_pixels = {
      {.red = 3.5F, .green = 3.5F, .blue = 3.5F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&sdr_hdr_pixels, 1, 1, sdr_tone_map_state);
  if (sdr_hdr_pixels[0].red > 1.0001F || sdr_hdr_pixels[0].green > 1.0001F ||
      sdr_hdr_pixels[0].blue > 1.0001F || sdr_hdr_pixels[0].red < 0.90F) {
    std::cerr << "SDR tone mapping should compress over-range HDR luminance into SDR headroom\n";
    return EXIT_FAILURE;
  }

  auto hdr_tone_map_state = sdr_tone_map_state;
  hdr_tone_map_state.color_pipeline.output_headroom = 2.0;
  std::vector<photo_editor::pipeline::ColorPixel> hdr_pixels = {
      {.red = 3.5F, .green = 3.5F, .blue = 3.5F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&hdr_pixels, 1, 1, hdr_tone_map_state);
  if (hdr_pixels[0].red <= sdr_hdr_pixels[0].red + 0.25F) {
    std::cerr << "HDR tone mapping should preserve extra output headroom\n";
    return EXIT_FAILURE;
  }
  if (hdr_pixels[0].red > 2.01F) {
    std::cerr << "HDR tone mapping should compress highlights into configured headroom\n";
    return EXIT_FAILURE;
  }
  if (std::abs(hdr_pixels[0].red - hdr_pixels[0].green) > 1.0e-6F ||
      std::abs(hdr_pixels[0].green - hdr_pixels[0].blue) > 1.0e-6F) {
    std::cerr << "HDR tone mapping should preserve neutral channel balance\n";
    return EXIT_FAILURE;
  }

  const auto hdr_lut = photo_editor::pipeline::build_color_lut(hdr_tone_map_state);
  if (!hdr_lut.valid || std::abs(hdr_lut.shoulder_headroom - 2.0) > 1.0e-9 ||
      hdr_lut.shoulder_threshold > hdr_lut.shoulder_headroom) {
    std::cerr << "HDR LUT should carry the configured output headroom into tone mapping\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState optical_headroom_state;
  optical_headroom_state.color_pipeline.output_headroom = 2.0;
  optical_headroom_state.lens_vignette = 65.0;
  std::vector<photo_editor::pipeline::ColorPixel> optical_headroom_pixels(
      9,
      {.red = 1.18F, .green = 1.18F, .blue = 1.18F, .alpha = 1.0F});
  photo_editor::pipeline::apply_color_adjustments(
      &optical_headroom_pixels, 3, 3, optical_headroom_state);
  if (optical_headroom_pixels[0].red <= 1.0001F ||
      optical_headroom_pixels[0].green <= 1.0001F ||
      optical_headroom_pixels[0].blue <= 1.0001F ||
      optical_headroom_pixels[0].red > 2.01F) {
    std::cerr << "optical corrections should not clamp scene-referred HDR pixels before output transform\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState mixer_headroom_state;
  mixer_headroom_state.color_pipeline.output_headroom = 2.0;
  mixer_headroom_state.rgb_channel_mixer.red_from_green = 18.0;
  std::vector<photo_editor::pipeline::ColorPixel> mixer_headroom_pixels = {
      {.red = 1.12F, .green = 0.72F, .blue = 0.38F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &mixer_headroom_pixels, 1, 1, mixer_headroom_state);
  if (mixer_headroom_pixels[0].red <= 1.12F ||
      mixer_headroom_pixels[0].red > 2.01F ||
      mixer_headroom_pixels[0].green < 0.70F) {
    std::cerr << "RGB color mixer should preserve over-range scene values without upper clipping\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState grading_headroom_state;
  grading_headroom_state.color_pipeline.output_headroom = 2.0;
  grading_headroom_state.grading_highlights_saturation = 22.0;
  grading_headroom_state.grading_highlights_luminance = 12.0;
  std::vector<photo_editor::pipeline::ColorPixel> grading_headroom_pixels = {
      {.red = 1.22F, .green = 1.10F, .blue = 0.92F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &grading_headroom_pixels, 1, 1, grading_headroom_state);
  if (grading_headroom_pixels[0].red <= 1.0001F ||
      std::max({grading_headroom_pixels[0].red,
                grading_headroom_pixels[0].green,
                grading_headroom_pixels[0].blue}) > 2.01F) {
    std::cerr << "color grading should preserve extended headroom without forcing SDR clipping\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState layer_headroom_state;
  layer_headroom_state.color_pipeline.output_headroom = 2.0;
  photo_editor::pipeline::PhotoLayer headroom_layer;
  headroom_layer.kind = "adjustment";
  headroom_layer.blend_mode = "normal";
  headroom_layer.exposure = 1.0;
  layer_headroom_state.photo_layers.push_back(headroom_layer);
  std::vector<photo_editor::pipeline::ColorPixel> layer_headroom_pixels = {
      {.red = 0.72F, .green = 0.68F, .blue = 0.62F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &layer_headroom_pixels, 1, 1, layer_headroom_state);
  if (layer_headroom_pixels[0].red <= 1.0001F ||
      layer_headroom_pixels[0].red > 2.01F ||
      layer_headroom_pixels[0].green <= 1.0001F) {
    std::cerr << "normal adjustment layers should preserve over-range scene-referred pixels\n";
    return EXIT_FAILURE;
  }

  auto render_fill_blend = [](
      const photo_editor::pipeline::ColorPixel& base,
      const photo_editor::pipeline::ColorPixel& fill,
      const std::string& blend_mode) {
    photo_editor::pipeline::ColorAdjustmentState state;
    state.color_pipeline.output_headroom = 2.0;
    photo_editor::pipeline::PhotoLayer layer;
    layer.kind = "fill";
    layer.blend_mode = blend_mode;
    layer.fill_color = fill;
    state.photo_layers.push_back(layer);
    std::vector<photo_editor::pipeline::ColorPixel> pixels = {base};
    photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
    return pixels[0];
  };

  const auto multiply_blend = render_fill_blend(
      {.red = 1.40F, .green = 1.40F, .blue = 1.40F, .alpha = 1.0F},
      {.red = 0.80F, .green = 0.80F, .blue = 0.80F, .alpha = 1.0F},
      "multiply");
  if (multiply_blend.red <= 1.0001F ||
      std::abs(multiply_blend.red - multiply_blend.green) > 1.0e-5F ||
      std::abs(multiply_blend.green - multiply_blend.blue) > 1.0e-5F) {
    std::cerr << "multiply layer blend should operate in linear scene values and preserve HDR neutral balance\n";
    return EXIT_FAILURE;
  }

  const auto overlay_blend = render_fill_blend(
      {.red = 1.40F, .green = 1.18F, .blue = 0.82F, .alpha = 1.0F},
      {.red = 1.50F, .green = 0.96F, .blue = 0.60F, .alpha = 1.0F},
      "overlay");
  if (overlay_blend.red <= 1.0001F ||
      overlay_blend.red > 2.01F ||
      overlay_blend.red <= overlay_blend.green) {
    std::cerr << "overlay layer blend should normalize against output headroom instead of SDR clipping\n";
    return EXIT_FAILURE;
  }

  const auto color_blend = render_fill_blend(
      {.red = 1.30F, .green = 1.18F, .blue = 0.96F, .alpha = 1.0F},
      {.red = 0.10F, .green = 0.30F, .blue = 1.55F, .alpha = 1.0F},
      "color");
  if (std::max({color_blend.red, color_blend.green, color_blend.blue}) <= 1.0001F ||
      color_blend.blue <= color_blend.red ||
      color_blend.blue > 2.01F) {
    std::cerr << "color layer blend should use OKLab hue/chroma while preserving extended lightness\n";
    return EXIT_FAILURE;
  }

  const auto parity_source = photo_editor::pipeline::ColorPixel{
      .red = 0.34F,
      .green = 0.47F,
      .blue = 0.63F,
      .alpha = 1.0F,
  };
  const auto apply_single_pixel_state =
      [&](const photo_editor::pipeline::ColorAdjustmentState& state) {
        std::vector<photo_editor::pipeline::ColorPixel> pixels = {parity_source};
        photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
        return pixels[0];
      };
  auto configure_equivalent_edit =
      [](auto* target) {
        target->exposure = 0.45;
        target->contrast = 18.0;
        target->highlights = -12.0;
        target->shadows = 16.0;
        target->temperature = 18.0;
        target->tint = -9.0;
        target->vibrance = 14.0;
        target->saturation = 9.0;
      };

  photo_editor::pipeline::ColorAdjustmentState global_parity_state;
  global_parity_state.color_pipeline.output_headroom = 2.0;
  configure_equivalent_edit(&global_parity_state);
  const auto global_parity_pixel = apply_single_pixel_state(global_parity_state);

  photo_editor::pipeline::ColorAdjustmentState local_parity_state;
  local_parity_state.color_pipeline.output_headroom = 2.0;
  photo_editor::pipeline::LocalMask full_local_mask;
  full_local_mask.kind = "brush";
  full_local_mask.operation = "add";
  full_local_mask.segmentation_width = 1;
  full_local_mask.segmentation_height = 1;
  full_local_mask.segmentation_mask = {0xff};
  configure_equivalent_edit(&full_local_mask);
  local_parity_state.local_masks.push_back(full_local_mask);
  const auto local_parity_pixel = apply_single_pixel_state(local_parity_state);

  photo_editor::pipeline::ColorAdjustmentState layer_parity_state;
  layer_parity_state.color_pipeline.output_headroom = 2.0;
  photo_editor::pipeline::PhotoLayer parity_layer;
  parity_layer.kind = "adjustment";
  parity_layer.blend_mode = "normal";
  parity_layer.opacity = 1.0;
  configure_equivalent_edit(&parity_layer);
  layer_parity_state.photo_layers.push_back(parity_layer);
  const auto layer_parity_pixel = apply_single_pixel_state(layer_parity_state);

  const auto max_float_delta = [](
      const photo_editor::pipeline::ColorPixel& left,
      const photo_editor::pipeline::ColorPixel& right) {
    return std::max({
        std::abs(left.red - right.red),
        std::abs(left.green - right.green),
        std::abs(left.blue - right.blue),
    });
  };
  if (max_float_delta(global_parity_pixel, local_parity_pixel) > 0.035F ||
      max_float_delta(global_parity_pixel, layer_parity_pixel) > 0.035F) {
    std::cerr << "global, full-mask local, and normal adjustment-layer color edits should match within tolerance\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState black_point_state;
  black_point_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  black_point_state.color_pipeline.output_headroom = 1.0;
  black_point_state.color_pipeline.black_point_compensation_enabled = true;
  black_point_state.color_pipeline.source_black_point = 0.0;
  black_point_state.color_pipeline.output_black_point = 0.04;
  std::vector<photo_editor::pipeline::ColorPixel> compensated_black_points = {
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F},
      {.red = 0.02F, .green = 0.02F, .blue = 0.02F, .alpha = 1.0F},
      {.red = 0.50F, .green = 0.50F, .blue = 0.50F, .alpha = 1.0F},
      {.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 0.5F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &compensated_black_points, 4, 1, black_point_state);
  if (compensated_black_points[0].red < 0.039F ||
      compensated_black_points[1].red <= compensated_black_points[0].red ||
      compensated_black_points[2].red <= compensated_black_points[1].red ||
      std::abs(compensated_black_points[3].red - 1.0F) > 1.0e-5F ||
      std::abs(compensated_black_points[3].alpha - 0.5F) > 1.0e-6F) {
    std::cerr << "black-point compensation should lift shadows, preserve order, and pin white\n";
    return EXIT_FAILURE;
  }

  auto disabled_black_point_state = black_point_state;
  disabled_black_point_state.color_pipeline.black_point_compensation_enabled = false;
  std::vector<photo_editor::pipeline::ColorPixel> disabled_black_points = {
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &disabled_black_points, 1, 1, disabled_black_point_state);
  if (disabled_black_points[0].red > 1.0e-6F ||
      disabled_black_points[0].green > 1.0e-6F ||
      disabled_black_points[0].blue > 1.0e-6F) {
    std::cerr << "disabled black-point compensation should not remap output black\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState raw_override_state;
  raw_override_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  raw_override_state.input_transform.raw_input_transform_enabled = true;
  raw_override_state.input_transform.raw_white_balance_override_enabled = true;
  raw_override_state.input_transform.raw_white_balance_temperature = 7800.0;
  raw_override_state.input_transform.raw_white_balance_tint = 25.0;
  std::vector<photo_editor::pipeline::ColorPixel> raw_override_pixels = {
      {.red = 0.55F, .green = 0.55F, .blue = 0.55F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&raw_override_pixels, 1, 1, raw_override_state);

  auto slider_white_balance_state = raw_override_state;
  slider_white_balance_state.input_transform.raw_white_balance_override_enabled = false;
  slider_white_balance_state.input_transform.temperature = 0.0;
  slider_white_balance_state.input_transform.tint = 0.0;
  std::vector<photo_editor::pipeline::ColorPixel> slider_white_balance_pixels = {
      {.red = 0.55F, .green = 0.55F, .blue = 0.55F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &slider_white_balance_pixels, 1, 1, slider_white_balance_state);
  const auto raw_override_delta = std::max({
      std::abs(raw_override_pixels[0].red - slider_white_balance_pixels[0].red),
      std::abs(raw_override_pixels[0].green - slider_white_balance_pixels[0].green),
      std::abs(raw_override_pixels[0].blue - slider_white_balance_pixels[0].blue),
  });
  if (raw_override_delta <= 1.0e-5F) {
    std::cerr << "raw white balance override should take precedence over neutral slider white balance\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState raw_level_state;
  raw_level_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  raw_level_state.input_transform.raw_input_transform_enabled = true;
  raw_level_state.input_transform.raw_black_level = 0.10;
  raw_level_state.input_transform.raw_white_level = 0.90;
  std::vector<photo_editor::pipeline::ColorPixel> raw_level_baseline_pixels = {
      {.red = 0.10F, .green = 0.50F, .blue = 0.90F, .alpha = 1.0F},
  };
  std::vector<photo_editor::pipeline::ColorPixel> raw_level_pixels = {
      {.red = 0.10F, .green = 0.50F, .blue = 0.90F, .alpha = 1.0F},
  };
  auto raw_level_baseline_state = raw_level_state;
  raw_level_baseline_state.input_transform.raw_black_level = 0.0;
  raw_level_baseline_state.input_transform.raw_white_level = 1.0;
  photo_editor::pipeline::apply_color_adjustments(
      &raw_level_baseline_pixels,
      1,
      1,
      raw_level_baseline_state);
  photo_editor::pipeline::apply_color_adjustments(&raw_level_pixels, 1, 1, raw_level_state);
  if (raw_level_pixels[0].red >= raw_level_baseline_pixels[0].red ||
      raw_level_pixels[0].blue <= raw_level_baseline_pixels[0].blue) {
    std::cerr << "raw black and white levels should normalize the input transform range\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState highlight_reconstruction_state;
  highlight_reconstruction_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  highlight_reconstruction_state.input_transform.raw_input_transform_enabled = true;
  highlight_reconstruction_state.input_transform.highlight_recovery_enabled = true;
  highlight_reconstruction_state.input_transform.highlight_reconstruction_mode = "Enabled";
  std::vector<photo_editor::pipeline::ColorPixel> reconstructed_highlights = {
      {.red = 1.8F, .green = 0.30F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &reconstructed_highlights, 1, 1, highlight_reconstruction_state);

  auto disabled_highlight_state = highlight_reconstruction_state;
  disabled_highlight_state.input_transform.highlight_reconstruction_mode = "Disabled";
  std::vector<photo_editor::pipeline::ColorPixel> disabled_highlights = {
      {.red = 1.8F, .green = 0.30F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &disabled_highlights, 1, 1, disabled_highlight_state);
  auto camera_default_highlight_state = highlight_reconstruction_state;
  camera_default_highlight_state.input_transform.highlight_reconstruction_mode = "Camera Default";
  std::vector<photo_editor::pipeline::ColorPixel> camera_default_highlights = {
      {.red = 1.8F, .green = 0.30F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &camera_default_highlights, 1, 1, camera_default_highlight_state);
  if (reconstructed_highlights[0].green <= disabled_highlights[0].green ||
      reconstructed_highlights[0].blue <= disabled_highlights[0].blue) {
    std::cerr << "highlight reconstruction should lift suppressed highlight channels before grading\n";
    return EXIT_FAILURE;
  }
  if (camera_default_highlights[0].green <= disabled_highlights[0].green ||
      camera_default_highlights[0].green >= reconstructed_highlights[0].green ||
      camera_default_highlights[0].blue <= disabled_highlights[0].blue ||
      camera_default_highlights[0].blue >= reconstructed_highlights[0].blue) {
    std::cerr << "highlight reconstruction modes should provide disabled, camera default, and full strengths\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState cancelled_state;
  cancelled_state.exposure = 1.0;
  std::vector<photo_editor::pipeline::ColorPixel> cancelled_pixels = {
      {.red = 0.25F, .green = 0.35F, .blue = 0.45F, .alpha = 1.0F},
  };
  const auto original_cancelled_pixel = cancelled_pixels[0];
  std::atomic<int> latest_generation = 2;
  photo_editor::pipeline::apply_color_adjustments(
      &cancelled_pixels,
      1,
      1,
      cancelled_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      photo_editor::pipeline::CancellationToken{
          .latest_generation = &latest_generation,
          .generation = 1,
      });
  if (std::abs(cancelled_pixels[0].red - original_cancelled_pixel.red) > 1.0e-7F ||
      std::abs(cancelled_pixels[0].green - original_cancelled_pixel.green) > 1.0e-7F ||
      std::abs(cancelled_pixels[0].blue - original_cancelled_pixel.blue) > 1.0e-7F) {
    std::cerr << "cancelled color adjustment work should leave pixels unchanged\n";
    return EXIT_FAILURE;
  }

  const photo_editor::pipeline::ColorPixel layer_opacity_source{
      .red = 0.24F,
      .green = 0.36F,
      .blue = 0.52F,
      .alpha = 1.0F,
  };
  photo_editor::pipeline::PhotoLayer opacity_layer;
  opacity_layer.kind = "adjustment";
  opacity_layer.blend_mode = "normal";
  opacity_layer.exposure = 0.55;
  opacity_layer.contrast = 18.0;
  opacity_layer.vibrance = 26.0;
  opacity_layer.curve_lights = 32.0;
  opacity_layer.luma_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 0.45, .y = 0.32},
      {.x = 1.0, .y = 1.0},
  };
  opacity_layer.grading_highlights_hue = 0.58;
  opacity_layer.grading_highlights_saturation = 34.0;
  opacity_layer.grading_highlights_luminance = 18.0;
  opacity_layer.grading_amount = 100.0;
  const auto render_adjustment_opacity = [&](double opacity) {
    photo_editor::pipeline::ColorAdjustmentState state;
    auto layer = opacity_layer;
    layer.opacity = opacity;
    state.photo_layers.push_back(layer);
    std::vector<photo_editor::pipeline::ColorPixel> pixels = {layer_opacity_source};
    photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
    return pixels[0];
  };
  const auto opacity_zero = render_adjustment_opacity(0.0);
  const auto opacity_half = render_adjustment_opacity(0.5);
  const auto opacity_full = render_adjustment_opacity(1.0);
  const auto midpoint = [](float low, float high) {
    return low + ((high - low) * 0.5F);
  };
  const auto opacity_tolerance = 0.04F;
  if (std::abs(opacity_full.red - opacity_zero.red) < 0.04F ||
      std::abs(opacity_full.green - opacity_zero.green) < 0.04F ||
      std::abs(opacity_full.blue - opacity_zero.blue) < 0.04F ||
      std::abs(opacity_half.red - midpoint(opacity_zero.red, opacity_full.red)) >
          opacity_tolerance ||
      std::abs(opacity_half.green - midpoint(opacity_zero.green, opacity_full.green)) >
          opacity_tolerance ||
      std::abs(opacity_half.blue - midpoint(opacity_zero.blue, opacity_full.blue)) >
          opacity_tolerance) {
    std::cerr << "adjustment layer opacity should gate the full tone/curve/grading result once\n";
    return EXIT_FAILURE;
  }
  const auto render_adjustment_mask_gate = [&](double mask_opacity, double mask_density) {
    photo_editor::pipeline::ColorAdjustmentState state;
    auto layer = opacity_layer;
    layer.opacity = 1.0;
    layer.mask_data = {0xff};
    layer.mask_width = 1;
    layer.mask_height = 1;
    layer.mask_opacity = mask_opacity;
    layer.mask_density = mask_density;
    state.photo_layers.push_back(layer);
    std::vector<photo_editor::pipeline::ColorPixel> pixels = {layer_opacity_source};
    photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
    return pixels[0];
  };
  const auto mask_opacity_half = render_adjustment_mask_gate(0.5, 1.0);
  const auto mask_density_half = render_adjustment_mask_gate(1.0, 0.5);
  const auto mask_gate_tolerance = 0.01F;
  if (std::abs(mask_opacity_half.red - opacity_half.red) > mask_gate_tolerance ||
      std::abs(mask_opacity_half.green - opacity_half.green) > mask_gate_tolerance ||
      std::abs(mask_opacity_half.blue - opacity_half.blue) > mask_gate_tolerance ||
      std::abs(mask_density_half.red - opacity_half.red) > mask_gate_tolerance ||
      std::abs(mask_density_half.green - opacity_half.green) > mask_gate_tolerance ||
      std::abs(mask_density_half.blue - opacity_half.blue) > mask_gate_tolerance) {
    std::cerr << "layer mask opacity and density should gate mask alpha before the layer recipe\n";
    return EXIT_FAILURE;
  }

  const photo_editor::pipeline::ColorPixel layer_effect_source{
      .red = 0.70F,
      .green = 0.24F,
      .blue = 0.10F,
      .alpha = 1.0F,
  };
  photo_editor::pipeline::PhotoLayer effect_layer;
  effect_layer.kind = "adjustment";
  effect_layer.blend_mode = "normal";
  effect_layer.dehaze = 100.0;
  const auto render_layer_effect = [&](double mask_opacity, double mask_density) {
    photo_editor::pipeline::ColorAdjustmentState state;
    auto layer = effect_layer;
    layer.mask_data = {0xff};
    layer.mask_width = 1;
    layer.mask_height = 1;
    layer.mask_opacity = mask_opacity;
    layer.mask_density = mask_density;
    state.photo_layers.push_back(layer);
    std::vector<photo_editor::pipeline::ColorPixel> pixels = {layer_effect_source};
    photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
    return pixels[0];
  };
  const auto effect_none = render_layer_effect(0.0, 1.0);
  const auto effect_half_opacity = render_layer_effect(0.5, 1.0);
  const auto effect_half_density = render_layer_effect(1.0, 0.5);
  const auto effect_full = render_layer_effect(1.0, 1.0);
  if (std::abs(effect_full.red - effect_none.red) < 0.003F ||
      std::abs(effect_half_opacity.red - midpoint(effect_none.red, effect_full.red)) >
          mask_gate_tolerance ||
      std::abs(effect_half_opacity.green - midpoint(effect_none.green, effect_full.green)) >
          mask_gate_tolerance ||
      std::abs(effect_half_opacity.blue - midpoint(effect_none.blue, effect_full.blue)) >
          mask_gate_tolerance ||
      std::abs(effect_half_density.red - effect_half_opacity.red) > mask_gate_tolerance ||
      std::abs(effect_half_density.green - effect_half_opacity.green) > mask_gate_tolerance ||
      std::abs(effect_half_density.blue - effect_half_opacity.blue) > mask_gate_tolerance) {
    std::cerr << "layer-local effects/detail should evaluate before mask opacity and density gate alpha\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::PhotoLayer spatial_effect_layer;
  spatial_effect_layer.kind = "adjustment";
  spatial_effect_layer.blend_mode = "normal";
  spatial_effect_layer.texture = 100.0;
  spatial_effect_layer.clarity = 100.0;
  const std::vector<photo_editor::pipeline::ColorPixel> spatial_effect_source = {
      {.red = 0.22F, .green = 0.24F, .blue = 0.26F, .alpha = 1.0F},
      {.red = 0.31F, .green = 0.33F, .blue = 0.35F, .alpha = 1.0F},
      {.red = 0.68F, .green = 0.70F, .blue = 0.72F, .alpha = 1.0F},
      {.red = 0.34F, .green = 0.36F, .blue = 0.38F, .alpha = 1.0F},
      {.red = 0.24F, .green = 0.26F, .blue = 0.28F, .alpha = 1.0F},
  };
  const auto render_layer_spatial_effect = [&](double mask_opacity, double mask_density) {
    photo_editor::pipeline::ColorAdjustmentState state;
    auto layer = spatial_effect_layer;
    layer.mask_data = {0xff, 0xff, 0xff, 0xff, 0xff};
    layer.mask_width = 5;
    layer.mask_height = 1;
    layer.mask_opacity = mask_opacity;
    layer.mask_density = mask_density;
    state.photo_layers.push_back(layer);
    auto pixels = spatial_effect_source;
    photo_editor::pipeline::apply_color_adjustments(&pixels, 5, 1, state);
    return pixels;
  };
  const auto spatial_effect_none = render_layer_spatial_effect(0.0, 1.0);
  const auto spatial_effect_half_opacity = render_layer_spatial_effect(0.5, 1.0);
  const auto spatial_effect_half_density = render_layer_spatial_effect(1.0, 0.5);
  const auto spatial_effect_full = render_layer_spatial_effect(1.0, 1.0);
  constexpr auto kSpatialEffectCenterIndex = std::size_t{2};
  const auto& spatial_none = spatial_effect_none[kSpatialEffectCenterIndex];
  const auto& spatial_half_opacity = spatial_effect_half_opacity[kSpatialEffectCenterIndex];
  const auto& spatial_half_density = spatial_effect_half_density[kSpatialEffectCenterIndex];
  const auto& spatial_full = spatial_effect_full[kSpatialEffectCenterIndex];
  if (std::abs(spatial_full.red - spatial_none.red) < 0.01F ||
      std::abs(spatial_half_opacity.red - midpoint(spatial_none.red, spatial_full.red)) >
          opacity_tolerance ||
      std::abs(spatial_half_opacity.green - midpoint(spatial_none.green, spatial_full.green)) >
          opacity_tolerance ||
      std::abs(spatial_half_opacity.blue - midpoint(spatial_none.blue, spatial_full.blue)) >
          opacity_tolerance ||
      std::abs(spatial_half_density.red - spatial_half_opacity.red) > mask_gate_tolerance ||
      std::abs(spatial_half_density.green - spatial_half_opacity.green) > mask_gate_tolerance ||
      std::abs(spatial_half_density.blue - spatial_half_opacity.blue) > mask_gate_tolerance) {
    std::cerr << "layer-local texture and clarity should render before mask opacity and density gate alpha\n";
    return EXIT_FAILURE;
  }

  const auto max_rgb_delta = [](const std::vector<photo_editor::pipeline::ColorPixel>& lhs,
                                const std::vector<photo_editor::pipeline::ColorPixel>& rhs) {
    auto max_delta = 0.0F;
    if (lhs.size() != rhs.size()) {
      return 1.0F;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      max_delta = std::max(
          max_delta,
          std::max({
              std::abs(lhs[index].red - rhs[index].red),
              std::abs(lhs[index].green - rhs[index].green),
              std::abs(lhs[index].blue - rhs[index].blue),
          }));
    }
    return max_delta;
  };
  std::vector<photo_editor::pipeline::ColorPixel> layer_detail_source;
  layer_detail_source.reserve(35);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 7; ++x) {
      const auto checker = ((x + y) % 2) == 0 ? 0.10F : -0.08F;
      const auto chroma_noise = ((x * 3 + y * 5) % 4) == 0 ? 0.07F : -0.05F;
      layer_detail_source.push_back({
          .red = 0.22F + static_cast<float>(x) * 0.055F + checker,
          .green = 0.28F + static_cast<float>(y) * 0.062F - checker * 0.45F,
          .blue = 0.34F + static_cast<float>(x + y) * 0.022F + chroma_noise,
          .alpha = 1.0F,
      });
    }
  }
  const auto render_adjustment_detail_layer =
      [&](const photo_editor::pipeline::PhotoLayer& layer) {
        photo_editor::pipeline::ColorAdjustmentState state;
        state.photo_layers.push_back(layer);
        auto pixels = layer_detail_source;
        photo_editor::pipeline::apply_color_adjustments(&pixels, 7, 5, state);
        return pixels;
      };
  photo_editor::pipeline::PhotoLayer neutral_detail_layer;
  neutral_detail_layer.kind = "adjustment";
  neutral_detail_layer.blend_mode = "normal";
  const auto neutral_detail_pixels = render_adjustment_detail_layer(neutral_detail_layer);

  auto sharpen_layer = neutral_detail_layer;
  sharpen_layer.sharpening = 100.0;
  sharpen_layer.sharpen_radius = 1.0;
  sharpen_layer.sharpen_detail = 80.0;
  if (max_rgb_delta(render_adjustment_detail_layer(sharpen_layer), neutral_detail_pixels) <
      0.002F) {
    std::cerr << "adjustment layer sharpening should visibly affect rendered pixels\n";
    return EXIT_FAILURE;
  }

  auto luma_nr_layer = neutral_detail_layer;
  luma_nr_layer.luminance_nr = 100.0;
  if (max_rgb_delta(render_adjustment_detail_layer(luma_nr_layer), neutral_detail_pixels) <
      0.002F) {
    std::cerr << "adjustment layer luminance noise reduction should visibly affect rendered pixels\n";
    return EXIT_FAILURE;
  }

  auto color_nr_layer = neutral_detail_layer;
  color_nr_layer.color_nr = 100.0;
  if (max_rgb_delta(render_adjustment_detail_layer(color_nr_layer), neutral_detail_pixels) <
      0.002F) {
    std::cerr << "adjustment layer color noise reduction should visibly affect rendered pixels\n";
    return EXIT_FAILURE;
  }

  auto lens_layer = neutral_detail_layer;
  lens_layer.lens_vignette = -80.0;
  lens_layer.lens_distortion = 60.0;
  lens_layer.chromatic_aberration = 45.0;
  if (max_rgb_delta(render_adjustment_detail_layer(lens_layer), neutral_detail_pixels) <
      0.002F) {
    std::cerr << "adjustment layer lens corrections should visibly affect rendered pixels\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

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
  if (photo_editor::pipeline::perceptual_delta(
          perceptual_pixels[0],
          perceptual_pixels[0],
          perceptual_state.color_pipeline.working_profile.color_space) > 1.0e-9) {
    std::cerr << "perceptual delta should be zero for identical colors\n";
    return EXIT_FAILURE;
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

  return EXIT_SUCCESS;
}

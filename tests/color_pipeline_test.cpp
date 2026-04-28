#include <cstdlib>
#include <atomic>
#include <cmath>
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
  if (reconstructed_highlights[0].green <= disabled_highlights[0].green ||
      reconstructed_highlights[0].blue <= disabled_highlights[0].blue) {
    std::cerr << "highlight reconstruction should lift suppressed highlight channels before grading\n";
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

namespace {

struct ChartPatch final {
  std::string_view name;
  int red = 0;
  int green = 0;
  int blue = 0;
  std::string_view category = "general";
  double tolerance = 0.0025;
};

constexpr std::array<ChartPatch, 24> kColorCheckerSrgbPatches = {{
    {"Dark Skin", 115, 82, 68, "skin", 0.0025},
    {"Light Skin", 194, 150, 130, "skin", 0.0025},
    {"Blue Sky", 98, 122, 157, "memory-color", 0.0028},
    {"Foliage", 87, 108, 67, "memory-color", 0.0028},
    {"Blue Flower", 133, 128, 177},
    {"Bluish Green", 103, 189, 170},
    {"Orange", 214, 126, 44, "saturated", 0.0030},
    {"Purplish Blue", 80, 91, 166, "saturated", 0.0030},
    {"Moderate Red", 193, 90, 99, "saturated", 0.0030},
    {"Purple", 94, 60, 108},
    {"Yellow Green", 157, 188, 64},
    {"Orange Yellow", 224, 163, 46, "saturated", 0.0030},
    {"Blue", 56, 61, 150, "saturated", 0.0030},
    {"Green", 70, 148, 73, "saturated", 0.0030},
    {"Red", 175, 54, 60, "saturated", 0.0030},
    {"Yellow", 231, 199, 31, "saturated", 0.0030},
    {"Magenta", 187, 86, 149, "saturated", 0.0030},
    {"Cyan", 8, 133, 161, "saturated", 0.0030},
    {"White", 243, 243, 242, "neutral", 0.0015},
    {"Neutral 8", 200, 200, 200, "neutral", 0.0015},
    {"Neutral 6.5", 160, 160, 160, "neutral", 0.0015},
    {"Neutral 5", 122, 122, 121, "neutral", 0.0015},
    {"Neutral 3.5", 85, 85, 85, "neutral", 0.0015},
    {"Black", 52, 52, 52, "neutral", 0.0015},
}};

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::pipeline::ColorPixel patch_to_linear_srgb(const ChartPatch& patch) {
  return photo_editor::pipeline::ColorPixel{
      .red = static_cast<float>(photo_editor::pipeline::srgb_to_linear(
          static_cast<double>(patch.red) / 255.0)),
      .green = static_cast<float>(photo_editor::pipeline::srgb_to_linear(
          static_cast<double>(patch.green) / 255.0)),
      .blue = static_cast<float>(photo_editor::pipeline::srgb_to_linear(
          static_cast<double>(patch.blue) / 255.0)),
      .alpha = 1.0F,
  };
}

double relative_luminance(const photo_editor::pipeline::ColorPixel& pixel) {
  return 0.2126 * static_cast<double>(pixel.red) +
         0.7152 * static_cast<double>(pixel.green) +
         0.0722 * static_cast<double>(pixel.blue);
}

double rgb_hue(const photo_editor::pipeline::ColorPixel& pixel) {
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
}

double hue_distance(double left, double right) {
  const auto distance = std::abs(left - right);
  return std::min(distance, 1.0 - distance);
}

double normalized_chroma(const photo_editor::pipeline::ColorPixel& pixel) {
  const auto red = std::max(0.0, static_cast<double>(pixel.red));
  const auto green = std::max(0.0, static_cast<double>(pixel.green));
  const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  return maximum <= 1.0e-9 ? 0.0 : (maximum - minimum) / maximum;
}

}  // namespace

int main() {
  std::vector<photo_editor::pipeline::ColorPixel> reference_patches;
  reference_patches.reserve(kColorCheckerSrgbPatches.size());
  for (const auto& patch : kColorCheckerSrgbPatches) {
    reference_patches.push_back(patch_to_linear_srgb(patch));
  }

  auto rendered_patches = reference_patches;
  photo_editor::pipeline::ColorAdjustmentState neutral_state;
  neutral_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  neutral_state.color_pipeline.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  neutral_state.color_pipeline.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  photo_editor::pipeline::apply_color_adjustments(
      &rendered_patches,
      6,
      4,
      neutral_state,
      photo_editor::pipeline::RemovalQuality::Final);

  const auto color_space = photo_editor::pipeline::make_linear_srgb_color_space();
  auto max_delta = 0.0;
  auto checked_neutral_tolerances = 0;
  auto checked_skin_tolerances = 0;
  auto checked_saturated_tolerances = 0;
  auto checked_memory_color_tolerances = 0;
  for (std::size_t index = 0; index < reference_patches.size(); ++index) {
    const auto delta = photo_editor::pipeline::perceptual_delta(
        reference_patches[index],
        rendered_patches[index],
        color_space);
    max_delta = std::max(max_delta, delta);
    const auto& patch = kColorCheckerSrgbPatches[index];
    if (patch.category == "neutral") {
      ++checked_neutral_tolerances;
    } else if (patch.category == "skin") {
      ++checked_skin_tolerances;
    } else if (patch.category == "saturated") {
      ++checked_saturated_tolerances;
    } else if (patch.category == "memory-color") {
      ++checked_memory_color_tolerances;
    }
    if (delta > patch.tolerance) {
      std::cerr << "neutral color chart render drifted on patch "
                << patch.name << " category=" << patch.category
                << " tolerance=" << patch.tolerance
                << " perceptual_delta=" << delta << '\n';
      return EXIT_FAILURE;
    }
  }
  if (checked_neutral_tolerances < 6 ||
      checked_skin_tolerances < 2 ||
      checked_saturated_tolerances < 10 ||
      checked_memory_color_tolerances < 2) {
    return fail("chart validation should define neutral, skin, saturated, and memory-color tolerances");
  }

  for (std::size_t index = 18; index + 1 < kColorCheckerSrgbPatches.size(); ++index) {
    const auto current_luminance = relative_luminance(rendered_patches[index]);
    const auto next_luminance = relative_luminance(rendered_patches[index + 1]);
    if (current_luminance <= next_luminance) {
      return fail("neutral color chart patches should remain ordered from white to black");
    }
    const auto reference_chroma_delta = std::max({
        std::abs(reference_patches[index].red - reference_patches[index].green),
        std::abs(reference_patches[index].green - reference_patches[index].blue),
        std::abs(reference_patches[index].red - reference_patches[index].blue),
    });
    const auto rendered_chroma_delta = std::max({
        std::abs(rendered_patches[index].red - rendered_patches[index].green),
        std::abs(rendered_patches[index].green - rendered_patches[index].blue),
        std::abs(rendered_patches[index].red - rendered_patches[index].blue),
    });
    if (rendered_chroma_delta > reference_chroma_delta + 0.001F) {
      return fail("neutral color chart patches should remain visually neutral");
    }
  }

  auto exposure_patches = reference_patches;
  auto exposure_state = neutral_state;
  exposure_state.exposure = 0.35;
  exposure_state.contrast = 12.0;
  photo_editor::pipeline::apply_color_adjustments(
      &exposure_patches,
      6,
      4,
      exposure_state,
      photo_editor::pipeline::RemovalQuality::Final);

  auto total_exposure_delta = 0.0;
  for (std::size_t index = 0; index < reference_patches.size(); ++index) {
    total_exposure_delta += photo_editor::pipeline::perceptual_delta(
        reference_patches[index],
        exposure_patches[index],
        color_space);
  }
  if (total_exposure_delta <= 0.20) {
    return fail("color chart validation should catch a visible global tone transform");
  }

  auto compression_config = photo_editor::pipeline::default_color_pipeline_config();
  compression_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  compression_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  compression_config.output_headroom = 1.0;
  compression_config.render_intent = photo_editor::pipeline::RenderIntent::Perceptual;
  auto checked_chromatic_patches = 0;
  auto max_hue_drift = 0.0;
  auto max_chroma_loss = 0.0;
  for (std::size_t index = 0; index < 18; ++index) {
    const auto source_hue = rgb_hue(reference_patches[index]);
    const auto source_chroma = normalized_chroma(reference_patches[index]);
    if (source_hue < 0.0 || source_chroma < 0.10) {
      continue;
    }
    auto pushed = reference_patches[index];
    pushed.red *= 2.4F;
    pushed.green *= 2.4F;
    pushed.blue *= 2.4F;
    if (std::max({pushed.red, pushed.green, pushed.blue}) <= 1.0F) {
      continue;
    }
    const auto compressed = photo_editor::pipeline::apply_output_transform(
        pushed,
        compression_config,
        photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    if (!compressed.source_out_of_gamut) {
      std::cerr << "chart compression fixture should exceed output gamut on patch "
                << kColorCheckerSrgbPatches[index].name << '\n';
      return EXIT_FAILURE;
    }
    const auto compressed_hue = rgb_hue(compressed.pixel);
    const auto compressed_chroma = normalized_chroma(compressed.pixel);
    const auto drift = hue_distance(source_hue, compressed_hue);
    const auto chroma_loss = std::max(0.0, source_chroma - compressed_chroma);
    max_hue_drift = std::max(max_hue_drift, drift);
    max_chroma_loss = std::max(max_chroma_loss, chroma_loss);
    if (drift > 0.020 || chroma_loss > 0.035) {
      std::cerr << "perceptual gamut compression drifted chart patch "
                << kColorCheckerSrgbPatches[index].name
                << " hue=" << drift
                << " chromaLoss=" << chroma_loss << '\n';
      return EXIT_FAILURE;
    }
    ++checked_chromatic_patches;
  }
  if (checked_chromatic_patches < 10 || max_hue_drift > 0.020 || max_chroma_loss > 0.035) {
    return fail("chart gamut compression should validate enough chromatic patches under hue/chroma thresholds");
  }

  const auto synthetic_camera_profile = photo_editor::pipeline::resolve_camera_profile(
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .input_profile_name = "Camera Native",
          .camera_make = "Canon",
          .camera_model = "EOS R5",
          .as_shot_temperature = 4300.0,
          .as_shot_tint = -6.0,
          .baseline_exposure = 0.12,
          .highlight_headroom = 0.78,
      });
  auto raw_fixture_state = neutral_state;
  raw_fixture_state.color_pipeline.input_profile =
      photo_editor::pipeline::input_color_profile_from_name("Camera Native");
  raw_fixture_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  raw_fixture_state.color_pipeline.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  raw_fixture_state.color_pipeline.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  raw_fixture_state.color_pipeline.output_headroom = 2.0;
  raw_fixture_state.color_pipeline.camera_profile = synthetic_camera_profile;
  raw_fixture_state.color_pipeline.input_transform_recipe =
      photo_editor::pipeline::resolve_input_transform_recipe(
          raw_fixture_state.color_pipeline.input_profile,
          synthetic_camera_profile,
          photo_editor::pipeline::DecodedImageMetadata{
              .valid = true,
              .is_raw = true,
              .input_profile_name = "Camera Native",
              .camera_make = "Canon",
              .camera_model = "EOS R5",
              .as_shot_temperature = 4300.0,
              .as_shot_tint = -6.0,
              .baseline_exposure = 0.12,
              .highlight_headroom = 0.78,
          });
  raw_fixture_state.input_transform.raw_input_transform_enabled = true;
  raw_fixture_state.input_transform.raw_white_balance_override_enabled = true;
  raw_fixture_state.input_transform.raw_white_balance_temperature = 5500.0;
  raw_fixture_state.input_transform.raw_white_balance_tint = 0.0;
  raw_fixture_state.input_transform.raw_black_level = 0.02;
  raw_fixture_state.input_transform.raw_white_level = 1.24;
  raw_fixture_state.input_transform.highlight_recovery_enabled = true;
  raw_fixture_state.input_transform.highlight_reconstruction_mode = "Camera Default";

  struct RawFixture final {
    std::string_view name;
    photo_editor::pipeline::ColorPixel camera_sample;
    std::size_t reference_patch = 0;
    bool neutral = false;
    bool saturated = false;
  };
  const std::array<RawFixture, 5> raw_fixtures = {{
      {"Neutral 6.5", {.red = 0.43F, .green = 0.34F, .blue = 0.24F, .alpha = 1.0F}, 20, true, false},
      {"Dark Skin", {.red = 0.19F, .green = 0.095F, .blue = 0.060F, .alpha = 1.0F}, 0, false, false},
      {"Blue Sky", {.red = 0.13F, .green = 0.19F, .blue = 0.33F, .alpha = 1.0F}, 2, false, false},
      {"Saturated Red", {.red = 1.45F, .green = 0.12F, .blue = 0.08F, .alpha = 1.0F}, 14, false, true},
      {"Saturated Cyan", {.red = 0.08F, .green = 1.22F, .blue = 1.35F, .alpha = 1.0F}, 17, false, true},
  }};
  std::vector<photo_editor::pipeline::ColorPixel> raw_fixture_pixels;
  raw_fixture_pixels.reserve(raw_fixtures.size());
  for (const auto& fixture : raw_fixtures) {
    raw_fixture_pixels.push_back(fixture.camera_sample);
  }
  photo_editor::pipeline::apply_color_adjustments(
      &raw_fixture_pixels,
      static_cast<int>(raw_fixture_pixels.size()),
      1,
      raw_fixture_state,
      photo_editor::pipeline::RemovalQuality::Final);
  auto checked_raw_fixtures = 0;
  auto checked_saturated_raw_fixtures = 0;
  for (std::size_t index = 0; index < raw_fixtures.size(); ++index) {
    const auto& fixture = raw_fixtures[index];
    const auto& rendered = raw_fixture_pixels[index];
    if (!std::isfinite(rendered.red) ||
        !std::isfinite(rendered.green) ||
        !std::isfinite(rendered.blue) ||
        rendered.red < -1.0e-6F ||
        rendered.green < -1.0e-6F ||
        rendered.blue < -1.0e-6F) {
      std::cerr << "synthetic RAW fixture produced invalid output on "
                << fixture.name << '\n';
      return EXIT_FAILURE;
    }
    if (fixture.neutral && normalized_chroma(rendered) > 0.08) {
      std::cerr << "synthetic RAW neutral fixture should remain near-neutral, chroma="
                << normalized_chroma(rendered)
                << " rgb=" << rendered.red
                << "," << rendered.green
                << "," << rendered.blue << '\n';
      return EXIT_FAILURE;
    }
    const auto reference = reference_patches[fixture.reference_patch];
    const auto rendered_hue = rgb_hue(rendered);
    const auto reference_hue = rgb_hue(reference);
    if (!fixture.neutral &&
        rendered_hue >= 0.0 &&
        reference_hue >= 0.0 &&
        hue_distance(rendered_hue, reference_hue) > (fixture.saturated ? 0.085 : 0.070)) {
      std::cerr << "synthetic RAW fixture hue drifted on "
                << fixture.name << " drift="
                << hue_distance(rendered_hue, reference_hue) << '\n';
      return EXIT_FAILURE;
    }
    if (fixture.saturated) {
      auto sdr_output_config = raw_fixture_state.color_pipeline;
      sdr_output_config.output_headroom = 1.0;
      const auto output = photo_editor::pipeline::apply_output_transform(
          rendered,
          sdr_output_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
          raw_fixture_state.input_transform.chromatic_adaptation_method);
      if (!output.source_out_of_gamut ||
          output.pixel.red < -1.0e-6F ||
          output.pixel.green < -1.0e-6F ||
          output.pixel.blue < -1.0e-6F ||
          output.pixel.red > 1.00001F ||
          output.pixel.green > 1.00001F ||
          output.pixel.blue > 1.00001F) {
        std::cerr << "synthetic saturated RAW fixture should report and compress output gamut on "
                  << fixture.name << '\n';
        return EXIT_FAILURE;
      }
      ++checked_saturated_raw_fixtures;
    }
    ++checked_raw_fixtures;
  }
  if (checked_raw_fixtures != static_cast<int>(raw_fixtures.size()) ||
      checked_saturated_raw_fixtures < 2) {
    return fail("synthetic RAW fixtures should cover neutral, skin, sky, and saturated patches");
  }

  return EXIT_SUCCESS;
}

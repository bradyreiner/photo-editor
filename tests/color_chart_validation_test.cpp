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
};

constexpr std::array<ChartPatch, 24> kColorCheckerSrgbPatches = {{
    {"Dark Skin", 115, 82, 68},
    {"Light Skin", 194, 150, 130},
    {"Blue Sky", 98, 122, 157},
    {"Foliage", 87, 108, 67},
    {"Blue Flower", 133, 128, 177},
    {"Bluish Green", 103, 189, 170},
    {"Orange", 214, 126, 44},
    {"Purplish Blue", 80, 91, 166},
    {"Moderate Red", 193, 90, 99},
    {"Purple", 94, 60, 108},
    {"Yellow Green", 157, 188, 64},
    {"Orange Yellow", 224, 163, 46},
    {"Blue", 56, 61, 150},
    {"Green", 70, 148, 73},
    {"Red", 175, 54, 60},
    {"Yellow", 231, 199, 31},
    {"Magenta", 187, 86, 149},
    {"Cyan", 8, 133, 161},
    {"White", 243, 243, 242},
    {"Neutral 8", 200, 200, 200},
    {"Neutral 6.5", 160, 160, 160},
    {"Neutral 5", 122, 122, 121},
    {"Neutral 3.5", 85, 85, 85},
    {"Black", 52, 52, 52},
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
  for (std::size_t index = 0; index < reference_patches.size(); ++index) {
    const auto delta = photo_editor::pipeline::perceptual_delta(
        reference_patches[index],
        rendered_patches[index],
        color_space);
    max_delta = std::max(max_delta, delta);
    if (delta > 0.0025) {
      std::cerr << "neutral color chart render drifted on patch "
                << kColorCheckerSrgbPatches[index].name << " with perceptual delta "
                << delta << '\n';
      return EXIT_FAILURE;
    }
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

  return EXIT_SUCCESS;
}

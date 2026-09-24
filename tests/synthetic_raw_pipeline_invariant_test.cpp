#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct RawCorpusCase final {
  std::string name;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  std::string input_profile;
  std::string camera_matching_look = "Standard";
};

[[nodiscard]] bool finite_pixel(const photo_editor::pipeline::ColorPixel& pixel) {
  return std::isfinite(pixel.red) && std::isfinite(pixel.green) && std::isfinite(pixel.blue) &&
         std::isfinite(pixel.alpha);
}

[[nodiscard]] double max_channel_delta(
    const photo_editor::pipeline::ColorPixel& left,
    const photo_editor::pipeline::ColorPixel& right) {
  return std::max({
      std::abs(static_cast<double>(left.red - right.red)),
      std::abs(static_cast<double>(left.green - right.green)),
      std::abs(static_cast<double>(left.blue - right.blue)),
  });
}

[[nodiscard]] std::optional<photo_editor::pipeline::DcpProfile> parsed_dcp_fixture_for(
    const RawCorpusCase& raw_case,
    photo_editor::pipeline::InputTransformKind kind) {
  if (kind != photo_editor::pipeline::InputTransformKind::CameraDcpProfile) {
    return std::nullopt;
  }
  return photo_editor::pipeline::DcpProfile{
      .payload_sha256 = std::string(64U, raw_case.name.front() == 'c' ? 'a' : 'b'),
      .byte_size = 1024U,
      .profile_name = raw_case.input_profile,
      .unique_camera_model = raw_case.metadata.camera_make + " " + raw_case.metadata.camera_model,
      .calibration_illuminant1 = 17U,
      .calibration_illuminant2 = 21U,
      .color_matrix1 = std::array<double, 9>{
          1.0, 0.0, 0.0,
          0.0, 1.0, 0.0,
          0.0, 0.0, 1.0,
      },
      .color_matrix2 = std::array<double, 9>{
          1.0, 0.0, 0.0,
          0.0, 1.0, 0.0,
          0.0, 0.0, 1.0,
      },
      .forward_matrix1 = std::array<double, 9>{
          1.13, -0.06, -0.02,
          -0.04, 1.05, -0.01,
          0.01, -0.04, 1.12,
      },
      .forward_matrix2 = std::array<double, 9>{
          1.04, -0.03, -0.01,
          -0.02, 1.02, 0.01,
          -0.01, -0.01, 1.05,
      },
  };
}

}  // namespace

int main() {
  using photo_editor::pipeline::ColorAdjustmentState;
  using photo_editor::pipeline::ColorPixel;
  using photo_editor::pipeline::InputTransformKind;

  const std::vector<ColorPixel> raw_patch = {
      {.red = 0.08F, .green = 0.12F, .blue = 0.18F, .alpha = 1.0F},
      {.red = 0.32F, .green = 0.28F, .blue = 0.19F, .alpha = 1.0F},
      {.red = 0.65F, .green = 0.52F, .blue = 0.34F, .alpha = 1.0F},
      {.red = 0.95F, .green = 0.78F, .blue = 0.56F, .alpha = 1.0F},
      {.red = 1.18F, .green = 0.94F, .blue = 0.62F, .alpha = 1.0F},
      {.red = 1.42F, .green = 1.08F, .blue = 0.72F, .alpha = 1.0F},
  };

  std::vector<RawCorpusCase> corpus = {
      {
          .name = "canon-r5-daylight-dcp",
          .metadata =
              {
                  .valid = true,
                  .is_raw = true,
                  .input_profile_name = "Camera DCP: Canon EOS R5 Adobe Standard",
                  .camera_make = "Canon",
                  .camera_model = "EOS R5",
                  .as_shot_temperature = 6500.0,
                  .as_shot_tint = 4.0,
                  .baseline_exposure = 0.10,
                  .highlight_headroom = 0.85,
              },
          .input_profile = "Camera DCP: Canon EOS R5 Adobe Standard",
          .camera_matching_look = "Vivid",
      },
      {
          .name = "nikon-z8-tungsten-dcp",
          .metadata =
              {
                  .valid = true,
                  .is_raw = true,
                  .input_profile_name = "Camera DCP: Nikon Z8 Neutral",
                  .camera_make = "NIKON CORPORATION",
                  .camera_model = "Z 8",
                  .as_shot_temperature = 2850.0,
                  .as_shot_tint = -6.0,
                  .baseline_exposure = -0.15,
                  .highlight_headroom = 1.15,
              },
          .input_profile = "Camera DCP: Nikon Z8 Neutral",
          .camera_matching_look = "Neutral",
      },
      {
          .name = "sony-a7r-icc",
          .metadata =
              {
                  .valid = true,
                  .is_raw = true,
                  .input_profile_name = "Camera ICC: Sony Manufacturer",
                  .camera_make = "SONY",
                  .camera_model = "ILCE-7RM5",
                  .as_shot_temperature = 5200.0,
                  .as_shot_tint = 2.0,
                  .baseline_exposure = 0.0,
                  .highlight_headroom = 0.95,
              },
          .input_profile = "Camera ICC: Sony Manufacturer",
          .camera_matching_look = "Portrait",
      },
      {
          .name = "fujifilm-raw-decoder-default",
          .metadata =
              {
                  .valid = true,
                  .is_raw = true,
                  .input_profile_name = "RAW Decoder Default",
                  .camera_make = "FUJIFILM",
                  .camera_model = "X-T5",
                  .as_shot_temperature = 4300.0,
                  .as_shot_tint = 9.0,
                  .baseline_exposure = 0.20,
                  .highlight_headroom = 0.75,
              },
          .input_profile = "RAW Decoder Default",
          .camera_matching_look = "Landscape",
      },
      {
          .name = "apple-proraw-native",
          .metadata =
              {
                  .valid = true,
                  .is_raw = true,
                  .input_profile_name = "Camera Native",
                  .camera_make = "Apple",
                  .camera_model = "iPhone 15 Pro",
                  .as_shot_temperature = 3900.0,
                  .as_shot_tint = -3.0,
                  .baseline_exposure = -0.05,
                  .highlight_headroom = 1.25,
              },
          .input_profile = "Camera Native",
          .camera_matching_look = "Monochrome",
      },
  };
  for (auto& raw_case : corpus) {
    raw_case.metadata.pixel_encoding =
        photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb;
    raw_case.metadata.decoder_backend = "reference-camera-linear-backend";
    raw_case.metadata.decoded_color_space_name = "Camera RGB";
    raw_case.metadata.demosaic_applied = true;
  }

  std::vector<ColorPixel> first_case_pixels;
  auto distinct_case_count = 0;

  for (const auto& raw_case : corpus) {
    const auto input_profile =
        photo_editor::pipeline::input_color_profile_from_name(raw_case.input_profile);
    const auto parsed_dcp = parsed_dcp_fixture_for(raw_case, input_profile.kind);
    const auto camera_profile =
        photo_editor::pipeline::resolve_camera_profile(
            raw_case.metadata,
            input_profile,
            parsed_dcp.has_value() ? &*parsed_dcp : nullptr);
    const auto recipe = photo_editor::pipeline::resolve_input_transform_recipe(
        input_profile,
        camera_profile,
        raw_case.metadata);

    if (!recipe.has_source_white_balance) {
      std::cerr << raw_case.name << " should resolve source white balance metadata\n";
      return EXIT_FAILURE;
    }
    if (camera_profile.valid && camera_profile.profile_name.empty()) {
      std::cerr << raw_case.name << " should have a named camera profile\n";
      return EXIT_FAILURE;
    }
    if (input_profile.kind == InputTransformKind::CameraDcpProfile &&
        !camera_profile.dual_illuminant) {
      std::cerr << raw_case.name << " should use dual-illuminant DCP data\n";
      return EXIT_FAILURE;
    }

    ColorAdjustmentState state;
    state.input_transform.raw_input_transform_enabled = true;
    state.input_transform.camera_matching_look = raw_case.camera_matching_look;
    state.input_transform.highlight_reconstruction_mode = "Enabled";
    state.input_transform.raw_black_level = 0.02;
    state.input_transform.raw_white_level = 1.35;
    state.color_pipeline.input_profile = input_profile;
    state.color_pipeline.camera_profile = camera_profile;
    state.color_pipeline.input_transform_recipe = recipe;

    auto pixels = raw_patch;
    photo_editor::pipeline::apply_color_adjustments(&pixels, 3, 2, state);
    for (const auto& pixel : pixels) {
      if (!finite_pixel(pixel) || pixel.alpha != 1.0F) {
        std::cerr << raw_case.name << " produced invalid RAW corpus output\n";
        return EXIT_FAILURE;
      }
    }

    if (first_case_pixels.empty()) {
      first_case_pixels = pixels;
    } else if (max_channel_delta(first_case_pixels[2], pixels[2]) > 1.0e-4) {
      ++distinct_case_count;
    }
  }

  constexpr int kHighResolutionRawStressWidth = 4096;
  constexpr int kHighResolutionRawStressHeight = 256;
  std::vector<ColorPixel> high_resolution_raw_strip;
  high_resolution_raw_strip.reserve(
      kHighResolutionRawStressWidth * kHighResolutionRawStressHeight);
  for (int y = 0; y < kHighResolutionRawStressHeight; ++y) {
    for (int x = 0; x < kHighResolutionRawStressWidth; ++x) {
      const auto horizontal =
          static_cast<float>(x) / static_cast<float>(kHighResolutionRawStressWidth - 1);
      const auto vertical =
          static_cast<float>(y) / static_cast<float>(kHighResolutionRawStressHeight - 1);
      high_resolution_raw_strip.push_back(ColorPixel{
          .red = 0.04F + horizontal * 1.42F,
          .green = 0.05F + vertical * 1.10F,
          .blue = 0.08F + (1.0F - horizontal) * 0.72F + vertical * 0.28F,
          .alpha = 1.0F,
      });
    }
  }
  std::vector<ColorPixel> high_resolution_first_output;
  for (const auto& raw_case : {corpus.front(), corpus.back()}) {
    const auto input_profile =
        photo_editor::pipeline::input_color_profile_from_name(raw_case.input_profile);
    const auto parsed_dcp = parsed_dcp_fixture_for(raw_case, input_profile.kind);
    const auto camera_profile =
        photo_editor::pipeline::resolve_camera_profile(
            raw_case.metadata,
            input_profile,
            parsed_dcp.has_value() ? &*parsed_dcp : nullptr);
    const auto recipe = photo_editor::pipeline::resolve_input_transform_recipe(
        input_profile,
        camera_profile,
        raw_case.metadata);

    ColorAdjustmentState state;
    state.input_transform.raw_input_transform_enabled = true;
    state.input_transform.camera_matching_look = raw_case.camera_matching_look;
    state.input_transform.highlight_reconstruction_mode = "Enabled";
    state.input_transform.raw_black_level = 0.015;
    state.input_transform.raw_white_level = 1.28;
    state.color_pipeline.input_profile = input_profile;
    state.color_pipeline.camera_profile = camera_profile;
    state.color_pipeline.input_transform_recipe = recipe;

    auto pixels = high_resolution_raw_strip;
    photo_editor::pipeline::apply_color_adjustments(
        &pixels,
        kHighResolutionRawStressWidth,
        kHighResolutionRawStressHeight,
        state);
    const auto sample_index =
        (kHighResolutionRawStressHeight / 2) * kHighResolutionRawStressWidth +
        (kHighResolutionRawStressWidth / 2);
    if (!finite_pixel(pixels.front()) || !finite_pixel(pixels[sample_index]) ||
        !finite_pixel(pixels.back()) || pixels[sample_index].alpha != 1.0F) {
      std::cerr << raw_case.name
                << " high-resolution RAW stress buffer produced invalid output\n";
      return EXIT_FAILURE;
    }
    if (high_resolution_first_output.empty()) {
      high_resolution_first_output = std::move(pixels);
    } else if (max_channel_delta(high_resolution_first_output[sample_index], pixels[sample_index]) <
               1.0e-4) {
      std::cerr << "high-resolution RAW stress buffers should preserve camera/profile differences\n";
      return EXIT_FAILURE;
    }
  }

  if (distinct_case_count < 3) {
    std::cerr << "representative RAW corpus should exercise distinct camera/profile outputs\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

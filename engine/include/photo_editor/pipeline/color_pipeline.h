#pragma once

#include "photo_editor/pipeline/color_space.h"

#include <array>
#include <string>
#include <string_view>

namespace photo_editor::pipeline {

enum class InputTransformKind {
  EmbeddedProfile,
  CameraNative,
  RawDecoderDefault,
};

enum class WorkingTransformKind {
  LinearSrgb,
  LinearDisplayP3,
  LinearProPhotoRgb,
  LinearRec2020,
};

enum class OutputTransformKind {
  Srgb,
  DisplayP3,
  AdobeRgb,
  ProPhotoRgb,
  Rec709,
  Rec2020,
};

enum class RenderIntent {
  Perceptual,
  RelativeColorimetric,
};

struct InputColorProfile final {
  std::string profile_name = "Embedded";
  InputTransformKind kind = InputTransformKind::EmbeddedProfile;
  RgbColorSpace color_space = make_srgb_color_space();
};

struct WorkingColorProfile final {
  std::string profile_name = "Linear ProPhoto RGB";
  WorkingTransformKind kind = WorkingTransformKind::LinearProPhotoRgb;
  RgbColorSpace color_space = make_linear_prophoto_rgb_color_space();
};

struct OutputColorProfile final {
  std::string profile_name = "sRGB";
  OutputTransformKind kind = OutputTransformKind::Srgb;
  RgbColorSpace color_space = make_srgb_color_space();
};

struct DecodedImageMetadata final {
  bool valid = false;
  bool is_raw = false;
  std::string input_profile_name = "Embedded";
  std::string camera_make;
  std::string camera_model;
  double as_shot_temperature = 5200.0;
  double as_shot_tint = 0.0;
  double baseline_exposure = 0.0;
  double highlight_headroom = 1.0;
};

struct CameraProfile final {
  bool valid = false;
  std::string profile_name = "Unknown Camera";
  std::string camera_make;
  std::string camera_model;
  std::array<double, 9> camera_to_xyz_d50 = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  double reference_temperature = 5200.0;
  double reference_tint = 0.0;
};

struct InputTransformRecipe final {
  bool has_source_white_balance = false;
  std::string camera_profile_name;
  double source_temperature = 5200.0;
  double source_tint = 0.0;
  double baseline_exposure = 0.0;
  double highlight_headroom = 1.0;
};

struct ColorPipelineConfig final {
  InputColorProfile input_profile;
  WorkingColorProfile working_profile;
  OutputColorProfile preview_output_profile;
  OutputColorProfile export_output_profile;
  CameraProfile camera_profile;
  InputTransformRecipe input_transform_recipe;
  RenderIntent render_intent = RenderIntent::Perceptual;
};

[[nodiscard]] std::string normalize_input_color_profile_name(std::string_view value);
[[nodiscard]] std::string normalize_working_color_profile_name(std::string_view value);
[[nodiscard]] std::string normalize_output_color_profile_name(std::string_view value);
[[nodiscard]] std::string normalize_render_intent_name(std::string_view value);

[[nodiscard]] InputColorProfile input_color_profile_from_name(std::string_view value);
[[nodiscard]] WorkingColorProfile working_color_profile_from_name(std::string_view value);
[[nodiscard]] OutputColorProfile output_color_profile_from_name(std::string_view value);
[[nodiscard]] RenderIntent render_intent_from_name(std::string_view value);
[[nodiscard]] const char* render_intent_name(RenderIntent intent);
[[nodiscard]] CameraProfile resolve_camera_profile(const DecodedImageMetadata& metadata);
[[nodiscard]] InputTransformRecipe resolve_input_transform_recipe(
    const InputColorProfile& input_profile,
    const CameraProfile& camera_profile,
    const DecodedImageMetadata& metadata);
[[nodiscard]] ColorPipelineConfig default_color_pipeline_config();

}  // namespace photo_editor::pipeline

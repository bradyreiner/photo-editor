#include "photo_editor/pipeline/color_pipeline.h"

#include <algorithm>
#include <cctype>

namespace photo_editor::pipeline {

namespace {

std::string trimmed_lower(std::string_view value) {
  auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return {};
  }
  auto end = value.find_last_not_of(" \t\r\n");
  std::string normalized(value.substr(start, end - start + 1));
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return normalized;
}

std::string trim_copy(std::string_view value) {
  auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return {};
  }
  auto end = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(start, end - start + 1));
}

bool string_equals_ci(std::string_view left, std::string_view right) {
  return trimmed_lower(left) == trimmed_lower(right);
}

bool ends_with_ci(std::string_view value, std::string_view suffix) {
  const auto normalized = trimmed_lower(value);
  const auto normalized_suffix = trimmed_lower(suffix);
  return normalized.size() >= normalized_suffix.size() &&
         normalized.compare(
             normalized.size() - normalized_suffix.size(),
             normalized_suffix.size(),
             normalized_suffix) == 0;
}

std::string basename_copy(std::string_view value) {
  const auto trimmed = trim_copy(value);
  const auto slash = trimmed.find_last_of("/\\");
  return slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
}

std::string prefixed_profile_name(std::string_view value, std::string_view prefix) {
  const auto trimmed = trim_copy(value);
  const auto separator = trimmed.find(':');
  const auto label = separator == std::string::npos ? basename_copy(trimmed)
                                                     : trim_copy(std::string_view(trimmed).substr(separator + 1));
  if (label.empty()) {
    return std::string(prefix);
  }
  std::string result(prefix);
  result += ": ";
  result += label;
  return result;
}

}  // namespace

RgbColorSpace make_linear_srgb_color_space() {
  return RgbColorSpace{
      .name = "Linear sRGB",
      .primaries = ColorPrimaries::Srgb,
      .transfer_function = TransferFunction::Linear,
      .white_point = WhitePoint::D65,
      .scene_referred = true,
      .wide_gamut = false,
  };
}

RgbColorSpace make_linear_display_p3_color_space() {
  return RgbColorSpace{
      .name = "Linear Display P3",
      .primaries = ColorPrimaries::DisplayP3,
      .transfer_function = TransferFunction::Linear,
      .white_point = WhitePoint::D65,
      .scene_referred = true,
      .wide_gamut = true,
  };
}

RgbColorSpace make_linear_prophoto_rgb_color_space() {
  return RgbColorSpace{
      .name = "Linear ProPhoto RGB",
      .primaries = ColorPrimaries::ProPhotoRgb,
      .transfer_function = TransferFunction::Linear,
      .white_point = WhitePoint::D50,
      .scene_referred = true,
      .wide_gamut = true,
  };
}

RgbColorSpace make_linear_rec2020_color_space() {
  return RgbColorSpace{
      .name = "Linear Rec.2020",
      .primaries = ColorPrimaries::Rec2020,
      .transfer_function = TransferFunction::Linear,
      .white_point = WhitePoint::D65,
      .scene_referred = true,
      .wide_gamut = true,
  };
}

RgbColorSpace make_srgb_color_space() {
  return RgbColorSpace{
      .name = "sRGB",
      .primaries = ColorPrimaries::Srgb,
      .transfer_function = TransferFunction::Srgb,
      .white_point = WhitePoint::D65,
      .scene_referred = false,
      .wide_gamut = false,
  };
}

RgbColorSpace make_display_p3_color_space() {
  return RgbColorSpace{
      .name = "Display P3",
      .primaries = ColorPrimaries::DisplayP3,
      .transfer_function = TransferFunction::Srgb,
      .white_point = WhitePoint::D65,
      .scene_referred = false,
      .wide_gamut = true,
  };
}

RgbColorSpace make_adobe_rgb_color_space() {
  return RgbColorSpace{
      .name = "Adobe RGB",
      .primaries = ColorPrimaries::AdobeRgb,
      .transfer_function = TransferFunction::Gamma22,
      .white_point = WhitePoint::D65,
      .scene_referred = false,
      .wide_gamut = true,
  };
}

RgbColorSpace make_prophoto_rgb_color_space() {
  return RgbColorSpace{
      .name = "ProPhoto RGB",
      .primaries = ColorPrimaries::ProPhotoRgb,
      .transfer_function = TransferFunction::Gamma22,
      .white_point = WhitePoint::D50,
      .scene_referred = false,
      .wide_gamut = true,
  };
}

RgbColorSpace make_rec709_color_space() {
  return RgbColorSpace{
      .name = "Rec.709",
      .primaries = ColorPrimaries::Rec709,
      .transfer_function = TransferFunction::Gamma24,
      .white_point = WhitePoint::D65,
      .scene_referred = false,
      .wide_gamut = false,
  };
}

RgbColorSpace make_rec2020_color_space() {
  return RgbColorSpace{
      .name = "Rec.2020",
      .primaries = ColorPrimaries::Rec2020,
      .transfer_function = TransferFunction::Bt2020,
      .white_point = WhitePoint::D65,
      .scene_referred = false,
      .wide_gamut = true,
  };
}

std::string normalize_input_color_profile_name(std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized.rfind("camera dcp:", 0) == 0 || normalized.rfind("dcp:", 0) == 0 ||
      ends_with_ci(value, ".dcp")) {
    return prefixed_profile_name(value, "Camera DCP");
  }
  if (normalized.rfind("camera icc:", 0) == 0 || normalized.rfind("icc:", 0) == 0 ||
      normalized.rfind("camera icm:", 0) == 0 || normalized.rfind("icm:", 0) == 0 ||
      ends_with_ci(value, ".icc") || ends_with_ci(value, ".icm")) {
    return prefixed_profile_name(value, "Camera ICC");
  }
  if (normalized == "camera native" || normalized == "camera-native") {
    return "Camera Native";
  }
  if (normalized == "raw decoder default" || normalized == "raw-decoder-default") {
    return "RAW Decoder Default";
  }
  if (normalized == "srgb") {
    return "sRGB";
  }
  if (normalized == "display p3" || normalized == "display-p3" || normalized == "p3") {
    return "Display P3";
  }
  if (normalized == "adobe rgb" || normalized == "adobe-rgb") {
    return "Adobe RGB";
  }
  if (normalized == "prophoto rgb" || normalized == "prophoto-rgb" || normalized == "prophoto") {
    return "ProPhoto RGB";
  }
  if (normalized == "rec.709" || normalized == "rec709" || normalized == "rec-709") {
    return "Rec.709";
  }
  if (normalized == "rec.2020" || normalized == "rec2020" || normalized == "rec-2020") {
    return "Rec.2020";
  }
  return "Embedded";
}

std::string normalize_working_color_profile_name(std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized == "linear srgb" || normalized == "linear-srgb" || normalized == "srgb-linear") {
    return "Linear sRGB";
  }
  if (normalized == "linear display p3" || normalized == "linear-display-p3" ||
      normalized == "display-p3-linear" || normalized == "display p3 linear") {
    return "Linear Display P3";
  }
  if (normalized == "linear prophoto rgb" || normalized == "linear-prophoto-rgb" ||
      normalized == "prophoto-linear" || normalized == "linear prophoto") {
    return "Linear ProPhoto RGB";
  }
  if (normalized == "linear rec.2020" || normalized == "linear-rec.2020" ||
      normalized == "linear rec2020" || normalized == "linear-rec2020" ||
      normalized == "rec2020-linear" || normalized == "rec.2020-linear") {
    return "Linear Rec.2020";
  }
  return "Linear ProPhoto RGB";
}

std::string normalize_output_color_profile_name(std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized == "display p3" || normalized == "display-p3" || normalized == "p3") {
    return "Display P3";
  }
  if (normalized == "adobe rgb" || normalized == "adobe-rgb") {
    return "Adobe RGB";
  }
  if (normalized == "prophoto rgb" || normalized == "prophoto-rgb" || normalized == "prophoto") {
    return "ProPhoto RGB";
  }
  if (normalized == "rec.709" || normalized == "rec709" || normalized == "rec-709") {
    return "Rec.709";
  }
  if (normalized == "rec.2020" || normalized == "rec2020" || normalized == "rec-2020") {
    return "Rec.2020";
  }
  return "sRGB";
}

std::string normalize_render_intent_name(std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized == "relative colorimetric" || normalized == "relative-colorimetric") {
    return "Relative Colorimetric";
  }
  return "Perceptual";
}

InputColorProfile input_color_profile_from_name(std::string_view value) {
  const auto normalized = normalize_input_color_profile_name(value);
  if (trimmed_lower(normalized).rfind("camera dcp:", 0) == 0) {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::CameraDcpProfile,
        .color_space = make_linear_prophoto_rgb_color_space(),
    };
  }
  if (trimmed_lower(normalized).rfind("camera icc:", 0) == 0) {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::CameraIccProfile,
        .color_space = make_linear_prophoto_rgb_color_space(),
    };
  }
  if (normalized == "Camera Native") {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::CameraNative,
        .color_space = make_srgb_color_space(),
    };
  }
  if (normalized == "RAW Decoder Default") {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::RawDecoderDefault,
        .color_space = make_srgb_color_space(),
    };
  }
  if (normalized == "Display P3") {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::EmbeddedProfile,
        .color_space = make_display_p3_color_space(),
    };
  }
  if (normalized == "sRGB") {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::EmbeddedProfile,
        .color_space = make_srgb_color_space(),
    };
  }
  if (normalized == "Adobe RGB") {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::EmbeddedProfile,
        .color_space = make_adobe_rgb_color_space(),
    };
  }
  if (normalized == "ProPhoto RGB") {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::EmbeddedProfile,
        .color_space = make_prophoto_rgb_color_space(),
    };
  }
  if (normalized == "Rec.709") {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::EmbeddedProfile,
        .color_space = make_rec709_color_space(),
    };
  }
  if (normalized == "Rec.2020") {
    return InputColorProfile{
        .profile_name = normalized,
        .kind = InputTransformKind::EmbeddedProfile,
        .color_space = make_rec2020_color_space(),
    };
  }
  return InputColorProfile{
      .profile_name = normalized,
      .kind = InputTransformKind::EmbeddedProfile,
      .color_space = make_srgb_color_space(),
  };
}

WorkingColorProfile working_color_profile_from_name(std::string_view value) {
  const auto normalized = normalize_working_color_profile_name(value);
  if (normalized == "Linear sRGB") {
    return WorkingColorProfile{
        .profile_name = normalized,
        .kind = WorkingTransformKind::LinearSrgb,
        .color_space = make_linear_srgb_color_space(),
    };
  }
  if (normalized == "Linear Display P3") {
    return WorkingColorProfile{
        .profile_name = normalized,
        .kind = WorkingTransformKind::LinearDisplayP3,
        .color_space = make_linear_display_p3_color_space(),
    };
  }
  if (normalized == "Linear Rec.2020") {
    return WorkingColorProfile{
        .profile_name = normalized,
        .kind = WorkingTransformKind::LinearRec2020,
        .color_space = make_linear_rec2020_color_space(),
    };
  }
  return WorkingColorProfile{
      .profile_name = normalized,
      .kind = WorkingTransformKind::LinearProPhotoRgb,
      .color_space = make_linear_prophoto_rgb_color_space(),
  };
}

OutputColorProfile output_color_profile_from_name(std::string_view value) {
  const auto normalized = normalize_output_color_profile_name(value);
  if (normalized == "Display P3") {
    return OutputColorProfile{
        .profile_name = normalized,
        .kind = OutputTransformKind::DisplayP3,
        .color_space = make_display_p3_color_space(),
    };
  }
  if (normalized == "Adobe RGB") {
    return OutputColorProfile{
        .profile_name = normalized,
        .kind = OutputTransformKind::AdobeRgb,
        .color_space = make_adobe_rgb_color_space(),
    };
  }
  if (normalized == "ProPhoto RGB") {
    return OutputColorProfile{
        .profile_name = normalized,
        .kind = OutputTransformKind::ProPhotoRgb,
        .color_space = make_prophoto_rgb_color_space(),
    };
  }
  if (normalized == "Rec.709") {
    return OutputColorProfile{
        .profile_name = normalized,
        .kind = OutputTransformKind::Rec709,
        .color_space = make_rec709_color_space(),
    };
  }
  if (normalized == "Rec.2020") {
    return OutputColorProfile{
        .profile_name = normalized,
        .kind = OutputTransformKind::Rec2020,
        .color_space = make_rec2020_color_space(),
    };
  }
  return OutputColorProfile{
      .profile_name = normalized,
      .kind = OutputTransformKind::Srgb,
      .color_space = make_srgb_color_space(),
  };
}

RenderIntent render_intent_from_name(std::string_view value) {
  return normalize_render_intent_name(value) == "Relative Colorimetric"
             ? RenderIntent::RelativeColorimetric
             : RenderIntent::Perceptual;
}

const char* render_intent_name(RenderIntent intent) {
  switch (intent) {
    case RenderIntent::RelativeColorimetric:
      return "Relative Colorimetric";
    case RenderIntent::Perceptual:
    default:
      return "Perceptual";
  }
}

CameraProfile resolve_camera_profile(
    const DecodedImageMetadata& metadata,
    const InputColorProfile& input_profile) {
  if (!metadata.valid) {
    return {};
  }

  CameraProfile profile{
      .valid = true,
      .profile_name = "Generic Camera",
      .profile_format = "Generic",
      .camera_make = trim_copy(metadata.camera_make),
      .camera_model = trim_copy(metadata.camera_model),
  };
  if (!profile.camera_make.empty() || !profile.camera_model.empty()) {
    profile.profile_name = profile.camera_make;
    if (!profile.profile_name.empty() && !profile.camera_model.empty()) {
      profile.profile_name += " ";
    }
    profile.profile_name += profile.camera_model;
  }

  if (input_profile.kind == InputTransformKind::CameraDcpProfile) {
    profile.profile_name = input_profile.profile_name;
    profile.profile_format = "DCP";
    profile.dual_illuminant = true;
    profile.tungsten_temperature = 2850.0;
    profile.daylight_temperature = 6500.0;
    profile.camera_to_xyz_d50_tungsten = {
        1.13, -0.06, -0.02,
        -0.04, 1.05, -0.01,
        0.01, -0.04, 1.12,
    };
    profile.camera_to_xyz_d50_daylight = {
        1.04, -0.03, -0.01,
        -0.02, 1.02, 0.01,
        -0.01, -0.01, 1.05,
    };
    profile.camera_to_xyz_d50 = profile.camera_to_xyz_d50_daylight;
    profile.reference_temperature = 6500.0;
    profile.reference_tint = metadata.as_shot_tint;
    return profile;
  }

  if (input_profile.kind == InputTransformKind::CameraIccProfile) {
    profile.profile_name = input_profile.profile_name;
    profile.profile_format = "ICC";
    profile.camera_to_xyz_d50 = {
        1.02, 0.02, -0.02,
        0.01, 0.98, 0.01,
        -0.02, 0.04, 1.04,
    };
    profile.reference_temperature = 5000.0;
    profile.reference_tint = metadata.as_shot_tint;
    return profile;
  }

  if (string_equals_ci(profile.camera_make, "Apple")) {
    profile.reference_temperature = 5500.0;
  } else if (string_equals_ci(profile.camera_make, "Canon")) {
    profile.reference_temperature = 5200.0;
  } else if (string_equals_ci(profile.camera_make, "NIKON CORPORATION") ||
             string_equals_ci(profile.camera_make, "Nikon")) {
    profile.reference_temperature = 5150.0;
  } else if (string_equals_ci(profile.camera_make, "SONY")) {
    profile.reference_temperature = 5300.0;
  } else if (string_equals_ci(profile.camera_make, "FUJIFILM")) {
    profile.reference_temperature = 5400.0;
  } else if (string_equals_ci(profile.camera_make, "Panasonic")) {
    profile.reference_temperature = 5250.0;
  } else {
    profile.reference_temperature = metadata.as_shot_temperature;
  }
  profile.reference_tint = metadata.as_shot_tint;
  return profile;
}

CameraProfile resolve_camera_profile(const DecodedImageMetadata& metadata) {
  return resolve_camera_profile(metadata, input_color_profile_from_name(metadata.input_profile_name));
}

InputTransformRecipe resolve_input_transform_recipe(
    const InputColorProfile& input_profile,
    const CameraProfile& camera_profile,
    const DecodedImageMetadata& metadata) {
  InputTransformRecipe recipe{};
  recipe.camera_profile_name = camera_profile.valid ? camera_profile.profile_name : std::string{};
  recipe.baseline_exposure = metadata.baseline_exposure;
  recipe.highlight_headroom = std::max(0.25, metadata.highlight_headroom);
  if (!metadata.valid || !metadata.is_raw) {
    return recipe;
  }

  if (input_profile.kind == InputTransformKind::CameraNative ||
      input_profile.kind == InputTransformKind::CameraDcpProfile ||
      input_profile.kind == InputTransformKind::CameraIccProfile ||
      input_profile.kind == InputTransformKind::RawDecoderDefault ||
      !metadata.camera_make.empty() || !metadata.camera_model.empty()) {
    recipe.has_source_white_balance = true;
    recipe.source_temperature = metadata.as_shot_temperature;
    recipe.source_tint = metadata.as_shot_tint;
  }

  if (!recipe.has_source_white_balance && camera_profile.valid) {
    recipe.has_source_white_balance = true;
    recipe.source_temperature = camera_profile.reference_temperature;
    recipe.source_tint = camera_profile.reference_tint;
  }

  if (recipe.camera_profile_name.empty() && metadata.valid) {
    recipe.camera_profile_name = metadata.camera_make;
    if (!recipe.camera_profile_name.empty() && !metadata.camera_model.empty()) {
      recipe.camera_profile_name += " ";
    }
    recipe.camera_profile_name += metadata.camera_model;
  }
  return recipe;
}

ColorPipelineConfig default_color_pipeline_config() {
  return ColorPipelineConfig{
      .input_profile = input_color_profile_from_name("Embedded"),
      .working_profile = working_color_profile_from_name("Linear ProPhoto RGB"),
      .preview_output_profile = output_color_profile_from_name("sRGB"),
      .export_output_profile = output_color_profile_from_name("sRGB"),
      .camera_profile = {},
      .input_transform_recipe = {},
      .render_intent = RenderIntent::Perceptual,
      .output_headroom = 1.0,
      .black_point_compensation_enabled = true,
      .source_black_point = 0.0,
      .output_black_point = 0.0,
  };
}

}  // namespace photo_editor::pipeline

#include "photo_editor/pipeline/color_pipeline.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace photo_editor::pipeline {

const char* decoded_pixel_encoding_name(DecodedPixelEncoding encoding) {
  switch (encoding) {
    case DecodedPixelEncoding::SensorMosaic:
      return "sensor-mosaic";
    case DecodedPixelEncoding::CameraLinearRgb:
      return "camera-linear-rgb";
    case DecodedPixelEncoding::SceneLinearRgb:
      return "scene-linear-rgb";
    case DecodedPixelEncoding::DisplayReferredRgb:
      return "display-referred-rgb";
    case DecodedPixelEncoding::Unknown:
      return "unknown";
  }
  return "unknown";
}

bool decoded_image_color_boundary_valid(const DecodedImageMetadata& metadata) {
  if (!metadata.valid) {
    return false;
  }
  if (!metadata.is_raw) {
    return true;
  }
  if (metadata.decoder_backend.empty() ||
      metadata.pixel_encoding == DecodedPixelEncoding::Unknown) {
    return false;
  }
  switch (metadata.pixel_encoding) {
    case DecodedPixelEncoding::SensorMosaic:
      return !metadata.demosaic_applied &&
             !metadata.camera_transform_applied;
    case DecodedPixelEncoding::CameraLinearRgb:
      return metadata.demosaic_applied &&
             !metadata.camera_transform_applied &&
             !metadata.decoded_color_space_name.empty();
    case DecodedPixelEncoding::SceneLinearRgb:
    case DecodedPixelEncoding::DisplayReferredRgb:
      return metadata.demosaic_applied &&
             metadata.camera_transform_applied &&
             !metadata.decoded_color_space_name.empty();
    case DecodedPixelEncoding::Unknown:
      return false;
  }
  return false;
}

bool decoded_image_requires_engine_input_transform(
    const DecodedImageMetadata& metadata) {
  if (!decoded_image_color_boundary_valid(metadata) || !metadata.is_raw) {
    return false;
  }
  return metadata.pixel_encoding == DecodedPixelEncoding::SensorMosaic ||
         metadata.pixel_encoding == DecodedPixelEncoding::CameraLinearRgb;
}

bool decoded_image_accepts_custom_input_profile(
    const DecodedImageMetadata& metadata) {
  return metadata.valid &&
         (!metadata.is_raw ||
          decoded_image_requires_engine_input_transform(metadata));
}

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

std::string normalized_color_space_identifier(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(byte)));
    }
  }
  return normalized;
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

RgbColorSpace make_aces2065_1_color_space() {
  return RgbColorSpace{
      .name = "ACES2065-1",
      .primaries = ColorPrimaries::AcesAp0,
      .transfer_function = TransferFunction::Linear,
      .white_point = WhitePoint::AcesD60,
      .scene_referred = true,
      .wide_gamut = true,
  };
}

RgbColorSpace make_acescg_color_space() {
  return RgbColorSpace{
      .name = "ACEScg",
      .primaries = ColorPrimaries::AcesAp1,
      .transfer_function = TransferFunction::Linear,
      .white_point = WhitePoint::AcesD60,
      .scene_referred = true,
      .wide_gamut = true,
  };
}

RgbColorSpace make_acescct_color_space() {
  return RgbColorSpace{
      .name = "ACEScct",
      .primaries = ColorPrimaries::AcesAp1,
      .transfer_function = TransferFunction::Acescct,
      .white_point = WhitePoint::AcesD60,
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
      .transfer_function = TransferFunction::Gamma18,
      .white_point = WhitePoint::D50,
      .scene_referred = false,
      .wide_gamut = true,
  };
}

RgbColorSpace make_rec709_color_space() {
  return RgbColorSpace{
      .name = "Rec.709",
      .primaries = ColorPrimaries::Rec709,
      .transfer_function = TransferFunction::Bt709,
      .white_point = WhitePoint::D65,
      .scene_referred = false,
      .wide_gamut = false,
  };
}

RgbColorSpace make_linear_rec709_color_space() {
  return RgbColorSpace{
      .name = "Linear Rec.709",
      .primaries = ColorPrimaries::Rec709,
      .transfer_function = TransferFunction::Linear,
      .white_point = WhitePoint::D65,
      .scene_referred = true,
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

RgbColorSpace make_linear_sony_s_gamut3_color_space() {
  return RgbColorSpace{
      .name = "Linear Sony S-Gamut3",
      .primaries = ColorPrimaries::SonySGamut3,
      .transfer_function = TransferFunction::Linear,
      .white_point = WhitePoint::D65,
      .scene_referred = true,
      .wide_gamut = true,
  };
}

RgbColorSpace make_linear_sony_s_gamut3_cine_color_space() {
  return RgbColorSpace{
      .name = "Linear Sony S-Gamut3.Cine",
      .primaries = ColorPrimaries::SonySGamut3Cine,
      .transfer_function = TransferFunction::Linear,
      .white_point = WhitePoint::D65,
      .scene_referred = true,
      .wide_gamut = true,
  };
}

std::optional<RgbColorSpace> resolve_builtin_rgb_color_space(
    std::string_view name) {
  const auto normalized = normalized_color_space_identifier(name);
  if (normalized == "srgb") {
    return make_srgb_color_space();
  }
  if (normalized == "linearsrgb") {
    return make_linear_srgb_color_space();
  }
  if (normalized == "displayp3") {
    return make_display_p3_color_space();
  }
  if (normalized == "lineardisplayp3") {
    return make_linear_display_p3_color_space();
  }
  if (normalized == "adobergb" || normalized == "adobergb1998") {
    return make_adobe_rgb_color_space();
  }
  if (normalized == "prophotorgb") {
    return make_prophoto_rgb_color_space();
  }
  if (normalized == "linearprophotorgb") {
    return make_linear_prophoto_rgb_color_space();
  }
  if (normalized == "rec709" || normalized == "bt709") {
    return make_rec709_color_space();
  }
  if (normalized == "linearrec709" || normalized == "linearbt709") {
    return make_linear_rec709_color_space();
  }
  if (normalized == "rec2020" || normalized == "bt2020") {
    return make_rec2020_color_space();
  }
  if (normalized == "linearrec2020" || normalized == "linearbt2020") {
    return make_linear_rec2020_color_space();
  }
  if (normalized == "linearsgamut3") {
    return make_linear_sony_s_gamut3_color_space();
  }
  if (normalized == "linearsgamut3cine") {
    return make_linear_sony_s_gamut3_cine_color_space();
  }
  if (normalized == "aces20651" || normalized == "acesap0") {
    return make_aces2065_1_color_space();
  }
  if (normalized == "acescg" || normalized == "acesap1") {
    return make_acescg_color_space();
  }
  if (normalized == "acescct") {
    return make_acescct_color_space();
  }
  return std::nullopt;
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

std::string normalize_gamut_mapping_policy_name(std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized == "clip" || normalized == "clipping" ||
      normalized == "relative colorimetric" || normalized == "relative-colorimetric") {
    return "Clip";
  }
  if (normalized == "preserve saturation" || normalized == "preserve-saturation") {
    return "Preserve Saturation";
  }
  return "Perceptual";
}

const char* display_eotf_name(DisplayEotf eotf) {
  switch (eotf) {
    case DisplayEotf::Gamma18:
      return "gamma-1.8";
    case DisplayEotf::Gamma22:
      return "gamma-2.2";
    case DisplayEotf::Gamma24:
      return "gamma-2.4";
    case DisplayEotf::Pq:
      return "pq-st2084";
    case DisplayEotf::Hlg:
      return "hlg-bt2100";
    case DisplayEotf::Linear:
      return "linear";
    case DisplayEotf::Srgb:
    default:
      return "srgb";
  }
}

DisplayEotf display_eotf_from_name(std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized == "gamma-1.8" || normalized == "gamma 1.8") {
    return DisplayEotf::Gamma18;
  }
  if (normalized == "gamma-2.2" || normalized == "gamma 2.2") {
    return DisplayEotf::Gamma22;
  }
  if (normalized == "gamma-2.4" || normalized == "gamma 2.4" ||
      normalized == "bt.1886") {
    return DisplayEotf::Gamma24;
  }
  if (normalized == "pq" || normalized == "pq-st2084" ||
      normalized == "st2084") {
    return DisplayEotf::Pq;
  }
  if (normalized == "hlg" || normalized == "hlg-bt2100" ||
      normalized == "bt.2100 hlg") {
    return DisplayEotf::Hlg;
  }
  if (normalized == "linear") {
    return DisplayEotf::Linear;
  }
  return DisplayEotf::Srgb;
}

const char* display_surround_name(DisplaySurround surround) {
  switch (surround) {
    case DisplaySurround::Dark:
      return "dark";
    case DisplaySurround::Average:
      return "average";
    case DisplaySurround::Dim:
    default:
      return "dim";
  }
}

DisplaySurround display_surround_from_name(std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized == "dark") {
    return DisplaySurround::Dark;
  }
  if (normalized == "average") {
    return DisplaySurround::Average;
  }
  return DisplaySurround::Dim;
}

const char* display_mastering_mode_name(DisplayMasteringMode mode) {
  switch (mode) {
    case DisplayMasteringMode::HdrSceneReferred:
      return "hdr-scene-referred";
    case DisplayMasteringMode::HdrDisplayReferred:
      return "hdr-display-referred";
    case DisplayMasteringMode::Sdr:
    default:
      return "sdr";
  }
}

DisplayMasteringMode display_mastering_mode_from_name(std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized == "hdr-scene-referred" || normalized == "hdr scene referred") {
    return DisplayMasteringMode::HdrSceneReferred;
  }
  if (normalized == "hdr-display-referred" || normalized == "hdr display referred") {
    return DisplayMasteringMode::HdrDisplayReferred;
  }
  return DisplayMasteringMode::Sdr;
}

const char* photographic_rendering_model_name(
    PhotographicRenderingModel model) {
  switch (model) {
    case PhotographicRenderingModel::Aces2OcioV1:
      return "aces2-ocio-v1";
    case PhotographicRenderingModel::Aces2HellwigJmhV1:
      return "aces2-hellwig-jmh-v1";
    case PhotographicRenderingModel::LegacyOklchV1:
    default:
      return "legacy-oklch-v1";
  }
}

PhotographicRenderingModel photographic_rendering_model_from_name(
    std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized == "legacy-oklch-v1") {
    return PhotographicRenderingModel::LegacyOklchV1;
  }
  if (normalized == "aces2-ocio-v1" || normalized == "aces 2" ||
      normalized == "aces2" || normalized == "ocio aces 2") {
    return PhotographicRenderingModel::Aces2OcioV1;
  }
  if (normalized == "aces2-hellwig-jmh-v1" ||
      normalized == "hellwig jmh") {
    return PhotographicRenderingModel::Aces2HellwigJmhV1;
  }
  return PhotographicRenderingModel::Aces2OcioV1;
}

const char* renderer_transition_look_name(RendererTransitionLook look) {
  switch (look) {
    case RendererTransitionLook::PreserveLegacyDirectLuminanceV1:
      return "preserve-legacy-direct-luminance-v1";
    case RendererTransitionLook::None:
    default:
      return "none";
  }
}

RendererTransitionLook renderer_transition_look_from_name(
    std::string_view value) {
  const auto normalized = trimmed_lower(value);
  return normalized == "preserve-legacy-direct-luminance-v1"
             ? RendererTransitionLook::PreserveLegacyDirectLuminanceV1
             : RendererTransitionLook::None;
}

bool photographic_rendering_model_uses_aces2(
    PhotographicRenderingModel model) {
  return model == PhotographicRenderingModel::Aces2HellwigJmhV1 ||
         model == PhotographicRenderingModel::Aces2OcioV1;
}

bool photographic_rendering_model_uses_official_aces2(
    PhotographicRenderingModel model) {
  return model == PhotographicRenderingModel::Aces2OcioV1;
}

DisplayTarget make_sdr_display_target(const OutputColorProfile& profile) {
  auto eotf = DisplayEotf::Srgb;
  switch (profile.kind) {
    case OutputTransformKind::ProPhotoRgb:
      eotf = DisplayEotf::Gamma18;
      break;
    case OutputTransformKind::AdobeRgb:
      eotf = DisplayEotf::Gamma22;
      break;
    case OutputTransformKind::Rec709:
    case OutputTransformKind::Rec2020:
      eotf = DisplayEotf::Gamma24;
      break;
    case OutputTransformKind::Srgb:
    case OutputTransformKind::DisplayP3:
    default:
      eotf = DisplayEotf::Srgb;
      break;
  }
  return DisplayTarget{
      .target_name = profile.profile_name + " SDR 100 nits",
      .color_space = profile.color_space,
      .eotf = eotf,
      .peak_luminance_nits = 100.0,
      .black_luminance_nits = 0.005,
      .reference_white_nits = 100.0,
      .surround_luminance_nits = 5.0,
      .surround = DisplaySurround::Dim,
      .mastering_mode = DisplayMasteringMode::Sdr,
  };
}

bool display_target_valid(const DisplayTarget& target) {
  if (target.target_name.empty() || target.color_space.name.empty() ||
      !std::isfinite(target.peak_luminance_nits) ||
      !std::isfinite(target.black_luminance_nits) ||
      !std::isfinite(target.reference_white_nits) ||
      !std::isfinite(target.surround_luminance_nits)) {
    return false;
  }
  if (target.peak_luminance_nits <= 0.0 || target.black_luminance_nits < 0.0 ||
      target.black_luminance_nits >= target.peak_luminance_nits ||
      target.reference_white_nits <= target.black_luminance_nits ||
      target.reference_white_nits > target.peak_luminance_nits ||
      target.surround_luminance_nits < 0.0) {
    return false;
  }
  const auto hdr_eotf = target.eotf == DisplayEotf::Pq || target.eotf == DisplayEotf::Hlg;
  const auto hdr_mode = target.mastering_mode != DisplayMasteringMode::Sdr;
  return hdr_eotf == hdr_mode;
}

double display_target_linear_headroom(const DisplayTarget& target) {
  if (!display_target_valid(target)) {
    return 1.0;
  }
  return std::clamp(
      target.peak_luminance_nits / target.reference_white_nits,
      1.0,
      100.0);
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

GamutMappingPolicy gamut_mapping_policy_from_name(std::string_view value) {
  const auto normalized = normalize_gamut_mapping_policy_name(value);
  if (normalized == "Clip") {
    return GamutMappingPolicy::Clip;
  }
  if (normalized == "Preserve Saturation") {
    return GamutMappingPolicy::PreserveSaturation;
  }
  return GamutMappingPolicy::Perceptual;
}

const char* gamut_mapping_policy_name(GamutMappingPolicy policy) {
  switch (policy) {
    case GamutMappingPolicy::Clip:
      return "Clip";
    case GamutMappingPolicy::PreserveSaturation:
      return "Preserve Saturation";
    case GamutMappingPolicy::Perceptual:
    default:
      return "Perceptual";
  }
}

CameraProfileRenderingModel camera_profile_rendering_model_from_name(
    std::string_view value) {
  const auto normalized = trimmed_lower(value);
  if (normalized == "matrix-only-v1" || normalized == "matrix only v1" ||
      normalized == "legacy") {
    return CameraProfileRenderingModel::MatrixOnlyV1;
  }
  return CameraProfileRenderingModel::DngProfileV2;
}

const char* camera_profile_rendering_model_name(
    CameraProfileRenderingModel model) {
  switch (model) {
    case CameraProfileRenderingModel::MatrixOnlyV1:
      return "matrix-only-v1";
    case CameraProfileRenderingModel::DngProfileV2:
    default:
      return "dng-profile-v2";
  }
}

const char* camera_profile_selection_source_name(
    CameraProfileSelectionSource source) {
  switch (source) {
    case CameraProfileSelectionSource::ExplicitUserDcp:
      return "explicit-user-dcp";
    case CameraProfileSelectionSource::ExplicitUserIcc:
      return "explicit-user-icc";
    case CameraProfileSelectionSource::EmbeddedCameraProfile:
      return "embedded-camera-profile";
    case CameraProfileSelectionSource::BundledCameraProfile:
      return "bundled-camera-profile";
    case CameraProfileSelectionSource::DecoderCharacterization:
      return "decoder-characterization";
    case CameraProfileSelectionSource::UnprofiledFallback:
      return "unprofiled-fallback";
    case CameraProfileSelectionSource::None:
    default:
      return "none";
  }
}

int camera_profile_selection_precedence_rank(
    CameraProfileSelectionSource source) {
  switch (source) {
    case CameraProfileSelectionSource::ExplicitUserDcp:
    case CameraProfileSelectionSource::ExplicitUserIcc:
      return 1;
    case CameraProfileSelectionSource::EmbeddedCameraProfile:
      return 2;
    case CameraProfileSelectionSource::BundledCameraProfile:
      return 3;
    case CameraProfileSelectionSource::DecoderCharacterization:
      return 4;
    case CameraProfileSelectionSource::UnprofiledFallback:
      return 5;
    case CameraProfileSelectionSource::None:
    default:
      return 0;
  }
}

CameraProfile resolve_camera_profile(
    const DecodedImageMetadata& metadata,
    const InputColorProfile& input_profile,
    const DcpProfile* parsed_dcp_profile,
    const ResolvedIccProfileAsset* resolved_icc_profile_asset) {
  const auto requested_source =
      input_profile.kind == InputTransformKind::CameraDcpProfile
          ? CameraProfileSelectionSource::ExplicitUserDcp
          : (input_profile.kind == InputTransformKind::CameraIccProfile
                 ? CameraProfileSelectionSource::ExplicitUserIcc
                 : CameraProfileSelectionSource::None);
  const auto requested_format =
      requested_source == CameraProfileSelectionSource::ExplicitUserDcp
          ? "DCP"
          : (requested_source == CameraProfileSelectionSource::ExplicitUserIcc
                 ? "ICC"
                 : "Generic");
  const auto unavailable_profile = [&](std::string failure_reason) {
    return CameraProfile{
        .valid = false,
        .profile_name = input_profile.profile_name,
        .profile_format = requested_format,
        .selection_source = requested_source,
        .selection_precedence_rank =
            camera_profile_selection_precedence_rank(requested_source),
        .fallback_used = false,
        .camera_make = trim_copy(metadata.camera_make),
        .camera_model = trim_copy(metadata.camera_model),
        .failure_reason = std::move(failure_reason),
    };
  };
  if (!metadata.valid) {
    return requested_source == CameraProfileSelectionSource::None
               ? CameraProfile{}
               : unavailable_profile("decoded_image_metadata_required");
  }
  if (metadata.is_raw && !decoded_image_color_boundary_valid(metadata)) {
    if (requested_source != CameraProfileSelectionSource::None) {
      return unavailable_profile("raw_decode_color_boundary_invalid");
    }
    return CameraProfile{
        .valid = false,
        .profile_name = metadata.camera_profile_identity.empty()
                            ? "Invalid RAW Decode Boundary"
                            : metadata.camera_profile_identity,
        .profile_format = "Invalid Decode Boundary",
        .selection_source = CameraProfileSelectionSource::None,
        .selection_precedence_rank = 0,
        .fallback_used = false,
        .camera_make = trim_copy(metadata.camera_make),
        .camera_model = trim_copy(metadata.camera_model),
        .failure_reason = "raw_decode_color_boundary_invalid",
    };
  }
  if (metadata.is_raw && decoded_image_color_boundary_valid(metadata) &&
      !decoded_image_requires_engine_input_transform(metadata)) {
    if (requested_source != CameraProfileSelectionSource::None) {
      return unavailable_profile(
          requested_source == CameraProfileSelectionSource::ExplicitUserDcp
              ? "dcp_profile_requires_camera_linear_raw"
              : "camera_icc_profile_requires_camera_linear_raw");
    }
    return CameraProfile{
        .valid = false,
        .profile_name = metadata.camera_profile_identity.empty()
                            ? metadata.decoder_backend
                            : metadata.camera_profile_identity,
        .profile_format = "Decoder Applied",
        .selection_source = CameraProfileSelectionSource::DecoderCharacterization,
        .selection_precedence_rank = camera_profile_selection_precedence_rank(
            CameraProfileSelectionSource::DecoderCharacterization),
        .fallback_used = false,
        .camera_make = trim_copy(metadata.camera_make),
        .camera_model = trim_copy(metadata.camera_model),
        .execution_model = "decoder-characterization:v1",
    };
  }
  if (!metadata.is_raw) {
    if (requested_source != CameraProfileSelectionSource::None) {
      return unavailable_profile("camera_profile_requires_raw_source");
    }
    return CameraProfile{
        .valid = false,
        .profile_name = input_profile.profile_name,
        .profile_format = "Embedded",
        .selection_source = CameraProfileSelectionSource::EmbeddedCameraProfile,
        .selection_precedence_rank = camera_profile_selection_precedence_rank(
            CameraProfileSelectionSource::EmbeddedCameraProfile),
        .fallback_used = false,
        .camera_make = trim_copy(metadata.camera_make),
        .camera_model = trim_copy(metadata.camera_model),
        .execution_model = "embedded-rgb-input:v1",
    };
  }

  CameraProfile profile{
      .valid = true,
      .profile_name = "Generic Camera",
      .profile_format = "Unprofiled Fallback",
      .selection_source = CameraProfileSelectionSource::UnprofiledFallback,
      .selection_precedence_rank = camera_profile_selection_precedence_rank(
          CameraProfileSelectionSource::UnprofiledFallback),
      .fallback_used = true,
      .camera_make = trim_copy(metadata.camera_make),
      .camera_model = trim_copy(metadata.camera_model),
      .execution_model = "unprofiled-camera-identity:v1",
  };
  if (!profile.camera_make.empty() || !profile.camera_model.empty()) {
    profile.profile_name = profile.camera_make;
    if (!profile.profile_name.empty() && !profile.camera_model.empty()) {
      profile.profile_name += " ";
    }
    profile.profile_name += profile.camera_model;
  }

  if (input_profile.kind == InputTransformKind::CameraDcpProfile) {
    profile.valid = false;
    profile.profile_name = input_profile.profile_name;
    profile.profile_format = "DCP";
    profile.selection_source = CameraProfileSelectionSource::ExplicitUserDcp;
    profile.selection_precedence_rank = camera_profile_selection_precedence_rank(
        profile.selection_source);
    profile.fallback_used = false;
    if (parsed_dcp_profile == nullptr) {
      profile.failure_reason = "dcp_profile_asset_required";
      return profile;
    }
    if (parsed_dcp_profile->payload_sha256.empty() ||
        parsed_dcp_profile->byte_size == 0U) {
      profile.failure_reason = "dcp_profile_asset_identity_invalid";
      return profile;
    }
    if (dcp_profile_camera_compatibility(
            *parsed_dcp_profile,
            metadata.camera_make,
            metadata.camera_model) == DcpCameraCompatibility::Mismatch) {
      profile.failure_reason = "dcp_profile_camera_mismatch";
      return profile;
    }
    if (!parsed_dcp_profile->has_executable_color_transform()) {
      profile.failure_reason = "dcp_color_transform_required";
      return profile;
    }
    const auto first_temperature =
        dcp_profile_calibration_illuminant_temperature(
            *parsed_dcp_profile,
            0U);
    if (!first_temperature.has_value()) {
      profile.failure_reason = "dcp_calibration_illuminant1_unsupported";
      return profile;
    }
    const auto second_temperature =
        dcp_profile_calibration_illuminant_temperature(
            *parsed_dcp_profile,
            1U);
    if (parsed_dcp_profile->color_matrix2.has_value() &&
        !second_temperature.has_value()) {
      profile.failure_reason = "dcp_calibration_illuminant2_unsupported";
      return profile;
    }

    const auto resolved_transform = dcp_camera_to_xyz_d50(
        *parsed_dcp_profile,
        metadata.as_shot_temperature,
        metadata.as_shot_tint);
    if (!resolved_transform.valid) {
      profile.failure_reason = resolved_transform.failure_reason;
      return profile;
    }

    profile.valid = true;
    profile.profile_name = parsed_dcp_profile->profile_name.empty()
                               ? input_profile.profile_name
                               : parsed_dcp_profile->profile_name;
    profile.payload_sha256 = parsed_dcp_profile->payload_sha256;
    profile.payload_byte_size = parsed_dcp_profile->byte_size;
    profile.camera_to_xyz_d50 = resolved_transform.camera_to_xyz_d50;
    const auto first_endpoint = dcp_camera_to_xyz_d50(
        *parsed_dcp_profile,
        *first_temperature,
        0.0);
    const auto second_endpoint = second_temperature.has_value()
                                     ? dcp_camera_to_xyz_d50(
                                           *parsed_dcp_profile,
                                           *second_temperature,
                                           0.0)
                                     : first_endpoint;
    profile.camera_to_xyz_d50_tungsten = first_endpoint.valid
                                                ? first_endpoint.camera_to_xyz_d50
                                                : resolved_transform.camera_to_xyz_d50;
    profile.camera_to_xyz_d50_daylight = second_endpoint.valid
                                                ? second_endpoint.camera_to_xyz_d50
                                                : resolved_transform.camera_to_xyz_d50;
    profile.dual_illuminant = parsed_dcp_profile->dual_illuminant();
    profile.triple_illuminant = parsed_dcp_profile->triple_illuminant();
    profile.illuminant_data_kinds = parsed_dcp_profile->illuminant_data_kinds;
    profile.execution_model = "dng-camera-to-pcs:v1";
    profile.second_illuminant_weight =
        resolved_transform.second_illuminant_weight;
    profile.illuminant_weights = resolved_transform.illuminant_weights;
    profile.used_forward_matrix = resolved_transform.used_forward_matrix;
    profile.used_camera_calibration =
        resolved_transform.used_camera_calibration;
    profile.has_hue_sat_map =
        parsed_dcp_profile->hue_sat_map1.configured();
    profile.has_profile_gain_table_map =
        parsed_dcp_profile->profile_gain_table_map.configured();
    profile.has_rgb_tables = parsed_dcp_profile->rgb_tables.configured();
    profile.has_look_table = parsed_dcp_profile->look_table.configured();
    profile.has_profile_tone_curve =
        parsed_dcp_profile->tone_curve.size() >= 2U;
    profile.profile_tone_method =
        dcp_profile_tone_method_name(parsed_dcp_profile->tone_method);
    profile.profile_dynamic_range_hdr =
        parsed_dcp_profile->dynamic_range.has_value() &&
        parsed_dcp_profile->dynamic_range->hdr;
    profile.profile_hint_max_output_value =
        parsed_dcp_profile->dynamic_range.has_value()
            ? parsed_dcp_profile->dynamic_range->hint_max_output_value
            : 0.0;
    profile.baseline_exposure_offset =
        parsed_dcp_profile->baseline_exposure.value_or(0.0);
    profile.tungsten_temperature = *first_temperature;
    profile.daylight_temperature = second_temperature.value_or(*first_temperature);
    profile.reference_temperature = metadata.as_shot_temperature;
    profile.reference_tint = metadata.as_shot_tint;
    return profile;
  }

  if (input_profile.kind == InputTransformKind::CameraIccProfile) {
    profile.valid = false;
    profile.profile_name = input_profile.profile_name;
    profile.profile_format = "ICC";
    profile.selection_source = CameraProfileSelectionSource::ExplicitUserIcc;
    profile.selection_precedence_rank = camera_profile_selection_precedence_rank(
        profile.selection_source);
    profile.fallback_used = false;
    if (resolved_icc_profile_asset == nullptr ||
        !resolved_icc_profile_asset->ready()) {
      profile.failure_reason = "camera_icc_profile_asset_required";
      return profile;
    }
    profile.valid = true;
    profile.profile_name =
        resolved_icc_profile_asset->reference.display_name.empty()
            ? input_profile.profile_name
            : resolved_icc_profile_asset->reference.display_name;
    profile.payload_sha256 =
        resolved_icc_profile_asset->reference.payload_sha256;
    profile.payload_byte_size =
        resolved_icc_profile_asset->reference.byte_size;
    // The immutable ICC payload is executed by Little CMS while the decoded
    // camera-linear buffer is promoted into the selected working space. Keep
    // the adjustment-stage matrix at identity so the characterization is not
    // fabricated or applied twice.
    profile.execution_model = "littlecms-input-icc-to-working:v1";
    profile.reference_temperature = metadata.as_shot_temperature;
    profile.reference_tint = metadata.as_shot_tint;
    return profile;
  }

  if (metadata.decoder_camera_to_xyz_valid &&
      (metadata.decoder_camera_to_xyz_reference_white == "D50" ||
       metadata.decoder_camera_to_xyz_reference_white == "D65")) {
    profile.valid = true;
    profile.profile_format = "LibRaw Camera Matrix";
    profile.selection_source =
        CameraProfileSelectionSource::DecoderCharacterization;
    profile.selection_precedence_rank = camera_profile_selection_precedence_rank(
        profile.selection_source);
    profile.fallback_used = false;
    profile.camera_to_xyz_d50 = metadata.decoder_camera_to_xyz;
    profile.camera_to_xyz_d50_tungsten = metadata.decoder_camera_to_xyz;
    profile.camera_to_xyz_d50_daylight = metadata.decoder_camera_to_xyz;
    profile.execution_model =
        metadata.decoder_camera_to_xyz_reference_white == "D50"
            ? "libraw-camera-to-xyz-d50:v1"
            : "libraw-camera-to-xyz-d65:v1";
  } else {
    profile.valid = false;
    profile.failure_reason = "camera_linear_characterization_required";
  }

  // Camera make/model is identity and matching metadata, never a substitute
  // for measured white-balance or characterization data.
  profile.reference_temperature = metadata.as_shot_temperature;
  profile.reference_tint = metadata.as_shot_tint;
  return profile;
}

CameraProfile resolve_camera_profile(
    const DecodedImageMetadata& metadata,
    const InputColorProfile& input_profile,
    const DcpProfile* parsed_dcp_profile) {
  return resolve_camera_profile(
      metadata,
      input_profile,
      parsed_dcp_profile,
      nullptr);
}

CameraProfile resolve_camera_profile(
    const DecodedImageMetadata& metadata,
    const InputColorProfile& input_profile) {
  return resolve_camera_profile(metadata, input_profile, nullptr);
}

CameraProfile resolve_camera_profile(const DecodedImageMetadata& metadata) {
  return resolve_camera_profile(metadata, input_color_profile_from_name(metadata.input_profile_name));
}

InputTransformRecipe resolve_input_transform_recipe(
    const InputColorProfile& input_profile,
    const CameraProfile& camera_profile,
    const DecodedImageMetadata& metadata) {
  InputTransformRecipe recipe{};
  recipe.decoder_white_balance_applied =
      metadata.valid && metadata.is_raw && metadata.white_balance_applied;
  recipe.camera_profile_name = camera_profile.valid ? camera_profile.profile_name : std::string{};
  recipe.baseline_exposure =
      metadata.baseline_exposure + camera_profile.baseline_exposure_offset;
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
  const auto preview_profile = output_color_profile_from_name("sRGB");
  const auto export_profile = output_color_profile_from_name("sRGB");
  const auto proof_profile = output_color_profile_from_name("sRGB");
  return ColorPipelineConfig{
      .input_profile = input_color_profile_from_name("Embedded"),
      .working_profile = working_color_profile_from_name("Linear ProPhoto RGB"),
      .preview_output_profile = preview_profile,
      .export_output_profile = export_profile,
      .proof_output_profile = proof_profile,
      .preview_display_target = make_sdr_display_target(preview_profile),
      .export_display_target = make_sdr_display_target(export_profile),
      .photographic_rendering_model = PhotographicRenderingModel::Aces2OcioV1,
      .camera_profile = {},
      .input_transform_recipe = {},
      .render_intent = RenderIntent::Perceptual,
      .proof_render_intent = RenderIntent::RelativeColorimetric,
      .soft_proof_enabled = false,
      .proof_gamut_check_enabled = false,
      .proof_simulate_paper_white = false,
      .proof_simulate_ink_black = false,
      .output_gamut_mapping_enabled = true,
      .output_gamut_mapping_policy = GamutMappingPolicy::Perceptual,
      .output_headroom = 1.0,
      .black_point_compensation_enabled = true,
      .source_black_point = 0.0,
      .output_black_point = 0.0,
  };
}

}  // namespace photo_editor::pipeline

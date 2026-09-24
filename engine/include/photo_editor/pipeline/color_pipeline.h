#pragma once

#include "photo_editor/pipeline/color_profile_asset.h"
#include "photo_editor/pipeline/color_space.h"
#include "photo_editor/pipeline/dcp_profile.h"
#include "photo_editor/pipeline/dcp_profile_asset.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace photo_editor::pipeline {

enum class InputTransformKind {
  EmbeddedProfile,
  CameraNative,
  CameraDcpProfile,
  CameraIccProfile,
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

enum class GamutMappingPolicy {
  Perceptual,
  Clip,
  PreserveSaturation,
};

enum class DisplayEotf {
  Srgb,
  Gamma18,
  Gamma22,
  Gamma24,
  Pq,
  Hlg,
  Linear,
};

enum class DisplaySurround {
  Dark,
  Dim,
  Average,
};

enum class DisplayMasteringMode {
  Sdr,
  HdrSceneReferred,
  HdrDisplayReferred,
};

enum class PhotographicRenderingModel {
  LegacyOklchV1,
  Aces2HellwigJmhV1,
  Aces2OcioV1,
};

enum class RendererTransitionLook {
  None,
  PreserveLegacyDirectLuminanceV1,
};

struct DisplayTarget final {
  std::string target_name = "sRGB SDR 100 nits";
  RgbColorSpace color_space = make_srgb_color_space();
  DisplayEotf eotf = DisplayEotf::Srgb;
  double peak_luminance_nits = 100.0;
  double black_luminance_nits = 0.005;
  double reference_white_nits = 100.0;
  double surround_luminance_nits = 5.0;
  DisplaySurround surround = DisplaySurround::Dim;
  DisplayMasteringMode mastering_mode = DisplayMasteringMode::Sdr;
};

enum class CameraProfileRenderingModel {
  MatrixOnlyV1,
  DngProfileV2,
};

// Camera characterization is selected by one deterministic policy. Lower
// precedence ranks win; a requested higher-priority source fails closed
// instead of silently falling through to a different characterization.
enum class CameraProfileSelectionSource {
  None,
  ExplicitUserDcp,
  ExplicitUserIcc,
  EmbeddedCameraProfile,
  BundledCameraProfile,
  DecoderCharacterization,
  UnprofiledFallback,
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

enum class DecodedPixelEncoding {
  Unknown,
  SensorMosaic,
  CameraLinearRgb,
  SceneLinearRgb,
  DisplayReferredRgb,
};

struct DecodedImageMetadata final {
  bool valid = false;
  bool is_raw = false;
  DecodedPixelEncoding pixel_encoding = DecodedPixelEncoding::Unknown;
  std::string decoder_backend;
  std::string decoded_color_space_name;
  std::string camera_profile_identity;
  bool black_subtraction_applied = false;
  bool white_normalization_applied = false;
  bool demosaic_applied = false;
  bool white_balance_applied = false;
  bool highlight_reconstruction_applied = false;
  bool lens_correction_applied = false;
  bool baseline_exposure_applied = false;
  bool camera_transform_applied = false;
  bool decoder_rendering_applied = false;
  // LibRaw's camera-RGB to CIE XYZ characterization. This is carried across
  // the decode boundary so ordinary camera-linear RAW never falls back to an
  // identity color transform. The reference white is explicit because DNG
  // forward transforms use D50 while LibRaw's native cam_xyz fallback is D65.
  bool decoder_camera_to_xyz_valid = false;
  std::array<double, 9> decoder_camera_to_xyz = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  std::string decoder_camera_to_xyz_reference_white;
  std::string input_profile_name = "Embedded";
  std::string camera_make;
  std::string camera_model;
  double as_shot_temperature = 5200.0;
  double as_shot_tint = 0.0;
  double baseline_exposure = 0.0;
  double highlight_headroom = 1.0;
};

[[nodiscard]] const char* decoded_pixel_encoding_name(DecodedPixelEncoding encoding);
[[nodiscard]] bool decoded_image_color_boundary_valid(
    const DecodedImageMetadata& metadata);
[[nodiscard]] bool decoded_image_requires_engine_input_transform(
    const DecodedImageMetadata& metadata);
[[nodiscard]] bool decoded_image_accepts_custom_input_profile(
    const DecodedImageMetadata& metadata);

struct CameraProfile final {
  bool valid = false;
  std::string profile_name = "Unknown Camera";
  std::string profile_format = "Generic";
  CameraProfileSelectionSource selection_source = CameraProfileSelectionSource::None;
  int selection_precedence_rank = 0;
  bool fallback_used = false;
  std::string camera_make;
  std::string camera_model;
  std::string payload_sha256;
  std::uint64_t payload_byte_size = 0U;
  std::array<double, 9> camera_to_xyz_d50 = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  std::array<double, 9> camera_to_xyz_d50_tungsten = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  std::array<double, 9> camera_to_xyz_d50_daylight = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  bool dual_illuminant = false;
  bool triple_illuminant = false;
  std::array<DcpIlluminantDataKind, 3> illuminant_data_kinds = {
      DcpIlluminantDataKind::None,
      DcpIlluminantDataKind::None,
      DcpIlluminantDataKind::None,
  };
  std::string execution_model = "legacy-camera-matrix:v1";
  double second_illuminant_weight = 0.0;
  std::array<double, 3> illuminant_weights = {1.0, 0.0, 0.0};
  bool used_forward_matrix = false;
  bool used_camera_calibration = false;
  bool has_hue_sat_map = false;
  bool has_profile_gain_table_map = false;
  bool has_rgb_tables = false;
  bool has_look_table = false;
  bool has_profile_tone_curve = false;
  std::string profile_tone_method = "unspecified";
  bool profile_dynamic_range_hdr = false;
  double profile_hint_max_output_value = 0.0;
  double baseline_exposure_offset = 0.0;
  double tungsten_temperature = 2850.0;
  double daylight_temperature = 6500.0;
  double reference_temperature = 5200.0;
  double reference_tint = 0.0;
  std::string failure_reason;
};

struct InputTransformRecipe final {
  bool has_source_white_balance = false;
  bool decoder_white_balance_applied = false;
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
  OutputColorProfile proof_output_profile;
  ResolvedIccProfileAsset input_icc_profile_asset;
  ResolvedIccProfileAsset preview_icc_profile_asset;
  ResolvedIccProfileAsset proof_icc_profile_asset;
  ResolvedIccProfileAsset export_icc_profile_asset;
  ResolvedDcpProfileAsset camera_dcp_profile_asset;
  DisplayTarget preview_display_target;
  DisplayTarget export_display_target;
  PhotographicRenderingModel photographic_rendering_model =
      PhotographicRenderingModel::Aces2OcioV1;
  RendererTransitionLook renderer_transition_look =
      RendererTransitionLook::None;
  CameraProfileRenderingModel camera_profile_rendering_model =
      CameraProfileRenderingModel::DngProfileV2;
  CameraProfile camera_profile;
  InputTransformRecipe input_transform_recipe;
  RenderIntent render_intent = RenderIntent::Perceptual;
  RenderIntent proof_render_intent = RenderIntent::RelativeColorimetric;
  bool soft_proof_enabled = false;
  bool proof_gamut_check_enabled = false;
  bool proof_simulate_paper_white = false;
  bool proof_simulate_ink_black = false;
  bool output_gamut_mapping_enabled = true;
  GamutMappingPolicy output_gamut_mapping_policy = GamutMappingPolicy::Perceptual;
  double output_headroom = 1.0;
  bool black_point_compensation_enabled = true;
  double source_black_point = 0.0;
  double output_black_point = 0.0;
};

[[nodiscard]] std::string normalize_input_color_profile_name(std::string_view value);
[[nodiscard]] std::string normalize_working_color_profile_name(std::string_view value);
[[nodiscard]] std::string normalize_output_color_profile_name(std::string_view value);
[[nodiscard]] std::string normalize_render_intent_name(std::string_view value);
[[nodiscard]] std::string normalize_gamut_mapping_policy_name(std::string_view value);
[[nodiscard]] const char* display_eotf_name(DisplayEotf eotf);
[[nodiscard]] DisplayEotf display_eotf_from_name(std::string_view value);
[[nodiscard]] const char* display_surround_name(DisplaySurround surround);
[[nodiscard]] DisplaySurround display_surround_from_name(std::string_view value);
[[nodiscard]] const char* display_mastering_mode_name(DisplayMasteringMode mode);
[[nodiscard]] DisplayMasteringMode display_mastering_mode_from_name(
    std::string_view value);
[[nodiscard]] const char* photographic_rendering_model_name(
    PhotographicRenderingModel model);
[[nodiscard]] PhotographicRenderingModel photographic_rendering_model_from_name(
    std::string_view value);
[[nodiscard]] const char* renderer_transition_look_name(
    RendererTransitionLook look);
[[nodiscard]] RendererTransitionLook renderer_transition_look_from_name(
    std::string_view value);
[[nodiscard]] bool photographic_rendering_model_uses_aces2(
    PhotographicRenderingModel model);
[[nodiscard]] bool photographic_rendering_model_uses_official_aces2(
    PhotographicRenderingModel model);
[[nodiscard]] DisplayTarget make_sdr_display_target(
    const OutputColorProfile& profile);
[[nodiscard]] bool display_target_valid(const DisplayTarget& target);
[[nodiscard]] double display_target_linear_headroom(const DisplayTarget& target);

[[nodiscard]] InputColorProfile input_color_profile_from_name(std::string_view value);
[[nodiscard]] WorkingColorProfile working_color_profile_from_name(std::string_view value);
[[nodiscard]] OutputColorProfile output_color_profile_from_name(std::string_view value);
[[nodiscard]] RenderIntent render_intent_from_name(std::string_view value);
[[nodiscard]] const char* render_intent_name(RenderIntent intent);
[[nodiscard]] GamutMappingPolicy gamut_mapping_policy_from_name(std::string_view value);
[[nodiscard]] const char* gamut_mapping_policy_name(GamutMappingPolicy policy);
[[nodiscard]] CameraProfileRenderingModel camera_profile_rendering_model_from_name(
    std::string_view value);
[[nodiscard]] const char* camera_profile_rendering_model_name(
    CameraProfileRenderingModel model);
[[nodiscard]] const char* camera_profile_selection_source_name(
    CameraProfileSelectionSource source);
[[nodiscard]] int camera_profile_selection_precedence_rank(
    CameraProfileSelectionSource source);
[[nodiscard]] CameraProfile resolve_camera_profile(const DecodedImageMetadata& metadata);
[[nodiscard]] CameraProfile resolve_camera_profile(
    const DecodedImageMetadata& metadata,
    const InputColorProfile& input_profile);
[[nodiscard]] CameraProfile resolve_camera_profile(
    const DecodedImageMetadata& metadata,
    const InputColorProfile& input_profile,
    const DcpProfile* parsed_dcp_profile);
[[nodiscard]] CameraProfile resolve_camera_profile(
    const DecodedImageMetadata& metadata,
    const InputColorProfile& input_profile,
    const DcpProfile* parsed_dcp_profile,
    const ResolvedIccProfileAsset* resolved_icc_profile_asset);
[[nodiscard]] InputTransformRecipe resolve_input_transform_recipe(
    const InputColorProfile& input_profile,
    const CameraProfile& camera_profile,
    const DecodedImageMetadata& metadata);
[[nodiscard]] ColorPipelineConfig default_color_pipeline_config();

}  // namespace photo_editor::pipeline

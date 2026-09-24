#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

inline constexpr std::size_t kMaximumDcpProfileBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kMaximumDcpTableEntries = 4U * 1024U * 1024U;

enum class DcpParseStatus {
  Ready,
  EmptyPayload,
  PayloadTooLarge,
  InvalidByteOrder,
  InvalidMagic,
  UnsupportedBigTiff,
  TruncatedHeader,
  TruncatedIfd,
  OffsetOutOfRange,
  CountOverflow,
  TooManyIfds,
  TooManyIfdEntries,
  IfdCycle,
  DuplicateTag,
  InvalidTagType,
  InvalidTagCount,
  InvalidTagValue,
  MissingColorMatrix,
  UnsupportedColorPlanes,
};

struct DcpHsvDelta final {
  float hue_shift_degrees = 0.0F;
  float saturation_scale = 1.0F;
  float value_scale = 1.0F;
};

struct DcpHsvTable final {
  std::array<std::uint32_t, 3> dimensions = {0U, 0U, 0U};
  std::vector<DcpHsvDelta> entries;
  bool srgb_value_encoding = false;

  [[nodiscard]] bool configured() const;
};

struct DcpGainTableMap final {
  std::uint32_t points_v = 0U;
  std::uint32_t points_h = 0U;
  double spacing_v = 1.0;
  double spacing_h = 1.0;
  double origin_v = 0.0;
  double origin_h = 0.0;
  std::uint32_t table_points = 0U;
  std::array<float, 5> input_weights = {};
  float gamma = 1.0F;
  std::vector<float> gains;

  [[nodiscard]] bool configured() const;
};

enum class DcpRgbTableCompositeMethod : std::uint32_t {
  WeightedSum = 0U,
  Sequential = 1U,
};

enum class DcpRgbTablePixelType : std::uint8_t {
  Uint8 = 0U,
  Uint16 = 1U,
  Float32 = 2U,
};

enum class DcpRgbTableGamma : std::uint8_t {
  Linear = 0U,
  Srgb = 1U,
  Gamma18 = 2U,
  Gamma22 = 3U,
  Rec2020 = 4U,
};

enum class DcpRgbTablePrimaries : std::uint8_t {
  Srgb = 0U,
  AdobeRgb = 1U,
  DisplayP3 = 2U,
  Rec2020 = 3U,
  ProPhotoRgb = 4U,
};

struct DcpRgbTable final {
  std::string semantic_name;
  std::uint8_t divisions = 0U;
  DcpRgbTablePixelType pixel_type = DcpRgbTablePixelType::Uint8;
  DcpRgbTableGamma gamma = DcpRgbTableGamma::Linear;
  DcpRgbTablePrimaries primaries = DcpRgbTablePrimaries::ProPhotoRgb;
  bool gamut_extension = false;
  std::vector<std::array<float, 3>> entries;

  [[nodiscard]] bool configured() const;
  [[nodiscard]] bool background() const;
};

struct DcpRgbTables final {
  DcpRgbTableCompositeMethod composite_method =
      DcpRgbTableCompositeMethod::WeightedSum;
  std::vector<DcpRgbTable> tables;

  [[nodiscard]] bool configured() const;
};

struct DcpSemanticMaskWeight final {
  std::string_view semantic_name;
  double weight = 0.0;
};

struct DcpChromaticity final {
  double x = 0.0;
  double y = 0.0;

  [[nodiscard]] bool valid() const;
};

enum class DcpIlluminantDataKind {
  None,
  WhiteXy,
  Spectrum,
};

enum class DcpProfileToneMethod : std::uint32_t {
  Unspecified = 0U,
  AdobePv5 = 1U,
  AdobePv6 = 2U,
};

struct DcpProfileDynamicRange final {
  bool hdr = false;
  float hint_max_output_value = 0.0F;
};

struct DcpProfile final {
  std::string payload_sha256;
  std::uint64_t byte_size = 0U;
  std::string profile_name;
  std::string unique_camera_model;
  std::string camera_calibration_signature;
  std::string profile_calibration_signature;
  std::string copyright;
  std::optional<std::uint16_t> calibration_illuminant1;
  std::optional<std::uint16_t> calibration_illuminant2;
  std::optional<std::uint16_t> calibration_illuminant3;
  std::optional<DcpChromaticity> illuminant_data1;
  std::optional<DcpChromaticity> illuminant_data2;
  std::optional<DcpChromaticity> illuminant_data3;
  std::array<DcpIlluminantDataKind, 3> illuminant_data_kinds = {
      DcpIlluminantDataKind::None,
      DcpIlluminantDataKind::None,
      DcpIlluminantDataKind::None,
  };
  std::optional<std::array<double, 9>> color_matrix1;
  std::optional<std::array<double, 9>> color_matrix2;
  std::optional<std::array<double, 9>> color_matrix3;
  std::optional<std::array<double, 9>> camera_calibration1;
  std::optional<std::array<double, 9>> camera_calibration2;
  std::optional<std::array<double, 9>> camera_calibration3;
  std::optional<std::array<double, 9>> reduction_matrix1;
  std::optional<std::array<double, 9>> reduction_matrix2;
  std::optional<std::array<double, 9>> reduction_matrix3;
  std::optional<std::array<double, 9>> forward_matrix1;
  std::optional<std::array<double, 9>> forward_matrix2;
  std::optional<std::array<double, 9>> forward_matrix3;
  std::optional<std::array<double, 3>> analog_balance;
  std::optional<std::array<double, 3>> as_shot_neutral;
  std::optional<double> baseline_exposure;
  DcpHsvTable hue_sat_map1;
  DcpHsvTable hue_sat_map2;
  DcpHsvTable hue_sat_map3;
  DcpHsvTable look_table;
  DcpGainTableMap profile_gain_table_map;
  DcpRgbTables rgb_tables;
  std::vector<std::pair<float, float>> tone_curve;
  DcpProfileToneMethod tone_method = DcpProfileToneMethod::Unspecified;
  std::optional<DcpProfileDynamicRange> dynamic_range;
  std::optional<std::uint32_t> embed_policy;

  [[nodiscard]] bool dual_illuminant() const;
  [[nodiscard]] bool triple_illuminant() const;
  [[nodiscard]] bool has_executable_forward_matrix() const;
  [[nodiscard]] bool has_executable_color_transform() const;
};

struct DcpRgb final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
};

struct DcpProfileRenderPlan final {
  struct RgbTableTransform final {
    std::array<double, 9> profile_to_table = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0,
    };
    std::array<double, 9> table_to_profile = profile_to_table;
  };

  double second_illuminant_weight = 0.0;
  std::array<double, 3> illuminant_weights = {1.0, 0.0, 0.0};
  std::vector<double> tone_curve_slopes;
  std::vector<RgbTableTransform> rgb_table_transforms;
  bool uses_hue_sat_map = false;
  bool uses_look_table = false;
  bool uses_tone_curve = false;
  bool uses_rgb_tables = false;
};

struct DcpProfileRenderPlanCacheStats final {
  std::size_t entries = 0U;
  std::uint64_t hits = 0U;
  std::uint64_t misses = 0U;
  std::uint64_t builds = 0U;
  std::uint64_t evictions = 0U;
  std::size_t capacity = 0U;
};

struct DcpCameraTransform final {
  bool valid = false;
  std::array<double, 9> camera_to_xyz_d50 = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  DcpChromaticity white;
  double second_illuminant_weight = 0.0;
  std::array<double, 3> illuminant_weights = {1.0, 0.0, 0.0};
  bool used_forward_matrix = false;
  bool used_camera_calibration = false;
  std::string failure_reason;
};

struct DcpParseResult final {
  DcpParseStatus status = DcpParseStatus::EmptyPayload;
  DcpProfile profile;
  std::string failure_reason;
  std::uint32_t failing_ifd_offset = 0U;
  std::uint16_t failing_tag = 0U;

  [[nodiscard]] bool ready() const;
};

enum class DcpCameraCompatibility {
  Unknown,
  Match,
  Mismatch,
};

[[nodiscard]] DcpParseResult parse_dcp_profile(std::span<const std::byte> payload);
[[nodiscard]] const char* dcp_parse_status_name(DcpParseStatus status);
[[nodiscard]] const char* dcp_illuminant_data_kind_name(
    DcpIlluminantDataKind kind);
[[nodiscard]] const char* dcp_profile_tone_method_name(
    DcpProfileToneMethod method);
[[nodiscard]] DcpCameraCompatibility dcp_profile_camera_compatibility(
    const DcpProfile& profile,
    std::string_view camera_make,
    std::string_view camera_model);
[[nodiscard]] const char* dcp_camera_compatibility_name(
    DcpCameraCompatibility compatibility);

// Returns the second-illuminant weight. The interpolation is linear in reciprocal
// correlated color temperature, as required by the DNG camera-profile model.
[[nodiscard]] double dcp_reciprocal_temperature_weight(
    double source_temperature_kelvin,
    double illuminant1_temperature_kelvin,
    double illuminant2_temperature_kelvin);

[[nodiscard]] std::optional<double> dcp_calibration_illuminant_temperature(
    std::uint16_t exif_light_source);
[[nodiscard]] std::optional<double> dcp_profile_calibration_illuminant_temperature(
    const DcpProfile& profile,
    std::size_t illuminant_index);

// Canonicalizes each endpoint with DNG profile precision and D50 normalization
// before reciprocal-temperature interpolation.
[[nodiscard]] std::optional<std::array<double, 9>> dcp_interpolated_forward_matrix(
    const DcpProfile& profile,
    double source_temperature_kelvin);

// DNG-compatible conversion between correlated temperature/tint and CIE 1931
// xy using published Robertson data. Positive tint moves toward magenta.
[[nodiscard]] DcpChromaticity dcp_temperature_tint_to_xy(
    double temperature_kelvin,
    double tint);

[[nodiscard]] std::optional<std::pair<double, double>>
dcp_chromaticity_to_temperature_tint(const DcpChromaticity& xy);

// Resolves the DNG two-illuminant camera-to-PCS transform. This includes
// reciprocal-temperature matrix interpolation, AnalogBalance,
// CameraCalibration, camera-domain white balance, ForwardMatrix normalization,
// and the ColorMatrix inversion fallback for three-channel profiles.
[[nodiscard]] DcpCameraTransform dcp_camera_to_xyz_d50(
    const DcpProfile& profile,
    double source_temperature_kelvin,
    double source_tint);

// Applies the DNG ProfileHueSatMap selected for the source illuminant. Within
// the profile's [0, 1] domain this follows the specified trilinear
// interpolation. Outside that domain the in-gamut correction is transferred
// as a bounded delta so signed and HDR working values are not destroyed.
[[nodiscard]] DcpRgb dcp_apply_hue_sat_map(
    const DcpProfile& profile,
    double source_temperature_kelvin,
    DcpRgb rgb);
[[nodiscard]] DcpProfileRenderPlan dcp_prepare_profile_render_plan(
    const DcpProfile& profile,
    double source_temperature_kelvin,
    double source_tint = 0.0);
[[nodiscard]] std::shared_ptr<const DcpProfileRenderPlan>
compile_dcp_profile_render_plan(
    const DcpProfile& profile,
    std::string_view immutable_profile_identity,
    double source_temperature_kelvin,
    double source_tint = 0.0);
[[nodiscard]] DcpProfileRenderPlanCacheStats
dcp_profile_render_plan_cache_stats();
void clear_dcp_profile_render_plan_cache_for_testing();
[[nodiscard]] DcpRgb dcp_apply_hue_sat_map(
    const DcpProfile& profile,
    double source_temperature_kelvin,
    double source_tint,
    DcpRgb rgb);
[[nodiscard]] DcpRgb dcp_apply_hue_sat_map(
    const DcpProfile& profile,
    const DcpProfileRenderPlan& plan,
    DcpRgb rgb);

// Applies the profile's single LookTable using the same HSV table rules.
[[nodiscard]] DcpRgb dcp_apply_look_table(
    const DcpProfile& profile,
    DcpRgb rgb);

// Applies ProfileToneCurve with the DNG natural cubic spline and RGB-tone
// hue-preserving channel interpolation. Linear endpoint extrapolation keeps
// signed and over-range samples representable.
[[nodiscard]] DcpRgb dcp_apply_profile_tone_curve(
    const DcpProfile& profile,
    DcpRgb rgb);
[[nodiscard]] DcpRgb dcp_apply_profile_tone_curve(
    const DcpProfile& profile,
    const DcpProfileRenderPlan& plan,
    DcpRgb rgb);

// Applies DNG 1.7 ProfileGainTableMap2 in linear RIMM/ProPhoto space. Image
// coordinates are normalized pixel-center coordinates. exposure_weight_gain
// compensates for applying the map before BaselineExposure as specified.
[[nodiscard]] DcpRgb dcp_apply_profile_gain_table_map(
    const DcpProfile& profile,
    double normalized_x,
    double normalized_y,
    double exposure_weight_gain,
    DcpRgb rgb);

// Applies DNG RGBTables after ProfileToneCurve in linear ProPhoto/RIMM RGB.
// Mask names are matched case-sensitively. Missing masks ignore their tables;
// an empty semantic name is the optional background table.
[[nodiscard]] DcpRgb dcp_apply_rgb_tables(
    const DcpProfile& profile,
    DcpRgb rgb,
    std::span<const DcpSemanticMaskWeight> semantic_mask_weights = {});
[[nodiscard]] DcpRgb dcp_apply_rgb_tables(
    const DcpProfile& profile,
    const DcpProfileRenderPlan& plan,
    DcpRgb rgb,
    std::span<const DcpSemanticMaskWeight> semantic_mask_weights = {});

}  // namespace photo_editor::pipeline

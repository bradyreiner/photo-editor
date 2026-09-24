#pragma once

#include "photo_editor/pipeline/photo_effect_types.h"

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

inline constexpr std::uint32_t kEditRecipeV13Version = 13;

enum class RawDecoder : std::uint8_t {
  LibRaw,
  Raster,
};

enum class CameraProfileMode : std::uint8_t {
  Embedded,
  CameraMatrix,
  Dcp,
  Icc,
};

enum class RawDemosaicQualityV13 : std::uint8_t {
  Preview,
  Balanced,
  Full,
};

enum class HighlightReconstructionModeV13 : std::uint8_t {
  Disabled,
  CameraDefault,
  Enabled,
};

enum class ChromaticAdaptationMethodV13 : std::uint8_t {
  Bradford,
  Cat02,
};

enum class CameraMatchingLookV13 : std::uint8_t {
  Standard,
  Neutral,
  Vivid,
  Portrait,
  Landscape,
  Monochrome,
};

enum class CurveInterpolation : std::uint8_t {
  Linear,
  MonotoneCubic,
};

enum class BlendMode : std::uint8_t {
  Normal,
  Luminosity,
  Color,
  Multiply,
  Screen,
  Overlay,
  SoftLight,
};

enum class LayerKind : std::uint8_t {
  Adjustment,
  Pixel,
  Text,
  Fill,
  Group,
};

enum class MaskSourceKind : std::uint8_t {
  LinearGradient,
  RadialGradient,
  BrushRaster,
  Lasso,
  Subject,
  Sky,
  Object,
  LuminanceRange,
  ColorRange,
  DepthRange,
  RetouchPatch,
};

enum class MaskCombineOperation : std::uint8_t {
  Add,
  Subtract,
  Intersect,
};

enum class MaskCoordinateSpace : std::uint8_t {
  SourceNormalized,
  SourcePixels,
};

enum class MaskSamplingMode : std::uint8_t {
  Analytic,
  Nearest,
  Bilinear,
};

enum class UprightMode : std::uint8_t {
  Off,
  Vertical,
  Full,
  Guided,
};

enum class PhotographicRenderer : std::uint8_t {
  Aces2OcioV1,
};

enum class RendererTransitionLookV13 : std::uint8_t {
  None,
  PreserveLegacyDirectLuminanceV1,
};

enum class DisplayRenderingTarget : std::uint8_t {
  ActiveDisplay,
  SrgbSimulation,
  DisplayP3Simulation,
  HdrPq,
  HdrHlg,
};

enum class ProofMode : std::uint8_t {
  Disabled,
  Icc,
};

// The document's delivery space is distinct from the current viewport target.
enum class ExportColorSpaceV13 : std::uint8_t {
  Srgb,
  DisplayP3,
  AdobeRgb,
  ProPhotoRgb,
  Rec709,
  Rec2020,
};

enum class OutputRenderIntent : std::uint8_t {
  Perceptual,
  RelativeColorimetric,
  AbsoluteColorimetric,
  Saturation,
};

enum class OutputGamutMappingPolicy : std::uint8_t {
  Perceptual,
  Clip,
  PreserveSaturation,
};

enum class ColorMixerModelV13 : std::uint8_t {
  Oklab,
  Hsl,
  Hsv,
  Lch,
};

enum class ColorHarmonyModeV13 : std::uint8_t {
  Complementary,
  Analogous,
  Triadic,
  SplitComplementary,
  Tetradic,
};

struct AssetIdentifier final {
  std::string value;

  bool operator==(const AssetIdentifier&) const = default;
};

struct InputRecipeV13 final {
  AssetIdentifier source;
  RawDecoder decoder = RawDecoder::LibRaw;
  std::string decoder_provenance;
  CameraProfileMode camera_profile_mode = CameraProfileMode::Embedded;
  std::optional<AssetIdentifier> camera_profile;
  std::optional<double> white_balance_temperature_kelvin;
  std::optional<double> white_balance_tint;
  std::optional<double> black_level_override;
  std::optional<double> white_level_override;
  bool input_transform_enabled = true;
  HighlightReconstructionModeV13 highlight_reconstruction =
      HighlightReconstructionModeV13::Enabled;
  ChromaticAdaptationMethodV13 chromatic_adaptation =
      ChromaticAdaptationMethodV13::Bradford;
  RawDemosaicQualityV13 demosaic_quality =
      RawDemosaicQualityV13::Balanced;
  double hot_dead_pixel_cleanup = 0.0;
  double false_color_suppression = 0.0;
  bool highlight_recovery_enabled = true;
  CameraMatchingLookV13 camera_matching_look =
      CameraMatchingLookV13::Standard;
  double calibration_hue = 0.0;
  double calibration_saturation = 0.0;
  double red_primary_hue = 0.0;
  double red_primary_saturation = 0.0;
  double green_primary_hue = 0.0;
  double green_primary_saturation = 0.0;
  double blue_primary_hue = 0.0;
  double blue_primary_saturation = 0.0;

  bool operator==(const InputRecipeV13&) const = default;
};

struct SceneRecipeV13 final {
  double exposure_ev = 0.0;

  bool operator==(const SceneRecipeV13&) const = default;
};

struct ToneRecipeV13 final {
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double brightness = 0.0;
  double hdr_local_tone_mapping = 0.0;
  double filmic_shoulder = 0.0;
  double filmic_toe = 0.0;
  double adaptive_contrast = 0.0;
  double levels_black_point = 0.0;
  double levels_gamma = 0.0;
  double levels_white_point = 0.0;
  double zone_deep_shadows = 0.0;
  double zone_shadows = 0.0;
  double zone_midtones = 0.0;
  double zone_lights = 0.0;
  double zone_highlights = 0.0;

  bool operator==(const ToneRecipeV13&) const = default;
};

struct CurvePointV13 final {
  double x = 0.0;
  double y = 0.0;

  bool operator==(const CurvePointV13&) const = default;
};

struct CurvesRecipeV13 final {
  CurveInterpolation interpolation = CurveInterpolation::MonotoneCubic;
  std::vector<CurvePointV13> master;
  std::vector<CurvePointV13> luma;
  std::vector<CurvePointV13> red;
  std::vector<CurvePointV13> green;
  std::vector<CurvePointV13> blue;

  bool operator==(const CurvesRecipeV13&) const = default;
};

struct ColorMixerChannelV13 final {
  double hue = 0.0;
  double saturation = 0.0;
  double luminance = 0.0;
  double selective_cyan = 0.0;
  double selective_magenta = 0.0;
  double selective_yellow = 0.0;
  double selective_black = 0.0;

  bool operator==(const ColorMixerChannelV13&) const = default;
};

struct RgbChannelMixerV13 final {
  double red_from_red = 1.0;
  double red_from_green = 0.0;
  double red_from_blue = 0.0;
  double green_from_red = 0.0;
  double green_from_green = 1.0;
  double green_from_blue = 0.0;
  double blue_from_red = 0.0;
  double blue_from_green = 0.0;
  double blue_from_blue = 1.0;

  bool operator==(const RgbChannelMixerV13&) const = default;
};

struct ColorGradingZoneV13 final {
  double hue = 0.0;
  double saturation = 0.0;
  double luminance = 0.0;

  bool operator==(const ColorGradingZoneV13&) const = default;
};

struct CreativeLutRecipeV13 final {
  AssetIdentifier payload_asset;
  bool enabled = true;
  double strength = 1.0;
  std::string name;
  std::array<double, 3> domain_min = {0.0, 0.0, 0.0};
  std::array<double, 3> domain_max = {1.0, 1.0, 1.0};
  std::string input_color_space = "sRGB";
  std::string output_color_space = "sRGB";

  bool operator==(const CreativeLutRecipeV13&) const = default;
};

struct LookRecipeV13 final {
  PhotographicRenderer renderer = PhotographicRenderer::Aces2OcioV1;
  double temperature = 0.0;
  double tint = 0.0;
  double vibrance = 0.0;
  double saturation = 0.0;
  double hue_shift = 0.0;
  double saturation_shift = 0.0;
  double colorize_hue = 0.0;
  double colorize_strength = 0.0;
  ColorMixerModelV13 mixer_model = ColorMixerModelV13::Oklab;
  std::array<ColorMixerChannelV13, 8> mixer_channels{};
  RgbChannelMixerV13 rgb_channel_mixer;
  bool black_and_white_enabled = false;
  std::array<double, 8> black_and_white_mixer{};
  bool color_replacement_enabled = false;
  double color_replacement_source_hue = 0.0;
  double color_replacement_target_hue = 0.33;
  double color_replacement_range = 0.35;
  double color_replacement_softness = 1.0;
  double color_replacement_strength = 0.0;
  bool color_replacement_preserve_luminance = true;
  bool color_replacement_preserve_saturation = true;
  bool skin_tone_protection_enabled = false;
  double skin_tone_line_hue = 0.08;
  double skin_tone_protection_range = 0.30;
  double skin_tone_protection_strength = 0.0;
  bool color_harmony_enabled = false;
  ColorHarmonyModeV13 color_harmony_mode =
      ColorHarmonyModeV13::Complementary;
  double color_harmony_base_hue = 0.0;
  double color_harmony_strength = 0.0;
  ColorGradingZoneV13 grading_shadows;
  ColorGradingZoneV13 grading_midtones;
  ColorGradingZoneV13 grading_highlights;
  double grading_amount = 1.0;
  double grading_balance = 0.0;
  std::vector<CreativeLutRecipeV13> luts;

  bool operator==(const LookRecipeV13&) const = default;
};

struct MaskGeometryV13 final {
  double feather = 0.0;
  double position = 0.5;
  double center_x = 0.5;
  double center_y = 0.5;
  double radius_x = 0.25;
  double radius_y = 0.25;
  double rotation_degrees = 0.0;
  double range_min = 0.0;
  double range_max = 1.0;

  bool operator==(const MaskGeometryV13&) const = default;
};

struct MaskSourceRecipeV13 final {
  AssetIdentifier id;
  MaskSourceKind kind = MaskSourceKind::LinearGradient;
  MaskCombineOperation operation = MaskCombineOperation::Add;
  std::optional<AssetIdentifier> parent;
  bool enabled = true;
  BlendMode blend_mode = BlendMode::Normal;
  double opacity = 1.0;
  double density = 1.0;
  bool inverted = false;
  std::optional<AssetIdentifier> output;
  bool reusable_output = false;
  MaskCoordinateSpace coordinate_space =
      MaskCoordinateSpace::SourceNormalized;
  MaskSamplingMode sampling = MaskSamplingMode::Analytic;
  std::optional<AssetIdentifier> source_revision;
  std::optional<AssetIdentifier> payload_asset;
  MaskGeometryV13 geometry;

  bool operator==(const MaskSourceRecipeV13&) const = default;
};

struct MaskSetRecipeV13 final {
  AssetIdentifier id;
  bool enabled = true;
  std::vector<MaskSourceRecipeV13> sources;

  bool operator==(const MaskSetRecipeV13&) const = default;
};

struct DetailRecipeV13 final {
  double texture = 0.0;
  double clarity = 0.0;
  double dehaze = 0.0;
  double sharpening = 0.0;
  double sharpen_radius = 1.0;
  double sharpen_detail = 0.0;
  double sharpen_masking = 0.0;
  double luminance_noise_reduction = 0.0;
  double chroma_noise_reduction = 0.0;

  bool operator==(const DetailRecipeV13&) const = default;
};

struct GeometryRecipeV13 final {
  double crop_x = 0.0;
  double crop_y = 0.0;
  double crop_width = 1.0;
  double crop_height = 1.0;
  double rotation_degrees = 0.0;
  double lens_distortion = 0.0;
  double lens_vignette = 0.0;
  double chromatic_aberration = 0.0;
  double perspective_vertical = 0.0;
  double perspective_horizontal = 0.0;
  double scale = 1.0;
  double skew_x = 0.0;
  double skew_y = 0.0;
  double crop_zoom = 1.0;
  UprightMode upright_mode = UprightMode::Off;
  double liquify_center_x = 0.5;
  double liquify_center_y = 0.5;
  double liquify_radius = 0.25;
  double liquify_strength = 0.0;
  bool content_aware_crop_fill = false;

  bool operator==(const GeometryRecipeV13&) const = default;
};

struct LayerRecipeV13 final {
  AssetIdentifier id;
  LayerKind kind = LayerKind::Adjustment;
  bool enabled = true;
  double opacity = 1.0;
  BlendMode blend_mode = BlendMode::Normal;
  std::optional<AssetIdentifier> mask;
  std::optional<AssetIdentifier> parent;
  std::optional<AssetIdentifier> content_asset;
  SceneRecipeV13 scene;
  ToneRecipeV13 tone;
  CurvesRecipeV13 curves;
  LookRecipeV13 look;
  DetailRecipeV13 detail;
  GeometryRecipeV13 geometry;
  std::vector<PhotoEffectInstance> effects;

  bool operator==(const LayerRecipeV13&) const = default;
};

struct OutputRecipeV13 final {
  DisplayRenderingTarget rendering_target = DisplayRenderingTarget::ActiveDisplay;
  RendererTransitionLookV13 renderer_transition_look =
      RendererTransitionLookV13::None;
  std::optional<AssetIdentifier> device_icc_profile;
  double peak_luminance_nits = 100.0;
  double black_luminance_nits = 0.0;
  OutputRenderIntent render_intent = OutputRenderIntent::Perceptual;
  bool gamut_mapping_enabled = true;
  OutputGamutMappingPolicy gamut_mapping_policy =
      OutputGamutMappingPolicy::Perceptual;
  bool black_point_compensation = true;
  double source_black_point = 0.0;
  double output_black_point = 0.0;
  ExportColorSpaceV13 export_color_space = ExportColorSpaceV13::Srgb;

  bool operator==(const OutputRecipeV13&) const = default;
};

struct ProofRecipeV13 final {
  ProofMode mode = ProofMode::Disabled;
  std::optional<AssetIdentifier> profile;
  bool simulate_paper = false;
  bool simulate_ink_black = false;
  bool black_point_compensation = true;

  bool operator==(const ProofRecipeV13&) const = default;
};

struct EditRecipeV13 final {
  std::uint32_t recipe_version = kEditRecipeV13Version;
  AssetIdentifier recipe_id;
  InputRecipeV13 input;
  SceneRecipeV13 scene;
  ToneRecipeV13 tone;
  CurvesRecipeV13 curves;
  LookRecipeV13 look;
  std::vector<MaskSetRecipeV13> masks;
  std::vector<LayerRecipeV13> layers;
  DetailRecipeV13 detail;
  GeometryRecipeV13 geometry;
  OutputRecipeV13 output;
  ProofRecipeV13 proof;

  bool operator==(const EditRecipeV13&) const = default;
};

enum class RecipeErrorCode : std::uint8_t {
  InvalidVersion,
  MissingIdentifier,
  NonFiniteValue,
  ValueOutOfRange,
  InvalidCurve,
  DuplicateIdentifier,
  MissingProfile,
  InvalidTarget,
  MalformedSerialization,
};

struct RecipeError final {
  RecipeErrorCode code = RecipeErrorCode::MalformedSerialization;
  std::string path;
  std::string message;

  bool operator==(const RecipeError&) const = default;
};

struct RecipeValidationResult final {
  std::vector<RecipeError> errors;

  [[nodiscard]] bool ok() const noexcept {
    return errors.empty();
  }
};

[[nodiscard]] RecipeValidationResult validate_edit_recipe_v13(
    const EditRecipeV13& recipe);
[[nodiscard]] EditRecipeV13 normalize_edit_recipe_v13(
    const EditRecipeV13& recipe);
[[nodiscard]] std::string serialize_edit_recipe_v13(
    const EditRecipeV13& recipe);
[[nodiscard]] std::optional<EditRecipeV13> deserialize_edit_recipe_v13(
    std::string_view serialized,
    RecipeError* error = nullptr);
[[nodiscard]] std::string edit_recipe_v13_cache_key(
    const EditRecipeV13& recipe);

}  // namespace photo_editor::pipeline

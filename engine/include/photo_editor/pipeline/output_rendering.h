#pragma once

#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

struct Aces2OfficialOutputTransform;

// ACES 2.0 reference tone-scale parameters. The forward function maps
// scene-linear values (1.0 = normalized scene white) to display luminance in
// cd/m^2; the inverse performs the supported mathematical round trip.
struct Aces2ToneScaleParameters final {
  double peak_luminance_nits = 100.0;
  double normalized_white_nits = 100.0;
  double contrast = 1.15;
  double flare = 0.04;
  double scene_scale = 0.0;
  double output_scale = 0.0;
  double output_power = 0.0;
  double forward_limit = 0.0;
  double inverse_limit = 0.0;
};

// Hellwig 2022 JMh appearance-space parameters used by the ACES 2.0 output
// transform. RGB inputs and outputs are scene-linear values in color_space;
// J is lightness, M is colorfulness, and h is degrees in [0, 360).
struct Aces2HellwigJmhParameters final {
  RgbColorSpace color_space = make_linear_srgb_color_space();
  ColorMatrix3 linear_rgb_to_adapted_cone;
  ColorMatrix3 adapted_cone_to_linear_rgb;
  ColorMatrix3 adapted_cone_to_aab;
  ColorMatrix3 aab_to_adapted_cone;
  double normalized_luminance_adaptation = 0.0;
  double lightness_exponent = 0.0;
  double inverse_lightness_exponent = 0.0;
  double achromatic_white_response = 0.0;
  double inverse_achromatic_white_response = 0.0;
};

struct HellwigJmh final {
  double j = 0.0;
  double m = 0.0;
  double h_degrees = 0.0;
};

inline constexpr std::size_t kAces2HueTableSize = 360U;
inline constexpr std::size_t kAces2PaddedHueTableSize =
    kAces2HueTableSize + 2U;

struct Aces2ChromaCompressionParameters final {
  Aces2ToneScaleParameters tone_scale;
  Aces2HellwigJmhParameters input_appearance;
  Aces2HellwigJmhParameters reach_appearance;
  std::array<double, kAces2PaddedHueTableSize> reach_colorfulness{};
  double maximum_lightness = 0.0;
  double inverse_model_gamma = 0.0;
  double saturation_expansion = 0.0;
  double saturation_threshold = 0.0;
  double highlight_compression = 0.0;
  double hue_normalization_scale = 0.0;
};

// Immutable target-gamut boundary sampled in Hellwig JMh. The table is an
// acceleration structure only: the exact reference renderer remains usable
// without it, and compiled processors verify/fall back to the numerical
// boundary search whenever interpolation would leave the target cube.
struct Aces2GamutBoundaryTable final {
  std::size_t lightness_sample_count = 0U;
  std::size_t hue_sample_count = 0U;
  double maximum_lightness = 0.0;
  std::vector<double> maximum_colorfulness;
};

struct Aces2OutputRenderingParameters final {
  DisplayTarget display_target;
  RendererTransitionLook transition_look =
      RendererTransitionLook::None;
  Aces2ChromaCompressionParameters tone_and_chroma;
  Aces2HellwigJmhParameters output_appearance;
  ColorMatrix3 working_to_aces2065_1;
  ColorMatrix3 aces2065_1_to_acescg;
  ColorMatrix3 acescg_to_aces2065_1;
  // Input conversion for the selected official OCIO processor. Standard
  // working spaces use the named OCIO source transform directly; arbitrary
  // spaces retain the specification-based ACES2065-1 matrix boundary.
  ColorMatrix3 acescg_to_official_input;
  double maximum_physical_rgb = 1.0;
  double physical_to_reference_white = 1.0;
  std::shared_ptr<const Aces2GamutBoundaryTable> gamut_boundary_table;
  // Present when the validated OpenColorIO ACES 2 fixed-function renderer is
  // available. The existing native implementation remains the deterministic
  // fallback for development builds without that dependency.
  std::shared_ptr<const Aces2OfficialOutputTransform> official_transform;
};

struct Aces2OutputProcessor final {
  Aces2OutputRenderingParameters parameters;
  std::string cache_key;
};

struct Aces2OutputProcessorCacheStats final {
  std::uint64_t requests = 0U;
  std::uint64_t hits = 0U;
  std::uint64_t misses = 0U;
  std::uint64_t builds = 0U;
  std::uint64_t evictions = 0U;
  std::size_t entries = 0U;
};

enum class Aces2OutputLutQuality {
  Interactive,
  Final,
};

struct Aces2OutputLutSettings final {
  int cube_size = 97;
  double shaper_offset = 0.01;
};

// GPU-ready approximation of the official output transform. Input RGB is
// converted to ACEScg/AP1, clamped exactly like the ACES reference wrapper,
// logarithmically shaped, and sampled with tetrahedral interpolation. Table
// values remain in physical target-linear RGB until post-sample containment.
struct Aces2OutputLut3D final {
  Aces2OutputLutSettings settings;
  double shaper_maximum = 0.0;
  double maximum_physical_rgb = 1.0;
  double physical_to_reference_white = 1.0;
  ColorMatrix3 working_to_acescg;
  std::vector<std::array<float, 4>> table;
  std::size_t repaired_non_finite_samples = 0U;
  std::string cache_key;

  [[nodiscard]] bool valid() const;
  [[nodiscard]] std::size_t byte_size() const;
};

struct Aces2OutputLutCacheStats final {
  std::uint64_t requests = 0U;
  std::uint64_t hits = 0U;
  std::uint64_t misses = 0U;
  std::uint64_t builds = 0U;
  std::uint64_t evictions = 0U;
  std::size_t entries = 0U;
  std::size_t bytes = 0U;
  std::size_t maximum_bytes = 0U;
  std::string last_failure_reason;
};

enum class Aces2OutputImplementation {
  OfficialWhenAvailable,
  NativeFallback,
};

[[nodiscard]] Aces2ToneScaleParameters make_aces2_tone_scale(
    double peak_luminance_nits);
[[nodiscard]] double aces2_tone_scale_forward(
    double scene_linear,
    const Aces2ToneScaleParameters& parameters);
[[nodiscard]] double aces2_tone_scale_inverse(
    double display_luminance_nits,
    const Aces2ToneScaleParameters& parameters);

[[nodiscard]] Aces2HellwigJmhParameters make_aces2_hellwig_jmh_parameters(
    const RgbColorSpace& color_space);
[[nodiscard]] HellwigJmh aces2_linear_rgb_to_hellwig_jmh(
    const std::array<double, 3>& linear_rgb,
    const Aces2HellwigJmhParameters& parameters);
[[nodiscard]] std::array<double, 3> aces2_hellwig_jmh_to_linear_rgb(
    const HellwigJmh& jmh,
    const Aces2HellwigJmhParameters& parameters);
[[nodiscard]] Aces2ChromaCompressionParameters
make_aces2_chroma_compression_parameters(double peak_luminance_nits);
[[nodiscard]] HellwigJmh aces2_tone_and_chroma_compress(
    const HellwigJmh& jmh,
    const Aces2ChromaCompressionParameters& parameters);
[[nodiscard]] Aces2OutputRenderingParameters make_aces2_output_rendering_parameters(
    const RgbColorSpace& working_color_space,
    const DisplayTarget& display_target,
    std::string_view chromatic_adaptation_method = "Bradford",
    Aces2OutputImplementation implementation =
        Aces2OutputImplementation::OfficialWhenAvailable,
    RendererTransitionLook transition_look =
        RendererTransitionLook::None);
[[nodiscard]] std::shared_ptr<const Aces2OutputProcessor>
compile_aces2_output_processor(
    const RgbColorSpace& working_color_space,
    const DisplayTarget& display_target,
    std::string_view chromatic_adaptation_method = "Bradford",
    Aces2OutputImplementation implementation =
        Aces2OutputImplementation::OfficialWhenAvailable,
    RendererTransitionLook transition_look =
        RendererTransitionLook::None);
[[nodiscard]] Aces2OutputProcessorCacheStats
aces2_output_processor_cache_stats();
void clear_aces2_output_processor_cache_for_testing();
[[nodiscard]] bool aces2_official_output_transform_available();
[[nodiscard]] std::string_view aces2_output_renderer_backend_name();
[[nodiscard]] std::string_view aces2_output_renderer_version();
[[nodiscard]] std::string_view aces2_output_processor_backend_name(
    const Aces2OutputProcessor& processor);
[[nodiscard]] Aces2OutputLutSettings aces2_output_lut_settings(
    Aces2OutputLutQuality quality);
[[nodiscard]] std::shared_ptr<const Aces2OutputLut3D>
compile_aces2_output_lut(
    const RgbColorSpace& working_color_space,
    const DisplayTarget& display_target,
    Aces2OutputLutSettings settings,
    std::string_view chromatic_adaptation_method = "Bradford",
    RendererTransitionLook transition_look =
        RendererTransitionLook::None);
[[nodiscard]] std::array<double, 3> aces2_output_lut_apply(
    const Aces2OutputLut3D& lut,
    const std::array<double, 3>& working_linear_rgb);
[[nodiscard]] Aces2OutputLutCacheStats aces2_output_lut_cache_stats();
void clear_aces2_output_lut_cache_for_testing();
[[nodiscard]] std::string_view aces2_output_processor_version(
    const Aces2OutputProcessor& processor);
[[nodiscard]] std::array<double, 3> aces2_render_to_linear_display_rgb(
    const std::array<double, 3>& working_linear_rgb,
    const Aces2OutputRenderingParameters& parameters);

// Display encoding helpers use normalized linear display light. PQ is
// absolute (1.0 = 10,000 cd/m^2); HLG is relative to the target nominal peak.
[[nodiscard]] double encode_display_eotf(
    double linear_display_light,
    const DisplayTarget& target);
[[nodiscard]] double decode_display_eotf(
    double encoded_signal,
    const DisplayTarget& target);

}  // namespace photo_editor::pipeline

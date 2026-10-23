#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_metrics.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/output_rendering.h"

namespace {

[[nodiscard]] photo_editor::pipeline::CieLab linear_srgb_to_lab(
    const std::array<double, 3>& rgb) {
  return photo_editor::pipeline::cie_xyz_to_lab(
      {
          .x = 0.4124564 * rgb[0] + 0.3575761 * rgb[1] +
               0.1804375 * rgb[2],
          .y = 0.2126729 * rgb[0] + 0.7151522 * rgb[1] +
               0.0721750 * rgb[2],
          .z = 0.0193339 * rgb[0] + 0.1191920 * rgb[1] +
               0.9503041 * rgb[2],
      },
      photo_editor::pipeline::cie_d65_white());
}

}  // namespace

int main() {
  const auto default_config = photo_editor::pipeline::default_color_pipeline_config();
  if (default_config.input_profile.profile_name != "Embedded") {
    std::cerr << "default input profile should be Embedded\n";
    return EXIT_FAILURE;
  }
  if (default_config.working_profile.profile_name != "Linear ProPhoto RGB") {
    std::cerr << "default working profile should be Linear ProPhoto RGB\n";
    return EXIT_FAILURE;
  }
  if (default_config.preview_output_profile.profile_name != "sRGB" ||
      default_config.export_output_profile.profile_name != "sRGB") {
    std::cerr << "default output profiles should be sRGB\n";
    return EXIT_FAILURE;
  }
  if (std::abs(default_config.output_headroom - 1.0) > 1.0e-9) {
    std::cerr << "default output headroom should target SDR\n";
    return EXIT_FAILURE;
  }
  if (!photo_editor::pipeline::display_target_valid(
          default_config.preview_display_target) ||
      !photo_editor::pipeline::display_target_valid(
          default_config.export_display_target) ||
      default_config.preview_display_target.eotf !=
          photo_editor::pipeline::DisplayEotf::Srgb ||
      default_config.photographic_rendering_model !=
          photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1 ||
      std::abs(photo_editor::pipeline::display_target_linear_headroom(
                   default_config.preview_display_target) -
               1.0) > 1.0e-9) {
    std::cerr << "default output targets should explicitly select official ACES 2 for the 100-nit SDR view\n";
    return EXIT_FAILURE;
  }
  constexpr auto kDeclaredBuiltinColorSpaces =
      std::array<std::string_view, 13>{
          "sRGB",
          "Linear sRGB",
          "Display P3",
          "Linear Display P3",
          "Adobe RGB (1998)",
          "ProPhoto RGB",
          "Linear ProPhoto RGB",
          "Rec.709",
          "Rec.2020",
          "Linear Rec.2020",
          "ACES2065-1",
          "ACEScg",
          "ACEScct",
      };
  if (!std::all_of(
          kDeclaredBuiltinColorSpaces.begin(),
          kDeclaredBuiltinColorSpaces.end(),
          [](const auto name) {
            const auto resolved =
                photo_editor::pipeline::resolve_builtin_rgb_color_space(name);
            return resolved.has_value() &&
                   resolved->primaries !=
                       photo_editor::pipeline::ColorPrimaries::Unknown &&
                   resolved->transfer_function !=
                       photo_editor::pipeline::TransferFunction::Unknown &&
                   resolved->white_point !=
                       photo_editor::pipeline::WhitePoint::Unknown;
          }) ||
      !photo_editor::pipeline::resolve_builtin_rgb_color_space(
           "  ACES AP1  ")
           .has_value() ||
      photo_editor::pipeline::resolve_builtin_rgb_color_space(
          "Unknown Camera Log")
          .has_value()) {
    std::cerr << "declared built-in color-space resolution should be complete, alias-stable, and fail closed\n";
    return EXIT_FAILURE;
  }
  const auto hdr_display_target = photo_editor::pipeline::DisplayTarget{
      .target_name = "Rec.2020 PQ 1000 nits",
      .color_space = photo_editor::pipeline::make_rec2020_color_space(),
      .eotf = photo_editor::pipeline::DisplayEotf::Pq,
      .peak_luminance_nits = 1000.0,
      .black_luminance_nits = 0.0005,
      .reference_white_nits = 203.0,
      .surround_luminance_nits = 5.0,
      .surround = photo_editor::pipeline::DisplaySurround::Dim,
      .mastering_mode =
          photo_editor::pipeline::DisplayMasteringMode::HdrDisplayReferred,
  };
  auto mismatched_hdr_display_target = hdr_display_target;
  mismatched_hdr_display_target.mastering_mode =
      photo_editor::pipeline::DisplayMasteringMode::Sdr;
  if (!photo_editor::pipeline::display_target_valid(hdr_display_target) ||
      photo_editor::pipeline::display_target_valid(mismatched_hdr_display_target) ||
      std::abs(photo_editor::pipeline::display_target_linear_headroom(
                   hdr_display_target) -
               (1000.0 / 203.0)) > 1.0e-9 ||
      std::string(photo_editor::pipeline::display_eotf_name(
          photo_editor::pipeline::display_eotf_from_name("ST2084"))) !=
          "pq-st2084" ||
      std::string(photo_editor::pipeline::display_mastering_mode_name(
          photo_editor::pipeline::display_mastering_mode_from_name(
              "HDR scene referred"))) != "hdr-scene-referred" ||
      std::string(photo_editor::pipeline::photographic_rendering_model_name(
          photo_editor::pipeline::photographic_rendering_model_from_name(
              "ACES 2"))) != "aces2-ocio-v1" ||
      photo_editor::pipeline::photographic_rendering_model_from_name(
          "unknown-renderer") !=
          photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1 ||
      photo_editor::pipeline::photographic_rendering_model_from_name(
          "legacy-oklch-v1") !=
          photo_editor::pipeline::PhotographicRenderingModel::LegacyOklchV1) {
    std::cerr << "explicit SDR/HDR display target metadata should validate and normalize deterministically\n";
    return EXIT_FAILURE;
  }
  const auto sdr_tone_scale =
      photo_editor::pipeline::make_aces2_tone_scale(100.0);
  const auto hdr_tone_scale =
      photo_editor::pipeline::make_aces2_tone_scale(1000.0);
  auto previous_sdr_luminance = -1.0;
  for (const auto scene_value :
       {0.0, 0.001, 0.01, 0.18, 1.0, 8.0, 64.0, 128.0}) {
    const auto display_luminance =
        photo_editor::pipeline::aces2_tone_scale_forward(
            scene_value,
            sdr_tone_scale);
    if (!std::isfinite(display_luminance) ||
        display_luminance + 1.0e-12 < previous_sdr_luminance ||
        display_luminance < 0.0 || display_luminance > 100.0 + 1.0e-9) {
      std::cerr << "ACES 2 tone scale should be finite, bounded, and monotonic\n";
      return EXIT_FAILURE;
    }
    if (scene_value > 0.0 && scene_value < sdr_tone_scale.forward_limit) {
      const auto round_trip =
          photo_editor::pipeline::aces2_tone_scale_inverse(
              display_luminance,
              sdr_tone_scale);
      if (std::abs(round_trip - scene_value) >
          std::max(1.0e-8, scene_value * 2.0e-7)) {
        std::cerr << "ACES 2 tone scale inverse should round-trip its supported domain\n";
        return EXIT_FAILURE;
      }
    }
    previous_sdr_luminance = display_luminance;
  }
  if (std::abs(photo_editor::pipeline::aces2_tone_scale_forward(
                   0.18,
                   sdr_tone_scale) -
               10.0) > 1.0e-3 ||
      photo_editor::pipeline::aces2_tone_scale_forward(8.0, hdr_tone_scale) <=
          photo_editor::pipeline::aces2_tone_scale_forward(8.0, sdr_tone_scale)) {
    std::cerr << "ACES 2 tone scale should preserve its 18-percent anchor and respond to target peak\n";
    return EXIT_FAILURE;
  }
  const auto srgb_jmh_parameters =
      photo_editor::pipeline::make_aces2_hellwig_jmh_parameters(
          photo_editor::pipeline::make_linear_srgb_color_space());
  auto previous_neutral_j = -1.0;
  for (const auto neutral : {0.001, 0.18, 1.0, 8.0}) {
    const std::array<double, 3> rgb = {neutral, neutral, neutral};
    const auto jmh = photo_editor::pipeline::aces2_linear_rgb_to_hellwig_jmh(
        rgb,
        srgb_jmh_parameters);
    const auto round_trip =
        photo_editor::pipeline::aces2_hellwig_jmh_to_linear_rgb(
            jmh,
            srgb_jmh_parameters);
    if (!std::isfinite(jmh.j) || !std::isfinite(jmh.m) ||
        jmh.j <= previous_neutral_j || jmh.m > 1.0e-9) {
      std::cerr << "ACES 2 Hellwig JMh should keep neutral lightness monotonic and achromatic\n";
      return EXIT_FAILURE;
    }
    for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
      if (std::abs(round_trip[channel] - rgb[channel]) >
          std::max(1.0e-9, rgb[channel] * 1.0e-8)) {
        std::cerr << "ACES 2 Hellwig JMh should round-trip neutral scene-linear RGB\n";
        return EXIT_FAILURE;
      }
    }
    previous_neutral_j = jmh.j;
  }
  const auto reference_white_jmh =
      photo_editor::pipeline::aces2_linear_rgb_to_hellwig_jmh(
          {1.0, 1.0, 1.0},
          srgb_jmh_parameters);
  if (std::abs(reference_white_jmh.j - 100.0) > 1.0e-9 ||
      reference_white_jmh.m > 1.0e-9) {
    std::cerr << "ACES 2 Hellwig JMh should anchor reference white at J=100\n";
    return EXIT_FAILURE;
  }
  for (const auto& rgb : {
           std::array<double, 3>{1.0, 0.0, 0.0},
           std::array<double, 3>{0.0, 1.0, 0.0},
           std::array<double, 3>{0.0, 0.0, 1.0},
           std::array<double, 3>{0.62, 0.18, 0.04},
           std::array<double, 3>{0.03, 0.41, 0.77},
       }) {
    const auto jmh = photo_editor::pipeline::aces2_linear_rgb_to_hellwig_jmh(
        rgb,
        srgb_jmh_parameters);
    const auto round_trip =
        photo_editor::pipeline::aces2_hellwig_jmh_to_linear_rgb(
            jmh,
            srgb_jmh_parameters);
    if (!std::isfinite(jmh.j) || !std::isfinite(jmh.m) ||
        !std::isfinite(jmh.h_degrees) || jmh.j <= 0.0 || jmh.m <= 0.0 ||
        jmh.h_degrees < 0.0 || jmh.h_degrees >= 360.0) {
      std::cerr << "ACES 2 Hellwig JMh should produce finite cylindrical appearance coordinates\n";
      return EXIT_FAILURE;
    }
    for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
      if (std::abs(round_trip[channel] - rgb[channel]) > 2.0e-8) {
        std::cerr << "ACES 2 Hellwig JMh should round-trip in-gamut chromatic RGB\n";
        return EXIT_FAILURE;
      }
    }
  }
  const auto aces_ap0_matrix = photo_editor::pipeline::rgb_to_xyz_matrix(
      photo_editor::pipeline::make_aces2065_1_color_space());
  const std::array<double, 9> expected_aces_ap0_matrix = {
      0.952552396, 0.0, 0.000093679,
      0.343966450, 0.728166097, -0.072132546,
      0.0, 0.0, 1.008825184,
  };
  for (std::size_t index = 0; index < expected_aces_ap0_matrix.size(); ++index) {
    if (std::abs(aces_ap0_matrix.values[index] -
                 expected_aces_ap0_matrix[index]) > 2.0e-6) {
      std::cerr << "ACES2065-1 should use the standardized AP0/D60 RGB-to-XYZ matrix\n";
      return EXIT_FAILURE;
    }
  }
  const auto acescct_space =
      photo_editor::pipeline::make_acescct_color_space();
  if (acescct_space.name != "ACEScct" ||
      acescct_space.primaries != photo_editor::pipeline::ColorPrimaries::AcesAp1 ||
      acescct_space.transfer_function !=
          photo_editor::pipeline::TransferFunction::Acescct ||
      acescct_space.white_point !=
          photo_editor::pipeline::WhitePoint::AcesD60 ||
      !acescct_space.scene_referred || !acescct_space.wide_gamut) {
    std::cerr << "ACEScct identity should declare AP1/D60 and its nonlinear grading encoding\n";
    return EXIT_FAILURE;
  }
  const std::array<std::pair<double, double>, 7> acescct_vectors = {{
      {-0.01, -0.03249684322070952},
      {0.0, 0.0729055341958355},
      {0.0078125, 0.1552511415525113},
      {0.18, 0.4135884024924423},
      {1.0, 0.5547945205479452},
      {222.0, 0.9996812709103942},
      {65504.0, 1.4679963120447153},
  }};
  for (const auto& [linear_ap1, expected_acescct] : acescct_vectors) {
    const auto encoded =
        photo_editor::pipeline::linear_ap1_to_acescct(linear_ap1);
    const auto decoded =
        photo_editor::pipeline::acescct_to_linear_ap1(encoded);
    if (std::abs(encoded - expected_acescct) > 2.0e-13 ||
        std::abs(decoded - linear_ap1) >
            2.0e-10 * std::max(1.0, std::abs(linear_ap1))) {
      std::cerr << "ACEScct should match the Academy encoding vectors and round-trip extended AP1 values\n";
      return EXIT_FAILURE;
    }
  }
  if (photo_editor::pipeline::acescct_to_linear_ap1(2.0) != 65504.0 ||
      photo_editor::pipeline::linear_ap1_to_acescct(-0.01) >= 0.0 ||
      photo_editor::pipeline::linear_ap1_to_acescct(222.0) <= 0.99 ||
      !std::isnan(photo_editor::pipeline::acescct_to_linear_ap1(
          std::numeric_limits<double>::quiet_NaN()))) {
    std::cerr << "ACEScct should preserve its specified negative and over-range grading domain and decode ceiling\n";
    return EXIT_FAILURE;
  }
  const auto prophoto_to_ap0 =
      photo_editor::pipeline::compile_builtin_color_transform(
          photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
          photo_editor::pipeline::make_aces2065_1_color_space());
  const auto adapted_white = photo_editor::pipeline::multiply_matrix3_vector(
      prophoto_to_ap0.matrix,
      {1.0, 1.0, 1.0});
  for (const auto channel : adapted_white) {
    if (std::abs(channel - 1.0) > 2.0e-6) {
      std::cerr << "working-space to ACES2065-1 conversion should adapt neutral white\n";
      return EXIT_FAILURE;
    }
  }
  const auto aces_chroma =
      photo_editor::pipeline::make_aces2_chroma_compression_parameters(100.0);
  for (std::size_t index = 1;
       index <= photo_editor::pipeline::kAces2HueTableSize;
       ++index) {
    if (!std::isfinite(aces_chroma.reach_colorfulness[index]) ||
        aces_chroma.reach_colorfulness[index] <= 0.0) {
      std::cerr << "ACES 2 reach-gamut colorfulness table should cover every hue\n";
      return EXIT_FAILURE;
    }
  }
  if (std::abs(
          aces_chroma.reach_colorfulness.front() -
          aces_chroma.reach_colorfulness[
              photo_editor::pipeline::kAces2HueTableSize]) > 1.0e-12 ||
      std::abs(
          aces_chroma.reach_colorfulness.back() -
          aces_chroma.reach_colorfulness[1]) > 1.0e-12) {
    std::cerr << "ACES 2 reach-gamut table should wrap continuously at zero degrees\n";
    return EXIT_FAILURE;
  }
  const auto ap0_parameters =
      photo_editor::pipeline::make_aces2_hellwig_jmh_parameters(
          photo_editor::pipeline::make_aces2065_1_color_space());
  const auto neutral_scene_jmh =
      photo_editor::pipeline::aces2_linear_rgb_to_hellwig_jmh(
          {0.18, 0.18, 0.18},
          ap0_parameters);
  const auto neutral_rendered_jmh =
      photo_editor::pipeline::aces2_tone_and_chroma_compress(
          neutral_scene_jmh,
          aces_chroma);
  const auto saturated_scene_jmh =
      photo_editor::pipeline::aces2_linear_rgb_to_hellwig_jmh(
          {8.0, 1.5, 0.2},
          ap0_parameters);
  const auto saturated_rendered_jmh =
      photo_editor::pipeline::aces2_tone_and_chroma_compress(
          saturated_scene_jmh,
          aces_chroma);
  if (!std::isfinite(neutral_rendered_jmh.j) ||
      neutral_rendered_jmh.j <= 0.0 ||
      neutral_rendered_jmh.j >= aces_chroma.maximum_lightness ||
      neutral_rendered_jmh.m > 1.0e-9 ||
      !std::isfinite(saturated_rendered_jmh.j) ||
      !std::isfinite(saturated_rendered_jmh.m) ||
      saturated_rendered_jmh.m < 0.0 ||
      std::abs(saturated_rendered_jmh.h_degrees -
               saturated_scene_jmh.h_degrees) > 1.0e-10) {
    std::cerr << "ACES 2 tone/chroma compression should remain finite, neutral-safe, bounded, and hue-preserving\n";
    return EXIT_FAILURE;
  }
  const auto sdr_output_rendering =
      photo_editor::pipeline::make_aces2_output_rendering_parameters(
          photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
          default_config.preview_display_target);
  photo_editor::pipeline::clear_aces2_output_processor_cache_for_testing();
  const auto compiled_sdr_output =
      photo_editor::pipeline::compile_aces2_output_processor(
          photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
          default_config.preview_display_target);
  const auto reused_sdr_output =
      photo_editor::pipeline::compile_aces2_output_processor(
          photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
          default_config.preview_display_target);
  const auto processor_cache_stats =
      photo_editor::pipeline::aces2_output_processor_cache_stats();
  const auto official_aces2_available =
      photo_editor::pipeline::aces2_official_output_transform_available();
  const auto has_expected_renderer =
      official_aces2_available
          ? (compiled_sdr_output != nullptr &&
             compiled_sdr_output->parameters.official_transform != nullptr &&
             compiled_sdr_output->parameters.gamut_boundary_table == nullptr)
          : (compiled_sdr_output != nullptr &&
             compiled_sdr_output->parameters.official_transform == nullptr &&
             compiled_sdr_output->parameters.gamut_boundary_table != nullptr &&
             !compiled_sdr_output->parameters.gamut_boundary_table
                  ->maximum_colorfulness.empty());
  if (compiled_sdr_output == nullptr ||
      reused_sdr_output.get() != compiled_sdr_output.get() ||
      compiled_sdr_output->cache_key.find("aces2-output-processor:v4") != 0U ||
      !has_expected_renderer ||
      processor_cache_stats.requests != 2U ||
      processor_cache_stats.hits != 1U ||
      processor_cache_stats.misses != 1U ||
      processor_cache_stats.builds != 1U ||
      processor_cache_stats.entries != 1U) {
    std::cerr << "ACES 2 output processors should compile once, reuse the official renderer when available, and retain the bounded native fallback\n";
    return EXIT_FAILURE;
  }
  const auto native_fallback_processor =
      photo_editor::pipeline::compile_aces2_output_processor(
          photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
          default_config.preview_display_target,
          "Bradford",
          photo_editor::pipeline::Aces2OutputImplementation::NativeFallback);
  if (native_fallback_processor == nullptr) {
    std::cerr << "native ACES 2 fallback processor should compile for the "
                 "gamut-boundary continuity regression\n";
    return EXIT_FAILURE;
  }
  // Near-identical working pixels from the custom-input ICC render fixture
  // previously straddled the output gamut gate and produced a visible jump.
  const auto near_boundary_custom_input =
      photo_editor::pipeline::aces2_render_to_linear_display_rgb(
          {0.131950, 0.318059, 0.087062},
          native_fallback_processor->parameters);
  const auto near_boundary_embedded_input =
      photo_editor::pipeline::aces2_render_to_linear_display_rgb(
          {0.131836, 0.318115, 0.0870361},
          native_fallback_processor->parameters);
  const auto near_boundary_output_delta = std::max({
      std::abs(near_boundary_custom_input[0] - near_boundary_embedded_input[0]),
      std::abs(near_boundary_custom_input[1] - near_boundary_embedded_input[1]),
      std::abs(near_boundary_custom_input[2] - near_boundary_embedded_input[2]),
  });
  if (near_boundary_output_delta > 0.002) {
    std::cerr << "native ACES 2 gamut compression should remain continuous "
                 "for near-identical working pixels near the target gamut "
                 "boundary: delta="
              << near_boundary_output_delta << '\n';
    return EXIT_FAILURE;
  }
  const auto transition_output =
      photo_editor::pipeline::make_aces2_output_rendering_parameters(
          photo_editor::pipeline::make_linear_srgb_color_space(),
          default_config.preview_display_target,
          "Bradford",
          photo_editor::pipeline::Aces2OutputImplementation::NativeFallback,
          photo_editor::pipeline::RendererTransitionLook::
              PreserveLegacyDirectLuminanceV1);
  auto previous_transition_output = -1.0;
  for (const auto neutral : {0.001, 0.004, 0.01, 0.04, 0.18, 0.5, 1.0}) {
    const auto rendered =
        photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            {neutral, neutral, neutral}, transition_output);
    if (!std::isfinite(rendered[0]) ||
        std::abs(rendered[0] - neutral) > 2.0e-5 ||
        std::abs(rendered[1] - neutral) > 2.0e-5 ||
        std::abs(rendered[2] - neutral) > 2.0e-5 ||
        rendered[0] <= previous_transition_output) {
      std::cerr << "the explicit migration transition look should preserve monotone legacy direct-linear neutral luminance without changing Exposure semantics\n";
      return EXIT_FAILURE;
    }
    previous_transition_output = rendered[0];
  }
  const auto transition_processor =
      photo_editor::pipeline::compile_aces2_output_processor(
          photo_editor::pipeline::make_linear_srgb_color_space(),
          default_config.preview_display_target,
          "Bradford",
          photo_editor::pipeline::Aces2OutputImplementation::NativeFallback,
          photo_editor::pipeline::RendererTransitionLook::
              PreserveLegacyDirectLuminanceV1);
  const auto ordinary_native_processor =
      photo_editor::pipeline::compile_aces2_output_processor(
          photo_editor::pipeline::make_linear_srgb_color_space(),
          default_config.preview_display_target,
          "Bradford",
          photo_editor::pipeline::Aces2OutputImplementation::NativeFallback);
  if (transition_processor == nullptr || ordinary_native_processor == nullptr ||
      transition_processor->cache_key == ordinary_native_processor->cache_key) {
    std::cerr << "renderer-transition intent must participate in output processor identity\n";
    return EXIT_FAILURE;
  }
  std::vector<double> compiled_processor_delta_e;
  const std::array<double, 9> compiled_processor_samples = {
      -0.25, 0.0, 0.01, 0.18, 0.5, 1.0, 4.0, 16.0, 128.0};
  compiled_processor_delta_e.reserve(
      compiled_processor_samples.size() *
      compiled_processor_samples.size() *
      compiled_processor_samples.size());
  for (const auto red : compiled_processor_samples) {
    for (const auto green : compiled_processor_samples) {
      for (const auto blue : compiled_processor_samples) {
        const std::array<double, 3> input = {red, green, blue};
        const auto reference =
            photo_editor::pipeline::aces2_render_to_linear_display_rgb(
                input,
                sdr_output_rendering);
        const auto compiled =
            photo_editor::pipeline::aces2_render_to_linear_display_rgb(
                input,
                compiled_sdr_output->parameters);
        compiled_processor_delta_e.push_back(
            photo_editor::pipeline::delta_e_2000(
                linear_srgb_to_lab(reference),
                linear_srgb_to_lab(compiled)));
      }
    }
  }
  std::sort(
      compiled_processor_delta_e.begin(),
      compiled_processor_delta_e.end());
  const auto compiled_p99_index = std::min(
      compiled_processor_delta_e.size() - 1U,
      static_cast<std::size_t>(std::ceil(
          0.99 * static_cast<double>(compiled_processor_delta_e.size()))) -
          1U);
  if (compiled_processor_delta_e[compiled_p99_index] > 0.25 ||
      compiled_processor_delta_e.back() > 0.75) {
    std::cerr << "compiled ACES 2 processor should match the exact CPU reference within the display-transform parity gate: p99="
              << compiled_processor_delta_e[compiled_p99_index]
              << ", max=" << compiled_processor_delta_e.back() << '\n';
    return EXIT_FAILURE;
  }
  if (official_aces2_available) {
    const std::array<std::array<double, 3>, 8> concurrent_inputs = {{
        {-0.25, 0.0, 0.5},
        {0.0, 0.0, 0.0},
        {0.18, 0.18, 0.18},
        {1.0, 0.25, 0.05},
        {4.0, 1.0, 0.25},
        {16.0, 0.5, 8.0},
        {128.0, 16.0, 1.0},
        {0.05, 64.0, 2.0},
    }};
    std::array<std::array<double, 3>, concurrent_inputs.size()>
        concurrent_references{};
    for (std::size_t index = 0; index < concurrent_inputs.size(); ++index) {
      concurrent_references[index] =
          photo_editor::pipeline::aces2_render_to_linear_display_rgb(
              concurrent_inputs[index],
              compiled_sdr_output->parameters);
    }
    std::atomic<bool> concurrent_processor_matches{true};
    std::vector<std::thread> processor_workers;
    processor_workers.reserve(8U);
    for (int worker = 0; worker < 8; ++worker) {
      processor_workers.emplace_back([&]() {
        for (int repetition = 0; repetition < 128; ++repetition) {
          for (std::size_t index = 0; index < concurrent_inputs.size();
               ++index) {
            const auto output =
                photo_editor::pipeline::aces2_render_to_linear_display_rgb(
                    concurrent_inputs[index],
                    compiled_sdr_output->parameters);
            for (std::size_t channel = 0; channel < output.size(); ++channel) {
              if (std::abs(
                      output[channel] -
                      concurrent_references[index][channel]) > 1.0e-7) {
                concurrent_processor_matches.store(
                    false,
                    std::memory_order_relaxed);
              }
            }
          }
        }
      });
    }
    for (auto& worker : processor_workers) {
      worker.join();
    }
    if (!concurrent_processor_matches.load(std::memory_order_relaxed)) {
      std::cerr << "the shared immutable OpenColorIO ACES 2 processor must produce deterministic results under concurrent render workers\n";
      return EXIT_FAILURE;
    }
  }
  photo_editor::pipeline::clear_aces2_output_processor_cache_for_testing();
  std::array<
      std::shared_ptr<const photo_editor::pipeline::Aces2OutputProcessor>,
      8>
      concurrent_compiled_processors;
  std::vector<std::thread> processor_compile_workers;
  processor_compile_workers.reserve(concurrent_compiled_processors.size());
  for (std::size_t worker = 0;
       worker < concurrent_compiled_processors.size();
       ++worker) {
    processor_compile_workers.emplace_back([&, worker]() {
      concurrent_compiled_processors[worker] =
          photo_editor::pipeline::compile_aces2_output_processor(
              photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
              default_config.preview_display_target);
    });
  }
  for (auto& worker : processor_compile_workers) {
    worker.join();
  }
  const auto concurrent_processor_cache_stats =
      photo_editor::pipeline::aces2_output_processor_cache_stats();
  if (concurrent_compiled_processors.front() == nullptr ||
      std::any_of(
          concurrent_compiled_processors.cbegin(),
          concurrent_compiled_processors.cend(),
          [&](const auto& processor) {
            return processor.get() !=
                   concurrent_compiled_processors.front().get();
          }) ||
      concurrent_processor_cache_stats.requests !=
          concurrent_compiled_processors.size() ||
      concurrent_processor_cache_stats.hits !=
          concurrent_compiled_processors.size() - 1U ||
      concurrent_processor_cache_stats.misses != 1U ||
      concurrent_processor_cache_stats.builds != 1U ||
      concurrent_processor_cache_stats.entries != 1U) {
    std::cerr << "concurrent ACES 2 processor requests must converge on one cold-cache build\n";
    return EXIT_FAILURE;
  }
  photo_editor::pipeline::clear_aces2_output_processor_cache_for_testing();
  const auto native_legacy_processor =
      photo_editor::pipeline::compile_aces2_output_processor(
          photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
          default_config.preview_display_target,
          "Bradford",
          photo_editor::pipeline::Aces2OutputImplementation::NativeFallback);
  if (native_legacy_processor == nullptr ||
      native_legacy_processor->parameters.official_transform != nullptr ||
      native_legacy_processor->parameters.gamut_boundary_table == nullptr ||
      native_legacy_processor->cache_key.find("native ACES 2 fallback") ==
          std::string::npos) {
    std::cerr << "the versioned native ACES 2 recipe path must remain isolated from the official renderer\n";
    return EXIT_FAILURE;
  }
  photo_editor::pipeline::clear_aces2_output_processor_cache_for_testing();
  for (int target_index = 0; target_index < 9; ++target_index) {
    auto cache_target = default_config.preview_display_target;
    cache_target.target_name =
        "cache-capacity-target-" + std::to_string(target_index);
    cache_target.black_luminance_nits =
        0.001 + static_cast<double>(target_index) * 0.0001;
    const auto cached =
        photo_editor::pipeline::compile_aces2_output_processor(
            photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
            cache_target);
    if (cached == nullptr) {
      std::cerr << "ACES 2 output processor cache should compile every valid target identity\n";
      return EXIT_FAILURE;
    }
  }
  const auto bounded_cache_stats =
      photo_editor::pipeline::aces2_output_processor_cache_stats();
  if (bounded_cache_stats.builds != 9U ||
      bounded_cache_stats.entries != 8U ||
      bounded_cache_stats.evictions != 1U) {
    std::cerr << "ACES 2 output processor cache should remain bounded with deterministic LRU eviction\n";
    return EXIT_FAILURE;
  }
  auto previous_neutral_output = -1.0;
  for (const auto neutral : {0.0, 0.001, 0.18, 1.0, 8.0, 128.0}) {
    const auto rendered =
        photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            {neutral, neutral, neutral},
            sdr_output_rendering);
    if (!std::isfinite(rendered[0]) ||
        rendered[0] + 1.0e-10 < previous_neutral_output ||
        rendered[0] < -1.0e-10 || rendered[0] > 1.0 + 1.0e-9 ||
        std::abs(rendered[0] - rendered[1]) > 2.0e-6 ||
        std::abs(rendered[1] - rendered[2]) > 2.0e-6) {
      std::cerr << "ACES 2 output rendering should keep SDR neutral ramps monotonic, bounded, and neutral\n";
      return EXIT_FAILURE;
    }
    previous_neutral_output = rendered[0];
  }
  for (const auto& rgb : {
           std::array<double, 3>{4.0, 0.01, 0.01},
           std::array<double, 3>{0.01, 4.0, 0.01},
           std::array<double, 3>{0.01, 0.01, 4.0},
           std::array<double, 3>{16.0, 2.0, 0.1},
           std::array<double, 3>{-0.25, 1.5, 3.0},
       }) {
    const auto reference_rendered =
        photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            rgb,
            sdr_output_rendering);
    const auto compiled_rendered =
        photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            rgb,
            compiled_sdr_output->parameters);
    for (std::size_t channel_index = 0U;
         channel_index < reference_rendered.size();
         ++channel_index) {
      const auto channel = compiled_rendered[channel_index];
      if (!std::isfinite(channel) || channel < -1.0e-10 ||
          channel > 1.0 + 1.0e-9 ||
          std::abs(channel - reference_rendered[channel_index]) > 1.0e-2) {
        std::cerr << "ACES 2 target-gamut rendering should contain extreme colors in the SDR cube\n";
        return EXIT_FAILURE;
      }
    }
  }
  const auto hdr_output_rendering =
      photo_editor::pipeline::make_aces2_output_rendering_parameters(
          photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
          hdr_display_target);
  const auto sdr_highlight =
      photo_editor::pipeline::aces2_render_to_linear_display_rgb(
          {8.0, 8.0, 8.0},
          sdr_output_rendering);
  const auto hdr_highlight =
      photo_editor::pipeline::aces2_render_to_linear_display_rgb(
          {8.0, 8.0, 8.0},
          hdr_output_rendering);
  if (hdr_highlight[0] <= sdr_highlight[0] ||
      hdr_highlight[0] >
          photo_editor::pipeline::display_target_linear_headroom(
              hdr_display_target) +
              1.0e-9) {
    std::cerr << "ACES 2 HDR output rendering should use target peak headroom without exceeding it\n";
    return EXIT_FAILURE;
  }
  const auto hdr_10000_display_target = photo_editor::pipeline::DisplayTarget{
      .target_name = "Rec.2020 PQ 10000 nits",
      .color_space = photo_editor::pipeline::make_rec2020_color_space(),
      .eotf = photo_editor::pipeline::DisplayEotf::Pq,
      .peak_luminance_nits = 10000.0,
      .black_luminance_nits = 0.0005,
      .reference_white_nits = 100.0,
      .surround_luminance_nits = 5.0,
      .surround = photo_editor::pipeline::DisplaySurround::Dim,
      .mastering_mode =
          photo_editor::pipeline::DisplayMasteringMode::HdrDisplayReferred,
  };
  const auto hdr_10000_output_rendering =
      photo_editor::pipeline::make_aces2_output_rendering_parameters(
          photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
          hdr_10000_display_target);
  const auto hdr_10000_highlight =
      photo_editor::pipeline::aces2_render_to_linear_display_rgb(
          {1024.0, 1024.0, 1024.0},
          hdr_10000_output_rendering);
  if (!std::isfinite(hdr_10000_highlight[0]) ||
      hdr_10000_highlight[0] <= 16.0 ||
      hdr_10000_highlight[0] > 100.0 + 1.0e-9 ||
      std::abs(hdr_10000_highlight[0] - hdr_10000_highlight[1]) >
          2.0e-5 ||
      std::abs(hdr_10000_highlight[1] - hdr_10000_highlight[2]) >
          2.0e-5) {
    std::cerr << "explicit 10000-nit targets should preserve neutral output above the legacy 16x recipe ceiling: output="
              << hdr_10000_highlight[0] << ','
              << hdr_10000_highlight[1] << ','
              << hdr_10000_highlight[2] << '\n';
    return EXIT_FAILURE;
  }
  auto aces_pipeline_config = default_config;
  aces_pipeline_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1;
  const auto aces_output = photo_editor::pipeline::apply_output_transform(
      photo_editor::pipeline::ColorPixel{
          .red = 8.0F,
          .green = 0.4F,
          .blue = 0.05F,
          .alpha = 0.75F,
      },
      aces_pipeline_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (aces_output.output_profile_name != "sRGB" ||
      aces_output.pixel.alpha != 0.75F ||
      aces_output.pixel.red < 0.0F || aces_output.pixel.red > 1.0F ||
      aces_output.pixel.green < 0.0F || aces_output.pixel.green > 1.0F ||
      aces_output.pixel.blue < 0.0F || aces_output.pixel.blue > 1.0F) {
    std::cerr << "the selected ACES 2 engine output path should return bounded linear target RGB and preserve alpha\n";
    return EXIT_FAILURE;
  }
  auto aces_lut_config = aces_pipeline_config;
  aces_lut_config.black_point_compensation_enabled = true;
  aces_lut_config.source_black_point = 0.02;
  aces_lut_config.output_black_point = 0.005;
  const auto aces_lut_application =
      photo_editor::pipeline::compile_aces2_output_lut_application(
          aces_lut_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
          photo_editor::pipeline::Aces2OutputLutQuality::Final);
  if (official_aces2_available) {
    if (!aces_lut_application.ready() ||
        aces_lut_application.lut->settings.cube_size != 97 ||
        !aces_lut_application.black_point_compensation_enabled) {
      std::cerr << "official ACES 2 should compile a final-quality, BPC-aware output LUT application\n";
      return EXIT_FAILURE;
    }
    auto transition_lut_config = aces_pipeline_config;
    transition_lut_config.renderer_transition_look =
        photo_editor::pipeline::RendererTransitionLook::
            PreserveLegacyDirectLuminanceV1;
    const auto ordinary_interactive_lut =
        photo_editor::pipeline::compile_aces2_output_lut_application(
            aces_pipeline_config,
            photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
            photo_editor::pipeline::Aces2OutputLutQuality::Interactive);
    const auto transition_interactive_lut =
        photo_editor::pipeline::compile_aces2_output_lut_application(
            transition_lut_config,
            photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
            photo_editor::pipeline::Aces2OutputLutQuality::Interactive);
    if (!ordinary_interactive_lut.ready() ||
        !transition_interactive_lut.ready()) {
      std::cerr << "ordinary and transition ACES LUTs must both compile on the official processor\n";
      return EXIT_FAILURE;
    }
    const auto neutral_sample = photo_editor::pipeline::ColorPixel{
        0.18F, 0.18F, 0.18F, 1.0F};
    auto transition_exact =
        std::vector<photo_editor::pipeline::ColorPixel>{neutral_sample};
    photo_editor::pipeline::apply_output_gamut_mapping_in_working_space(
        transition_exact,
        transition_lut_config,
        photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    const auto transition_approximated =
        photo_editor::pipeline::apply_aces2_output_lut_application(
            transition_interactive_lut, neutral_sample);
    const auto ordinary_approximated =
        photo_editor::pipeline::apply_aces2_output_lut_application(
            ordinary_interactive_lut, neutral_sample);
    const auto transition_lut_error =
        photo_editor::pipeline::perceptual_delta(
            transition_exact.front(),
            transition_approximated,
            transition_lut_config.working_profile.color_space);
    if (ordinary_interactive_lut.lut->cache_key ==
            transition_interactive_lut.lut->cache_key ||
        transition_interactive_lut.lut->table ==
            ordinary_interactive_lut.lut->table ||
        !std::isfinite(transition_lut_error) ||
        transition_lut_error > 0.75 ||
        transition_approximated.red <= ordinary_approximated.red + 0.03F) {
      std::cerr << "the production ACES LUT must contain the versioned renderer transition and match its direct processor: error="
                << transition_lut_error << '\n';
      return EXIT_FAILURE;
    }
    for (const auto& sample : {
             photo_editor::pipeline::ColorPixel{0.0F, 0.0F, 0.0F, 0.25F},
             photo_editor::pipeline::ColorPixel{0.18F, 0.18F, 0.18F, 0.5F},
             photo_editor::pipeline::ColorPixel{1.0F, 0.25F, 0.05F, 0.75F},
             photo_editor::pipeline::ColorPixel{4.0F, 1.0F, 0.25F, 1.0F},
             photo_editor::pipeline::ColorPixel{16.0F, 0.5F, 8.0F, 0.9F},
             photo_editor::pipeline::ColorPixel{0.4F, 0.2F, 0.1F, -0.25F},
             photo_editor::pipeline::ColorPixel{0.4F, 0.2F, 0.1F, 1.25F},
         }) {
      auto exact = std::vector<photo_editor::pipeline::ColorPixel>{sample};
      photo_editor::pipeline::apply_output_gamut_mapping_in_working_space(
          exact,
          aces_lut_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
      const auto approximated =
          photo_editor::pipeline::apply_aces2_output_lut_application(
              aces_lut_application,
              sample);
      const auto error = photo_editor::pipeline::perceptual_delta(
          exact.front(),
          approximated,
          aces_lut_config.working_profile.color_space);
      if (!std::isfinite(approximated.red) ||
          !std::isfinite(approximated.green) ||
          !std::isfinite(approximated.blue) ||
          approximated.alpha != std::clamp(sample.alpha, 0.0F, 1.0F) ||
          error > 0.75) {
        std::cerr << "final ACES 2 LUT application should match exact working-space output with BPC and publication-domain alpha: error="
                  << error << '\n';
        return EXIT_FAILURE;
      }
    }
    auto bulk_samples = std::vector<photo_editor::pipeline::ColorPixel>{
        {0.18F, 0.18F, 0.18F, 0.5F},
        {4.0F, 1.0F, 0.25F, 1.0F},
        {16.0F, 0.5F, 8.0F, 0.9F},
    };
    auto scalar_samples = bulk_samples;
    for (auto& sample : scalar_samples) {
      sample = photo_editor::pipeline::apply_aces2_output_lut_application(
          aces_lut_application,
          sample);
    }
    photo_editor::pipeline::apply_aces2_output_lut_application(
        bulk_samples,
        aces_lut_application);
    for (std::size_t index = 0; index < bulk_samples.size(); ++index) {
      const auto& bulk = bulk_samples[index];
      const auto& scalar = scalar_samples[index];
      if (bulk.red != scalar.red ||
          bulk.green != scalar.green ||
          bulk.blue != scalar.blue ||
          bulk.alpha != scalar.alpha) {
        std::cerr << "parallel ACES 2 LUT application should be bit-identical to scalar application\n";
        return EXIT_FAILURE;
      }
    }
  } else if (aces_lut_application.ready()) {
    std::cerr << "builds without OpenColorIO must not advertise a GPU output LUT\n";
    return EXIT_FAILURE;
  }
  auto aces_relative_config = aces_pipeline_config;
  aces_relative_config.render_intent =
      photo_editor::pipeline::RenderIntent::RelativeColorimetric;
  auto legacy_relative_config = aces_relative_config;
  legacy_relative_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::LegacyOklchV1;
  const photo_editor::pipeline::ColorPixel relative_input{
      .red = 1.4F,
      .green = 0.25F,
      .blue = -0.05F,
      .alpha = 0.6F,
  };
  const auto aces_relative_output =
      photo_editor::pipeline::apply_output_transform(
          relative_input,
          aces_relative_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  const auto legacy_relative_output =
      photo_editor::pipeline::apply_output_transform(
          relative_input,
          legacy_relative_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (std::abs(aces_relative_output.pixel.red -
               legacy_relative_output.pixel.red) > 1.0e-7F ||
      std::abs(aces_relative_output.pixel.green -
               legacy_relative_output.pixel.green) > 1.0e-7F ||
      std::abs(aces_relative_output.pixel.blue -
               legacy_relative_output.pixel.blue) > 1.0e-7F ||
      aces_relative_output.pixel.alpha != relative_input.alpha) {
    std::cerr << "relative-colorimetric proofing should bypass the photographic renderer even when the ACES 2 model is selected\n";
    return EXIT_FAILURE;
  }
  std::vector<photo_editor::pipeline::ColorPixel> aces_working_pixels = {
      {.red = 8.0F, .green = 0.4F, .blue = 0.05F, .alpha = 1.0F},
      {.red = 0.18F, .green = 0.18F, .blue = 0.18F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_output_gamut_mapping_in_working_space(
      aces_working_pixels,
      aces_pipeline_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  const auto working_to_srgb =
      photo_editor::pipeline::compile_builtin_color_transform(
          aces_pipeline_config.working_profile.color_space,
          aces_pipeline_config.preview_display_target.color_space)
          .matrix;
  for (const auto& pixel : aces_working_pixels) {
    const auto target_rgb = photo_editor::pipeline::multiply_matrix3_vector(
        working_to_srgb,
        {pixel.red, pixel.green, pixel.blue});
    for (const auto channel : target_rgb) {
      if (!std::isfinite(channel) || channel < -2.0e-6 ||
          channel > 1.0 + 2.0e-6) {
        std::cerr << "ACES 2 working-space output mapping should survive the desktop target conversion without excursions\n";
        return EXIT_FAILURE;
      }
    }
  }
  const auto pq_reference_signal =
      photo_editor::pipeline::encode_display_eotf(1.0, hdr_display_target);
  if (std::abs(photo_editor::pipeline::decode_display_eotf(
                   pq_reference_signal,
                   hdr_display_target) -
               1.0) > 1.0e-9 ||
      std::abs(photo_editor::pipeline::encode_display_eotf(
                   10000.0 / hdr_display_target.reference_white_nits,
                   hdr_display_target) -
               1.0) > 1.0e-9) {
    std::cerr << "PQ display encoding should use absolute 10,000-nit normalization and round-trip\n";
    return EXIT_FAILURE;
  }
  auto hlg_display_target = hdr_display_target;
  hlg_display_target.eotf = photo_editor::pipeline::DisplayEotf::Hlg;
  if (std::abs(photo_editor::pipeline::encode_display_eotf(
                   1.0 / 12.0,
                   hlg_display_target) -
               0.5) > 1.0e-7 ||
      std::abs(photo_editor::pipeline::decode_display_eotf(
                   0.5,
                   hlg_display_target) -
               (1.0 / 12.0)) > 1.0e-9) {
    std::cerr << "HLG display encoding should preserve its piecewise join and round-trip\n";
    return EXIT_FAILURE;
  }
  if (!default_config.output_gamut_mapping_enabled ||
      default_config.output_gamut_mapping_policy !=
          photo_editor::pipeline::GamutMappingPolicy::Perceptual) {
    std::cerr << "default output gamut mapping should use perceptual compression\n";
    return EXIT_FAILURE;
  }
  if (!default_config.black_point_compensation_enabled ||
      std::abs(default_config.source_black_point) > 1.0e-9 ||
      std::abs(default_config.output_black_point) > 1.0e-9) {
    std::cerr << "default black-point compensation should be enabled but neutral\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_output_color_profile_name("display-p3") != "Display P3") {
    std::cerr << "output profile normalization should canonicalize Display P3\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_render_intent_name("relative-colorimetric") !=
      "Relative Colorimetric") {
    std::cerr << "render intent normalization should canonicalize relative colorimetric\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_gamut_mapping_policy_name("clipping") != "Clip" ||
      photo_editor::pipeline::normalize_gamut_mapping_policy_name("preserve-saturation") !=
          "Preserve Saturation") {
    std::cerr << "gamut mapping policy normalization should canonicalize supported policies\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_working_color_profile_name("linear-rec2020") !=
      "Linear Rec.2020") {
    std::cerr << "working profile normalization should canonicalize Linear Rec.2020\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::normalize_output_color_profile_name("rec2020") != "Rec.2020") {
    std::cerr << "output profile normalization should canonicalize Rec.2020\n";
    return EXIT_FAILURE;
  }

  struct ProfileExpectation final {
    const char* input_name;
    const char* canonical_name;
    photo_editor::pipeline::OutputTransformKind output_kind;
    photo_editor::pipeline::ColorPrimaries primaries;
  };
  const std::vector<ProfileExpectation> rgb_profile_expectations = {
      {"srgb", "sRGB", photo_editor::pipeline::OutputTransformKind::Srgb,
       photo_editor::pipeline::ColorPrimaries::Srgb},
      {"display-p3", "Display P3", photo_editor::pipeline::OutputTransformKind::DisplayP3,
       photo_editor::pipeline::ColorPrimaries::DisplayP3},
      {"adobe-rgb", "Adobe RGB", photo_editor::pipeline::OutputTransformKind::AdobeRgb,
       photo_editor::pipeline::ColorPrimaries::AdobeRgb},
      {"prophoto", "ProPhoto RGB", photo_editor::pipeline::OutputTransformKind::ProPhotoRgb,
       photo_editor::pipeline::ColorPrimaries::ProPhotoRgb},
      {"rec709", "Rec.709", photo_editor::pipeline::OutputTransformKind::Rec709,
       photo_editor::pipeline::ColorPrimaries::Rec709},
      {"rec2020", "Rec.2020", photo_editor::pipeline::OutputTransformKind::Rec2020,
       photo_editor::pipeline::ColorPrimaries::Rec2020},
  };
  for (const auto& expectation : rgb_profile_expectations) {
    const auto input_profile =
        photo_editor::pipeline::input_color_profile_from_name(expectation.input_name);
    const auto output_profile =
        photo_editor::pipeline::output_color_profile_from_name(expectation.input_name);
    if (input_profile.profile_name != expectation.canonical_name ||
        input_profile.kind != photo_editor::pipeline::InputTransformKind::EmbeddedProfile ||
        input_profile.color_space.primaries != expectation.primaries) {
      std::cerr << "input profile should support " << expectation.canonical_name << '\n';
      return EXIT_FAILURE;
    }
    if (output_profile.profile_name != expectation.canonical_name ||
        output_profile.kind != expectation.output_kind ||
        output_profile.color_space.primaries != expectation.primaries) {
      std::cerr << "output profile should support " << expectation.canonical_name << '\n';
      return EXIT_FAILURE;
    }
  }
  if (photo_editor::pipeline::make_prophoto_rgb_color_space().transfer_function !=
      photo_editor::pipeline::TransferFunction::Gamma18) {
    std::cerr << "ProPhoto RGB should use its standard 1.8 transfer gamma\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::working_color_profile_from_name("linear-display-p3")
          .color_space.primaries != photo_editor::pipeline::ColorPrimaries::DisplayP3 ||
      photo_editor::pipeline::working_color_profile_from_name("linear-prophoto-rgb")
          .color_space.primaries != photo_editor::pipeline::ColorPrimaries::ProPhotoRgb ||
      photo_editor::pipeline::working_color_profile_from_name("linear-rec2020")
          .color_space.primaries != photo_editor::pipeline::ColorPrimaries::Rec2020) {
    std::cerr << "working profiles should support wide-gamut RGB working spaces\n";
    return EXIT_FAILURE;
  }

  const auto decoder_rendered_raw = photo_editor::pipeline::DecodedImageMetadata{
      .valid = true,
      .is_raw = true,
      .pixel_encoding = photo_editor::pipeline::DecodedPixelEncoding::SceneLinearRgb,
      .decoder_backend = "Core Image CIRAWFilter",
      .decoded_color_space_name = "Linear sRGB",
      .camera_profile_identity = "core-image-camera-characterization",
      .demosaic_applied = true,
      .white_balance_applied = true,
      .camera_transform_applied = true,
      .decoder_rendering_applied = true,
  };
  if (!photo_editor::pipeline::decoded_image_color_boundary_valid(decoder_rendered_raw) ||
      photo_editor::pipeline::decoded_image_requires_engine_input_transform(decoder_rendered_raw) ||
      std::string(photo_editor::pipeline::decoded_pixel_encoding_name(
          decoder_rendered_raw.pixel_encoding)) != "scene-linear-rgb") {
    std::cerr << "decoder-rendered RAW RGB must not receive a second camera transform\n";
    return EXIT_FAILURE;
  }
  const auto decoder_owned_profile =
      photo_editor::pipeline::resolve_camera_profile(decoder_rendered_raw);
  if (decoder_owned_profile.valid ||
      decoder_owned_profile.profile_format != "Decoder Applied" ||
      decoder_owned_profile.profile_name != "core-image-camera-characterization" ||
      decoder_owned_profile.selection_source !=
          photo_editor::pipeline::CameraProfileSelectionSource::DecoderCharacterization ||
      decoder_owned_profile.selection_precedence_rank != 4 ||
      decoder_owned_profile.fallback_used) {
    std::cerr << "decoder-owned RAW must not resolve a second synthetic camera profile\n";
    return EXIT_FAILURE;
  }
  const auto engine_owned_raw = photo_editor::pipeline::DecodedImageMetadata{
      .valid = true,
      .is_raw = true,
      .pixel_encoding = photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb,
      .decoder_backend = "reference-sensor-linear-backend",
      .decoded_color_space_name = "Camera RGB",
      .camera_profile_identity = "dcp:fixture",
      .demosaic_applied = true,
      .white_balance_applied = false,
      .camera_transform_applied = false,
      .decoder_rendering_applied = false,
  };
  if (!photo_editor::pipeline::decoded_image_color_boundary_valid(engine_owned_raw) ||
      !photo_editor::pipeline::decoded_image_requires_engine_input_transform(engine_owned_raw)) {
    std::cerr << "camera-linear RAW RGB must declare that the engine owns characterization\n";
    return EXIT_FAILURE;
  }
  auto contradictory_boundary = engine_owned_raw;
  contradictory_boundary.camera_transform_applied = true;
  if (photo_editor::pipeline::decoded_image_color_boundary_valid(contradictory_boundary) ||
      photo_editor::pipeline::decoded_image_requires_engine_input_transform(
          contradictory_boundary)) {
    std::cerr << "contradictory RAW color ownership must fail closed\n";
    return EXIT_FAILURE;
  }
  const auto contradictory_profile =
      photo_editor::pipeline::resolve_camera_profile(contradictory_boundary);
  if (contradictory_profile.valid ||
      contradictory_profile.profile_format != "Invalid Decode Boundary") {
    std::cerr << "invalid RAW ownership must not resolve a synthetic camera profile\n";
    return EXIT_FAILURE;
  }
  const auto unspecified_raw = photo_editor::pipeline::DecodedImageMetadata{
      .valid = true,
      .is_raw = true,
  };
  if (photo_editor::pipeline::decoded_image_color_boundary_valid(unspecified_raw) ||
      photo_editor::pipeline::decoded_image_requires_engine_input_transform(unspecified_raw)) {
    std::cerr << "unspecified RAW ownership must not guess that an engine transform is safe\n";
    return EXIT_FAILURE;
  }
  const auto unspecified_profile =
      photo_editor::pipeline::resolve_camera_profile(unspecified_raw);
  if (unspecified_profile.valid ||
      unspecified_profile.failure_reason != "raw_decode_color_boundary_invalid" ||
      unspecified_profile.selection_source !=
          photo_editor::pipeline::CameraProfileSelectionSource::None) {
    std::cerr << "unspecified RAW profile resolution must fail closed\n";
    return EXIT_FAILURE;
  }

  const auto resolved_profile = photo_editor::pipeline::resolve_camera_profile(
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .pixel_encoding = photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb,
          .decoder_backend = "reference-camera-linear-backend",
          .decoded_color_space_name = "Camera RGB",
          .demosaic_applied = true,
          .input_profile_name = "Camera Native",
          .camera_make = "Canon",
          .camera_model = "EOS R5",
          .as_shot_temperature = 4850.0,
          .as_shot_tint = 8.0,
          .baseline_exposure = 0.25,
          .highlight_headroom = 0.9,
      });
  if (resolved_profile.valid || resolved_profile.profile_name != "Canon EOS R5" ||
      resolved_profile.selection_source !=
          photo_editor::pipeline::CameraProfileSelectionSource::UnprofiledFallback ||
      resolved_profile.selection_precedence_rank != 5 ||
      !resolved_profile.fallback_used ||
      resolved_profile.failure_reason !=
          "camera_linear_characterization_required") {
    std::cerr << "uncharacterized camera-linear RAW must fail closed while preserving identity\n";
    return EXIT_FAILURE;
  }

  const auto decoder_matrix_profile =
      photo_editor::pipeline::resolve_camera_profile(
          photo_editor::pipeline::DecodedImageMetadata{
              .valid = true,
              .is_raw = true,
              .pixel_encoding =
                  photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb,
              .decoder_backend = "LibRaw camera-linear fixture",
              .decoded_color_space_name = "Camera RGB",
              .demosaic_applied = true,
              .white_balance_applied = true,
              .camera_transform_applied = false,
              .decoder_camera_to_xyz_valid = true,
              .decoder_camera_to_xyz = {
                  0.62, 0.24, 0.14,
                  0.18, 0.72, 0.10,
                  0.02, 0.12, 0.86,
              },
              .decoder_camera_to_xyz_reference_white = "D65",
              .input_profile_name = "Embedded",
              .camera_make = "Fixture",
              .camera_model = "Matrix Camera",
          });
  if (!decoder_matrix_profile.valid ||
      decoder_matrix_profile.profile_format != "LibRaw Camera Matrix" ||
      decoder_matrix_profile.selection_source !=
          photo_editor::pipeline::CameraProfileSelectionSource::
              DecoderCharacterization ||
      decoder_matrix_profile.fallback_used ||
      decoder_matrix_profile.execution_model !=
          "libraw-camera-to-xyz-d65:v1" ||
      decoder_matrix_profile.camera_to_xyz_d50 !=
          std::array<double, 9>{
              0.62, 0.24, 0.14,
              0.18, 0.72, 0.10,
              0.02, 0.12, 0.86,
          }) {
    std::cerr << "decoder camera matrix must become an executable engine characterization\n";
    return EXIT_FAILURE;
  }
  auto decoder_matrix_state =
      photo_editor::pipeline::ColorAdjustmentState{};
  decoder_matrix_state.input_transform.raw_input_transform_enabled = true;
  decoder_matrix_state.input_transform.highlight_recovery_enabled = false;
  decoder_matrix_state.color_pipeline.input_profile =
      photo_editor::pipeline::input_color_profile_from_name("Embedded");
  decoder_matrix_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  decoder_matrix_state.color_pipeline.camera_profile =
      decoder_matrix_profile;
  auto characterized_sample =
      std::vector<photo_editor::pipeline::ColorPixel>{
      {.red = 0.40F, .green = 0.20F, .blue = 0.10F, .alpha = 1.0F},
  };
  auto identity_sample = characterized_sample;
  photo_editor::pipeline::apply_color_adjustments(
      &characterized_sample, 1, 1, decoder_matrix_state);
  decoder_matrix_state.color_pipeline.camera_profile = {};
  photo_editor::pipeline::apply_color_adjustments(
      &identity_sample, 1, 1, decoder_matrix_state);
  const auto characterization_delta =
      std::abs(characterized_sample[0].red - identity_sample[0].red) +
      std::abs(characterized_sample[0].green - identity_sample[0].green) +
      std::abs(characterized_sample[0].blue - identity_sample[0].blue);
  if (!std::isfinite(characterization_delta) ||
      characterization_delta < 1.0e-3F) {
    std::cerr << "decoder camera matrix must execute in the engine input stage\n";
    return EXIT_FAILURE;
  }
  auto decoder_white_balance_state = decoder_matrix_state;
  decoder_white_balance_state.color_pipeline.camera_profile =
      decoder_matrix_profile;
  decoder_white_balance_state.color_pipeline.input_transform_recipe = {
      .has_source_white_balance = true,
      .decoder_white_balance_applied = true,
      .camera_profile_name = decoder_matrix_profile.profile_name,
      .source_temperature = 3000.0,
      .source_tint = -12.0,
  };
  auto first_owned_white_balance =
      std::vector<photo_editor::pipeline::ColorPixel>{
          {.red = 0.40F, .green = 0.20F, .blue = 0.10F, .alpha = 1.0F},
      };
  auto second_owned_white_balance = first_owned_white_balance;
  photo_editor::pipeline::apply_color_adjustments(
      &first_owned_white_balance, 1, 1, decoder_white_balance_state);
  decoder_white_balance_state.color_pipeline.input_transform_recipe
      .source_temperature = 8000.0;
  decoder_white_balance_state.color_pipeline.input_transform_recipe
      .source_tint = 18.0;
  photo_editor::pipeline::apply_color_adjustments(
      &second_owned_white_balance, 1, 1, decoder_white_balance_state);
  const auto owned_white_balance_delta =
      std::abs(
          first_owned_white_balance[0].red -
          second_owned_white_balance[0].red) +
      std::abs(
          first_owned_white_balance[0].green -
          second_owned_white_balance[0].green) +
      std::abs(
          first_owned_white_balance[0].blue -
          second_owned_white_balance[0].blue);
  auto creative_temperature_sample = first_owned_white_balance;
  decoder_white_balance_state.input_transform.temperature = 24.0;
  photo_editor::pipeline::apply_color_adjustments(
      &creative_temperature_sample, 1, 1, decoder_white_balance_state);
  const auto creative_temperature_delta =
      std::abs(
          first_owned_white_balance[0].red -
          creative_temperature_sample[0].red) +
      std::abs(
          first_owned_white_balance[0].green -
          creative_temperature_sample[0].green) +
      std::abs(
          first_owned_white_balance[0].blue -
          creative_temperature_sample[0].blue);
  if (owned_white_balance_delta > 1.0e-6F ||
      creative_temperature_delta < 1.0e-4F) {
    std::cerr << "decoder-owned RAW white balance must not be applied twice, while creative temperature remains active\n";
    return EXIT_FAILURE;
  }

  const auto dcp_input_profile =
      photo_editor::pipeline::input_color_profile_from_name("/profiles/Canon EOS R5 Adobe Standard.dcp");
  if (dcp_input_profile.profile_name != "Camera DCP: Canon EOS R5 Adobe Standard.dcp" ||
      dcp_input_profile.kind != photo_editor::pipeline::InputTransformKind::CameraDcpProfile) {
    std::cerr << "DCP camera profiles should normalize as camera input profiles\n";
    return EXIT_FAILURE;
  }
  const auto icc_input_profile =
      photo_editor::pipeline::input_color_profile_from_name("icc: Canon EOS R5 Manufacturer");
  if (icc_input_profile.profile_name != "Camera ICC: Canon EOS R5 Manufacturer" ||
      icc_input_profile.kind != photo_editor::pipeline::InputTransformKind::CameraIccProfile) {
    std::cerr << "ICC camera profiles should normalize as camera input profiles\n";
    return EXIT_FAILURE;
  }
  const auto decoder_rejected_dcp = photo_editor::pipeline::resolve_camera_profile(
      decoder_rendered_raw,
      dcp_input_profile);
  const auto decoder_rejected_icc = photo_editor::pipeline::resolve_camera_profile(
      decoder_rendered_raw,
      icc_input_profile);
  if (decoder_rejected_dcp.valid ||
      decoder_rejected_dcp.selection_source !=
          photo_editor::pipeline::CameraProfileSelectionSource::ExplicitUserDcp ||
      decoder_rejected_dcp.selection_precedence_rank != 1 ||
      decoder_rejected_dcp.failure_reason !=
          "dcp_profile_requires_camera_linear_raw" ||
      decoder_rejected_icc.valid ||
      decoder_rejected_icc.selection_source !=
          photo_editor::pipeline::CameraProfileSelectionSource::ExplicitUserIcc ||
      decoder_rejected_icc.failure_reason !=
          "camera_icc_profile_requires_camera_linear_raw") {
    std::cerr << "explicit camera profiles must retain precedence and fail instead of silently using decoder characterization\n";
    return EXIT_FAILURE;
  }
  if (std::string(photo_editor::pipeline::camera_profile_selection_source_name(
          photo_editor::pipeline::CameraProfileSelectionSource::EmbeddedCameraProfile)) !=
          "embedded-camera-profile" ||
      photo_editor::pipeline::camera_profile_selection_precedence_rank(
          photo_editor::pipeline::CameraProfileSelectionSource::BundledCameraProfile) != 3) {
    std::cerr << "camera profile precedence names and ranks should remain stable for provenance\n";
    return EXIT_FAILURE;
  }
  const auto unresolved_icc_profile = photo_editor::pipeline::resolve_camera_profile(
      engine_owned_raw,
      icc_input_profile);
  if (unresolved_icc_profile.valid ||
      unresolved_icc_profile.failure_reason != "camera_icc_profile_asset_required" ||
      unresolved_icc_profile.selection_source !=
          photo_editor::pipeline::CameraProfileSelectionSource::ExplicitUserIcc ||
      unresolved_icc_profile.fallback_used) {
    std::cerr << "camera ICC names must not manufacture characterization matrices\n";
    return EXIT_FAILURE;
  }
  const auto resolved_icc_asset = photo_editor::pipeline::ResolvedIccProfileAsset{
      .reference = photo_editor::pipeline::IccProfileAssetReference{
          .asset_id = "icc:camera-profile-fixture",
          .payload_sha256 = std::string(64U, 'c'),
          .display_name = "Canon EOS R5 Manufacturer",
          .source_uri = "memory:camera-profile-fixture",
          .source_kind = "test-generated",
          .byte_size = 128U,
      },
      .status = photo_editor::pipeline::IccProfileAssetStatus::Ready,
      .payload = std::make_shared<const std::vector<std::byte>>(
          128U,
          std::byte{0}),
      .resolved_uri = "memory:camera-profile-fixture",
  };
  const auto resolved_icc_profile = photo_editor::pipeline::resolve_camera_profile(
      engine_owned_raw,
      icc_input_profile,
      nullptr,
      &resolved_icc_asset);
  const auto identity_camera_matrix = std::array<double, 9>{
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  if (!resolved_icc_profile.valid ||
      resolved_icc_profile.execution_model != "littlecms-input-icc-to-working:v1" ||
      resolved_icc_profile.payload_sha256 != resolved_icc_asset.reference.payload_sha256 ||
      resolved_icc_profile.selection_source !=
          photo_editor::pipeline::CameraProfileSelectionSource::ExplicitUserIcc ||
      resolved_icc_profile.camera_to_xyz_d50 != identity_camera_matrix) {
    std::cerr << "payload-backed camera ICC should delegate exactly once to the ICC transform boundary\n";
    return EXIT_FAILURE;
  }

  const auto parsed_dcp = photo_editor::pipeline::DcpProfile{
      .payload_sha256 = std::string(64U, 'a'),
      .byte_size = 1024U,
      .profile_name = "Canon EOS R5 Fixture DCP",
      .unique_camera_model = "Canon EOS R5",
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
      .baseline_exposure = -0.35,
  };
  const auto unresolved_dcp_profile = photo_editor::pipeline::resolve_camera_profile(
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .pixel_encoding = photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb,
          .decoder_backend = "reference-camera-linear-backend",
          .decoded_color_space_name = "Camera RGB",
          .demosaic_applied = true,
          .camera_make = "Canon",
          .camera_model = "EOS R5",
      },
      dcp_input_profile);
  if (unresolved_dcp_profile.valid ||
      unresolved_dcp_profile.failure_reason != "dcp_profile_asset_required") {
    std::cerr << "DCP profile names must not manufacture camera transforms without parsed bytes\n";
    return EXIT_FAILURE;
  }

  const auto dcp_resolved_profile = photo_editor::pipeline::resolve_camera_profile(
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .pixel_encoding = photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb,
          .decoder_backend = "reference-camera-linear-backend",
          .decoded_color_space_name = "Camera RGB",
          .demosaic_applied = true,
          .input_profile_name = dcp_input_profile.profile_name,
          .camera_make = "Canon",
          .camera_model = "EOS R5",
          .as_shot_temperature = 4850.0,
          .as_shot_tint = 8.0,
      },
      dcp_input_profile,
      &parsed_dcp);
  if (!dcp_resolved_profile.valid || dcp_resolved_profile.profile_format != "DCP" ||
      dcp_resolved_profile.profile_name != parsed_dcp.profile_name ||
      dcp_resolved_profile.payload_sha256 != parsed_dcp.payload_sha256 ||
      dcp_resolved_profile.selection_source !=
          photo_editor::pipeline::CameraProfileSelectionSource::ExplicitUserDcp ||
      dcp_resolved_profile.fallback_used ||
      !dcp_resolved_profile.dual_illuminant ||
      dcp_resolved_profile.execution_model != "dng-camera-to-pcs:v1" ||
      !dcp_resolved_profile.used_forward_matrix ||
      !std::isfinite(dcp_resolved_profile.camera_to_xyz_d50_daylight[0])) {
    std::cerr << "DCP failure reason: " << dcp_resolved_profile.failure_reason << '\n';
    std::cerr << "DCP camera profiles should resolve into a camera transform\n";
    return EXIT_FAILURE;
  }

  const auto resolved_recipe = photo_editor::pipeline::resolve_input_transform_recipe(
      photo_editor::pipeline::input_color_profile_from_name("Camera Native"),
      resolved_profile,
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .input_profile_name = "Camera Native",
          .camera_make = "Canon",
          .camera_model = "EOS R5",
          .as_shot_temperature = 4850.0,
          .as_shot_tint = 8.0,
          .baseline_exposure = 0.25,
          .highlight_headroom = 0.9,
      });
  if (!resolved_recipe.has_source_white_balance ||
      std::abs(resolved_recipe.source_temperature - 4850.0) > 1.0e-6 ||
      std::abs(resolved_recipe.baseline_exposure - 0.25) > 1.0e-6) {
    std::cerr << "input transform recipe should keep decoded RAW normalization metadata\n";
    return EXIT_FAILURE;
  }

  const auto dcp_recipe = photo_editor::pipeline::resolve_input_transform_recipe(
      dcp_input_profile,
      dcp_resolved_profile,
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .as_shot_temperature = 4850.0,
          .as_shot_tint = 8.0,
          .baseline_exposure = 0.25,
      });
  if (!dcp_recipe.has_source_white_balance ||
      std::abs(dcp_recipe.baseline_exposure - (-0.10)) > 1.0e-9) {
    std::cerr << "DCP baseline exposure offset should compose with RAW metadata\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState state;
  state.input_transform.exposure = 1.25;
  if (photo_editor::pipeline::effective_input_exposure(state) != 1.25) {
    std::cerr << "input exposure helper should fall back to legacy exposure\n";
    return EXIT_FAILURE;
  }
  state.input_transform.exposure = 0.5;
  state.input_transform.temperature = 12.0;
  state.input_transform.tint = -8.0;
  if (photo_editor::pipeline::effective_input_exposure(state) != 0.5 ||
      photo_editor::pipeline::effective_input_temperature(state) != 12.0 ||
      photo_editor::pipeline::effective_input_tint(state) != -8.0) {
    std::cerr << "input transform helpers should prefer explicit input transform values\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState white_balance_bradford;
  white_balance_bradford.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  white_balance_bradford.input_transform.temperature = 55.0;
  white_balance_bradford.input_transform.tint = 30.0;
  white_balance_bradford.input_transform.chromatic_adaptation_method = "Bradford";
  std::vector<photo_editor::pipeline::ColorPixel> bradford_pixels = {
      {.red = 0.5F, .green = 0.5F, .blue = 0.5F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&bradford_pixels, 1, 1, white_balance_bradford);

  auto white_balance_cat02 = white_balance_bradford;
  white_balance_cat02.input_transform.chromatic_adaptation_method = "CAT02";
  std::vector<photo_editor::pipeline::ColorPixel> cat02_pixels = {
      {.red = 0.5F, .green = 0.5F, .blue = 0.5F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&cat02_pixels, 1, 1, white_balance_cat02);
  const auto adaptation_delta = std::max({
      std::abs(bradford_pixels[0].red - cat02_pixels[0].red),
      std::abs(bradford_pixels[0].green - cat02_pixels[0].green),
      std::abs(bradford_pixels[0].blue - cat02_pixels[0].blue),
  });
  if (adaptation_delta <= 1.0e-5F) {
    std::cerr << "chromatic adaptation method should affect white balance in wide-gamut working space\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState input_stage_white_balance_state;
  input_stage_white_balance_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  input_stage_white_balance_state.color_pipeline.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  input_stage_white_balance_state.color_pipeline.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("Display P3");
  input_stage_white_balance_state.input_transform.raw_input_transform_enabled = true;
  input_stage_white_balance_state.input_transform.temperature = 46.0;
  input_stage_white_balance_state.input_transform.tint = -24.0;
  input_stage_white_balance_state.input_transform.chromatic_adaptation_method = "Bradford";
  input_stage_white_balance_state.input_transform.raw_black_level = 0.04;
  input_stage_white_balance_state.input_transform.raw_white_level = 1.18;
  input_stage_white_balance_state.input_transform.calibration_hue = 18.0;
  input_stage_white_balance_state.input_transform.red_primary_saturation = 14.0;
  input_stage_white_balance_state.input_transform.exposure = 0.38;
  input_stage_white_balance_state.contrast = 24.0;
  input_stage_white_balance_state.color_pipeline.output_headroom = 1.0;
  std::vector<photo_editor::pipeline::ColorPixel> input_stage_white_balance_pixels = {
      {.red = 0.68F, .green = 0.37F, .blue = 0.16F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &input_stage_white_balance_pixels,
      1,
      1,
      input_stage_white_balance_state);

  auto late_white_balance_state = input_stage_white_balance_state;
  late_white_balance_state.input_transform.temperature = 0.0;
  late_white_balance_state.input_transform.tint = 0.0;
  photo_editor::pipeline::PhotoLayer late_white_balance_layer;
  late_white_balance_layer.enabled = true;
  late_white_balance_layer.kind = "adjustment";
  late_white_balance_layer.blend_mode = "normal";
  late_white_balance_layer.opacity = 1.0;
  late_white_balance_layer.temperature = 46.0;
  late_white_balance_layer.tint = -24.0;
  late_white_balance_state.photo_layers.push_back(late_white_balance_layer);
  std::vector<photo_editor::pipeline::ColorPixel> late_white_balance_pixels = {
      {.red = 0.68F, .green = 0.37F, .blue = 0.16F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &late_white_balance_pixels,
      1,
      1,
      late_white_balance_state);
  const auto input_vs_late_white_balance_delta = std::max({
      std::abs(input_stage_white_balance_pixels[0].red - late_white_balance_pixels[0].red),
      std::abs(input_stage_white_balance_pixels[0].green - late_white_balance_pixels[0].green),
      std::abs(input_stage_white_balance_pixels[0].blue - late_white_balance_pixels[0].blue),
  });
  if (input_vs_late_white_balance_delta <= 0.01F) {
    std::cerr << "RAW white balance should run in the input transform, not as late RGB tinting\n";
    return EXIT_FAILURE;
  }

  const auto preview_transform = photo_editor::pipeline::apply_output_transform(
      input_stage_white_balance_pixels[0],
      input_stage_white_balance_state.color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
      input_stage_white_balance_state.input_transform.chromatic_adaptation_method);
  const auto export_transform = photo_editor::pipeline::apply_output_transform(
      input_stage_white_balance_pixels[0],
      input_stage_white_balance_state.color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::ExportOutput,
      input_stage_white_balance_state.input_transform.chromatic_adaptation_method);
  if (preview_transform.output_profile_name != "sRGB" ||
      export_transform.output_profile_name != "Display P3" ||
      !std::isfinite(preview_transform.pixel.red) ||
      !std::isfinite(preview_transform.pixel.green) ||
      !std::isfinite(preview_transform.pixel.blue) ||
      !std::isfinite(export_transform.pixel.red) ||
      !std::isfinite(export_transform.pixel.green) ||
      !std::isfinite(export_transform.pixel.blue) ||
      preview_transform.pixel.red < -1.0e-6F ||
      preview_transform.pixel.green < -1.0e-6F ||
      preview_transform.pixel.blue < -1.0e-6F ||
      export_transform.pixel.red < -1.0e-6F ||
      export_transform.pixel.green < -1.0e-6F ||
      export_transform.pixel.blue < -1.0e-6F ||
      preview_transform.pixel.red > 1.00001F ||
      preview_transform.pixel.green > 1.00001F ||
      preview_transform.pixel.blue > 1.00001F ||
      export_transform.pixel.red > 1.00001F ||
      export_transform.pixel.green > 1.00001F ||
      export_transform.pixel.blue > 1.00001F) {
    std::cerr << "input-stage white balance should feed bounded preview and export transforms\n";
    return EXIT_FAILURE;
  }
  std::vector<photo_editor::pipeline::ColorPixel> preview_transform_pixels = {
      input_stage_white_balance_pixels[0],
  };
  std::vector<photo_editor::pipeline::ColorPixel> export_transform_pixels = {
      input_stage_white_balance_pixels[0],
  };
  photo_editor::pipeline::apply_output_transform(
      preview_transform_pixels,
      input_stage_white_balance_state.color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
      input_stage_white_balance_state.input_transform.chromatic_adaptation_method);
  photo_editor::pipeline::apply_output_transform(
      export_transform_pixels,
      input_stage_white_balance_state.color_pipeline,
      photo_editor::pipeline::GamutWarningTarget::ExportOutput,
      input_stage_white_balance_state.input_transform.chromatic_adaptation_method);
  if (std::abs(preview_transform_pixels[0].red - preview_transform.pixel.red) > 1.0e-6F ||
      std::abs(preview_transform_pixels[0].green - preview_transform.pixel.green) > 1.0e-6F ||
      std::abs(preview_transform_pixels[0].blue - preview_transform.pixel.blue) > 1.0e-6F ||
      std::abs(export_transform_pixels[0].red - export_transform.pixel.red) > 1.0e-6F ||
      std::abs(export_transform_pixels[0].green - export_transform.pixel.green) > 1.0e-6F ||
      std::abs(export_transform_pixels[0].blue - export_transform.pixel.blue) > 1.0e-6F) {
    std::cerr << "preview/export output transforms should use the same input-stage white-balance result\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState neutral_raw_state;
  neutral_raw_state.input_transform.raw_input_transform_enabled = true;
  auto calibration_state = neutral_raw_state;
  calibration_state.input_transform.calibration_hue = 24.0;
  calibration_state.input_transform.calibration_saturation = 35.0;
  calibration_state.input_transform.red_primary_hue = 40.0;
  calibration_state.input_transform.red_primary_saturation = 30.0;
  std::vector<photo_editor::pipeline::ColorPixel> calibrated_pixels = {
      {.red = 0.72F, .green = 0.18F, .blue = 0.08F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&calibrated_pixels, 1, 1, calibration_state);
  std::vector<photo_editor::pipeline::ColorPixel> neutral_calibration_pixels = {
      {.red = 0.72F, .green = 0.18F, .blue = 0.08F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &neutral_calibration_pixels,
      1,
      1,
      neutral_raw_state);
  const auto calibration_delta = std::max({
      std::abs(calibrated_pixels[0].red - neutral_calibration_pixels[0].red),
      std::abs(calibrated_pixels[0].green - neutral_calibration_pixels[0].green),
      std::abs(calibrated_pixels[0].blue - neutral_calibration_pixels[0].blue),
  });
  if (calibration_delta <= 1.0e-4F || calibrated_pixels[0].green <= neutral_calibration_pixels[0].green) {
    std::cerr << "RAW calibration controls should affect input-stage hue, saturation, and primary shifts\n";
    return EXIT_FAILURE;
  }

  auto camera_look_state = neutral_raw_state;
  camera_look_state.input_transform.camera_matching_look = "Vivid";
  std::vector<photo_editor::pipeline::ColorPixel> vivid_look_pixels = {
      {.red = 0.58F, .green = 0.30F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&vivid_look_pixels, 1, 1, camera_look_state);
  std::vector<photo_editor::pipeline::ColorPixel> standard_look_pixels = {
      {.red = 0.58F, .green = 0.30F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &standard_look_pixels,
      1,
      1,
      neutral_raw_state);
  const auto camera_look_delta = std::max({
      std::abs(vivid_look_pixels[0].red - standard_look_pixels[0].red),
      std::abs(vivid_look_pixels[0].green - standard_look_pixels[0].green),
      std::abs(vivid_look_pixels[0].blue - standard_look_pixels[0].blue),
  });
  if (camera_look_delta <= 1.0e-4F || vivid_look_pixels[0].red <= standard_look_pixels[0].red) {
    std::cerr << "camera matching looks should affect the RAW input-stage rendering\n";
    return EXIT_FAILURE;
  }

  auto dcp_profile_state = neutral_raw_state;
  dcp_profile_state.color_pipeline.input_profile = dcp_input_profile;
  dcp_profile_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  dcp_profile_state.color_pipeline.camera_profile = dcp_resolved_profile;
  dcp_profile_state.color_pipeline.camera_dcp_profile_asset =
      photo_editor::pipeline::ResolvedDcpProfileAsset{
          .reference = photo_editor::pipeline::DcpProfileAssetReference{
              .schema_version = 1U,
              .asset_id = "dcp:color-pipeline-fixture",
              .payload_sha256 = parsed_dcp.payload_sha256,
              .display_name = parsed_dcp.profile_name,
              .source_kind = "test-generated",
              .byte_size = parsed_dcp.byte_size,
          },
          .status = photo_editor::pipeline::DcpProfileAssetStatus::Ready,
          .payload = std::make_shared<const std::vector<std::byte>>(
              parsed_dcp.byte_size,
              std::byte{0}),
          .profile = std::make_shared<const photo_editor::pipeline::DcpProfile>(
              parsed_dcp),
          .resolved_uri = "memory:color-pipeline-fixture",
      };
  dcp_profile_state.color_pipeline.input_transform_recipe.has_source_white_balance = true;
  dcp_profile_state.color_pipeline.input_transform_recipe.source_temperature = 4850.0;
  dcp_profile_state.color_pipeline.input_transform_recipe.source_tint = 8.0;
  std::vector<photo_editor::pipeline::ColorPixel> dcp_profile_pixels = {
      {.red = 0.42F, .green = 0.30F, .blue = 0.18F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&dcp_profile_pixels, 1, 1, dcp_profile_state);
  std::vector<photo_editor::pipeline::ColorPixel> embedded_profile_pixels = {
      {.red = 0.42F, .green = 0.30F, .blue = 0.18F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &embedded_profile_pixels,
      1,
      1,
      neutral_raw_state);
  const auto camera_profile_delta = std::max({
      std::abs(dcp_profile_pixels[0].red - embedded_profile_pixels[0].red),
      std::abs(dcp_profile_pixels[0].green - embedded_profile_pixels[0].green),
      std::abs(dcp_profile_pixels[0].blue - embedded_profile_pixels[0].blue),
  });
  if (camera_profile_delta <= 1.0e-4F) {
    std::cerr << "DCP/ICC camera profiles should affect the input-stage camera transform\n";
    return EXIT_FAILURE;
  }
  auto unselected_dcp_state = dcp_profile_state;
  unselected_dcp_state.color_pipeline.camera_profile.selection_source =
      photo_editor::pipeline::CameraProfileSelectionSource::None;
  auto unselected_dcp_reference_state = unselected_dcp_state;
  unselected_dcp_reference_state.color_pipeline.camera_dcp_profile_asset = {};
  std::vector<photo_editor::pipeline::ColorPixel> unselected_dcp_pixels = {
      {.red = 0.42F, .green = 0.30F, .blue = 0.18F, .alpha = 0.73F},
  };
  auto unselected_dcp_reference_pixels = unselected_dcp_pixels;
  photo_editor::pipeline::apply_color_adjustments(
      &unselected_dcp_pixels,
      1,
      1,
      unselected_dcp_state);
  photo_editor::pipeline::apply_color_adjustments(
      &unselected_dcp_reference_pixels,
      1,
      1,
      unselected_dcp_reference_state);
  if (std::abs(unselected_dcp_pixels[0].red - unselected_dcp_reference_pixels[0].red) >
          1.0e-7F ||
      std::abs(unselected_dcp_pixels[0].green - unselected_dcp_reference_pixels[0].green) >
          1.0e-7F ||
      std::abs(unselected_dcp_pixels[0].blue - unselected_dcp_reference_pixels[0].blue) >
          1.0e-7F ||
      std::abs(unselected_dcp_pixels[0].alpha - 0.73F) > 1.0e-7F) {
    std::cerr << "DCP bytes must not execute unless the precedence resolver selected that explicit profile\n";
    return EXIT_FAILURE;
  }

  const auto selected_white = photo_editor::pipeline::dcp_temperature_tint_to_xy(
      4850.0,
      8.0);
  auto selected_camera_white = std::array<double, 3>{
      selected_white.x / selected_white.y,
      1.0,
      (1.0 - selected_white.x - selected_white.y) / selected_white.y,
  };
  const auto selected_camera_white_max = *std::max_element(
      selected_camera_white.cbegin(),
      selected_camera_white.cend());
  for (auto& channel : selected_camera_white) {
    channel = std::clamp(channel / selected_camera_white_max, 0.001, 1.0) * 0.90;
  }
  std::vector<photo_editor::pipeline::ColorPixel> selected_white_pixels = {{
      .red = static_cast<float>(selected_camera_white[0]),
      .green = static_cast<float>(selected_camera_white[1]),
      .blue = static_cast<float>(selected_camera_white[2]),
      .alpha = 0.6F,
  }};
  photo_editor::pipeline::apply_color_adjustments(
      &selected_white_pixels,
      1,
      1,
      dcp_profile_state);
  const auto selected_white_neutral_drift = std::max({
      std::abs(selected_white_pixels[0].red - selected_white_pixels[0].green),
      std::abs(selected_white_pixels[0].green - selected_white_pixels[0].blue),
      std::abs(selected_white_pixels[0].red - selected_white_pixels[0].blue),
  });
  if (selected_white_neutral_drift > 2.0e-4F ||
      std::abs(selected_white_pixels[0].alpha - 0.6F) > 1.0e-7F) {
    std::cerr << "DNG camera white should become a neutral in the selected D50 working RGB space\n";
    return EXIT_FAILURE;
  }

  auto complete_dcp = parsed_dcp;
  complete_dcp.payload_sha256 = std::string(64U, 'c');
  complete_dcp.hue_sat_map1.dimensions = {1U, 2U, 1U};
  complete_dcp.hue_sat_map1.entries = {
      {.hue_shift_degrees = 0.0F, .saturation_scale = 1.0F, .value_scale = 1.0F},
      {.hue_shift_degrees = 18.0F, .saturation_scale = 1.12F, .value_scale = 0.96F},
  };
  complete_dcp.hue_sat_map2 = complete_dcp.hue_sat_map1;
  complete_dcp.look_table.dimensions = {1U, 2U, 1U};
  complete_dcp.look_table.entries = {
      {.hue_shift_degrees = 0.0F, .saturation_scale = 1.0F, .value_scale = 1.0F},
      {.hue_shift_degrees = -11.0F, .saturation_scale = 1.06F, .value_scale = 1.0F},
  };
  complete_dcp.tone_curve = {
      {0.0F, 0.0F},
      {0.5F, 0.32F},
      {1.0F, 1.0F},
  };
  complete_dcp.profile_gain_table_map.points_v = 1U;
  complete_dcp.profile_gain_table_map.points_h = 2U;
  complete_dcp.profile_gain_table_map.spacing_v = 1.0;
  complete_dcp.profile_gain_table_map.spacing_h = 1.0;
  complete_dcp.profile_gain_table_map.origin_v = 0.0;
  complete_dcp.profile_gain_table_map.origin_h = 0.0;
  complete_dcp.profile_gain_table_map.table_points = 1U;
  complete_dcp.profile_gain_table_map.input_weights = {};
  complete_dcp.profile_gain_table_map.gamma = 1.0F;
  complete_dcp.profile_gain_table_map.gains = {0.8F, 2.0F};
  const auto complete_dcp_profile = photo_editor::pipeline::resolve_camera_profile(
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .pixel_encoding = photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb,
          .decoder_backend = "reference-camera-linear-backend",
          .decoded_color_space_name = "Camera RGB",
          .demosaic_applied = true,
          .input_profile_name = dcp_input_profile.profile_name,
          .camera_make = "Canon",
          .camera_model = "EOS R5",
          .as_shot_temperature = 4850.0,
          .as_shot_tint = 8.0,
      },
      dcp_input_profile,
      &complete_dcp);
  if (!complete_dcp_profile.valid || !complete_dcp_profile.has_hue_sat_map ||
      !complete_dcp_profile.has_profile_gain_table_map ||
      !complete_dcp_profile.has_look_table ||
      !complete_dcp_profile.has_profile_tone_curve) {
    std::cerr << "resolved DCP metadata should expose every executable profile stage\n";
    return EXIT_FAILURE;
  }
  auto complete_dcp_state = dcp_profile_state;
  complete_dcp_state.color_pipeline.camera_profile = complete_dcp_profile;
  complete_dcp_state.color_pipeline.camera_dcp_profile_asset.reference.asset_id =
      "dcp:complete-color-pipeline-fixture";
  complete_dcp_state.color_pipeline.camera_dcp_profile_asset.reference.payload_sha256 =
      complete_dcp.payload_sha256;
  complete_dcp_state.color_pipeline.camera_dcp_profile_asset.profile =
      std::make_shared<const photo_editor::pipeline::DcpProfile>(complete_dcp);
  std::vector<photo_editor::pipeline::ColorPixel> spatial_gain_pixels = {
      {.red = 0.35F, .green = 0.22F, .blue = 0.10F, .alpha = 0.41F},
      {.red = 0.35F, .green = 0.22F, .blue = 0.10F, .alpha = 0.41F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &spatial_gain_pixels,
      2,
      1,
      complete_dcp_state);
  if (std::abs(spatial_gain_pixels[0].red - spatial_gain_pixels[1].red) <= 1.0e-3F ||
      std::abs(spatial_gain_pixels[0].alpha - 0.41F) > 1.0e-7F ||
      std::abs(spatial_gain_pixels[1].alpha - 0.41F) > 1.0e-7F) {
    std::cerr << "DNG ProfileGainTableMap2 should execute at pixel-center coordinates and preserve alpha\n";
    return EXIT_FAILURE;
  }
  auto full_frame_mapping_state = complete_dcp_state;
  full_frame_mapping_state.color_pipeline.output_headroom = 16.0;
  std::vector<photo_editor::pipeline::ColorPixel> full_frame_mapping_pixels(
      4U,
      {.red = 0.20F, .green = 0.12F, .blue = 0.05F, .alpha = 0.62F});
  photo_editor::pipeline::apply_color_adjustments(
      &full_frame_mapping_pixels,
      4,
      1,
      full_frame_mapping_state);
  auto cropped_mapping_state = full_frame_mapping_state;
  cropped_mapping_state.input_transform.source_area_x = 0.25;
  cropped_mapping_state.input_transform.source_area_width = 0.50;
  std::vector<photo_editor::pipeline::ColorPixel> cropped_mapping_pixels(
      2U,
      {.red = 0.20F, .green = 0.12F, .blue = 0.05F, .alpha = 0.62F});
  photo_editor::pipeline::apply_color_adjustments(
      &cropped_mapping_pixels,
      2,
      1,
      cropped_mapping_state);
  for (std::size_t index = 0U; index < cropped_mapping_pixels.size(); ++index) {
    const auto& expected = full_frame_mapping_pixels[index + 1U];
    const auto& actual = cropped_mapping_pixels[index];
    if (std::abs(actual.red - expected.red) > 1.0e-6F ||
        std::abs(actual.green - expected.green) > 1.0e-6F ||
        std::abs(actual.blue - expected.blue) > 1.0e-6F ||
        std::abs(actual.alpha - expected.alpha) > 1.0e-7F) {
      std::cerr << "cropped DNG spatial maps should match the corresponding full-frame pixel centers\n";
      return EXIT_FAILURE;
    }
  }
  std::vector<photo_editor::pipeline::ColorPixel> complete_dcp_pixels = {
      {.red = 0.72F, .green = 0.24F, .blue = 0.08F, .alpha = 0.37F},
      {.red = 1.45F, .green = 0.32F, .blue = -0.04F, .alpha = 0.81F},
  };
  auto matrix_only_pixels = complete_dcp_pixels;
  photo_editor::pipeline::apply_color_adjustments(
      &complete_dcp_pixels,
      2,
      1,
      complete_dcp_state);
  auto matrix_only_dcp_state = complete_dcp_state;
  matrix_only_dcp_state.color_pipeline.camera_profile_rendering_model =
      photo_editor::pipeline::CameraProfileRenderingModel::MatrixOnlyV1;
  photo_editor::pipeline::apply_color_adjustments(
      &matrix_only_pixels,
      2,
      1,
      matrix_only_dcp_state);
  const auto complete_profile_stage_delta = std::max({
      std::abs(complete_dcp_pixels[0].red - matrix_only_pixels[0].red),
      std::abs(complete_dcp_pixels[0].green - matrix_only_pixels[0].green),
      std::abs(complete_dcp_pixels[0].blue - matrix_only_pixels[0].blue),
  });
  if (complete_profile_stage_delta <= 1.0e-3F ||
      std::abs(complete_dcp_pixels[0].alpha - 0.37F) > 1.0e-7F ||
      std::abs(complete_dcp_pixels[1].alpha - 0.81F) > 1.0e-7F ||
      !std::isfinite(complete_dcp_pixels[1].red) ||
      !std::isfinite(complete_dcp_pixels[1].green) ||
      !std::isfinite(complete_dcp_pixels[1].blue)) {
    std::cerr << "complete DCP rendering should execute optional stages without damaging alpha or extended values\n";
    return EXIT_FAILURE;
  }
  const auto complete_dcp_lut = photo_editor::pipeline::build_color_lut(complete_dcp_state);
  const auto matrix_only_dcp_lut = photo_editor::pipeline::build_color_lut(matrix_only_dcp_state);
  auto rendering_model_lut_delta = 0.0F;
  for (std::size_t index = 0; index < complete_dcp_lut.table.size(); ++index) {
    rendering_model_lut_delta = std::max({
        rendering_model_lut_delta,
        std::abs(complete_dcp_lut.table[index].red - matrix_only_dcp_lut.table[index].red),
        std::abs(complete_dcp_lut.table[index].green - matrix_only_dcp_lut.table[index].green),
        std::abs(complete_dcp_lut.table[index].blue - matrix_only_dcp_lut.table[index].blue),
    });
  }
  if (!complete_dcp_lut.valid || !matrix_only_dcp_lut.valid ||
      rendering_model_lut_delta <= 1.0e-3F) {
    std::cerr << "DCP rendering-model changes must invalidate the global color LUT cache\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState raw_normalization_state;
  raw_normalization_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  raw_normalization_state.color_pipeline.input_profile = dcp_input_profile;
  raw_normalization_state.color_pipeline.camera_profile = dcp_resolved_profile;
  raw_normalization_state.color_pipeline.input_transform_recipe.has_source_white_balance = true;
  raw_normalization_state.color_pipeline.input_transform_recipe.source_temperature = 4200.0;
  raw_normalization_state.color_pipeline.input_transform_recipe.source_tint = -10.0;
  raw_normalization_state.color_pipeline.input_transform_recipe.baseline_exposure = 0.55;
  raw_normalization_state.color_pipeline.input_transform_recipe.highlight_headroom = 0.72;
  raw_normalization_state.input_transform.raw_input_transform_enabled = true;
  raw_normalization_state.input_transform.raw_white_balance_override_enabled = true;
  raw_normalization_state.input_transform.raw_white_balance_temperature = 6400.0;
  raw_normalization_state.input_transform.raw_white_balance_tint = 16.0;
  raw_normalization_state.input_transform.raw_black_level = 0.08;
  raw_normalization_state.input_transform.raw_white_level = 1.45;
  raw_normalization_state.input_transform.highlight_recovery_enabled = true;
  raw_normalization_state.input_transform.highlight_reconstruction_mode = "Enabled";
  raw_normalization_state.input_transform.chromatic_adaptation_method = "CAT02";
  raw_normalization_state.input_transform.camera_matching_look = "Landscape";
  raw_normalization_state.input_transform.calibration_hue = -18.0;
  raw_normalization_state.input_transform.calibration_saturation = 24.0;
  raw_normalization_state.input_transform.red_primary_hue = 15.0;
  raw_normalization_state.input_transform.red_primary_saturation = 20.0;
  raw_normalization_state.input_transform.green_primary_hue = -12.0;
  raw_normalization_state.input_transform.green_primary_saturation = -18.0;
  raw_normalization_state.input_transform.blue_primary_hue = 10.0;
  raw_normalization_state.input_transform.blue_primary_saturation = 14.0;
  std::vector<photo_editor::pipeline::ColorPixel> raw_normalization_pixels = {
      {.red = 0.16F, .green = 0.38F, .blue = 0.92F, .alpha = 1.0F},
      {.red = 1.70F, .green = 0.26F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &raw_normalization_pixels,
      2,
      1,
      raw_normalization_state);

  auto disabled_raw_normalization_state = raw_normalization_state;
  disabled_raw_normalization_state.input_transform.raw_input_transform_enabled = false;
  std::vector<photo_editor::pipeline::ColorPixel> disabled_raw_normalization_pixels = {
      {.red = 0.16F, .green = 0.38F, .blue = 0.92F, .alpha = 1.0F},
      {.red = 1.70F, .green = 0.26F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &disabled_raw_normalization_pixels,
      2,
      1,
      disabled_raw_normalization_state);

  photo_editor::pipeline::ColorAdjustmentState non_raw_reference_state;
  non_raw_reference_state.color_pipeline.working_profile =
      raw_normalization_state.color_pipeline.working_profile;
  non_raw_reference_state.input_transform.raw_input_transform_enabled = false;
  non_raw_reference_state.input_transform.chromatic_adaptation_method = "CAT02";
  std::vector<photo_editor::pipeline::ColorPixel> non_raw_reference_pixels = {
      {.red = 0.16F, .green = 0.38F, .blue = 0.92F, .alpha = 1.0F},
      {.red = 1.70F, .green = 0.26F, .blue = 0.12F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &non_raw_reference_pixels,
      2,
      1,
      non_raw_reference_state);
  auto max_pixel_delta = [](const std::vector<photo_editor::pipeline::ColorPixel>& left,
                            const std::vector<photo_editor::pipeline::ColorPixel>& right) {
    double delta = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
      delta = std::max(delta, static_cast<double>(std::abs(left[index].red - right[index].red)));
      delta = std::max(delta, static_cast<double>(std::abs(left[index].green - right[index].green)));
      delta = std::max(delta, static_cast<double>(std::abs(left[index].blue - right[index].blue)));
    }
    return delta;
  };
  if (max_pixel_delta(raw_normalization_pixels, disabled_raw_normalization_pixels) <= 0.05) {
    std::cerr << "camera normalization fields should affect RAW-like input when enabled\n";
    return EXIT_FAILURE;
  }
  if (max_pixel_delta(disabled_raw_normalization_pixels, non_raw_reference_pixels) > 1.0e-5) {
    std::cerr << "camera normalization fields should be inert when the input transform is disabled\n";
    return EXIT_FAILURE;
  }

  auto tungsten_profile_state = dcp_profile_state;
  tungsten_profile_state.color_pipeline.input_transform_recipe.has_source_white_balance = true;
  tungsten_profile_state.color_pipeline.input_transform_recipe.source_temperature = 2850.0;
  auto daylight_profile_state = dcp_profile_state;
  daylight_profile_state.color_pipeline.input_transform_recipe.has_source_white_balance = true;
  daylight_profile_state.color_pipeline.input_transform_recipe.source_temperature = 6500.0;
  std::vector<photo_editor::pipeline::ColorPixel> tungsten_profile_pixels = {
      {.red = 0.42F, .green = 0.30F, .blue = 0.18F, .alpha = 1.0F},
  };
  std::vector<photo_editor::pipeline::ColorPixel> daylight_profile_pixels = tungsten_profile_pixels;
  photo_editor::pipeline::apply_color_adjustments(
      &tungsten_profile_pixels,
      1,
      1,
      tungsten_profile_state);
  photo_editor::pipeline::apply_color_adjustments(
      &daylight_profile_pixels,
      1,
      1,
      daylight_profile_state);
  const auto dual_illuminant_delta = std::max({
      std::abs(tungsten_profile_pixels[0].red - daylight_profile_pixels[0].red),
      std::abs(tungsten_profile_pixels[0].green - daylight_profile_pixels[0].green),
      std::abs(tungsten_profile_pixels[0].blue - daylight_profile_pixels[0].blue),
  });
  if (dual_illuminant_delta <= 1.0e-4F) {
    std::cerr << "dual-illuminant DCP profiles should interpolate by RAW source temperature\n";
    return EXIT_FAILURE;
  }

  auto replacement_dcp = parsed_dcp;
  replacement_dcp.payload_sha256 = std::string(64U, 'b');
  replacement_dcp.forward_matrix1 = std::array<double, 9>{
      0.93, 0.08, 0.04,
      0.04, 0.91, 0.05,
      -0.02, 0.12, 0.99,
  };
  replacement_dcp.forward_matrix2 = std::array<double, 9>{
      0.90, 0.11, 0.04,
      0.06, 0.87, 0.07,
      -0.03, 0.15, 0.97,
  };
  const auto replacement_profile = photo_editor::pipeline::resolve_camera_profile(
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .pixel_encoding = photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb,
          .decoder_backend = "reference-camera-linear-backend",
          .decoded_color_space_name = "Camera RGB",
          .demosaic_applied = true,
          .input_profile_name = dcp_input_profile.profile_name,
          .camera_make = "Canon",
          .camera_model = "EOS R5",
          .as_shot_temperature = 4850.0,
          .as_shot_tint = 8.0,
      },
      dcp_input_profile,
      &replacement_dcp);
  if (!replacement_profile.valid) {
    std::cerr << "replacement DCP should resolve for LUT cache identity coverage\n";
    return EXIT_FAILURE;
  }
  auto replacement_profile_state = dcp_profile_state;
  replacement_profile_state.color_pipeline.camera_profile = replacement_profile;
  replacement_profile_state.color_pipeline.camera_dcp_profile_asset.reference.asset_id =
      "dcp:color-pipeline-fixture-replacement";
  replacement_profile_state.color_pipeline.camera_dcp_profile_asset.reference.payload_sha256 =
      replacement_dcp.payload_sha256;
  replacement_profile_state.color_pipeline.camera_dcp_profile_asset.profile =
      std::make_shared<const photo_editor::pipeline::DcpProfile>(replacement_dcp);

  const auto original_dcp_lut = photo_editor::pipeline::build_color_lut(dcp_profile_state);
  const auto replacement_dcp_lut =
      photo_editor::pipeline::build_color_lut(replacement_profile_state);
  auto dcp_lut_delta = 0.0F;
  for (std::size_t index = 0; index < original_dcp_lut.table.size(); ++index) {
    dcp_lut_delta = std::max({
        dcp_lut_delta,
        std::abs(original_dcp_lut.table[index].red - replacement_dcp_lut.table[index].red),
        std::abs(original_dcp_lut.table[index].green - replacement_dcp_lut.table[index].green),
        std::abs(original_dcp_lut.table[index].blue - replacement_dcp_lut.table[index].blue),
    });
  }
  if (!original_dcp_lut.valid || !replacement_dcp_lut.valid || dcp_lut_delta <= 1.0e-4F) {
    std::cerr << "DCP payload and matrix identity must invalidate the global color LUT cache\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState perceptual_state;
  perceptual_state.color_pipeline.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::
          LegacyOklchV1;
  perceptual_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  perceptual_state.color_pipeline.render_intent = photo_editor::pipeline::RenderIntent::Perceptual;
  perceptual_state.saturation = 100.0;
  perceptual_state.vibrance = 100.0;
  std::vector<photo_editor::pipeline::ColorPixel> perceptual_pixels = {
      {.red = 1.0F, .green = 0.15F, .blue = 0.02F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&perceptual_pixels, 1, 1, perceptual_state);

  auto relative_state = perceptual_state;
  relative_state.color_pipeline.render_intent = photo_editor::pipeline::RenderIntent::RelativeColorimetric;
  std::vector<photo_editor::pipeline::ColorPixel> relative_pixels = {
      {.red = 1.0F, .green = 0.15F, .blue = 0.02F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&relative_pixels, 1, 1, relative_state);
  const auto intent_delta = std::max({
      std::abs(perceptual_pixels[0].red - relative_pixels[0].red),
      std::abs(perceptual_pixels[0].green - relative_pixels[0].green),
      std::abs(perceptual_pixels[0].blue - relative_pixels[0].blue),
  });
  if (intent_delta <= 1.0e-5F) {
    std::cerr << "render intent should affect gamut fitting for saturated wide-gamut edits\n";
    return EXIT_FAILURE;
  }
  const auto perceptual_intent_delta = photo_editor::pipeline::perceptual_delta(
      perceptual_pixels[0],
      relative_pixels[0],
      perceptual_state.color_pipeline.working_profile.color_space);
  if (perceptual_intent_delta <= 1.0e-4) {
    std::cerr << "render intent differences should be visible in perceptual delta space\n";
    return EXIT_FAILURE;
  }
  auto clip_policy_state = perceptual_state;
  clip_policy_state.color_pipeline.output_gamut_mapping_policy =
      photo_editor::pipeline::GamutMappingPolicy::Clip;
  std::vector<photo_editor::pipeline::ColorPixel> clip_policy_pixels = {
      {.red = 1.0F, .green = 0.15F, .blue = 0.02F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&clip_policy_pixels, 1, 1, clip_policy_state);
  const auto clip_policy_delta = photo_editor::pipeline::perceptual_delta(
      perceptual_pixels[0],
      clip_policy_pixels[0],
      perceptual_state.color_pipeline.working_profile.color_space);
  if (clip_policy_delta <= 1.0e-4) {
    std::cerr << "output gamut mapping policy should affect saturated wide-gamut edits\n";
    return EXIT_FAILURE;
  }
  auto disabled_gamut_state = perceptual_state;
  disabled_gamut_state.color_pipeline.output_gamut_mapping_enabled = false;
  std::vector<photo_editor::pipeline::ColorPixel> disabled_gamut_pixels = {
      {.red = 1.0F, .green = 0.15F, .blue = 0.02F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &disabled_gamut_pixels,
      1,
      1,
      disabled_gamut_state);
  if (photo_editor::pipeline::perceptual_delta(
          clip_policy_pixels[0],
          disabled_gamut_pixels[0],
          perceptual_state.color_pipeline.working_profile.color_space) > 1.0e-6) {
    std::cerr << "disabled output gamut mapping should match explicit clipping\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::perceptual_delta(
          perceptual_pixels[0],
          perceptual_pixels[0],
          perceptual_state.color_pipeline.working_profile.color_space) > 1.0e-9) {
    std::cerr << "perceptual delta should be zero for identical colors\n";
    return EXIT_FAILURE;
  }

  auto output_transform_config = photo_editor::pipeline::default_color_pipeline_config();
  output_transform_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::LegacyOklchV1;
  output_transform_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear Display P3");
  output_transform_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  output_transform_config.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("Display P3");
  const photo_editor::pipeline::ColorPixel p3_green{
      .red = 0.0F,
      .green = 1.0F,
      .blue = 0.0F,
      .alpha = 1.0F,
  };
  const auto srgb_output_transform = photo_editor::pipeline::apply_output_transform(
      p3_green,
      output_transform_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  const auto display_p3_output_transform = photo_editor::pipeline::apply_output_transform(
      p3_green,
      output_transform_config,
      photo_editor::pipeline::GamutWarningTarget::ExportOutput);
  if (srgb_output_transform.output_profile_name != "sRGB" ||
      !srgb_output_transform.source_out_of_gamut ||
      srgb_output_transform.max_channel_excursion <= 1.0e-4 ||
      srgb_output_transform.pixel.red > 1.0e-4F ||
      srgb_output_transform.pixel.green < 0.85F ||
      srgb_output_transform.pixel.blue < 0.02F ||
      srgb_output_transform.pixel.blue > 0.12F ||
      display_p3_output_transform.output_profile_name != "Display P3" ||
      display_p3_output_transform.source_out_of_gamut ||
      std::abs(display_p3_output_transform.pixel.green - 1.0F) > 1.0e-5F) {
    std::cerr << "output transform should convert working pixels into explicit sRGB and Display P3 targets"
              << " srgb=" << srgb_output_transform.pixel.red << ","
              << srgb_output_transform.pixel.green << ","
              << srgb_output_transform.pixel.blue
              << " srgbOog=" << srgb_output_transform.source_out_of_gamut
              << " p3=" << display_p3_output_transform.pixel.red << ","
              << display_p3_output_transform.pixel.green << ","
              << display_p3_output_transform.pixel.blue
              << " p3Oog=" << display_p3_output_transform.source_out_of_gamut << '\n';
    return EXIT_FAILURE;
  }

  auto p3_green_clip_config = output_transform_config;
  p3_green_clip_config.render_intent =
      photo_editor::pipeline::RenderIntent::RelativeColorimetric;
  const auto clipped_p3_green = photo_editor::pipeline::apply_output_transform(
      p3_green,
      p3_green_clip_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (std::abs(srgb_output_transform.pixel.green - clipped_p3_green.pixel.green) < 0.02F ||
      std::abs(srgb_output_transform.pixel.blue - clipped_p3_green.pixel.blue) < 0.02F) {
    std::cerr << "perceptual P3-to-sRGB mapping should differ from channel clipping\n";
    return EXIT_FAILURE;
  }
  std::vector<photo_editor::pipeline::ColorPixel> display_ready_working_pixels = {p3_green};
  photo_editor::pipeline::apply_output_gamut_mapping_in_working_space(
      display_ready_working_pixels,
      output_transform_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  const auto display_ready_output = photo_editor::pipeline::apply_output_transform(
      display_ready_working_pixels.front(),
      p3_green_clip_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (display_ready_output.source_out_of_gamut ||
      photo_editor::pipeline::perceptual_delta(
          display_ready_output.pixel,
          srgb_output_transform.pixel,
          photo_editor::pipeline::make_linear_srgb_color_space()) > 1.0e-5) {
    std::cerr << "display handoff should preserve perceptual mapping while returning pixels to working space\n";
    return EXIT_FAILURE;
  }

  struct ProfileRoundTripCase final {
    const char* working_profile;
    const char* output_profile;
  };
  for (const auto& profile_case : std::vector<ProfileRoundTripCase>{
           {"Linear sRGB", "sRGB"},
           {"Linear Display P3", "Display P3"},
           {"Linear Rec.2020", "Rec.2020"},
           {"Linear ProPhoto RGB", "ProPhoto RGB"},
       }) {
    auto identity_transform_config = photo_editor::pipeline::default_color_pipeline_config();
    identity_transform_config.photographic_rendering_model =
        photo_editor::pipeline::PhotographicRenderingModel::LegacyOklchV1;
    identity_transform_config.working_profile =
        photo_editor::pipeline::working_color_profile_from_name(profile_case.working_profile);
    identity_transform_config.export_output_profile =
        photo_editor::pipeline::output_color_profile_from_name(profile_case.output_profile);
    identity_transform_config.render_intent =
        photo_editor::pipeline::RenderIntent::RelativeColorimetric;
    identity_transform_config.output_gamut_mapping_enabled = false;
    identity_transform_config.black_point_compensation_enabled = false;
    identity_transform_config.output_headroom = 1.0;
    const photo_editor::pipeline::ColorPixel profile_sample = {
        .red = 0.18F,
        .green = 0.42F,
        .blue = 0.74F,
        .alpha = 0.65F,
    };
    const auto round_trip = photo_editor::pipeline::apply_output_transform(
        profile_sample,
        identity_transform_config,
        photo_editor::pipeline::GamutWarningTarget::ExportOutput);
    if (round_trip.output_profile_name != profile_case.output_profile ||
        round_trip.source_out_of_gamut ||
        std::abs(round_trip.pixel.red - profile_sample.red) > 1.0e-5F ||
        std::abs(round_trip.pixel.green - profile_sample.green) > 1.0e-5F ||
        std::abs(round_trip.pixel.blue - profile_sample.blue) > 1.0e-5F ||
        std::abs(round_trip.pixel.alpha - profile_sample.alpha) > 1.0e-6F) {
      std::cerr << "same-profile output transform should round-trip within 1e-5 for "
                << profile_case.working_profile << " -> "
                << profile_case.output_profile << '\n';
      return EXIT_FAILURE;
    }
  }

  for (const auto* profile_name : {"Rec.2020", "ProPhoto RGB"}) {
    auto profile_transform_config = output_transform_config;
    profile_transform_config.export_output_profile =
        photo_editor::pipeline::output_color_profile_from_name(profile_name);
    const auto profile_output = photo_editor::pipeline::apply_output_transform(
        p3_green,
        profile_transform_config,
        photo_editor::pipeline::GamutWarningTarget::ExportOutput);
    if (profile_output.output_profile_name != profile_name ||
        !std::isfinite(profile_output.pixel.red) ||
        !std::isfinite(profile_output.pixel.green) ||
        !std::isfinite(profile_output.pixel.blue) ||
        profile_output.pixel.red < -1.0e-6F ||
        profile_output.pixel.green < -1.0e-6F ||
        profile_output.pixel.blue < -1.0e-6F ||
        profile_output.pixel.red > 1.0001F ||
        profile_output.pixel.green > 1.0001F ||
        profile_output.pixel.blue > 1.0001F) {
      std::cerr << "output transform should support bounded Rec.2020 and ProPhoto RGB targets\n";
      return EXIT_FAILURE;
    }
  }

  auto output_intent_config = photo_editor::pipeline::default_color_pipeline_config();
  output_intent_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::LegacyOklchV1;
  output_intent_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  output_intent_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  output_intent_config.output_headroom = 1.0;
  output_intent_config.render_intent = photo_editor::pipeline::RenderIntent::Perceptual;
  const photo_editor::pipeline::ColorPixel overrange_srgb{
      .red = 1.40F,
      .green = 0.70F,
      .blue = 0.35F,
      .alpha = 1.0F,
  };
  const auto perceptual_output_transform = photo_editor::pipeline::apply_output_transform(
      overrange_srgb,
      output_intent_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  output_intent_config.render_intent =
      photo_editor::pipeline::RenderIntent::RelativeColorimetric;
  const auto relative_output_transform = photo_editor::pipeline::apply_output_transform(
      overrange_srgb,
      output_intent_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (perceptual_output_transform.pixel.red > 1.0001F ||
      relative_output_transform.pixel.red > 1.0001F ||
      std::abs(perceptual_output_transform.pixel.green - relative_output_transform.pixel.green) <=
          0.05F ||
      perceptual_output_transform.pixel.green >= relative_output_transform.pixel.green) {
    std::cerr << "output transform should distinguish perceptual compression from relative colorimetric clipping\n";
    return EXIT_FAILURE;
  }

  auto output_bpc_config = photo_editor::pipeline::default_color_pipeline_config();
  output_bpc_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::LegacyOklchV1;
  output_bpc_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  output_bpc_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  output_bpc_config.black_point_compensation_enabled = true;
  output_bpc_config.source_black_point = 0.0;
  output_bpc_config.output_black_point = 0.04;
  const auto compensated_output_transform = photo_editor::pipeline::apply_output_transform(
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.5F},
      output_bpc_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (compensated_output_transform.pixel.red < 0.039F ||
      compensated_output_transform.pixel.green < 0.039F ||
      compensated_output_transform.pixel.blue < 0.039F ||
      std::abs(compensated_output_transform.pixel.alpha - 0.5F) > 1.0e-6F) {
    std::cerr << "output transform should apply black-point compensation in the final target stage\n";
    return EXIT_FAILURE;
  }

  const auto hue_for_rgb = [](
      const photo_editor::pipeline::ColorPixel& pixel) {
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
  };
  const auto hue_distance = [](double left, double right) {
    auto distance = std::abs(left - right);
    return std::min(distance, 1.0 - distance);
  };
  const auto normalized_chroma = [](
      const photo_editor::pipeline::ColorPixel& pixel) {
    const auto red = std::max(0.0, static_cast<double>(pixel.red));
    const auto green = std::max(0.0, static_cast<double>(pixel.green));
    const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
    const auto maximum = std::max({red, green, blue});
    const auto minimum = std::min({red, green, blue});
    return maximum <= 1.0e-9 ? 0.0 : (maximum - minimum) / maximum;
  };
  auto compression_config = photo_editor::pipeline::default_color_pipeline_config();
  compression_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::LegacyOklchV1;
  compression_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  compression_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  compression_config.output_headroom = 1.0;
  compression_config.render_intent = photo_editor::pipeline::RenderIntent::Perceptual;
  const std::vector<photo_editor::pipeline::ColorPixel> saturated_patches = {
      {.red = 1.50F, .green = 0.42F, .blue = 0.12F, .alpha = 1.0F},
      {.red = 0.20F, .green = 1.42F, .blue = 0.52F, .alpha = 1.0F},
      {.red = 0.16F, .green = 0.48F, .blue = 1.36F, .alpha = 1.0F},
      {.red = 1.35F, .green = 0.24F, .blue = 1.48F, .alpha = 1.0F},
  };
  for (const auto& patch : saturated_patches) {
    const auto source_hue = hue_for_rgb(patch);
    const auto source_chroma = normalized_chroma(patch);
    const auto compressed = photo_editor::pipeline::apply_output_transform(
        patch,
        compression_config,
        photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    auto clipped_config = compression_config;
    clipped_config.render_intent =
        photo_editor::pipeline::RenderIntent::RelativeColorimetric;
    const auto clipped = photo_editor::pipeline::apply_output_transform(
        patch,
        clipped_config,
        photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    if (!compressed.source_out_of_gamut ||
        hue_distance(source_hue, hue_for_rgb(compressed.pixel)) > 0.015 ||
        normalized_chroma(compressed.pixel) + 0.02 < source_chroma ||
        normalized_chroma(compressed.pixel) < normalized_chroma(clipped.pixel)) {
      std::cerr << "perceptual output compression should preserve saturated-patch hue and chroma better than clipping\n";
      return EXIT_FAILURE;
    }
  }

  const auto small_delta = photo_editor::pipeline::perceptual_delta(
      {.red = 0.50F, .green = 0.50F, .blue = 0.50F, .alpha = 1.0F},
      {.red = 0.505F, .green = 0.50F, .blue = 0.50F, .alpha = 1.0F},
      photo_editor::pipeline::make_linear_srgb_color_space());
  const auto large_delta = photo_editor::pipeline::perceptual_delta(
      {.red = 0.50F, .green = 0.50F, .blue = 0.50F, .alpha = 1.0F},
      {.red = 0.80F, .green = 0.25F, .blue = 0.10F, .alpha = 1.0F},
      photo_editor::pipeline::make_linear_srgb_color_space());
  if (!(small_delta > 0.0 && large_delta > small_delta * 8.0)) {
    std::cerr << "perceptual delta should scale with visible color differences\n";
    return EXIT_FAILURE;
  }
  const auto signed_component_delta = photo_editor::pipeline::perceptual_delta(
      {.red = -0.05F, .green = 0.50F, .blue = 0.20F, .alpha = 1.0F},
      {.red = 0.0F, .green = 0.50F, .blue = 0.20F, .alpha = 1.0F},
      photo_editor::pipeline::make_linear_srgb_color_space());
  if (!std::isfinite(signed_component_delta) || signed_component_delta <= 0.005) {
    std::cerr << "perceptual math should preserve signed wide-gamut components until gamut mapping\n";
    return EXIT_FAILURE;
  }

  auto gamut_config = photo_editor::pipeline::default_color_pipeline_config();
  gamut_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::LegacyOklchV1;
  gamut_config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear Display P3");
  gamut_config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  gamut_config.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("Display P3");
  const std::vector<photo_editor::pipeline::ColorPixel> display_p3_pixels = {
      {.red = 0.0F, .green = 1.0F, .blue = 0.0F, .alpha = 1.0F},
      {.red = 0.4F, .green = 0.5F, .blue = 0.2F, .alpha = 0.0F},
  };
  const auto display_warning = photo_editor::pipeline::analyze_gamut_warnings(
      display_p3_pixels,
      gamut_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (!display_warning.has_warnings() || display_warning.tested_pixels != 1 ||
      display_warning.out_of_gamut_pixels != 1 ||
      display_warning.out_of_gamut_fraction < 0.99 ||
      display_warning.max_channel_excursion <= 1.0e-4) {
    std::cerr << "display gamut warning analysis should flag Display P3 colors outside sRGB\n";
    return EXIT_FAILURE;
  }
  auto transform_warning_count = std::size_t{0};
  auto transform_max_excursion = 0.0;
  auto transform_tested_pixels = std::size_t{0};
  for (const auto& pixel : display_p3_pixels) {
    if (pixel.alpha <= 1.0e-6F) {
      continue;
    }
    ++transform_tested_pixels;
    const auto output = photo_editor::pipeline::apply_output_transform(
        pixel,
        gamut_config,
        photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
    if (output.source_out_of_gamut) {
      ++transform_warning_count;
      transform_max_excursion =
          std::max(transform_max_excursion, output.max_channel_excursion);
    }
  }
  if (display_warning.tested_pixels != transform_tested_pixels ||
      display_warning.out_of_gamut_pixels != transform_warning_count ||
      std::abs(display_warning.max_channel_excursion - transform_max_excursion) > 1.0e-9) {
    std::cerr << "gamut diagnostics should match final output-transform decisions pixel for pixel\n";
    return EXIT_FAILURE;
  }
  const auto export_warning = photo_editor::pipeline::analyze_gamut_warnings(
      display_p3_pixels,
      gamut_config,
      photo_editor::pipeline::GamutWarningTarget::ExportOutput);
  if (export_warning.has_warnings()) {
    std::cerr << "export gamut warning analysis should not flag pixels inside the export profile\n";
    return EXIT_FAILURE;
  }

  const auto custom_gamut_profile_payload =
      photo_editor::pipeline::builtin_rgb_icc_profile_payload(
          photo_editor::pipeline::make_srgb_color_space());
  const auto custom_gamut_profile_hash =
      photo_editor::pipeline::color_payload_sha256(custom_gamut_profile_payload);
  photo_editor::pipeline::IccProfileAssetRegistry custom_gamut_registry;
  const auto custom_gamut_profile = custom_gamut_registry.register_payload(
      photo_editor::pipeline::IccProfileAssetReference{
          .asset_id = "icc:gamut-diagnostics-srgb",
          .payload_sha256 = custom_gamut_profile_hash,
          .display_name = "Gamut Diagnostics sRGB",
          .source_uri = {},
          .source_kind = "test-generated",
          .byte_size = custom_gamut_profile_payload.size(),
      },
      custom_gamut_profile_payload,
      "memory:gamut-diagnostics-srgb");
  auto custom_gamut_config = gamut_config;
  custom_gamut_config.preview_icc_profile_asset = custom_gamut_profile;
  const auto custom_gamut_warning = photo_editor::pipeline::analyze_gamut_warnings(
      display_p3_pixels,
      custom_gamut_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (!custom_gamut_warning.transform_ready ||
      custom_gamut_warning.processor != "Little CMS" ||
      custom_gamut_warning.profile_asset_id != "icc:gamut-diagnostics-srgb" ||
      custom_gamut_warning.profile_payload_sha256 != custom_gamut_profile_hash ||
      custom_gamut_warning.transform_cache_key.empty() ||
      !custom_gamut_warning.failure_reason.empty() ||
      custom_gamut_warning.tested_pixels != 1) {
    std::cerr << "custom-profile gamut diagnostics should use the verified ICC processor\n";
    return EXIT_FAILURE;
  }
  auto aces_custom_gamut_config = custom_gamut_config;
  aces_custom_gamut_config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1;
  const auto aces_custom_gamut_warning =
      photo_editor::pipeline::analyze_gamut_warnings(
          display_p3_pixels,
          aces_custom_gamut_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (!aces_custom_gamut_warning.transform_ready ||
      aces_custom_gamut_warning.processor !=
          "aces2-ocio-v1 + Little CMS" ||
      aces_custom_gamut_warning.profile_asset_id !=
          "icc:gamut-diagnostics-srgb" ||
      aces_custom_gamut_warning.tested_pixels != 1 ||
      aces_custom_gamut_warning.transform_cache_key.find("->") ==
          std::string::npos ||
      aces_custom_gamut_warning.transform_cache_key.find(
          "aces2-rec2020-interchange") == std::string::npos) {
    std::cerr << "ACES 2 custom-profile gamut diagnostics should execute the same composed processor as final output\n";
    return EXIT_FAILURE;
  }
  auto unavailable_gamut_config = gamut_config;
  unavailable_gamut_config.preview_icc_profile_asset =
      photo_editor::pipeline::ResolvedIccProfileAsset{
          .reference = photo_editor::pipeline::IccProfileAssetReference{
              .asset_id = "icc:missing-gamut-profile",
              .payload_sha256 = std::string(64, '0'),
              .display_name = "Missing Gamut Profile",
              .source_uri = {},
              .source_kind = "test",
              .byte_size = 128,
          },
          .status = photo_editor::pipeline::IccProfileAssetStatus::AssetNotFound,
          .payload = {},
          .resolved_uri = {},
          .failure_reason = "icc_profile_asset_location_missing",
      };
  const auto unavailable_gamut_warning = photo_editor::pipeline::analyze_gamut_warnings(
      display_p3_pixels,
      unavailable_gamut_config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  if (unavailable_gamut_warning.transform_ready ||
      unavailable_gamut_warning.failure_reason != "icc_profile_asset_location_missing" ||
      unavailable_gamut_warning.tested_pixels != 0) {
    std::cerr << "unavailable custom-profile gamut diagnostics must not fall back to built-in sRGB\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState sdr_tone_map_state;
  sdr_tone_map_state.color_pipeline.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::
          LegacyOklchV1;
  sdr_tone_map_state.color_pipeline.output_headroom = 1.0;
  std::vector<photo_editor::pipeline::ColorPixel> sdr_hdr_pixels = {
      {.red = 3.5F, .green = 3.5F, .blue = 3.5F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&sdr_hdr_pixels, 1, 1, sdr_tone_map_state);
  if (sdr_hdr_pixels[0].red > 1.0001F || sdr_hdr_pixels[0].green > 1.0001F ||
      sdr_hdr_pixels[0].blue > 1.0001F || sdr_hdr_pixels[0].red < 0.90F) {
    std::cerr << "SDR tone mapping should compress over-range HDR luminance into SDR headroom\n";
    return EXIT_FAILURE;
  }

  auto hdr_tone_map_state = sdr_tone_map_state;
  hdr_tone_map_state.color_pipeline.output_headroom = 2.0;
  std::vector<photo_editor::pipeline::ColorPixel> hdr_pixels = {
      {.red = 3.5F, .green = 3.5F, .blue = 3.5F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&hdr_pixels, 1, 1, hdr_tone_map_state);
  if (hdr_pixels[0].red <= sdr_hdr_pixels[0].red + 0.25F) {
    std::cerr << "HDR tone mapping should preserve extra output headroom\n";
    return EXIT_FAILURE;
  }
  if (hdr_pixels[0].red > 2.01F) {
    std::cerr << "HDR tone mapping should compress highlights into configured headroom\n";
    return EXIT_FAILURE;
  }
  if (std::abs(hdr_pixels[0].red - hdr_pixels[0].green) > 1.0e-6F ||
      std::abs(hdr_pixels[0].green - hdr_pixels[0].blue) > 1.0e-6F) {
    std::cerr << "HDR tone mapping should preserve neutral channel balance\n";
    return EXIT_FAILURE;
  }

  const auto hdr_lut = photo_editor::pipeline::build_color_lut(hdr_tone_map_state);
  if (!hdr_lut.valid || std::abs(hdr_lut.shoulder_headroom - 2.0) > 1.0e-9 ||
      hdr_lut.shoulder_threshold > hdr_lut.shoulder_headroom) {
    std::cerr << "HDR LUT should carry the configured output headroom into tone mapping\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState optical_headroom_state;
  optical_headroom_state.color_pipeline.output_headroom = 2.0;
  optical_headroom_state.lens_vignette = 65.0;
  std::vector<photo_editor::pipeline::ColorPixel> optical_headroom_pixels(
      9,
      {.red = 1.18F, .green = 1.18F, .blue = 1.18F, .alpha = 1.0F});
  photo_editor::pipeline::apply_color_adjustments(
      &optical_headroom_pixels, 3, 3, optical_headroom_state);
  if (optical_headroom_pixels[0].red <= 1.0001F ||
      optical_headroom_pixels[0].green <= 1.0001F ||
      optical_headroom_pixels[0].blue <= 1.0001F ||
      optical_headroom_pixels[0].red > 2.01F) {
    std::cerr << "optical corrections should not clamp scene-referred HDR pixels before output transform\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState mixer_headroom_state;
  mixer_headroom_state.color_pipeline.output_headroom = 2.0;
  mixer_headroom_state.rgb_channel_mixer.red_from_green = 18.0;
  std::vector<photo_editor::pipeline::ColorPixel> mixer_headroom_pixels = {
      {.red = 1.12F, .green = 0.72F, .blue = 0.38F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &mixer_headroom_pixels, 1, 1, mixer_headroom_state);
  if (mixer_headroom_pixels[0].red <= 1.12F ||
      mixer_headroom_pixels[0].red > 2.01F ||
      mixer_headroom_pixels[0].green < 0.70F) {
    std::cerr << "RGB color mixer should preserve over-range scene values without upper clipping\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState grading_headroom_state;
  grading_headroom_state.color_pipeline.output_headroom = 2.0;
  grading_headroom_state.grading_highlights_saturation = 22.0;
  grading_headroom_state.grading_highlights_luminance = 12.0;
  std::vector<photo_editor::pipeline::ColorPixel> grading_headroom_pixels = {
      {.red = 1.22F, .green = 1.10F, .blue = 0.92F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &grading_headroom_pixels, 1, 1, grading_headroom_state);
  if (grading_headroom_pixels[0].red <= 1.0001F ||
      std::max({grading_headroom_pixels[0].red,
                grading_headroom_pixels[0].green,
                grading_headroom_pixels[0].blue}) > 2.01F) {
    std::cerr << "color grading should preserve extended headroom without forcing SDR clipping\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState layer_headroom_state;
  layer_headroom_state.color_pipeline.output_headroom = 2.0;
  photo_editor::pipeline::PhotoLayer headroom_layer;
  headroom_layer.kind = "adjustment";
  headroom_layer.blend_mode = "normal";
  headroom_layer.exposure = 1.0;
  layer_headroom_state.photo_layers.push_back(headroom_layer);
  std::vector<photo_editor::pipeline::ColorPixel> layer_headroom_pixels = {
      {.red = 0.72F, .green = 0.68F, .blue = 0.62F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &layer_headroom_pixels, 1, 1, layer_headroom_state);
  if (layer_headroom_pixels[0].red <= 1.0001F ||
      layer_headroom_pixels[0].red > 2.01F ||
      layer_headroom_pixels[0].green <= 1.0001F) {
    std::cerr << "normal adjustment layers should preserve over-range scene-referred pixels\n";
    return EXIT_FAILURE;
  }

  auto render_fill_blend = [](
      const photo_editor::pipeline::ColorPixel& base,
      const photo_editor::pipeline::ColorPixel& fill,
      const std::string& blend_mode) {
    photo_editor::pipeline::ColorAdjustmentState state;
    state.color_pipeline.photographic_rendering_model =
        photo_editor::pipeline::PhotographicRenderingModel::
            LegacyOklchV1;
    state.color_pipeline.output_headroom = 2.0;
    photo_editor::pipeline::PhotoLayer layer;
    layer.kind = "fill";
    layer.blend_mode = blend_mode;
    layer.fill_color = fill;
    state.photo_layers.push_back(layer);
    std::vector<photo_editor::pipeline::ColorPixel> pixels = {base};
    photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
    return pixels[0];
  };

  const auto multiply_blend = render_fill_blend(
      {.red = 1.40F, .green = 1.40F, .blue = 1.40F, .alpha = 1.0F},
      {.red = 0.80F, .green = 0.80F, .blue = 0.80F, .alpha = 1.0F},
      "multiply");
  if (multiply_blend.red <= 1.0001F ||
      std::abs(multiply_blend.red - multiply_blend.green) > 1.0e-5F ||
      std::abs(multiply_blend.green - multiply_blend.blue) > 1.0e-5F) {
    std::cerr << "multiply layer blend should operate in linear scene values and preserve HDR neutral balance\n";
    return EXIT_FAILURE;
  }

  const auto overlay_blend = render_fill_blend(
      {.red = 1.40F, .green = 1.18F, .blue = 0.82F, .alpha = 1.0F},
      {.red = 1.50F, .green = 0.96F, .blue = 0.60F, .alpha = 1.0F},
      "overlay");
  if (overlay_blend.red <= 1.0001F ||
      overlay_blend.red > 2.01F ||
      overlay_blend.red <= overlay_blend.green) {
    std::cerr << "overlay layer blend should normalize against output headroom instead of SDR clipping\n";
    return EXIT_FAILURE;
  }

  const auto color_blend = render_fill_blend(
      {.red = 1.30F, .green = 1.18F, .blue = 0.96F, .alpha = 1.0F},
      {.red = 0.10F, .green = 0.30F, .blue = 1.55F, .alpha = 1.0F},
      "color");
  if (std::max({color_blend.red, color_blend.green, color_blend.blue}) <= 1.0001F ||
      color_blend.blue <= color_blend.red ||
      color_blend.blue > 2.01F) {
    std::cerr << "color layer blend should use OKLab hue/chroma while preserving extended lightness\n";
    return EXIT_FAILURE;
  }

  const auto parity_source = photo_editor::pipeline::ColorPixel{
      .red = 0.34F,
      .green = 0.47F,
      .blue = 0.63F,
      .alpha = 1.0F,
  };
  const auto apply_single_pixel_state =
      [&](const photo_editor::pipeline::ColorAdjustmentState& state,
          const photo_editor::pipeline::ColorPixel& source) {
        std::vector<photo_editor::pipeline::ColorPixel> pixels = {source};
        photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
        return pixels[0];
      };
  auto configure_equivalent_edit =
      [](auto* target) {
        if constexpr (requires { target->input_transform.exposure; }) {
          target->input_transform.exposure = 0.45;
          target->input_transform.temperature = 18.0;
          target->input_transform.tint = -9.0;
        } else {
          target->exposure = 0.45;
          target->temperature = 18.0;
          target->tint = -9.0;
        }
        target->contrast = 18.0;
        target->highlights = -12.0;
        target->shadows = 16.0;
        target->whites = 7.0;
        target->blacks = -6.0;
        target->vibrance = 14.0;
        target->saturation = 9.0;
      };

  photo_editor::pipeline::ColorAdjustmentState global_parity_state;
  global_parity_state.color_pipeline.output_headroom = 2.0;
  configure_equivalent_edit(&global_parity_state);
  const auto global_parity_pixel =
      apply_single_pixel_state(global_parity_state, parity_source);

  photo_editor::pipeline::ColorAdjustmentState local_parity_state;
  local_parity_state.color_pipeline.output_headroom = 2.0;
  photo_editor::pipeline::LocalMask full_local_mask;
  full_local_mask.kind = "brush";
  full_local_mask.operation = "add";
  full_local_mask.segmentation_width = 1;
  full_local_mask.segmentation_height = 1;
  full_local_mask.segmentation_mask = {0xff};
  configure_equivalent_edit(&full_local_mask);
  local_parity_state.local_masks.push_back(full_local_mask);
  const auto local_parity_pixel =
      apply_single_pixel_state(local_parity_state, parity_source);

  photo_editor::pipeline::ColorAdjustmentState layer_parity_state;
  layer_parity_state.color_pipeline.output_headroom = 2.0;
  photo_editor::pipeline::PhotoLayer parity_layer;
  parity_layer.kind = "adjustment";
  parity_layer.blend_mode = "normal";
  parity_layer.opacity = 1.0;
  configure_equivalent_edit(&parity_layer);
  layer_parity_state.photo_layers.push_back(parity_layer);
  const auto layer_parity_pixel =
      apply_single_pixel_state(layer_parity_state, parity_source);

  const auto max_float_delta = [](
      const photo_editor::pipeline::ColorPixel& left,
      const photo_editor::pipeline::ColorPixel& right) {
    return std::max({
        std::abs(left.red - right.red),
        std::abs(left.green - right.green),
        std::abs(left.blue - right.blue),
    });
  };
  if (max_float_delta(global_parity_pixel, local_parity_pixel) > 3.0e-6F ||
      max_float_delta(global_parity_pixel, layer_parity_pixel) > 3.0e-6F) {
    std::cerr << "global/local delta="
              << max_float_delta(global_parity_pixel, local_parity_pixel)
              << " global/layer delta="
              << max_float_delta(global_parity_pixel, layer_parity_pixel)
              << '\n';
    std::cerr << "global, full-mask local, and normal adjustment-layer color edits should match within tolerance\n";
    return EXIT_FAILURE;
  }

  const auto aces_highlight_source = photo_editor::pipeline::ColorPixel{
      .red = 0.88F,
      .green = 0.64F,
      .blue = 0.37F,
      .alpha = 1.0F,
  };
  auto configure_aces_parity_state =
      [](photo_editor::pipeline::ColorAdjustmentState* state) {
        state->color_pipeline.output_headroom = 16.0;
        state->color_pipeline.photographic_rendering_model =
            photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1;
      };
  photo_editor::pipeline::ColorAdjustmentState aces_global_parity_state;
  configure_aces_parity_state(&aces_global_parity_state);
  configure_equivalent_edit(&aces_global_parity_state);
  const auto aces_global_parity_pixel =
      apply_single_pixel_state(aces_global_parity_state, aces_highlight_source);

  photo_editor::pipeline::ColorAdjustmentState aces_local_parity_state;
  configure_aces_parity_state(&aces_local_parity_state);
  aces_local_parity_state.local_masks.push_back(full_local_mask);
  const auto aces_local_parity_pixel =
      apply_single_pixel_state(aces_local_parity_state, aces_highlight_source);

  photo_editor::pipeline::ColorAdjustmentState aces_layer_parity_state;
  configure_aces_parity_state(&aces_layer_parity_state);
  aces_layer_parity_state.photo_layers.push_back(parity_layer);
  const auto aces_layer_parity_pixel =
      apply_single_pixel_state(aces_layer_parity_state, aces_highlight_source);

  if (std::max({
          aces_global_parity_pixel.red,
          aces_global_parity_pixel.green,
          aces_global_parity_pixel.blue,
      }) <= 1.0F) {
    std::cerr << "ACES parity fixture must exercise scene-linear values above diffuse white\n";
    return EXIT_FAILURE;
  }
  if (max_float_delta(aces_global_parity_pixel, aces_local_parity_pixel) > 3.0e-6F ||
      max_float_delta(aces_global_parity_pixel, aces_layer_parity_pixel) > 3.0e-6F) {
    std::cerr << "ACES extended-highlight global/local delta="
              << max_float_delta(aces_global_parity_pixel, aces_local_parity_pixel)
              << " global/layer delta="
              << max_float_delta(aces_global_parity_pixel, aces_layer_parity_pixel)
              << " global=("
              << aces_global_parity_pixel.red << ','
              << aces_global_parity_pixel.green << ','
              << aces_global_parity_pixel.blue << ") local=("
              << aces_local_parity_pixel.red << ','
              << aces_local_parity_pixel.green << ','
              << aces_local_parity_pixel.blue << ") layer=("
              << aces_layer_parity_pixel.red << ','
              << aces_layer_parity_pixel.green << ','
              << aces_layer_parity_pixel.blue << ')'
              << '\n';
    std::cerr << "ACES global, full-mask local, and normal adjustment-layer edits should share one operator in extended range\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState black_point_state;
  black_point_state.color_pipeline.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::
          LegacyOklchV1;
  black_point_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  black_point_state.color_pipeline.output_headroom = 1.0;
  black_point_state.color_pipeline.black_point_compensation_enabled = true;
  black_point_state.color_pipeline.source_black_point = 0.0;
  black_point_state.color_pipeline.output_black_point = 0.04;
  std::vector<photo_editor::pipeline::ColorPixel> compensated_black_points = {
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F},
      {.red = 0.02F, .green = 0.02F, .blue = 0.02F, .alpha = 1.0F},
      {.red = 0.50F, .green = 0.50F, .blue = 0.50F, .alpha = 1.0F},
      {.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 0.5F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &compensated_black_points, 4, 1, black_point_state);
  if (compensated_black_points[0].red < 0.039F ||
      compensated_black_points[1].red <= compensated_black_points[0].red ||
      compensated_black_points[2].red <= compensated_black_points[1].red ||
      std::abs(compensated_black_points[3].red - 1.0F) > 1.0e-5F ||
      std::abs(compensated_black_points[3].alpha - 0.5F) > 1.0e-6F) {
    std::cerr << "black-point compensation should lift shadows, preserve order, and pin white: "
              << compensated_black_points[0].red << ", "
              << compensated_black_points[1].red << ", "
              << compensated_black_points[2].red << ", "
              << compensated_black_points[3].red << '\n';
    return EXIT_FAILURE;
  }

  auto disabled_black_point_state = black_point_state;
  disabled_black_point_state.color_pipeline.black_point_compensation_enabled = false;
  std::vector<photo_editor::pipeline::ColorPixel> disabled_black_points = {
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &disabled_black_points, 1, 1, disabled_black_point_state);
  if (disabled_black_points[0].red > 1.0e-6F ||
      disabled_black_points[0].green > 1.0e-6F ||
      disabled_black_points[0].blue > 1.0e-6F) {
    std::cerr << "disabled black-point compensation should not remap output black\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState raw_override_state;
  raw_override_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  raw_override_state.input_transform.raw_input_transform_enabled = true;
  raw_override_state.input_transform.raw_white_balance_override_enabled = true;
  raw_override_state.input_transform.raw_white_balance_temperature = 7800.0;
  raw_override_state.input_transform.raw_white_balance_tint = 25.0;
  std::vector<photo_editor::pipeline::ColorPixel> raw_override_pixels = {
      {.red = 0.55F, .green = 0.55F, .blue = 0.55F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(&raw_override_pixels, 1, 1, raw_override_state);

  auto slider_white_balance_state = raw_override_state;
  slider_white_balance_state.input_transform.raw_white_balance_override_enabled = false;
  slider_white_balance_state.input_transform.temperature = 0.0;
  slider_white_balance_state.input_transform.tint = 0.0;
  std::vector<photo_editor::pipeline::ColorPixel> slider_white_balance_pixels = {
      {.red = 0.55F, .green = 0.55F, .blue = 0.55F, .alpha = 1.0F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &slider_white_balance_pixels, 1, 1, slider_white_balance_state);
  const auto raw_override_delta = std::max({
      std::abs(raw_override_pixels[0].red - slider_white_balance_pixels[0].red),
      std::abs(raw_override_pixels[0].green - slider_white_balance_pixels[0].green),
      std::abs(raw_override_pixels[0].blue - slider_white_balance_pixels[0].blue),
  });
  if (raw_override_delta <= 1.0e-5F) {
    std::cerr << "raw white balance override should take precedence over neutral slider white balance\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState raw_level_state;
  raw_level_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  raw_level_state.input_transform.raw_input_transform_enabled = true;
  raw_level_state.input_transform.raw_black_level = 0.10;
  raw_level_state.input_transform.raw_white_level = 0.90;
  std::vector<photo_editor::pipeline::ColorPixel> raw_level_baseline_pixels = {
      {.red = 0.10F, .green = 0.50F, .blue = 0.90F, .alpha = 1.0F},
  };
  std::vector<photo_editor::pipeline::ColorPixel> raw_level_pixels = {
      {.red = 0.10F, .green = 0.50F, .blue = 0.90F, .alpha = 1.0F},
  };
  auto raw_level_baseline_state = raw_level_state;
  raw_level_baseline_state.input_transform.raw_black_level = 0.0;
  raw_level_baseline_state.input_transform.raw_white_level = 1.0;
  photo_editor::pipeline::apply_color_adjustments(
      &raw_level_baseline_pixels,
      1,
      1,
      raw_level_baseline_state);
  photo_editor::pipeline::apply_color_adjustments(&raw_level_pixels, 1, 1, raw_level_state);
  if (raw_level_pixels[0].red >= raw_level_baseline_pixels[0].red ||
      raw_level_pixels[0].blue <= raw_level_baseline_pixels[0].blue) {
    std::cerr << "raw black and white levels should normalize the input transform range\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState highlight_reconstruction_state;
  highlight_reconstruction_state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear ProPhoto RGB");
  highlight_reconstruction_state.input_transform.raw_input_transform_enabled = true;
  highlight_reconstruction_state.input_transform.highlight_recovery_enabled = true;
  highlight_reconstruction_state.input_transform.highlight_reconstruction_mode = "Enabled";
  highlight_reconstruction_state.input_transform.highlight_reconstruction_model =
      "camera-linear-opposed-v2";
  std::vector<photo_editor::pipeline::ColorPixel> reconstructed_highlights = {
      {.red = 0.995F, .green = 1.60F, .blue = 1.40F, .alpha = 0.61F},
  };
  photo_editor::pipeline::ColorAdjustmentTelemetry highlight_telemetry;
  photo_editor::pipeline::apply_color_adjustments(
      &reconstructed_highlights,
      1,
      1,
      highlight_reconstruction_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      {},
      &highlight_telemetry);

  auto disabled_highlight_state = highlight_reconstruction_state;
  disabled_highlight_state.input_transform.highlight_reconstruction_mode = "Disabled";
  std::vector<photo_editor::pipeline::ColorPixel> disabled_highlights = {
      {.red = 0.995F, .green = 1.60F, .blue = 1.40F, .alpha = 0.61F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &disabled_highlights, 1, 1, disabled_highlight_state);
  auto camera_default_highlight_state = highlight_reconstruction_state;
  camera_default_highlight_state.input_transform.highlight_reconstruction_mode = "Camera Default";
  std::vector<photo_editor::pipeline::ColorPixel> camera_default_highlights = {
      {.red = 0.995F, .green = 1.60F, .blue = 1.40F, .alpha = 0.61F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &camera_default_highlights, 1, 1, camera_default_highlight_state);
  if (reconstructed_highlights[0].red <= disabled_highlights[0].red ||
      std::abs(reconstructed_highlights[0].alpha - 0.61F) > 1.0e-7F ||
      !highlight_telemetry.highlight_reconstruction.active ||
      highlight_telemetry.highlight_reconstruction.model != "camera-linear-opposed-v2" ||
      highlight_telemetry.highlight_reconstruction.clipped_pixels != 1U ||
      highlight_telemetry.highlight_reconstruction.fully_clipped_pixels != 1U ||
      highlight_telemetry.highlight_reconstruction.reconstructed_channel_samples == 0U) {
    std::cerr << "camera-linear highlight reconstruction should rebuild clipped channels from opposed sensor signals\n";
    return EXIT_FAILURE;
  }
  if (camera_default_highlights[0].red <= disabled_highlights[0].red ||
      camera_default_highlights[0].red >= reconstructed_highlights[0].red) {
    std::cerr << "highlight reconstruction modes should provide disabled, camera default, and full strengths\n";
    return EXIT_FAILURE;
  }

  std::vector<photo_editor::pipeline::ColorPixel> saturated_camera_highlight = {
      {.red = 1.80F, .green = 0.30F, .blue = 0.12F, .alpha = 0.42F},
  };
  auto saturated_disabled_highlight = saturated_camera_highlight;
  photo_editor::pipeline::apply_color_adjustments(
      &saturated_camera_highlight, 1, 1, highlight_reconstruction_state);
  photo_editor::pipeline::apply_color_adjustments(
      &saturated_disabled_highlight, 1, 1, disabled_highlight_state);
  if (std::abs(saturated_camera_highlight[0].red - saturated_disabled_highlight[0].red) > 1.0e-6F ||
      std::abs(saturated_camera_highlight[0].green - saturated_disabled_highlight[0].green) > 1.0e-6F ||
      std::abs(saturated_camera_highlight[0].blue - saturated_disabled_highlight[0].blue) > 1.0e-6F ||
      std::abs(saturated_camera_highlight[0].alpha - 0.42F) > 1.0e-7F) {
    std::cerr << "camera-linear reconstruction should not invent neutral color in a valid saturated highlight\n";
    return EXIT_FAILURE;
  }

  auto legacy_highlight_state = highlight_reconstruction_state;
  legacy_highlight_state.input_transform.highlight_reconstruction_model =
      "legacy-working-rgb-v1";
  std::vector<photo_editor::pipeline::ColorPixel> legacy_highlight = {
      {.red = 1.80F, .green = 0.30F, .blue = 0.12F, .alpha = 0.42F},
  };
  photo_editor::pipeline::apply_color_adjustments(
      &legacy_highlight, 1, 1, legacy_highlight_state);
  if (legacy_highlight[0].green <= saturated_camera_highlight[0].green ||
      legacy_highlight[0].blue <= saturated_camera_highlight[0].blue) {
    std::cerr << "legacy highlight recipes should retain their prior working-RGB reconstruction\n";
    return EXIT_FAILURE;
  }

  constexpr int kHighlightDiagnosticWidth = 12;
  constexpr int kHighlightDiagnosticHeight = 12;
  std::vector<photo_editor::pipeline::ColorPixel> highlight_diagnostic_pixels(
      kHighlightDiagnosticWidth * kHighlightDiagnosticHeight,
      {.red = 0.90F, .green = 0.50F, .blue = 0.40F, .alpha = 1.0F});
  for (int y = 3; y < 9; ++y) {
    for (int x = 3; x < 9; ++x) {
      highlight_diagnostic_pixels[static_cast<std::size_t>(
          y * kHighlightDiagnosticWidth + x)] =
          {.red = 1.0F, .green = 1.40F, .blue = 1.20F, .alpha = 1.0F};
    }
  }
  const auto highlight_analysis =
      photo_editor::pipeline::analyze_highlight_reconstruction(
          highlight_diagnostic_pixels,
          kHighlightDiagnosticWidth,
          kHighlightDiagnosticHeight,
          highlight_reconstruction_state.input_transform);
  if (!highlight_analysis.active || highlight_analysis.clipped_pixels != 36U ||
      highlight_analysis.fully_clipped_pixels != 36U ||
      highlight_analysis.clipped_channel_samples[0] != 36U ||
      highlight_analysis.clipped_channel_samples[1] != 36U ||
      highlight_analysis.clipped_channel_samples[2] != 36U ||
      highlight_analysis.correction_reference_samples[0] < 32U ||
      highlight_analysis.chrominance_correction[0] <= 0.30) {
    std::cerr << "highlight diagnostics should report per-channel clipping and nearby chrominance evidence\n";
    return EXIT_FAILURE;
  }
  const std::vector<photo_editor::pipeline::ColorPixel>
      highlight_channel_clipping_matrix = {
          {.red = 1.0F, .green = 0.5F, .blue = 0.4F, .alpha = 1.0F},
          {.red = 1.0F, .green = 1.0F, .blue = 0.4F, .alpha = 1.0F},
          {.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F},
      };
  const auto highlight_channel_analysis =
      photo_editor::pipeline::analyze_highlight_reconstruction(
          highlight_channel_clipping_matrix,
          3,
          1,
          highlight_reconstruction_state.input_transform);
  if (!highlight_channel_analysis.active ||
      highlight_channel_analysis.clipped_pixels != 3U ||
      highlight_channel_analysis.multi_channel_clipped_pixels != 2U ||
      highlight_channel_analysis.fully_clipped_pixels != 1U ||
      highlight_channel_analysis.clipped_channel_samples[0] != 3U ||
      highlight_channel_analysis.clipped_channel_samples[1] != 2U ||
      highlight_channel_analysis.clipped_channel_samples[2] != 1U) {
    std::cerr << "highlight diagnostics should distinguish one-, two-, and three-channel sensor clipping\n";
    return EXIT_FAILURE;
  }
  const auto legacy_highlight_analysis =
      photo_editor::pipeline::analyze_highlight_reconstruction(
          highlight_diagnostic_pixels,
          kHighlightDiagnosticWidth,
          kHighlightDiagnosticHeight,
          legacy_highlight_state.input_transform);
  if (legacy_highlight_analysis.active ||
      legacy_highlight_analysis.model != "legacy-working-rgb-v1") {
    std::cerr << "legacy highlight rendering should remain explicit in diagnostics\n";
    return EXIT_FAILURE;
  }
  if (photo_editor::pipeline::metal_color_pipeline_incompatibility_reason(
          highlight_reconstruction_state) !=
      "camera_linear_highlight_reconstruction_requires_cpu") {
    std::cerr << "camera-linear highlight reconstruction should fail over to the exact CPU path\n";
    return EXIT_FAILURE;
  }
  auto camera_lut_state = photo_editor::pipeline::ColorAdjustmentState{};
  camera_lut_state.input_transform.raw_input_transform_enabled = true;
  camera_lut_state.input_transform.highlight_recovery_enabled = false;
  camera_lut_state.color_pipeline.camera_profile.valid = true;
  if (photo_editor::pipeline::metal_color_pipeline_incompatibility_reason(camera_lut_state) !=
      "camera_linear_lut_fidelity_requires_cpu") {
    std::cerr << "sampled camera-linear LUT must not silently replace exact characterization\n";
    return EXIT_FAILURE;
  }

  std::vector<photo_editor::pipeline::ColorPixel> highlight_ramp;
  highlight_ramp.reserve(64U);
  for (int index = 0; index < 64; ++index) {
    highlight_ramp.push_back({
        .red = static_cast<float>(
            0.995 +
            (1.80 - 0.995) *
                static_cast<double>(index) / 63.0),
        .green = 1.40F,
        .blue = 1.20F,
        .alpha = 0.73F,
    });
  }
  photo_editor::pipeline::apply_color_adjustments(
      &highlight_ramp,
      static_cast<int>(highlight_ramp.size()),
      1,
      highlight_reconstruction_state);
  for (std::size_t index = 0U; index < highlight_ramp.size(); ++index) {
    const auto& sample = highlight_ramp[index];
    if (!std::isfinite(sample.red) || !std::isfinite(sample.green) ||
        !std::isfinite(sample.blue) || std::abs(sample.alpha - 0.73F) > 1.0e-7F ||
        (index > 0U && sample.red + 1.0e-6F < highlight_ramp[index - 1U].red)) {
      std::cerr << "camera-linear highlight reconstruction should remain finite, alpha-safe, and monotonic\n";
      return EXIT_FAILURE;
    }
  }

  photo_editor::pipeline::ColorAdjustmentState cancelled_state;
  cancelled_state.input_transform.exposure = 1.0;
  std::vector<photo_editor::pipeline::ColorPixel> cancelled_pixels = {
      {.red = 0.25F, .green = 0.35F, .blue = 0.45F, .alpha = 1.0F},
  };
  const auto original_cancelled_pixel = cancelled_pixels[0];
  std::atomic<int> latest_generation = 2;
  photo_editor::pipeline::apply_color_adjustments(
      &cancelled_pixels,
      1,
      1,
      cancelled_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      photo_editor::pipeline::CancellationToken{
          .latest_generation = &latest_generation,
          .generation = 1,
      });
  if (std::abs(cancelled_pixels[0].red - original_cancelled_pixel.red) > 1.0e-7F ||
      std::abs(cancelled_pixels[0].green - original_cancelled_pixel.green) > 1.0e-7F ||
      std::abs(cancelled_pixels[0].blue - original_cancelled_pixel.blue) > 1.0e-7F) {
    std::cerr << "cancelled color adjustment work should leave pixels unchanged\n";
    return EXIT_FAILURE;
  }

  const photo_editor::pipeline::ColorPixel layer_opacity_source{
      .red = 0.24F,
      .green = 0.36F,
      .blue = 0.52F,
      .alpha = 1.0F,
  };
  photo_editor::pipeline::PhotoLayer opacity_layer;
  opacity_layer.kind = "adjustment";
  opacity_layer.blend_mode = "normal";
  opacity_layer.exposure = 0.55;
  opacity_layer.contrast = 18.0;
  opacity_layer.vibrance = 26.0;
  opacity_layer.curve_lights = 32.0;
  opacity_layer.luma_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 0.45, .y = 0.32},
      {.x = 1.0, .y = 1.0},
  };
  opacity_layer.grading_highlights_hue = 0.58;
  opacity_layer.grading_highlights_saturation = 34.0;
  opacity_layer.grading_highlights_luminance = 18.0;
  opacity_layer.grading_amount = 100.0;
  const auto render_adjustment_opacity = [&](double opacity) {
    photo_editor::pipeline::ColorAdjustmentState state;
    auto layer = opacity_layer;
    layer.opacity = opacity;
    state.photo_layers.push_back(layer);
    std::vector<photo_editor::pipeline::ColorPixel> pixels = {layer_opacity_source};
    photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
    return pixels[0];
  };
  const auto opacity_zero = render_adjustment_opacity(0.0);
  const auto opacity_half = render_adjustment_opacity(0.5);
  const auto opacity_full = render_adjustment_opacity(1.0);
  const auto midpoint = [](float low, float high) {
    return low + ((high - low) * 0.5F);
  };
  const auto opacity_tolerance = 0.04F;
  if (std::abs(opacity_full.red - opacity_zero.red) < 0.04F ||
      std::abs(opacity_full.green - opacity_zero.green) < 0.04F ||
      std::abs(opacity_full.blue - opacity_zero.blue) < 0.04F ||
      std::abs(opacity_half.red - midpoint(opacity_zero.red, opacity_full.red)) >
          opacity_tolerance ||
      std::abs(opacity_half.green - midpoint(opacity_zero.green, opacity_full.green)) >
          opacity_tolerance ||
      std::abs(opacity_half.blue - midpoint(opacity_zero.blue, opacity_full.blue)) >
          opacity_tolerance) {
    std::cerr << "adjustment layer opacity should gate the full tone/curve/grading result once\n";
    return EXIT_FAILURE;
  }
  const auto render_adjustment_mask_gate = [&](double mask_opacity, double mask_density) {
    photo_editor::pipeline::ColorAdjustmentState state;
    auto layer = opacity_layer;
    layer.opacity = 1.0;
    layer.mask_data = {0xff};
    layer.mask_width = 1;
    layer.mask_height = 1;
    layer.mask_opacity = mask_opacity;
    layer.mask_density = mask_density;
    state.photo_layers.push_back(layer);
    std::vector<photo_editor::pipeline::ColorPixel> pixels = {layer_opacity_source};
    photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
    return pixels[0];
  };
  const auto mask_opacity_half = render_adjustment_mask_gate(0.5, 1.0);
  const auto mask_density_half = render_adjustment_mask_gate(1.0, 0.5);
  const auto mask_gate_tolerance = 0.01F;
  if (std::abs(mask_opacity_half.red - opacity_half.red) > mask_gate_tolerance ||
      std::abs(mask_opacity_half.green - opacity_half.green) > mask_gate_tolerance ||
      std::abs(mask_opacity_half.blue - opacity_half.blue) > mask_gate_tolerance ||
      std::abs(mask_density_half.red - opacity_half.red) > mask_gate_tolerance ||
      std::abs(mask_density_half.green - opacity_half.green) > mask_gate_tolerance ||
      std::abs(mask_density_half.blue - opacity_half.blue) > mask_gate_tolerance) {
    std::cerr << "layer mask opacity and density should gate mask alpha before the layer recipe\n";
    return EXIT_FAILURE;
  }

  const photo_editor::pipeline::ColorPixel layer_effect_source{
      .red = 0.70F,
      .green = 0.24F,
      .blue = 0.10F,
      .alpha = 1.0F,
  };
  photo_editor::pipeline::PhotoLayer effect_layer;
  effect_layer.kind = "adjustment";
  effect_layer.blend_mode = "normal";
  effect_layer.dehaze = 100.0;
  const auto render_layer_effect = [&](double mask_opacity, double mask_density) {
    photo_editor::pipeline::ColorAdjustmentState state;
    auto layer = effect_layer;
    layer.mask_data = {0xff};
    layer.mask_width = 1;
    layer.mask_height = 1;
    layer.mask_opacity = mask_opacity;
    layer.mask_density = mask_density;
    state.photo_layers.push_back(layer);
    std::vector<photo_editor::pipeline::ColorPixel> pixels = {layer_effect_source};
    photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
    return pixels[0];
  };
  const auto effect_none = render_layer_effect(0.0, 1.0);
  const auto effect_half_opacity = render_layer_effect(0.5, 1.0);
  const auto effect_half_density = render_layer_effect(1.0, 0.5);
  const auto effect_full = render_layer_effect(1.0, 1.0);
  if (std::abs(effect_full.red - effect_none.red) < 0.003F ||
      std::abs(effect_half_opacity.red - midpoint(effect_none.red, effect_full.red)) >
          mask_gate_tolerance ||
      std::abs(effect_half_opacity.green - midpoint(effect_none.green, effect_full.green)) >
          mask_gate_tolerance ||
      std::abs(effect_half_opacity.blue - midpoint(effect_none.blue, effect_full.blue)) >
          mask_gate_tolerance ||
      std::abs(effect_half_density.red - effect_half_opacity.red) > mask_gate_tolerance ||
      std::abs(effect_half_density.green - effect_half_opacity.green) > mask_gate_tolerance ||
      std::abs(effect_half_density.blue - effect_half_opacity.blue) > mask_gate_tolerance) {
    std::cerr << "layer-local effects/detail should evaluate before mask opacity and density gate alpha\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::PhotoLayer spatial_effect_layer;
  spatial_effect_layer.kind = "adjustment";
  spatial_effect_layer.blend_mode = "normal";
  spatial_effect_layer.texture = 100.0;
  spatial_effect_layer.clarity = 100.0;
  const std::vector<photo_editor::pipeline::ColorPixel> spatial_effect_source = {
      {.red = 0.22F, .green = 0.24F, .blue = 0.26F, .alpha = 1.0F},
      {.red = 0.31F, .green = 0.33F, .blue = 0.35F, .alpha = 1.0F},
      {.red = 0.68F, .green = 0.70F, .blue = 0.72F, .alpha = 1.0F},
      {.red = 0.34F, .green = 0.36F, .blue = 0.38F, .alpha = 1.0F},
      {.red = 0.24F, .green = 0.26F, .blue = 0.28F, .alpha = 1.0F},
  };
  const auto render_layer_spatial_effect = [&](double mask_opacity, double mask_density) {
    photo_editor::pipeline::ColorAdjustmentState state;
    auto layer = spatial_effect_layer;
    layer.mask_data = {0xff, 0xff, 0xff, 0xff, 0xff};
    layer.mask_width = 5;
    layer.mask_height = 1;
    layer.mask_opacity = mask_opacity;
    layer.mask_density = mask_density;
    state.photo_layers.push_back(layer);
    auto pixels = spatial_effect_source;
    photo_editor::pipeline::apply_color_adjustments(&pixels, 5, 1, state);
    return pixels;
  };
  const auto spatial_effect_none = render_layer_spatial_effect(0.0, 1.0);
  const auto spatial_effect_half_opacity = render_layer_spatial_effect(0.5, 1.0);
  const auto spatial_effect_half_density = render_layer_spatial_effect(1.0, 0.5);
  const auto spatial_effect_full = render_layer_spatial_effect(1.0, 1.0);
  constexpr auto kSpatialEffectCenterIndex = std::size_t{2};
  const auto& spatial_none = spatial_effect_none[kSpatialEffectCenterIndex];
  const auto& spatial_half_opacity = spatial_effect_half_opacity[kSpatialEffectCenterIndex];
  const auto& spatial_half_density = spatial_effect_half_density[kSpatialEffectCenterIndex];
  const auto& spatial_full = spatial_effect_full[kSpatialEffectCenterIndex];
  if (std::abs(spatial_full.red - spatial_none.red) < 0.01F ||
      std::abs(spatial_half_opacity.red - midpoint(spatial_none.red, spatial_full.red)) >
          opacity_tolerance ||
      std::abs(spatial_half_opacity.green - midpoint(spatial_none.green, spatial_full.green)) >
          opacity_tolerance ||
      std::abs(spatial_half_opacity.blue - midpoint(spatial_none.blue, spatial_full.blue)) >
          opacity_tolerance ||
      std::abs(spatial_half_density.red - spatial_half_opacity.red) > mask_gate_tolerance ||
      std::abs(spatial_half_density.green - spatial_half_opacity.green) > mask_gate_tolerance ||
      std::abs(spatial_half_density.blue - spatial_half_opacity.blue) > mask_gate_tolerance) {
    std::cerr << "layer-local texture and clarity should render before mask opacity and density gate alpha\n";
    return EXIT_FAILURE;
  }

  const auto max_rgb_delta = [](const std::vector<photo_editor::pipeline::ColorPixel>& lhs,
                                const std::vector<photo_editor::pipeline::ColorPixel>& rhs) {
    auto max_delta = 0.0F;
    if (lhs.size() != rhs.size()) {
      return 1.0F;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      max_delta = std::max(
          max_delta,
          std::max({
              std::abs(lhs[index].red - rhs[index].red),
              std::abs(lhs[index].green - rhs[index].green),
              std::abs(lhs[index].blue - rhs[index].blue),
          }));
    }
    return max_delta;
  };
  std::vector<photo_editor::pipeline::ColorPixel> layer_detail_source;
  layer_detail_source.reserve(35);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 7; ++x) {
      const auto checker = ((x + y) % 2) == 0 ? 0.10F : -0.08F;
      const auto chroma_noise = ((x * 3 + y * 5) % 4) == 0 ? 0.07F : -0.05F;
      layer_detail_source.push_back({
          .red = 0.22F + static_cast<float>(x) * 0.055F + checker,
          .green = 0.28F + static_cast<float>(y) * 0.062F - checker * 0.45F,
          .blue = 0.34F + static_cast<float>(x + y) * 0.022F + chroma_noise,
          .alpha = 1.0F,
      });
    }
  }
  const auto render_adjustment_detail_layer =
      [&](const photo_editor::pipeline::PhotoLayer& layer) {
        photo_editor::pipeline::ColorAdjustmentState state;
        state.photo_layers.push_back(layer);
        auto pixels = layer_detail_source;
        photo_editor::pipeline::apply_color_adjustments(&pixels, 7, 5, state);
        return pixels;
      };
  photo_editor::pipeline::PhotoLayer neutral_detail_layer;
  neutral_detail_layer.kind = "adjustment";
  neutral_detail_layer.blend_mode = "normal";
  const auto neutral_detail_pixels = render_adjustment_detail_layer(neutral_detail_layer);

  auto sharpen_layer = neutral_detail_layer;
  sharpen_layer.sharpening = 100.0;
  sharpen_layer.sharpen_radius = 1.0;
  sharpen_layer.sharpen_detail = 80.0;
  if (max_rgb_delta(render_adjustment_detail_layer(sharpen_layer), neutral_detail_pixels) <
      0.002F) {
    std::cerr << "adjustment layer sharpening should visibly affect rendered pixels\n";
    return EXIT_FAILURE;
  }

  auto luma_nr_layer = neutral_detail_layer;
  luma_nr_layer.luminance_nr = 100.0;
  if (max_rgb_delta(render_adjustment_detail_layer(luma_nr_layer), neutral_detail_pixels) <
      0.002F) {
    std::cerr << "adjustment layer luminance noise reduction should visibly affect rendered pixels\n";
    return EXIT_FAILURE;
  }

  auto color_nr_layer = neutral_detail_layer;
  color_nr_layer.color_nr = 100.0;
  if (max_rgb_delta(render_adjustment_detail_layer(color_nr_layer), neutral_detail_pixels) <
      0.002F) {
    std::cerr << "adjustment layer color noise reduction should visibly affect rendered pixels\n";
    return EXIT_FAILURE;
  }

  auto lens_layer = neutral_detail_layer;
  lens_layer.lens_vignette = -80.0;
  lens_layer.lens_distortion = 60.0;
  lens_layer.chromatic_aberration = 45.0;
  if (max_rgb_delta(render_adjustment_detail_layer(lens_layer), neutral_detail_pixels) <
      0.002F) {
    std::cerr << "adjustment layer lens corrections should visibly affect rendered pixels\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::ColorAdjustmentState
      color_lut_identity_state;
  color_lut_identity_state.saturation =
      12.345678;
  const auto first_color_lut =
      photo_editor::pipeline::build_color_lut(
          color_lut_identity_state);
  const auto reused_color_lut =
      photo_editor::pipeline::build_color_lut(
          color_lut_identity_state);
  color_lut_identity_state.saturation += 0.125;
  const auto changed_color_lut =
      photo_editor::pipeline::build_color_lut(
          color_lut_identity_state);
  if (!first_color_lut.valid ||
      first_color_lut.cache_identity == 0U ||
      reused_color_lut.cache_identity !=
          first_color_lut.cache_identity ||
      changed_color_lut.cache_identity == 0U ||
      changed_color_lut.cache_identity ==
          first_color_lut.cache_identity) {
    std::cerr
        << "compiled color LUTs must retain one immutable cache identity until their inputs change\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

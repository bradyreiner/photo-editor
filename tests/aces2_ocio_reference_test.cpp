#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "photo_editor/pipeline/color_metrics.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/output_rendering.h"

#ifndef PHOTO_EDITOR_ACES2_OCIO_REFERENCE_PATH
#error "PHOTO_EDITOR_ACES2_OCIO_REFERENCE_PATH must point to the ACES 2 corpus"
#endif
#ifndef PHOTO_EDITOR_ACES2_OCIO_REFERENCE_SHA256
#error "PHOTO_EDITOR_ACES2_OCIO_REFERENCE_SHA256 must identify the ACES 2 corpus"
#endif

namespace {

struct ReferencePatch final {
  std::string patch_id;
  std::array<double, 3> input_aces2065_1{};
  std::array<double, 3> expected_linear_srgb{};
};

[[nodiscard]] std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ',')) {
    fields.push_back(field);
  }
  return fields;
}

[[nodiscard]] bool parse_double(const std::string& value, double* output) {
  if (output == nullptr) {
    return false;
  }
  try {
    std::size_t parsed = 0U;
    const auto result = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(result)) {
      return false;
    }
    *output = result;
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] std::vector<ReferencePatch> load_reference_patches() {
  std::ifstream input(PHOTO_EDITOR_ACES2_OCIO_REFERENCE_PATH);
  if (!input) {
    return {};
  }

  std::vector<ReferencePatch> patches;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.rfind("patch_id,", 0U) == 0U) {
      continue;
    }
    const auto fields = split_csv_line(line);
    if (fields.size() != 10U) {
      return {};
    }
    ReferencePatch patch{.patch_id = fields[0]};
    auto valid = true;
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      valid = parse_double(
                  fields[1U + channel],
                  &patch.input_aces2065_1[channel]) &&
              valid;
      valid = parse_double(
                  fields[7U + channel],
                  &patch.expected_linear_srgb[channel]) &&
              valid;
    }
    if (!valid) {
      return {};
    }
    patches.push_back(std::move(patch));
  }
  return patches;
}

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

[[nodiscard]] double percentile(
    const std::vector<double>& sorted_values,
    double fraction) {
  if (sorted_values.empty()) {
    return 0.0;
  }
  const auto rank = static_cast<std::size_t>(std::ceil(
      std::clamp(fraction, 0.0, 1.0) *
      static_cast<double>(sorted_values.size())));
  return sorted_values[std::min(
      sorted_values.size() - 1U,
      std::max<std::size_t>(1U, rank) - 1U)];
}

}  // namespace

int main(int argc, char* argv[]) {
  if (!photo_editor::pipeline::aces2_official_output_transform_available()) {
    std::cerr << "OpenColorIO ACES 2 backend unavailable; independent output-transform parity is not executable on this build\n";
    return 77;
  }
  const auto patches = load_reference_patches();
  if (patches.size() != 600U) {
    std::cerr << "invalid ACES 2 OCIO reference corpus: expected 600 patches, got "
              << patches.size() << '\n';
    return EXIT_FAILURE;
  }

  const auto target = photo_editor::pipeline::make_sdr_display_target(
      photo_editor::pipeline::output_color_profile_from_name("sRGB"));
  const auto processor =
      photo_editor::pipeline::compile_aces2_output_processor(
          photo_editor::pipeline::make_aces2065_1_color_space(),
          target);
  if (processor == nullptr) {
    std::cerr << "failed to compile the ACES 2 output processor\n";
    return EXIT_FAILURE;
  }

  std::vector<double> delta_e_00;
  delta_e_00.reserve(patches.size());
  std::string worst_patch;
  for (const auto& patch : patches) {
    const auto actual =
        photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            patch.input_aces2065_1,
            processor->parameters);
    const auto delta = photo_editor::pipeline::delta_e_2000(
        linear_srgb_to_lab(actual),
        linear_srgb_to_lab(patch.expected_linear_srgb));
    delta_e_00.push_back(delta);
    if (delta >= *std::max_element(delta_e_00.begin(), delta_e_00.end())) {
      worst_patch = patch.patch_id;
    }
  }
  std::sort(delta_e_00.begin(), delta_e_00.end());
  const auto p50 = percentile(delta_e_00, 0.50);
  const auto p95 = percentile(delta_e_00, 0.95);
  const auto p99 = percentile(delta_e_00, 0.99);
  const auto maximum = delta_e_00.back();
  std::cout << "ACES 2 OCIO reference corpus SHA-256: "
            << PHOTO_EDITOR_ACES2_OCIO_REFERENCE_SHA256 << '\n'
            << "patches=" << patches.size() << " p50_delta_e_00=" << p50
            << " p95_delta_e_00=" << p95 << " p99_delta_e_00=" << p99
            << " max_delta_e_00=" << maximum
            << " worst_patch=" << worst_patch << '\n';

  // A production display transform should be visually indistinguishable from
  // the pinned independent ACES 2 implementation for nearly every patch, with
  // a small allowance at pathological negative/high-energy gamut boundaries.
  if (p99 > 0.5 || maximum > 2.0) {
    std::cerr << "ACES 2 output transform exceeds independent OCIO parity gate: "
              << "p99=" << p99 << ", max=" << maximum << '\n';
    return EXIT_FAILURE;
  }

  // Pin the standard working-space route separately from the ACES2065-1
  // fixed-function corpus above. These values were generated by applying
  // OpenColorIO 2.5.1's built-in cg-config v4 ACES 2 sRGB display/view
  // processor directly to linear Rec.709 (sRGB), then decoding its sRGB
  // signal to linear display light. This catches accidental replacement with
  // a separately rounded matrix authority or an integer reference buffer.
  struct NamedSourcePatch final {
    std::array<double, 3> input_linear_srgb;
    std::array<double, 3> expected_linear_srgb;
  };
  constexpr std::array<NamedSourcePatch, 7> kNamedSourcePatches = {{
      {
          .input_linear_srgb = {0.0, 0.0, 0.0},
          .expected_linear_srgb = {0.0, 0.0, 0.0},
      },
      {
          .input_linear_srgb = {0.2, 0.203921568627451, 0.2},
          .expected_linear_srgb = {
              0.11333331833987174,
              0.11550470281039123,
              0.11335599198061912,
          },
      },
      {
          .input_linear_srgb = {0.18, 0.18, 0.18},
          .expected_linear_srgb = {
              0.099998403035552852,
              0.099998756952985893,
              0.099998615385924972,
          },
      },
      {
          .input_linear_srgb = {0.8, 0.2, 0.05},
          .expected_linear_srgb = {
              0.46611786441547765,
              0.11989795845770704,
              0.03140979145020989,
          },
      },
      {
          .input_linear_srgb = {0.05, 0.7, 0.2},
          .expected_linear_srgb = {
              0.065065747201518129,
              0.37715509435271477,
              0.13544554520485463,
          },
      },
      {
          .input_linear_srgb = {0.1, 0.05, 0.9},
          .expected_linear_srgb = {
              0.050274446167518076,
              0.015532868551473329,
              0.5357264283201586,
          },
      },
      {
          .input_linear_srgb = {1.0, 1.0, 1.0},
          .expected_linear_srgb = {
              0.45756430447475227,
              0.45756490601889993,
              0.45756490601889993,
          },
      },
  }};
  const auto named_source_processor =
      photo_editor::pipeline::compile_aces2_output_processor(
          photo_editor::pipeline::make_linear_srgb_color_space(), target);
  if (named_source_processor == nullptr) {
    std::cerr << "failed to compile the named linear-sRGB OCIO processor\n";
    return EXIT_FAILURE;
  }
  auto named_source_max_channel_error = 0.0;
  auto named_source_max_delta_e_00 = 0.0;
  for (const auto& patch : kNamedSourcePatches) {
    const auto actual =
        photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            patch.input_linear_srgb,
            named_source_processor->parameters);
    for (std::size_t channel = 0U; channel < actual.size(); ++channel) {
      named_source_max_channel_error = std::max(
          named_source_max_channel_error,
          std::abs(actual[channel] - patch.expected_linear_srgb[channel]));
    }
    named_source_max_delta_e_00 = std::max(
        named_source_max_delta_e_00,
        photo_editor::pipeline::delta_e_2000(
            linear_srgb_to_lab(actual),
            linear_srgb_to_lab(patch.expected_linear_srgb)));
  }
  std::cout << "ACES 2 named linear-sRGB source route: max_channel_error="
            << named_source_max_channel_error
            << " max_delta_e_00=" << named_source_max_delta_e_00 << '\n';
  if (named_source_max_channel_error > 2.0e-5 ||
      named_source_max_delta_e_00 > 0.01) {
    std::cerr << "named linear-sRGB OCIO display/view route exceeds its pinned direct-processor gate\n";
    return EXIT_FAILURE;
  }

  // OpenColorIO 2.5.1 can produce NaNs for a few extremely high-energy AP1
  // lattice nodes. The production wrapper must turn those pathological inputs
  // into deterministic finite output before either the CPU or Metal path sees
  // them.
  const auto pathological_aces2065_1 =
      photo_editor::pipeline::multiply_matrix3_vector(
          processor->parameters.acescg_to_aces2065_1,
          {441.5294544, 0.07698253, 5.16505266});
  const auto pathological_output =
      photo_editor::pipeline::aces2_render_to_linear_display_rgb(
          pathological_aces2065_1,
          processor->parameters);
  if (!std::isfinite(pathological_output[0]) ||
      !std::isfinite(pathological_output[1]) ||
      !std::isfinite(pathological_output[2]) ||
      pathological_output[0] < 0.0 || pathological_output[0] > 1.0 ||
      pathological_output[1] < 0.0 || pathological_output[1] > 1.0 ||
      pathological_output[2] < 0.0 || pathological_output[2] > 1.0) {
    std::cerr << "pathological ACEScg input must produce finite, contained official ACES 2 output\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::clear_aces2_output_lut_cache_for_testing();
  const auto interactive_lut =
      photo_editor::pipeline::compile_aces2_output_lut(
          photo_editor::pipeline::make_aces2065_1_color_space(),
          target,
          photo_editor::pipeline::aces2_output_lut_settings(
              photo_editor::pipeline::Aces2OutputLutQuality::Interactive));
  const auto final_lut = photo_editor::pipeline::compile_aces2_output_lut(
      photo_editor::pipeline::make_aces2065_1_color_space(),
      target,
      photo_editor::pipeline::aces2_output_lut_settings(
          photo_editor::pipeline::Aces2OutputLutQuality::Final));
  const auto reused_final_lut =
      photo_editor::pipeline::compile_aces2_output_lut(
          photo_editor::pipeline::make_aces2065_1_color_space(),
          target,
          photo_editor::pipeline::aces2_output_lut_settings(
              photo_editor::pipeline::Aces2OutputLutQuality::Final));
  const auto lut_cache_stats =
      photo_editor::pipeline::aces2_output_lut_cache_stats();
  if (interactive_lut == nullptr || final_lut == nullptr ||
      reused_final_lut.get() != final_lut.get() ||
      interactive_lut->settings.cube_size != 65 ||
      final_lut->settings.cube_size != 97 ||
      interactive_lut->cache_key.find("ap1-log2") == std::string::npos ||
      final_lut->cache_key.find("tetrahedral") == std::string::npos ||
      lut_cache_stats.requests != 3U || lut_cache_stats.hits != 1U ||
      lut_cache_stats.misses != 2U || lut_cache_stats.builds != 2U ||
      lut_cache_stats.entries != 2U ||
      lut_cache_stats.bytes !=
          interactive_lut->byte_size() + final_lut->byte_size() ||
      lut_cache_stats.maximum_bytes != 32U * 1024U * 1024U ||
      lut_cache_stats.bytes >
          lut_cache_stats.maximum_bytes) {
    std::cerr << "ACES 2 output LUT tiers should be valid, bounded, cached, and identified by shaper/interpolation semantics"
              << " interactive=" << (interactive_lut != nullptr)
              << " final=" << (final_lut != nullptr)
              << " reused="
              << (final_lut != nullptr && reused_final_lut.get() == final_lut.get())
              << " requests=" << lut_cache_stats.requests
              << " hits=" << lut_cache_stats.hits
              << " misses=" << lut_cache_stats.misses
              << " builds=" << lut_cache_stats.builds
              << " entries=" << lut_cache_stats.entries
              << " bytes=" << lut_cache_stats.bytes
              << " failure=" << lut_cache_stats.last_failure_reason << '\n';
    return EXIT_FAILURE;
  }
  photo_editor::pipeline::clear_aces2_output_lut_cache_for_testing();
  std::array<
      std::future<std::shared_ptr<
          const photo_editor::pipeline::Aces2OutputLut3D>>,
      8>
      concurrent_lut_compiles;
  for (auto& future : concurrent_lut_compiles) {
    future = std::async(std::launch::async, [&]() {
      return photo_editor::pipeline::compile_aces2_output_lut(
          photo_editor::pipeline::make_aces2065_1_color_space(),
          target,
          photo_editor::pipeline::aces2_output_lut_settings(
              photo_editor::pipeline::Aces2OutputLutQuality::Final));
    });
  }
  std::shared_ptr<const photo_editor::pipeline::Aces2OutputLut3D>
      concurrent_lut_identity;
  for (auto& future : concurrent_lut_compiles) {
    const auto compiled_lut = future.get();
    if (compiled_lut == nullptr) {
      std::cerr << "concurrent ACES 2 output LUT compilation must succeed\n";
      return EXIT_FAILURE;
    }
    if (concurrent_lut_identity == nullptr) {
      concurrent_lut_identity = compiled_lut;
    } else if (compiled_lut.get() != concurrent_lut_identity.get()) {
      std::cerr << "concurrent ACES 2 output LUT requests must share one immutable result\n";
      return EXIT_FAILURE;
    }
  }
  const auto concurrent_lut_cache_stats =
      photo_editor::pipeline::aces2_output_lut_cache_stats();
  if (concurrent_lut_cache_stats.requests !=
          concurrent_lut_compiles.size() ||
      concurrent_lut_cache_stats.hits !=
          concurrent_lut_compiles.size() - 1U ||
      concurrent_lut_cache_stats.misses != 1U ||
      concurrent_lut_cache_stats.builds != 1U ||
      concurrent_lut_cache_stats.entries != 1U ||
      concurrent_lut_cache_stats.maximum_bytes !=
          32U * 1024U * 1024U ||
      concurrent_lut_cache_stats.bytes >
          concurrent_lut_cache_stats.maximum_bytes ||
      concurrent_lut_cache_stats.bytes !=
          concurrent_lut_identity->byte_size()) {
    std::cerr << "concurrent ACES 2 output LUT requests must converge on one bounded cold-cache build\n";
    return EXIT_FAILURE;
  }
  const auto final_non_finite = std::find_if(
      final_lut->table.cbegin(),
      final_lut->table.cend(),
      [](const std::array<float, 4>& value) {
        return !std::isfinite(value[0]) || !std::isfinite(value[1]) ||
               !std::isfinite(value[2]) || !std::isfinite(value[3]);
      });
  if (final_non_finite != final_lut->table.cend()) {
    std::cerr << "final ACES 2 output LUT must not expose non-finite lattice values\n";
    return EXIT_FAILURE;
  }

  const auto evaluate_lut = [&](const photo_editor::pipeline::Aces2OutputLut3D& lut) {
    std::vector<double> errors;
    errors.reserve(patches.size());
    std::string worst;
    auto worst_error = -1.0;
    for (const auto& patch : patches) {
      const auto actual = photo_editor::pipeline::aces2_output_lut_apply(
          lut,
          patch.input_aces2065_1);
      const auto error = photo_editor::pipeline::delta_e_2000(
          linear_srgb_to_lab(actual),
          linear_srgb_to_lab(patch.expected_linear_srgb));
      errors.push_back(error);
      if (error > worst_error) {
        worst_error = error;
        worst = patch.patch_id;
      }
    }
    std::sort(errors.begin(), errors.end());
    return std::tuple{
        percentile(errors, 0.50),
        percentile(errors, 0.95),
        percentile(errors, 0.99),
        errors.back(),
        worst,
    };
  };
  const auto [interactive_p50,
              interactive_p95,
              interactive_p99,
              interactive_maximum,
              interactive_worst] = evaluate_lut(*interactive_lut);
  const auto [final_p50,
              final_p95,
              final_p99,
              final_maximum,
              final_worst] = evaluate_lut(*final_lut);
  std::cout << "ACES 2 interactive 65^3 AP1-log LUT: p50="
            << interactive_p50 << " p95=" << interactive_p95
            << " p99=" << interactive_p99 << " max="
            << interactive_maximum << " worst_patch=" << interactive_worst
            << '\n'
            << "ACES 2 final 97^3 AP1-log LUT: p50=" << final_p50
            << " p95=" << final_p95 << " p99=" << final_p99
            << " max=" << final_maximum << " worst_patch=" << final_worst
            << " cache_bytes=" << lut_cache_stats.bytes
            << " repaired_non_finite_samples="
            << final_lut->repaired_non_finite_samples << '\n';
  if (argc == 2 && std::string_view(argv[1]) == "--sweep-shaper") {
    constexpr std::array<double, 11> kOffsets = {
        0.001, 0.002, 0.003, 0.005, 0.0075, 0.01,
        0.015, 0.02, 0.03, 0.05, 0.1,
    };
    for (const auto offset : kOffsets) {
      const auto swept_lut = photo_editor::pipeline::compile_aces2_output_lut(
          photo_editor::pipeline::make_aces2065_1_color_space(),
          target,
          photo_editor::pipeline::Aces2OutputLutSettings{
              .cube_size = 97,
              .shaper_offset = offset,
          });
      if (swept_lut == nullptr) {
        std::cout << "shaper_offset=" << offset << " unavailable\n";
        continue;
      }
      const auto [sweep_p50, sweep_p95, sweep_p99, sweep_maximum, sweep_worst] =
          evaluate_lut(*swept_lut);
      std::cout << "shaper_offset=" << offset
                << " p50=" << sweep_p50
                << " p95=" << sweep_p95
                << " p99=" << sweep_p99
                << " max=" << sweep_maximum
                << " worst=" << sweep_worst << '\n';
    }
    return EXIT_SUCCESS;
  }
  if (interactive_p95 > 0.25 || interactive_p99 > 0.75 ||
      interactive_maximum > 1.5) {
    std::cerr << "interactive ACES 2 output LUT exceeds the transient preview approximation gate\n";
    return EXIT_FAILURE;
  }
  if (final_p99 > 0.25 || final_maximum > 0.75) {
    std::cerr << "final ACES 2 output LUT exceeds the CPU/GPU display-transform parity gate\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

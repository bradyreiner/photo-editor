#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "photo_editor/pipeline/color_metrics.h"
#include "photo_editor/pipeline/color_transform.h"

#ifndef PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_PATH
#error "PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_PATH must point to the reference corpus"
#endif
#ifndef PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_SHA256
#error "PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_SHA256 must identify the reference corpus"
#endif

namespace {

using photo_editor::pipeline::CieLab;
using photo_editor::pipeline::CieXyz;
using photo_editor::pipeline::ChromaticAdaptationMethod;

struct LabReference final {
  std::string name;
  CieXyz white;
  CieXyz xyz;
  CieLab expected;
  std::string source;
};

struct DeltaEReference final {
  CieLab left;
  CieLab right;
  double expected = 0.0;
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
    std::size_t parsed = 0;
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

[[nodiscard]] std::vector<LabReference> load_lab_references() {
  std::ifstream input(PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_PATH);
  if (!input) {
    return {};
  }

  std::vector<LabReference> references;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#' || line.rfind("name,", 0) == 0) {
      continue;
    }
    const auto fields = split_csv_line(line);
    if (fields.size() != 11) {
      return {};
    }
    std::array<double, 9> values{};
    auto valid = true;
    for (std::size_t index = 0; index < values.size(); ++index) {
      valid = parse_double(fields[index + 1], &values[index]) && valid;
    }
    if (!valid) {
      return {};
    }
    references.push_back(LabReference{
        .name = fields[0],
        .white = CieXyz{.x = values[0], .y = values[1], .z = values[2]},
        .xyz = CieXyz{.x = values[3], .y = values[4], .z = values[5]},
        .expected = CieLab{
            .lightness = values[6],
            .a = values[7],
            .b = values[8],
        },
        .source = fields[10],
    });
  }
  return references;
}

[[nodiscard]] double max_lab_component_error(const CieLab& left, const CieLab& right) {
  return std::max({
      std::abs(left.lightness - right.lightness),
      std::abs(left.a - right.a),
      std::abs(left.b - right.b),
  });
}

[[nodiscard]] double max_xyz_component_error(const CieXyz& left, const CieXyz& right) {
  return std::max({
      std::abs(left.x - right.x),
      std::abs(left.y - right.y),
      std::abs(left.z - right.z),
  });
}

[[nodiscard]] int fail(std::string_view message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  std::string_view report_path;
  auto expect_no_icc_backend = false;
  for (auto index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--report" && index + 1 < argc) {
      report_path = argv[++index];
    } else if (argument == "--expect-no-icc-backend") {
      expect_no_icc_backend = true;
    } else {
      return fail(
          "usage: color_transform_reference_test [--report output.json] "
          "[--expect-no-icc-backend]");
    }
  }

  const auto icc_capability = photo_editor::pipeline::icc_backend_capability();
  if (expect_no_icc_backend) {
    if (icc_capability.available || icc_capability.backend != "none" ||
        icc_capability.failure_reason != "little_cms_not_compiled") {
      return fail("disabled ICC builds should expose a stable unavailable capability");
    }
    const auto unavailable_transform =
        photo_editor::pipeline::compile_icc_transform({}, {});
    if (unavailable_transform.ready() ||
        unavailable_transform.description().status !=
            photo_editor::pipeline::IccTransformStatus::BackendUnavailable ||
        unavailable_transform.description().failure_reason !=
            "little_cms_not_compiled") {
      return fail("disabled ICC builds should reject transforms without silent fallback");
    }
  } else if (icc_capability.available) {
    if (icc_capability.backend != "Little CMS" ||
        icc_capability.compiled_version.empty() ||
        icc_capability.runtime_version.empty() ||
        icc_capability.validated_version.empty() ||
        !icc_capability.failure_reason.empty()) {
      return fail("enabled ICC builds should expose complete backend provenance");
    }
  } else if (icc_capability.failure_reason != "little_cms_not_compiled") {
    return fail("unavailable ICC builds should expose a stable failure reason");
  }

  if (icc_capability.available) {
    const auto working_space = photo_editor::pipeline::make_linear_prophoto_rgb_color_space();
    const auto first_working_profile =
        photo_editor::pipeline::builtin_rgb_icc_profile_payload(working_space);
    const auto second_working_profile =
        photo_editor::pipeline::builtin_rgb_icc_profile_payload(working_space);
    const auto destination_profile = photo_editor::pipeline::builtin_rgb_icc_profile_payload(
        photo_editor::pipeline::make_srgb_color_space());
    if (first_working_profile.empty() || destination_profile.empty() ||
        photo_editor::pipeline::color_payload_sha256(first_working_profile) !=
            photo_editor::pipeline::color_payload_sha256(second_working_profile)) {
      return fail("built-in ICC payload generation should be non-empty and deterministic");
    }
    const auto generated_transform =
        photo_editor::pipeline::compile_builtin_to_icc_transform(
            working_space,
            destination_profile,
            photo_editor::pipeline::IccTransformOptions{
                .rendering_intent =
                    photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
                .black_point_compensation = true,
                .pixel_format = photo_editor::pipeline::IccPixelFormat::RgbaFloat32,
                .target = "generated-profile-reference",
            });
    std::array<photo_editor::pipeline::IccRgbaFloat, 2> generated_pixels = {
        photo_editor::pipeline::IccRgbaFloat{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.25F},
        photo_editor::pipeline::IccRgbaFloat{.red = 0.25F, .green = 0.25F, .blue = 0.25F, .alpha = 0.75F},
    };
    if (!generated_transform.ready() || !generated_transform.apply(generated_pixels) ||
        generated_transform.description().target != "generated-profile-reference" ||
        generated_transform.description().source_profile_sha256 !=
            photo_editor::pipeline::color_payload_sha256(first_working_profile) ||
        std::abs(generated_pixels[0].alpha - 0.25F) > 1.0e-7F ||
        std::abs(generated_pixels[1].alpha - 0.75F) > 1.0e-7F) {
      return fail("a generated built-in source profile should compile through the production ICC backend");
    }
    const auto reverse_generated_transform =
        photo_editor::pipeline::compile_icc_to_builtin_transform(
            destination_profile,
            working_space,
            photo_editor::pipeline::IccTransformOptions{
                .rendering_intent =
                    photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
                .black_point_compensation = true,
                .pixel_format = photo_editor::pipeline::IccPixelFormat::RgbaFloat32,
                .target = "input-to-working",
            });
    if (!reverse_generated_transform.ready() ||
        reverse_generated_transform.description().target != "input-to-working") {
      return fail("a verified ICC input profile should compile into a built-in linear working space");
    }
  } else if (!photo_editor::pipeline::builtin_rgb_icc_profile_payload(
                  photo_editor::pipeline::make_linear_prophoto_rgb_color_space())
                  .empty()) {
    return fail("disabled ICC builds should not fabricate a generated profile payload");
  }

  const std::array<std::byte, 3> abc = {
      std::byte{'a'},
      std::byte{'b'},
      std::byte{'c'},
  };
  if (photo_editor::pipeline::color_payload_sha256({}) !=
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ||
      photo_editor::pipeline::color_payload_sha256(abc) !=
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
    return fail("profile payload hashing should match published SHA-256 vectors");
  }

  const auto lab_references = load_lab_references();
  if (lab_references.size() < 7) {
    return fail("color reference corpus should load all XYZ/Lab fixtures");
  }

  auto max_lab_error = 0.0;
  auto max_xyz_round_trip_error = 0.0;
  for (const auto& reference : lab_references) {
    const auto actual = photo_editor::pipeline::cie_xyz_to_lab(
        reference.xyz,
        reference.white);
    const auto lab_error = max_lab_component_error(actual, reference.expected);
    max_lab_error = std::max(max_lab_error, lab_error);
    if (lab_error > 0.0015) {
      std::cerr << "XYZ/Lab reference mismatch name=" << reference.name
                << " source=" << reference.source
                << " expected=" << reference.expected.lightness << ','
                << reference.expected.a << ',' << reference.expected.b
                << " actual=" << actual.lightness << ',' << actual.a << ',' << actual.b
                << " maxError=" << lab_error << '\n';
      return EXIT_FAILURE;
    }
    const auto round_trip = photo_editor::pipeline::cie_lab_to_xyz(actual, reference.white);
    const auto xyz_error = max_xyz_component_error(round_trip, reference.xyz);
    max_xyz_round_trip_error = std::max(max_xyz_round_trip_error, xyz_error);
    if (xyz_error > 1.0e-12) {
      std::cerr << "XYZ/Lab round trip mismatch name=" << reference.name
                << " maxError=" << xyz_error << '\n';
      return EXIT_FAILURE;
    }
  }

  // Published supplementary test pairs from Sharma, Wu, and Dalal,
  // "The CIEDE2000 Color-Difference Formula: Implementation Notes,
  // Supplementary Test Data, and Mathematical Observations" (2005).
  const std::vector<DeltaEReference> delta_e_references = {
      {{50.0, 2.6772, -79.7751}, {50.0, 0.0, -82.7485}, 2.0425},
      {{50.0, 3.1571, -77.2803}, {50.0, 0.0, -82.7485}, 2.8615},
      {{50.0, 2.8361, -74.0200}, {50.0, 0.0, -82.7485}, 3.4412},
      {{50.0, -1.3802, -84.2814}, {50.0, 0.0, -82.7485}, 1.0000},
      {{50.0, -1.1848, -84.8006}, {50.0, 0.0, -82.7485}, 1.0000},
      {{50.0, -0.9009, -85.5211}, {50.0, 0.0, -82.7485}, 1.0000},
      {{50.0, 0.0, 0.0}, {50.0, -1.0, 2.0}, 2.3669},
      {{50.0, -1.0, 2.0}, {50.0, 0.0, 0.0}, 2.3669},
      {{50.0, 2.4900, -0.0010}, {50.0, -2.4900, 0.0009}, 7.1792},
      {{50.0, 2.4900, -0.0010}, {50.0, -2.4900, 0.0011}, 7.2195},
      {{50.0, 2.5000, 0.0000}, {50.0, 0.0000, -2.5000}, 4.3065},
      {{50.0, 2.5000, 0.0000}, {73.0, 25.0000, -18.0000}, 27.1492},
      {{50.0, 2.5000, 0.0000}, {61.0, -5.0000, 29.0000}, 22.8977},
      {{50.0, 2.5000, 0.0000}, {56.0, -27.0000, -3.0000}, 31.9030},
      {{50.0, 2.5000, 0.0000}, {58.0, 24.0000, 15.0000}, 19.4535},
      {{50.0, 2.5000, 0.0000}, {50.0, 3.1736, 0.5854}, 1.0000},
      {{50.0, 2.5000, 0.0000}, {50.0, 3.2972, 0.0000}, 1.0000},
      {{50.0, 2.5000, 0.0000}, {50.0, 1.8634, 0.5757}, 1.0000},
      {{50.0, 2.5000, 0.0000}, {50.0, 3.2592, 0.3350}, 1.0000},
  };

  auto max_delta_e_error = 0.0;
  for (const auto& reference : delta_e_references) {
    const auto actual = photo_editor::pipeline::delta_e_2000(
        reference.left,
        reference.right);
    const auto reverse = photo_editor::pipeline::delta_e_2000(
        reference.right,
        reference.left);
    const auto error = std::abs(actual - reference.expected);
    max_delta_e_error = std::max(max_delta_e_error, error);
    if (error > 5.0e-5 || std::abs(actual - reverse) > 1.0e-12) {
      std::cerr << "CIEDE2000 reference mismatch expected=" << reference.expected
                << " actual=" << actual
                << " reverse=" << reverse
                << " error=" << error << '\n';
      return EXIT_FAILURE;
    }
  }

  // Published ICC D65-to-D50 adaptation matrices, evaluated by adapting XYZ
  // basis vectors. Tolerances account for the rounded D50/D65 white values in
  // the ICC reference matrices.
  const std::array<CieXyz, 3> xyz_basis = {
      CieXyz{.x = 1.0, .y = 0.0, .z = 0.0},
      CieXyz{.x = 0.0, .y = 1.0, .z = 0.0},
      CieXyz{.x = 0.0, .y = 0.0, .z = 1.0},
  };
  const std::array<CieXyz, 3> expected_bradford_columns = {
      CieXyz{.x = 1.0478112, .y = 0.0295424, .z = -0.0092345},
      CieXyz{.x = 0.0228866, .y = 0.9904844, .z = 0.0150436},
      CieXyz{.x = -0.0501270, .y = -0.0170491, .z = 0.7521316},
  };
  const std::array<CieXyz, 3> expected_cat02_columns = {
      CieXyz{.x = 1.0425739, .y = 0.0221935, .z = -0.0011649},
      CieXyz{.x = 0.0308911, .y = 1.0018566, .z = -0.0034205},
      CieXyz{.x = -0.0528126, .y = -0.0210738, .z = 0.7617891},
  };
  auto max_bradford_error = 0.0;
  auto max_cat02_error = 0.0;
  for (std::size_t index = 0; index < xyz_basis.size(); ++index) {
    const auto bradford = photo_editor::pipeline::chromatically_adapt_xyz(
        xyz_basis[index],
        photo_editor::pipeline::cie_d65_white(),
        photo_editor::pipeline::cie_d50_white(),
        ChromaticAdaptationMethod::Bradford);
    const auto cat02 = photo_editor::pipeline::chromatically_adapt_xyz(
        xyz_basis[index],
        photo_editor::pipeline::cie_d65_white(),
        photo_editor::pipeline::cie_d50_white(),
        ChromaticAdaptationMethod::Cat02);
    max_bradford_error = std::max(
        max_bradford_error,
        max_xyz_component_error(bradford, expected_bradford_columns[index]));
    max_cat02_error = std::max(
        max_cat02_error,
        max_xyz_component_error(cat02, expected_cat02_columns[index]));
  }
  // CAT02's commonly published rounded matrix uses a slightly different
  // rounded D50 tristimulus, so its conformance tolerance is correspondingly
  // wider than Bradford while remaining below 0.0003 per matrix component.
  if (max_bradford_error > 2.0e-5 || max_cat02_error > 3.0e-4) {
    std::cerr << "chromatic adaptation reference mismatch bradford="
              << max_bradford_error << " cat02=" << max_cat02_error << '\n';
    return EXIT_FAILURE;
  }

  for (const auto method : {
           ChromaticAdaptationMethod::Bradford,
           ChromaticAdaptationMethod::Cat02,
       }) {
    const auto adapted_white = photo_editor::pipeline::chromatically_adapt_xyz(
        photo_editor::pipeline::cie_d65_white(),
        photo_editor::pipeline::cie_d65_white(),
        photo_editor::pipeline::cie_d50_white(),
        method);
    if (max_xyz_component_error(adapted_white, photo_editor::pipeline::cie_d50_white()) >
        1.0e-12) {
      return fail("chromatic adaptation should map the source white onto the target white");
    }
  }

  const auto srgb_matrix = photo_editor::pipeline::rgb_to_xyz_matrix(
      photo_editor::pipeline::make_linear_srgb_color_space());
  const std::array<double, 9> expected_srgb_to_xyz = {
      0.4124564, 0.3575761, 0.1804375,
      0.2126729, 0.7151522, 0.0721750,
      0.0193339, 0.1191920, 0.9503041,
  };
  auto max_srgb_matrix_error = 0.0;
  for (std::size_t index = 0; index < srgb_matrix.values.size(); ++index) {
    max_srgb_matrix_error = std::max(
        max_srgb_matrix_error,
        std::abs(srgb_matrix.values[index] - expected_srgb_to_xyz[index]));
  }
  // The engine uses the CIE D65 chromaticity rounded to five decimal places,
  // while the tabulated IEC matrix embeds a slightly different rounding.
  if (max_srgb_matrix_error > 7.5e-5) {
    std::cerr << "built-in sRGB matrix should match the IEC/CIE reference matrix maxError="
              << max_srgb_matrix_error << '\n';
    return EXIT_FAILURE;
  }

  const auto identity_transform = photo_editor::pipeline::compile_builtin_color_transform(
      photo_editor::pipeline::make_linear_display_p3_color_space(),
      photo_editor::pipeline::make_linear_display_p3_color_space());
  auto max_identity_transform_error = 0.0;
  for (std::size_t index = 0; index < identity_transform.matrix.values.size(); ++index) {
    const auto expected = index % 4 == 0 ? 1.0 : 0.0;
    max_identity_transform_error = std::max(
        max_identity_transform_error,
        std::abs(identity_transform.matrix.values[index] - expected));
  }
  if (max_identity_transform_error > 1.0e-12 ||
      identity_transform.provenance != "builtin-matrix:v1") {
    return fail("compiled built-in identity transform should be exact and versioned");
  }

  if (std::abs(photo_editor::pipeline::st2084_encode_nits(100.0) - 0.5080784215) >
          1.0e-9 ||
      std::abs(photo_editor::pipeline::st2084_encode_nits(1000.0) - 0.7518270962) >
          1.0e-9 ||
      std::abs(photo_editor::pipeline::st2084_encode_nits(10000.0) - 1.0) > 1.0e-12) {
    return fail("ST 2084 encoding should match published reference code values");
  }

  const auto neutral_ictcp = photo_editor::pipeline::rec2020_nits_to_ictcp(
      {.red = 100.0, .green = 100.0, .blue = 100.0});
  if (std::abs(neutral_ictcp.intensity - 0.5080784215) > 1.0e-9 ||
      std::abs(neutral_ictcp.tritan) > 1.0e-12 ||
      std::abs(neutral_ictcp.protan) > 1.0e-12) {
    return fail("neutral Rec.2020 should map to neutral ICtCp at the matching PQ intensity");
  }

  const auto one_delta_e_itp_i = photo_editor::pipeline::delta_e_itp(
      {},
      {.intensity = 1.0 / 720.0, .tritan = 0.0, .protan = 0.0});
  const auto one_delta_e_itp_t = photo_editor::pipeline::delta_e_itp(
      {},
      {.intensity = 0.0, .tritan = 1.0 / 360.0, .protan = 0.0});
  const auto one_delta_e_itp_p = photo_editor::pipeline::delta_e_itp(
      {},
      {.intensity = 0.0, .tritan = 0.0, .protan = 1.0 / 720.0});
  if (std::abs(one_delta_e_itp_i - 1.0) > 1.0e-12 ||
      std::abs(one_delta_e_itp_t - 1.0) > 1.0e-12 ||
      std::abs(one_delta_e_itp_p - 1.0) > 1.0e-12) {
    return fail("Delta E ITP should apply the BT.2124 intensity and chroma weights");
  }

  if (!report_path.empty()) {
    std::ofstream report{std::string(report_path)};
    if (!report) {
      return fail("color reference report path should be writable");
    }
    report << std::fixed << std::setprecision(12)
           << "{\n"
           << "  \"schema\": \"photo-editor-color-reference:v1\",\n"
           << "  \"icc_backend_available\": "
           << (icc_capability.available ? "true" : "false") << ",\n"
           << "  \"icc_backend\": \"" << icc_capability.backend << "\",\n"
           << "  \"icc_compiled_version\": \""
           << icc_capability.compiled_version << "\",\n"
           << "  \"icc_runtime_version\": \""
           << icc_capability.runtime_version << "\",\n"
           << "  \"icc_validated_version\": \""
           << icc_capability.validated_version << "\",\n"
           << "  \"icc_strict_version\": "
           << (icc_capability.strict_version ? "true" : "false") << ",\n"
           << "  \"icc_failure_reason\": \""
           << icc_capability.failure_reason << "\",\n"
           << "  \"sha256_reference_vectors\": 2,\n"
           << "  \"corpus\": \"" << PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_PATH << "\",\n"
           << "  \"corpus_sha256\": \"" << PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_SHA256 << "\",\n"
           << "  \"lab_reference_count\": " << lab_references.size() << ",\n"
           << "  \"ciede2000_reference_count\": " << delta_e_references.size() << ",\n"
           << "  \"max_lab_component_error\": " << max_lab_error << ",\n"
           << "  \"max_xyz_round_trip_error\": " << max_xyz_round_trip_error << ",\n"
           << "  \"max_ciede2000_error\": " << max_delta_e_error << ",\n"
           << "  \"max_bradford_matrix_error\": " << max_bradford_error << ",\n"
           << "  \"max_cat02_matrix_error\": " << max_cat02_error << ",\n"
           << "  \"max_srgb_matrix_error\": " << max_srgb_matrix_error << ",\n"
           << "  \"max_builtin_identity_error\": " << max_identity_transform_error << ",\n"
           << "  \"st2084_reference_vectors\": 3,\n"
           << "  \"delta_e_itp_weight_vectors\": 3\n"
           << "}\n";
  }

  return EXIT_SUCCESS;
}

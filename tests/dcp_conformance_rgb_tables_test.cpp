#include "photo_editor/pipeline/dcp_profile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::string> split_csv_row(const std::string& row) {
  auto fields = std::vector<std::string>{};
  auto stream = std::istringstream(row);
  for (auto field = std::string{}; std::getline(stream, field, ',');) {
    fields.push_back(std::move(field));
  }
  return fields;
}

[[nodiscard]] bool parse_number(std::string_view text, double* value) {
  if (value == nullptr || text.empty()) {
    return false;
  }
  try {
    std::size_t parsed = 0U;
    const auto result = std::stod(std::string(text), &parsed);
    if (parsed != text.size() || !std::isfinite(result)) {
      return false;
    }
    *value = result;
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] photo_editor::pipeline::DcpRgbTable make_table(
    std::string semantic_name,
    photo_editor::pipeline::DcpRgbTableGamma gamma,
    bool gamut_extension,
    bool swap_red_blue) {
  auto table = photo_editor::pipeline::DcpRgbTable{
      .semantic_name = std::move(semantic_name),
      .divisions = 2U,
      .pixel_type = photo_editor::pipeline::DcpRgbTablePixelType::Float32,
      .gamma = gamma,
      .primaries = photo_editor::pipeline::DcpRgbTablePrimaries::ProPhotoRgb,
      .gamut_extension = gamut_extension,
  };
  table.entries.reserve(8U);
  for (auto red = 0U; red < 2U; ++red) {
    for (auto green = 0U; green < 2U; ++green) {
      for (auto blue = 0U; blue < 2U; ++blue) {
        table.entries.push_back(swap_red_blue
                                    ? std::array{
                                          static_cast<float>(blue),
                                          static_cast<float>(green),
                                          static_cast<float>(red),
                                      }
                                    : std::array{
                                          static_cast<float>(green),
                                          static_cast<float>(red),
                                          static_cast<float>(blue),
                                      });
      }
    }
  }
  return table;
}

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  constexpr auto kExpectedHeader =
      "case_id,composite,gamma,hdr,gamut_extension,mask_weight,"
      "input_r,input_g,input_b,output_r,output_g,output_b";
  auto stream = std::ifstream(PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_CSV);
  auto line = std::string{};
  if (!stream.is_open() || !std::getline(stream, line) ||
      line != kExpectedHeader) {
    return fail("DNG conformance RGBTables fixture is missing or has the wrong schema");
  }

  auto case_count = std::size_t{0U};
  auto maximum_error = 0.0;
  auto worst_case = std::string{};
  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }
    const auto fields = split_csv_row(line);
    if (fields.size() != 12U || fields[0].empty() ||
        (fields[1] != "weighted" && fields[1] != "sequential")) {
      return fail("DNG conformance RGBTables fixture contains a malformed row");
    }
    auto values = std::array<double, 10>{};
    for (std::size_t index = 0U; index < values.size(); ++index) {
      if (!parse_number(fields[index + 2U], &values[index])) {
        return fail("DNG conformance RGBTables fixture contains a non-finite value");
      }
    }
    if (values[0] < 0.0 || values[0] > 4.0 ||
        (values[1] != 0.0 && values[1] != 1.0) ||
        (values[2] != 0.0 && values[2] != 1.0)) {
      return fail("DNG conformance RGBTables fixture contains invalid metadata");
    }
    const auto gamma = static_cast<photo_editor::pipeline::DcpRgbTableGamma>(
        static_cast<std::uint8_t>(values[0]));
    auto profile = photo_editor::pipeline::DcpProfile{};
    profile.rgb_tables.composite_method =
        fields[1] == "sequential"
            ? photo_editor::pipeline::DcpRgbTableCompositeMethod::Sequential
            : photo_editor::pipeline::DcpRgbTableCompositeMethod::WeightedSum;
    profile.rgb_tables.tables.push_back(make_table(
        "subject",
        gamma,
        values[2] == 1.0,
        true));
    profile.rgb_tables.tables.push_back(make_table(
        {},
        gamma,
        values[2] == 1.0,
        false));
    if (values[1] == 1.0) {
      profile.dynamic_range = photo_editor::pipeline::DcpProfileDynamicRange{
          .hdr = true,
          .hint_max_output_value = 8.0F,
      };
    }
    const std::array masks = {
        photo_editor::pipeline::DcpSemanticMaskWeight{
            .semantic_name = "subject",
            .weight = values[3],
        },
    };
    const auto output = photo_editor::pipeline::dcp_apply_rgb_tables(
        profile,
        {values[4], values[5], values[6]},
        masks);
    const auto error = std::max({
        std::abs(output.red - values[7]),
        std::abs(output.green - values[8]),
        std::abs(output.blue - values[9]),
    });
    if (error > maximum_error) {
      maximum_error = error;
      worst_case = fields[0];
    }
    ++case_count;
  }

  std::cout << "DNG conformance RGBTables parity: cases=" << case_count
            << " max_rgb_error=" << maximum_error
            << " worst_case=" << worst_case << '\n';
  if (case_count != 50U || maximum_error > 2.0e-7) {
    return fail("engine RGBTables stage exceeds the independent DNG reference tolerance");
  }
  return EXIT_SUCCESS;
}

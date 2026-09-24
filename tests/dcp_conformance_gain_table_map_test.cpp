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

[[nodiscard]] photo_editor::pipeline::DcpProfile reference_profile() {
  auto profile = photo_editor::pipeline::DcpProfile{};
  auto& map = profile.profile_gain_table_map;
  map.points_v = 2U;
  map.points_h = 2U;
  map.spacing_v = 0.75;
  map.spacing_h = 0.75;
  map.origin_v = 0.10;
  map.origin_h = 0.05;
  map.table_points = 4U;
  map.input_weights = {0.25F, 0.50F, 0.10F, 0.05F, 0.10F};
  map.gamma = 0.8F;
  map.gains = {
      0.8F, 1.0F, 1.2F, 1.4F,
      1.1F, 1.3F, 1.5F, 1.7F,
      0.9F, 1.15F, 1.35F, 1.6F,
      1.2F, 1.4F, 1.8F, 2.0F,
  };
  return profile;
}

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  constexpr auto kExpectedHeader =
      "case_id,normalized_x,normalized_y,exposure_gain,input_r,input_g,input_b,"
      "output_r,output_g,output_b";
  auto stream = std::ifstream(PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_CSV);
  auto line = std::string{};
  if (!stream.is_open() || !std::getline(stream, line) ||
      line != kExpectedHeader) {
    return fail("DNG conformance gain-table fixture is missing or has the wrong schema");
  }
  const auto profile = reference_profile();
  if (!profile.profile_gain_table_map.configured()) {
    return fail("test ProfileGainTableMap2 is invalid");
  }
  auto case_count = std::size_t{0U};
  auto maximum_error = 0.0;
  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }
    const auto fields = split_csv_row(line);
    if (fields.size() != 10U || fields[0].empty()) {
      return fail("DNG conformance gain-table fixture contains a malformed row");
    }
    auto values = std::array<double, 9>{};
    for (std::size_t index = 0U; index < values.size(); ++index) {
      if (!parse_number(fields[index + 1U], &values[index])) {
        return fail("DNG conformance gain-table fixture contains a non-finite value");
      }
    }
    const auto output = photo_editor::pipeline::dcp_apply_profile_gain_table_map(
        profile,
        values[0],
        values[1],
        values[2],
        {values[3], values[4], values[5]});
    maximum_error = std::max({
        maximum_error,
        std::abs(output.red - values[6]),
        std::abs(output.green - values[7]),
        std::abs(output.blue - values[8]),
    });
    ++case_count;
  }
  std::cout << "DNG conformance ProfileGainTableMap2 parity: cases=" << case_count
            << " max_rgb_error=" << maximum_error << '\n';
  if (case_count != 6U || maximum_error > 2.0e-7) {
    return fail("engine ProfileGainTableMap2 exceeds the independent DNG reference tolerance");
  }
  return EXIT_SUCCESS;
}

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

[[nodiscard]] photo_editor::pipeline::DcpHsvTable reference_table(
    bool srgb_value_encoding,
    int variant = 0) {
  auto table = photo_editor::pipeline::DcpHsvTable{};
  table.dimensions = {6U, 3U, 2U};
  table.srgb_value_encoding = srgb_value_encoding;
  table.entries.reserve(36U);
  for (auto value = std::uint32_t{0U}; value < 2U; ++value) {
    for (auto hue = std::uint32_t{0U}; hue < 6U; ++hue) {
      for (auto saturation = std::uint32_t{0U}; saturation < 3U; ++saturation) {
        table.entries.push_back(photo_editor::pipeline::DcpHsvDelta{
            .hue_shift_degrees =
                (static_cast<float>(hue) - 2.5F) * 1.75F +
                static_cast<float>(saturation) * 0.5F +
                static_cast<float>(value) * 0.25F +
                static_cast<float>(variant) * 0.6F,
            .saturation_scale =
                0.82F + static_cast<float>(hue) * 0.04F +
                static_cast<float>(saturation) * 0.07F +
                static_cast<float>(value) * 0.03F +
                static_cast<float>(variant) * 0.025F,
            .value_scale =
                saturation == 0U
                    ? 1.0F
                    : 0.91F + static_cast<float>(hue) * 0.02F +
                          static_cast<float>(saturation) * 0.025F +
                          static_cast<float>(value) * 0.04F +
                          static_cast<float>(variant) * 0.015F,
        });
      }
    }
  }
  return table;
}

[[nodiscard]] photo_editor::pipeline::DcpProfile reference_profile() {
  auto profile = photo_editor::pipeline::DcpProfile{};
  profile.hue_sat_map1 = reference_table(false);
  profile.look_table = reference_table(false);
  profile.tone_curve = {
      {0.0F, 0.0F},
      {0.2F, 0.1F},
      {0.5F, 0.65F},
      {0.8F, 0.9F},
      {1.0F, 1.0F},
  };
  return profile;
}

[[nodiscard]] photo_editor::pipeline::DcpProfile triple_reference_profile() {
  auto profile = reference_profile();
  profile.calibration_illuminant1 = 17U;
  profile.calibration_illuminant2 = 21U;
  profile.calibration_illuminant3 = 23U;
  const auto identity = std::array<double, 9>{
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  profile.color_matrix1 = identity;
  profile.color_matrix2 = identity;
  profile.color_matrix3 = identity;
  profile.hue_sat_map1 = reference_table(false, 0);
  profile.hue_sat_map2 = reference_table(false, 1);
  profile.hue_sat_map3 = reference_table(false, 2);
  return profile;
}

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  constexpr auto kExpectedHeader =
      "stage,case_id,input_r,input_g,input_b,output_r,output_g,output_b";
  auto stream = std::ifstream(PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_CSV);
  auto line = std::string{};
  if (!stream.is_open() || !std::getline(stream, line) ||
      line != kExpectedHeader) {
    return fail("DNG conformance profile-stage fixture is missing or has the wrong schema");
  }

  auto case_count = std::size_t{0U};
  auto maximum_error = 0.0;
  auto worst_case = std::string{};
  auto worst_actual = photo_editor::pipeline::DcpRgb{};
  auto worst_expected = photo_editor::pipeline::DcpRgb{};
  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }
    const auto fields = split_csv_row(line);
    if (fields.size() != 8U || fields[0].empty()) {
      return fail("DNG conformance profile-stage fixture contains a malformed row");
    }
    auto values = std::array<double, 7>{};
    for (std::size_t index = 0U; index < values.size(); ++index) {
      if (!parse_number(fields[index + 1U], &values[index])) {
        return fail("DNG conformance profile-stage fixture contains a non-finite value");
      }
    }
    auto profile = reference_profile();
    const auto input = photo_editor::pipeline::DcpRgb{
        .red = values[1],
        .green = values[2],
        .blue = values[3],
    };
    auto output = input;
    if (fields[0] == "hue-sat-linear" || fields[0] == "hue-sat-srgb") {
      profile.hue_sat_map1.srgb_value_encoding = fields[0] == "hue-sat-srgb";
      output = photo_editor::pipeline::dcp_apply_hue_sat_map(
          profile,
          5500.0,
          input);
    } else if (fields[0] == "look-linear" || fields[0] == "look-srgb") {
      profile.look_table.srgb_value_encoding = fields[0] == "look-srgb";
      output = photo_editor::pipeline::dcp_apply_look_table(profile, input);
    } else if (fields[0] == "tone") {
      output = photo_editor::pipeline::dcp_apply_profile_tone_curve(profile, input);
    } else if (fields[0].starts_with("hue-sat-triple-")) {
      profile = triple_reference_profile();
      const auto white_index = fields[0].back() - '0';
      constexpr std::array<std::array<double, 2>, 3> kTripleWhites{{
          {3200.0, -20.0},
          {5000.0, 12.0},
          {7000.0, 30.0},
      }};
      if (white_index < 0 || white_index >= static_cast<int>(kTripleWhites.size())) {
        return fail("DNG conformance profile-stage fixture has an invalid triple white index");
      }
      output = photo_editor::pipeline::dcp_apply_hue_sat_map(
          profile,
          kTripleWhites[static_cast<std::size_t>(white_index)][0],
          kTripleWhites[static_cast<std::size_t>(white_index)][1],
          input);
    } else {
      return fail("DNG conformance profile-stage fixture contains an unknown stage");
    }
    const auto case_error = std::max({
        std::abs(output.red - values[4]),
        std::abs(output.green - values[5]),
        std::abs(output.blue - values[6]),
    });
    if (case_error > maximum_error) {
      maximum_error = case_error;
      worst_case = fields[0] + ":" + fields[1];
      worst_actual = output;
      worst_expected = {
          .red = values[4],
          .green = values[5],
          .blue = values[6],
      };
    }
    ++case_count;
  }

  std::cout << "DNG conformance profile-stage parity: cases=" << case_count
            << " max_rgb_error=" << maximum_error
            << " worst_case=" << worst_case
            << " actual=" << worst_actual.red << ',' << worst_actual.green << ','
            << worst_actual.blue << " expected=" << worst_expected.red << ','
            << worst_expected.green << ',' << worst_expected.blue << '\n';
  if (case_count != 72U) {
    return fail("DNG conformance profile-stage fixture must contain 72 cases");
  }
  if (maximum_error > 2.0e-6) {
    return fail("engine profile stage exceeds the independent DNG reference tolerance");
  }
  return EXIT_SUCCESS;
}

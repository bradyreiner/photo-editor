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
  profile.calibration_illuminant1 = 17U;
  profile.calibration_illuminant2 = 21U;
  profile.color_matrix1 = std::array<double, 9>{
      1.0, 0.012, 0.012,
      0.012, 1.0, 0.012,
      0.012, 0.012, 1.0,
  };
  profile.color_matrix2 = std::array<double, 9>{
      1.0, -0.008, -0.008,
      -0.008, 1.0, -0.008,
      -0.008, -0.008, 1.0,
  };
  profile.camera_calibration1 = std::array<double, 9>{
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  profile.camera_calibration2 = profile.camera_calibration1;
  profile.forward_matrix1 = std::array<double, 9>{
      0.9, 0.0321, 0.0321,
      0.02, 0.96, 0.02,
      0.0126, 0.0126, 0.8,
  };
  profile.forward_matrix2 = std::array<double, 9>{
      0.94, 0.0121, 0.0121,
      0.01, 0.98, 0.01,
      0.0051, 0.0051, 0.815,
  };
  profile.analog_balance = std::array<double, 3>{1.0, 1.0, 1.0};
  profile.camera_calibration_signature = "shared-calibration";
  profile.profile_calibration_signature = "shared-calibration";
  return profile;
}

[[nodiscard]] photo_editor::pipeline::DcpProfile custom_xy_reference_profile() {
  auto profile = reference_profile();
  profile.calibration_illuminant1 = 255U;
  profile.illuminant_data1 = photo_editor::pipeline::DcpChromaticity{
      .x = 0.3457,
      .y = 0.3585,
  };
  return profile;
}

[[nodiscard]] photo_editor::pipeline::DcpProfile triple_reference_profile() {
  auto profile = reference_profile();
  profile.calibration_illuminant3 = 23U;
  profile.color_matrix3 = std::array<double, 9>{
      1.0, 0.003, 0.003,
      0.003, 1.0, 0.003,
      0.003, 0.003, 1.0,
  };
  profile.camera_calibration3 = profile.camera_calibration1;
  profile.forward_matrix3 = std::array<double, 9>{
      0.92, 0.0221, 0.0221,
      0.015, 0.97, 0.015,
      0.0096, 0.0096, 0.806,
  };
  return profile;
}

[[nodiscard]] photo_editor::pipeline::DcpProfile spectrum_reference_profile(
    double calibration_white_x,
    double calibration_white_y) {
  auto profile = reference_profile();
  profile.calibration_illuminant1 = 255U;
  profile.illuminant_data1 = photo_editor::pipeline::DcpChromaticity{
      .x = calibration_white_x,
      .y = calibration_white_y,
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
      "case_id,profile_model,temperature_kelvin,tint,white_x,white_y,"
      "calibration_white_x,calibration_white_y,"
      "calibration_temperature_kelvin,calibration_tint,"
      "camera_white_r,camera_white_g,camera_white_b,"
      "camera_to_xyz_d50_0_0,camera_to_xyz_d50_0_1,"
      "camera_to_xyz_d50_0_2,camera_to_xyz_d50_1_0,"
      "camera_to_xyz_d50_1_1,camera_to_xyz_d50_1_2,"
      "camera_to_xyz_d50_2_0,camera_to_xyz_d50_2_1,"
      "camera_to_xyz_d50_2_2";
  auto stream = std::ifstream(PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_CSV);
  auto line = std::string{};
  if (!stream.is_open() || !std::getline(stream, line) ||
      line != kExpectedHeader) {
    return fail("DNG conformance camera-transform fixture is missing or has the wrong schema");
  }

  auto case_count = std::size_t{0U};
  auto maximum_white_error = 0.0;
  auto maximum_matrix_error = 0.0;
  auto maximum_custom_calibration_temperature_error = 0.0;
  auto worst_case = std::string{};
  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }
    const auto fields = split_csv_row(line);
    if (fields.size() != 22U || fields[0].empty()) {
      return fail("DNG conformance camera-transform fixture contains a malformed row");
    }
    auto values = std::array<double, 20>{};
    for (std::size_t index = 0U; index < values.size(); ++index) {
      if (!parse_number(fields[index + 2U], &values[index])) {
        return fail("DNG conformance camera-transform fixture contains a non-finite value");
      }
    }
    const auto profile = fields[1] == "standard"
                             ? reference_profile()
                             : fields[1] == "custom-xy"
                                   ? custom_xy_reference_profile()
                                   : fields[1] == "triple"
                                         ? triple_reference_profile()
                                   : fields[1] == "spectrum"
                                         ? spectrum_reference_profile(values[4], values[5])
                                   : photo_editor::pipeline::DcpProfile{};
    if (fields[1] != "standard" && fields[1] != "custom-xy" &&
        fields[1] != "triple" && fields[1] != "spectrum") {
      return fail("DNG conformance camera-transform fixture contains an unknown profile model");
    }
    const auto result = photo_editor::pipeline::dcp_camera_to_xyz_d50(
        profile, values[0], values[1]);
    if (!result.valid || !result.used_forward_matrix ||
        !result.used_camera_calibration) {
      return fail("engine failed to compile an independent DNG reference camera transform");
    }
    if (fields[1] == "custom-xy" || fields[1] == "spectrum") {
      const auto calibration_temperature =
          photo_editor::pipeline::dcp_profile_calibration_illuminant_temperature(
              profile,
              0U);
      if (!calibration_temperature.has_value()) {
        return fail("engine failed to resolve a custom DNG calibration illuminant");
      }
      maximum_custom_calibration_temperature_error = std::max(
          maximum_custom_calibration_temperature_error,
          std::abs(*calibration_temperature - values[6]));
    }
    maximum_white_error = std::max({
        maximum_white_error,
        std::abs(result.white.x - values[2]),
        std::abs(result.white.y - values[3]),
    });
    const auto camera_white_maximum =
        std::max({values[8], values[9], values[10]});
    if (std::abs(camera_white_maximum - 1.0) > 1.0e-12) {
      return fail("DNG conformance fixture camera white is not normalized");
    }
    for (std::size_t index = 0U; index < result.camera_to_xyz_d50.size(); ++index) {
      const auto error =
          std::abs(result.camera_to_xyz_d50[index] - values[index + 11U]);
      if (error > maximum_matrix_error) {
        maximum_matrix_error = error;
        worst_case = fields[0];
      }
    }
    ++case_count;
  }

  std::cout << "DNG conformance camera transform parity: cases=" << case_count
            << " max_white_xy_error=" << maximum_white_error
            << " max_custom_calibration_temperature_error="
            << maximum_custom_calibration_temperature_error
            << " max_matrix_error=" << maximum_matrix_error
            << " worst_case=" << worst_case << '\n';
  if (case_count != 11U) {
    return fail("DNG conformance camera-transform fixture must contain eleven cases");
  }
  if (maximum_white_error > 1.0e-12 ||
      maximum_custom_calibration_temperature_error > 1.0e-3 ||
      maximum_matrix_error > 2.0e-8) {
    return fail("engine camera transform exceeds the independent DNG reference tolerance");
  }
  return EXIT_SUCCESS;
}

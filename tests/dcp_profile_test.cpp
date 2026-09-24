#include "photo_editor/pipeline/dcp_profile.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

enum class ByteOrder {
  Little,
  Big,
};

struct Tag final {
  std::uint16_t id = 0U;
  std::uint16_t type = 0U;
  std::uint32_t count = 0U;
  std::vector<std::byte> data;
};

void append_u16(std::vector<std::byte>* output, std::uint16_t value, ByteOrder order) {
  if (order == ByteOrder::Little) {
    output->push_back(static_cast<std::byte>(value & 0xFFU));
    output->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
  } else {
    output->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    output->push_back(static_cast<std::byte>(value & 0xFFU));
  }
}

void append_u32(std::vector<std::byte>* output, std::uint32_t value, ByteOrder order) {
  if (order == ByteOrder::Little) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
      output->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
  } else {
    for (unsigned int shift = 32U; shift > 0U; shift -= 8U) {
      output->push_back(static_cast<std::byte>((value >> (shift - 8U)) & 0xFFU));
    }
  }
}

void append_u64(std::vector<std::byte>* output, std::uint64_t value, ByteOrder order) {
  if (order == ByteOrder::Little) {
    for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
      output->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
  } else {
    for (unsigned int shift = 64U; shift > 0U; shift -= 8U) {
      output->push_back(static_cast<std::byte>((value >> (shift - 8U)) & 0xFFU));
    }
  }
}

std::vector<std::byte> shorts(
    std::initializer_list<std::uint16_t> values,
    ByteOrder order) {
  std::vector<std::byte> result;
  for (const auto value : values) {
    append_u16(&result, value, order);
  }
  return result;
}

std::vector<std::byte> longs(
    std::initializer_list<std::uint32_t> values,
    ByteOrder order) {
  std::vector<std::byte> result;
  for (const auto value : values) {
    append_u32(&result, value, order);
  }
  return result;
}

std::vector<std::byte> rationals(
    std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> values,
    ByteOrder order) {
  std::vector<std::byte> result;
  for (const auto [numerator, denominator] : values) {
    append_u32(&result, numerator, order);
    append_u32(&result, denominator, order);
  }
  return result;
}

std::vector<std::byte> signed_rationals(
    const std::vector<std::pair<std::int32_t, std::int32_t>>& values,
    ByteOrder order) {
  std::vector<std::byte> result;
  for (const auto [numerator, denominator] : values) {
    append_u32(&result, std::bit_cast<std::uint32_t>(numerator), order);
    append_u32(&result, std::bit_cast<std::uint32_t>(denominator), order);
  }
  return result;
}

std::vector<std::byte> floats(
    std::initializer_list<float> values,
    ByteOrder order) {
  std::vector<std::byte> result;
  for (const auto value : values) {
    append_u32(&result, std::bit_cast<std::uint32_t>(value), order);
  }
  return result;
}

std::vector<std::byte> text(std::string value) {
  std::vector<std::byte> result;
  result.reserve(value.size() + 1U);
  for (const auto character : value) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  result.push_back(std::byte{0});
  return result;
}

std::vector<std::byte> illuminant_xy_data(
    std::uint32_t x_numerator,
    std::uint32_t x_denominator,
    std::uint32_t y_numerator,
    std::uint32_t y_denominator,
    ByteOrder order) {
  auto result = std::vector<std::byte>{};
  append_u16(&result, 0U, order);
  append_u32(&result, x_numerator, order);
  append_u32(&result, x_denominator, order);
  append_u32(&result, y_numerator, order);
  append_u32(&result, y_denominator, order);
  return result;
}

std::vector<std::byte> illuminant_spectrum_data(
    std::pair<std::uint32_t, std::uint32_t> minimum_wavelength,
    std::pair<std::uint32_t, std::uint32_t> wavelength_spacing,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& samples,
    ByteOrder order) {
  auto result = std::vector<std::byte>{};
  append_u16(&result, 1U, order);
  append_u32(&result, static_cast<std::uint32_t>(samples.size()), order);
  append_u32(&result, minimum_wavelength.first, order);
  append_u32(&result, minimum_wavelength.second, order);
  append_u32(&result, wavelength_spacing.first, order);
  append_u32(&result, wavelength_spacing.second, order);
  for (const auto [numerator, denominator] : samples) {
    append_u32(&result, numerator, order);
    append_u32(&result, denominator, order);
  }
  return result;
}

std::vector<std::byte> profile_gain_table_map2_data(
    ByteOrder order,
    float gamma = 0.8F,
    std::uint32_t data_type = 3U) {
  auto result = std::vector<std::byte>{};
  append_u32(&result, 2U, order);
  append_u32(&result, 2U, order);
  append_u64(&result, std::bit_cast<std::uint64_t>(0.75), order);
  append_u64(&result, std::bit_cast<std::uint64_t>(0.75), order);
  append_u64(&result, std::bit_cast<std::uint64_t>(0.10), order);
  append_u64(&result, std::bit_cast<std::uint64_t>(0.05), order);
  append_u32(&result, 4U, order);
  for (const auto weight : {0.25F, 0.50F, 0.10F, 0.05F, 0.10F}) {
    append_u32(&result, std::bit_cast<std::uint32_t>(weight), order);
  }
  append_u32(&result, data_type, order);
  append_u32(&result, std::bit_cast<std::uint32_t>(gamma), order);
  append_u32(&result, std::bit_cast<std::uint32_t>(0.5F), order);
  append_u32(&result, std::bit_cast<std::uint32_t>(2.5F), order);
  constexpr std::array gains = {
           0.8F, 1.0F, 1.2F, 1.4F,
           1.1F, 1.3F, 1.5F, 1.7F,
           0.9F, 1.15F, 1.35F, 1.6F,
           1.2F, 1.4F, 1.8F, 2.0F,
  };
  for (std::size_t index = 0U; index < gains.size(); ++index) {
    if (data_type == 0U) {
      result.push_back(static_cast<std::byte>(index * 17U));
    } else if (data_type == 1U) {
      append_u16(
          &result,
          static_cast<std::uint16_t>(index * 4369U),
          order);
    } else if (data_type == 2U) {
      append_u16(&result, 0x3C00U, order);
    } else {
      append_u32(
          &result,
          std::bit_cast<std::uint32_t>(gains[index]),
          order);
    }
  }
  return result;
}

void append_rgb_table_sample(
    std::vector<std::byte>* output,
    float value,
    std::uint8_t pixel_type,
    ByteOrder order) {
  if (pixel_type == 0U) {
    output->push_back(static_cast<std::byte>(
        static_cast<std::uint8_t>(std::lround(value * 255.0F))));
  } else if (pixel_type == 1U) {
    append_u16(
        output,
        static_cast<std::uint16_t>(std::lround(value * 65535.0F)),
        order);
  } else {
    append_u32(output, std::bit_cast<std::uint32_t>(value), order);
  }
}

void append_rgb_table(
    std::vector<std::byte>* output,
    std::string_view semantic_name,
    std::uint8_t pixel_type,
    std::uint8_t gamma,
    std::uint8_t primaries,
    bool gamut_extension,
    bool background_transform,
    ByteOrder order) {
  append_u16(
      output,
      static_cast<std::uint16_t>(semantic_name.size()),
      order);
  for (const auto character : semantic_name) {
    output->push_back(
        static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  output->push_back(std::byte{2});
  output->push_back(static_cast<std::byte>(pixel_type));
  output->push_back(static_cast<std::byte>(gamma));
  output->push_back(static_cast<std::byte>(primaries));
  output->push_back(gamut_extension ? std::byte{1} : std::byte{0});
  for (std::uint8_t red = 0U; red < 2U; ++red) {
    for (std::uint8_t green = 0U; green < 2U; ++green) {
      for (std::uint8_t blue = 0U; blue < 2U; ++blue) {
        const auto output_red = background_transform
                                    ? static_cast<float>(red)
                                    : static_cast<float>(blue);
        const auto output_green = background_transform
                                      ? static_cast<float>(green) * 0.5F
                                      : static_cast<float>(green);
        const auto output_blue = background_transform
                                     ? static_cast<float>(blue)
                                     : static_cast<float>(red);
        append_rgb_table_sample(output, output_red, pixel_type, order);
        append_rgb_table_sample(output, output_green, pixel_type, order);
        append_rgb_table_sample(output, output_blue, pixel_type, order);
      }
    }
  }
}

std::vector<std::byte> rgb_tables_data(
    ByteOrder order,
    std::uint32_t composite_method = 0U,
    std::uint8_t pixel_type = 2U,
    std::uint8_t gamma = 0U,
    std::uint8_t primaries = 4U,
    bool gamut_extension = true) {
  auto result = std::vector<std::byte>{};
  append_u32(&result, 2U, order);
  append_u32(&result, composite_method, order);
  append_rgb_table(
      &result,
      "subject",
      pixel_type,
      gamma,
      primaries,
      gamut_extension,
      false,
      order);
  append_rgb_table(
      &result,
      {},
      pixel_type,
      gamma,
      primaries,
      gamut_extension,
      true,
      order);
  return result;
}

std::vector<std::byte> profile_dynamic_range_data(
    ByteOrder order,
    std::uint16_t version = 1U,
    std::uint16_t dynamic_range = 1U,
    float hint_max_output_value = 4.0F) {
  auto result = std::vector<std::byte>{};
  append_u16(&result, version, order);
  append_u16(&result, dynamic_range, order);
  append_u32(
      &result,
      std::bit_cast<std::uint32_t>(hint_max_output_value),
      order);
  return result;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> reference_spectrum() {
  auto result = std::vector<std::pair<std::uint32_t, std::uint32_t>>{};
  for (const auto value : {
           3500U, 4100U, 5200U, 6500U, 7800U, 9000U, 9800U, 10000U,
           9700U, 9200U, 8800U, 8500U, 8300U, 8200U, 8100U, 8000U,
           7800U, 7500U, 7200U, 6900U, 6500U, 6100U, 5700U,
       }) {
    result.emplace_back(value, 10000U);
  }
  return result;
}

std::vector<std::pair<std::int32_t, std::int32_t>> matrix_values(
    std::int32_t diagonal,
    std::int32_t off_diagonal = 0) {
  std::vector<std::pair<std::int32_t, std::int32_t>> values;
  values.reserve(9U);
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      values.emplace_back(row == column ? diagonal : off_diagonal, 10000);
    }
  }
  return values;
}

std::vector<std::pair<std::int32_t, std::int32_t>> forward_matrix_values(
    bool second) {
  if (second) {
    return {
        {9400, 10000}, {121, 10000}, {121, 10000},
        {100, 10000}, {9800, 10000}, {100, 10000},
        {51, 10000}, {51, 10000}, {8150, 10000},
    };
  }
  return {
      {9000, 10000}, {321, 10000}, {321, 10000},
      {200, 10000}, {9600, 10000}, {200, 10000},
      {126, 10000}, {126, 10000}, {8000, 10000},
  };
}

std::vector<std::byte> make_tiff(
    const std::vector<Tag>& tags,
    ByteOrder order,
    std::uint16_t magic = 0x4352U,
    std::uint32_t next_ifd_offset = 0U) {
  std::vector<std::byte> result;
  result.push_back(order == ByteOrder::Little ? std::byte{'I'} : std::byte{'M'});
  result.push_back(order == ByteOrder::Little ? std::byte{'I'} : std::byte{'M'});
  append_u16(&result, magic, order);
  append_u32(&result, 8U, order);
  append_u16(&result, static_cast<std::uint16_t>(tags.size()), order);

  const auto external_start = 8U + 2U + tags.size() * 12U + 4U;
  std::vector<std::byte> external;
  for (const auto& tag : tags) {
    append_u16(&result, tag.id, order);
    append_u16(&result, tag.type, order);
    append_u32(&result, tag.count, order);
    if (tag.data.size() <= 4U) {
      result.insert(result.end(), tag.data.begin(), tag.data.end());
      for (std::size_t padding = tag.data.size(); padding < 4U; ++padding) {
        result.push_back(std::byte{0});
      }
    } else {
      append_u32(
          &result,
          static_cast<std::uint32_t>(external_start + external.size()),
          order);
      external.insert(external.end(), tag.data.begin(), tag.data.end());
    }
  }
  append_u32(&result, next_ifd_offset, order);
  result.insert(result.end(), external.begin(), external.end());
  return result;
}

std::vector<Tag> valid_tags(ByteOrder order) {
  return {
      {50708U, 2U, 15U, text("Fixture Camera")},
      {50778U, 3U, 1U, shorts({17U}, order)},
      {50779U, 3U, 1U, shorts({21U}, order)},
      {50721U, 10U, 9U, signed_rationals(matrix_values(10000, 120), order)},
      {50722U, 10U, 9U, signed_rationals(matrix_values(10000, -80), order)},
      {50723U, 10U, 9U, signed_rationals(matrix_values(10000), order)},
      {50724U, 10U, 9U, signed_rationals(matrix_values(10000), order)},
      {50725U, 10U, 9U, signed_rationals(matrix_values(10000), order)},
      {50726U, 10U, 9U, signed_rationals(matrix_values(10000), order)},
      {50727U, 5U, 3U, rationals({{1U, 1U}, {1U, 1U}, {1U, 1U}}, order)},
      {50728U, 5U, 3U, rationals({{2U, 1U}, {1U, 1U}, {3U, 2U}}, order)},
      {50730U, 10U, 1U, signed_rationals({{-1, 2}}, order)},
      {50931U, 2U, 10U, text("camera-v1")},
      {50932U, 2U, 11U, text("profile-v1")},
      {50936U, 2U, 17U, text("Fixture Dual DCP")},
      {50937U, 4U, 3U, longs({1U, 2U, 1U}, order)},
      {50938U, 11U, 6U, floats({0.0F, 1.0F, 1.0F, 2.0F, 1.1F, 0.95F}, order)},
      {50939U, 11U, 6U, floats({0.0F, 1.0F, 1.0F, -2.0F, 0.9F, 1.05F}, order)},
      {50940U, 11U, 4U, floats({0.0F, 0.0F, 1.0F, 1.0F}, order)},
      {50941U, 4U, 1U, longs({3U}, order)},
      {50942U, 2U, 18U, text("Fixture copyright")},
      {50964U, 10U, 9U, signed_rationals(forward_matrix_values(false), order)},
      {50965U, 10U, 9U, signed_rationals(forward_matrix_values(true), order)},
      {50981U, 4U, 3U, longs({1U, 2U, 1U}, order)},
      {50982U, 11U, 6U, floats({0.0F, 1.0F, 1.0F, 1.0F, 1.05F, 1.0F}, order)},
      {51107U, 4U, 1U, longs({1U}, order)},
      {51108U, 4U, 1U, longs({0U}, order)},
  };
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool nearly_equal(double left, double right, double tolerance = 1e-9) {
  return std::abs(left - right) <= tolerance;
}

std::array<double, 3> multiply_matrix_vector(
    const std::array<double, 9>& matrix,
    const std::array<double, 3>& vector) {
  return {
      matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2],
      matrix[3] * vector[0] + matrix[4] * vector[1] + matrix[5] * vector[2],
      matrix[6] * vector[0] + matrix[7] * vector[1] + matrix[8] * vector[2],
  };
}

}  // namespace

int main(int argc, char** argv) {
  using photo_editor::pipeline::DcpIlluminantDataKind;
  using photo_editor::pipeline::DcpParseStatus;
  using photo_editor::pipeline::DcpSemanticMaskWeight;
  using photo_editor::pipeline::dcp_interpolated_forward_matrix;
  using photo_editor::pipeline::dcp_apply_hue_sat_map;
  using photo_editor::pipeline::dcp_apply_look_table;
  using photo_editor::pipeline::dcp_apply_profile_tone_curve;
  using photo_editor::pipeline::dcp_apply_profile_gain_table_map;
  using photo_editor::pipeline::dcp_apply_rgb_tables;
  using photo_editor::pipeline::dcp_camera_to_xyz_d50;
  using photo_editor::pipeline::dcp_calibration_illuminant_temperature;
  using photo_editor::pipeline::dcp_profile_camera_compatibility;
  using photo_editor::pipeline::dcp_profile_calibration_illuminant_temperature;
  using photo_editor::pipeline::dcp_profile_render_plan_cache_stats;
  using photo_editor::pipeline::dcp_reciprocal_temperature_weight;
  using photo_editor::pipeline::dcp_temperature_tint_to_xy;
  using photo_editor::pipeline::clear_dcp_profile_render_plan_cache_for_testing;
  using photo_editor::pipeline::compile_dcp_profile_render_plan;
  using photo_editor::pipeline::parse_dcp_profile;

  if (argc == 2) {
    std::ifstream stream(argv[1], std::ios::binary);
    if (!stream) {
      return fail("DCP inspection file could not be opened");
    }
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size <= 0) {
      return fail("DCP inspection file is empty");
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
      return fail("DCP inspection file could not be read");
    }
    const auto inspected = parse_dcp_profile(bytes);
    std::cout << photo_editor::pipeline::dcp_parse_status_name(inspected.status)
              << " name=" << inspected.profile.profile_name
              << " camera=" << inspected.profile.unique_camera_model
              << " sha256=" << inspected.profile.payload_sha256
              << " forward=" << inspected.profile.has_executable_forward_matrix()
              << " failure=" << inspected.failure_reason << '\n';
    return inspected.ready() ? 0 : 1;
  }

  const auto little_payload = make_tiff(valid_tags(ByteOrder::Little), ByteOrder::Little);
  const auto little = parse_dcp_profile(little_payload);
  if (!little.ready() || little.profile.profile_name != "Fixture Dual DCP" ||
      little.profile.unique_camera_model != "Fixture Camera" ||
      little.profile.payload_sha256.size() != 64U ||
      little.profile.byte_size != little_payload.size() ||
      !little.profile.dual_illuminant() ||
      !little.profile.has_executable_forward_matrix()) {
    return fail("little-endian DCP profile should parse into immutable profile data");
  }
  if (!little.profile.baseline_exposure.has_value() ||
      !nearly_equal(*little.profile.baseline_exposure, -0.5) ||
      !little.profile.analog_balance.has_value() ||
      little.profile.hue_sat_map1.entries.size() != 2U ||
      little.profile.hue_sat_map2.entries.size() != 2U ||
      little.profile.look_table.entries.size() != 2U ||
      little.profile.tone_curve.size() != 2U ||
      !little.profile.embed_policy.has_value() || *little.profile.embed_policy != 3U) {
    return fail("DCP optional data should be decoded only after validation");
  }
  if (dcp_profile_camera_compatibility(little.profile, "Fixture", "Camera") !=
          photo_editor::pipeline::DcpCameraCompatibility::Match ||
      dcp_profile_camera_compatibility(little.profile, "Sony", "ILCE-7M4") !=
          photo_editor::pipeline::DcpCameraCompatibility::Mismatch ||
      dcp_profile_camera_compatibility(little.profile, {}, {}) !=
          photo_editor::pipeline::DcpCameraCompatibility::Unknown) {
    return fail("DCP camera compatibility should distinguish verified matches, mismatches, and unknown metadata");
  }

  const auto big_payload = make_tiff(valid_tags(ByteOrder::Big), ByteOrder::Big, 42U);
  const auto big = parse_dcp_profile(big_payload);
  if (!big.ready() || !big.profile.color_matrix1.has_value() ||
      !nearly_equal((*big.profile.color_matrix1)[1], 0.012)) {
    return fail("big-endian TIFF-hosted DCP data should parse identically");
  }

  const auto tungsten = dcp_interpolated_forward_matrix(little.profile, 2850.0);
  const auto daylight = dcp_interpolated_forward_matrix(little.profile, 6500.0);
  const auto d50_reference_xyz = std::array<double, 3>{
      0.3457 / 0.3585,
      1.0,
      (1.0 - 0.3457 - 0.3585) / 0.3585,
  };
  const auto maps_camera_one_to_d50 = [&](const std::array<double, 9>& matrix) {
    const auto mapped = multiply_matrix_vector(
        matrix, std::array<double, 3>{1.0, 1.0, 1.0});
    return nearly_equal(mapped[0], d50_reference_xyz[0], 1.0e-12) &&
           nearly_equal(mapped[1], d50_reference_xyz[1], 1.0e-12) &&
           nearly_equal(mapped[2], d50_reference_xyz[2], 1.0e-12);
  };
  if (!tungsten.has_value() || !daylight.has_value() ||
      !maps_camera_one_to_d50(*tungsten) ||
      !maps_camera_one_to_d50(*daylight)) {
    return fail("DNG forward matrices should be normalized at each illuminant before interpolation");
  }
  const auto reciprocal_midpoint_temperature = 2.0 / (1.0 / 2850.0 + 1.0 / 6500.0);
  const auto reciprocal_midpoint = dcp_interpolated_forward_matrix(
      little.profile,
      reciprocal_midpoint_temperature);
  if (!reciprocal_midpoint.has_value() ||
      !nearly_equal(
          (*reciprocal_midpoint)[0],
          ((*tungsten)[0] + (*daylight)[0]) * 0.5,
          1.0e-12) ||
      nearly_equal(reciprocal_midpoint_temperature, (2850.0 + 6500.0) * 0.5)) {
    return fail("DCP interpolation should operate in reciprocal temperature, not Kelvin");
  }
  if (!nearly_equal(dcp_reciprocal_temperature_weight(2850.0, 2850.0, 6500.0), 0.0) ||
      !nearly_equal(dcp_reciprocal_temperature_weight(6500.0, 2850.0, 6500.0), 1.0)) {
    return fail("reciprocal-temperature interpolation weights should clamp at endpoints");
  }
  const std::array sdk_illuminant_temperatures = {
      std::pair{std::uint16_t{1U}, 5500.0},
      std::pair{std::uint16_t{2U}, 4150.0},
      std::pair{std::uint16_t{3U}, 2850.0},
      std::pair{std::uint16_t{4U}, 5500.0},
      std::pair{std::uint16_t{9U}, 5500.0},
      std::pair{std::uint16_t{10U}, 6500.0},
      std::pair{std::uint16_t{11U}, 7500.0},
      std::pair{std::uint16_t{12U}, 6400.0},
      std::pair{std::uint16_t{13U}, 5050.0},
      std::pair{std::uint16_t{14U}, 4150.0},
      std::pair{std::uint16_t{15U}, 3525.0},
      std::pair{std::uint16_t{16U}, 2925.0},
      std::pair{std::uint16_t{17U}, 2850.0},
      std::pair{std::uint16_t{18U}, 5500.0},
      std::pair{std::uint16_t{19U}, 6500.0},
      std::pair{std::uint16_t{20U}, 5500.0},
      std::pair{std::uint16_t{21U}, 6500.0},
      std::pair{std::uint16_t{22U}, 7500.0},
      std::pair{std::uint16_t{23U}, 5000.0},
      std::pair{std::uint16_t{24U}, 3200.0},
  };
  for (const auto& [light_source, expected_temperature] :
       sdk_illuminant_temperatures) {
    const auto actual_temperature =
        dcp_calibration_illuminant_temperature(light_source);
    if (!actual_temperature.has_value() ||
        !nearly_equal(*actual_temperature, expected_temperature)) {
      return fail("DNG calibration illuminants should use the specified nominal temperatures");
    }
  }
  if (dcp_calibration_illuminant_temperature(0U).has_value() ||
      dcp_calibration_illuminant_temperature(255U).has_value()) {
    return fail("unknown or custom DNG illuminants require explicit illuminant data");
  }
  auto custom_illuminant_tags = valid_tags(ByteOrder::Little);
  const auto first_illuminant = std::find_if(
      custom_illuminant_tags.begin(),
      custom_illuminant_tags.end(),
      [](const Tag& tag) { return tag.id == 50778U; });
  first_illuminant->data = shorts({255U}, ByteOrder::Little);
  custom_illuminant_tags.push_back(Tag{
      .id = 52533U,
      .type = 7U,
      .count = 18U,
      .data = illuminant_xy_data(
          3457U,
          10000U,
          3585U,
          10000U,
          ByteOrder::Little),
  });
  const auto custom_illuminant = parse_dcp_profile(
      make_tiff(custom_illuminant_tags, ByteOrder::Little));
  const auto custom_temperature =
      dcp_profile_calibration_illuminant_temperature(
          custom_illuminant.profile,
          0U);
  if (!custom_illuminant.ready() ||
      !custom_illuminant.profile.illuminant_data1.has_value() ||
      custom_illuminant.profile.illuminant_data_kinds[0] !=
          DcpIlluminantDataKind::WhiteXy ||
      !custom_temperature.has_value() ||
      std::abs(*custom_temperature - 5000.0) > 25.0) {
    return fail("DNG custom xy illuminant data should drive matrix interpolation temperature");
  }

  constexpr auto kExpectedSpectrumWhiteX = 0.3376709852808692;
  constexpr auto kExpectedSpectrumWhiteY = 0.3675694528094348;
  for (const auto order : {ByteOrder::Little, ByteOrder::Big}) {
    auto spectrum_tags = valid_tags(order);
    const auto spectrum_first_illuminant = std::find_if(
        spectrum_tags.begin(),
        spectrum_tags.end(),
        [](const Tag& tag) { return tag.id == 50778U; });
    spectrum_first_illuminant->data = shorts({255U}, order);
    const auto spectrum_payload = illuminant_spectrum_data(
        {380U, 1U},
        {20U, 1U},
        reference_spectrum(),
        order);
    spectrum_tags.push_back(Tag{
        .id = 52533U,
        .type = 7U,
        .count = static_cast<std::uint32_t>(spectrum_payload.size()),
        .data = spectrum_payload,
    });
    const auto spectral_illuminant = parse_dcp_profile(
        make_tiff(spectrum_tags, order));
    const auto spectral_transform = dcp_camera_to_xyz_d50(
        spectral_illuminant.profile,
        4800.0,
        5.0);
    if (!spectral_illuminant.ready() ||
        !spectral_illuminant.profile.illuminant_data1.has_value() ||
        spectral_illuminant.profile.illuminant_data_kinds[0] !=
            DcpIlluminantDataKind::Spectrum ||
        !spectral_transform.valid ||
        std::abs(
            spectral_illuminant.profile.illuminant_data1->x -
            kExpectedSpectrumWhiteX) > 1.0e-12 ||
        std::abs(
            spectral_illuminant.profile.illuminant_data1->y -
            kExpectedSpectrumWhiteY) > 1.0e-12) {
      return fail("DNG spectrum illuminant data should match the CIE-derived white");
    }
  }
  for (const auto data_type : {0U, 1U, 2U}) {
    auto gain_table_tags = valid_tags(ByteOrder::Little);
    const auto payload = profile_gain_table_map2_data(
        ByteOrder::Little,
        0.8F,
        data_type);
    gain_table_tags.push_back(Tag{
        .id = 52544U,
        .type = 7U,
        .count = static_cast<std::uint32_t>(payload.size()),
        .data = payload,
    });
    const auto parsed = parse_dcp_profile(
        make_tiff(gain_table_tags, ByteOrder::Little));
    if (!parsed.ready() ||
        !parsed.profile.profile_gain_table_map.configured() ||
        parsed.profile.profile_gain_table_map.gains.size() != 16U) {
      return fail("DNG ProfileGainTableMap2 should decode uint8, uint16, and float16 tables");
    }
  }

  const auto parse_rgb_tables = [&](const std::vector<std::byte>& payload,
                                    ByteOrder order = ByteOrder::Little,
                                    std::uint16_t type = 7U) {
    auto tags = valid_tags(order);
    tags.push_back(Tag{
        .id = 52543U,
        .type = type,
        .count = static_cast<std::uint32_t>(payload.size()),
        .data = payload,
    });
    return parse_dcp_profile(make_tiff(tags, order));
  };
  for (const auto order : {ByteOrder::Little, ByteOrder::Big}) {
    for (const auto pixel_type : {0U, 1U, 2U}) {
      for (const auto gamma : {0U, 1U, 2U, 3U, 4U}) {
        for (const auto primaries : {0U, 1U, 2U, 3U, 4U}) {
          const auto payload = rgb_tables_data(
              order,
              0U,
              static_cast<std::uint8_t>(pixel_type),
              static_cast<std::uint8_t>(gamma),
              static_cast<std::uint8_t>(primaries));
          const auto parsed = parse_rgb_tables(payload, order);
          if (!parsed.ready() || !parsed.profile.rgb_tables.configured() ||
              parsed.profile.rgb_tables.tables.size() != 2U ||
              parsed.profile.rgb_tables.tables[0].semantic_name != "subject" ||
              parsed.profile.rgb_tables.tables[0].entries.size() != 8U ||
              !parsed.profile.rgb_tables.tables[1].background() ||
              parsed.profile.rgb_tables.tables[1].entries.size() != 8U) {
            return fail("DNG RGBTables should parse every specified encoding, gamma, primaries, and byte order");
          }
        }
      }
    }
  }
  const auto weighted_rgb_tables = parse_rgb_tables(
      rgb_tables_data(ByteOrder::Little));
  if (!weighted_rgb_tables.ready()) {
    return fail("valid DNG RGBTables should be executable");
  }
  const auto source_rgb = photo_editor::pipeline::DcpRgb{0.2, 0.4, 0.8};
  const auto background_rgb = dcp_apply_rgb_tables(
      weighted_rgb_tables.profile,
      source_rgb);
  const std::array subject_mask = {
      DcpSemanticMaskWeight{.semantic_name = "subject", .weight = 0.25},
  };
  const auto blended_rgb = dcp_apply_rgb_tables(
      weighted_rgb_tables.profile,
      source_rgb,
      subject_mask);
  const std::array duplicate_subject_masks = {
      DcpSemanticMaskWeight{.semantic_name = "subject", .weight = 0.75},
      DcpSemanticMaskWeight{.semantic_name = "subject", .weight = 0.75},
  };
  const auto clamped_subject_rgb = dcp_apply_rgb_tables(
      weighted_rgb_tables.profile,
      source_rgb,
      duplicate_subject_masks);
  if (!nearly_equal(background_rgb.red, 0.2, 1.0e-7) ||
      !nearly_equal(background_rgb.green, 0.2, 1.0e-7) ||
      !nearly_equal(background_rgb.blue, 0.8, 1.0e-7) ||
      !nearly_equal(blended_rgb.red, 0.35, 1.0e-7) ||
      !nearly_equal(blended_rgb.green, 0.25, 1.0e-7) ||
      !nearly_equal(blended_rgb.blue, 0.65, 1.0e-7) ||
      !nearly_equal(clamped_subject_rgb.red, 0.8, 1.0e-7) ||
      !nearly_equal(clamped_subject_rgb.green, 0.4, 1.0e-7) ||
      !nearly_equal(clamped_subject_rgb.blue, 0.2, 1.0e-7)) {
    return fail("DNG weighted RGBTables should combine semantic masks, background, and duplicate-mask clamping");
  }
  auto sequential_profile = weighted_rgb_tables.profile;
  sequential_profile.rgb_tables.composite_method =
      photo_editor::pipeline::DcpRgbTableCompositeMethod::Sequential;
  const auto sequential_rgb = dcp_apply_rgb_tables(
      sequential_profile,
      source_rgb,
      subject_mask);
  if (!nearly_equal(sequential_rgb.red, 0.35, 1.0e-7) ||
      !nearly_equal(sequential_rgb.green, 0.25, 1.0e-7) ||
      !nearly_equal(sequential_rgb.blue, 0.65, 1.0e-7)) {
    return fail("DNG sequential RGBTables should apply each table in declared order");
  }
  auto no_background_profile = weighted_rgb_tables.profile;
  no_background_profile.rgb_tables.tables.pop_back();
  const auto implicit_identity_rgb = dcp_apply_rgb_tables(
      no_background_profile,
      source_rgb);
  if (!nearly_equal(implicit_identity_rgb.red, source_rgb.red) ||
      !nearly_equal(implicit_identity_rgb.green, source_rgb.green) ||
      !nearly_equal(implicit_identity_rgb.blue, source_rgb.blue)) {
    return fail("DNG RGBTables without a background table should use identity for uncovered pixels");
  }
  auto hdr_identity_profile = weighted_rgb_tables.profile;
  hdr_identity_profile.dynamic_range =
      photo_editor::pipeline::DcpProfileDynamicRange{
          .hdr = true,
          .hint_max_output_value = 8.0F,
      };
  auto& identity_table = hdr_identity_profile.rgb_tables.tables.back();
  for (std::size_t index = 0U; index < identity_table.entries.size(); ++index) {
    identity_table.entries[index] = {
        static_cast<float>((index / 4U) % 2U),
        static_cast<float>((index / 2U) % 2U),
        static_cast<float>(index % 2U),
    };
  }
  const auto hdr_source = photo_editor::pipeline::DcpRgb{4.0, 0.5, -0.25};
  const auto hdr_identity = dcp_apply_rgb_tables(
      hdr_identity_profile,
      hdr_source);
  if (!nearly_equal(hdr_identity.red, hdr_source.red, 1.0e-7) ||
      !nearly_equal(hdr_identity.green, hdr_source.green, 1.0e-7) ||
      !nearly_equal(hdr_identity.blue, hdr_source.blue, 1.0e-7)) {
    return fail("DNG HDR RGBTables should preserve extended-range identity values");
  }

  const auto rgb_tables_fail_closed = [&](std::vector<std::byte> payload) {
    const auto parsed = parse_rgb_tables(payload);
    return !parsed.ready() && parsed.failing_tag == 52543U;
  };
  auto invalid_rgb_tables = rgb_tables_data(ByteOrder::Little);
  invalid_rgb_tables[0U] = std::byte{0};
  if (!rgb_tables_fail_closed(invalid_rgb_tables)) {
    return fail("DNG RGBTables with zero tables must fail closed");
  }
  invalid_rgb_tables = rgb_tables_data(ByteOrder::Little);
  invalid_rgb_tables[4U] = std::byte{2};
  if (!rgb_tables_fail_closed(invalid_rgb_tables)) {
    return fail("DNG RGBTables with an unknown composite method must fail closed");
  }
  for (const auto [offset, invalid_value] : {
           std::pair{17U, std::byte{1}},
           std::pair{18U, std::byte{3}},
           std::pair{19U, std::byte{5}},
           std::pair{20U, std::byte{5}},
           std::pair{21U, std::byte{2}},
       }) {
    invalid_rgb_tables = rgb_tables_data(ByteOrder::Little);
    invalid_rgb_tables[offset] = invalid_value;
    if (!rgb_tables_fail_closed(invalid_rgb_tables)) {
      return fail("DNG RGBTables with invalid table metadata must fail closed");
    }
  }
  invalid_rgb_tables = rgb_tables_data(ByteOrder::Little);
  invalid_rgb_tables[10U] = std::byte{0x80};
  if (!rgb_tables_fail_closed(invalid_rgb_tables)) {
    return fail("DNG RGBTables semantic names must be non-NUL ASCII");
  }
  invalid_rgb_tables = rgb_tables_data(ByteOrder::Little);
  invalid_rgb_tables[22U] = std::byte{0x00};
  invalid_rgb_tables[23U] = std::byte{0x00};
  invalid_rgb_tables[24U] = std::byte{0xC0};
  invalid_rgb_tables[25U] = std::byte{0x7F};
  if (!rgb_tables_fail_closed(invalid_rgb_tables)) {
    return fail("DNG RGBTables with non-finite float samples must fail closed");
  }
  invalid_rgb_tables = rgb_tables_data(ByteOrder::Little);
  invalid_rgb_tables.pop_back();
  if (!rgb_tables_fail_closed(invalid_rgb_tables)) {
    return fail("truncated DNG RGBTables must fail closed");
  }
  invalid_rgb_tables = rgb_tables_data(ByteOrder::Little);
  invalid_rgb_tables.push_back(std::byte{0});
  if (!rgb_tables_fail_closed(invalid_rgb_tables)) {
    return fail("DNG RGBTables with trailing bytes must fail closed");
  }
  auto duplicate_background_tables = std::vector<std::byte>{};
  append_u32(&duplicate_background_tables, 2U, ByteOrder::Little);
  append_u32(&duplicate_background_tables, 0U, ByteOrder::Little);
  append_rgb_table(
      &duplicate_background_tables,
      {},
      2U,
      0U,
      4U,
      true,
      true,
      ByteOrder::Little);
  append_rgb_table(
      &duplicate_background_tables,
      {},
      2U,
      0U,
      4U,
      true,
      true,
      ByteOrder::Little);
  if (!rgb_tables_fail_closed(duplicate_background_tables)) {
    return fail("DNG RGBTables must reject multiple background tables");
  }
  auto invalid_manual_rgb_tables = weighted_rgb_tables.profile.rgb_tables;
  invalid_manual_rgb_tables.tables[0].entries[0][0] =
      std::numeric_limits<float>::quiet_NaN();
  if (invalid_manual_rgb_tables.configured()) {
    return fail("manually constructed DNG RGBTables with non-finite samples must fail closed");
  }

  const auto parse_invalid_spectrum = [&](const std::vector<std::byte>& payload) {
    auto tags = valid_tags(ByteOrder::Little);
    const auto illuminant = std::find_if(
        tags.begin(),
        tags.end(),
        [](const Tag& tag) { return tag.id == 50778U; });
    illuminant->data = shorts({255U}, ByteOrder::Little);
    tags.push_back(Tag{
        .id = 52533U,
        .type = 7U,
        .count = static_cast<std::uint32_t>(payload.size()),
        .data = payload,
    });
    return parse_dcp_profile(make_tiff(tags, ByteOrder::Little));
  };
  if (parse_invalid_spectrum(illuminant_spectrum_data(
          {380U, 1U}, {20U, 1U}, {{1U, 1U}}, ByteOrder::Little)).ready()) {
    return fail("DNG spectrum illuminants with fewer than two samples must fail closed");
  }
  auto invalid_denominator_samples = reference_spectrum();
  invalid_denominator_samples[4].second = 0U;
  if (parse_invalid_spectrum(illuminant_spectrum_data(
          {380U, 1U},
          {20U, 1U},
          invalid_denominator_samples,
          ByteOrder::Little)).ready()) {
    return fail("DNG spectrum illuminants with invalid rationals must fail closed");
  }
  auto zero_spectrum = reference_spectrum();
  for (auto& sample : zero_spectrum) {
    sample.first = 0U;
  }
  if (parse_invalid_spectrum(illuminant_spectrum_data(
          {380U, 1U}, {20U, 1U}, zero_spectrum, ByteOrder::Little)).ready()) {
    return fail("DNG spectra that cannot produce a white point must fail closed");
  }
  auto truncated_spectrum = illuminant_spectrum_data(
      {380U, 1U}, {20U, 1U}, reference_spectrum(), ByteOrder::Little);
  truncated_spectrum.pop_back();
  if (parse_invalid_spectrum(truncated_spectrum).ready()) {
    return fail("truncated DNG spectrum illuminant data must fail closed");
  }

  for (const auto order : {ByteOrder::Little, ByteOrder::Big}) {
    auto gain_table_tags = valid_tags(order);
    const auto payload = profile_gain_table_map2_data(order);
    gain_table_tags.push_back(Tag{
        .id = 52544U,
        .type = 7U,
        .count = static_cast<std::uint32_t>(payload.size()),
        .data = payload,
    });
    const auto parsed = parse_dcp_profile(make_tiff(gain_table_tags, order));
    if (!parsed.ready() ||
        !parsed.profile.profile_gain_table_map.configured() ||
        parsed.profile.profile_gain_table_map.points_v != 2U ||
        parsed.profile.profile_gain_table_map.points_h != 2U ||
        parsed.profile.profile_gain_table_map.table_points != 4U) {
      return fail("DNG ProfileGainTableMap2 should parse in both byte orders");
    }
    const auto mapped = dcp_apply_profile_gain_table_map(
        parsed.profile,
        0.35,
        0.65,
        1.25,
        {0.2, 0.4, 0.1});
    if (!(mapped.red > 0.2 && mapped.green > 0.4 && mapped.blue > 0.1) ||
        !nearly_equal(mapped.red / 0.2, mapped.green / 0.4, 1.0e-6) ||
        !nearly_equal(mapped.red / 0.2, mapped.blue / 0.1, 1.0e-6)) {
      return fail("DNG ProfileGainTableMap2 should apply one spatially interpolated RGB gain");
    }
    auto metadata_tags = valid_tags(order);
    metadata_tags.push_back(Tag{
        .id = 52549U,
        .type = 4U,
        .count = 1U,
        .data = longs({2U}, order),
    });
    const auto dynamic_range = profile_dynamic_range_data(order);
    metadata_tags.push_back(Tag{
        .id = 52551U,
        .type = 7U,
        .count = static_cast<std::uint32_t>(dynamic_range.size()),
        .data = dynamic_range,
    });
    const auto parsed_metadata = parse_dcp_profile(make_tiff(metadata_tags, order));
    if (!parsed_metadata.ready() ||
        photo_editor::pipeline::dcp_profile_tone_method_name(
            parsed_metadata.profile.tone_method) != std::string_view("adobe-pv6") ||
        !parsed_metadata.profile.dynamic_range.has_value() ||
        !parsed_metadata.profile.dynamic_range->hdr ||
        !nearly_equal(
            parsed_metadata.profile.dynamic_range->hint_max_output_value,
            4.0,
            1.0e-7)) {
      return fail("DNG tone-method and dynamic-range metadata should parse in both byte orders");
    }
  }
  {
    auto invalid_manual_profile = parse_dcp_profile(make_tiff(
        [&]() {
          auto tags = valid_tags(ByteOrder::Little);
          const auto payload = profile_gain_table_map2_data(ByteOrder::Little);
          tags.push_back(Tag{
              .id = 52544U,
              .type = 7U,
              .count = static_cast<std::uint32_t>(payload.size()),
              .data = payload,
          });
          return tags;
        }(),
        ByteOrder::Little)).profile;
    invalid_manual_profile.profile_gain_table_map.input_weights[0] =
        std::numeric_limits<float>::quiet_NaN();
    if (invalid_manual_profile.profile_gain_table_map.configured()) {
      return fail("manually constructed ProfileGainTableMap2 weights must fail closed");
    }
    invalid_manual_profile.profile_gain_table_map.input_weights[0] = 0.25F;
    invalid_manual_profile.profile_gain_table_map.gains[0] =
        std::numeric_limits<float>::infinity();
    if (invalid_manual_profile.profile_gain_table_map.configured()) {
      return fail("manually constructed ProfileGainTableMap2 gains must fail closed");
    }
  }
  auto invalid_gain_table_tags = valid_tags(ByteOrder::Little);
  auto invalid_gain_table_payload = profile_gain_table_map2_data(
      ByteOrder::Little,
      std::numeric_limits<float>::quiet_NaN());
  invalid_gain_table_tags.push_back(Tag{
      .id = 52544U,
      .type = 7U,
      .count = static_cast<std::uint32_t>(invalid_gain_table_payload.size()),
      .data = invalid_gain_table_payload,
  });
  if (parse_dcp_profile(
          make_tiff(invalid_gain_table_tags, ByteOrder::Little)).ready()) {
    return fail("DNG ProfileGainTableMap2 with non-finite gamma must fail closed");
  }
  invalid_gain_table_tags.pop_back();
  invalid_gain_table_payload = profile_gain_table_map2_data(
      ByteOrder::Little,
      0.8F,
      4U);
  invalid_gain_table_tags.push_back(Tag{
      .id = 52544U,
      .type = 7U,
      .count = static_cast<std::uint32_t>(invalid_gain_table_payload.size()),
      .data = invalid_gain_table_payload,
  });
  if (parse_dcp_profile(
          make_tiff(invalid_gain_table_tags, ByteOrder::Little)).ready()) {
    return fail("unsupported DNG ProfileGainTableMap2 data types must fail closed");
  }
  invalid_gain_table_tags.pop_back();
  invalid_gain_table_payload = profile_gain_table_map2_data(ByteOrder::Little);
  invalid_gain_table_payload[80U] = std::byte{0x00};
  invalid_gain_table_payload[81U] = std::byte{0x00};
  invalid_gain_table_payload[82U] = std::byte{0xC0};
  invalid_gain_table_payload[83U] = std::byte{0x7F};
  invalid_gain_table_tags.push_back(Tag{
      .id = 52544U,
      .type = 7U,
      .count = static_cast<std::uint32_t>(invalid_gain_table_payload.size()),
      .data = invalid_gain_table_payload,
  });
  if (parse_dcp_profile(
          make_tiff(invalid_gain_table_tags, ByteOrder::Little)).ready()) {
    return fail("non-finite DNG ProfileGainTableMap2 samples must fail closed");
  }
  invalid_gain_table_tags.pop_back();
  invalid_gain_table_payload = profile_gain_table_map2_data(ByteOrder::Little);
  invalid_gain_table_payload.pop_back();
  invalid_gain_table_tags.push_back(Tag{
      .id = 52544U,
      .type = 7U,
      .count = static_cast<std::uint32_t>(invalid_gain_table_payload.size()),
      .data = invalid_gain_table_payload,
  });
  if (parse_dcp_profile(
          make_tiff(invalid_gain_table_tags, ByteOrder::Little)).ready()) {
    return fail("truncated DNG ProfileGainTableMap2 must fail closed");
  }

  auto invalid_metadata_tags = valid_tags(ByteOrder::Little);
  invalid_metadata_tags.push_back(Tag{
      .id = 52549U,
      .type = 4U,
      .count = 1U,
      .data = longs({3U}, ByteOrder::Little),
  });
  if (parse_dcp_profile(
          make_tiff(invalid_metadata_tags, ByteOrder::Little)).ready()) {
    return fail("unsupported DNG ProfileToneMethod values must fail closed");
  }
  invalid_metadata_tags.pop_back();
  const auto invalid_dynamic_range = profile_dynamic_range_data(
      ByteOrder::Little,
      1U,
      0U,
      2.0F);
  invalid_metadata_tags.push_back(Tag{
      .id = 52551U,
      .type = 7U,
      .count = static_cast<std::uint32_t>(invalid_dynamic_range.size()),
      .data = invalid_dynamic_range,
  });
  if (parse_dcp_profile(
          make_tiff(invalid_metadata_tags, ByteOrder::Little)).ready()) {
    return fail("SDR DNG profiles with HDR output hints must fail closed");
  }

  custom_illuminant_tags.pop_back();
  const auto missing_custom_illuminant_data = parse_dcp_profile(
      make_tiff(custom_illuminant_tags, ByteOrder::Little));
  if (missing_custom_illuminant_data.ready() ||
      missing_custom_illuminant_data.failure_reason !=
          "dcp_matrix_illuminant_pairing_invalid") {
    return fail("DNG custom illuminants without IlluminantData must fail closed");
  }
  auto triple_tags = valid_tags(ByteOrder::Little);
  triple_tags.push_back(Tag{
      52529U,
      3U,
      1U,
      shorts({23U}, ByteOrder::Little),
  });
  triple_tags.push_back(Tag{
      52531U,
      10U,
      9U,
      signed_rationals(matrix_values(10000, 30), ByteOrder::Little),
  });
  triple_tags.push_back(Tag{
      52532U,
      10U,
      9U,
      signed_rationals(forward_matrix_values(false), ByteOrder::Little),
  });
  triple_tags.push_back(Tag{
      52537U,
      11U,
      6U,
      floats({0.0F, 1.0F, 1.0F, 4.0F, 1.2F, 1.1F}, ByteOrder::Little),
  });
  const auto triple = parse_dcp_profile(
      make_tiff(triple_tags, ByteOrder::Little));
  const auto triple_transform = dcp_camera_to_xyz_d50(
      triple.profile,
      5000.0,
      12.0);
  const auto triple_weight_sum =
      triple_transform.illuminant_weights[0] +
      triple_transform.illuminant_weights[1] +
      triple_transform.illuminant_weights[2];
  if (!triple.ready() || !triple.profile.triple_illuminant() ||
      triple.profile.hue_sat_map3.entries.size() != 2U ||
      !triple_transform.valid ||
      !nearly_equal(triple_weight_sum, 1.0, 1.0e-12) ||
      triple_transform.illuminant_weights[2] <= 0.0) {
    return fail("DNG triple-illuminant profiles should parse and execute normalized three-way interpolation");
  }
  auto partial_triple_forward_tags = triple_tags;
  partial_triple_forward_tags.erase(std::remove_if(
      partial_triple_forward_tags.begin(),
      partial_triple_forward_tags.end(),
      [](const Tag& tag) { return tag.id == 52532U; }));
  if (parse_dcp_profile(
          make_tiff(partial_triple_forward_tags, ByteOrder::Little))
          .ready()) {
    return fail("DNG triple profiles with partial ForwardMatrix sets must fail closed");
  }
  auto duplicate_triple_illuminant_tags = triple_tags;
  const auto third_illuminant = std::find_if(
      duplicate_triple_illuminant_tags.begin(),
      duplicate_triple_illuminant_tags.end(),
      [](const Tag& tag) { return tag.id == 52529U; });
  third_illuminant->data = shorts({21U}, ByteOrder::Little);
  if (parse_dcp_profile(
          make_tiff(duplicate_triple_illuminant_tags, ByteOrder::Little))
          .ready()) {
    return fail("DNG triple profiles require three distinct calibration illuminants");
  }

  const auto midpoint_hue_map = dcp_apply_hue_sat_map(
      little.profile,
      reciprocal_midpoint_temperature,
      photo_editor::pipeline::DcpRgb{1.0, 0.0, 0.0});
  if (!nearly_equal(midpoint_hue_map.red, 1.0, 1.0e-6) ||
      !nearly_equal(midpoint_hue_map.green, 0.0, 1.0e-6) ||
      !nearly_equal(midpoint_hue_map.blue, 0.0, 1.0e-6)) {
    return fail("dual-illuminant HueSatMap entries should interpolate before HSV application");
  }
  const auto tungsten_hue_map = dcp_apply_hue_sat_map(
      little.profile,
      2850.0,
      photo_editor::pipeline::DcpRgb{1.0, 0.0, 0.0});
  const auto expected_srgb_decoded_value =
      std::pow((0.95 + 0.055) / 1.055, 2.4);
  if (!nearly_equal(tungsten_hue_map.red, expected_srgb_decoded_value, 1.0e-6) ||
      tungsten_hue_map.green <= 0.0 || tungsten_hue_map.blue > 1.0e-7) {
    return fail("HueSatMap should use SDK hue wrapping and sRGB value encoding semantics");
  }
  const auto look_mapped = dcp_apply_look_table(
      little.profile,
      photo_editor::pipeline::DcpRgb{1.0, 0.0, 0.0});
  if (!nearly_equal(look_mapped.red, 1.0, 1.0e-7) ||
      look_mapped.green <= 0.0 || look_mapped.blue > 1.0e-7) {
    return fail("ProfileLookTable should execute its HSV delta in linear value encoding");
  }

  auto curved_profile = little.profile;
  curved_profile.tone_curve = {
      {0.0F, 0.0F},
      {0.5F, 0.25F},
      {1.0F, 1.0F},
  };
  const auto curved_quarter = dcp_apply_profile_tone_curve(
      curved_profile,
      photo_editor::pipeline::DcpRgb{0.25, 0.25, 0.25});
  if (!nearly_equal(curved_quarter.red, 0.078125, 1.0e-9) ||
      !nearly_equal(curved_quarter.green, curved_quarter.red) ||
      !nearly_equal(curved_quarter.blue, curved_quarter.red)) {
    return fail("ProfileToneCurve should reproduce the DNG natural cubic spline");
  }
  const auto curved_overrange = dcp_apply_profile_tone_curve(
      curved_profile,
      photo_editor::pipeline::DcpRgb{1.5, 1.5, 1.5});
  if (!nearly_equal(curved_overrange.red, 1.875, 1.0e-9) ||
      curved_overrange.green != curved_overrange.red ||
      curved_overrange.blue != curved_overrange.red) {
    return fail("ProfileToneCurve endpoint extrapolation should preserve HDR headroom");
  }
  clear_dcp_profile_render_plan_cache_for_testing();
  const auto first_compiled_plan =
      compile_dcp_profile_render_plan(
          curved_profile,
          "sha256:camera-profile-a",
          5000.0,
          12.0);
  const auto repeated_compiled_plan =
      compile_dcp_profile_render_plan(
          curved_profile,
          "sha256:camera-profile-a",
          5000.0,
          12.0);
  auto compiled_plan_stats =
      dcp_profile_render_plan_cache_stats();
  if (first_compiled_plan == nullptr ||
      repeated_compiled_plan !=
          first_compiled_plan ||
      compiled_plan_stats.entries != 1U ||
      compiled_plan_stats.builds != 1U ||
      compiled_plan_stats.misses != 1U ||
      compiled_plan_stats.hits != 1U) {
    return fail(
        "compiled DCP render plans should single-build and reuse immutable profile, temperature, and tint identities");
  }
  for (std::size_t index = 0U;
       index < compiled_plan_stats.capacity;
       ++index) {
    static_cast<void>(
        compile_dcp_profile_render_plan(
            curved_profile,
            "sha256:camera-profile-" +
                std::to_string(index),
            5000.0,
            12.0));
  }
  compiled_plan_stats =
      dcp_profile_render_plan_cache_stats();
  if (compiled_plan_stats.entries >
          compiled_plan_stats.capacity ||
      compiled_plan_stats.evictions == 0U) {
    return fail(
        "compiled DCP render plans should enforce their fixed LRU capacity");
  }
  const auto hue_overrange = dcp_apply_hue_sat_map(
      little.profile,
      2850.0,
      photo_editor::pipeline::DcpRgb{1.5, 0.0, 0.0});
  if (hue_overrange.red <= 1.0 || hue_overrange.red >= 1.5 ||
      hue_overrange.green <= 0.0) {
    return fail("HueSatMap should transfer its bounded correction without clipping over-range input");
  }

  const auto dng_6500_white = dcp_temperature_tint_to_xy(6500.0, 0.0);
  const auto dng_magenta_white = dcp_temperature_tint_to_xy(6500.0, 25.0);
  if (!dng_6500_white.valid() || !dng_magenta_white.valid() ||
      !nearly_equal(dng_6500_white.x, 0.3135, 0.0010) ||
      !nearly_equal(dng_6500_white.y, 0.3237, 0.0010) ||
      nearly_equal(dng_6500_white.x, dng_magenta_white.x, 1.0e-6) ||
      nearly_equal(dng_6500_white.y, dng_magenta_white.y, 1.0e-6)) {
    return fail("DNG temperature/tint should reproduce the SDK isotemperature xy model");
  }

  auto executable_profile = little.profile;
  executable_profile.camera_calibration_signature = "shared-calibration";
  executable_profile.profile_calibration_signature = "shared-calibration";
  const auto resolved_dcp = dcp_camera_to_xyz_d50(
      executable_profile,
      reciprocal_midpoint_temperature,
      12.0);
  if (!resolved_dcp.valid || !resolved_dcp.used_forward_matrix ||
      !resolved_dcp.used_camera_calibration ||
      !nearly_equal(resolved_dcp.second_illuminant_weight, 0.5, 1.0e-5) ||
      !resolved_dcp.white.valid()) {
    return fail("DNG camera transform should execute calibration and reciprocal-temperature interpolation");
  }
  const auto d50_xyz = std::array<double, 3>{
      0.3457 / 0.3585,
      1.0,
      (1.0 - 0.3457 - 0.3585) / 0.3585,
  };
  const auto source_white_xyz = std::array<double, 3>{
      resolved_dcp.white.x / resolved_dcp.white.y,
      1.0,
      (1.0 - resolved_dcp.white.x - resolved_dcp.white.y) /
          resolved_dcp.white.y,
  };
  const auto canonical_color_matrix = [&](std::array<double, 9> matrix) {
    const auto mapped_d50 = multiply_matrix_vector(matrix, d50_xyz);
    const auto maximum = *std::max_element(
        mapped_d50.cbegin(), mapped_d50.cend());
    if (maximum < 0.99 || maximum > 1.01) {
      for (auto& value : matrix) {
        value /= maximum;
      }
    }
    for (auto& value : matrix) {
      value = std::round(value * 10000.0) / 10000.0;
    }
    return matrix;
  };
  const auto canonical_color_matrix1 =
      canonical_color_matrix(*executable_profile.color_matrix1);
  const auto canonical_color_matrix2 =
      canonical_color_matrix(*executable_profile.color_matrix2);
  auto interpolated_color_matrix = std::array<double, 9>{};
  for (std::size_t index = 0U; index < interpolated_color_matrix.size(); ++index) {
    interpolated_color_matrix[index] = std::lerp(
        canonical_color_matrix1[index],
        canonical_color_matrix2[index],
        0.5);
  }
  auto camera_white = multiply_matrix_vector(
      interpolated_color_matrix,
      source_white_xyz);
  const auto camera_white_scale = *std::max_element(
      camera_white.cbegin(),
      camera_white.cend());
  for (auto& channel : camera_white) {
    channel = std::clamp(channel / camera_white_scale, 0.001, 1.0);
  }
  const auto mapped_white = multiply_matrix_vector(
      resolved_dcp.camera_to_xyz_d50,
      camera_white);
  const auto mapped_scale = mapped_white[1] / d50_xyz[1];
  if (!nearly_equal(mapped_white[0] / mapped_scale, d50_xyz[0], 1.0e-7) ||
      !nearly_equal(mapped_white[1] / mapped_scale, d50_xyz[1], 1.0e-7) ||
      !nearly_equal(mapped_white[2] / mapped_scale, d50_xyz[2], 1.0e-7)) {
    return fail("normalized DNG ForwardMatrix execution should map the selected camera white to PCS D50");
  }

  auto color_matrix_only_profile = executable_profile;
  color_matrix_only_profile.forward_matrix1.reset();
  color_matrix_only_profile.forward_matrix2.reset();
  const auto color_matrix_only = dcp_camera_to_xyz_d50(
      color_matrix_only_profile,
      5000.0,
      0.0);
  if (!color_matrix_only.valid || color_matrix_only.used_forward_matrix ||
      !color_matrix_only_profile.has_executable_color_transform()) {
    return fail("three-channel DCP profiles should execute the ColorMatrix inversion fallback without a ForwardMatrix");
  }

  auto singular_calibration_profile = executable_profile;
  singular_calibration_profile.camera_calibration1 = std::array<double, 9>{};
  singular_calibration_profile.camera_calibration2 = std::array<double, 9>{};
  const auto singular_calibration = dcp_camera_to_xyz_d50(
      singular_calibration_profile,
      5000.0,
      0.0);
  if (singular_calibration.valid ||
      singular_calibration.failure_reason != "dcp_camera_calibration_singular") {
    return fail("singular DNG camera calibration must fail closed instead of becoming identity");
  }

  auto duplicate_tags = valid_tags(ByteOrder::Little);
  duplicate_tags.push_back(duplicate_tags.front());
  const auto duplicate = parse_dcp_profile(make_tiff(duplicate_tags, ByteOrder::Little));
  if (duplicate.status != DcpParseStatus::DuplicateTag || duplicate.failing_tag != 50708U) {
    return fail("duplicate profile tags should fail explicitly");
  }

  const auto cycle = parse_dcp_profile(
      make_tiff(valid_tags(ByteOrder::Little), ByteOrder::Little, 0x4352U, 8U));
  if (cycle.status != DcpParseStatus::IfdCycle) {
    return fail("cyclic IFD chains should be rejected");
  }

  auto wrong_type_tags = valid_tags(ByteOrder::Little);
  wrong_type_tags[3].type = 5U;
  const auto wrong_type = parse_dcp_profile(make_tiff(wrong_type_tags, ByteOrder::Little));
  if (wrong_type.status != DcpParseStatus::InvalidTagValue ||
      wrong_type.failing_tag != 50721U) {
    return fail("matrix tags with the wrong TIFF type should fail");
  }

  auto invalid_forward_tags = valid_tags(ByteOrder::Little);
  invalid_forward_tags[21].data =
      signed_rationals(matrix_values(5000), ByteOrder::Little);
  const auto invalid_forward = parse_dcp_profile(
      make_tiff(invalid_forward_tags, ByteOrder::Little));
  if (invalid_forward.status != DcpParseStatus::InvalidTagValue ||
      invalid_forward.failing_tag != 50964U ||
      invalid_forward.failure_reason !=
          "dcp_forward_matrix_does_not_map_camera_white_to_d50") {
    return fail("DNG ForwardMatrix must map equal camera values to PCS D50 within the specified tolerance");
  }

  auto four_plane_tags = valid_tags(ByteOrder::Little);
  four_plane_tags[3].count = 12U;
  four_plane_tags[3].data = signed_rationals(
      {
          {1, 1}, {0, 1}, {0, 1},
          {0, 1}, {1, 1}, {0, 1},
          {0, 1}, {0, 1}, {1, 1},
          {0, 1}, {0, 1}, {0, 1},
      },
      ByteOrder::Little);
  const auto four_plane = parse_dcp_profile(make_tiff(four_plane_tags, ByteOrder::Little));
  if (four_plane.status != DcpParseStatus::UnsupportedColorPlanes) {
    return fail("non-RGB camera matrices should fail with an explicit color-plane status");
  }

  auto zero_denominator_tags = valid_tags(ByteOrder::Little);
  zero_denominator_tags[9].data = rationals({{1U, 0U}, {1U, 1U}, {1U, 1U}}, ByteOrder::Little);
  const auto zero_denominator =
      parse_dcp_profile(make_tiff(zero_denominator_tags, ByteOrder::Little));
  if (zero_denominator.status != DcpParseStatus::InvalidTagValue) {
    return fail("zero rational denominators should be rejected");
  }

  auto bad_table_tags = valid_tags(ByteOrder::Little);
  bad_table_tags[16].data = floats(
      {0.0F, 1.0F, 0.5F, 2.0F, 1.1F, 0.95F},
      ByteOrder::Little);
  const auto bad_table = parse_dcp_profile(make_tiff(bad_table_tags, ByteOrder::Little));
  if (bad_table.status != DcpParseStatus::InvalidTagValue ||
      bad_table.failing_tag != 50938U) {
    return fail("invalid zero-saturation hue table entries should be rejected");
  }

  auto huge_count_tags = valid_tags(ByteOrder::Little);
  huge_count_tags[3].count = 0xFFFFFFFFU;
  const auto huge_count = parse_dcp_profile(make_tiff(huge_count_tags, ByteOrder::Little));
  if (huge_count.status != DcpParseStatus::CountOverflow) {
    return fail("profile tag counts exceeding the resource limit should fail before allocation");
  }

  auto missing_matrix_tags = valid_tags(ByteOrder::Little);
  missing_matrix_tags.erase(missing_matrix_tags.begin() + 3, missing_matrix_tags.begin() + 5);
  const auto missing_matrix =
      parse_dcp_profile(make_tiff(missing_matrix_tags, ByteOrder::Little));
  if (missing_matrix.status != DcpParseStatus::MissingColorMatrix) {
    return fail("profiles without ColorMatrix data should fail explicitly");
  }

  auto out_of_range_payload = little_payload;
  constexpr std::size_t color_matrix1_entry_offset = 8U + 2U + 3U * 12U;
  const auto value_offset = color_matrix1_entry_offset + 8U;
  out_of_range_payload[value_offset] = std::byte{0xFF};
  out_of_range_payload[value_offset + 1U] = std::byte{0xFF};
  out_of_range_payload[value_offset + 2U] = std::byte{0xFF};
  out_of_range_payload[value_offset + 3U] = std::byte{0x7F};
  const auto out_of_range = parse_dcp_profile(out_of_range_payload);
  if (out_of_range.status != DcpParseStatus::OffsetOutOfRange ||
      out_of_range.failing_tag != 50721U) {
    return fail("out-of-range profile payload offsets should fail before decoding");
  }

  const std::array invalid_header = {
      std::byte{'Z'}, std::byte{'Z'}, std::byte{0}, std::byte{0},
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
  };
  if (parse_dcp_profile(invalid_header).status != DcpParseStatus::InvalidByteOrder ||
      parse_dcp_profile({}).status != DcpParseStatus::EmptyPayload) {
    return fail("invalid and empty DCP headers should fail closed");
  }

  if (std::string(photo_editor::pipeline::dcp_parse_status_name(DcpParseStatus::Ready)) != "ready" ||
      std::string(photo_editor::pipeline::dcp_parse_status_name(DcpParseStatus::IfdCycle)) != "ifd-cycle") {
    return fail("DCP parse status names should remain stable for diagnostics");
  }

  return 0;
}

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace photo_editor::test {

inline void append_dcp_u16(
    std::vector<std::byte>* output,
    std::uint16_t value) {
  output->push_back(static_cast<std::byte>(value & 0xFFU));
  output->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

inline void append_dcp_u32(
    std::vector<std::byte>* output,
    std::uint32_t value) {
  for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
    output->push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
  }
}

inline std::vector<std::byte> dcp_matrix_payload(std::int32_t diagonal) {
  std::vector<std::byte> output;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      append_dcp_u32(
          &output,
          std::bit_cast<std::uint32_t>(row == column ? diagonal : 0));
      append_dcp_u32(&output, 10000U);
    }
  }
  return output;
}

inline std::vector<std::byte> dcp_forward_matrix_payload() {
  constexpr std::array<std::int32_t, 9> values = {
      9642, 0, 0,
      0, 10000, 0,
      0, 0, 8252,
  };
  std::vector<std::byte> output;
  for (const auto value : values) {
    append_dcp_u32(&output, std::bit_cast<std::uint32_t>(value));
    append_dcp_u32(&output, 10000U);
  }
  return output;
}

inline std::vector<std::byte> valid_dcp_asset_payload() {
  struct Tag final {
    std::uint16_t id;
    std::uint16_t type;
    std::uint32_t count;
    std::vector<std::byte> data;
  };
  const std::vector<Tag> tags = {
      {50778U, 3U, 1U, {std::byte{17}, std::byte{0}}},
      {50721U, 10U, 9U, dcp_matrix_payload(10000)},
      {50936U,
       2U,
       14U,
       {
           std::byte{'A'}, std::byte{'s'}, std::byte{'s'}, std::byte{'e'},
           std::byte{'t'}, std::byte{' '}, std::byte{'F'}, std::byte{'i'},
           std::byte{'x'}, std::byte{'t'}, std::byte{'u'}, std::byte{'r'},
           std::byte{'e'}, std::byte{0},
       }},
      {50964U, 10U, 9U, dcp_forward_matrix_payload()},
  };
  std::vector<std::byte> output = {
      std::byte{'I'}, std::byte{'I'}, std::byte{0x52}, std::byte{0x43},
  };
  append_dcp_u32(&output, 8U);
  append_dcp_u16(&output, static_cast<std::uint16_t>(tags.size()));
  const auto external_start = 8U + 2U + tags.size() * 12U + 4U;
  std::vector<std::byte> external;
  for (const auto& tag : tags) {
    append_dcp_u16(&output, tag.id);
    append_dcp_u16(&output, tag.type);
    append_dcp_u32(&output, tag.count);
    if (tag.data.size() <= 4U) {
      output.insert(output.end(), tag.data.begin(), tag.data.end());
      for (std::size_t padding = tag.data.size(); padding < 4U; ++padding) {
        output.push_back(std::byte{0});
      }
    } else {
      append_dcp_u32(
          &output,
          static_cast<std::uint32_t>(external_start + external.size()));
      external.insert(external.end(), tag.data.begin(), tag.data.end());
    }
  }
  append_dcp_u32(&output, 0U);
  output.insert(output.end(), external.begin(), external.end());
  return output;
}

}  // namespace photo_editor::test

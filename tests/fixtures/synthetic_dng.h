#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace photo_editor::test_fixtures {
namespace detail {

struct TiffEntry final {
  std::uint16_t tag = 0U;
  std::uint16_t type = 0U;
  std::uint32_t count = 0U;
  std::vector<std::uint8_t> data;
  std::uint32_t external_offset = 0U;
};

inline void append_u16(std::vector<std::uint8_t>* bytes, std::uint16_t value) {
  bytes->push_back(static_cast<std::uint8_t>(value & 0xffU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

inline void append_u32(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
  append_u16(bytes, static_cast<std::uint16_t>(value & 0xffffU));
  append_u16(bytes, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

[[nodiscard]] inline std::vector<std::uint8_t> short_values(
    std::initializer_list<std::uint16_t> values) {
  auto bytes = std::vector<std::uint8_t>{};
  for (const auto value : values) append_u16(&bytes, value);
  return bytes;
}

[[nodiscard]] inline std::vector<std::uint8_t> long_values(
    std::initializer_list<std::uint32_t> values) {
  auto bytes = std::vector<std::uint8_t>{};
  for (const auto value : values) append_u32(&bytes, value);
  return bytes;
}

[[nodiscard]] inline std::vector<std::uint8_t> rational_values(
    std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> values) {
  auto bytes = std::vector<std::uint8_t>{};
  for (const auto& [numerator, denominator] : values) {
    append_u32(&bytes, numerator);
    append_u32(&bytes, denominator);
  }
  return bytes;
}

[[nodiscard]] inline std::vector<std::uint8_t> signed_rational_values(
    std::initializer_list<std::pair<std::int32_t, std::int32_t>> values) {
  auto bytes = std::vector<std::uint8_t>{};
  for (const auto& [numerator, denominator] : values) {
    append_u32(&bytes, static_cast<std::uint32_t>(numerator));
    append_u32(&bytes, static_cast<std::uint32_t>(denominator));
  }
  return bytes;
}

[[nodiscard]] inline std::vector<std::uint8_t> ascii_value(const std::string& value) {
  auto bytes = std::vector<std::uint8_t>(value.begin(), value.end());
  bytes.push_back(0U);
  return bytes;
}

}  // namespace detail

// Small deterministic Bayer DNG for decoder and image-provider integration
// tests. No camera files or external test artifacts are needed.
[[nodiscard]] inline std::vector<std::uint8_t> synthetic_dng() {
  using namespace detail;
  constexpr auto kWidth = std::uint32_t{128U};
  constexpr auto kHeight = std::uint32_t{128U};
  constexpr auto kPixelBytes = kWidth * kHeight * 2U;
  auto entries = std::vector<TiffEntry>{
      {256U, 4U, 1U, long_values({kWidth})},
      {257U, 4U, 1U, long_values({kHeight})},
      {258U, 3U, 1U, short_values({16U})},
      {259U, 3U, 1U, short_values({1U})},
      {262U, 3U, 1U, short_values({32803U})},
      {271U, 2U, 16U, ascii_value("OpenAI Fixtures")},
      {272U, 2U, 19U, ascii_value("Synthetic Bayer v1")},
      {273U, 4U, 1U, long_values({0U})},
      {274U, 3U, 1U, short_values({1U})},
      {277U, 3U, 1U, short_values({1U})},
      {278U, 4U, 1U, long_values({kHeight})},
      {279U, 4U, 1U, long_values({kPixelBytes})},
      {284U, 3U, 1U, short_values({1U})},
      {33421U, 3U, 2U, short_values({2U, 2U})},
      {33422U, 1U, 4U, {0U, 1U, 1U, 2U}},
      {50706U, 1U, 4U, {1U, 4U, 0U, 0U}},
      {50707U, 1U, 4U, {1U, 1U, 0U, 0U}},
      {50708U, 2U, 19U, ascii_value("Synthetic Bayer v1")},
      {50710U, 1U, 3U, {0U, 1U, 2U}},
      {50711U, 3U, 1U, short_values({1U})},
      {50713U, 3U, 2U, short_values({1U, 1U})},
      {50714U, 5U, 1U, rational_values({{64U, 1U}})},
      {50717U, 4U, 1U, long_values({4095U})},
      {50719U, 5U, 2U, rational_values({{0U, 1U}, {0U, 1U}})},
      {50720U, 5U, 2U, rational_values({{kWidth, 1U}, {kHeight, 1U}})},
      {50721U, 10U, 9U,
       signed_rational_values({
           {10000, 10000}, {0, 10000}, {0, 10000},
           {0, 10000}, {10000, 10000}, {0, 10000},
           {0, 10000}, {0, 10000}, {10000, 10000},
       })},
      {50728U, 5U, 3U, rational_values({{1U, 1U}, {1U, 1U}, {1U, 1U}})},
      {50778U, 3U, 1U, short_values({21U})},
      {50829U, 4U, 4U, long_values({0U, 0U, kHeight, kWidth})},
  };
  std::sort(entries.begin(), entries.end(),
            [](const TiffEntry& left, const TiffEntry& right) {
              return left.tag < right.tag;
            });

  const auto ifd_bytes =
      std::uint32_t{2U + static_cast<std::uint32_t>(entries.size()) * 12U + 4U};
  auto external_offset = std::uint32_t{8U} + ifd_bytes;
  for (auto& entry : entries) {
    if (entry.data.size() > 4U) {
      entry.external_offset = external_offset;
      external_offset += static_cast<std::uint32_t>(entry.data.size());
      external_offset = (external_offset + 1U) & ~std::uint32_t{1U};
    }
  }
  const auto pixel_offset = external_offset;
  const auto strip =
      std::find_if(entries.begin(), entries.end(),
                   [](const TiffEntry& entry) { return entry.tag == 273U; });
  strip->data = long_values({pixel_offset});

  auto bytes = std::vector<std::uint8_t>{'I', 'I'};
  append_u16(&bytes, 42U);
  append_u32(&bytes, 8U);
  append_u16(&bytes, static_cast<std::uint16_t>(entries.size()));
  for (const auto& entry : entries) {
    append_u16(&bytes, entry.tag);
    append_u16(&bytes, entry.type);
    append_u32(&bytes, entry.count);
    if (entry.data.size() <= 4U) {
      bytes.insert(bytes.end(), entry.data.begin(), entry.data.end());
      bytes.resize(bytes.size() + 4U - entry.data.size(), 0U);
    } else {
      append_u32(&bytes, entry.external_offset);
    }
  }
  append_u32(&bytes, 0U);
  for (const auto& entry : entries) {
    if (entry.data.size() <= 4U) continue;
    bytes.resize(entry.external_offset, 0U);
    bytes.insert(bytes.end(), entry.data.begin(), entry.data.end());
  }
  bytes.resize(pixel_offset, 0U);
  for (std::uint32_t y = 0U; y < kHeight; ++y) {
    for (std::uint32_t x = 0U; x < kWidth; ++x) {
      const auto red = (y % 2U) == 0U && (x % 2U) == 0U;
      const auto blue = (y % 2U) == 1U && (x % 2U) == 1U;
      const auto sample = static_cast<std::uint16_t>(red ? 3200U : (blue ? 1200U : 2200U));
      append_u16(&bytes, sample);
    }
  }
  return bytes;
}

}  // namespace photo_editor::test_fixtures

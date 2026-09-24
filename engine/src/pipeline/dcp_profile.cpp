#include "photo_editor/pipeline/dcp_profile.h"

#include "cie_1931_observer.h"
#include "photo_editor/pipeline/color_transform.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <list>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace photo_editor::pipeline {
namespace {

constexpr std::uint16_t kTiffMagic = 42U;
constexpr std::uint16_t kBigTiffMagic = 43U;
constexpr std::uint16_t kDcpMagic = 0x4352U;
constexpr std::size_t kMaximumIfds = 64U;
constexpr std::size_t kMaximumIfdEntries = 4096U;
constexpr std::size_t kMaximumTextBytes = 1024U * 1024U;
constexpr std::size_t kDcpRenderPlanCacheCapacity = 16U;

struct DcpRenderPlanCacheEntry final {
  std::shared_ptr<const DcpProfileRenderPlan> plan;
  std::list<std::string>::iterator age;
};

struct DcpRenderPlanCache final {
  std::mutex mutex;
  std::unordered_map<std::string, DcpRenderPlanCacheEntry> entries;
  std::list<std::string> age;
  std::uint64_t hits = 0U;
  std::uint64_t misses = 0U;
  std::uint64_t builds = 0U;
  std::uint64_t evictions = 0U;
};

DcpRenderPlanCache& dcp_render_plan_cache() {
  static DcpRenderPlanCache cache;
  return cache;
}

std::string dcp_render_plan_cache_key(
    std::string_view immutable_profile_identity,
    double source_temperature_kelvin,
    double source_tint) {
  return std::string{immutable_profile_identity} + "|" +
         std::to_string(
             std::bit_cast<std::uint64_t>(
                 source_temperature_kelvin)) +
         "|" +
         std::to_string(
             std::bit_cast<std::uint64_t>(
                 source_tint));
}

constexpr std::uint16_t kTypeByte = 1U;
constexpr std::uint16_t kTypeAscii = 2U;
constexpr std::uint16_t kTypeShort = 3U;
constexpr std::uint16_t kTypeLong = 4U;
constexpr std::uint16_t kTypeRational = 5U;
constexpr std::uint16_t kTypeUndefined = 7U;
constexpr std::uint16_t kTypeSLong = 9U;
constexpr std::uint16_t kTypeSRational = 10U;
constexpr std::uint16_t kTypeFloat = 11U;
constexpr std::uint16_t kTypeDouble = 12U;

constexpr std::uint16_t kTagUniqueCameraModel = 50708U;
constexpr std::uint16_t kTagColorMatrix1 = 50721U;
constexpr std::uint16_t kTagColorMatrix2 = 50722U;
constexpr std::uint16_t kTagCameraCalibration1 = 50723U;
constexpr std::uint16_t kTagCameraCalibration2 = 50724U;
constexpr std::uint16_t kTagReductionMatrix1 = 50725U;
constexpr std::uint16_t kTagReductionMatrix2 = 50726U;
constexpr std::uint16_t kTagAnalogBalance = 50727U;
constexpr std::uint16_t kTagAsShotNeutral = 50728U;
constexpr std::uint16_t kTagBaselineExposure = 50730U;
constexpr std::uint16_t kTagCalibrationIlluminant1 = 50778U;
constexpr std::uint16_t kTagCalibrationIlluminant2 = 50779U;
constexpr std::uint16_t kTagCameraCalibrationSignature = 50931U;
constexpr std::uint16_t kTagProfileCalibrationSignature = 50932U;
constexpr std::uint16_t kTagProfileName = 50936U;
constexpr std::uint16_t kTagProfileHueSatMapDims = 50937U;
constexpr std::uint16_t kTagProfileHueSatMapData1 = 50938U;
constexpr std::uint16_t kTagProfileHueSatMapData2 = 50939U;
constexpr std::uint16_t kTagProfileToneCurve = 50940U;
constexpr std::uint16_t kTagProfileEmbedPolicy = 50941U;
constexpr std::uint16_t kTagProfileCopyright = 50942U;
constexpr std::uint16_t kTagForwardMatrix1 = 50964U;
constexpr std::uint16_t kTagForwardMatrix2 = 50965U;
constexpr std::uint16_t kTagProfileLookTableDims = 50981U;
constexpr std::uint16_t kTagProfileLookTableData = 50982U;
constexpr std::uint16_t kTagProfileLookTableEncoding = 51108U;
constexpr std::uint16_t kTagProfileHueSatMapEncoding = 51107U;
constexpr std::uint16_t kTagCalibrationIlluminant3 = 52529U;
constexpr std::uint16_t kTagCameraCalibration3 = 52530U;
constexpr std::uint16_t kTagColorMatrix3 = 52531U;
constexpr std::uint16_t kTagForwardMatrix3 = 52532U;
constexpr std::uint16_t kTagIlluminantData1 = 52533U;
constexpr std::uint16_t kTagIlluminantData2 = 52534U;
constexpr std::uint16_t kTagIlluminantData3 = 52535U;
constexpr std::uint16_t kTagProfileHueSatMapData3 = 52537U;
constexpr std::uint16_t kTagReductionMatrix3 = 52538U;
constexpr std::uint16_t kTagRgbTables = 52543U;
constexpr std::uint16_t kTagProfileGainTableMap2 = 52544U;
constexpr std::uint16_t kTagProfileToneMethod = 52549U;
constexpr std::uint16_t kTagProfileDynamicRange = 52551U;

enum class ByteOrder {
  Little,
  Big,
};

struct IfdEntry final {
  std::uint16_t tag = 0U;
  std::uint16_t type = 0U;
  std::uint32_t count = 0U;
  std::uint32_t entry_offset = 0U;
};

using ProfileEntries = std::unordered_map<std::uint16_t, IfdEntry>;

[[nodiscard]] bool is_profile_tag(std::uint16_t tag) {
  switch (tag) {
    case kTagUniqueCameraModel:
    case kTagColorMatrix1:
    case kTagColorMatrix2:
    case kTagCameraCalibration1:
    case kTagCameraCalibration2:
    case kTagReductionMatrix1:
    case kTagReductionMatrix2:
    case kTagAnalogBalance:
    case kTagAsShotNeutral:
    case kTagBaselineExposure:
    case kTagCalibrationIlluminant1:
    case kTagCalibrationIlluminant2:
    case kTagCameraCalibrationSignature:
    case kTagProfileCalibrationSignature:
    case kTagProfileName:
    case kTagProfileHueSatMapDims:
    case kTagProfileHueSatMapData1:
    case kTagProfileHueSatMapData2:
    case kTagProfileToneCurve:
    case kTagProfileEmbedPolicy:
    case kTagProfileCopyright:
    case kTagForwardMatrix1:
    case kTagForwardMatrix2:
    case kTagProfileLookTableDims:
    case kTagProfileLookTableData:
    case kTagProfileLookTableEncoding:
    case kTagProfileHueSatMapEncoding:
    case kTagCalibrationIlluminant3:
    case kTagCameraCalibration3:
    case kTagColorMatrix3:
    case kTagForwardMatrix3:
    case kTagIlluminantData1:
    case kTagIlluminantData2:
    case kTagIlluminantData3:
    case kTagProfileHueSatMapData3:
    case kTagReductionMatrix3:
    case kTagRgbTables:
    case kTagProfileGainTableMap2:
    case kTagProfileToneMethod:
    case kTagProfileDynamicRange:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] std::string normalized_camera_identity(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) != 0) {
      result.push_back(static_cast<char>(std::tolower(byte)));
    }
  }
  return result;
}

[[nodiscard]] std::string primary_make_token(std::string_view value) {
  const auto separator = value.find_first_of(" \t");
  return normalized_camera_identity(value.substr(0U, separator));
}

[[nodiscard]] std::optional<std::size_t> tiff_type_size(std::uint16_t type) {
  switch (type) {
    case kTypeByte:
    case kTypeAscii:
    case kTypeUndefined:
      return 1U;
    case kTypeShort:
      return 2U;
    case kTypeLong:
    case kTypeSLong:
    case kTypeFloat:
      return 4U;
    case kTypeRational:
    case kTypeSRational:
    case kTypeDouble:
      return 8U;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] bool checked_add(
    std::size_t left,
    std::size_t right,
    std::size_t* result) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  *result = left + right;
  return true;
}

[[nodiscard]] bool checked_multiply(
    std::size_t left,
    std::size_t right,
    std::size_t* result) {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  *result = left * right;
  return true;
}

class TiffReader final {
 public:
  TiffReader(std::span<const std::byte> payload, ByteOrder byte_order)
      : payload_(payload), byte_order_(byte_order) {}

  [[nodiscard]] bool contains(std::size_t offset, std::size_t length) const {
    return offset <= payload_.size() && length <= payload_.size() - offset;
  }

  [[nodiscard]] std::optional<std::uint16_t> u16(std::size_t offset) const {
    if (!contains(offset, 2U)) {
      return std::nullopt;
    }
    const auto first = std::to_integer<std::uint8_t>(payload_[offset]);
    const auto second = std::to_integer<std::uint8_t>(payload_[offset + 1U]);
    return byte_order_ == ByteOrder::Little
               ? static_cast<std::uint16_t>(first | (static_cast<std::uint16_t>(second) << 8U))
               : static_cast<std::uint16_t>((static_cast<std::uint16_t>(first) << 8U) | second);
  }

  [[nodiscard]] std::optional<std::uint32_t> u32(std::size_t offset) const {
    if (!contains(offset, 4U)) {
      return std::nullopt;
    }
    std::uint32_t result = 0U;
    if (byte_order_ == ByteOrder::Little) {
      for (std::size_t index = 0U; index < 4U; ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(payload_[offset + index]))
                  << static_cast<unsigned int>(index * 8U);
      }
    } else {
      for (std::size_t index = 0U; index < 4U; ++index) {
        result = (result << 8U) |
                 std::to_integer<std::uint8_t>(payload_[offset + index]);
      }
    }
    return result;
  }

  [[nodiscard]] std::optional<std::int32_t> i32(std::size_t offset) const {
    const auto value = u32(offset);
    return value.has_value()
               ? std::optional<std::int32_t>(std::bit_cast<std::int32_t>(*value))
               : std::nullopt;
  }

  [[nodiscard]] std::optional<float> f32(std::size_t offset) const {
    const auto value = u32(offset);
    return value.has_value()
               ? std::optional<float>(std::bit_cast<float>(*value))
               : std::nullopt;
  }

  [[nodiscard]] std::optional<double> f64(std::size_t offset) const {
    if (!contains(offset, 8U)) {
      return std::nullopt;
    }
    std::uint64_t value = 0U;
    if (byte_order_ == ByteOrder::Little) {
      for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<std::uint8_t>(payload_[offset + index]))
                 << static_cast<unsigned int>(index * 8U);
      }
    } else {
      for (std::size_t index = 0U; index < 8U; ++index) {
        value = (value << 8U) |
                std::to_integer<std::uint8_t>(payload_[offset + index]);
      }
    }
    return std::bit_cast<double>(value);
  }

  [[nodiscard]] std::optional<std::span<const std::byte>> entry_bytes(
      const IfdEntry& entry) const {
    const auto type_size = tiff_type_size(entry.type);
    if (!type_size.has_value()) {
      return std::nullopt;
    }
    std::size_t byte_count = 0U;
    if (!checked_multiply(
            static_cast<std::size_t>(entry.count),
            *type_size,
            &byte_count)) {
      return std::nullopt;
    }
    std::size_t data_offset = static_cast<std::size_t>(entry.entry_offset) + 8U;
    if (byte_count > 4U) {
      const auto external_offset = u32(data_offset);
      if (!external_offset.has_value()) {
        return std::nullopt;
      }
      data_offset = static_cast<std::size_t>(*external_offset);
    }
    if (!contains(data_offset, byte_count)) {
      return std::nullopt;
    }
    return payload_.subspan(data_offset, byte_count);
  }

  [[nodiscard]] ByteOrder byte_order() const {
    return byte_order_;
  }

 private:
  std::span<const std::byte> payload_;
  ByteOrder byte_order_;
};

[[nodiscard]] DcpParseResult failure(
    DcpParseStatus status,
    std::string reason,
    std::uint32_t ifd_offset = 0U,
    std::uint16_t tag = 0U) {
  return DcpParseResult{
      .status = status,
      .profile = {},
      .failure_reason = std::move(reason),
      .failing_ifd_offset = ifd_offset,
      .failing_tag = tag,
  };
}

[[nodiscard]] bool valid_utf8(std::span<const std::byte> bytes) {
  std::size_t index = 0U;
  while (index < bytes.size()) {
    const auto lead = std::to_integer<std::uint8_t>(bytes[index]);
    std::size_t continuation_count = 0U;
    std::uint32_t code_point = 0U;
    if (lead <= 0x7FU) {
      ++index;
      continue;
    }
    if ((lead & 0xE0U) == 0xC0U) {
      continuation_count = 1U;
      code_point = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
      continuation_count = 2U;
      code_point = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
      continuation_count = 3U;
      code_point = lead & 0x07U;
    } else {
      return false;
    }
    if (continuation_count > bytes.size() - index - 1U) {
      return false;
    }
    for (std::size_t continuation = 0U; continuation < continuation_count; ++continuation) {
      const auto next = std::to_integer<std::uint8_t>(bytes[index + continuation + 1U]);
      if ((next & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (next & 0x3FU);
    }
    if ((continuation_count == 1U && code_point < 0x80U) ||
        (continuation_count == 2U && code_point < 0x800U) ||
        (continuation_count == 3U && code_point < 0x10000U) ||
        code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
    index += continuation_count + 1U;
  }
  return true;
}

[[nodiscard]] std::optional<std::string> read_text(
    const TiffReader& reader,
    const IfdEntry& entry) {
  if ((entry.type != kTypeAscii && entry.type != kTypeByte) ||
      entry.count == 0U || entry.count > kMaximumTextBytes) {
    return std::nullopt;
  }
  const auto bytes = reader.entry_bytes(entry);
  if (!bytes.has_value() || bytes->empty() || bytes->back() != std::byte{0}) {
    return std::nullopt;
  }
  auto text_bytes = *bytes;
  while (!text_bytes.empty() && text_bytes.back() == std::byte{0}) {
    text_bytes = text_bytes.first(text_bytes.size() - 1U);
  }
  if (std::find(text_bytes.begin(), text_bytes.end(), std::byte{0}) != text_bytes.end() ||
      !valid_utf8(text_bytes)) {
    return std::nullopt;
  }
  return std::string(
      reinterpret_cast<const char*>(text_bytes.data()),
      text_bytes.size());
}

[[nodiscard]] std::optional<std::vector<double>> read_numeric_values(
    const TiffReader& reader,
    const IfdEntry& entry,
    std::uint16_t required_type) {
  if (entry.type != required_type) {
    return std::nullopt;
  }
  const auto bytes = reader.entry_bytes(entry);
  const auto type_size = tiff_type_size(entry.type);
  if (!bytes.has_value() || !type_size.has_value()) {
    return std::nullopt;
  }
  std::vector<double> values;
  values.reserve(entry.count);

  const auto read_u32_from_bytes = [&](std::size_t offset) -> std::optional<std::uint32_t> {
    if (offset > bytes->size() || 4U > bytes->size() - offset) {
      return std::nullopt;
    }
    std::uint32_t result = 0U;
    if (reader.byte_order() == ByteOrder::Little) {
      for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>((*bytes)[offset + byte_index]))
                  << static_cast<unsigned int>(byte_index * 8U);
      }
    } else {
      for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
        result = (result << 8U) |
                 std::to_integer<std::uint8_t>((*bytes)[offset + byte_index]);
      }
    }
    return result;
  };

  const auto read_u64_from_bytes = [&](std::size_t offset) -> std::optional<std::uint64_t> {
    if (offset > bytes->size() || 8U > bytes->size() - offset) {
      return std::nullopt;
    }
    std::uint64_t result = 0U;
    if (reader.byte_order() == ByteOrder::Little) {
      for (std::size_t byte_index = 0U; byte_index < 8U; ++byte_index) {
        result |= static_cast<std::uint64_t>(
                      std::to_integer<std::uint8_t>((*bytes)[offset + byte_index]))
                  << static_cast<unsigned int>(byte_index * 8U);
      }
    } else {
      for (std::size_t byte_index = 0U; byte_index < 8U; ++byte_index) {
        result = (result << 8U) |
                 std::to_integer<std::uint8_t>((*bytes)[offset + byte_index]);
      }
    }
    return result;
  };

  for (std::size_t index = 0U; index < entry.count; ++index) {
    const auto offset = index * *type_size;
    double value = 0.0;
    if (entry.type == kTypeRational || entry.type == kTypeSRational) {
      const auto numerator_bits = read_u32_from_bytes(offset);
      const auto denominator_bits = read_u32_from_bytes(offset + 4U);
      if (!numerator_bits.has_value() || !denominator_bits.has_value()) {
        return std::nullopt;
      }
      if (entry.type == kTypeRational) {
        if (*denominator_bits == 0U) {
          return std::nullopt;
        }
        value = static_cast<double>(*numerator_bits) /
                static_cast<double>(*denominator_bits);
      } else {
        const auto numerator = std::bit_cast<std::int32_t>(*numerator_bits);
        const auto denominator = std::bit_cast<std::int32_t>(*denominator_bits);
        if (denominator == 0) {
          return std::nullopt;
        }
        value = static_cast<double>(numerator) /
                static_cast<double>(denominator);
      }
    } else if (entry.type == kTypeFloat) {
      const auto bits = read_u32_from_bytes(offset);
      if (!bits.has_value()) {
        return std::nullopt;
      }
      value = static_cast<double>(std::bit_cast<float>(*bits));
    } else if (entry.type == kTypeDouble) {
      const auto bits = read_u64_from_bytes(offset);
      if (!bits.has_value()) {
        return std::nullopt;
      }
      value = std::bit_cast<double>(*bits);
    } else {
      return std::nullopt;
    }
    if (!std::isfinite(value)) {
      return std::nullopt;
    }
    values.push_back(value);
  }
  return values;
}

struct ParsedIlluminantData final {
  DcpChromaticity white;
  DcpIlluminantDataKind kind = DcpIlluminantDataKind::None;
};

[[nodiscard]] std::optional<ParsedIlluminantData> read_illuminant_data(
    const TiffReader& reader,
    const IfdEntry& entry) {
  constexpr std::size_t kXyPayloadBytes = 18U;
  constexpr std::uint32_t kMinimumSpectrumSamples = 2U;
  constexpr std::uint32_t kMaximumSpectrumSamples = 1000U;
  constexpr std::size_t kSpectrumHeaderBytes = 22U;
  if (entry.type != kTypeUndefined || entry.count < 2U) {
    return std::nullopt;
  }
  const auto bytes = reader.entry_bytes(entry);
  if (!bytes.has_value() || bytes->size() < 2U) {
    return std::nullopt;
  }
  const auto read_u16 = [&](std::size_t offset) {
    const auto first = std::to_integer<std::uint8_t>((*bytes)[offset]);
    const auto second = std::to_integer<std::uint8_t>((*bytes)[offset + 1U]);
    return reader.byte_order() == ByteOrder::Little
               ? static_cast<std::uint16_t>(first | (second << 8U))
               : static_cast<std::uint16_t>((first << 8U) | second);
  };
  const auto read_u32 = [&](std::size_t offset) {
    auto result = std::uint32_t{0U};
    if (reader.byte_order() == ByteOrder::Little) {
      for (std::size_t index = 0U; index < 4U; ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>((*bytes)[offset + index]))
                  << static_cast<unsigned int>(index * 8U);
      }
    } else {
      for (std::size_t index = 0U; index < 4U; ++index) {
        result = (result << 8U) |
                 std::to_integer<std::uint8_t>((*bytes)[offset + index]);
      }
    }
    return result;
  };
  const auto read_rational = [&](std::size_t offset) -> std::optional<double> {
    if (offset > bytes->size() || bytes->size() - offset < 8U) {
      return std::nullopt;
    }
    const auto denominator = read_u32(offset + 4U);
    if (denominator == 0U) {
      return std::nullopt;
    }
    const auto value = static_cast<double>(read_u32(offset)) /
                       static_cast<double>(denominator);
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
  };

  const auto payload_type = read_u16(0U);
  if (payload_type == 0U) {
    if (entry.count < kXyPayloadBytes || bytes->size() < kXyPayloadBytes) {
      return std::nullopt;
    }
    const auto x = read_rational(2U);
    const auto y = read_rational(10U);
    if (!x.has_value() || !y.has_value()) {
      return std::nullopt;
    }
    const auto result = DcpChromaticity{.x = *x, .y = *y};
    return result.valid()
               ? std::optional<ParsedIlluminantData>(ParsedIlluminantData{
                     .white = result,
                     .kind = DcpIlluminantDataKind::WhiteXy,
                 })
               : std::nullopt;
  }
  if (payload_type != 1U || entry.count < kSpectrumHeaderBytes ||
      bytes->size() < kSpectrumHeaderBytes) {
    return std::nullopt;
  }

  const auto sample_count = read_u32(2U);
  if (sample_count < kMinimumSpectrumSamples ||
      sample_count > kMaximumSpectrumSamples) {
    return std::nullopt;
  }
  const auto expected_bytes = kSpectrumHeaderBytes +
                              static_cast<std::size_t>(sample_count) * 8U;
  if (entry.count < expected_bytes || bytes->size() < expected_bytes) {
    return std::nullopt;
  }
  const auto minimum_wavelength = read_rational(6U);
  const auto wavelength_spacing = read_rational(14U);
  if (!minimum_wavelength.has_value() || !wavelength_spacing.has_value() ||
      *minimum_wavelength <= 0.0 || *wavelength_spacing <= 0.0) {
    return std::nullopt;
  }
  auto spectrum = std::vector<double>{};
  spectrum.reserve(sample_count);
  for (std::uint32_t index = 0U; index < sample_count; ++index) {
    const auto value = read_rational(
        kSpectrumHeaderBytes + static_cast<std::size_t>(index) * 8U);
    if (!value.has_value()) {
      return std::nullopt;
    }
    spectrum.push_back(*value);
  }
  const auto maximum_wavelength =
      *minimum_wavelength +
      *wavelength_spacing * static_cast<double>(sample_count - 1U);
  if (!std::isfinite(maximum_wavelength) ||
      maximum_wavelength <= *minimum_wavelength) {
    return std::nullopt;
  }

  const auto sample_spectrum = [&](double wavelength) {
    const auto clamped = std::clamp(
        wavelength,
        *minimum_wavelength,
        maximum_wavelength);
    const auto coordinate =
        (clamped - *minimum_wavelength) / *wavelength_spacing;
    const auto lower = std::min(
        static_cast<std::size_t>(std::floor(coordinate)),
        spectrum.size() - 1U);
    const auto upper = std::min(lower + 1U, spectrum.size() - 1U);
    return std::lerp(
        spectrum[lower],
        spectrum[upper],
        coordinate - static_cast<double>(lower));
  };
  auto sum = std::array<double, 3>{};
  auto observer_sum = std::array<double, 3>{};
  for (int wavelength = 360; wavelength <= 830; ++wavelength) {
    const auto observer = detail::cie1931_observer(
        static_cast<double>(wavelength));
    const auto light = sample_spectrum(static_cast<double>(wavelength));
    for (std::size_t channel = 0U; channel < sum.size(); ++channel) {
      observer_sum[channel] += observer[channel];
      sum[channel] += observer[channel] * light;
    }
  }
  for (std::size_t channel = 0U; channel < sum.size(); ++channel) {
    if (!std::isfinite(sum[channel]) || observer_sum[channel] <= 0.0) {
      return std::nullopt;
    }
    sum[channel] /= observer_sum[channel];
    if (sum[channel] <= 0.0) {
      return std::nullopt;
    }
  }
  const auto xyz_sum = sum[0] + sum[1] + sum[2];
  if (!std::isfinite(xyz_sum) || xyz_sum <= 0.0) {
    return std::nullopt;
  }
  const auto result = DcpChromaticity{
      .x = sum[0] / xyz_sum,
      .y = sum[1] / xyz_sum,
  };
  return result.valid()
             ? std::optional<ParsedIlluminantData>(ParsedIlluminantData{
                   .white = result,
                   .kind = DcpIlluminantDataKind::Spectrum,
               })
             : std::nullopt;
}

[[nodiscard]] float half_to_float(std::uint16_t bits) {
  const auto negative = (bits & 0x8000U) != 0U;
  const auto exponent = static_cast<unsigned int>((bits >> 10U) & 0x1FU);
  const auto fraction = static_cast<unsigned int>(bits & 0x03FFU);
  auto value = 0.0F;
  if (exponent == 0U) {
    value = std::ldexp(static_cast<float>(fraction), -24);
  } else if (exponent == 31U) {
    value = fraction == 0U
                ? std::numeric_limits<float>::infinity()
                : std::numeric_limits<float>::quiet_NaN();
  } else {
    value = std::ldexp(
        static_cast<float>(1024U + fraction),
        static_cast<int>(exponent) - 25);
  }
  return negative ? -value : value;
}

[[nodiscard]] std::optional<DcpGainTableMap> read_profile_gain_table_map2(
    const TiffReader& reader,
    const IfdEntry& entry) {
  constexpr std::size_t kHeaderBytes = 80U;
  constexpr float kMinimumGain = 1.0F / 4096.0F;
  constexpr float kMaximumGain = 4096.0F;
  if (entry.type != kTypeUndefined || entry.count < kHeaderBytes) {
    return std::nullopt;
  }
  const auto bytes = reader.entry_bytes(entry);
  if (!bytes.has_value() || bytes->size() < kHeaderBytes) {
    return std::nullopt;
  }
  const auto read_u16 = [&](std::size_t offset) {
    const auto first = std::to_integer<std::uint8_t>((*bytes)[offset]);
    const auto second = std::to_integer<std::uint8_t>((*bytes)[offset + 1U]);
    return reader.byte_order() == ByteOrder::Little
               ? static_cast<std::uint16_t>(first | (second << 8U))
               : static_cast<std::uint16_t>((first << 8U) | second);
  };
  const auto read_u32 = [&](std::size_t offset) {
    auto result = std::uint32_t{0U};
    if (reader.byte_order() == ByteOrder::Little) {
      for (std::size_t index = 0U; index < 4U; ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>((*bytes)[offset + index]))
                  << static_cast<unsigned int>(index * 8U);
      }
    } else {
      for (std::size_t index = 0U; index < 4U; ++index) {
        result = (result << 8U) |
                 std::to_integer<std::uint8_t>((*bytes)[offset + index]);
      }
    }
    return result;
  };
  const auto read_u64 = [&](std::size_t offset) {
    auto result = std::uint64_t{0U};
    if (reader.byte_order() == ByteOrder::Little) {
      for (std::size_t index = 0U; index < 8U; ++index) {
        result |= static_cast<std::uint64_t>(
                      std::to_integer<std::uint8_t>((*bytes)[offset + index]))
                  << static_cast<unsigned int>(index * 8U);
      }
    } else {
      for (std::size_t index = 0U; index < 8U; ++index) {
        result = (result << 8U) |
                 std::to_integer<std::uint8_t>((*bytes)[offset + index]);
      }
    }
    return result;
  };
  const auto read_float = [&](std::size_t offset) {
    return std::bit_cast<float>(read_u32(offset));
  };
  const auto read_double = [&](std::size_t offset) {
    return std::bit_cast<double>(read_u64(offset));
  };

  auto result = DcpGainTableMap{};
  result.points_v = read_u32(0U);
  result.points_h = read_u32(4U);
  result.spacing_v = read_double(8U);
  result.spacing_h = read_double(16U);
  result.origin_v = read_double(24U);
  result.origin_h = read_double(32U);
  result.table_points = read_u32(40U);
  for (std::size_t index = 0U; index < result.input_weights.size(); ++index) {
    result.input_weights[index] = read_float(44U + index * 4U);
    if (!std::isfinite(result.input_weights[index])) {
      return std::nullopt;
    }
  }
  const auto data_type = read_u32(64U);
  result.gamma = read_float(68U);
  const auto gain_min = read_float(72U);
  const auto gain_max = read_float(76U);
  if (result.points_v == 0U || result.points_h == 0U ||
      result.table_points == 0U || data_type > 3U ||
      !std::isfinite(result.gamma) || result.gamma < 0.125F ||
      result.gamma > 8.0F || !std::isfinite(gain_min) ||
      gain_min < kMinimumGain || !std::isfinite(gain_max) ||
      gain_max > kMaximumGain || gain_min > gain_max) {
    return std::nullopt;
  }
  if (result.points_v == 1U) {
    result.spacing_v = 1.0;
    result.origin_v = 0.0;
  }
  if (result.points_h == 1U) {
    result.spacing_h = 1.0;
    result.origin_h = 0.0;
  }
  const auto spacing_v_float = static_cast<float>(result.spacing_v);
  const auto spacing_h_float = static_cast<float>(result.spacing_h);
  const auto origin_v_float = static_cast<float>(result.origin_v);
  const auto origin_h_float = static_cast<float>(result.origin_h);
  if (!std::isfinite(spacing_v_float) || spacing_v_float <= 0.0F ||
      !std::isfinite(spacing_h_float) || spacing_h_float <= 0.0F ||
      !std::isfinite(origin_v_float) || !std::isfinite(origin_h_float)) {
    return std::nullopt;
  }
  if (result.points_v > kMaximumDcpTableEntries / result.points_h) {
    return std::nullopt;
  }
  const auto spatial_points =
      static_cast<std::size_t>(result.points_v) * result.points_h;
  if (result.table_points > kMaximumDcpTableEntries / spatial_points) {
    return std::nullopt;
  }
  const auto sample_count = spatial_points * result.table_points;
  const auto bytes_per_sample = data_type == 0U ? 1U : (data_type <= 2U ? 2U : 4U);
  const auto expected_bytes = kHeaderBytes + sample_count * bytes_per_sample;
  if (entry.count < expected_bytes || bytes->size() < expected_bytes) {
    return std::nullopt;
  }
  result.gains.reserve(sample_count);
  auto offset = kHeaderBytes;
  for (std::size_t index = 0U; index < sample_count; ++index) {
    auto gain = 0.0F;
    if (data_type == 0U) {
      const auto raw = std::to_integer<std::uint8_t>((*bytes)[offset]);
      gain = gain_min + static_cast<float>(raw) * (1.0F / 255.0F) *
                            (gain_max - gain_min);
    } else if (data_type == 1U) {
      gain = gain_min + static_cast<float>(read_u16(offset)) *
                            (1.0F / 65535.0F) * (gain_max - gain_min);
    } else if (data_type == 2U) {
      gain = half_to_float(read_u16(offset));
    } else {
      gain = read_float(offset);
    }
    if (!std::isfinite(gain) || gain < kMinimumGain || gain > kMaximumGain) {
      return std::nullopt;
    }
    result.gains.push_back(gain);
    offset += bytes_per_sample;
  }
  return result.configured() ? std::optional<DcpGainTableMap>(std::move(result))
                             : std::nullopt;
}

[[nodiscard]] std::optional<DcpRgbTables> read_rgb_tables(
    const TiffReader& reader,
    const IfdEntry& entry) {
  constexpr std::uint32_t kMaximumTables = 20U;
  constexpr std::uint8_t kMinimumDivisions = 2U;
  constexpr std::uint8_t kMaximumDivisions = 32U;
  constexpr std::size_t kFixedHeaderBytes = 8U;
  constexpr std::size_t kTableFieldsBytes = 5U;
  if (entry.type != kTypeUndefined || entry.count < kFixedHeaderBytes) {
    return std::nullopt;
  }
  const auto bytes = reader.entry_bytes(entry);
  if (!bytes.has_value() || bytes->size() != entry.count) {
    return std::nullopt;
  }
  const auto contains = [&](std::size_t offset, std::size_t count) {
    return offset <= bytes->size() && count <= bytes->size() - offset;
  };
  const auto read_u16 = [&](std::size_t offset) {
    const auto first = std::to_integer<std::uint8_t>((*bytes)[offset]);
    const auto second = std::to_integer<std::uint8_t>((*bytes)[offset + 1U]);
    return reader.byte_order() == ByteOrder::Little
               ? static_cast<std::uint16_t>(first | (second << 8U))
               : static_cast<std::uint16_t>((first << 8U) | second);
  };
  const auto read_u32 = [&](std::size_t offset) {
    auto value = std::uint32_t{0U};
    if (reader.byte_order() == ByteOrder::Little) {
      for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>((*bytes)[offset + index]))
                 << static_cast<unsigned int>(index * 8U);
      }
    } else {
      for (std::size_t index = 0U; index < 4U; ++index) {
        value = (value << 8U) |
                std::to_integer<std::uint8_t>((*bytes)[offset + index]);
      }
    }
    return value;
  };

  const auto table_count = read_u32(0U);
  const auto composite_method = read_u32(4U);
  if (table_count < 1U || table_count > kMaximumTables ||
      composite_method > 1U) {
    return std::nullopt;
  }
  auto result = DcpRgbTables{
      .composite_method =
          static_cast<DcpRgbTableCompositeMethod>(composite_method),
  };
  result.tables.reserve(table_count);
  auto total_entries = std::size_t{0U};
  auto background_count = std::size_t{0U};
  auto offset = kFixedHeaderBytes;
  for (std::uint32_t table_index = 0U; table_index < table_count;
       ++table_index) {
    if (!contains(offset, 2U)) {
      return std::nullopt;
    }
    const auto semantic_name_length =
        static_cast<std::size_t>(read_u16(offset));
    offset += 2U;
    if (!contains(offset, semantic_name_length) ||
        !contains(offset + semantic_name_length, kTableFieldsBytes)) {
      return std::nullopt;
    }
    auto semantic_name = std::string{};
    semantic_name.reserve(semantic_name_length);
    for (std::size_t index = 0U; index < semantic_name_length; ++index) {
      const auto character =
          std::to_integer<std::uint8_t>((*bytes)[offset + index]);
      if (character == 0U || character > 0x7FU) {
        return std::nullopt;
      }
      semantic_name.push_back(static_cast<char>(character));
    }
    offset += semantic_name_length;
    const auto divisions =
        std::to_integer<std::uint8_t>((*bytes)[offset]);
    const auto pixel_type =
        std::to_integer<std::uint8_t>((*bytes)[offset + 1U]);
    const auto gamma =
        std::to_integer<std::uint8_t>((*bytes)[offset + 2U]);
    const auto primaries =
        std::to_integer<std::uint8_t>((*bytes)[offset + 3U]);
    const auto gamut_extension =
        std::to_integer<std::uint8_t>((*bytes)[offset + 4U]);
    offset += kTableFieldsBytes;
    if (divisions < kMinimumDivisions || divisions > kMaximumDivisions ||
        pixel_type > 2U || gamma > 4U || primaries > 4U ||
        gamut_extension > 1U) {
      return std::nullopt;
    }
    if (semantic_name.empty()) {
      ++background_count;
      if (background_count > 1U) {
        return std::nullopt;
      }
    }
    std::size_t plane_entries = 0U;
    std::size_t table_entries = 0U;
    if (!checked_multiply(
            static_cast<std::size_t>(divisions),
            static_cast<std::size_t>(divisions),
            &plane_entries) ||
        !checked_multiply(
            plane_entries,
            static_cast<std::size_t>(divisions),
            &table_entries) ||
        table_entries > kMaximumDcpTableEntries - total_entries) {
      return std::nullopt;
    }
    total_entries += table_entries;
    std::size_t scalar_count = 0U;
    std::size_t data_bytes = 0U;
    const auto bytes_per_sample = pixel_type == 0U ? 1U :
                                  pixel_type == 1U ? 2U : 4U;
    if (!checked_multiply(table_entries, 3U, &scalar_count) ||
        !checked_multiply(scalar_count, bytes_per_sample, &data_bytes) ||
        !contains(offset, data_bytes)) {
      return std::nullopt;
    }
    auto table = DcpRgbTable{
        .semantic_name = std::move(semantic_name),
        .divisions = divisions,
        .pixel_type = static_cast<DcpRgbTablePixelType>(pixel_type),
        .gamma = static_cast<DcpRgbTableGamma>(gamma),
        .primaries = static_cast<DcpRgbTablePrimaries>(primaries),
        .gamut_extension = gamut_extension == 1U,
    };
    table.entries.reserve(table_entries);
    const auto read_sample = [&](std::size_t sample_offset) {
      if (pixel_type == 0U) {
        return static_cast<float>(
                   std::to_integer<std::uint8_t>((*bytes)[sample_offset])) /
               255.0F;
      }
      if (pixel_type == 1U) {
        return static_cast<float>(read_u16(sample_offset)) / 65535.0F;
      }
      return std::bit_cast<float>(read_u32(sample_offset));
    };
    for (std::size_t entry_index = 0U; entry_index < table_entries;
         ++entry_index) {
      auto value = std::array<float, 3>{};
      for (std::size_t channel = 0U; channel < value.size(); ++channel) {
        value[channel] = read_sample(
            offset + (entry_index * 3U + channel) * bytes_per_sample);
        if (!std::isfinite(value[channel]) || value[channel] < 0.0F ||
            value[channel] > 1.0F) {
          return std::nullopt;
        }
      }
      table.entries.push_back(value);
    }
    offset += data_bytes;
    result.tables.push_back(std::move(table));
  }
  if (offset != bytes->size() || !result.configured()) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] std::optional<std::vector<std::uint32_t>> read_unsigned_values(
    const TiffReader& reader,
    const IfdEntry& entry,
    std::uint16_t required_type) {
  if (entry.type != required_type ||
      (entry.type != kTypeShort && entry.type != kTypeLong)) {
    return std::nullopt;
  }
  const auto bytes = reader.entry_bytes(entry);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  std::vector<std::uint32_t> values;
  values.reserve(entry.count);
  for (std::size_t index = 0U; index < entry.count; ++index) {
    const auto offset = index * (entry.type == kTypeShort ? 2U : 4U);
    if (entry.type == kTypeShort) {
      if (offset > bytes->size() || 2U > bytes->size() - offset) {
        return std::nullopt;
      }
      const auto first = std::to_integer<std::uint8_t>((*bytes)[offset]);
      const auto second = std::to_integer<std::uint8_t>((*bytes)[offset + 1U]);
      values.push_back(
          reader.byte_order() == ByteOrder::Little
              ? static_cast<std::uint16_t>(
                    first | (static_cast<std::uint16_t>(second) << 8U))
              : static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(first) << 8U) | second));
    } else {
      if (offset > bytes->size() || 4U > bytes->size() - offset) {
        return std::nullopt;
      }
      std::uint32_t value = 0U;
      if (reader.byte_order() == ByteOrder::Little) {
        for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
          value |= static_cast<std::uint32_t>(
                       std::to_integer<std::uint8_t>((*bytes)[offset + byte_index]))
                   << static_cast<unsigned int>(byte_index * 8U);
        }
      } else {
        for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
          value = (value << 8U) |
                  std::to_integer<std::uint8_t>((*bytes)[offset + byte_index]);
        }
      }
      values.push_back(value);
    }
  }
  return values;
}

[[nodiscard]] bool matrix_is_nonsingular(const std::array<double, 9>& matrix) {
  const auto determinant =
      matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
      matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
      matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
  return std::isfinite(determinant) && std::abs(determinant) > 1e-12;
}

[[nodiscard]] std::optional<std::array<double, 9>> read_matrix(
    const TiffReader& reader,
    const IfdEntry& entry,
    bool require_nonsingular) {
  if (entry.count != 9U) {
    return std::nullopt;
  }
  const auto values = read_numeric_values(reader, entry, kTypeSRational);
  if (!values.has_value() || values->size() != 9U) {
    return std::nullopt;
  }
  std::array<double, 9> matrix{};
  std::copy(values->begin(), values->end(), matrix.begin());
  if (require_nonsingular && !matrix_is_nonsingular(matrix)) {
    return std::nullopt;
  }
  return matrix;
}

[[nodiscard]] bool checked_table_entry_count(
    const std::array<std::uint32_t, 3>& dimensions,
    std::size_t* entry_count) {
  if (dimensions[0] < 1U || dimensions[1] < 2U || dimensions[2] < 1U) {
    return false;
  }
  std::size_t hue_saturation = 0U;
  return checked_multiply(dimensions[0], dimensions[1], &hue_saturation) &&
         checked_multiply(hue_saturation, dimensions[2], entry_count) &&
         *entry_count <= kMaximumDcpTableEntries;
}

[[nodiscard]] std::optional<DcpHsvTable> read_hsv_table(
    const TiffReader& reader,
    const IfdEntry& dimensions_entry,
    const IfdEntry& data_entry,
    const IfdEntry* encoding_entry) {
  if (dimensions_entry.type != kTypeLong || dimensions_entry.count != 3U) {
    return std::nullopt;
  }
  const auto dimensions_values = read_unsigned_values(reader, dimensions_entry, kTypeLong);
  if (!dimensions_values.has_value() || dimensions_values->size() != 3U) {
    return std::nullopt;
  }
  DcpHsvTable table;
  std::copy(dimensions_values->begin(), dimensions_values->end(), table.dimensions.begin());
  std::size_t entry_count = 0U;
  if (!checked_table_entry_count(table.dimensions, &entry_count)) {
    return std::nullopt;
  }
  std::size_t scalar_count = 0U;
  if (!checked_multiply(entry_count, 3U, &scalar_count) ||
      scalar_count > std::numeric_limits<std::uint32_t>::max() ||
      data_entry.type != kTypeFloat || data_entry.count != scalar_count) {
    return std::nullopt;
  }
  const auto values = read_numeric_values(reader, data_entry, kTypeFloat);
  if (!values.has_value() || values->size() != scalar_count) {
    return std::nullopt;
  }
  if (encoding_entry != nullptr) {
    const auto encoding = read_unsigned_values(reader, *encoding_entry, kTypeLong);
    if (!encoding.has_value() || encoding->size() != 1U || encoding->front() > 1U) {
      return std::nullopt;
    }
    table.srgb_value_encoding = encoding->front() == 1U;
  }
  table.entries.reserve(entry_count);
  for (std::size_t index = 0U; index < entry_count; ++index) {
    const auto hue_shift = (*values)[index * 3U];
    const auto saturation_scale = (*values)[index * 3U + 1U];
    const auto value_scale = (*values)[index * 3U + 2U];
    if (saturation_scale < 0.0 || value_scale < 0.0 ||
        std::abs(hue_shift) > 3600.0 || saturation_scale > 64.0 || value_scale > 64.0) {
      return std::nullopt;
    }
    table.entries.push_back(DcpHsvDelta{
        .hue_shift_degrees = static_cast<float>(hue_shift),
        .saturation_scale = static_cast<float>(saturation_scale),
        .value_scale = static_cast<float>(value_scale),
    });
  }
  const auto saturation_divisions = static_cast<std::size_t>(table.dimensions[1]);
  for (std::size_t index = 0U; index < table.entries.size(); index += saturation_divisions) {
    if (std::abs(table.entries[index].value_scale - 1.0F) > 1e-5F) {
      return std::nullopt;
    }
  }
  return table;
}

[[nodiscard]] const IfdEntry* find_entry(
    const ProfileEntries& entries,
    std::uint16_t tag) {
  const auto found = entries.find(tag);
  return found == entries.end() ? nullptr : &found->second;
}

[[nodiscard]] std::optional<std::vector<std::pair<float, float>>> read_tone_curve(
    const TiffReader& reader,
    const IfdEntry& entry) {
  if (entry.type != kTypeFloat || entry.count < 4U || entry.count % 2U != 0U) {
    return std::nullopt;
  }
  const auto values = read_numeric_values(reader, entry, kTypeFloat);
  if (!values.has_value()) {
    return std::nullopt;
  }
  std::vector<std::pair<float, float>> result;
  result.reserve(values->size() / 2U);
  double previous_x = -1.0;
  double previous_y = -1.0;
  for (std::size_t index = 0U; index < values->size(); index += 2U) {
    const auto x = (*values)[index];
    const auto y = (*values)[index + 1U];
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 ||
        x <= previous_x || y < previous_y) {
      return std::nullopt;
    }
    result.emplace_back(static_cast<float>(x), static_cast<float>(y));
    previous_x = x;
    previous_y = y;
  }
  if (std::abs(result.front().first) > 1e-6F ||
      std::abs(result.front().second) > 1e-6F ||
      std::abs(result.back().first - 1.0F) > 1e-6F ||
      std::abs(result.back().second - 1.0F) > 1e-6F) {
    return std::nullopt;
  }
  return result;
}

struct TemperatureTableEntry final {
  double reciprocal_megakelvin;
  double u;
  double v;
  double slope;
};

// Robertson isotemperature-line data published by Wyszecki & Stiles,
// Color Science, 2nd edition, Table 1(3.11).
constexpr std::array<TemperatureTableEntry, 31> kDngTemperatureTable = {{
    {0.0, 0.18006, 0.26352, -0.24341},
    {10.0, 0.18066, 0.26589, -0.25479},
    {20.0, 0.18133, 0.26846, -0.26876},
    {30.0, 0.18208, 0.27119, -0.28539},
    {40.0, 0.18293, 0.27407, -0.30470},
    {50.0, 0.18388, 0.27709, -0.32675},
    {60.0, 0.18494, 0.28021, -0.35156},
    {70.0, 0.18611, 0.28342, -0.37915},
    {80.0, 0.18740, 0.28668, -0.40955},
    {90.0, 0.18880, 0.28997, -0.44278},
    {100.0, 0.19032, 0.29326, -0.47888},
    {125.0, 0.19462, 0.30141, -0.58204},
    {150.0, 0.19962, 0.30921, -0.70471},
    {175.0, 0.20525, 0.31647, -0.84901},
    {200.0, 0.21142, 0.32312, -1.0182},
    {225.0, 0.21807, 0.32909, -1.2168},
    {250.0, 0.22511, 0.33439, -1.4512},
    {275.0, 0.23247, 0.33904, -1.7298},
    {300.0, 0.24010, 0.34308, -2.0637},
    {325.0, 0.24702, 0.34655, -2.4681},
    {350.0, 0.25591, 0.34951, -2.9641},
    {375.0, 0.26400, 0.35200, -3.5814},
    {400.0, 0.27218, 0.35407, -4.3633},
    {425.0, 0.28039, 0.35577, -5.3762},
    {450.0, 0.28863, 0.35714, -6.7262},
    {475.0, 0.29685, 0.35823, -8.5955},
    {500.0, 0.30505, 0.35907, -11.324},
    {525.0, 0.31320, 0.35968, -15.628},
    {550.0, 0.32129, 0.36011, -23.325},
    {575.0, 0.32931, 0.36038, -40.770},
    {600.0, 0.33724, 0.36051, -116.45},
}};

constexpr double kDngTintScale = -3000.0;
constexpr DcpChromaticity kD50Chromaticity{.x = 0.3457, .y = 0.3585};

[[nodiscard]] ColorMatrix3 dcp_matrix(const std::array<double, 9>& values) {
  return ColorMatrix3{.values = values};
}

[[nodiscard]] ColorMatrix3 diagonal_matrix(const std::array<double, 3>& values) {
  return matrix3_from_rows(
      values[0], 0.0, 0.0,
      0.0, values[1], 0.0,
      0.0, 0.0, values[2]);
}

[[nodiscard]] std::optional<ColorMatrix3> invert_dcp_matrix(
    const ColorMatrix3& matrix) {
  const auto& values = matrix.values;
  const auto determinant =
      values[0] * (values[4] * values[8] - values[5] * values[7]) -
      values[1] * (values[3] * values[8] - values[5] * values[6]) +
      values[2] * (values[3] * values[7] - values[4] * values[6]);
  if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-12) {
    return std::nullopt;
  }
  return invert_matrix3(matrix);
}

[[nodiscard]] bool finite_matrix(const ColorMatrix3& matrix) {
  return std::all_of(
      matrix.values.cbegin(),
      matrix.values.cend(),
      [](double value) { return std::isfinite(value); });
}

[[nodiscard]] ColorMatrix3 interpolate_matrix(
    const std::array<double, 9>& first,
    const std::array<double, 9>& second,
    double second_weight) {
  ColorMatrix3 result;
  for (std::size_t index = 0U; index < result.values.size(); ++index) {
    result.values[index] =
        std::lerp(first[index], second[index], second_weight);
  }
  return result;
}

[[nodiscard]] ColorMatrix3 weighted_matrix(
    const ColorMatrix3& first,
    const ColorMatrix3& second,
    const ColorMatrix3& third,
    const std::array<double, 3>& weights) {
  auto result = ColorMatrix3{};
  for (std::size_t index = 0U; index < result.values.size(); ++index) {
    result.values[index] = first.values[index] * weights[0] +
                           second.values[index] * weights[1] +
                           third.values[index] * weights[2];
  }
  return result;
}

[[nodiscard]] std::array<double, 3> xyz_from_dcp_xy(
    const DcpChromaticity& xy) {
  return {
      xy.x / xy.y,
      1.0,
      (1.0 - xy.x - xy.y) / xy.y,
  };
}

[[nodiscard]] ColorMatrix3 scale_matrix(
    ColorMatrix3 matrix,
    double scale) {
  for (auto& value : matrix.values) {
    value *= scale;
  }
  return matrix;
}

[[nodiscard]] double dng_rounded_value(double value, double factor) {
  return std::round(value * factor) / factor;
}

[[nodiscard]] ColorMatrix3 rounded_dng_matrix(
    ColorMatrix3 matrix,
    double factor = 10000.0) {
  for (auto& value : matrix.values) {
    value = dng_rounded_value(value, factor);
  }
  return matrix;
}

[[nodiscard]] ColorMatrix3 canonical_dng_color_matrix(
    ColorMatrix3 matrix) {
  const auto mapped_white = multiply_matrix3_vector(
      matrix, xyz_from_dcp_xy(kD50Chromaticity));
  const auto maximum = *std::max_element(
      mapped_white.cbegin(), mapped_white.cend());
  if (std::isfinite(maximum) && maximum > 0.0 &&
      (maximum < 0.99 || maximum > 1.01)) {
    matrix = scale_matrix(matrix, 1.0 / maximum);
  }
  return rounded_dng_matrix(matrix);
}

[[nodiscard]] std::optional<ColorMatrix3> normalize_forward_matrix(
    ColorMatrix3 matrix) {
  const auto mapped_one = multiply_matrix3_vector(matrix, {1.0, 1.0, 1.0});
  const auto d50 = xyz_from_dcp_xy(kD50Chromaticity);
  for (std::size_t row = 0U; row < 3U; ++row) {
    if (!std::isfinite(mapped_one[row]) || std::abs(mapped_one[row]) <= 1.0e-12) {
      return std::nullopt;
    }
    const auto row_scale = d50[row] / mapped_one[row];
    for (std::size_t column = 0U; column < 3U; ++column) {
      matrix.values[row * 3U + column] *= row_scale;
    }
  }
  return finite_matrix(matrix) ? std::optional<ColorMatrix3>(matrix) : std::nullopt;
}

[[nodiscard]] std::optional<ColorMatrix3> canonical_dng_forward_matrix(
    const std::array<double, 9>& values) {
  return normalize_forward_matrix(rounded_dng_matrix(dcp_matrix(values)));
}

[[nodiscard]] std::array<double, 3> canonical_dng_analog_balance(
    std::array<double, 3> values) {
  const auto minimum = *std::min_element(values.cbegin(), values.cend());
  if (!std::isfinite(minimum) || minimum <= 0.0) {
    return values;
  }
  for (auto& value : values) {
    value = dng_rounded_value(value / minimum, 1000000.0);
  }
  return values;
}

[[nodiscard]] bool forward_matrix_maps_camera_white_to_d50(
    const std::array<double, 9>& values) {
  constexpr double kDngForwardMatrixTolerance = 0.01;
  const auto mapped_one = multiply_matrix3_vector(
      dcp_matrix(values),
      {1.0, 1.0, 1.0});
  const auto d50 = xyz_from_dcp_xy(kD50Chromaticity);
  for (std::size_t channel = 0U; channel < mapped_one.size(); ++channel) {
    if (!std::isfinite(mapped_one[channel]) ||
        std::abs(mapped_one[channel] - d50[channel]) >
            kDngForwardMatrixTolerance) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] ColorMatrix3 dng_map_white_matrix(
    const DcpChromaticity& source,
    const DcpChromaticity& destination) {
  const auto bradford = matrix3_from_rows(
      0.8951, 0.2664, -0.1614,
      -0.7502, 1.7135, 0.0367,
      0.0389, -0.0685, 1.0296);
  const auto inverse_bradford = invert_matrix3(bradford);
  auto source_cone = multiply_matrix3_vector(bradford, xyz_from_dcp_xy(source));
  auto destination_cone = multiply_matrix3_vector(
      bradford,
      xyz_from_dcp_xy(destination));
  std::array<double, 3> scale{};
  for (std::size_t channel = 0U; channel < scale.size(); ++channel) {
    source_cone[channel] = std::max(0.0, source_cone[channel]);
    destination_cone[channel] = std::max(0.0, destination_cone[channel]);
    scale[channel] = std::clamp(
        source_cone[channel] > 0.0
            ? destination_cone[channel] / source_cone[channel]
            : 10.0,
        0.1,
        10.0);
  }
  return multiply_matrix3(
      inverse_bradford,
      multiply_matrix3(diagonal_matrix(scale), bradford));
}

struct DcpHsv final {
  double hue = 0.0;
  double saturation = 0.0;
  double value = 0.0;
};

struct DcpHsvDelta64 final {
  double hue_shift_degrees = 0.0;
  double saturation_scale = 1.0;
  double value_scale = 1.0;
};

[[nodiscard]] bool valid_hsv_table(const DcpHsvTable& table) {
  const auto hues = static_cast<std::size_t>(table.dimensions[0]);
  const auto saturations = static_cast<std::size_t>(table.dimensions[1]);
  const auto values = static_cast<std::size_t>(table.dimensions[2]);
  if (hues < 1U || saturations < 2U || values < 1U ||
      hues > kMaximumDcpTableEntries ||
      saturations > kMaximumDcpTableEntries ||
      values > kMaximumDcpTableEntries) {
    return false;
  }
  std::size_t hue_saturation = 0U;
  std::size_t expected = 0U;
  return checked_multiply(hues, saturations, &hue_saturation) &&
         checked_multiply(hue_saturation, values, &expected) &&
         expected == table.entries.size() &&
         expected <= kMaximumDcpTableEntries;
}

[[nodiscard]] DcpHsv rgb_to_dcp_hsv(const DcpRgb& rgb) {
  const auto maximum = std::max({rgb.red, rgb.green, rgb.blue});
  const auto minimum = std::min({rgb.red, rgb.green, rgb.blue});
  const auto chroma = maximum - minimum;
  DcpHsv hsv{
      .hue = 0.0,
      .saturation = maximum > 0.0 ? chroma / maximum : 0.0,
      .value = maximum,
  };
  if (chroma <= 1.0e-15) {
    return hsv;
  }
  if (maximum == rgb.red) {
    hsv.hue = (rgb.green - rgb.blue) / chroma;
    if (hsv.hue < 0.0) {
      hsv.hue += 6.0;
    }
  } else if (maximum == rgb.green) {
    hsv.hue = 2.0 + (rgb.blue - rgb.red) / chroma;
  } else {
    hsv.hue = 4.0 + (rgb.red - rgb.green) / chroma;
  }
  hsv.hue /= 6.0;
  return hsv;
}

[[nodiscard]] DcpRgb dcp_hsv_to_rgb(DcpHsv hsv) {
  hsv.hue -= std::floor(hsv.hue);
  hsv.saturation = std::clamp(hsv.saturation, 0.0, 1.0);
  const auto hue_six = hsv.hue * 6.0;
  const auto sector = static_cast<int>(std::floor(hue_six)) % 6;
  const auto fraction = hue_six - std::floor(hue_six);
  const auto p = hsv.value * (1.0 - hsv.saturation);
  const auto q = hsv.value * (1.0 - hsv.saturation * fraction);
  const auto t = hsv.value * (1.0 - hsv.saturation * (1.0 - fraction));
  switch (sector) {
    case 0:
      return {hsv.value, t, p};
    case 1:
      return {q, hsv.value, p};
    case 2:
      return {p, hsv.value, t};
    case 3:
      return {p, q, hsv.value};
    case 4:
      return {t, p, hsv.value};
    default:
      return {hsv.value, p, q};
  }
}

[[nodiscard]] double srgb_encode_unit(double value) {
  const auto x = std::clamp(value, 0.0, 1.0);
  return x <= 0.0031308
             ? 12.92 * x
             : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
}

[[nodiscard]] double srgb_decode_unit(double value) {
  const auto x = std::clamp(value, 0.0, 1.0);
  return x <= 0.04045
             ? x / 12.92
             : std::pow((x + 0.055) / 1.055, 2.4);
}

[[nodiscard]] RgbColorSpace dcp_rgb_table_color_space(
    DcpRgbTablePrimaries primaries) {
  auto result = make_linear_prophoto_rgb_color_space();
  switch (primaries) {
    case DcpRgbTablePrimaries::Srgb:
      result = make_linear_srgb_color_space();
      break;
    case DcpRgbTablePrimaries::AdobeRgb:
      result = make_adobe_rgb_color_space();
      result.transfer_function = TransferFunction::Linear;
      result.scene_referred = true;
      break;
    case DcpRgbTablePrimaries::DisplayP3:
      result = make_linear_display_p3_color_space();
      break;
    case DcpRgbTablePrimaries::Rec2020:
      result = make_linear_rec2020_color_space();
      break;
    case DcpRgbTablePrimaries::ProPhotoRgb:
      break;
  }
  return result;
}

[[nodiscard]] double dcp_rgb_table_gamma_encode(
    DcpRgbTableGamma gamma,
    double value) {
  const auto x = std::clamp(value, 0.0, 1.0);
  switch (gamma) {
    case DcpRgbTableGamma::Linear:
      return x;
    case DcpRgbTableGamma::Srgb:
      return linear_to_srgb(x);
    case DcpRgbTableGamma::Gamma18:
      return std::pow(x, 1.0 / 1.8);
    case DcpRgbTableGamma::Gamma22:
      return std::pow(x, 1.0 / 2.2);
    case DcpRgbTableGamma::Rec2020: {
      constexpr double kAlpha = 1.09929682680944;
      constexpr double kBeta = 0.018053968510807;
      return x < kBeta
                 ? 4.5 * x
                 : kAlpha * std::pow(x, 0.45) - (kAlpha - 1.0);
    }
  }
  return x;
}

[[nodiscard]] double dcp_rgb_table_gamma_decode(
    DcpRgbTableGamma gamma,
    double value) {
  const auto x = std::clamp(value, 0.0, 1.0);
  switch (gamma) {
    case DcpRgbTableGamma::Linear:
      return x;
    case DcpRgbTableGamma::Srgb:
      return srgb_to_linear(x);
    case DcpRgbTableGamma::Gamma18:
      return std::pow(x, 1.8);
    case DcpRgbTableGamma::Gamma22:
      return std::pow(x, 2.2);
    case DcpRgbTableGamma::Rec2020: {
      constexpr double kAlpha = 1.09929682680944;
      constexpr double kBeta = 0.018053968510807;
      return x < 4.5 * kBeta
                 ? x / 4.5
                 : std::pow((x + kAlpha - 1.0) / kAlpha, 1.0 / 0.45);
    }
  }
  return x;
}

[[nodiscard]] double dcp_hdr_table_encode(double value) {
  const auto sign = std::copysign(1.0, value);
  const auto magnitude = std::abs(value);
  return sign * magnitude * (256.0 + magnitude) /
         (256.0 * (1.0 + magnitude));
}

[[nodiscard]] double dcp_hdr_table_decode(double value) {
  const auto sign = std::copysign(1.0, value);
  const auto x = std::abs(value);
  const auto radicand = 64.0 * x * x - 127.0 * x + 64.0;
  return sign * 16.0 *
         (8.0 * x - 8.0 + std::sqrt(std::max(radicand, 0.0)));
}

[[nodiscard]] DcpRgb transform_dcp_rgb(
    const std::array<double, 9>& matrix,
    const DcpRgb& rgb) {
  return {
      matrix[0] * rgb.red + matrix[1] * rgb.green + matrix[2] * rgb.blue,
      matrix[3] * rgb.red + matrix[4] * rgb.green + matrix[5] * rgb.blue,
      matrix[6] * rgb.red + matrix[7] * rgb.green + matrix[8] * rgb.blue,
  };
}

[[nodiscard]] DcpRgb sample_dcp_rgb_table(
    const DcpRgbTable& table,
    const DcpRgb& encoded_rgb) {
  const auto scale = static_cast<double>(table.divisions - 1U);
  const auto coordinate = [&](double value) {
    const auto scaled = std::clamp(value, 0.0, 1.0) * scale;
    const auto lower = std::min(
        static_cast<std::size_t>(std::floor(scaled)),
        static_cast<std::size_t>(table.divisions - 1U));
    const auto upper = std::min(
        lower + 1U,
        static_cast<std::size_t>(table.divisions - 1U));
    return std::tuple{lower, upper, scaled - static_cast<double>(lower)};
  };
  const auto [red0, red1, red_fraction] = coordinate(encoded_rgb.red);
  const auto [green0, green1, green_fraction] = coordinate(encoded_rgb.green);
  const auto [blue0, blue1, blue_fraction] = coordinate(encoded_rgb.blue);
  const auto divisions = static_cast<std::size_t>(table.divisions);
  const auto at = [&](std::size_t red,
                      std::size_t green,
                      std::size_t blue,
                      std::size_t channel) {
    return static_cast<double>(
        table.entries[(red * divisions + green) * divisions + blue][channel]);
  };
  const auto component = [&](std::size_t channel) {
    const auto rg0b0 = std::lerp(
        at(red0, green0, blue0, channel),
        at(red1, green0, blue0, channel),
        red_fraction);
    const auto rg0b1 = std::lerp(
        at(red0, green0, blue1, channel),
        at(red1, green0, blue1, channel),
        red_fraction);
    const auto rg1b0 = std::lerp(
        at(red0, green1, blue0, channel),
        at(red1, green1, blue0, channel),
        red_fraction);
    const auto rg1b1 = std::lerp(
        at(red0, green1, blue1, channel),
        at(red1, green1, blue1, channel),
        red_fraction);
    return std::lerp(
        std::lerp(rg0b0, rg1b0, green_fraction),
        std::lerp(rg0b1, rg1b1, green_fraction),
        blue_fraction);
  };
  return {component(0U), component(1U), component(2U)};
}

[[nodiscard]] DcpRgb apply_dcp_rgb_table(
    const DcpRgbTable& table,
    const DcpProfileRenderPlan::RgbTableTransform& transform,
    bool hdr,
    DcpRgb rgb) {
  auto table_rgb = transform_dcp_rgb(transform.profile_to_table, rgb);
  auto encoded = std::array<double, 3>{
      hdr ? dcp_hdr_table_encode(table_rgb.red) : table_rgb.red,
      hdr ? dcp_hdr_table_encode(table_rgb.green) : table_rgb.green,
      hdr ? dcp_hdr_table_encode(table_rgb.blue) : table_rgb.blue,
  };
  auto clipped = encoded;
  auto delta = std::array<double, 3>{};
  for (std::size_t channel = 0U; channel < clipped.size(); ++channel) {
    clipped[channel] = std::clamp(clipped[channel], 0.0, 1.0);
    delta[channel] = table.gamut_extension
                         ? encoded[channel] - clipped[channel]
                         : 0.0;
    clipped[channel] = dcp_rgb_table_gamma_encode(table.gamma, clipped[channel]);
  }
  const auto sampled = sample_dcp_rgb_table(
      table,
      {clipped[0], clipped[1], clipped[2]});
  auto decoded = std::array<double, 3>{
      dcp_rgb_table_gamma_decode(table.gamma, sampled.red) + delta[0],
      dcp_rgb_table_gamma_decode(table.gamma, sampled.green) + delta[1],
      dcp_rgb_table_gamma_decode(table.gamma, sampled.blue) + delta[2],
  };
  if (hdr) {
    for (auto& channel : decoded) {
      channel = dcp_hdr_table_decode(channel);
    }
  }
  return transform_dcp_rgb(
      transform.table_to_profile,
      {decoded[0], decoded[1], decoded[2]});
}

[[nodiscard]] DcpHsvDelta64 sample_hsv_table(
    const DcpHsvTable& table,
    const DcpHsv& hsv) {
  if (!valid_hsv_table(table)) {
    return {};
  }
  const auto hue_divisions = static_cast<std::size_t>(table.dimensions[0]);
  const auto saturation_divisions = static_cast<std::size_t>(table.dimensions[1]);
  const auto value_divisions = static_cast<std::size_t>(table.dimensions[2]);
  const auto hue_scaled = hue_divisions < 2U
                              ? 0.0
                              : (hsv.hue - std::floor(hsv.hue)) *
                                    static_cast<double>(hue_divisions);
  const auto saturation_scaled =
      std::clamp(hsv.saturation, 0.0, 1.0) *
      static_cast<double>(saturation_divisions - 1U);
  auto value_for_lookup = std::clamp(hsv.value, 0.0, 1.0);
  if (value_divisions >= 2U && table.srgb_value_encoding) {
    value_for_lookup = srgb_encode_unit(value_for_lookup);
  }
  const auto value_scaled =
      value_for_lookup * static_cast<double>(value_divisions - 1U);

  auto hue0 = static_cast<std::size_t>(std::floor(hue_scaled));
  auto hue1 = hue0 + 1U;
  if (hue0 >= hue_divisions - 1U) {
    hue0 = hue_divisions - 1U;
    hue1 = 0U;
  }
  auto saturation0 = static_cast<std::size_t>(std::floor(saturation_scaled));
  saturation0 = std::min(saturation0, saturation_divisions - 2U);
  const auto saturation1 = saturation0 + 1U;
  auto value0 = std::size_t{0};
  auto value1 = std::size_t{0};
  if (value_divisions >= 2U) {
    value0 = std::min(
        static_cast<std::size_t>(std::floor(value_scaled)),
        value_divisions - 2U);
    value1 = value0 + 1U;
  }
  const auto hue_fraction = hue_scaled - static_cast<double>(hue0);
  const auto saturation_fraction =
      saturation_scaled - static_cast<double>(saturation0);
  const auto value_fraction = value_divisions >= 2U
                                  ? value_scaled - static_cast<double>(value0)
                                  : 0.0;
  const auto index = [&](std::size_t value_index,
                         std::size_t hue_index,
                         std::size_t saturation_index) {
    return (value_index * hue_divisions + hue_index) *
               saturation_divisions +
           saturation_index;
  };
  const auto component = [&](auto member) {
    const auto at = [&](std::size_t v, std::size_t h, std::size_t s) {
      return static_cast<double>(table.entries[index(v, h, s)].*member);
    };
    const auto hue_sat0_value0 = std::lerp(
        at(value0, hue0, saturation0),
        at(value0, hue1, saturation0),
        hue_fraction);
    const auto hue_sat1_value0 = std::lerp(
        at(value0, hue0, saturation1),
        at(value0, hue1, saturation1),
        hue_fraction);
    const auto value0_result = std::lerp(
        hue_sat0_value0,
        hue_sat1_value0,
        saturation_fraction);
    if (value_divisions < 2U) {
      return value0_result;
    }
    const auto hue_sat0_value1 = std::lerp(
        at(value1, hue0, saturation0),
        at(value1, hue1, saturation0),
        hue_fraction);
    const auto hue_sat1_value1 = std::lerp(
        at(value1, hue0, saturation1),
        at(value1, hue1, saturation1),
        hue_fraction);
    return std::lerp(
        value0_result,
        std::lerp(
            hue_sat0_value1,
            hue_sat1_value1,
            saturation_fraction),
        value_fraction);
  };
  return DcpHsvDelta64{
      .hue_shift_degrees = component(&DcpHsvDelta::hue_shift_degrees),
      .saturation_scale = component(&DcpHsvDelta::saturation_scale),
      .value_scale = component(&DcpHsvDelta::value_scale),
  };
}

[[nodiscard]] DcpRgb apply_hsv_delta(
    const DcpHsvTable& table,
    const DcpHsvDelta64& delta,
    const DcpRgb& rgb) {
  if (!std::isfinite(rgb.red) || !std::isfinite(rgb.green) ||
      !std::isfinite(rgb.blue)) {
    return rgb;
  }
  const auto proxy = DcpRgb{
      .red = std::clamp(rgb.red, 0.0, 1.0),
      .green = std::clamp(rgb.green, 0.0, 1.0),
      .blue = std::clamp(rgb.blue, 0.0, 1.0),
  };
  auto hsv = rgb_to_dcp_hsv(proxy);
  hsv.hue += delta.hue_shift_degrees / 360.0;
  hsv.saturation = std::min(hsv.saturation * delta.saturation_scale, 1.0);
  auto encoded_value = hsv.value;
  if (table.dimensions[2] >= 2U && table.srgb_value_encoding) {
    encoded_value = srgb_encode_unit(encoded_value);
  }
  encoded_value = std::clamp(encoded_value * delta.value_scale, 0.0, 1.0);
  hsv.value = table.srgb_value_encoding
                  ? srgb_decode_unit(encoded_value)
                  : encoded_value;
  const auto transformed = dcp_hsv_to_rgb(hsv);
  const auto in_standard_domain =
      rgb.red >= 0.0 && rgb.red <= 1.0 &&
      rgb.green >= 0.0 && rgb.green <= 1.0 &&
      rgb.blue >= 0.0 && rgb.blue <= 1.0;
  if (in_standard_domain) {
    return transformed;
  }
  return DcpRgb{
      .red = rgb.red + transformed.red - proxy.red,
      .green = rgb.green + transformed.green - proxy.green,
      .blue = rgb.blue + transformed.blue - proxy.blue,
  };
}

[[nodiscard]] std::vector<double> solve_dng_spline_slopes(
    const std::vector<std::pair<float, float>>& points) {
  const auto count = points.size();
  if (count < 2U) {
    return {};
  }
  std::vector<double> slopes(count, 0.0);
  auto interval = static_cast<double>(points[1].first - points[0].first);
  auto secant = static_cast<double>(points[1].second - points[0].second) /
                interval;
  slopes[0] = secant;
  for (std::size_t index = 2U; index < count; ++index) {
    const auto next_interval =
        static_cast<double>(points[index].first - points[index - 1U].first);
    const auto next_secant =
        static_cast<double>(points[index].second - points[index - 1U].second) /
        next_interval;
    slopes[index - 1U] =
        (secant * next_interval + next_secant * interval) /
        (interval + next_interval);
    interval = next_interval;
    secant = next_secant;
  }
  slopes[count - 1U] = 2.0 * secant - slopes[count - 2U];
  slopes[0] = 2.0 * slopes[0] - slopes[1];
  if (count <= 2U) {
    return slopes;
  }
  std::vector<double> lower(count, 0.0);
  std::vector<double> upper(count, 0.0);
  std::vector<double> rhs(count, 0.0);
  upper[0] = 0.5;
  lower[count - 1U] = 0.5;
  rhs[0] = 0.75 * (slopes[0] + slopes[1]);
  rhs[count - 1U] =
      0.75 * (slopes[count - 2U] + slopes[count - 1U]);
  for (std::size_t index = 1U; index + 1U < count; ++index) {
    const auto span =
        static_cast<double>(points[index + 1U].first -
                            points[index - 1U].first) *
        2.0;
    lower[index] =
        static_cast<double>(points[index + 1U].first - points[index].first) /
        span;
    upper[index] =
        static_cast<double>(points[index].first - points[index - 1U].first) /
        span;
    rhs[index] = 1.5 * slopes[index];
  }
  for (std::size_t index = 1U; index < count; ++index) {
    const auto divisor = 1.0 - upper[index - 1U] * lower[index];
    if (index + 1U < count) {
      upper[index] /= divisor;
    }
    rhs[index] =
        (rhs[index] - rhs[index - 1U] * lower[index]) / divisor;
  }
  for (std::size_t index = count - 1U; index-- > 0U;) {
    rhs[index] -= upper[index] * rhs[index + 1U];
  }
  return rhs;
}

[[nodiscard]] double evaluate_dng_spline(
    const std::vector<std::pair<float, float>>& points,
    const std::vector<double>& slopes,
    double value) {
  if (points.size() < 2U || slopes.size() != points.size()) {
    return value;
  }
  if (value <= points.front().first) {
    return static_cast<double>(points.front().second) +
           slopes.front() * (value - points.front().first);
  }
  if (value >= points.back().first) {
    return static_cast<double>(points.back().second) +
           slopes.back() * (value - points.back().first);
  }
  const auto upper = std::lower_bound(
      points.cbegin(),
      points.cend(),
      value,
      [](const auto& point, double x) {
        return static_cast<double>(point.first) < x;
      });
  const auto upper_index = static_cast<std::size_t>(
      std::distance(points.cbegin(), upper));
  const auto lower_index = upper_index - 1U;
  const auto x0 = static_cast<double>(points[lower_index].first);
  const auto y0 = static_cast<double>(points[lower_index].second);
  const auto x1 = static_cast<double>(points[upper_index].first);
  const auto y1 = static_cast<double>(points[upper_index].second);
  const auto span = x1 - x0;
  const auto toward_upper = (value - x0) / span;
  const auto toward_lower = (x1 - value) / span;
  return ((y0 * (2.0 - toward_lower + toward_upper) +
           slopes[lower_index] * span * toward_upper) *
          toward_lower * toward_lower) +
         ((y1 * (2.0 - toward_upper + toward_lower) -
           slopes[upper_index] * span * toward_lower) *
          toward_upper * toward_upper);
}

[[nodiscard]] DcpRgb apply_rgb_tone_curve(
    const std::vector<std::pair<float, float>>& points,
    const std::vector<double>& slopes,
    const DcpRgb& rgb) {
  const std::array<double, 3> channels = {rgb.red, rgb.green, rgb.blue};
  const auto minimum = static_cast<std::size_t>(std::distance(
      channels.cbegin(),
      std::min_element(channels.cbegin(), channels.cend())));
  const auto maximum = static_cast<std::size_t>(std::distance(
      channels.cbegin(),
      std::max_element(channels.cbegin(), channels.cend())));
  std::array<double, 3> result{};
  if (std::abs(channels[maximum] - channels[minimum]) <= 1.0e-15) {
    const auto mapped = evaluate_dng_spline(points, slopes, channels[0]);
    result.fill(mapped);
  } else {
    const auto middle = 3U - minimum - maximum;
    result[minimum] = evaluate_dng_spline(points, slopes, channels[minimum]);
    result[maximum] = evaluate_dng_spline(points, slopes, channels[maximum]);
    result[middle] = std::lerp(
        result[minimum],
        result[maximum],
        (channels[middle] - channels[minimum]) /
            (channels[maximum] - channels[minimum]));
  }
  return {result[0], result[1], result[2]};
}

[[nodiscard]] std::optional<std::pair<double, double>>
dcp_xy_to_temperature_tint(const DcpChromaticity& xy) {
  if (!xy.valid()) {
    return std::nullopt;
  }
  const auto denominator = 1.5 - xy.x + 6.0 * xy.y;
  if (!std::isfinite(denominator) || std::abs(denominator) <= 1.0e-12) {
    return std::nullopt;
  }
  const auto u = 2.0 * xy.x / denominator;
  const auto v = 3.0 * xy.y / denominator;
  auto last_distance = 0.0;
  auto last_u_direction = 0.0;
  auto last_v_direction = 0.0;
  for (std::size_t index = 1U; index < kDngTemperatureTable.size(); ++index) {
    auto u_direction = 1.0;
    auto v_direction = kDngTemperatureTable[index].slope;
    const auto length = std::hypot(u_direction, v_direction);
    u_direction /= length;
    v_direction /= length;
    auto u_offset = u - kDngTemperatureTable[index].u;
    auto v_offset = v - kDngTemperatureTable[index].v;
    auto distance = -u_offset * v_direction + v_offset * u_direction;
    if (distance > 0.0 && index + 1U < kDngTemperatureTable.size()) {
      last_distance = distance;
      last_u_direction = u_direction;
      last_v_direction = v_direction;
      continue;
    }
    distance = std::max(-distance, 0.0);
    const auto first_weight = index == 1U
                                  ? 0.0
                                  : distance / (last_distance + distance);
    const auto reciprocal_megakelvin = std::lerp(
        kDngTemperatureTable[index].reciprocal_megakelvin,
        kDngTemperatureTable[index - 1U].reciprocal_megakelvin,
        first_weight);
    if (!std::isfinite(reciprocal_megakelvin) ||
        reciprocal_megakelvin <= 0.0) {
      return std::nullopt;
    }
    u_offset = u - std::lerp(
                       kDngTemperatureTable[index].u,
                       kDngTemperatureTable[index - 1U].u,
                       first_weight);
    v_offset = v - std::lerp(
                       kDngTemperatureTable[index].v,
                       kDngTemperatureTable[index - 1U].v,
                       first_weight);
    u_direction = std::lerp(
        u_direction, last_u_direction, first_weight);
    v_direction = std::lerp(
        v_direction, last_v_direction, first_weight);
    const auto direction_length = std::hypot(u_direction, v_direction);
    if (!std::isfinite(direction_length) || direction_length <= 1.0e-12) {
      return std::nullopt;
    }
    u_direction /= direction_length;
    v_direction /= direction_length;
    const auto tint =
        (u_offset * u_direction + v_offset * v_direction) * kDngTintScale;
    const auto temperature = 1.0e6 / reciprocal_megakelvin;
    if (!std::isfinite(temperature) || !std::isfinite(tint)) {
      return std::nullopt;
    }
    return std::pair{temperature, tint};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<DcpChromaticity> dcp_profile_illuminant_xy(
    const DcpProfile& profile,
    std::size_t index) {
  const std::array<const std::optional<std::uint16_t>*, 3> illuminants = {
      &profile.calibration_illuminant1,
      &profile.calibration_illuminant2,
      &profile.calibration_illuminant3,
  };
  const std::array<const std::optional<DcpChromaticity>*, 3> data = {
      &profile.illuminant_data1,
      &profile.illuminant_data2,
      &profile.illuminant_data3,
  };
  if (index >= illuminants.size() || !illuminants[index]->has_value()) {
    return std::nullopt;
  }
  switch (**illuminants[index]) {
    case 3U:
    case 17U:
      return DcpChromaticity{.x = 0.4476, .y = 0.4074};
    case 24U:
      return dcp_temperature_tint_to_xy(3200.0, 0.0);
    case 18U:
      return DcpChromaticity{.x = 0.348483, .y = 0.351747};
    case 19U:
      return DcpChromaticity{.x = 0.310061, .y = 0.316150};
    case 23U:
      return DcpChromaticity{.x = 0.3457, .y = 0.3585};
    case 1U:
    case 4U:
    case 9U:
    case 20U:
      return DcpChromaticity{.x = 0.3324, .y = 0.3474};
    case 10U:
    case 21U:
      return DcpChromaticity{.x = 0.3127, .y = 0.3290};
    case 11U:
    case 22U:
      return DcpChromaticity{.x = 0.2990, .y = 0.3149};
    case 12U:
      return DcpChromaticity{.x = 0.31310, .y = 0.33727};
    case 13U:
      return DcpChromaticity{.x = 0.34588, .y = 0.35875};
    case 2U:
      return DcpChromaticity{.x = 0.37208, .y = 0.37529};
    case 14U:
      return DcpChromaticity{.x = 0.37417, .y = 0.37281};
    case 15U:
      return DcpChromaticity{.x = 0.40910, .y = 0.39430};
    case 16U:
      return DcpChromaticity{.x = 0.44018, .y = 0.40329};
    case 255U:
      return *data[index];
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::optional<std::array<double, 3>>
dcp_triple_illuminant_weights(
    const DcpProfile& profile,
    const DcpChromaticity& source_white) {
  const auto source = dcp_xy_to_temperature_tint(source_white);
  auto calibration = std::array<std::pair<double, double>, 3>{};
  if (!source.has_value()) {
    return std::nullopt;
  }
  for (std::size_t index = 0U; index < calibration.size(); ++index) {
    const auto white = dcp_profile_illuminant_xy(profile, index);
    const auto temperature_tint = white.has_value()
                                      ? dcp_xy_to_temperature_tint(*white)
                                      : std::nullopt;
    if (!temperature_tint.has_value()) {
      return std::nullopt;
    }
    calibration[index] = *temperature_tint;
  }
  const auto mapped_point = [](const std::pair<double, double>& value) {
    return std::array<double, 2>{
        value.second / 200.0,
        std::min(1500.0 / value.first, 1.0),
    };
  };
  const auto source_point = mapped_point(*source);
  auto weights = std::array<double, 3>{};
  auto sum = 0.0;
  for (std::size_t index = 0U; index < weights.size(); ++index) {
    const auto point = mapped_point(calibration[index]);
    const auto delta_tint = source_point[0] - point[0];
    const auto delta_temperature = source_point[1] - point[1];
    weights[index] = 1.0 /
                     (delta_tint * delta_tint +
                      delta_temperature * delta_temperature + 1.0e-8);
    sum += weights[index];
  }
  for (auto& weight : weights) {
    weight /= sum;
    weight = weight * weight * (3.0 - 2.0 * weight);
    weight = std::clamp((weight - 0.02) / 0.98, 0.0, 1.0);
  }
  sum = weights[0] + weights[1] + weights[2];
  if (!std::isfinite(sum) || sum <= 0.0) {
    return std::nullopt;
  }
  weights[0] /= sum;
  weights[1] /= sum;
  weights[2] = std::max(1.0 - weights[0] - weights[1], 0.0);
  return weights;
}

}  // namespace

std::optional<std::pair<double, double>>
dcp_chromaticity_to_temperature_tint(const DcpChromaticity& xy) {
  return dcp_xy_to_temperature_tint(xy);
}

bool DcpHsvTable::configured() const {
  return dimensions[0] != 0U || dimensions[1] != 0U || dimensions[2] != 0U ||
         !entries.empty();
}

bool DcpGainTableMap::configured() const {
  constexpr float kMinimumGain = 1.0F / 4096.0F;
  constexpr float kMaximumGain = 4096.0F;
  if (points_v == 0U || points_h == 0U || table_points == 0U ||
      !std::isfinite(spacing_v) || spacing_v <= 0.0 ||
      !std::isfinite(spacing_h) || spacing_h <= 0.0 ||
      !std::isfinite(origin_v) || !std::isfinite(origin_h) ||
      !std::isfinite(gamma) || gamma < 0.125F || gamma > 8.0F) {
    return false;
  }
  if (points_v > kMaximumDcpTableEntries / points_h) {
    return false;
  }
  const auto spatial_points = static_cast<std::size_t>(points_v) * points_h;
  if (table_points > kMaximumDcpTableEntries / spatial_points ||
      gains.size() != spatial_points * table_points) {
    return false;
  }
  if (std::any_of(
          input_weights.cbegin(),
          input_weights.cend(),
          [](float weight) { return !std::isfinite(weight); })) {
    return false;
  }
  return std::all_of(
      gains.cbegin(),
      gains.cend(),
      [](float gain) {
        return std::isfinite(gain) && gain >= kMinimumGain &&
               gain <= kMaximumGain;
      });
}

bool DcpRgbTable::configured() const {
  if (divisions < 2U || divisions > 32U ||
      static_cast<std::uint8_t>(pixel_type) > 2U ||
      static_cast<std::uint8_t>(gamma) > 4U ||
      static_cast<std::uint8_t>(primaries) > 4U) {
    return false;
  }
  if (std::any_of(
          semantic_name.cbegin(),
          semantic_name.cend(),
          [](char character) {
            const auto value = static_cast<unsigned char>(character);
            return value == 0U || value > 0x7FU;
          })) {
    return false;
  }
  const auto division_count = static_cast<std::size_t>(divisions);
  const auto expected_entries =
      division_count * division_count * division_count;
  return entries.size() == expected_entries &&
         entries.size() <= kMaximumDcpTableEntries &&
         std::all_of(
             entries.cbegin(),
             entries.cend(),
             [](const std::array<float, 3>& entry) {
               return std::all_of(
                   entry.cbegin(),
                   entry.cend(),
                   [](float value) {
                     return std::isfinite(value) && value >= 0.0F &&
                            value <= 1.0F;
                   });
             });
}

bool DcpRgbTable::background() const {
  return semantic_name.empty();
}

bool DcpRgbTables::configured() const {
  if (tables.empty() || tables.size() > 20U ||
      static_cast<std::uint32_t>(composite_method) > 1U) {
    return false;
  }
  auto background_count = std::size_t{0U};
  auto entry_count = std::size_t{0U};
  for (const auto& table : tables) {
    if (!table.configured() ||
        table.entries.size() > kMaximumDcpTableEntries - entry_count) {
      return false;
    }
    entry_count += table.entries.size();
    background_count += table.background() ? 1U : 0U;
  }
  return background_count <= 1U;
}

bool DcpProfile::dual_illuminant() const {
  return calibration_illuminant1.has_value() && calibration_illuminant2.has_value() &&
         color_matrix1.has_value() && color_matrix2.has_value();
}

bool DcpProfile::triple_illuminant() const {
  return dual_illuminant() && calibration_illuminant3.has_value() &&
         color_matrix3.has_value();
}

bool DcpProfile::has_executable_forward_matrix() const {
  return forward_matrix1.has_value() &&
         (!dual_illuminant() || forward_matrix2.has_value()) &&
         (!triple_illuminant() || forward_matrix3.has_value());
}

bool DcpProfile::has_executable_color_transform() const {
  return color_matrix1.has_value() && calibration_illuminant1.has_value();
}

bool DcpChromaticity::valid() const {
  return std::isfinite(x) && std::isfinite(y) && x > 0.0 && y > 0.0 &&
         x < 1.0 && y < 1.0 && x + y < 1.0;
}

bool DcpParseResult::ready() const {
  return status == DcpParseStatus::Ready;
}

DcpParseResult parse_dcp_profile(std::span<const std::byte> payload) {
  if (payload.empty()) {
    return failure(DcpParseStatus::EmptyPayload, "dcp_payload_empty");
  }
  if (payload.size() > kMaximumDcpProfileBytes) {
    return failure(DcpParseStatus::PayloadTooLarge, "dcp_payload_exceeds_64_mib_limit");
  }
  if (payload.size() < 8U) {
    return failure(DcpParseStatus::TruncatedHeader, "dcp_header_truncated");
  }

  ByteOrder byte_order;
  if (payload[0] == std::byte{'I'} && payload[1] == std::byte{'I'}) {
    byte_order = ByteOrder::Little;
  } else if (payload[0] == std::byte{'M'} && payload[1] == std::byte{'M'}) {
    byte_order = ByteOrder::Big;
  } else {
    return failure(DcpParseStatus::InvalidByteOrder, "dcp_byte_order_invalid");
  }
  const TiffReader reader(payload, byte_order);
  const auto magic = reader.u16(2U);
  if (!magic.has_value()) {
    return failure(DcpParseStatus::TruncatedHeader, "dcp_magic_truncated");
  }
  if (*magic == kBigTiffMagic) {
    return failure(DcpParseStatus::UnsupportedBigTiff, "dcp_bigtiff_not_supported");
  }
  if (*magic != kTiffMagic && *magic != kDcpMagic) {
    return failure(DcpParseStatus::InvalidMagic, "dcp_tiff_magic_invalid");
  }
  const auto first_ifd = reader.u32(4U);
  if (!first_ifd.has_value()) {
    return failure(DcpParseStatus::TruncatedHeader, "dcp_first_ifd_offset_truncated");
  }
  if (*first_ifd == 0U) {
    return failure(DcpParseStatus::MissingColorMatrix, "dcp_profile_ifd_missing");
  }

  std::unordered_set<std::uint32_t> visited_offsets;
  std::vector<std::pair<std::uint32_t, ProfileEntries>> ifds;
  auto ifd_offset = *first_ifd;
  while (ifd_offset != 0U) {
    if (ifds.size() >= kMaximumIfds) {
      return failure(
          DcpParseStatus::TooManyIfds,
          "dcp_ifd_chain_exceeds_limit",
          ifd_offset);
    }
    if (!visited_offsets.insert(ifd_offset).second) {
      return failure(DcpParseStatus::IfdCycle, "dcp_ifd_cycle", ifd_offset);
    }
    const auto entry_count = reader.u16(ifd_offset);
    if (!entry_count.has_value()) {
      return failure(DcpParseStatus::TruncatedIfd, "dcp_ifd_entry_count_truncated", ifd_offset);
    }
    if (*entry_count > kMaximumIfdEntries) {
      return failure(
          DcpParseStatus::TooManyIfdEntries,
          "dcp_ifd_entry_count_exceeds_limit",
          ifd_offset);
    }
    std::size_t entries_bytes = 0U;
    std::size_t ifd_end = 0U;
    if (!checked_multiply(*entry_count, 12U, &entries_bytes) ||
        !checked_add(static_cast<std::size_t>(ifd_offset), 2U, &ifd_end) ||
        !checked_add(ifd_end, entries_bytes, &ifd_end) ||
        !reader.contains(ifd_end, 4U)) {
      return failure(DcpParseStatus::TruncatedIfd, "dcp_ifd_entries_truncated", ifd_offset);
    }

    ProfileEntries profile_entries;
    for (std::size_t index = 0U; index < *entry_count; ++index) {
      const auto entry_offset_size =
          static_cast<std::size_t>(ifd_offset) + 2U + index * 12U;
      if (entry_offset_size > std::numeric_limits<std::uint32_t>::max()) {
        return failure(DcpParseStatus::OffsetOutOfRange, "dcp_ifd_entry_offset_overflow", ifd_offset);
      }
      const auto tag = reader.u16(entry_offset_size);
      const auto type = reader.u16(entry_offset_size + 2U);
      const auto count = reader.u32(entry_offset_size + 4U);
      if (!tag.has_value() || !type.has_value() || !count.has_value()) {
        return failure(DcpParseStatus::TruncatedIfd, "dcp_ifd_entry_truncated", ifd_offset);
      }
      if (!is_profile_tag(*tag)) {
        continue;
      }
      if (!tiff_type_size(*type).has_value()) {
        return failure(
            DcpParseStatus::InvalidTagType,
            "dcp_profile_tag_type_unsupported",
            ifd_offset,
            *tag);
      }
      std::size_t profile_value_bytes = 0U;
      if (!checked_multiply(
              static_cast<std::size_t>(*count),
              *tiff_type_size(*type),
              &profile_value_bytes) ||
          profile_value_bytes > kMaximumDcpProfileBytes) {
        return failure(
            DcpParseStatus::CountOverflow,
            "dcp_profile_tag_count_exceeds_limit",
            ifd_offset,
            *tag);
      }
      IfdEntry entry{
          .tag = *tag,
          .type = *type,
          .count = *count,
          .entry_offset = static_cast<std::uint32_t>(entry_offset_size),
      };
      if (!reader.entry_bytes(entry).has_value()) {
        return failure(
            DcpParseStatus::OffsetOutOfRange,
            "dcp_profile_tag_payload_out_of_range",
            ifd_offset,
            *tag);
      }
      if (!profile_entries.emplace(*tag, entry).second) {
        return failure(
            DcpParseStatus::DuplicateTag,
            "dcp_profile_tag_duplicate",
            ifd_offset,
            *tag);
      }
    }
    ifds.emplace_back(ifd_offset, std::move(profile_entries));
    const auto next_ifd = reader.u32(ifd_end);
    if (!next_ifd.has_value()) {
      return failure(DcpParseStatus::TruncatedIfd, "dcp_next_ifd_offset_truncated", ifd_offset);
    }
    ifd_offset = *next_ifd;
  }

  const auto selected = std::find_if(
      ifds.begin(),
      ifds.end(),
      [](const auto& candidate) {
        return candidate.second.contains(kTagColorMatrix1) ||
               candidate.second.contains(kTagColorMatrix2);
      });
  if (selected == ifds.end()) {
    return failure(DcpParseStatus::MissingColorMatrix, "dcp_color_matrix_missing");
  }
  const auto selected_ifd_offset = selected->first;
  const auto& entries = selected->second;
  DcpProfile profile{
      .payload_sha256 = color_payload_sha256(payload),
      .byte_size = payload.size(),
  };

  const auto parse_text_tag = [&](std::uint16_t tag, std::string* destination) -> bool {
    const auto* entry = find_entry(entries, tag);
    if (entry == nullptr) {
      return true;
    }
    const auto value = read_text(reader, *entry);
    if (!value.has_value()) {
      return false;
    }
    *destination = *value;
    return true;
  };
  if (!parse_text_tag(kTagProfileName, &profile.profile_name) ||
      !parse_text_tag(kTagUniqueCameraModel, &profile.unique_camera_model) ||
      !parse_text_tag(kTagCameraCalibrationSignature, &profile.camera_calibration_signature) ||
      !parse_text_tag(kTagProfileCalibrationSignature, &profile.profile_calibration_signature) ||
      !parse_text_tag(kTagProfileCopyright, &profile.copyright)) {
    return failure(
        DcpParseStatus::InvalidTagValue,
        "dcp_profile_text_invalid",
        selected_ifd_offset);
  }

  const auto parse_illuminant = [&](std::uint16_t tag, std::optional<std::uint16_t>* destination) -> bool {
    const auto* entry = find_entry(entries, tag);
    if (entry == nullptr) {
      return true;
    }
    const auto value = read_unsigned_values(reader, *entry, kTypeShort);
    if (!value.has_value() || value->size() != 1U || value->front() > 255U) {
      return false;
    }
    *destination = static_cast<std::uint16_t>(value->front());
    return true;
  };
  if (!parse_illuminant(kTagCalibrationIlluminant1, &profile.calibration_illuminant1) ||
      !parse_illuminant(kTagCalibrationIlluminant2, &profile.calibration_illuminant2) ||
      !parse_illuminant(kTagCalibrationIlluminant3, &profile.calibration_illuminant3)) {
    return failure(
        DcpParseStatus::InvalidTagValue,
        "dcp_calibration_illuminant_invalid",
        selected_ifd_offset);
  }
  const auto parse_illuminant_data = [&](
      std::uint16_t tag,
      std::optional<DcpChromaticity>* destination,
      DcpIlluminantDataKind* kind) -> bool {
    const auto* entry = find_entry(entries, tag);
    if (entry == nullptr) {
      return true;
    }
    const auto value = read_illuminant_data(reader, *entry);
    if (!value.has_value()) {
      return false;
    }
    *destination = value->white;
    *kind = value->kind;
    return true;
  };
  if (!parse_illuminant_data(
          kTagIlluminantData1,
          &profile.illuminant_data1,
          &profile.illuminant_data_kinds[0]) ||
      !parse_illuminant_data(
          kTagIlluminantData2,
          &profile.illuminant_data2,
          &profile.illuminant_data_kinds[1]) ||
      !parse_illuminant_data(
          kTagIlluminantData3,
          &profile.illuminant_data3,
          &profile.illuminant_data_kinds[2])) {
    return failure(
        DcpParseStatus::InvalidTagValue,
        "dcp_illuminant_data_invalid",
        selected_ifd_offset);
  }

  const auto parse_matrix_tag = [&](
                                    std::uint16_t tag,
                                    std::optional<std::array<double, 9>>* destination,
                                    bool require_nonsingular) -> bool {
    const auto* entry = find_entry(entries, tag);
    if (entry == nullptr) {
      return true;
    }
    if (entry->count != 9U) {
      return false;
    }
    const auto value = read_matrix(reader, *entry, require_nonsingular);
    if (!value.has_value()) {
      return false;
    }
    *destination = *value;
    return true;
  };
  const std::array matrix_tags = {
      std::pair{kTagColorMatrix1, &profile.color_matrix1},
      std::pair{kTagColorMatrix2, &profile.color_matrix2},
      std::pair{kTagColorMatrix3, &profile.color_matrix3},
      std::pair{kTagCameraCalibration1, &profile.camera_calibration1},
      std::pair{kTagCameraCalibration2, &profile.camera_calibration2},
      std::pair{kTagCameraCalibration3, &profile.camera_calibration3},
      std::pair{kTagReductionMatrix1, &profile.reduction_matrix1},
      std::pair{kTagReductionMatrix2, &profile.reduction_matrix2},
      std::pair{kTagReductionMatrix3, &profile.reduction_matrix3},
      std::pair{kTagForwardMatrix1, &profile.forward_matrix1},
      std::pair{kTagForwardMatrix2, &profile.forward_matrix2},
      std::pair{kTagForwardMatrix3, &profile.forward_matrix3},
  };
  for (const auto& [tag, destination] : matrix_tags) {
    const auto nonsingular = tag == kTagColorMatrix1 || tag == kTagColorMatrix2 ||
                             tag == kTagColorMatrix3 || tag == kTagForwardMatrix1 ||
                             tag == kTagForwardMatrix2 || tag == kTagForwardMatrix3;
    if (!parse_matrix_tag(tag, destination, nonsingular)) {
      const auto* entry = find_entry(entries, tag);
      return failure(
          entry != nullptr && entry->count != 9U
              ? DcpParseStatus::UnsupportedColorPlanes
              : DcpParseStatus::InvalidTagValue,
          entry != nullptr && entry->count != 9U
              ? "dcp_profile_requires_three_color_planes"
              : "dcp_matrix_invalid",
          selected_ifd_offset,
          tag);
    }
  }
  if (!profile.color_matrix1.has_value()) {
    return failure(
        DcpParseStatus::MissingColorMatrix,
        "dcp_color_matrix1_missing",
        selected_ifd_offset,
        kTagColorMatrix1);
  }
  for (const auto& [tag, matrix] : {
           std::pair{kTagForwardMatrix1, &profile.forward_matrix1},
           std::pair{kTagForwardMatrix2, &profile.forward_matrix2},
           std::pair{kTagForwardMatrix3, &profile.forward_matrix3},
       }) {
    if (matrix->has_value() &&
        !forward_matrix_maps_camera_white_to_d50(**matrix)) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_forward_matrix_does_not_map_camera_white_to_d50",
          selected_ifd_offset,
          tag);
    }
  }
  if (!profile.calibration_illuminant1.has_value() ||
      *profile.calibration_illuminant1 == 0U ||
      profile.color_matrix2.has_value() != profile.calibration_illuminant2.has_value() ||
      (profile.calibration_illuminant2.has_value() && *profile.calibration_illuminant2 == 0U) ||
      profile.color_matrix3.has_value() != profile.calibration_illuminant3.has_value() ||
      (profile.calibration_illuminant3.has_value() && *profile.calibration_illuminant3 == 0U) ||
      (profile.color_matrix3.has_value() && !profile.color_matrix2.has_value()) ||
      (*profile.calibration_illuminant1 == 255U &&
       !profile.illuminant_data1.has_value()) ||
      (profile.calibration_illuminant2 == std::optional<std::uint16_t>{255U} &&
       !profile.illuminant_data2.has_value()) ||
      (profile.calibration_illuminant3 == std::optional<std::uint16_t>{255U} &&
       !profile.illuminant_data3.has_value()) ||
      (profile.color_matrix3.has_value() &&
       ((profile.forward_matrix1.has_value() || profile.forward_matrix2.has_value() ||
         profile.forward_matrix3.has_value()) &&
        (!profile.forward_matrix1.has_value() || !profile.forward_matrix2.has_value() ||
         !profile.forward_matrix3.has_value())))) {
    return failure(
        DcpParseStatus::InvalidTagValue,
        "dcp_matrix_illuminant_pairing_invalid",
        selected_ifd_offset);
  }
  if (profile.triple_illuminant()) {
    const auto first_white = dcp_profile_illuminant_xy(profile, 0U);
    const auto second_white = dcp_profile_illuminant_xy(profile, 1U);
    const auto third_white = dcp_profile_illuminant_xy(profile, 2U);
    if (!first_white.has_value() || !second_white.has_value() ||
        !third_white.has_value() ||
        (first_white->x == second_white->x && first_white->y == second_white->y) ||
        (first_white->x == third_white->x && first_white->y == third_white->y) ||
        (second_white->x == third_white->x && second_white->y == third_white->y)) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_triple_illuminants_must_be_distinct",
          selected_ifd_offset);
    }
  }

  const auto parse_vector3 = [&](
                                 std::uint16_t tag,
                                 std::uint16_t required_type,
                                 std::optional<std::array<double, 3>>* destination) -> bool {
    const auto* entry = find_entry(entries, tag);
    if (entry == nullptr) {
      return true;
    }
    if (entry->count != 3U) {
      return false;
    }
    const auto values = read_numeric_values(reader, *entry, required_type);
    if (!values.has_value() || values->size() != 3U ||
        std::any_of(values->begin(), values->end(), [](double value) { return value <= 0.0; })) {
      return false;
    }
    *destination = std::array<double, 3>{(*values)[0], (*values)[1], (*values)[2]};
    return true;
  };
  if (!parse_vector3(kTagAnalogBalance, kTypeRational, &profile.analog_balance) ||
      !parse_vector3(kTagAsShotNeutral, kTypeRational, &profile.as_shot_neutral)) {
    return failure(
        DcpParseStatus::InvalidTagValue,
        "dcp_profile_vector_invalid",
        selected_ifd_offset);
  }
  if (const auto* baseline = find_entry(entries, kTagBaselineExposure); baseline != nullptr) {
    const auto value = read_numeric_values(reader, *baseline, kTypeSRational);
    if (!value.has_value() || value->size() != 1U || std::abs(value->front()) > 32.0) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_baseline_exposure_invalid",
          selected_ifd_offset,
          kTagBaselineExposure);
    }
    profile.baseline_exposure = value->front();
  }
  if (const auto* embed_policy = find_entry(entries, kTagProfileEmbedPolicy);
      embed_policy != nullptr) {
    const auto value = read_unsigned_values(reader, *embed_policy, kTypeLong);
    if (!value.has_value() || value->size() != 1U || value->front() > 3U) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_embed_policy_invalid",
          selected_ifd_offset,
          kTagProfileEmbedPolicy);
    }
    profile.embed_policy = value->front();
  }

  const auto* hue_dimensions = find_entry(entries, kTagProfileHueSatMapDims);
  const auto* hue_data1 = find_entry(entries, kTagProfileHueSatMapData1);
  const auto* hue_data2 = find_entry(entries, kTagProfileHueSatMapData2);
  const auto* hue_data3 = find_entry(entries, kTagProfileHueSatMapData3);
  const auto* hue_encoding = find_entry(entries, kTagProfileHueSatMapEncoding);
  if ((hue_dimensions == nullptr) != (hue_data1 == nullptr) ||
      (hue_data2 != nullptr && hue_dimensions == nullptr) ||
      (hue_data3 != nullptr && hue_dimensions == nullptr) ||
      (profile.triple_illuminant() &&
       ((hue_data1 != nullptr || hue_data2 != nullptr || hue_data3 != nullptr) &&
        (hue_data1 == nullptr || hue_data2 == nullptr || hue_data3 == nullptr)))) {
    return failure(
        DcpParseStatus::InvalidTagValue,
        "dcp_hue_sat_map_pairing_invalid",
        selected_ifd_offset);
  }
  if (hue_dimensions != nullptr && hue_data1 != nullptr) {
    const auto table = read_hsv_table(reader, *hue_dimensions, *hue_data1, hue_encoding);
    if (!table.has_value()) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_hue_sat_map1_invalid",
          selected_ifd_offset,
          kTagProfileHueSatMapData1);
    }
    profile.hue_sat_map1 = *table;
  }
  if (hue_dimensions != nullptr && hue_data2 != nullptr) {
    const auto table = read_hsv_table(reader, *hue_dimensions, *hue_data2, hue_encoding);
    if (!table.has_value()) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_hue_sat_map2_invalid",
          selected_ifd_offset,
          kTagProfileHueSatMapData2);
    }
    profile.hue_sat_map2 = *table;
  }
  if (hue_dimensions != nullptr && hue_data3 != nullptr) {
    const auto table = read_hsv_table(reader, *hue_dimensions, *hue_data3, hue_encoding);
    if (!table.has_value()) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_hue_sat_map3_invalid",
          selected_ifd_offset,
          kTagProfileHueSatMapData3);
    }
    profile.hue_sat_map3 = *table;
  }

  const auto* look_dimensions = find_entry(entries, kTagProfileLookTableDims);
  const auto* look_data = find_entry(entries, kTagProfileLookTableData);
  const auto* look_encoding = find_entry(entries, kTagProfileLookTableEncoding);
  if ((look_dimensions == nullptr) != (look_data == nullptr)) {
    return failure(
        DcpParseStatus::InvalidTagValue,
        "dcp_look_table_pairing_invalid",
        selected_ifd_offset);
  }
  if (look_dimensions != nullptr && look_data != nullptr) {
    const auto table = read_hsv_table(reader, *look_dimensions, *look_data, look_encoding);
    if (!table.has_value()) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_look_table_invalid",
          selected_ifd_offset,
          kTagProfileLookTableData);
    }
    profile.look_table = *table;
  }
  if (const auto* tone_curve = find_entry(entries, kTagProfileToneCurve);
      tone_curve != nullptr) {
    const auto value = read_tone_curve(reader, *tone_curve);
    if (!value.has_value()) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_tone_curve_invalid",
          selected_ifd_offset,
          kTagProfileToneCurve);
    }
    profile.tone_curve = *value;
  }
  if (const auto* gain_table = find_entry(entries, kTagProfileGainTableMap2);
      gain_table != nullptr) {
    const auto value = read_profile_gain_table_map2(reader, *gain_table);
    if (!value.has_value()) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_profile_gain_table_map2_invalid",
          selected_ifd_offset,
          kTagProfileGainTableMap2);
    }
    profile.profile_gain_table_map = *value;
  }
  if (const auto* rgb_tables = find_entry(entries, kTagRgbTables);
      rgb_tables != nullptr) {
    const auto value = read_rgb_tables(reader, *rgb_tables);
    if (!value.has_value()) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_rgb_tables_invalid",
          selected_ifd_offset,
          kTagRgbTables);
    }
    profile.rgb_tables = *value;
  }
  if (const auto* tone_method = find_entry(entries, kTagProfileToneMethod);
      tone_method != nullptr) {
    const auto value = read_unsigned_values(reader, *tone_method, kTypeLong);
    if (!value.has_value() || value->size() != 1U || value->front() > 2U) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_profile_tone_method_invalid",
          selected_ifd_offset,
          kTagProfileToneMethod);
    }
    profile.tone_method = static_cast<DcpProfileToneMethod>(value->front());
  }
  if (const auto* dynamic_range = find_entry(entries, kTagProfileDynamicRange);
      dynamic_range != nullptr) {
    const auto bytes = reader.entry_bytes(*dynamic_range);
    if (dynamic_range->type != kTypeUndefined || dynamic_range->count != 8U ||
        !bytes.has_value() || bytes->size() != 8U) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_profile_dynamic_range_invalid",
          selected_ifd_offset,
          kTagProfileDynamicRange);
    }
    const auto read_u16 = [&](std::size_t offset) {
      const auto first = std::to_integer<std::uint8_t>((*bytes)[offset]);
      const auto second = std::to_integer<std::uint8_t>((*bytes)[offset + 1U]);
      return reader.byte_order() == ByteOrder::Little
                 ? static_cast<std::uint16_t>(first | (second << 8U))
                 : static_cast<std::uint16_t>((first << 8U) | second);
    };
    auto hint_bits = std::uint32_t{0U};
    if (reader.byte_order() == ByteOrder::Little) {
      for (std::size_t index = 0U; index < 4U; ++index) {
        hint_bits |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>((*bytes)[4U + index]))
                     << static_cast<unsigned int>(index * 8U);
      }
    } else {
      for (std::size_t index = 0U; index < 4U; ++index) {
        hint_bits = (hint_bits << 8U) |
                    std::to_integer<std::uint8_t>((*bytes)[4U + index]);
      }
    }
    const auto version = read_u16(0U);
    const auto range = read_u16(2U);
    const auto hint = std::bit_cast<float>(hint_bits);
    if (version != 1U || range > 1U || !std::isfinite(hint) || hint < 0.0F ||
        (range == 0U && hint > 1.0F)) {
      return failure(
          DcpParseStatus::InvalidTagValue,
          "dcp_profile_dynamic_range_invalid",
          selected_ifd_offset,
          kTagProfileDynamicRange);
    }
    profile.dynamic_range = DcpProfileDynamicRange{
        .hdr = range == 1U,
        .hint_max_output_value = hint,
    };
  }

  return DcpParseResult{
      .status = DcpParseStatus::Ready,
      .profile = std::move(profile),
      .failure_reason = {},
      .failing_ifd_offset = 0U,
      .failing_tag = 0U,
  };
}

const char* dcp_parse_status_name(DcpParseStatus status) {
  switch (status) {
    case DcpParseStatus::Ready:
      return "ready";
    case DcpParseStatus::EmptyPayload:
      return "empty-payload";
    case DcpParseStatus::PayloadTooLarge:
      return "payload-too-large";
    case DcpParseStatus::InvalidByteOrder:
      return "invalid-byte-order";
    case DcpParseStatus::InvalidMagic:
      return "invalid-magic";
    case DcpParseStatus::UnsupportedBigTiff:
      return "unsupported-bigtiff";
    case DcpParseStatus::TruncatedHeader:
      return "truncated-header";
    case DcpParseStatus::TruncatedIfd:
      return "truncated-ifd";
    case DcpParseStatus::OffsetOutOfRange:
      return "offset-out-of-range";
    case DcpParseStatus::CountOverflow:
      return "count-overflow";
    case DcpParseStatus::TooManyIfds:
      return "too-many-ifds";
    case DcpParseStatus::TooManyIfdEntries:
      return "too-many-ifd-entries";
    case DcpParseStatus::IfdCycle:
      return "ifd-cycle";
    case DcpParseStatus::DuplicateTag:
      return "duplicate-tag";
    case DcpParseStatus::InvalidTagType:
      return "invalid-tag-type";
    case DcpParseStatus::InvalidTagCount:
      return "invalid-tag-count";
    case DcpParseStatus::InvalidTagValue:
      return "invalid-tag-value";
    case DcpParseStatus::MissingColorMatrix:
      return "missing-color-matrix";
    case DcpParseStatus::UnsupportedColorPlanes:
      return "unsupported-color-planes";
  }
  return "unknown";
}

const char* dcp_illuminant_data_kind_name(DcpIlluminantDataKind kind) {
  switch (kind) {
    case DcpIlluminantDataKind::None:
      return "none";
    case DcpIlluminantDataKind::WhiteXy:
      return "white-xy";
    case DcpIlluminantDataKind::Spectrum:
      return "spectrum";
  }
  return "unknown";
}

const char* dcp_profile_tone_method_name(DcpProfileToneMethod method) {
  switch (method) {
    case DcpProfileToneMethod::Unspecified:
      return "unspecified";
    case DcpProfileToneMethod::AdobePv5:
      return "adobe-pv5";
    case DcpProfileToneMethod::AdobePv6:
      return "adobe-pv6";
  }
  return "unknown";
}

DcpCameraCompatibility dcp_profile_camera_compatibility(
    const DcpProfile& profile,
    std::string_view camera_make,
    std::string_view camera_model) {
  const auto profile_identity = normalized_camera_identity(profile.unique_camera_model);
  const auto model_identity = normalized_camera_identity(camera_model);
  const auto make_identity = primary_make_token(camera_make);
  if (profile_identity.empty() || model_identity.empty()) {
    return DcpCameraCompatibility::Unknown;
  }
  if (profile_identity.find(model_identity) == std::string::npos ||
      (!make_identity.empty() &&
       profile_identity.find(make_identity) == std::string::npos)) {
    return DcpCameraCompatibility::Mismatch;
  }
  return DcpCameraCompatibility::Match;
}

const char* dcp_camera_compatibility_name(DcpCameraCompatibility compatibility) {
  switch (compatibility) {
    case DcpCameraCompatibility::Unknown:
      return "unknown";
    case DcpCameraCompatibility::Match:
      return "match";
    case DcpCameraCompatibility::Mismatch:
      return "mismatch";
  }
  return "unknown";
}

double dcp_reciprocal_temperature_weight(
    double source_temperature_kelvin,
    double illuminant1_temperature_kelvin,
    double illuminant2_temperature_kelvin) {
  if (!std::isfinite(source_temperature_kelvin) ||
      !std::isfinite(illuminant1_temperature_kelvin) ||
      !std::isfinite(illuminant2_temperature_kelvin) ||
      source_temperature_kelvin <= 0.0 ||
      illuminant1_temperature_kelvin <= 0.0 ||
      illuminant2_temperature_kelvin <= 0.0 ||
      std::abs(illuminant1_temperature_kelvin - illuminant2_temperature_kelvin) < 1e-9) {
    return 0.0;
  }
  const auto reciprocal_source = 1.0 / source_temperature_kelvin;
  const auto reciprocal_first = 1.0 / illuminant1_temperature_kelvin;
  const auto reciprocal_second = 1.0 / illuminant2_temperature_kelvin;
  return std::clamp(
      (reciprocal_source - reciprocal_first) /
          (reciprocal_second - reciprocal_first),
      0.0,
      1.0);
}

std::optional<double> dcp_calibration_illuminant_temperature(
    std::uint16_t exif_light_source) {
  switch (exif_light_source) {
    case 3U:   // Tungsten.
    case 17U:  // Standard light A.
      return 2850.0;
    case 24U:  // ISO studio tungsten.
      return 3200.0;
    case 23U:  // D50.
      return 5000.0;
    case 1U:   // Daylight.
    case 4U:   // Flash.
    case 9U:   // Fine weather.
    case 18U:  // Standard light B.
    case 20U:  // D55.
      return 5500.0;
    case 10U:  // Cloudy weather.
    case 19U:  // Standard light C.
    case 21U:  // D65.
      return 6500.0;
    case 11U:  // Shade.
    case 22U:  // D75.
      return 7500.0;
    case 12U:  // Daylight fluorescent, 5700-7100 K.
      return 6400.0;
    case 13U:  // Day white fluorescent, 4600-5500 K.
      return 5050.0;
    case 2U:   // Fluorescent.
    case 14U:  // Cool white fluorescent, 3800-4500 K.
      return 4150.0;
    case 15U:  // White fluorescent, 3250-3800 K.
      return 3525.0;
    case 16U:  // Warm white fluorescent, 2600-3250 K.
      return 2925.0;
    default:
      return std::nullopt;
  }
}

std::optional<double> dcp_profile_calibration_illuminant_temperature(
    const DcpProfile& profile,
    std::size_t illuminant_index) {
  const auto* illuminant = illuminant_index == 0U
                               ? &profile.calibration_illuminant1
                               : illuminant_index == 1U
                                     ? &profile.calibration_illuminant2
                                     : illuminant_index == 2U
                                           ? &profile.calibration_illuminant3
                                           : nullptr;
  const auto* data = illuminant_index == 0U
                         ? &profile.illuminant_data1
                         : illuminant_index == 1U
                               ? &profile.illuminant_data2
                               : illuminant_index == 2U
                                     ? &profile.illuminant_data3
                                     : nullptr;
  if (illuminant == nullptr || data == nullptr || !illuminant->has_value()) {
    return std::nullopt;
  }
  if (**illuminant != 255U) {
    return dcp_calibration_illuminant_temperature(**illuminant);
  }
  if (!data->has_value()) {
    return std::nullopt;
  }
  const auto temperature_tint = dcp_xy_to_temperature_tint(**data);
  return temperature_tint.has_value()
             ? std::optional<double>(temperature_tint->first)
             : std::nullopt;
}

std::optional<std::array<double, 9>> dcp_interpolated_forward_matrix(
    const DcpProfile& profile,
    double source_temperature_kelvin) {
  if (!profile.forward_matrix1.has_value()) {
    return std::nullopt;
  }
  const auto first_forward =
      canonical_dng_forward_matrix(*profile.forward_matrix1);
  if (!first_forward.has_value()) {
    return std::nullopt;
  }
  if (!profile.forward_matrix2.has_value()) {
    return first_forward->values;
  }
  const auto second_forward =
      canonical_dng_forward_matrix(*profile.forward_matrix2);
  if (!second_forward.has_value()) {
    return std::nullopt;
  }
  if (profile.triple_illuminant()) {
    if (!profile.forward_matrix3.has_value()) {
      return std::nullopt;
    }
    const auto third_forward =
        canonical_dng_forward_matrix(*profile.forward_matrix3);
    const auto weights = dcp_triple_illuminant_weights(
        profile,
        dcp_temperature_tint_to_xy(source_temperature_kelvin, 0.0));
    if (!third_forward.has_value() || !weights.has_value()) {
      return std::nullopt;
    }
    return weighted_matrix(
               *first_forward,
               *second_forward,
               *third_forward,
               *weights)
        .values;
  }
  if (!profile.calibration_illuminant1.has_value() ||
      !profile.calibration_illuminant2.has_value()) {
    return std::nullopt;
  }
  const auto first_temperature =
      dcp_profile_calibration_illuminant_temperature(profile, 0U);
  const auto second_temperature =
      dcp_profile_calibration_illuminant_temperature(profile, 1U);
  if (!first_temperature.has_value() || !second_temperature.has_value()) {
    return std::nullopt;
  }
  const auto second_weight = dcp_reciprocal_temperature_weight(
      source_temperature_kelvin,
      *first_temperature,
      *second_temperature);
  return interpolate_matrix(
             first_forward->values,
             second_forward->values,
             second_weight)
      .values;
}

DcpChromaticity dcp_temperature_tint_to_xy(
    double temperature_kelvin,
    double tint) {
  if (!std::isfinite(temperature_kelvin) || temperature_kelvin <= 0.0 ||
      !std::isfinite(tint)) {
    return {};
  }
  const auto reciprocal_megakelvin = 1.0e6 / temperature_kelvin;
  const auto tint_offset = tint / kDngTintScale;
  for (std::size_t index = 0U; index + 1U < kDngTemperatureTable.size(); ++index) {
    const auto& first = kDngTemperatureTable[index];
    const auto& second = kDngTemperatureTable[index + 1U];
    if (reciprocal_megakelvin >= second.reciprocal_megakelvin &&
        index + 2U < kDngTemperatureTable.size()) {
      continue;
    }
    const auto denominator =
        second.reciprocal_megakelvin - first.reciprocal_megakelvin;
    const auto first_weight = denominator > 0.0
                                  ? (second.reciprocal_megakelvin -
                                     reciprocal_megakelvin) /
                                        denominator
                                  : 1.0;
    const auto clamped_first_weight = std::clamp(first_weight, 0.0, 1.0);
    auto u = std::lerp(second.u, first.u, clamped_first_weight);
    auto v = std::lerp(second.v, first.v, clamped_first_weight);

    auto first_u_direction = 1.0;
    auto first_v_direction = first.slope;
    auto second_u_direction = 1.0;
    auto second_v_direction = second.slope;
    const auto first_length =
        std::hypot(first_u_direction, first_v_direction);
    const auto second_length =
        std::hypot(second_u_direction, second_v_direction);
    first_u_direction /= first_length;
    first_v_direction /= first_length;
    second_u_direction /= second_length;
    second_v_direction /= second_length;
    auto u_direction = std::lerp(
        second_u_direction,
        first_u_direction,
        clamped_first_weight);
    auto v_direction = std::lerp(
        second_v_direction,
        first_v_direction,
        clamped_first_weight);
    const auto direction_length = std::hypot(u_direction, v_direction);
    if (!std::isfinite(direction_length) || direction_length <= 1.0e-12) {
      return {};
    }
    u_direction /= direction_length;
    v_direction /= direction_length;
    u += u_direction * tint_offset;
    v += v_direction * tint_offset;
    const auto xy_denominator = u - 4.0 * v + 2.0;
    if (!std::isfinite(xy_denominator) ||
        std::abs(xy_denominator) <= 1.0e-12) {
      return {};
    }
    const auto result = DcpChromaticity{
        .x = 1.5 * u / xy_denominator,
        .y = v / xy_denominator,
    };
    return result.valid() ? result : DcpChromaticity{};
  }
  return {};
}

DcpCameraTransform dcp_camera_to_xyz_d50(
    const DcpProfile& profile,
    double source_temperature_kelvin,
    double source_tint) {
  auto failed = [](std::string reason) {
    return DcpCameraTransform{.failure_reason = std::move(reason)};
  };
  if (!profile.color_matrix1.has_value()) {
    return failed("dcp_color_matrix1_required");
  }
  if (!profile.calibration_illuminant1.has_value()) {
    return failed("dcp_calibration_illuminant1_required");
  }
  const auto first_temperature =
      dcp_profile_calibration_illuminant_temperature(profile, 0U);
  if (!first_temperature.has_value()) {
    return failed("dcp_calibration_illuminant1_unsupported");
  }

  const auto white = dcp_temperature_tint_to_xy(
      source_temperature_kelvin,
      source_tint);
  if (!white.valid()) {
    return failed("dcp_source_white_invalid");
  }
  const auto resolved_source = dcp_xy_to_temperature_tint(white);
  if (!resolved_source.has_value()) {
    return failed("dcp_source_white_temperature_invalid");
  }

  auto second_weight = 0.0;
  if (profile.color_matrix2.has_value()) {
    if (!profile.calibration_illuminant2.has_value()) {
      return failed("dcp_calibration_illuminant2_required");
    }
    const auto second_temperature =
        dcp_profile_calibration_illuminant_temperature(profile, 1U);
    if (!second_temperature.has_value()) {
      return failed("dcp_calibration_illuminant2_unsupported");
    }
    second_weight = dcp_reciprocal_temperature_weight(
        resolved_source->first,
        *first_temperature,
        *second_temperature);
  }
  auto illuminant_weights = std::array<double, 3>{
      1.0 - second_weight,
      second_weight,
      0.0,
  };
  if (profile.triple_illuminant()) {
    const auto triple_weights = dcp_triple_illuminant_weights(profile, white);
    if (!triple_weights.has_value()) {
      return failed("dcp_triple_illuminant_weights_invalid");
    }
    illuminant_weights = *triple_weights;
    second_weight = illuminant_weights[1];
  }

  const auto first_color_matrix =
      canonical_dng_color_matrix(dcp_matrix(*profile.color_matrix1));
  auto color_matrix = first_color_matrix;
  if (profile.triple_illuminant()) {
    color_matrix = weighted_matrix(
        first_color_matrix,
        canonical_dng_color_matrix(dcp_matrix(*profile.color_matrix2)),
        canonical_dng_color_matrix(dcp_matrix(*profile.color_matrix3)),
        illuminant_weights);
  } else if (profile.color_matrix2.has_value()) {
    color_matrix = interpolate_matrix(
        first_color_matrix.values,
        canonical_dng_color_matrix(dcp_matrix(*profile.color_matrix2)).values,
        second_weight);
  }
  const auto analog_values = canonical_dng_analog_balance(
      profile.analog_balance.value_or(
          std::array<double, 3>{1.0, 1.0, 1.0}));
  if (std::any_of(
          analog_values.cbegin(),
          analog_values.cend(),
          [](double value) {
            return !std::isfinite(value) || value <= 0.0;
          })) {
    return failed("dcp_analog_balance_invalid");
  }
  const auto analog_balance = diagonal_matrix(analog_values);

  const auto calibration_signatures_match =
      profile.camera_calibration_signature ==
      profile.profile_calibration_signature;
  auto camera_calibration = identity_matrix3();
  auto used_camera_calibration = false;
  if (calibration_signatures_match) {
    const auto calibration_or_identity = [](const auto& calibration) {
      return calibration.has_value()
                 ? rounded_dng_matrix(dcp_matrix(*calibration))
                 : identity_matrix3();
    };
    if (profile.triple_illuminant()) {
      camera_calibration = weighted_matrix(
          calibration_or_identity(profile.camera_calibration1),
          calibration_or_identity(profile.camera_calibration2),
          calibration_or_identity(profile.camera_calibration3),
          illuminant_weights);
      used_camera_calibration = profile.camera_calibration1.has_value() ||
                                profile.camera_calibration2.has_value() ||
                                profile.camera_calibration3.has_value();
    } else if (profile.dual_illuminant()) {
      camera_calibration = interpolate_matrix(
          calibration_or_identity(profile.camera_calibration1).values,
          calibration_or_identity(profile.camera_calibration2).values,
          second_weight);
      used_camera_calibration = profile.camera_calibration1.has_value() ||
                                profile.camera_calibration2.has_value();
    } else if (profile.camera_calibration1.has_value()) {
      camera_calibration =
          rounded_dng_matrix(dcp_matrix(*profile.camera_calibration1));
      used_camera_calibration = true;
    } else if (profile.camera_calibration2.has_value()) {
      camera_calibration =
          rounded_dng_matrix(dcp_matrix(*profile.camera_calibration2));
      used_camera_calibration = true;
    }
  }
  const auto individual_to_reference_source =
      multiply_matrix3(analog_balance, camera_calibration);
  const auto individual_to_reference =
      invert_dcp_matrix(individual_to_reference_source);
  if (!individual_to_reference.has_value()) {
    return failed("dcp_camera_calibration_singular");
  }
  const auto calibrated_color_matrix = multiply_matrix3(
      individual_to_reference_source,
      color_matrix);
  const auto camera_white_unscaled = multiply_matrix3_vector(
      calibrated_color_matrix,
      xyz_from_dcp_xy(white));
  const auto camera_white_max = *std::max_element(
      camera_white_unscaled.cbegin(),
      camera_white_unscaled.cend());
  if (!std::isfinite(camera_white_max) || camera_white_max <= 0.0) {
    return failed("dcp_camera_white_invalid");
  }
  std::array<double, 3> camera_white{};
  for (std::size_t channel = 0U; channel < camera_white.size(); ++channel) {
    camera_white[channel] = std::clamp(
        camera_white_unscaled[channel] / camera_white_max,
        0.001,
        1.0);
  }

  std::optional<ColorMatrix3> interpolated_forward;
  if (profile.triple_illuminant() &&
      profile.forward_matrix1.has_value() &&
      profile.forward_matrix2.has_value() &&
      profile.forward_matrix3.has_value()) {
    const auto first_forward =
        canonical_dng_forward_matrix(*profile.forward_matrix1);
    const auto second_forward =
        canonical_dng_forward_matrix(*profile.forward_matrix2);
    const auto third_forward =
        canonical_dng_forward_matrix(*profile.forward_matrix3);
    if (!first_forward.has_value() || !second_forward.has_value() ||
        !third_forward.has_value()) {
      return failed("dcp_forward_matrix_normalization_failed");
    }
    interpolated_forward = weighted_matrix(
        *first_forward,
        *second_forward,
        *third_forward,
        illuminant_weights);
  } else if (profile.forward_matrix1.has_value() &&
      profile.forward_matrix2.has_value()) {
    const auto first_forward =
        canonical_dng_forward_matrix(*profile.forward_matrix1);
    const auto second_forward =
        canonical_dng_forward_matrix(*profile.forward_matrix2);
    if (!first_forward.has_value() || !second_forward.has_value()) {
      return failed("dcp_forward_matrix_normalization_failed");
    }
    interpolated_forward = interpolate_matrix(
        first_forward->values,
        second_forward->values,
        second_weight);
  } else if (profile.forward_matrix1.has_value()) {
    interpolated_forward =
        canonical_dng_forward_matrix(*profile.forward_matrix1);
  } else if (profile.forward_matrix2.has_value()) {
    interpolated_forward =
        canonical_dng_forward_matrix(*profile.forward_matrix2);
  }

  ColorMatrix3 camera_to_pcs;
  auto used_forward_matrix = false;
  if (interpolated_forward.has_value()) {
    const auto reference_camera_white = multiply_matrix3_vector(
        *individual_to_reference,
        camera_white);
    if (std::any_of(
            reference_camera_white.cbegin(),
            reference_camera_white.cend(),
            [](double value) {
              return !std::isfinite(value) || value <= 1.0e-12;
            })) {
      return failed("dcp_reference_camera_white_invalid");
    }
    const std::array<double, 3> inverse_reference_white = {
        1.0 / reference_camera_white[0],
        1.0 / reference_camera_white[1],
        1.0 / reference_camera_white[2],
    };
    camera_to_pcs = multiply_matrix3(
        *interpolated_forward,
        multiply_matrix3(
            diagonal_matrix(inverse_reference_white),
            *individual_to_reference));
    used_forward_matrix = true;
  } else {
    auto pcs_to_camera = multiply_matrix3(
        calibrated_color_matrix,
        dng_map_white_matrix(kD50Chromaticity, white));
    const auto mapped_pcs_white = multiply_matrix3_vector(
        pcs_to_camera,
        xyz_from_dcp_xy(kD50Chromaticity));
    const auto scale = *std::max_element(
        mapped_pcs_white.cbegin(),
        mapped_pcs_white.cend());
    if (!std::isfinite(scale) || scale <= 0.0) {
      return failed("dcp_pcs_to_camera_scale_invalid");
    }
    pcs_to_camera = scale_matrix(pcs_to_camera, 1.0 / scale);
    const auto inverse = invert_dcp_matrix(pcs_to_camera);
    if (!inverse.has_value()) {
      return failed("dcp_color_matrix_transform_singular");
    }
    camera_to_pcs = *inverse;
  }
  if (!finite_matrix(camera_to_pcs)) {
    return failed("dcp_camera_transform_non_finite");
  }
  return DcpCameraTransform{
      .valid = true,
      .camera_to_xyz_d50 = camera_to_pcs.values,
      .white = white,
      .second_illuminant_weight = second_weight,
      .illuminant_weights = illuminant_weights,
      .used_forward_matrix = used_forward_matrix,
      .used_camera_calibration = used_camera_calibration,
      .failure_reason = {},
  };
}

DcpProfileRenderPlan dcp_prepare_profile_render_plan(
    const DcpProfile& profile,
    double source_temperature_kelvin,
    double source_tint) {
  auto second_weight = 0.0;
  auto illuminant_weights = std::array<double, 3>{1.0, 0.0, 0.0};
  if (profile.hue_sat_map1.configured() &&
      profile.hue_sat_map2.configured() &&
      profile.calibration_illuminant1.has_value() &&
      profile.calibration_illuminant2.has_value()) {
    const auto first_temperature =
        dcp_profile_calibration_illuminant_temperature(profile, 0U);
    const auto second_temperature =
        dcp_profile_calibration_illuminant_temperature(profile, 1U);
    if (first_temperature.has_value() && second_temperature.has_value()) {
      second_weight = dcp_reciprocal_temperature_weight(
          source_temperature_kelvin,
          *first_temperature,
          *second_temperature);
      illuminant_weights = {1.0 - second_weight, second_weight, 0.0};
    }
  }
  if (profile.triple_illuminant() &&
      valid_hsv_table(profile.hue_sat_map3)) {
    const auto triple_weights = dcp_triple_illuminant_weights(
        profile,
        dcp_temperature_tint_to_xy(source_temperature_kelvin, source_tint));
    if (triple_weights.has_value()) {
      illuminant_weights = *triple_weights;
      second_weight = illuminant_weights[1];
    }
  }
  auto rgb_table_transforms =
      std::vector<DcpProfileRenderPlan::RgbTableTransform>{};
  if (profile.rgb_tables.configured()) {
    const auto profile_space = make_linear_prophoto_rgb_color_space();
    rgb_table_transforms.reserve(profile.rgb_tables.tables.size());
    for (const auto& table : profile.rgb_tables.tables) {
      const auto table_space = dcp_rgb_table_color_space(table.primaries);
      const auto forward = compile_builtin_color_transform(
          profile_space,
          table_space);
      const auto inverse = compile_builtin_color_transform(
          table_space,
          profile_space);
      rgb_table_transforms.push_back({
          .profile_to_table = forward.matrix.values,
          .table_to_profile = inverse.matrix.values,
      });
    }
  }
  return DcpProfileRenderPlan{
      .second_illuminant_weight = second_weight,
      .illuminant_weights = illuminant_weights,
      .tone_curve_slopes = solve_dng_spline_slopes(profile.tone_curve),
      .rgb_table_transforms = std::move(rgb_table_transforms),
      .uses_hue_sat_map =
          valid_hsv_table(profile.hue_sat_map1) &&
          (!profile.triple_illuminant() ||
           (valid_hsv_table(profile.hue_sat_map2) &&
            valid_hsv_table(profile.hue_sat_map3) &&
            profile.hue_sat_map1.dimensions == profile.hue_sat_map2.dimensions &&
            profile.hue_sat_map1.dimensions == profile.hue_sat_map3.dimensions)),
      .uses_look_table = valid_hsv_table(profile.look_table),
      .uses_tone_curve = profile.tone_curve.size() >= 2U,
      .uses_rgb_tables = profile.rgb_tables.configured(),
  };
}

std::shared_ptr<const DcpProfileRenderPlan>
compile_dcp_profile_render_plan(
    const DcpProfile& profile,
    std::string_view immutable_profile_identity,
    double source_temperature_kelvin,
    double source_tint) {
  if (immutable_profile_identity.empty()) {
    return std::make_shared<const DcpProfileRenderPlan>(
        dcp_prepare_profile_render_plan(
            profile,
            source_temperature_kelvin,
            source_tint));
  }
  const auto key = dcp_render_plan_cache_key(
      immutable_profile_identity,
      source_temperature_kelvin,
      source_tint);
  auto& cache = dcp_render_plan_cache();
  const auto lock = std::scoped_lock(cache.mutex);
  const auto existing = cache.entries.find(key);
  if (existing != cache.entries.end()) {
    ++cache.hits;
    cache.age.erase(existing->second.age);
    cache.age.push_front(key);
    existing->second.age = cache.age.begin();
    return existing->second.plan;
  }
  ++cache.misses;
  ++cache.builds;
  auto plan =
      std::make_shared<const DcpProfileRenderPlan>(
          dcp_prepare_profile_render_plan(
              profile,
              source_temperature_kelvin,
              source_tint));
  cache.age.push_front(key);
  cache.entries.emplace(
      key,
      DcpRenderPlanCacheEntry{
          .plan = plan,
          .age = cache.age.begin(),
      });
  while (cache.entries.size() >
         kDcpRenderPlanCacheCapacity) {
    const auto oldest = cache.age.back();
    cache.age.pop_back();
    cache.entries.erase(oldest);
    ++cache.evictions;
  }
  return plan;
}

DcpProfileRenderPlanCacheStats
dcp_profile_render_plan_cache_stats() {
  auto& cache = dcp_render_plan_cache();
  const auto lock = std::scoped_lock(cache.mutex);
  return DcpProfileRenderPlanCacheStats{
      .entries = cache.entries.size(),
      .hits = cache.hits,
      .misses = cache.misses,
      .builds = cache.builds,
      .evictions = cache.evictions,
      .capacity = kDcpRenderPlanCacheCapacity,
  };
}

void clear_dcp_profile_render_plan_cache_for_testing() {
  auto& cache = dcp_render_plan_cache();
  const auto lock = std::scoped_lock(cache.mutex);
  cache.entries.clear();
  cache.age.clear();
  cache.hits = 0U;
  cache.misses = 0U;
  cache.builds = 0U;
  cache.evictions = 0U;
}

DcpRgb dcp_apply_hue_sat_map(
    const DcpProfile& profile,
    const DcpProfileRenderPlan& plan,
    DcpRgb rgb) {
  if (!plan.uses_hue_sat_map || !valid_hsv_table(profile.hue_sat_map1)) {
    return rgb;
  }
  const auto proxy = DcpRgb{
      .red = std::clamp(rgb.red, 0.0, 1.0),
      .green = std::clamp(rgb.green, 0.0, 1.0),
      .blue = std::clamp(rgb.blue, 0.0, 1.0),
  };
  const auto hsv = rgb_to_dcp_hsv(proxy);
  auto delta = sample_hsv_table(profile.hue_sat_map1, hsv);
  if (profile.triple_illuminant() && valid_hsv_table(profile.hue_sat_map2) &&
      valid_hsv_table(profile.hue_sat_map3) &&
      profile.hue_sat_map1.dimensions == profile.hue_sat_map2.dimensions &&
      profile.hue_sat_map1.dimensions == profile.hue_sat_map3.dimensions) {
    const auto second = sample_hsv_table(profile.hue_sat_map2, hsv);
    const auto third = sample_hsv_table(profile.hue_sat_map3, hsv);
    const auto weighted = [&](double first_value, double second_value, double third_value) {
      return first_value * plan.illuminant_weights[0] +
             second_value * plan.illuminant_weights[1] +
             third_value * plan.illuminant_weights[2];
    };
    delta.hue_shift_degrees = weighted(
        delta.hue_shift_degrees,
        second.hue_shift_degrees,
        third.hue_shift_degrees);
    delta.saturation_scale = weighted(
        delta.saturation_scale,
        second.saturation_scale,
        third.saturation_scale);
    delta.value_scale = weighted(
        delta.value_scale,
        second.value_scale,
        third.value_scale);
  } else if (valid_hsv_table(profile.hue_sat_map2) &&
      profile.hue_sat_map1.dimensions == profile.hue_sat_map2.dimensions) {
    const auto second = sample_hsv_table(profile.hue_sat_map2, hsv);
    delta.hue_shift_degrees = std::lerp(
        delta.hue_shift_degrees,
        second.hue_shift_degrees,
        plan.second_illuminant_weight);
    delta.saturation_scale = std::lerp(
        delta.saturation_scale,
        second.saturation_scale,
        plan.second_illuminant_weight);
    delta.value_scale = std::lerp(
        delta.value_scale,
        second.value_scale,
        plan.second_illuminant_weight);
  }
  return apply_hsv_delta(profile.hue_sat_map1, delta, rgb);
}

DcpRgb dcp_apply_hue_sat_map(
    const DcpProfile& profile,
    double source_temperature_kelvin,
    DcpRgb rgb) {
  return dcp_apply_hue_sat_map(
      profile,
      source_temperature_kelvin,
      0.0,
      rgb);
}

DcpRgb dcp_apply_hue_sat_map(
    const DcpProfile& profile,
    double source_temperature_kelvin,
    double source_tint,
    DcpRgb rgb) {
  return dcp_apply_hue_sat_map(
      profile,
      dcp_prepare_profile_render_plan(
          profile,
          source_temperature_kelvin,
          source_tint),
      rgb);
}

DcpRgb dcp_apply_look_table(
    const DcpProfile& profile,
    DcpRgb rgb) {
  if (!valid_hsv_table(profile.look_table)) {
    return rgb;
  }
  const auto proxy = DcpRgb{
      .red = std::clamp(rgb.red, 0.0, 1.0),
      .green = std::clamp(rgb.green, 0.0, 1.0),
      .blue = std::clamp(rgb.blue, 0.0, 1.0),
  };
  return apply_hsv_delta(
      profile.look_table,
      sample_hsv_table(profile.look_table, rgb_to_dcp_hsv(proxy)),
      rgb);
}

DcpRgb dcp_apply_profile_tone_curve(
    const DcpProfile& profile,
    const DcpProfileRenderPlan& plan,
    DcpRgb rgb) {
  if (!plan.uses_tone_curve ||
      plan.tone_curve_slopes.size() != profile.tone_curve.size()) {
    return rgb;
  }
  return apply_rgb_tone_curve(
      profile.tone_curve,
      plan.tone_curve_slopes,
      rgb);
}

DcpRgb dcp_apply_profile_tone_curve(
    const DcpProfile& profile,
    DcpRgb rgb) {
  return dcp_apply_profile_tone_curve(
      profile,
      dcp_prepare_profile_render_plan(profile, 5500.0),
      rgb);
}

DcpRgb dcp_apply_profile_gain_table_map(
    const DcpProfile& profile,
    double normalized_x,
    double normalized_y,
    double exposure_weight_gain,
    DcpRgb rgb) {
  const auto& map = profile.profile_gain_table_map;
  if (!map.configured() || !std::isfinite(normalized_x) ||
      !std::isfinite(normalized_y) || !std::isfinite(exposure_weight_gain)) {
    return rgb;
  }
  const auto x_map = std::clamp(
      (static_cast<float>(normalized_x) - static_cast<float>(map.origin_h)) /
          static_cast<float>(map.spacing_h),
      0.0F,
      static_cast<float>(map.points_h - 1U));
  const auto y_map = std::clamp(
      (static_cast<float>(normalized_y) - static_cast<float>(map.origin_v)) /
          static_cast<float>(map.spacing_v),
      0.0F,
      static_cast<float>(map.points_v - 1U));
  const auto x0 = static_cast<std::uint32_t>(x_map);
  const auto y0 = static_cast<std::uint32_t>(y_map);
  const auto x1 = std::min(x0 + 1U, map.points_h - 1U);
  const auto y1 = std::min(y0 + 1U, map.points_v - 1U);
  const auto xf = x_map - static_cast<float>(x0);
  const auto yf = y_map - static_cast<float>(y0);
  const auto red = static_cast<float>(rgb.red);
  const auto green = static_cast<float>(rgb.green);
  const auto blue = static_cast<float>(rgb.blue);
  const auto minimum = std::min({red, green, blue});
  const auto maximum = std::max({red, green, blue});
  auto weight = map.input_weights[0] * red +
                map.input_weights[1] * green +
                map.input_weights[2] * blue +
                map.input_weights[3] * minimum +
                map.input_weights[4] * maximum;
  weight *= static_cast<float>(exposure_weight_gain);
  weight = std::isfinite(weight) ? std::clamp(weight, 0.0F, 1.0F) : 0.0F;
  if (map.gamma != 1.0F) {
    weight = std::pow(weight, map.gamma);
  }
  const auto weight_scaled = weight * static_cast<float>(map.table_points);
  const auto w0 = std::min(
      static_cast<std::uint32_t>(weight_scaled),
      map.table_points - 1U);
  const auto w1 = std::min(w0 + 1U, map.table_points - 1U);
  const auto wf = weight_scaled - static_cast<float>(w0);
  const auto gain_at = [&](std::uint32_t y, std::uint32_t x, std::uint32_t w) {
    const auto index =
        (static_cast<std::size_t>(y) * map.points_h + x) * map.table_points + w;
    return map.gains[index];
  };
  const auto gain00 = std::lerp(gain_at(y0, x0, w0), gain_at(y0, x0, w1), wf);
  const auto gain01 = std::lerp(gain_at(y0, x1, w0), gain_at(y0, x1, w1), wf);
  const auto gain10 = std::lerp(gain_at(y1, x0, w0), gain_at(y1, x0, w1), wf);
  const auto gain11 = std::lerp(gain_at(y1, x1, w0), gain_at(y1, x1, w1), wf);
  const auto gain0 = std::lerp(gain00, gain01, xf);
  const auto gain1 = std::lerp(gain10, gain11, xf);
  const auto gain = std::lerp(gain0, gain1, yf);
  return {
      static_cast<double>(red * gain),
      static_cast<double>(green * gain),
      static_cast<double>(blue * gain),
  };
}

DcpRgb dcp_apply_rgb_tables(
    const DcpProfile& profile,
    const DcpProfileRenderPlan& plan,
    DcpRgb rgb,
    std::span<const DcpSemanticMaskWeight> semantic_mask_weights) {
  if (!plan.uses_rgb_tables || !profile.rgb_tables.configured() ||
      plan.rgb_table_transforms.size() != profile.rgb_tables.tables.size() ||
      !std::isfinite(rgb.red) || !std::isfinite(rgb.green) ||
      !std::isfinite(rgb.blue)) {
    return rgb;
  }
  const auto mask_weight = [&](std::string_view semantic_name) {
    auto result = 0.0;
    for (const auto& candidate : semantic_mask_weights) {
      if (candidate.semantic_name == semantic_name &&
          std::isfinite(candidate.weight)) {
        result += std::clamp(candidate.weight, 0.0, 1.0);
      }
    }
    return std::clamp(result, 0.0, 1.0);
  };
  auto table_weights = std::vector<double>{};
  table_weights.reserve(profile.rgb_tables.tables.size());
  auto foreground_weight_sum = 0.0;
  auto has_background_table = false;
  for (const auto& table : profile.rgb_tables.tables) {
    if (table.background()) {
      table_weights.push_back(0.0);
      has_background_table = true;
      continue;
    }
    const auto weight = mask_weight(table.semantic_name);
    table_weights.push_back(weight);
    foreground_weight_sum += weight;
  }
  const auto background_weight =
      1.0 - std::min(1.0, foreground_weight_sum);
  for (std::size_t index = 0U;
       index < profile.rgb_tables.tables.size();
       ++index) {
    if (profile.rgb_tables.tables[index].background()) {
      table_weights[index] = background_weight;
    }
  }
  const auto hdr = profile.dynamic_range.has_value() &&
                   profile.dynamic_range->hdr;
  if (profile.rgb_tables.composite_method ==
      DcpRgbTableCompositeMethod::Sequential) {
    auto result = rgb;
    for (std::size_t index = 0U;
         index < profile.rgb_tables.tables.size();
         ++index) {
      const auto weight = table_weights[index];
      if (weight <= 0.0) {
        continue;
      }
      const auto transformed = apply_dcp_rgb_table(
          profile.rgb_tables.tables[index],
          plan.rgb_table_transforms[index],
          hdr,
          result);
      result.red = std::lerp(result.red, transformed.red, weight);
      result.green = std::lerp(result.green, transformed.green, weight);
      result.blue = std::lerp(result.blue, transformed.blue, weight);
    }
    return result;
  }

  auto result = DcpRgb{};
  auto denominator = 0.0;
  for (std::size_t index = 0U;
       index < profile.rgb_tables.tables.size();
       ++index) {
    const auto weight = table_weights[index];
    if (weight <= 0.0) {
      continue;
    }
    const auto transformed = apply_dcp_rgb_table(
        profile.rgb_tables.tables[index],
        plan.rgb_table_transforms[index],
        hdr,
        rgb);
    result.red += weight * transformed.red;
    result.green += weight * transformed.green;
    result.blue += weight * transformed.blue;
    denominator += weight;
  }
  if (!has_background_table && background_weight > 0.0) {
    result.red += background_weight * rgb.red;
    result.green += background_weight * rgb.green;
    result.blue += background_weight * rgb.blue;
    denominator += background_weight;
  }
  if (!std::isfinite(denominator) || denominator <= 0.0) {
    return rgb;
  }
  return {
      result.red / denominator,
      result.green / denominator,
      result.blue / denominator,
  };
}

DcpRgb dcp_apply_rgb_tables(
    const DcpProfile& profile,
    DcpRgb rgb,
    std::span<const DcpSemanticMaskWeight> semantic_mask_weights) {
  return dcp_apply_rgb_tables(
      profile,
      dcp_prepare_profile_render_plan(profile, 5500.0),
      rgb,
      semantic_mask_weights);
}

}  // namespace photo_editor::pipeline

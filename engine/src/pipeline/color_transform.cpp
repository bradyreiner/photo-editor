#include "photo_editor/pipeline/color_transform.h"

#include "photo_editor/pipeline/color_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <list>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <tuple>
#include <utility>

#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
#include <lcms2.h>
#endif

namespace photo_editor::pipeline {

struct CompiledIccTransform::Impl final {
  IccTransformDescription description;
  mutable std::mutex apply_mutex;
#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
  cmsContext context = nullptr;
  cmsHTRANSFORM transform = nullptr;
  cmsHTRANSFORM output_to_lab_transform = nullptr;

  ~Impl() {
    if (output_to_lab_transform != nullptr) {
      cmsDeleteTransform(output_to_lab_transform);
    }
    if (transform != nullptr) {
      cmsDeleteTransform(transform);
    }
    if (context != nullptr) {
      cmsDeleteContext(context);
    }
  }
#endif
};

namespace {

constexpr std::size_t kIccTransformCacheCapacity = 16;
constexpr std::size_t kIccTransformLutCacheCapacity = 2U;
constexpr std::size_t kIccTransformLutCacheMaximumBytes =
    32U * 1024U * 1024U;

#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
// A transform-local alarm avoids mutating Little CMS process-global state. The
// deliberately asymmetric code is detected before the proof result is packed.
constexpr std::array<cmsUInt16Number, 3> kGamutAlarmCodes = {
    0xf123U,
    0x0e45U,
    0xd678U,
};

struct BuiltinIccProfileCache final {
  std::mutex mutex;
  std::map<std::tuple<int, int, int>, std::vector<std::byte>> payloads;
};

BuiltinIccProfileCache& builtin_icc_profile_cache() {
  static BuiltinIccProfileCache cache;
  return cache;
}

[[nodiscard]] cmsToneCurve* build_transfer_curve(TransferFunction transfer_function) {
  switch (transfer_function) {
    case TransferFunction::Linear:
      return cmsBuildGamma(nullptr, 1.0);
    case TransferFunction::Acescct:
      // ACEScct is a transient grading encoding, not an ICC transfer curve.
      return nullptr;
    case TransferFunction::Gamma18:
      return cmsBuildGamma(nullptr, 1.8);
    case TransferFunction::Gamma22:
      return cmsBuildGamma(nullptr, 2.2);
    case TransferFunction::Gamma24:
      return cmsBuildGamma(nullptr, 2.4);
    case TransferFunction::Srgb: {
      constexpr cmsFloat64Number parameters[] = {
          2.4,
          1.0 / 1.055,
          0.055 / 1.055,
          0.0,
          0.04045,
          1.0 / 12.92,
          0.0,
      };
      return cmsBuildParametricToneCurve(nullptr, 4, parameters);
    }
    case TransferFunction::Bt2020: {
      constexpr auto alpha = 1.09929682680944;
      constexpr auto beta = 0.018053968510807;
      constexpr cmsFloat64Number parameters[] = {
          1.0 / 0.45,
          1.0 / alpha,
          (alpha - 1.0) / alpha,
          0.0,
          4.5 * beta,
          1.0 / 4.5,
          0.0,
      };
      return cmsBuildParametricToneCurve(nullptr, 4, parameters);
    }
    case TransferFunction::Unknown:
      return nullptr;
  }
  return nullptr;
}

void canonicalize_generated_icc_header(std::vector<std::byte>* payload) {
  if (payload == nullptr || payload->size() < 128U) {
    return;
  }
  // ICC headers carry a creation timestamp and optional profile ID. Pin both
  // so a built-in source profile has the same payload identity across runs.
  constexpr std::array<unsigned char, 12> timestamp = {
      0x07U, 0xd0U,  // 2000
      0x00U, 0x01U,  // January
      0x00U, 0x01U,  // first day
      0x00U, 0x00U,  // 00 hours
      0x00U, 0x00U,  // 00 minutes
      0x00U, 0x00U,  // 00 seconds
  };
  for (std::size_t index = 0; index < timestamp.size(); ++index) {
    (*payload)[24U + index] = static_cast<std::byte>(timestamp[index]);
  }
  std::fill(
      payload->begin() + 84,
      payload->begin() + 100,
      std::byte{0});
}
#endif

[[nodiscard]] constexpr std::uint32_t rotate_right(std::uint32_t value, int bits) {
  return (value >> bits) | (value << (32 - bits));
}

void sha256_process_block(
    const std::byte* block,
    std::array<std::uint32_t, 8>* state) {
  constexpr std::array<std::uint32_t, 64> constants = {
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
      0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
      0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
      0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
      0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
      0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
      0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
      0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
      0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
      0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
      0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
      0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
      0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
      0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
      0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
  };
  std::array<std::uint32_t, 64> words{};
  for (std::size_t index = 0; index < 16; ++index) {
    const auto offset = index * 4;
    words[index] =
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(block[offset])) << 24U) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(block[offset + 1])) << 16U) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(block[offset + 2])) << 8U) |
        static_cast<std::uint32_t>(std::to_integer<unsigned char>(block[offset + 3]));
  }
  for (std::size_t index = 16; index < words.size(); ++index) {
    const auto s0 = rotate_right(words[index - 15], 7) ^
                    rotate_right(words[index - 15], 18) ^
                    (words[index - 15] >> 3U);
    const auto s1 = rotate_right(words[index - 2], 17) ^
                    rotate_right(words[index - 2], 19) ^
                    (words[index - 2] >> 10U);
    words[index] = words[index - 16] + s0 + words[index - 7] + s1;
  }

  auto a = (*state)[0];
  auto b = (*state)[1];
  auto c = (*state)[2];
  auto d = (*state)[3];
  auto e = (*state)[4];
  auto f = (*state)[5];
  auto g = (*state)[6];
  auto h = (*state)[7];
  for (std::size_t index = 0; index < words.size(); ++index) {
    const auto sigma1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
    const auto choose = (e & f) ^ ((~e) & g);
    const auto temp1 = h + sigma1 + choose + constants[index] + words[index];
    const auto sigma0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    const auto temp2 = sigma0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  (*state)[0] += a;
  (*state)[1] += b;
  (*state)[2] += c;
  (*state)[3] += d;
  (*state)[4] += e;
  (*state)[5] += f;
  (*state)[6] += g;
  (*state)[7] += h;
}

class Sha256Accumulator final {
 public:
  [[nodiscard]] bool update(std::span<const std::byte> payload) {
    constexpr auto kMaximumBytes =
        std::numeric_limits<std::uint64_t>::max() / 8U;
    if (payload.size() > kMaximumBytes - total_bytes_) {
      return false;
    }
    total_bytes_ += static_cast<std::uint64_t>(payload.size());

    std::size_t offset = 0U;
    if (tail_size_ > 0U) {
      const auto count = std::min(64U - tail_size_, payload.size());
      std::copy_n(payload.data(), count, tail_.data() + tail_size_);
      tail_size_ += count;
      offset += count;
      if (tail_size_ == 64U) {
        sha256_process_block(tail_.data(), &state_);
        tail_size_ = 0U;
      }
    }
    while (payload.size() - offset >= 64U) {
      sha256_process_block(payload.data() + offset, &state_);
      offset += 64U;
    }
    const auto remainder = payload.size() - offset;
    if (remainder > 0U) {
      std::copy_n(payload.data() + offset, remainder, tail_.data());
      tail_size_ = remainder;
    }
    return true;
  }

  [[nodiscard]] std::string digest() const {
    auto final_state = state_;
    std::array<std::byte, 128> final_blocks{};
    if (tail_size_ > 0U) {
      std::copy_n(tail_.data(), tail_size_, final_blocks.data());
    }
    final_blocks[tail_size_] = std::byte{0x80};
    const auto final_size = tail_size_ < 56U ? std::size_t{64U}
                                             : std::size_t{128U};
    const auto bit_length = total_bytes_ * 8U;
    for (std::size_t index = 0U; index < 8U; ++index) {
      final_blocks[final_size - 1U - index] = static_cast<std::byte>(
          (bit_length >> static_cast<unsigned int>(index * 8U)) & 0xffU);
    }
    sha256_process_block(final_blocks.data(), &final_state);
    if (final_size == 128U) {
      sha256_process_block(final_blocks.data() + 64U, &final_state);
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string value(64U, '0');
    for (std::size_t word = 0U; word < final_state.size(); ++word) {
      for (std::size_t nibble = 0U; nibble < 8U; ++nibble) {
        const auto shift = static_cast<unsigned int>((7U - nibble) * 4U);
        value[word * 8U + nibble] =
            hex[(final_state[word] >> shift) & 0x0fU];
      }
    }
    return value;
  }

 private:
  std::array<std::uint32_t, 8> state_ = {
      0x6a09e667U,
      0xbb67ae85U,
      0x3c6ef372U,
      0xa54ff53aU,
      0x510e527fU,
      0x9b05688cU,
      0x1f83d9abU,
      0x5be0cd19U,
  };
  std::array<std::byte, 64> tail_{};
  std::size_t tail_size_ = 0U;
  std::uint64_t total_bytes_ = 0U;
};

struct IccTransformCacheEntry final {
  std::string key;
  std::shared_ptr<CompiledIccTransform::Impl> transform;
};

struct IccTransformCache final {
  std::mutex mutex;
  std::list<IccTransformCacheEntry> entries;
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t evictions = 0;
};

IccTransformCache& icc_transform_cache() {
  static IccTransformCache cache;
  return cache;
}

struct IccTransformLutCacheEntry final {
  std::string key;
  std::shared_ptr<const IccTransformLut3D> lut;
  std::uint64_t last_use = 0U;
};

struct IccTransformLutCache final {
  std::mutex mutex;
  std::mutex build_mutex;
  std::vector<IccTransformLutCacheEntry> entries;
  IccTransformLutCacheStats stats;
  std::uint64_t use_counter = 0U;
};

IccTransformLutCache& icc_transform_lut_cache() {
  static IccTransformLutCache cache;
  return cache;
}

[[nodiscard]] bool icc_transform_lut_settings_valid(
    const IccTransformLutSettings& settings) {
  return settings.cube_size >= 2 && settings.cube_size <= 129 &&
         std::isfinite(settings.input_shaper_exponent) &&
         settings.input_shaper_exponent > 0.0 &&
         settings.input_shaper_exponent <= 1.0 &&
         std::isfinite(settings.p99_delta_e_00_limit) &&
         settings.p99_delta_e_00_limit >= 0.0 &&
         std::isfinite(settings.maximum_delta_e_00_limit) &&
         settings.maximum_delta_e_00_limit >=
             settings.p99_delta_e_00_limit &&
         std::isfinite(settings.maximum_unsafe_cell_fraction) &&
         settings.maximum_unsafe_cell_fraction >= 0.0 &&
         settings.maximum_unsafe_cell_fraction <= 1.0;
}

[[nodiscard]] std::size_t icc_transform_lut_index(
    int size,
    int red,
    int green,
    int blue) {
  const auto safe_size = static_cast<std::size_t>(size);
  return (static_cast<std::size_t>(red) * safe_size +
          static_cast<std::size_t>(green)) *
             safe_size +
         static_cast<std::size_t>(blue);
}

[[nodiscard]] IccRgbaFloat sample_icc_transform_lut(
    const IccTransformLut3D& lut,
    const IccRgbaFloat& input) {
  const auto size = lut.settings.cube_size;
  const auto scale = static_cast<double>(size - 1);
  const std::array<double, 3> encoded = {
      std::pow(std::clamp(static_cast<double>(input.red), 0.0, 1.0),
               lut.settings.input_shaper_exponent),
      std::pow(std::clamp(static_cast<double>(input.green), 0.0, 1.0),
               lut.settings.input_shaper_exponent),
      std::pow(std::clamp(static_cast<double>(input.blue), 0.0, 1.0),
               lut.settings.input_shaper_exponent),
  };
  std::array<int, 3> lower{};
  std::array<double, 3> fraction{};
  for (std::size_t channel = 0U; channel < encoded.size(); ++channel) {
    const auto position = encoded[channel] * scale;
    lower[channel] = std::min(static_cast<int>(position), size - 2);
    fraction[channel] = position - static_cast<double>(lower[channel]);
  }
  const auto sample = [&](int red, int green, int blue) {
    return lut.table[icc_transform_lut_index(
        size,
        lower[0] + red,
        lower[1] + green,
        lower[2] + blue)];
  };
  const auto c000 = sample(0, 0, 0);
  const auto c111 = sample(1, 1, 1);
  std::array<IccRgbaFloat, 4> vertices{};
  std::array<double, 4> weights{};
  const auto red = fraction[0];
  const auto green = fraction[1];
  const auto blue = fraction[2];
  if (red >= green) {
    if (green >= blue) {
      vertices = {c000, sample(1, 0, 0), sample(1, 1, 0), c111};
      weights = {1.0 - red, red - green, green - blue, blue};
    } else if (red >= blue) {
      vertices = {c000, sample(1, 0, 0), sample(1, 0, 1), c111};
      weights = {1.0 - red, red - blue, blue - green, green};
    } else {
      vertices = {c000, sample(0, 0, 1), sample(1, 0, 1), c111};
      weights = {1.0 - blue, blue - red, red - green, green};
    }
  } else if (red >= blue) {
    vertices = {c000, sample(0, 1, 0), sample(1, 1, 0), c111};
    weights = {1.0 - green, green - red, red - blue, blue};
  } else if (green >= blue) {
    vertices = {c000, sample(0, 1, 0), sample(0, 1, 1), c111};
    weights = {1.0 - green, green - blue, blue - red, red};
  } else {
    vertices = {c000, sample(0, 0, 1), sample(0, 1, 1), c111};
    weights = {1.0 - blue, blue - green, green - red, red};
  }
  IccRgbaFloat output{.alpha = input.alpha};
  const auto interpolate = [&](auto IccRgbaFloat::*channel) {
    auto value = 0.0;
    for (std::size_t vertex = 0U; vertex < vertices.size(); ++vertex) {
      value += static_cast<double>(vertices[vertex].*channel) * weights[vertex];
    }
    return static_cast<float>(value);
  };
  output.red = interpolate(&IccRgbaFloat::red);
  output.green = interpolate(&IccRgbaFloat::green);
  output.blue = interpolate(&IccRgbaFloat::blue);
  return output;
}

[[nodiscard]] std::size_t icc_transform_lut_cell_index(
    const IccTransformLut3D& lut,
    const IccRgbaFloat& input) {
  const auto cell_size = lut.settings.cube_size - 1;
  const auto coordinate = [&](float value) {
    const auto encoded = std::pow(
        std::clamp(static_cast<double>(value), 0.0, 1.0),
        lut.settings.input_shaper_exponent);
    return std::min(
        static_cast<int>(encoded * static_cast<double>(cell_size)),
        cell_size - 1);
  };
  return icc_transform_lut_index(
      cell_size,
      coordinate(input.red),
      coordinate(input.green),
      coordinate(input.blue));
}

[[nodiscard]] bool icc_transform_lut_accepts_unchecked(
    const IccTransformLut3D& lut,
    const IccRgbaFloat& pixel) {
  if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) ||
      !std::isfinite(pixel.blue) || !std::isfinite(pixel.alpha) ||
      pixel.red < 0.0F || pixel.red > 1.0F || pixel.green < 0.0F ||
      pixel.green > 1.0F || pixel.blue < 0.0F || pixel.blue > 1.0F) {
    return false;
  }
  return lut.unsafe_cells[icc_transform_lut_cell_index(lut, pixel)] == 0U;
}

[[nodiscard]] std::string make_icc_transform_lut_cache_key(
    const CompiledIccTransform& transform,
    const IccTransformLutSettings& settings) {
  std::ostringstream stream;
  stream.precision(std::numeric_limits<double>::max_digits10);
  stream << "icc-transform-lut:v1|tetrahedral|power-shaper|"
         << settings.cube_size << '|' << settings.input_shaper_exponent << '|'
         << settings.p99_delta_e_00_limit << '|'
         << settings.maximum_delta_e_00_limit << '|'
         << settings.maximum_unsafe_cell_fraction << '|'
         << transform.description().cache_key;
  return stream.str();
}

[[nodiscard]] std::string make_icc_cache_key(
    std::string_view source_hash,
    std::string_view destination_hash,
    const IccTransformOptions& options) {
  return std::string(source_hash) + ":" + std::string(destination_hash) + ":" +
         std::to_string(static_cast<int>(options.rendering_intent)) + ":" +
         (options.black_point_compensation ? "bpc" : "no-bpc") + ":" +
         std::to_string(static_cast<int>(options.pixel_format)) + ":" +
         std::to_string(options.target.size()) + ":" + options.target;
}

[[nodiscard]] std::string make_icc_proof_cache_key(
    std::string_view source_hash,
    std::string_view proof_hash,
    std::string_view destination_hash,
    const IccProofTransformOptions& options) {
  return "proof:" + std::string(source_hash) + ":" + std::string(proof_hash) +
         ":" + std::string(destination_hash) + ":" +
         std::to_string(static_cast<int>(options.rendering_intent)) + ":" +
         std::to_string(static_cast<int>(options.proofing_intent)) + ":" +
         (options.black_point_compensation ? "bpc" : "no-bpc") + ":" +
         (options.gamut_check ? "gamut-check" : "no-gamut-check") + ":" +
         std::to_string(static_cast<int>(options.pixel_format)) + ":" +
         std::to_string(options.target.size()) + ":" + options.target;
}

#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
[[nodiscard]] cmsUInt32Number lcms_intent(IccRenderingIntent intent) {
  switch (intent) {
    case IccRenderingIntent::RelativeColorimetric:
      return INTENT_RELATIVE_COLORIMETRIC;
    case IccRenderingIntent::Saturation:
      return INTENT_SATURATION;
    case IccRenderingIntent::AbsoluteColorimetric:
      return INTENT_ABSOLUTE_COLORIMETRIC;
    case IccRenderingIntent::Perceptual:
    default:
      return INTENT_PERCEPTUAL;
  }
}

[[nodiscard]] std::string profile_description(cmsHPROFILE profile) {
  const auto required = cmsGetProfileInfoASCII(
      profile,
      cmsInfoDescription,
      "en",
      "US",
      nullptr,
      0);
  if (required <= 1) {
    return "unnamed-profile";
  }
  std::string value(static_cast<std::size_t>(required), '\0');
  (void)cmsGetProfileInfoASCII(
      profile,
      cmsInfoDescription,
      "en",
      "US",
      value.data(),
      required);
  while (!value.empty() && value.back() == '\0') {
    value.pop_back();
  }
  return value.empty() ? "unnamed-profile" : value;
}

[[nodiscard]] cmsHTRANSFORM create_rgb_to_lab_validation_transform(
    cmsContext context,
    cmsHPROFILE rgb_profile) {
  if (rgb_profile == nullptr) {
    return nullptr;
  }
  auto lab_profile = cmsCreateLab4ProfileTHR(context, nullptr);
  if (lab_profile == nullptr) {
    return nullptr;
  }
  const auto transform = cmsCreateTransformTHR(
      context,
      rgb_profile,
      TYPE_RGBA_FLT,
      lab_profile,
      TYPE_LabA_FLT,
      INTENT_RELATIVE_COLORIMETRIC,
      static_cast<cmsUInt32Number>(cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE));
  cmsCloseProfile(lab_profile);
  return transform;
}
#endif

}  // namespace

bool IccTransformLut3D::valid() const {
  if (schema != "icc-transform-lut:v1" || transform_cache_key.empty() ||
      cache_key.empty() ||
      !icc_transform_lut_settings_valid(settings) ||
      validation_sample_count == 0U ||
      !std::isfinite(measured_p99_delta_e_00) ||
      !std::isfinite(measured_maximum_delta_e_00) ||
      !std::isfinite(measured_maximum_channel_error) ||
      measured_p99_delta_e_00 > settings.p99_delta_e_00_limit ||
      measured_maximum_delta_e_00 >
          settings.maximum_delta_e_00_limit) {
    return false;
  }
  const auto size = static_cast<std::size_t>(settings.cube_size);
  const auto cell_size = size - 1U;
  if (table.size() != size * size * size ||
      unsafe_cells.size() != cell_size * cell_size * cell_size ||
      unsafe_cell_count != static_cast<std::size_t>(std::count(
                               unsafe_cells.cbegin(),
                               unsafe_cells.cend(),
                               std::uint8_t{1}))) {
    return false;
  }
  const auto unsafe_fraction = unsafe_cells.empty()
                                   ? 0.0
                                   : static_cast<double>(unsafe_cell_count) /
                                         static_cast<double>(unsafe_cells.size());
  if (unsafe_fraction > settings.maximum_unsafe_cell_fraction) {
    return false;
  }
  return std::all_of(
      table.cbegin(),
      table.cend(),
      [](const IccRgbaFloat& value) {
        return std::isfinite(value.red) && std::isfinite(value.green) &&
               std::isfinite(value.blue) && std::isfinite(value.alpha) &&
               value.alpha == 1.0F;
      });
}

bool IccTransformLut3D::accepts(const IccRgbaFloat& pixel) const {
  return valid() && icc_transform_lut_accepts_unchecked(*this, pixel);
}

std::size_t IccTransformLut3D::byte_size() const {
  return table.size() * sizeof(IccRgbaFloat) +
         unsafe_cells.size() * sizeof(std::uint8_t);
}

CompiledIccTransform::CompiledIccTransform() : impl_(std::make_shared<Impl>()) {}

CompiledIccTransform::CompiledIccTransform(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

CompiledIccTransform::~CompiledIccTransform() = default;
CompiledIccTransform::CompiledIccTransform(const CompiledIccTransform&) = default;
CompiledIccTransform& CompiledIccTransform::operator=(const CompiledIccTransform&) = default;
CompiledIccTransform::CompiledIccTransform(CompiledIccTransform&&) noexcept = default;
CompiledIccTransform& CompiledIccTransform::operator=(CompiledIccTransform&&) noexcept = default;

bool CompiledIccTransform::ready() const {
  return impl_ != nullptr && impl_->description.status == IccTransformStatus::Ready;
}

const IccTransformDescription& CompiledIccTransform::description() const {
  static const IccTransformDescription unavailable{};
  return impl_ != nullptr ? impl_->description : unavailable;
}

bool CompiledIccTransform::apply(std::span<IccRgbaFloat> pixels) const {
  if (!ready()) {
    return false;
  }
  if (pixels.empty()) {
    return true;
  }
#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
  const std::lock_guard lock(impl_->apply_mutex);
  constexpr std::size_t kChunkPixels = 1U << 20U;
  std::size_t offset = 0;
  while (offset < pixels.size()) {
    const auto count = std::min(kChunkPixels, pixels.size() - offset);
    auto* data = pixels.data() + offset;
    cmsDoTransform(
        impl_->transform,
        data,
        data,
        static_cast<cmsUInt32Number>(count));
    offset += count;
  }
  return true;
#else
  return false;
#endif
}

std::vector<std::uint8_t> CompiledIccTransform::gamut_mask(
    std::span<const IccRgbaFloat> pixels) const {
  if (!ready() || !description().gamut_check) {
    return {};
  }
  std::vector<std::uint8_t> mask(pixels.size(), 0U);
  if (pixels.empty()) {
    return mask;
  }
#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
  auto transformed = std::vector<IccRgbaFloat>(pixels.begin(), pixels.end());
  const std::lock_guard lock(impl_->apply_mutex);
  constexpr std::size_t kChunkPixels = 1U << 20U;
  std::size_t offset = 0;
  while (offset < transformed.size()) {
    const auto count = std::min(kChunkPixels, transformed.size() - offset);
    auto* data = transformed.data() + offset;
    cmsDoTransform(
        impl_->transform,
        data,
        data,
        static_cast<cmsUInt32Number>(count));
    offset += count;
  }
  constexpr auto kAlarmTolerance = 2.0F / 65535.0F;
  const auto alarm_red = static_cast<float>(kGamutAlarmCodes[0]) / 65535.0F;
  const auto alarm_green = static_cast<float>(kGamutAlarmCodes[1]) / 65535.0F;
  const auto alarm_blue = static_cast<float>(kGamutAlarmCodes[2]) / 65535.0F;
  for (std::size_t index = 0; index < transformed.size(); ++index) {
    const auto& pixel = transformed[index];
    mask[index] =
        std::abs(pixel.red - alarm_red) <= kAlarmTolerance &&
                std::abs(pixel.green - alarm_green) <= kAlarmTolerance &&
                std::abs(pixel.blue - alarm_blue) <= kAlarmTolerance
            ? 255U
            : 0U;
  }
  return mask;
#else
  return {};
#endif
}

std::string color_payload_sha256(std::span<const std::byte> payload) {
  Sha256Accumulator accumulator;
  return accumulator.update(payload) ? accumulator.digest() : std::string{};
}

std::string color_file_sha256(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  Sha256Accumulator accumulator;
  std::array<char, 64U * 1024U> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      const auto bytes = std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(buffer.data()),
          static_cast<std::size_t>(count));
      if (!accumulator.update(bytes)) {
        return {};
      }
    }
  }
  if (!input.eof()) {
    return {};
  }
  return accumulator.digest();
}

ColorMatrix3 identity_matrix3() {
  return {};
}

double srgb_to_linear(double value) {
  const auto clamped = std::clamp(value, 0.0, 1.0);
  if (clamped <= 0.04045) {
    return clamped / 12.92;
  }
  return std::pow((clamped + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) {
  const auto clamped = std::clamp(value, 0.0, 1.0);
  if (clamped <= 0.0031308) {
    return clamped * 12.92;
  }
  return 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
}

double linear_ap1_to_acescct(double value) {
  constexpr auto linear_break = 0.0078125;
  constexpr auto linear_slope = 10.5402377416545;
  constexpr auto linear_offset = 0.0729055341958355;
  if (value <= linear_break) {
    return linear_slope * value + linear_offset;
  }
  return (std::log2(value) + 9.72) / 17.52;
}

double acescct_to_linear_ap1(double value) {
  constexpr auto encoded_break = 0.155251141552511;
  constexpr auto linear_slope = 10.5402377416545;
  constexpr auto linear_offset = 0.0729055341958355;
  constexpr auto maximum_linear_value = 65504.0;
  const auto maximum_encoded_value =
      (std::log2(maximum_linear_value) + 9.72) / 17.52;
  if (std::isnan(value)) {
    return value;
  }
  if (value <= encoded_break) {
    return (value - linear_offset) / linear_slope;
  }
  if (value < maximum_encoded_value) {
    return std::exp2(value * 17.52 - 9.72);
  }
  return maximum_linear_value;
}

ColorMatrix3 matrix3_from_rows(
    double m00,
    double m01,
    double m02,
    double m10,
    double m11,
    double m12,
    double m20,
    double m21,
    double m22) {
  return ColorMatrix3{
      .values = {
          m00, m01, m02,
          m10, m11, m12,
          m20, m21, m22,
      },
  };
}

ColorMatrix3 multiply_matrix3(const ColorMatrix3& left, const ColorMatrix3& right) {
  ColorMatrix3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      auto value = 0.0;
      for (int inner = 0; inner < 3; ++inner) {
        value += left.values[static_cast<std::size_t>(row * 3 + inner)] *
                 right.values[static_cast<std::size_t>(inner * 3 + column)];
      }
      result.values[static_cast<std::size_t>(row * 3 + column)] = value;
    }
  }
  return result;
}

std::array<double, 3> multiply_matrix3_vector(
    const ColorMatrix3& matrix,
    const std::array<double, 3>& vector) {
  return {
      matrix.values[0] * vector[0] + matrix.values[1] * vector[1] +
          matrix.values[2] * vector[2],
      matrix.values[3] * vector[0] + matrix.values[4] * vector[1] +
          matrix.values[5] * vector[2],
      matrix.values[6] * vector[0] + matrix.values[7] * vector[1] +
          matrix.values[8] * vector[2],
  };
}

ColorXyz multiply_matrix3_xyz(const ColorMatrix3& matrix, const ColorXyz& xyz) {
  const auto transformed = multiply_matrix3_vector(matrix, {xyz.x, xyz.y, xyz.z});
  return ColorXyz{
      .x = transformed[0],
      .y = transformed[1],
      .z = transformed[2],
  };
}

ColorMatrix3 invert_matrix3(const ColorMatrix3& matrix) {
  const auto& values = matrix.values;
  const auto determinant =
      values[0] * (values[4] * values[8] - values[5] * values[7]) -
      values[1] * (values[3] * values[8] - values[5] * values[6]) +
      values[2] * (values[3] * values[7] - values[4] * values[6]);
  if (std::abs(determinant) <= 1.0e-12) {
    return identity_matrix3();
  }
  const auto inverse = 1.0 / determinant;
  return matrix3_from_rows(
      (values[4] * values[8] - values[5] * values[7]) * inverse,
      (values[2] * values[7] - values[1] * values[8]) * inverse,
      (values[1] * values[5] - values[2] * values[4]) * inverse,
      (values[5] * values[6] - values[3] * values[8]) * inverse,
      (values[0] * values[8] - values[2] * values[6]) * inverse,
      (values[2] * values[3] - values[0] * values[5]) * inverse,
      (values[3] * values[7] - values[4] * values[6]) * inverse,
      (values[1] * values[6] - values[0] * values[7]) * inverse,
      (values[0] * values[4] - values[1] * values[3]) * inverse);
}

ColorXyz xyz_from_chromaticity(const ColorChromaticity& chromaticity) {
  // ACES AP0 intentionally includes an imaginary blue primary with negative
  // y. Preserve its sign; only guard the actual singularity at y == 0.
  const auto safe_y = std::abs(chromaticity.y) < 1.0e-12
                          ? std::copysign(1.0e-12, chromaticity.y)
                          : chromaticity.y;
  return {
      .x = chromaticity.x / safe_y,
      .y = 1.0,
      .z = (1.0 - chromaticity.x - chromaticity.y) / safe_y,
  };
}

ColorChromaticity white_point_chromaticity(WhitePoint white_point) {
  switch (white_point) {
    case WhitePoint::D50:
      return {.x = 0.34567, .y = 0.35850};
    case WhitePoint::AcesD60:
      return {.x = 0.32168, .y = 0.33767};
    case WhitePoint::D65:
    case WhitePoint::Unknown:
    default:
      return {.x = 0.31271, .y = 0.32902};
  }
}

std::array<ColorChromaticity, 3> rgb_primaries(ColorPrimaries primaries) {
  switch (primaries) {
    case ColorPrimaries::AcesAp0:
      return {{{.x = 0.73470, .y = 0.26530},
               {.x = 0.00000, .y = 1.00000},
               {.x = 0.00010, .y = -0.07700}}};
    case ColorPrimaries::AcesAp1:
      return {{{.x = 0.713, .y = 0.293},
               {.x = 0.165, .y = 0.830},
               {.x = 0.128, .y = 0.044}}};
    case ColorPrimaries::DisplayP3:
      return {{{.x = 0.680, .y = 0.320},
               {.x = 0.265, .y = 0.690},
               {.x = 0.150, .y = 0.060}}};
    case ColorPrimaries::AdobeRgb:
      return {{{.x = 0.640, .y = 0.330},
               {.x = 0.210, .y = 0.710},
               {.x = 0.150, .y = 0.060}}};
    case ColorPrimaries::ProPhotoRgb:
      return {{{.x = 0.7347, .y = 0.2653},
               {.x = 0.1596, .y = 0.8404},
               {.x = 0.0366, .y = 0.0001}}};
    case ColorPrimaries::Rec2020:
      return {{{.x = 0.708, .y = 0.292},
               {.x = 0.170, .y = 0.797},
               {.x = 0.131, .y = 0.046}}};
    case ColorPrimaries::Rec709:
    case ColorPrimaries::Srgb:
    case ColorPrimaries::Unknown:
    default:
      return {{{.x = 0.640, .y = 0.330},
               {.x = 0.300, .y = 0.600},
               {.x = 0.150, .y = 0.060}}};
  }
}

ColorMatrix3 rgb_to_xyz_matrix(const RgbColorSpace& color_space) {
  const auto primaries = rgb_primaries(color_space.primaries);
  const auto red = xyz_from_chromaticity(primaries[0]);
  const auto green = xyz_from_chromaticity(primaries[1]);
  const auto blue = xyz_from_chromaticity(primaries[2]);
  const auto white = xyz_from_chromaticity(
      white_point_chromaticity(color_space.white_point));
  const auto base = matrix3_from_rows(
      red.x, green.x, blue.x,
      red.y, green.y, blue.y,
      red.z, green.z, blue.z);
  const auto scales = multiply_matrix3_vector(
      invert_matrix3(base),
      {white.x, white.y, white.z});
  return matrix3_from_rows(
      red.x * scales[0], green.x * scales[1], blue.x * scales[2],
      red.y * scales[0], green.y * scales[1], blue.y * scales[2],
      red.z * scales[0], green.z * scales[1], blue.z * scales[2]);
}

ColorMatrix3 chromatic_adaptation_cone_matrix(std::string_view method) {
  return method == "CAT02"
             ? matrix3_from_rows(
                   0.7328, 0.4296, -0.1624,
                   -0.7036, 1.6975, 0.0061,
                   0.0030, 0.0136, 0.9834)
             : matrix3_from_rows(
                   0.8951, 0.2664, -0.1614,
                   -0.7502, 1.7135, 0.0367,
                   0.0389, -0.0685, 1.0296);
}

ColorMatrix3 chromatic_adaptation_matrix(
    const ColorXyz& source_white,
    const ColorXyz& target_white,
    std::string_view method) {
  const auto cone = chromatic_adaptation_cone_matrix(method);
  const auto source_response = multiply_matrix3_xyz(cone, source_white);
  const auto target_response = multiply_matrix3_xyz(cone, target_white);
  const auto scale = matrix3_from_rows(
      target_response.x / std::max(1.0e-6, source_response.x), 0.0, 0.0,
      0.0, target_response.y / std::max(1.0e-6, source_response.y), 0.0,
      0.0, 0.0, target_response.z / std::max(1.0e-6, source_response.z));
  return multiply_matrix3(invert_matrix3(cone), multiply_matrix3(scale, cone));
}

CompiledBuiltinColorTransform compile_builtin_color_transform(
    const RgbColorSpace& source,
    const RgbColorSpace& destination,
    std::string_view chromatic_adaptation_method) {
  const auto source_to_xyz = rgb_to_xyz_matrix(source);
  const auto xyz_to_destination = invert_matrix3(rgb_to_xyz_matrix(destination));
  const auto source_white = xyz_from_chromaticity(
      white_point_chromaticity(source.white_point));
  const auto destination_white = xyz_from_chromaticity(
      white_point_chromaticity(destination.white_point));
  const auto adaptation = chromatic_adaptation_matrix(
      source_white,
      destination_white,
      chromatic_adaptation_method);
  return {
      .matrix = multiply_matrix3(
          xyz_to_destination,
          multiply_matrix3(adaptation, source_to_xyz)),
      .source = source,
      .destination = destination,
      .chromatic_adaptation_method = std::string(chromatic_adaptation_method),
      .provenance = "builtin-matrix:v1",
  };
}

std::vector<std::byte> builtin_rgb_icc_profile_payload(
    const RgbColorSpace& color_space) {
#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
  if (color_space.primaries == ColorPrimaries::Unknown ||
      color_space.white_point == WhitePoint::Unknown ||
      color_space.transfer_function == TransferFunction::Unknown) {
    return {};
  }

  const auto cache_key = std::make_tuple(
      static_cast<int>(color_space.primaries),
      static_cast<int>(color_space.transfer_function),
      static_cast<int>(color_space.white_point));
  auto& cache = builtin_icc_profile_cache();
  {
    const std::lock_guard lock(cache.mutex);
    const auto cached = cache.payloads.find(cache_key);
    if (cached != cache.payloads.end()) {
      return cached->second;
    }
  }

  const auto white_xy = white_point_chromaticity(color_space.white_point);
  const auto primary_xy = rgb_primaries(color_space.primaries);
  const cmsCIExyY white_point{
      .x = white_xy.x,
      .y = white_xy.y,
      .Y = 1.0,
  };
  cmsCIExyYTRIPLE primaries{};
  primaries.Red = cmsCIExyY{.x = primary_xy[0].x, .y = primary_xy[0].y, .Y = 1.0};
  primaries.Green = cmsCIExyY{.x = primary_xy[1].x, .y = primary_xy[1].y, .Y = 1.0};
  primaries.Blue = cmsCIExyY{.x = primary_xy[2].x, .y = primary_xy[2].y, .Y = 1.0};

  std::array<cmsToneCurve*, 3> curves = {
      build_transfer_curve(color_space.transfer_function),
      build_transfer_curve(color_space.transfer_function),
      build_transfer_curve(color_space.transfer_function),
  };
  if (std::any_of(curves.cbegin(), curves.cend(), [](const cmsToneCurve* curve) {
        return curve == nullptr;
      })) {
    for (auto* curve : curves) {
      if (curve != nullptr) {
        cmsFreeToneCurve(curve);
      }
    }
    return {};
  }

  auto* profile = cmsCreateRGBProfileTHR(
      nullptr,
      &white_point,
      &primaries,
      curves.data());
  for (auto* curve : curves) {
    cmsFreeToneCurve(curve);
  }
  if (profile == nullptr) {
    return {};
  }
  cmsSetProfileVersion(profile, 4.4);
  cmsSetDeviceClass(profile, cmsSigDisplayClass);

  cmsUInt32Number byte_count = 0;
  if (!cmsSaveProfileToMem(profile, nullptr, &byte_count) || byte_count == 0U ||
      byte_count > kMaximumIccProfileBytes) {
    cmsCloseProfile(profile);
    return {};
  }
  std::vector<std::byte> payload(byte_count);
  if (!cmsSaveProfileToMem(profile, payload.data(), &byte_count)) {
    cmsCloseProfile(profile);
    return {};
  }
  cmsCloseProfile(profile);
  payload.resize(byte_count);
  canonicalize_generated_icc_header(&payload);
  {
    const std::lock_guard lock(cache.mutex);
    const auto [cached, inserted] = cache.payloads.emplace(cache_key, payload);
    static_cast<void>(inserted);
    return cached->second;
  }
#else
  static_cast<void>(color_space);
  return {};
#endif
}

CompiledIccTransform compile_builtin_to_icc_transform(
    const RgbColorSpace& source_color_space,
    std::span<const std::byte> destination_profile,
    const IccTransformOptions& options) {
  const auto source_profile = builtin_rgb_icc_profile_payload(source_color_space);
  return compile_icc_transform(source_profile, destination_profile, options);
}

CompiledIccTransform compile_icc_to_builtin_transform(
    std::span<const std::byte> source_profile,
    const RgbColorSpace& destination_color_space,
    const IccTransformOptions& options) {
  const auto destination_profile = builtin_rgb_icc_profile_payload(destination_color_space);
  return compile_icc_transform(source_profile, destination_profile, options);
}

CompiledIccTransform compile_icc_transform(
    std::span<const std::byte> source_profile,
    std::span<const std::byte> destination_profile,
    IccRenderingIntent rendering_intent,
    bool black_point_compensation) {
  return compile_icc_transform(
      source_profile,
      destination_profile,
      IccTransformOptions{
          .rendering_intent = rendering_intent,
          .black_point_compensation = black_point_compensation,
      });
}

CompiledIccTransform compile_icc_transform(
    std::span<const std::byte> source_profile,
    std::span<const std::byte> destination_profile,
    const IccTransformOptions& options) {
  auto impl = std::make_shared<CompiledIccTransform::Impl>();
  impl->description.rendering_intent = options.rendering_intent;
  impl->description.black_point_compensation = options.black_point_compensation;
  impl->description.pixel_format = options.pixel_format;
  impl->description.target = options.target;
  impl->description.source_profile_sha256 = color_payload_sha256(source_profile);
  impl->description.destination_profile_sha256 = color_payload_sha256(destination_profile);
  impl->description.cache_key = make_icc_cache_key(
      impl->description.source_profile_sha256,
      impl->description.destination_profile_sha256,
      options);

  if (source_profile.size() > kMaximumIccProfileBytes ||
      destination_profile.size() > kMaximumIccProfileBytes) {
    impl->description.status = IccTransformStatus::ProfilePayloadTooLarge;
    impl->description.failure_reason = "icc_profile_payload_exceeds_16_mib_limit";
    return CompiledIccTransform(std::move(impl));
  }

  {
    auto& cache = icc_transform_cache();
    const std::lock_guard lock(cache.mutex);
    const auto match = std::find_if(
        cache.entries.begin(),
        cache.entries.end(),
        [&impl](const IccTransformCacheEntry& entry) {
          return entry.key == impl->description.cache_key;
        });
    if (match != cache.entries.end()) {
      auto cached = match->transform;
      cache.entries.splice(cache.entries.begin(), cache.entries, match);
      ++cache.hits;
      return CompiledIccTransform(std::move(cached));
    }
    ++cache.misses;
  }
#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
  impl->description.backend = "Little CMS";
  impl->description.backend_version = std::to_string(cmsGetEncodedCMMversion());
  if (source_profile.empty() ||
      source_profile.size() > std::numeric_limits<cmsUInt32Number>::max()) {
    impl->description.status = IccTransformStatus::InvalidSourceProfile;
    impl->description.failure_reason = "invalid_source_profile_payload";
    return CompiledIccTransform(std::move(impl));
  }
  if (destination_profile.size() > std::numeric_limits<cmsUInt32Number>::max()) {
    impl->description.status = IccTransformStatus::InvalidDestinationProfile;
    impl->description.failure_reason = "invalid_destination_profile_payload";
    return CompiledIccTransform(std::move(impl));
  }
  auto source = cmsOpenProfileFromMem(
      source_profile.data(),
      static_cast<cmsUInt32Number>(source_profile.size()));
  if (source == nullptr) {
    impl->description.status = IccTransformStatus::InvalidSourceProfile;
    impl->description.failure_reason = "little_cms_rejected_source_profile";
    return CompiledIccTransform(std::move(impl));
  }
  const auto source_class = cmsGetDeviceClass(source);
  const auto is_device_link = source_class == cmsSigLinkClass;
  if (is_device_link && !destination_profile.empty()) {
    cmsCloseProfile(source);
    impl->description.status = IccTransformStatus::UnsupportedSourceProfile;
    impl->description.failure_reason = "device_link_requires_dedicated_compile_api";
    return CompiledIccTransform(std::move(impl));
  }
  if (!is_device_link && destination_profile.empty()) {
    cmsCloseProfile(source);
    impl->description.status = IccTransformStatus::InvalidDestinationProfile;
    impl->description.failure_reason = "invalid_destination_profile_payload";
    return CompiledIccTransform(std::move(impl));
  }
  cmsHPROFILE destination = nullptr;
  if (!is_device_link) {
    destination = cmsOpenProfileFromMem(
        destination_profile.data(),
        static_cast<cmsUInt32Number>(destination_profile.size()));
    if (destination == nullptr) {
      cmsCloseProfile(source);
      impl->description.status = IccTransformStatus::InvalidDestinationProfile;
      impl->description.failure_reason = "little_cms_rejected_destination_profile";
      return CompiledIccTransform(std::move(impl));
    }
  }
  if (cmsGetColorSpace(source) != cmsSigRgbData) {
    cmsCloseProfile(source);
    if (destination != nullptr) {
      cmsCloseProfile(destination);
    }
    impl->description.status = IccTransformStatus::UnsupportedSourceProfile;
    impl->description.failure_reason = "source_profile_is_not_rgb";
    return CompiledIccTransform(std::move(impl));
  }
  if (is_device_link && cmsGetPCS(source) != cmsSigRgbData) {
    cmsCloseProfile(source);
    impl->description.status = IccTransformStatus::UnsupportedDestinationProfile;
    impl->description.failure_reason = "device_link_output_is_not_rgb";
    return CompiledIccTransform(std::move(impl));
  }
  if (destination != nullptr && cmsGetDeviceClass(destination) == cmsSigLinkClass) {
    cmsCloseProfile(source);
    cmsCloseProfile(destination);
    impl->description.status = IccTransformStatus::UnsupportedDestinationProfile;
    impl->description.failure_reason = "destination_profile_is_device_link";
    return CompiledIccTransform(std::move(impl));
  }
  if (destination != nullptr && cmsGetColorSpace(destination) != cmsSigRgbData) {
    cmsCloseProfile(source);
    cmsCloseProfile(destination);
    impl->description.status = IccTransformStatus::UnsupportedDestinationProfile;
    impl->description.failure_reason = "destination_profile_is_not_rgb";
    return CompiledIccTransform(std::move(impl));
  }
  impl->description.source_profile_description = profile_description(source);
  impl->description.device_link = is_device_link;
  impl->description.rendering_intent_baked = is_device_link;
  impl->description.destination_profile_description =
      is_device_link ? "embedded device-link output" : profile_description(destination);
  auto flags = static_cast<cmsUInt32Number>(cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE);
  if (options.black_point_compensation) {
    flags = static_cast<cmsUInt32Number>(flags | cmsFLAGS_BLACKPOINTCOMPENSATION);
  }
  if (is_device_link) {
    std::array<cmsHPROFILE, 1> profiles = {source};
    impl->transform = cmsCreateMultiprofileTransform(
        profiles.data(),
        static_cast<cmsUInt32Number>(profiles.size()),
        TYPE_RGBA_FLT,
        TYPE_RGBA_FLT,
        lcms_intent(options.rendering_intent),
        flags);
  } else {
    impl->transform = cmsCreateTransform(
        source,
        TYPE_RGBA_FLT,
        destination,
        TYPE_RGBA_FLT,
        lcms_intent(options.rendering_intent),
        flags);
    impl->output_to_lab_transform =
        create_rgb_to_lab_validation_transform(nullptr, destination);
  }
  cmsCloseProfile(source);
  if (destination != nullptr) {
    cmsCloseProfile(destination);
  }
  if (impl->transform == nullptr) {
    impl->description.status = IccTransformStatus::CompileFailed;
    impl->description.failure_reason = "little_cms_transform_compile_failed";
    return CompiledIccTransform(std::move(impl));
  }
  impl->description.status = IccTransformStatus::Ready;
  {
    auto& cache = icc_transform_cache();
    const std::lock_guard lock(cache.mutex);
    const auto existing = std::find_if(
        cache.entries.begin(),
        cache.entries.end(),
        [&impl](const IccTransformCacheEntry& entry) {
          return entry.key == impl->description.cache_key;
        });
    if (existing != cache.entries.end()) {
      auto cached = existing->transform;
      cache.entries.splice(cache.entries.begin(), cache.entries, existing);
      ++cache.hits;
      return CompiledIccTransform(std::move(cached));
    }
    cache.entries.push_front({impl->description.cache_key, impl});
    if (cache.entries.size() > kIccTransformCacheCapacity) {
      cache.entries.pop_back();
      ++cache.evictions;
    }
  }
#else
  (void)source_profile;
  (void)destination_profile;
  impl->description.status = IccTransformStatus::BackendUnavailable;
  impl->description.backend = "none";
  impl->description.failure_reason = "little_cms_not_compiled";
#endif
  return CompiledIccTransform(std::move(impl));
}

CompiledIccTransform compile_icc_device_link(
    std::span<const std::byte> device_link_profile,
    const IccTransformOptions& options) {
  return compile_icc_transform(device_link_profile, {}, options);
}

CompiledIccTransform compile_icc_proofing_transform(
    std::span<const std::byte> source_profile,
    std::span<const std::byte> proof_profile,
    std::span<const std::byte> destination_profile,
    const IccProofTransformOptions& options) {
  auto impl = std::make_shared<CompiledIccTransform::Impl>();
  impl->description.rendering_intent = options.rendering_intent;
  impl->description.proofing_intent = options.proofing_intent;
  impl->description.black_point_compensation = options.black_point_compensation;
  impl->description.pixel_format = options.pixel_format;
  impl->description.target = options.target;
  impl->description.soft_proofing = true;
  impl->description.gamut_check = options.gamut_check;
  impl->description.source_profile_sha256 = color_payload_sha256(source_profile);
  impl->description.proof_profile_sha256 = color_payload_sha256(proof_profile);
  impl->description.destination_profile_sha256 = color_payload_sha256(destination_profile);
  impl->description.cache_key = make_icc_proof_cache_key(
      impl->description.source_profile_sha256,
      impl->description.proof_profile_sha256,
      impl->description.destination_profile_sha256,
      options);

  if (source_profile.size() > kMaximumIccProfileBytes ||
      proof_profile.size() > kMaximumIccProfileBytes ||
      destination_profile.size() > kMaximumIccProfileBytes) {
    impl->description.status = IccTransformStatus::ProfilePayloadTooLarge;
    impl->description.failure_reason = "icc_profile_payload_exceeds_16_mib_limit";
    return CompiledIccTransform(std::move(impl));
  }

  {
    auto& cache = icc_transform_cache();
    const std::lock_guard lock(cache.mutex);
    const auto match = std::find_if(
        cache.entries.begin(),
        cache.entries.end(),
        [&impl](const IccTransformCacheEntry& entry) {
          return entry.key == impl->description.cache_key;
        });
    if (match != cache.entries.end()) {
      auto cached = match->transform;
      cache.entries.splice(cache.entries.begin(), cache.entries, match);
      ++cache.hits;
      return CompiledIccTransform(std::move(cached));
    }
    ++cache.misses;
  }

#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
  impl->description.backend = "Little CMS";
  impl->description.backend_version = std::to_string(cmsGetEncodedCMMversion());
  const auto payload_valid = [](std::span<const std::byte> payload) {
    return !payload.empty() &&
           payload.size() <= std::numeric_limits<cmsUInt32Number>::max();
  };
  if (!payload_valid(source_profile)) {
    impl->description.status = IccTransformStatus::InvalidSourceProfile;
    impl->description.failure_reason = "invalid_source_profile_payload";
    return CompiledIccTransform(std::move(impl));
  }
  if (!payload_valid(proof_profile)) {
    impl->description.status = IccTransformStatus::InvalidProofProfile;
    impl->description.failure_reason = "invalid_proof_profile_payload";
    return CompiledIccTransform(std::move(impl));
  }
  if (!payload_valid(destination_profile)) {
    impl->description.status = IccTransformStatus::InvalidDestinationProfile;
    impl->description.failure_reason = "invalid_destination_profile_payload";
    return CompiledIccTransform(std::move(impl));
  }

  impl->context = cmsCreateContext(nullptr, nullptr);
  if (impl->context == nullptr) {
    impl->description.status = IccTransformStatus::CompileFailed;
    impl->description.failure_reason = "little_cms_context_creation_failed";
    return CompiledIccTransform(std::move(impl));
  }
  std::array<cmsUInt16Number, cmsMAXCHANNELS> alarm_codes{};
  std::copy(kGamutAlarmCodes.begin(), kGamutAlarmCodes.end(), alarm_codes.begin());
  cmsSetAlarmCodesTHR(impl->context, alarm_codes.data());

  auto source = cmsOpenProfileFromMemTHR(
      impl->context,
      source_profile.data(),
      static_cast<cmsUInt32Number>(source_profile.size()));
  if (source == nullptr) {
    impl->description.status = IccTransformStatus::InvalidSourceProfile;
    impl->description.failure_reason = "little_cms_rejected_source_profile";
    return CompiledIccTransform(std::move(impl));
  }
  auto proof = cmsOpenProfileFromMemTHR(
      impl->context,
      proof_profile.data(),
      static_cast<cmsUInt32Number>(proof_profile.size()));
  if (proof == nullptr) {
    cmsCloseProfile(source);
    impl->description.status = IccTransformStatus::InvalidProofProfile;
    impl->description.failure_reason = "little_cms_rejected_proof_profile";
    return CompiledIccTransform(std::move(impl));
  }
  auto destination = cmsOpenProfileFromMemTHR(
      impl->context,
      destination_profile.data(),
      static_cast<cmsUInt32Number>(destination_profile.size()));
  if (destination == nullptr) {
    cmsCloseProfile(source);
    cmsCloseProfile(proof);
    impl->description.status = IccTransformStatus::InvalidDestinationProfile;
    impl->description.failure_reason = "little_cms_rejected_destination_profile";
    return CompiledIccTransform(std::move(impl));
  }

  const auto source_supported =
      cmsGetDeviceClass(source) != cmsSigLinkClass &&
      cmsGetColorSpace(source) == cmsSigRgbData;
  const auto proof_class = cmsGetDeviceClass(proof);
  const auto proof_supported =
      proof_class != cmsSigLinkClass &&
      (cmsGetPCS(proof) == cmsSigXYZData || cmsGetPCS(proof) == cmsSigLabData);
  const auto destination_supported =
      cmsGetDeviceClass(destination) != cmsSigLinkClass &&
      cmsGetColorSpace(destination) == cmsSigRgbData;
  if (!source_supported || !proof_supported || !destination_supported) {
    cmsCloseProfile(source);
    cmsCloseProfile(proof);
    cmsCloseProfile(destination);
    if (!source_supported) {
      impl->description.status = IccTransformStatus::UnsupportedSourceProfile;
      impl->description.failure_reason = "proof_source_profile_is_not_rgb";
    } else if (!proof_supported) {
      impl->description.status = IccTransformStatus::UnsupportedProofProfile;
      impl->description.failure_reason = "proof_profile_has_unsupported_class_or_pcs";
    } else {
      impl->description.status = IccTransformStatus::UnsupportedDestinationProfile;
      impl->description.failure_reason = "proof_destination_profile_is_not_rgb";
    }
    return CompiledIccTransform(std::move(impl));
  }

  impl->description.source_profile_description = profile_description(source);
  impl->description.proof_profile_description = profile_description(proof);
  impl->description.destination_profile_description = profile_description(destination);
  auto flags = static_cast<cmsUInt32Number>(
      cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE | cmsFLAGS_SOFTPROOFING);
  if (options.black_point_compensation) {
    flags = static_cast<cmsUInt32Number>(flags | cmsFLAGS_BLACKPOINTCOMPENSATION);
  }
  if (options.gamut_check) {
    flags = static_cast<cmsUInt32Number>(flags | cmsFLAGS_GAMUTCHECK);
  }
  impl->transform = cmsCreateProofingTransformTHR(
      impl->context,
      source,
      TYPE_RGBA_FLT,
      destination,
      TYPE_RGBA_FLT,
      proof,
      lcms_intent(options.rendering_intent),
      lcms_intent(options.proofing_intent),
      flags);
  impl->output_to_lab_transform = create_rgb_to_lab_validation_transform(
      impl->context,
      destination);
  cmsCloseProfile(source);
  cmsCloseProfile(proof);
  cmsCloseProfile(destination);
  if (impl->transform == nullptr) {
    impl->description.status = IccTransformStatus::CompileFailed;
    impl->description.failure_reason = "little_cms_proof_transform_compile_failed";
    return CompiledIccTransform(std::move(impl));
  }
  impl->description.status = IccTransformStatus::Ready;
  {
    auto& cache = icc_transform_cache();
    const std::lock_guard lock(cache.mutex);
    const auto existing = std::find_if(
        cache.entries.begin(),
        cache.entries.end(),
        [&impl](const IccTransformCacheEntry& entry) {
          return entry.key == impl->description.cache_key;
        });
    if (existing != cache.entries.end()) {
      auto cached = existing->transform;
      cache.entries.splice(cache.entries.begin(), cache.entries, existing);
      ++cache.hits;
      return CompiledIccTransform(std::move(cached));
    }
    cache.entries.push_front({impl->description.cache_key, impl});
    if (cache.entries.size() > kIccTransformCacheCapacity) {
      cache.entries.pop_back();
      ++cache.evictions;
    }
  }
#else
  (void)source_profile;
  (void)proof_profile;
  (void)destination_profile;
  impl->description.status = IccTransformStatus::BackendUnavailable;
  impl->description.backend = "none";
  impl->description.failure_reason = "little_cms_not_compiled";
#endif
  return CompiledIccTransform(std::move(impl));
}

IccTransformCacheStats icc_transform_cache_stats() {
  auto& cache = icc_transform_cache();
  const std::lock_guard lock(cache.mutex);
  return {
      .hits = cache.hits,
      .misses = cache.misses,
      .evictions = cache.evictions,
      .entries = cache.entries.size(),
      .capacity = kIccTransformCacheCapacity,
  };
}

IccTransformLutSettings icc_transform_lut_settings(
    IccTransformLutQuality quality) {
  switch (quality) {
    case IccTransformLutQuality::Interactive:
      return {
          .cube_size = 33,
          .input_shaper_exponent = 1.0 / 3.0,
          .p99_delta_e_00_limit = 0.5,
          .maximum_delta_e_00_limit = 2.0,
      };
    case IccTransformLutQuality::Final:
      return {
          .cube_size = 97,
          .input_shaper_exponent = 1.0 / 3.0,
          .p99_delta_e_00_limit = 0.25,
          .maximum_delta_e_00_limit = 0.75,
      };
  }
  return {};
}

std::shared_ptr<const IccTransformLut3D> find_cached_icc_transform_lut(
    const CompiledIccTransform& transform,
    IccTransformLutSettings settings) {
  if (!transform.ready() || !icc_transform_lut_settings_valid(settings)) {
    return {};
  }
  const auto key = make_icc_transform_lut_cache_key(transform, settings);
  auto& cache = icc_transform_lut_cache();
  const std::scoped_lock lock(cache.mutex);
  ++cache.stats.requests;
  const auto match = std::find_if(
      cache.entries.begin(),
      cache.entries.end(),
      [&](const IccTransformLutCacheEntry& entry) {
        return entry.key == key;
      });
  if (match == cache.entries.end()) {
    ++cache.stats.misses;
    return {};
  }
  ++cache.stats.hits;
  match->last_use = ++cache.use_counter;
  cache.stats.last_failure_reason.clear();
  return match->lut;
}

std::shared_ptr<const IccTransformLut3D> compile_icc_transform_lut(
    const CompiledIccTransform& transform,
    IccTransformLutSettings settings) {
  auto& cache = icc_transform_lut_cache();
  const auto reject = [&](std::string reason) {
    const std::scoped_lock lock(cache.mutex);
    ++cache.stats.rejected;
    cache.stats.last_failure_reason = std::move(reason);
    return std::shared_ptr<const IccTransformLut3D>{};
  };
  if (!transform.ready()) {
    return reject("icc_transform_not_ready");
  }
  if (transform.description().gamut_check) {
    return reject("gamut_alarm_transform_requires_exact_processor");
  }
#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
  if (transform.impl_ == nullptr ||
      transform.impl_->output_to_lab_transform == nullptr) {
    return reject("icc_destination_lab_validation_unavailable");
  }
#endif
  if (!icc_transform_lut_settings_valid(settings)) {
    return reject("invalid_icc_transform_lut_settings");
  }
  const auto key = make_icc_transform_lut_cache_key(transform, settings);
  {
    const std::scoped_lock lock(cache.mutex);
    ++cache.stats.requests;
    const auto match = std::find_if(
        cache.entries.begin(),
        cache.entries.end(),
        [&](const IccTransformLutCacheEntry& entry) {
          return entry.key == key;
        });
    if (match != cache.entries.end()) {
      ++cache.stats.hits;
      match->last_use = ++cache.use_counter;
      cache.stats.last_failure_reason.clear();
      return match->lut;
    }
    ++cache.stats.misses;
  }

  const std::scoped_lock build_lock(cache.build_mutex);
  {
    const std::scoped_lock lock(cache.mutex);
    const auto raced_match = std::find_if(
        cache.entries.begin(),
        cache.entries.end(),
        [&](const IccTransformLutCacheEntry& entry) {
          return entry.key == key;
        });
    if (raced_match != cache.entries.end()) {
      ++cache.stats.hits;
      raced_match->last_use = ++cache.use_counter;
      cache.stats.last_failure_reason.clear();
      return raced_match->lut;
    }
  }

  const auto size = static_cast<std::size_t>(settings.cube_size);
  auto lut = std::make_shared<IccTransformLut3D>();
  lut->transform_cache_key = transform.description().cache_key;
  lut->cache_key = key;
  lut->settings = settings;
  lut->table.resize(size * size * size);
  const auto cell_size = size - 1U;
  lut->unsafe_cells.resize(cell_size * cell_size * cell_size, 0U);
  const auto inverse_exponent = 1.0 / settings.input_shaper_exponent;
  for (int red = 0; red < settings.cube_size; ++red) {
    const auto red_value = static_cast<float>(std::pow(
        static_cast<double>(red) /
            static_cast<double>(settings.cube_size - 1),
        inverse_exponent));
    for (int green = 0; green < settings.cube_size; ++green) {
      const auto green_value = static_cast<float>(std::pow(
          static_cast<double>(green) /
              static_cast<double>(settings.cube_size - 1),
          inverse_exponent));
      for (int blue = 0; blue < settings.cube_size; ++blue) {
        const auto blue_value = static_cast<float>(std::pow(
            static_cast<double>(blue) /
                static_cast<double>(settings.cube_size - 1),
            inverse_exponent));
        lut->table[icc_transform_lut_index(
            settings.cube_size,
            red,
            green,
            blue)] = {
            .red = red_value,
            .green = green_value,
            .blue = blue_value,
            .alpha = 1.0F,
        };
      }
    }
  }
  if (!transform.apply(lut->table)) {
    return reject("icc_transform_lut_bulk_apply_failed");
  }
  const auto invalid_table_value = std::find_if(
      lut->table.cbegin(),
      lut->table.cend(),
      [](const IccRgbaFloat& value) {
        return !std::isfinite(value.red) || !std::isfinite(value.green) ||
               !std::isfinite(value.blue) || !std::isfinite(value.alpha) ||
               value.alpha != 1.0F;
      });
  if (invalid_table_value != lut->table.cend()) {
    return reject("icc_transform_lut_contains_invalid_samples");
  }

  constexpr std::size_t kRandomValidationSamples = 4096U;
  std::vector<IccRgbaFloat> validation_inputs;
  validation_inputs.reserve(lut->unsafe_cells.size() + kRandomValidationSamples);
  std::vector<std::size_t> validation_cell_indices;
  validation_cell_indices.reserve(
      lut->unsafe_cells.size() + kRandomValidationSamples);
  const auto decode_shaper = [&](double coordinate) {
    return static_cast<float>(std::pow(
        std::clamp(coordinate, 0.0, 1.0),
        inverse_exponent));
  };
  const auto validation_cell_size = settings.cube_size - 1;
  for (int red = 0; red < validation_cell_size; ++red) {
    for (int green = 0; green < validation_cell_size; ++green) {
      for (int blue = 0; blue < validation_cell_size; ++blue) {
        const auto input = IccRgbaFloat{
            .red = decode_shaper(
                (static_cast<double>(red) + 0.5) /
                static_cast<double>(validation_cell_size)),
            .green = decode_shaper(
                (static_cast<double>(green) + 0.5) /
                static_cast<double>(validation_cell_size)),
            .blue = decode_shaper(
                (static_cast<double>(blue) + 0.5) /
                static_cast<double>(validation_cell_size)),
            .alpha = static_cast<float>(
                0.1 + 0.8 * static_cast<double>(
                                (red + green + blue) % 17) /
                            16.0),
        };
        validation_inputs.push_back(input);
        validation_cell_indices.push_back(
            icc_transform_lut_cell_index(*lut, input));
      }
    }
  }
  auto random_state = std::uint32_t{0x9e3779b9U};
  auto random_unit = [&]() {
    random_state = random_state * 1664525U + 1013904223U;
    return static_cast<double>(random_state) /
           static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  };
  for (std::size_t index = 0U; index < kRandomValidationSamples; ++index) {
    const auto input = IccRgbaFloat{
        .red = decode_shaper(random_unit()),
        .green = decode_shaper(random_unit()),
        .blue = decode_shaper(random_unit()),
        .alpha = static_cast<float>(random_unit()),
    };
    validation_inputs.push_back(input);
    validation_cell_indices.push_back(
        icc_transform_lut_cell_index(*lut, input));
  }
  auto exact_outputs = validation_inputs;
  if (!transform.apply(exact_outputs)) {
    return reject("icc_transform_lut_validation_apply_failed");
  }
  auto approximate_outputs = validation_inputs;
  for (auto& pixel : approximate_outputs) {
    pixel = sample_icc_transform_lut(*lut, pixel);
  }
  auto exact_lab = exact_outputs;
  auto approximate_lab = approximate_outputs;
#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
  {
    const std::lock_guard lock(transform.impl_->apply_mutex);
    cmsDoTransform(
        transform.impl_->output_to_lab_transform,
        exact_lab.data(),
        exact_lab.data(),
        static_cast<cmsUInt32Number>(exact_lab.size()));
    cmsDoTransform(
        transform.impl_->output_to_lab_transform,
        approximate_lab.data(),
        approximate_lab.data(),
        static_cast<cmsUInt32Number>(approximate_lab.size()));
  }
#endif
  std::vector<double> delta_e_by_sample;
  delta_e_by_sample.reserve(validation_inputs.size());
  std::vector<double> channel_error_by_sample;
  channel_error_by_sample.reserve(validation_inputs.size());
  for (std::size_t index = 0U; index < validation_inputs.size(); ++index) {
    const auto& exact = exact_outputs[index];
    const auto& input = validation_inputs[index];
    if (!std::isfinite(exact.red) || !std::isfinite(exact.green) ||
        !std::isfinite(exact.blue) || !std::isfinite(exact.alpha) ||
        exact.alpha != input.alpha) {
      return reject("icc_transform_lut_validation_output_invalid");
    }
    const auto& approximate = approximate_outputs[index];
    channel_error_by_sample.push_back(std::max({
        std::abs(static_cast<double>(approximate.red) - exact.red),
        std::abs(static_cast<double>(approximate.green) - exact.green),
        std::abs(static_cast<double>(approximate.blue) - exact.blue),
    }));
    const auto& exact_lab_value = exact_lab[index];
    const auto& approximate_lab_value = approximate_lab[index];
    if (!std::isfinite(exact_lab_value.red) ||
        !std::isfinite(exact_lab_value.green) ||
        !std::isfinite(exact_lab_value.blue) ||
        !std::isfinite(approximate_lab_value.red) ||
        !std::isfinite(approximate_lab_value.green) ||
        !std::isfinite(approximate_lab_value.blue)) {
      return reject("icc_transform_lut_lab_validation_output_invalid");
    }
    const auto delta_e = delta_e_2000(
        CieLab{
            .lightness = exact_lab_value.red,
            .a = exact_lab_value.green,
            .b = exact_lab_value.blue,
        },
        CieLab{
            .lightness = approximate_lab_value.red,
            .a = approximate_lab_value.green,
            .b = approximate_lab_value.blue,
        });
    delta_e_by_sample.push_back(delta_e);
    const auto unsafe_threshold = std::min(
        settings.maximum_delta_e_00_limit * 0.5,
        settings.p99_delta_e_00_limit * 0.8);
    if (delta_e > unsafe_threshold) {
      lut->unsafe_cells[validation_cell_indices[index]] = 1U;
    }
  }
  lut->unsafe_cell_count = static_cast<std::size_t>(std::count(
      lut->unsafe_cells.cbegin(),
      lut->unsafe_cells.cend(),
      std::uint8_t{1}));
  std::vector<double> delta_e_errors;
  delta_e_errors.reserve(validation_inputs.size());
  auto maximum_channel_error = 0.0;
  for (std::size_t index = 0U; index < validation_inputs.size(); ++index) {
    if (lut->unsafe_cells[validation_cell_indices[index]] != 0U) {
      continue;
    }
    delta_e_errors.push_back(delta_e_by_sample[index]);
    maximum_channel_error = std::max(
        maximum_channel_error,
        channel_error_by_sample[index]);
  }
  if (delta_e_errors.empty()) {
    return reject("icc_transform_lut_has_no_safe_cells");
  }
  std::sort(delta_e_errors.begin(), delta_e_errors.end());
  lut->validation_sample_count = validation_inputs.size();
  const auto percentile_index = std::min(
      delta_e_errors.size() - 1U,
      static_cast<std::size_t>(
          std::ceil(0.99 * static_cast<double>(delta_e_errors.size()))) -
          1U);
  lut->measured_p99_delta_e_00 = delta_e_errors[percentile_index];
  lut->measured_maximum_delta_e_00 = delta_e_errors.back();
  lut->measured_maximum_channel_error = maximum_channel_error;
  if (!lut->valid()) {
    std::ostringstream reason;
    reason.precision(std::numeric_limits<double>::max_digits10);
    reason << "icc_transform_lut_accuracy_gate_failed:p99_delta_e_00="
           << lut->measured_p99_delta_e_00 << ":max_delta_e_00="
           << lut->measured_maximum_delta_e_00 << ":max_channel="
           << lut->measured_maximum_channel_error << ":unsafe_cells="
           << lut->unsafe_cell_count << '/' << lut->unsafe_cells.size();
    return reject(reason.str());
  }
  if (lut->byte_size() >
      kIccTransformLutCacheMaximumBytes) {
    return reject(
        "icc_transform_lut_exceeds_cache_byte_ceiling");
  }

  const std::scoped_lock lock(cache.mutex);
  const auto raced_match = std::find_if(
      cache.entries.begin(),
      cache.entries.end(),
      [&](const IccTransformLutCacheEntry& entry) {
        return entry.key == key;
      });
  if (raced_match != cache.entries.end()) {
    ++cache.stats.hits;
    raced_match->last_use = ++cache.use_counter;
    cache.stats.last_failure_reason.clear();
    return raced_match->lut;
  }
  const auto resident_bytes = [&]() {
    auto bytes = std::size_t{0U};
    for (const auto& entry : cache.entries) {
      bytes += entry.lut->byte_size();
    }
    return bytes;
  };
  while (!cache.entries.empty() &&
         (cache.entries.size() >=
              kIccTransformLutCacheCapacity ||
          resident_bytes() +
                  lut->byte_size() >
              kIccTransformLutCacheMaximumBytes)) {
    const auto least_recently_used = std::min_element(
        cache.entries.begin(),
        cache.entries.end(),
        [](const IccTransformLutCacheEntry& left,
           const IccTransformLutCacheEntry& right) {
          return left.last_use < right.last_use;
        });
    cache.entries.erase(least_recently_used);
    ++cache.stats.evictions;
  }
  cache.entries.push_back({
      .key = key,
      .lut = lut,
      .last_use = ++cache.use_counter,
  });
  ++cache.stats.builds;
  cache.stats.entries = cache.entries.size();
  cache.stats.bytes = 0U;
  for (const auto& entry : cache.entries) {
    cache.stats.bytes += entry.lut->byte_size();
  }
  cache.stats.last_failure_reason.clear();
  return lut;
}

bool apply_icc_transform_lut(
    const IccTransformLut3D& lut,
    std::span<IccRgbaFloat> pixels) {
  if (!lut.valid()) {
    return false;
  }
  const auto input_valid = std::all_of(
      pixels.begin(),
      pixels.end(),
      [&](const IccRgbaFloat& pixel) {
        return icc_transform_lut_accepts_unchecked(lut, pixel);
      });
  if (!input_valid) {
    return false;
  }
  for (auto& pixel : pixels) {
    pixel = sample_icc_transform_lut(lut, pixel);
  }
  return true;
}

bool apply_icc_transform_lut_hybrid(
    const IccTransformLut3D& lut,
    const CompiledIccTransform& exact_transform,
    std::span<IccRgbaFloat> pixels,
    IccTransformLutApplyStats* stats) {
  if (stats != nullptr) {
    *stats = {};
  }
  if (!lut.valid() || !exact_transform.ready() ||
      lut.transform_cache_key != exact_transform.description().cache_key) {
    return false;
  }
  const auto finite_input = std::all_of(
      pixels.begin(),
      pixels.end(),
      [](const IccRgbaFloat& pixel) {
        return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
               std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
      });
  if (!finite_input) {
    return false;
  }

  std::vector<IccRgbaFloat> exact_pixels;
  std::vector<std::size_t> exact_indices;
  std::vector<std::uint8_t> exact_pixel_mask(pixels.size(), std::uint8_t{0});
  exact_pixels.reserve(pixels.size() / 16U + 1U);
  exact_indices.reserve(exact_pixels.capacity());
  auto lut_pixel_count = std::size_t{0U};
  for (std::size_t index = 0U; index < pixels.size(); ++index) {
    const auto& pixel = pixels[index];
    if (icc_transform_lut_accepts_unchecked(lut, pixel)) {
      ++lut_pixel_count;
    } else {
      exact_pixel_mask[index] = std::uint8_t{1};
      exact_pixels.push_back(pixel);
      exact_indices.push_back(index);
    }
  }
  if (!exact_pixels.empty() && !exact_transform.apply(exact_pixels)) {
    return false;
  }
  for (std::size_t index = 0U; index < exact_pixels.size(); ++index) {
    const auto& transformed = exact_pixels[index];
    const auto source_index = exact_indices[index];
    if (!std::isfinite(transformed.red) || !std::isfinite(transformed.green) ||
        !std::isfinite(transformed.blue) ||
        !std::isfinite(transformed.alpha) ||
        transformed.alpha != pixels[source_index].alpha) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < pixels.size(); ++index) {
    if (exact_pixel_mask[index] == std::uint8_t{0}) {
      pixels[index] = sample_icc_transform_lut(lut, pixels[index]);
    }
  }
  for (std::size_t index = 0U; index < exact_pixels.size(); ++index) {
    pixels[exact_indices[index]] = exact_pixels[index];
  }
  if (stats != nullptr) {
    stats->lut_pixels = lut_pixel_count;
    stats->exact_fallback_pixels = exact_pixels.size();
  }
  return true;
}

IccTransformLutCacheStats icc_transform_lut_cache_stats() {
  auto& cache = icc_transform_lut_cache();
  const std::scoped_lock lock(cache.mutex);
  auto stats = cache.stats;
  stats.entries = cache.entries.size();
  stats.bytes = 0U;
  for (const auto& entry : cache.entries) {
    stats.bytes += entry.lut->byte_size();
  }
  stats.capacity = kIccTransformLutCacheCapacity;
  stats.maximum_bytes =
      kIccTransformLutCacheMaximumBytes;
  return stats;
}

void clear_icc_transform_lut_cache_for_testing() {
  auto& cache = icc_transform_lut_cache();
  const std::scoped_lock lock(cache.build_mutex, cache.mutex);
  cache.entries.clear();
  cache.stats = {};
  cache.use_counter = 0U;
}

void clear_icc_transform_cache() {
  {
    auto& cache = icc_transform_cache();
    const std::lock_guard lock(cache.mutex);
    cache.entries.clear();
    cache.hits = 0;
    cache.misses = 0;
    cache.evictions = 0;
  }
  clear_icc_transform_lut_cache_for_testing();
}

IccBackendCapability icc_backend_capability() {
#if defined(PHOTO_EDITOR_HAS_LCMS2) && PHOTO_EDITOR_HAS_LCMS2
  return {
      .available = true,
      .strict_version = PHOTO_EDITOR_LCMS_STRICT_VERSION != 0,
      .backend = "Little CMS",
      .compiled_version = PHOTO_EDITOR_LCMS_COMPILED_VERSION,
      .runtime_version = std::to_string(cmsGetEncodedCMMversion()),
      .validated_version = PHOTO_EDITOR_LCMS_VALIDATED_VERSION,
  };
#else
  return {
      .available = false,
      .backend = "none",
      .failure_reason = "little_cms_not_compiled",
  };
#endif
}

}  // namespace photo_editor::pipeline

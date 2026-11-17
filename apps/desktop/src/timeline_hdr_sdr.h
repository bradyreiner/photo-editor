#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <QColorSpace>
#include <QImage>
#include <qrgba64.h>

namespace photo_editor::desktop::timeline_hdr_sdr {

inline constexpr double kSdrReferenceWhiteNits = 100.0;
inline constexpr double kHlgReferencePeakNits = 1000.0;
inline constexpr double kPqReferencePeakNits = 10000.0;

[[nodiscard]] inline double source_peak_in_reference_whites(bool hlg) {
  return (hlg ? kHlgReferencePeakNits : kPqReferencePeakNits) /
         kSdrReferenceWhiteNits;
}

[[nodiscard]] inline double hable_curve(double value) {
  constexpr auto a = 0.15;
  constexpr auto b = 0.50;
  constexpr auto c = 0.10;
  constexpr auto d = 0.20;
  constexpr auto e = 0.02;
  constexpr auto f = 0.30;
  return (value * (value * a + b * c) + d * e) /
             (value * (value * a + b) + d * f) -
         e / f;
}

[[nodiscard]] inline double pq_eotf_nits(double encoded) {
  constexpr auto m1 = 2610.0 / 16384.0;
  constexpr auto m2 = 2523.0 / 32.0;
  constexpr auto c1 = 3424.0 / 4096.0;
  constexpr auto c2 = 2413.0 / 128.0;
  constexpr auto c3 = 2392.0 / 128.0;
  const auto signal = std::clamp(encoded, 0.0, 1.0);
  const auto powered = std::pow(signal, 1.0 / m2);
  const auto numerator = std::max(powered - c1, 0.0);
  const auto denominator = std::max(c2 - c3 * powered, 1.0e-12);
  return 10000.0 * std::pow(numerator / denominator, 1.0 / m1);
}

[[nodiscard]] inline double hlg_inverse_oetf(double encoded) {
  constexpr auto a = 0.17883277;
  constexpr auto b = 1.0 - 4.0 * a;
  const auto c = 0.5 - a * std::log(4.0 * a);
  const auto signal = std::clamp(encoded, 0.0, 1.0);
  const auto scene_linear = signal <= 0.5
                                ? signal * signal / 3.0
                                : (std::exp((signal - c) / a) + b) / 12.0;
  return scene_linear;
}

// The preview and FFmpeg export paths use the same fixed SDR transform:
// decode PQ/HLG to linear light, convert BT.2020 primaries to BT.709, apply
// Hable with a 100-nit reference white, then encode the BT.709 transfer curve.
[[nodiscard]] inline std::array<double, 3> to_rec709_sdr(
    const std::array<double, 3>& encoded_rgb,
    bool hlg,
    bool bt2020_primaries) {
  auto linear = std::array<double, 3>{};
  if (hlg) {
    for (std::size_t channel = 0; channel < linear.size(); ++channel) {
      linear[channel] = hlg_inverse_oetf(encoded_rgb[channel]);
    }
    // BT.2100 HLG applies the display OOTF to scene RGB as a luminance-scaled
    // triplet. Applying gamma per channel changes saturated colors and does
    // not match the HLG signal used by zscale for delivery.
    const auto scene_luminance = bt2020_primaries
        ? 0.2627 * linear[0] + 0.6780 * linear[1] + 0.0593 * linear[2]
        : 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2];
    const auto ootf_scale = kHlgReferencePeakNits *
        std::pow(std::max(0.0, scene_luminance), 0.2) /
        kSdrReferenceWhiteNits;
    for (auto& channel : linear) {
      channel *= ootf_scale;
    }
  } else {
    for (std::size_t channel = 0; channel < linear.size(); ++channel) {
      linear[channel] = pq_eotf_nits(encoded_rgb[channel]) /
                        kSdrReferenceWhiteNits;
    }
  }

  if (bt2020_primaries) {
    const auto source = linear;
    linear = {
        1.660491 * source[0] - 0.587641 * source[1] - 0.072850 * source[2],
        -0.124550 * source[0] + 1.132900 * source[1] - 0.008349 * source[2],
        -0.018151 * source[0] - 0.100579 * source[1] + 1.118730 * source[2],
    };
  }

  const auto peak = source_peak_in_reference_whites(hlg);
  const auto normalized_hable_peak = hable_curve(peak);
  const auto encode_rec709 = [](double value) {
    const auto linear_value = std::max(0.0, value);
    const auto encoded = linear_value <= 0.018
                             ? 4.5 * linear_value
                             : 1.099 * std::pow(linear_value, 0.45) - 0.099;
    return std::clamp(encoded, 0.0, 1.0);
  };
  for (auto& channel : linear) {
    channel = encode_rec709(
        hable_curve(std::max(0.0, channel)) / normalized_hable_peak);
  }
  return linear;
}

[[nodiscard]] inline bool apply_to_rec709_sdr(
    QImage* image,
    bool hlg,
    bool bt2020_primaries,
    const QColorSpace& output_color_space) {
  if (image == nullptr || image->isNull()) {
    return false;
  }
  auto converted = image->convertToFormat(QImage::Format_RGBA64);
  if (converted.isNull()) {
    return false;
  }
  constexpr auto maximum = 65535.0;
  const auto encode_channel = [](double value) {
    return static_cast<std::uint16_t>(
        std::lround(std::clamp(value, 0.0, 1.0) * 65535.0));
  };
  for (int y = 0; y < converted.height(); ++y) {
    auto* row = reinterpret_cast<QRgba64*>(converted.scanLine(y));
    for (int x = 0; x < converted.width(); ++x) {
      const auto pixel = row[x];
      const auto mapped = to_rec709_sdr(
          {static_cast<double>(pixel.red()) / maximum,
           static_cast<double>(pixel.green()) / maximum,
           static_cast<double>(pixel.blue()) / maximum},
          hlg,
          bt2020_primaries);
      row[x] = QRgba64::fromRgba64(
          encode_channel(mapped[0]), encode_channel(mapped[1]),
          encode_channel(mapped[2]), pixel.alpha());
    }
  }
  converted.setColorSpace(output_color_space);
  for (const auto& key : image->textKeys()) {
    converted.setText(key, image->text(key));
  }
  *image = std::move(converted);
  return true;
}

}  // namespace photo_editor::desktop::timeline_hdr_sdr

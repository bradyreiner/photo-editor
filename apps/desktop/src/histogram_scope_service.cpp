#include "histogram_scope_service.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

#include <QRgb>
#include <QVariantMap>

namespace photo_editor::desktop {

QString histogram_scope_service_contract() {
  return QString::fromLatin1(kHistogramScopeServiceContract);
}

HistogramComputationResult compute_histogram_scope_data(const QImage& image) {
  constexpr int kDisplayBins = 256;
  constexpr int kInternalBins = 4096;
  static_assert(kInternalBins % kDisplayBins == 0);
  constexpr int kScopeColumns = 128;
  constexpr int kScopeLevels = 64;
  constexpr int kVectorscopeSampleTarget = 1800;
  HistogramComputationResult result;
  struct HistogramScratch final {
    std::array<int, kInternalBins> red_bins{};
    std::array<int, kInternalBins> green_bins{};
    std::array<int, kInternalBins> blue_bins{};
    std::array<int, kInternalBins> luma_bins{};
    std::array<int, kDisplayBins> red_display_counts{};
    std::array<int, kDisplayBins> green_display_counts{};
    std::array<int, kDisplayBins> blue_display_counts{};
    std::array<int, kDisplayBins> luma_display_counts{};
    std::array<int, kScopeColumns * kScopeLevels> red_scope_density{};
    std::array<int, kScopeColumns * kScopeLevels> green_scope_density{};
    std::array<int, kScopeColumns * kScopeLevels> blue_scope_density{};
    std::array<int, kScopeColumns * kScopeLevels> luma_scope_density{};
    std::array<double, kScopeColumns> red_scope_sum{};
    std::array<double, kScopeColumns> green_scope_sum{};
    std::array<double, kScopeColumns> blue_scope_sum{};
    std::array<double, kScopeColumns> luma_scope_sum{};
    std::array<double, kScopeColumns> red_scope_min{};
    std::array<double, kScopeColumns> green_scope_min{};
    std::array<double, kScopeColumns> blue_scope_min{};
    std::array<double, kScopeColumns> luma_scope_min{};
    std::array<double, kScopeColumns> red_scope_max{};
    std::array<double, kScopeColumns> green_scope_max{};
    std::array<double, kScopeColumns> blue_scope_max{};
    std::array<double, kScopeColumns> luma_scope_max{};
    std::array<int, kScopeColumns> scope_counts{};
  };
  const auto scratch = std::make_unique<HistogramScratch>();
  auto& red_bins = scratch->red_bins;
  auto& green_bins = scratch->green_bins;
  auto& blue_bins = scratch->blue_bins;
  auto& luma_bins = scratch->luma_bins;
  auto& red_display_counts = scratch->red_display_counts;
  auto& green_display_counts = scratch->green_display_counts;
  auto& blue_display_counts = scratch->blue_display_counts;
  auto& luma_display_counts = scratch->luma_display_counts;
  auto& red_scope_density = scratch->red_scope_density;
  auto& green_scope_density = scratch->green_scope_density;
  auto& blue_scope_density = scratch->blue_scope_density;
  auto& luma_scope_density = scratch->luma_scope_density;
  auto& red_scope_sum = scratch->red_scope_sum;
  auto& green_scope_sum = scratch->green_scope_sum;
  auto& blue_scope_sum = scratch->blue_scope_sum;
  auto& luma_scope_sum = scratch->luma_scope_sum;
  auto& red_scope_min = scratch->red_scope_min;
  auto& green_scope_min = scratch->green_scope_min;
  auto& blue_scope_min = scratch->blue_scope_min;
  auto& luma_scope_min = scratch->luma_scope_min;
  auto& red_scope_max = scratch->red_scope_max;
  auto& green_scope_max = scratch->green_scope_max;
  auto& blue_scope_max = scratch->blue_scope_max;
  auto& luma_scope_max = scratch->luma_scope_max;
  auto& scope_counts = scratch->scope_counts;
  QVariantList vectorscope_points;

  red_scope_min.fill(1.0);
  green_scope_min.fill(1.0);
  blue_scope_min.fill(1.0);
  luma_scope_min.fill(1.0);

  const auto width = image.width();
  const auto height = image.height();
  result.sample_count = std::max(0, width * height);
  const auto sample_stride = std::max(
      1,
      static_cast<int>(std::sqrt(
          static_cast<double>(std::max(1, width * height)) /
          static_cast<double>(kVectorscopeSampleTarget))));
  const auto density_slot = [](int column, double value) {
    const auto level = std::clamp(
        static_cast<int>(value * static_cast<double>(kScopeLevels - 1) + 0.5),
        0,
        kScopeLevels - 1);
    return static_cast<std::size_t>(column * kScopeLevels + level);
  };
  const auto internal_bin_for_value = [](double value) {
    return std::clamp(
        static_cast<int>(
            std::clamp(value, 0.0, 1.0) * static_cast<double>(kInternalBins - 1) +
            0.5),
        0,
        kInternalBins - 1);
  };
  const auto display_bin_for_internal_bin = [](int internal_bin) {
    return std::clamp(
        internal_bin / (kInternalBins / kDisplayBins),
        0,
        kDisplayBins - 1);
  };
  const auto direct_argb =
      image.format() == QImage::Format_ARGB32 ||
      image.format() == QImage::Format_ARGB32_Premultiplied ||
      image.format() == QImage::Format_RGB32;
  const auto premultiplied_argb = image.format() == QImage::Format_ARGB32_Premultiplied;
  const auto record_sample = [&](int x, int y, double red, double green, double blue, double luma) {
    red = std::clamp(red, 0.0, 1.0);
    green = std::clamp(green, 0.0, 1.0);
    blue = std::clamp(blue, 0.0, 1.0);
    luma = std::clamp(luma, 0.0, 1.0);
    if (red <= 0.0 || green <= 0.0 || blue <= 0.0) {
      ++result.shadow_clip_count;
    }
    if (red >= 1.0 || green >= 1.0 || blue >= 1.0) {
      ++result.highlight_clip_count;
    }

    ++red_bins[static_cast<std::size_t>(internal_bin_for_value(red))];
    ++green_bins[static_cast<std::size_t>(internal_bin_for_value(green))];
    ++blue_bins[static_cast<std::size_t>(internal_bin_for_value(blue))];
    ++luma_bins[static_cast<std::size_t>(internal_bin_for_value(luma))];

    const auto scope_index = std::clamp(
        static_cast<int>(
            (static_cast<double>(x) / static_cast<double>(std::max(1, width))) *
            static_cast<double>(kScopeColumns)),
        0,
        kScopeColumns - 1);
    const auto scope_slot = static_cast<std::size_t>(scope_index);
    ++red_scope_density[density_slot(scope_index, red)];
    ++green_scope_density[density_slot(scope_index, green)];
    ++blue_scope_density[density_slot(scope_index, blue)];
    ++luma_scope_density[density_slot(scope_index, luma)];
    red_scope_sum[scope_slot] += red;
    green_scope_sum[scope_slot] += green;
    blue_scope_sum[scope_slot] += blue;
    luma_scope_sum[scope_slot] += luma;
    red_scope_min[scope_slot] = std::min(red_scope_min[scope_slot], red);
    green_scope_min[scope_slot] = std::min(green_scope_min[scope_slot], green);
    blue_scope_min[scope_slot] = std::min(blue_scope_min[scope_slot], blue);
    luma_scope_min[scope_slot] = std::min(luma_scope_min[scope_slot], luma);
    red_scope_max[scope_slot] = std::max(red_scope_max[scope_slot], red);
    green_scope_max[scope_slot] = std::max(green_scope_max[scope_slot], green);
    blue_scope_max[scope_slot] = std::max(blue_scope_max[scope_slot], blue);
    luma_scope_max[scope_slot] = std::max(luma_scope_max[scope_slot], luma);
    ++scope_counts[scope_slot];

    if ((x % sample_stride) == 0 && (y % sample_stride) == 0 &&
        vectorscope_points.size() < kVectorscopeSampleTarget) {
      const auto y_value = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
      const auto cb = (blue - y_value) * 0.565;
      const auto cr = (red - y_value) * 0.713;
      QVariantMap point;
      point.insert(QStringLiteral("x"), std::clamp(0.5 + cb, 0.0, 1.0));
      point.insert(QStringLiteral("y"), std::clamp(0.5 - cr, 0.0, 1.0));
      point.insert(QStringLiteral("luma"), std::clamp(y_value, 0.0, 1.0));
      point.insert(QStringLiteral("red"), red);
      point.insert(QStringLiteral("green"), green);
      point.insert(QStringLiteral("blue"), blue);
      vectorscope_points.append(point);
    }
  };
  for (int y = 0; y < height; ++y) {
    if (direct_argb) {
      const auto* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
      for (int x = 0; x < width; ++x) {
        // Scopes measure straight color, just like pixelColor() in the
        // high-precision path. Alpha encoding must not darken the samples.
        const auto pixel = premultiplied_argb ? qUnpremultiply(scanline[x]) : scanline[x];
        const auto red = qRed(pixel);
        const auto green = qGreen(pixel);
        const auto blue = qBlue(pixel);
        const auto luma = static_cast<int>(
            0.2126 * red + 0.7152 * green + 0.0722 * blue + 0.5);
        record_sample(
            x,
            y,
            red / 255.0,
            green / 255.0,
            blue / 255.0,
            std::clamp(luma, 0, 255) / 255.0);
      }
    } else {
      for (int x = 0; x < width; ++x) {
        const auto color = image.pixelColor(x, y);
        const auto red = static_cast<double>(color.redF());
        const auto green = static_cast<double>(color.greenF());
        const auto blue = static_cast<double>(color.blueF());
        record_sample(
            x,
            y,
            red,
            green,
            blue,
            0.2126 * red + 0.7152 * green + 0.0722 * blue);
      }
    }
  }

  for (int index = 0; index < kInternalBins; ++index) {
    const auto display_slot =
        static_cast<std::size_t>(display_bin_for_internal_bin(index));
    const auto internal_slot = static_cast<std::size_t>(index);
    red_display_counts[display_slot] += red_bins[internal_slot];
    green_display_counts[display_slot] += green_bins[internal_slot];
    blue_display_counts[display_slot] += blue_bins[internal_slot];
    luma_display_counts[display_slot] += luma_bins[internal_slot];
  }

  const auto smoothed_display_count = [](const auto& counts, int index) {
    const auto slot = static_cast<std::size_t>(index);
    const auto center = static_cast<double>(counts[slot]) * 0.50;
    const auto left =
        index > 0
            ? static_cast<double>(counts[static_cast<std::size_t>(index - 1)]) * 0.25
            : 0.0;
    const auto right =
        index + 1 < kDisplayBins
            ? static_cast<double>(counts[static_cast<std::size_t>(index + 1)]) * 0.25
            : 0.0;
    return center + left + right;
  };
  const auto max_smoothed_count = [&](const auto& counts) {
    auto maximum = 0.0;
    for (int index = 0; index < kDisplayBins; ++index) {
      maximum = std::max(maximum, smoothed_display_count(counts, index));
    }
    return maximum;
  };
  const auto max_count = std::max({
      max_smoothed_count(red_display_counts),
      max_smoothed_count(green_display_counts),
      max_smoothed_count(blue_display_counts),
      max_smoothed_count(luma_display_counts),
  });
  if (max_count <= 0.0) {
    return result;
  }

  const auto scale = 1.0 / max_count;
  QVariantList red_values, green_values, blue_values, luminance_values;
  QVariantList red_counts, green_counts, blue_counts, luminance_counts;
  QVariantList waveform_red, waveform_green, waveform_blue, waveform_luminance;
  QVariantList density_red, density_green, density_blue, density_luminance;
  red_values.reserve(kDisplayBins);
  green_values.reserve(kDisplayBins);
  blue_values.reserve(kDisplayBins);
  luminance_values.reserve(kDisplayBins);
  red_counts.reserve(kDisplayBins);
  green_counts.reserve(kDisplayBins);
  blue_counts.reserve(kDisplayBins);
  luminance_counts.reserve(kDisplayBins);
  waveform_red.reserve(kScopeColumns);
  waveform_green.reserve(kScopeColumns);
  waveform_blue.reserve(kScopeColumns);
  waveform_luminance.reserve(kScopeColumns);
  density_red.reserve(kScopeColumns * kScopeLevels);
  density_green.reserve(kScopeColumns * kScopeLevels);
  density_blue.reserve(kScopeColumns * kScopeLevels);
  density_luminance.reserve(kScopeColumns * kScopeLevels);
  for (int index = 0; index < kDisplayBins; ++index) {
    const auto slot = static_cast<std::size_t>(index);
    red_values.append(smoothed_display_count(red_display_counts, index) * scale);
    green_values.append(smoothed_display_count(green_display_counts, index) * scale);
    blue_values.append(smoothed_display_count(blue_display_counts, index) * scale);
    luminance_values.append(smoothed_display_count(luma_display_counts, index) * scale);
    red_counts.append(red_display_counts[slot]);
    green_counts.append(green_display_counts[slot]);
    blue_counts.append(blue_display_counts[slot]);
    luminance_counts.append(luma_display_counts[slot]);
  }
  for (int index = 0; index < kScopeColumns; ++index) {
    const auto slot = static_cast<std::size_t>(index);
    const auto count = scope_counts[slot];
    const auto scope_sample = [count](double min_value, double max_value, double sum_value) {
      QVariantMap sample;
      if (count <= 0) {
        sample.insert(QStringLiteral("min"), 0.0);
        sample.insert(QStringLiteral("max"), 0.0);
        sample.insert(QStringLiteral("avg"), 0.0);
        return sample;
      }
      sample.insert(QStringLiteral("min"), std::clamp(min_value, 0.0, 1.0));
      sample.insert(QStringLiteral("max"), std::clamp(max_value, 0.0, 1.0));
      sample.insert(
          QStringLiteral("avg"),
          std::clamp(sum_value / static_cast<double>(count), 0.0, 1.0));
      return sample;
    };
    waveform_red.append(
        scope_sample(red_scope_min[slot], red_scope_max[slot], red_scope_sum[slot]));
    waveform_green.append(
        scope_sample(green_scope_min[slot], green_scope_max[slot], green_scope_sum[slot]));
    waveform_blue.append(
        scope_sample(blue_scope_min[slot], blue_scope_max[slot], blue_scope_sum[slot]));
    waveform_luminance.append(
        scope_sample(luma_scope_min[slot], luma_scope_max[slot], luma_scope_sum[slot]));
  }
  const auto max_density = std::max({
      *std::max_element(red_scope_density.begin(), red_scope_density.end()),
      *std::max_element(green_scope_density.begin(), green_scope_density.end()),
      *std::max_element(blue_scope_density.begin(), blue_scope_density.end()),
      *std::max_element(luma_scope_density.begin(), luma_scope_density.end()),
      1,
  });
  const auto density_scale = 1.0 / std::log1p(static_cast<double>(max_density));
  const auto append_density = [density_scale](const auto& source, QVariantList* target) {
    for (const auto count : source) {
      target->append(std::log1p(static_cast<double>(count)) * density_scale);
    }
  };
  append_density(red_scope_density, &density_red);
  append_density(green_scope_density, &density_green);
  append_density(blue_scope_density, &density_blue);
  append_density(luma_scope_density, &density_luminance);

  result.red = std::move(red_values);
  result.green = std::move(green_values);
  result.blue = std::move(blue_values);
  result.luminance = std::move(luminance_values);
  result.red_counts = std::move(red_counts);
  result.green_counts = std::move(green_counts);
  result.blue_counts = std::move(blue_counts);
  result.luminance_counts = std::move(luminance_counts);
  result.waveform_red = std::move(waveform_red);
  result.waveform_green = std::move(waveform_green);
  result.waveform_blue = std::move(waveform_blue);
  result.waveform_luminance = std::move(waveform_luminance);
  result.density_red = std::move(density_red);
  result.density_green = std::move(density_green);
  result.density_blue = std::move(density_blue);
  result.density_luminance = std::move(density_luminance);
  result.vectorscope_points = std::move(vectorscope_points);
  return result;
}

}  // namespace photo_editor::desktop

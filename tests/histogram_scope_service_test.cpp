#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <utility>

#include <QColor>
#include <QImage>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "histogram_scope_service.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

int count_sum(const QVariantList& values) {
  return std::accumulate(
      values.cbegin(),
      values.cend(),
      0,
      [](int total, const QVariant& value) { return total + value.toInt(); });
}

bool normalized_values(const QVariantList& values) {
  return std::all_of(values.cbegin(), values.cend(), [](const QVariant& value) {
    const auto sample = value.toDouble();
    return sample >= 0.0 && sample <= 1.0;
  });
}

}  // namespace

int main() {
  using photo_editor::desktop::compute_histogram_scope_data;

  if (photo_editor::desktop::histogram_scope_service_contract() !=
      QStringLiteral(
          "histogram-scope-service:v1|histogram:4096-to-256|waveform:128x64|vectorscope:max-1800")) {
    return fail("histogram scope service should expose its stable compute contract");
  }

  const auto empty = compute_histogram_scope_data(QImage{});
  if (empty.sample_count != 0 || !empty.red.isEmpty() ||
      !empty.waveform_luminance.isEmpty() || !empty.vectorscope_points.isEmpty()) {
    return fail("empty input should produce an empty scope result");
  }

  QImage image(4, 1, QImage::Format_ARGB32);
  image.setPixelColor(0, 0, QColor(0, 0, 0));
  image.setPixelColor(1, 0, QColor(255, 255, 255));
  image.setPixelColor(2, 0, QColor(255, 0, 0));
  image.setPixelColor(3, 0, QColor(0, 255, 0));
  const auto result = compute_histogram_scope_data(image);

  if (result.sample_count != 4 || result.shadow_clip_count != 3 ||
      result.highlight_clip_count != 3 || result.red.size() != 256 ||
      result.green.size() != 256 || result.blue.size() != 256 ||
      result.luminance.size() != 256 || result.red_counts.size() != 256 ||
      result.waveform_red.size() != 128 || result.waveform_green.size() != 128 ||
      result.waveform_blue.size() != 128 || result.waveform_luminance.size() != 128 ||
      result.density_red.size() != 128 * 64 ||
      result.density_green.size() != 128 * 64 ||
      result.density_blue.size() != 128 * 64 ||
      result.density_luminance.size() != 128 * 64 ||
      result.vectorscope_points.size() != 4) {
    return fail("histogram scope output should retain its fixed public shapes and clipping counts");
  }
  if (count_sum(result.red_counts) != 4 || count_sum(result.green_counts) != 4 ||
      count_sum(result.blue_counts) != 4 || count_sum(result.luminance_counts) != 4 ||
      result.red_counts.at(0).toInt() != 2 || result.red_counts.at(255).toInt() != 2 ||
      result.green_counts.at(0).toInt() != 2 || result.green_counts.at(255).toInt() != 2 ||
      result.blue_counts.at(0).toInt() != 3 || result.blue_counts.at(255).toInt() != 1 ||
      result.luminance_counts.at(0).toInt() != 1 ||
      result.luminance_counts.at(54).toInt() != 1 ||
      result.luminance_counts.at(182).toInt() != 1 ||
      result.luminance_counts.at(255).toInt() != 1) {
    return fail("histogram count bins should preserve every input pixel without quantization loss");
  }
  if (!normalized_values(result.red) || !normalized_values(result.green) ||
      !normalized_values(result.blue) || !normalized_values(result.luminance) ||
      !normalized_values(result.density_red) ||
      !normalized_values(result.density_green) ||
      !normalized_values(result.density_blue) ||
      !normalized_values(result.density_luminance)) {
    return fail("normalized histogram and density outputs should stay in display range");
  }

  const auto black_waveform = result.waveform_luminance.at(0).toMap();
  const auto white_waveform = result.waveform_luminance.at(32).toMap();
  if (black_waveform.value(QStringLiteral("min")).toDouble() != 0.0 ||
      black_waveform.value(QStringLiteral("max")).toDouble() != 0.0 ||
      white_waveform.value(QStringLiteral("min")).toDouble() != 1.0 ||
      white_waveform.value(QStringLiteral("max")).toDouble() != 1.0) {
    return fail("waveform columns should preserve the source horizontal and luminance coordinates");
  }
  for (const auto& point_value : result.vectorscope_points) {
    const auto point = point_value.toMap();
    if (point.value(QStringLiteral("x")).toDouble() < 0.0 ||
        point.value(QStringLiteral("x")).toDouble() > 1.0 ||
        point.value(QStringLiteral("y")).toDouble() < 0.0 ||
        point.value(QStringLiteral("y")).toDouble() > 1.0 ||
        point.value(QStringLiteral("luma")).toDouble() < 0.0 ||
        point.value(QStringLiteral("luma")).toDouble() > 1.0) {
      return fail("vectorscope samples should remain normalized");
    }
  }

  QImage rgba64(2, 1, QImage::Format_RGBA64);
  rgba64.setPixelColor(0, 0, QColor(12, 34, 56));
  rgba64.setPixelColor(1, 0, QColor(210, 180, 90));
  const auto rgba64_result = compute_histogram_scope_data(rgba64);
  if (rgba64_result.sample_count != 2 || count_sum(rgba64_result.red_counts) != 2 ||
      count_sum(rgba64_result.green_counts) != 2 ||
      count_sum(rgba64_result.blue_counts) != 2 ||
      rgba64_result.vectorscope_points.size() != 2) {
    return fail("non-ARGB images should use the color-safe sampling path without dropping pixels");
  }

  // Identical straight colors must not darken when their storage format uses
  // premultiplied alpha. Use primaries so alpha round-trip quantization cannot
  // explain away a mismatch in bins, clipping, waveform, density or vectors.
  for (const auto formats : {
           std::pair{QImage::Format_ARGB32, QImage::Format_ARGB32_Premultiplied},
           std::pair{QImage::Format_RGBA64, QImage::Format_RGBA64_Premultiplied},
           std::pair{QImage::Format_RGBA16FPx4, QImage::Format_RGBA16FPx4_Premultiplied},
           std::pair{QImage::Format_RGBA32FPx4, QImage::Format_RGBA32FPx4_Premultiplied}}) {
    QImage straight(4, 1, formats.first);
    straight.setPixelColor(0, 0, QColor(255, 0, 0, 64));
    straight.setPixelColor(1, 0, QColor(0, 255, 0, 128));
    straight.setPixelColor(2, 0, QColor(0, 0, 255, 192));
    straight.setPixelColor(3, 0, QColor(255, 255, 255, 255));
    const auto expected = compute_histogram_scope_data(straight);
    const auto premultiplied = compute_histogram_scope_data(straight.convertToFormat(formats.second));
    if (expected.red_counts != premultiplied.red_counts ||
        expected.green_counts != premultiplied.green_counts ||
        expected.blue_counts != premultiplied.blue_counts ||
        expected.luminance_counts != premultiplied.luminance_counts ||
        expected.waveform_red != premultiplied.waveform_red ||
        expected.waveform_green != premultiplied.waveform_green ||
        expected.waveform_blue != premultiplied.waveform_blue ||
        expected.waveform_luminance != premultiplied.waveform_luminance ||
        expected.density_red != premultiplied.density_red ||
        expected.density_green != premultiplied.density_green ||
        expected.density_blue != premultiplied.density_blue ||
        expected.density_luminance != premultiplied.density_luminance ||
        expected.vectorscope_points != premultiplied.vectorscope_points ||
        expected.shadow_clip_count != premultiplied.shadow_clip_count ||
        expected.highlight_clip_count != premultiplied.highlight_clip_count) {
      std::cerr << "straight format=" << formats.first << " premultiplied=" << formats.second << '\n';
      return fail("scope color must not depend on alpha storage representation");
    }
  }
  return EXIT_SUCCESS;
}

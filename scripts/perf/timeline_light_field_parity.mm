#include "timeline_desktop_light_field.h"
#include "timeline_desktop_light_field_metal.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSize>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

using photo_editor::desktop::timeline_motion::DesktopLightFieldParameters;

struct PixelMetrics final {
  double rgb_mae = 0.0;
  double rgb_p95 = 0.0;
  int rgb_max = 0;
  quint64 rgb_differing_channels = 0;
  double alpha_mae = 0.0;
  double alpha_p95 = 0.0;
  int alpha_max = 0;
  double spatial_16_mae = 0.0;
  int edge_position_max_delta_pixels = 0;
  quint64 edge_rows_compared = 0;
};

PixelMetrics compare_images(const QImage& reference, const QImage& actual) {
  PixelMetrics metrics;
  std::array<quint64, 256> histogram{};
  std::array<quint64, 256> alpha_histogram{};
  quint64 rgb_sum = 0;
  quint64 rgb_count = 0;
  quint64 alpha_sum = 0;
  quint64 alpha_count = 0;
  constexpr int block_size = 16;
  double spatial_sum = 0.0;
  quint64 spatial_count = 0;
  const auto width = reference.width();
  const auto height = reference.height();
  for (int y = 0; y < height; ++y) {
    const auto* reference_row = reference.constScanLine(y);
    const auto* actual_row = actual.constScanLine(y);
    for (int x = 0; x < width; ++x) {
      const auto pixel_offset = x * 4;
      for (int channel = 0; channel < 3; ++channel) {
        const auto difference = std::abs(
            static_cast<int>(reference_row[pixel_offset + channel]) -
            static_cast<int>(actual_row[pixel_offset + channel]));
        ++histogram[static_cast<size_t>(difference)];
        rgb_sum += static_cast<quint64>(difference);
        ++rgb_count;
        metrics.rgb_max = std::max(metrics.rgb_max, difference);
        if (difference != 0) {
          ++metrics.rgb_differing_channels;
        }
      }
      const auto alpha_difference = std::abs(
          static_cast<int>(reference_row[pixel_offset + 3]) -
          static_cast<int>(actual_row[pixel_offset + 3]));
      alpha_sum += static_cast<quint64>(alpha_difference);
      ++alpha_count;
      ++alpha_histogram[static_cast<size_t>(alpha_difference)];
      metrics.alpha_max = std::max(metrics.alpha_max, alpha_difference);
    }
    int reference_first = -1;
    int actual_first = -1;
    int reference_last = -1;
    int actual_last = -1;
    for (int x = 0; x < width; ++x) {
      if (reference_row[x * 4 + 3] >= 128) {
        if (reference_first < 0) reference_first = x;
        reference_last = x;
      }
      if (actual_row[x * 4 + 3] >= 128) {
        if (actual_first < 0) actual_first = x;
        actual_last = x;
      }
    }
    if (reference_first >= 0 && actual_first >= 0) {
      metrics.edge_position_max_delta_pixels = std::max(
          metrics.edge_position_max_delta_pixels,
          std::max(std::abs(reference_first - actual_first),
                   std::abs(reference_last - actual_last)));
      ++metrics.edge_rows_compared;
    } else if (reference_first != actual_first) {
      metrics.edge_position_max_delta_pixels = width;
    }
  }
  metrics.rgb_mae = rgb_count == 0 ? 0.0 : static_cast<double>(rgb_sum) / rgb_count;
  metrics.alpha_mae = alpha_count == 0 ? 0.0 : static_cast<double>(alpha_sum) / alpha_count;
  const auto p95_threshold = static_cast<quint64>(std::ceil(rgb_count * 0.95));
  quint64 accumulated = 0;
  for (int difference = 0; difference < static_cast<int>(histogram.size()); ++difference) {
    accumulated += histogram[static_cast<size_t>(difference)];
    if (accumulated >= p95_threshold) {
      metrics.rgb_p95 = difference;
      break;
    }
  }
  const auto alpha_p95_threshold = static_cast<quint64>(std::ceil(alpha_count * 0.95));
  accumulated = 0;
  for (int difference = 0; difference < static_cast<int>(alpha_histogram.size()); ++difference) {
    accumulated += alpha_histogram[static_cast<size_t>(difference)];
    if (accumulated >= alpha_p95_threshold) {
      metrics.alpha_p95 = difference;
      break;
    }
  }
  for (int y_start = 0; y_start < height; y_start += block_size) {
    for (int x_start = 0; x_start < width; x_start += block_size) {
      const auto y_end = std::min(height, y_start + block_size);
      const auto x_end = std::min(width, x_start + block_size);
      for (int channel = 0; channel < 3; ++channel) {
        double reference_sum = 0.0;
        double actual_sum = 0.0;
        int pixels = 0;
        for (int y = y_start; y < y_end; ++y) {
          const auto* reference_row = reference.constScanLine(y);
          const auto* actual_row = actual.constScanLine(y);
          for (int x = x_start; x < x_end; ++x) {
            const auto pixel_offset = x * 4 + channel;
            reference_sum += reference_row[pixel_offset];
            actual_sum += actual_row[pixel_offset];
            ++pixels;
          }
        }
        if (pixels > 0) {
          spatial_sum += std::abs(reference_sum / pixels - actual_sum / pixels);
          ++spatial_count;
        }
      }
    }
  }
  metrics.spatial_16_mae = spatial_count == 0 ? 0.0 : spatial_sum / spatial_count;
  return metrics;
}

QJsonObject to_json(const PixelMetrics& metrics) {
  return {
      {QStringLiteral("rgbMae"), metrics.rgb_mae},
      {QStringLiteral("rgbP95AbsoluteDifference"), metrics.rgb_p95},
      {QStringLiteral("rgbMaxAbsoluteDifference"), metrics.rgb_max},
      {QStringLiteral("rgbDifferingChannelCount"),
       static_cast<double>(metrics.rgb_differing_channels)},
      {QStringLiteral("alphaMae"), metrics.alpha_mae},
      {QStringLiteral("alphaP95AbsoluteDifference"), metrics.alpha_p95},
      {QStringLiteral("alphaMaxAbsoluteDifference"), metrics.alpha_max},
      {QStringLiteral("edgePositionMaxDeltaPixels"), metrics.edge_position_max_delta_pixels},
      {QStringLiteral("edgeRowsCompared"), static_cast<double>(metrics.edge_rows_compared)},
      {QStringLiteral("spatial16x16AveragedRgbMae"), metrics.spatial_16_mae},
  };
}

QByteArray image_sha256(const QImage& image) {
  const auto bytes = QByteArray::fromRawData(
      reinterpret_cast<const char*>(image.constBits()),
      static_cast<qsizetype>(image.sizeInBytes()));
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

double quintic_smooth(double seconds, double start, double end) {
  const auto progress = std::clamp((seconds - start) / (end - start), 0.0, 1.0);
  const auto progress_squared = progress * progress;
  const auto progress_cubed = progress_squared * progress;
  return progress_cubed * (10.0 + progress * (-15.0 + 6.0 * progress));
}

bool save_frame(const QImage& image, const QString& path) {
  return image.save(path, "PNG");
}

QJsonObject run(const QString& output_directory) {
  using namespace photo_editor::desktop::timeline_motion;
  constexpr QSize size(1920, 1080);
  if (!desktop_light_field_metal_available()) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Native Metal is unavailable.")}};
  }
  if (!QDir().mkpath(output_directory)) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Cannot create the parity artifact folder.")}};
  }

  const std::array<double, 5> light_times{0.0, 2.0, 149.0 / 30.0, 7.0, 9.0};
  QJsonArray light_samples;
  bool all_light_samples_pass = true;
  quint64 light_rgb_sum = 0;
  quint64 light_rgb_count = 0;
  quint64 light_alpha_sum = 0;
  quint64 light_alpha_count = 0;
  std::array<quint64, 256> light_rgb_histogram{};
  int light_rgb_max = 0;
  int light_alpha_max = 0;
  quint64 light_differing = 0;
  double light_spatial_sum = 0.0;
  quint64 light_spatial_count = 0;
  QStringList light_hashes;

  for (size_t sample_index = 0; sample_index < light_times.size(); ++sample_index) {
    const auto seconds = light_times.at(sample_index);
    const auto gather = 1.0 - quintic_smooth(seconds, 1.0, 4.0);
    const DesktopLightFieldParameters parameters{seconds, gather, 1.0};
    const auto cpu = render_desktop_light_field(size, parameters);
    QString render_error;
    const auto metal = render_desktop_light_field_metal(
        size, parameters, nullptr, &render_error);
    if (cpu.isNull() || metal.isNull()) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), render_error.isEmpty()
                   ? QStringLiteral("A full-resolution desktop-light frame failed.")
                   : render_error}};
    }
    const auto metrics = compare_images(cpu, metal);
    const auto sample_passes = metrics.rgb_mae <= 8.0 && metrics.rgb_p95 <= 20.0 &&
                               metrics.spatial_16_mae <= 2.0 &&
                               metrics.alpha_mae <= 0.01 && metrics.alpha_max == 0;
    all_light_samples_pass = all_light_samples_pass && sample_passes;
    const auto label = QStringLiteral("desktop_%1_t%2")
                           .arg(static_cast<int>(sample_index))
                           .arg(seconds, 0, 'f', 3);
    if (!save_frame(cpu, QDir(output_directory).filePath(label + QStringLiteral("_cpu.png"))) ||
        !save_frame(metal, QDir(output_directory).filePath(label + QStringLiteral("_metal.png")))) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("A desktop-light frame artifact could not be saved.")}};
    }
    light_samples.append(QJsonObject{
        {QStringLiteral("timeSeconds"), seconds},
        {QStringLiteral("gather"), gather},
        {QStringLiteral("sourceAlpha"), 1.0},
        {QStringLiteral("cpuSha256"), QString::fromLatin1(image_sha256(cpu))},
        {QStringLiteral("metalSha256"), QString::fromLatin1(image_sha256(metal))},
        {QStringLiteral("metrics"), to_json(metrics)},
        {QStringLiteral("passesPixelAndAlphaGates"), sample_passes},
    });
    light_hashes.append(QString::fromLatin1(image_sha256(metal)));
    light_rgb_sum += static_cast<quint64>(std::llround(
        metrics.rgb_mae * static_cast<double>(size.width()) * size.height() * 3.0));
    light_rgb_count += static_cast<quint64>(size.width()) * size.height() * 3ULL;
    light_alpha_sum += static_cast<quint64>(std::llround(
        metrics.alpha_mae * static_cast<double>(size.width()) * size.height()));
    light_alpha_count += static_cast<quint64>(size.width()) * size.height();
    light_rgb_max = std::max(light_rgb_max, metrics.rgb_max);
    light_alpha_max = std::max(light_alpha_max, metrics.alpha_max);
    light_differing += metrics.rgb_differing_channels;
    light_spatial_sum += metrics.spatial_16_mae;
    ++light_spatial_count;
    // Recompute the channel histogram per image so aggregate p95 uses every pixel.
    for (int y = 0; y < size.height(); ++y) {
      const auto* cpu_row = cpu.constScanLine(y);
      const auto* metal_row = metal.constScanLine(y);
      for (int x = 0; x < size.width(); ++x) {
        const auto pixel_offset = x * 4;
        for (int channel = 0; channel < 3; ++channel) {
          const auto difference = std::abs(
              static_cast<int>(cpu_row[pixel_offset + channel]) -
              static_cast<int>(metal_row[pixel_offset + channel]));
          ++light_rgb_histogram[static_cast<size_t>(difference)];
        }
      }
    }
  }

  QJsonArray veil_samples;
  bool all_veil_samples_pass = true;
  std::array<quint64, 256> veil_rgb_histogram{};
  quint64 veil_rgb_sum = 0;
  quint64 veil_rgb_count = 0;
  quint64 veil_alpha_sum = 0;
  quint64 veil_alpha_count = 0;
  int veil_rgb_max = 0;
  int veil_alpha_max = 0;
  quint64 veil_differing = 0;
  double veil_spatial_sum = 0.0;
  quint64 veil_spatial_count = 0;
  const std::array<double, 5> veil_local_times{-0.5, -0.25, 0.0, 0.25, 0.5};
  for (size_t sample_index = 0; sample_index < veil_local_times.size(); ++sample_index) {
    const auto local_seconds = veil_local_times.at(sample_index);
    const auto light_seconds = light_times.at(sample_index);
    const auto cpu = render_transition_veil_frame(
                        size, local_seconds, light_seconds, 0.18, 1.0, 0.42)
                        .convertToFormat(QImage::Format_RGBA8888);
    QString render_error;
    const auto metal = render_transition_veil_field_metal(
        size, local_seconds, light_seconds, 0.18, 0.42, nullptr, &render_error);
    if (cpu.isNull() || metal.isNull()) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), render_error.isEmpty()
                   ? QStringLiteral("A full-resolution transition-veil frame failed.")
                   : render_error}};
    }
    const auto metrics = compare_images(cpu, metal);
    const auto sample_passes = metrics.rgb_mae <= 8.0 && metrics.rgb_p95 <= 20.0 &&
                               metrics.spatial_16_mae <= 2.0 &&
                               metrics.alpha_mae <= 0.20 && metrics.alpha_p95 <= 20.0 &&
                               metrics.edge_position_max_delta_pixels <= 1;
    all_veil_samples_pass = all_veil_samples_pass && sample_passes;
    const auto label = QStringLiteral("veil_%1_local%2")
                           .arg(static_cast<int>(sample_index))
                           .arg(local_seconds, 0, 'f', 2);
    if (!save_frame(cpu, QDir(output_directory).filePath(label + QStringLiteral("_cpu.png"))) ||
        !save_frame(metal, QDir(output_directory).filePath(label + QStringLiteral("_metal.png")))) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("A transition-veil frame artifact could not be saved.")}};
    }
    veil_samples.append(QJsonObject{
        {QStringLiteral("localTimeSeconds"), local_seconds},
        {QStringLiteral("lightTimeSeconds"), light_seconds},
        {QStringLiteral("gather"), 0.18},
        {QStringLiteral("fieldOpacity"), 0.42},
        {QStringLiteral("cpuSha256"), QString::fromLatin1(image_sha256(cpu))},
        {QStringLiteral("metalSha256"), QString::fromLatin1(image_sha256(metal))},
        {QStringLiteral("metrics"), to_json(metrics)},
        {QStringLiteral("passesPixelAndEdgeGates"), sample_passes},
    });
    veil_rgb_sum += static_cast<quint64>(std::llround(
        metrics.rgb_mae * static_cast<double>(size.width()) * size.height() * 3.0));
    veil_rgb_count += static_cast<quint64>(size.width()) * size.height() * 3ULL;
    veil_alpha_sum += static_cast<quint64>(std::llround(
        metrics.alpha_mae * static_cast<double>(size.width()) * size.height()));
    veil_alpha_count += static_cast<quint64>(size.width()) * size.height();
    veil_rgb_max = std::max(veil_rgb_max, metrics.rgb_max);
    veil_alpha_max = std::max(veil_alpha_max, metrics.alpha_max);
    veil_differing += metrics.rgb_differing_channels;
    veil_spatial_sum += metrics.spatial_16_mae;
    ++veil_spatial_count;
    for (int y = 0; y < size.height(); ++y) {
      const auto* cpu_row = cpu.constScanLine(y);
      const auto* metal_row = metal.constScanLine(y);
      for (int x = 0; x < size.width(); ++x) {
        const auto pixel_offset = x * 4;
        for (int channel = 0; channel < 3; ++channel) {
          const auto difference = std::abs(
              static_cast<int>(cpu_row[pixel_offset + channel]) -
              static_cast<int>(metal_row[pixel_offset + channel]));
          ++veil_rgb_histogram[static_cast<size_t>(difference)];
        }
      }
    }
  }

  const auto percentile_95 = [](const std::array<quint64, 256>& histogram,
                                quint64 count) {
    const auto threshold = static_cast<quint64>(std::ceil(count * 0.95));
    quint64 accumulated = 0;
    for (int value = 0; value < static_cast<int>(histogram.size()); ++value) {
      accumulated += histogram[static_cast<size_t>(value)];
      if (accumulated >= threshold) {
        return value;
      }
    }
    return 0;
  };
  QStringList unique_light_hashes = light_hashes;
  unique_light_hashes.removeDuplicates();
  const auto light_temporal_frames_pass = unique_light_hashes.size() ==
                                          static_cast<qsizetype>(light_times.size());
  const auto light_rgb_mae = light_rgb_count == 0
      ? 0.0
      : static_cast<double>(light_rgb_sum) / light_rgb_count;
  const auto light_alpha_mae = light_alpha_count == 0
      ? 0.0
      : static_cast<double>(light_alpha_sum) / light_alpha_count;
  const auto veil_rgb_mae = veil_rgb_count == 0
      ? 0.0
      : static_cast<double>(veil_rgb_sum) / veil_rgb_count;
  const auto veil_alpha_mae = veil_alpha_count == 0
      ? 0.0
      : static_cast<double>(veil_alpha_sum) / veil_alpha_count;

  return {
      {QStringLiteral("ok"), all_light_samples_pass && all_veil_samples_pass &&
                                   light_temporal_frames_pass},
      {QStringLiteral("thresholds"), QJsonObject{
           {QStringLiteral("rgbMaeMax"), 8.0},
           {QStringLiteral("rgbP95AbsoluteDifferenceMax"), 20.0},
           {QStringLiteral("spatial16x16AveragedRgbMaeMax"), 2.0},
           {QStringLiteral("veilAlphaMaeMax"), 0.20},
           {QStringLiteral("veilAlphaP95AbsoluteDifferenceMax"), 20.0},
           {QStringLiteral("veilEdgePositionMaxDeltaPixels"), 1},
           {QStringLiteral("desktopAlphaMaxAbsoluteDifference"), 0}}},
      {QStringLiteral("resolution"), QJsonObject{
           {QStringLiteral("width"), size.width()},
           {QStringLiteral("height"), size.height()}}},
      {QStringLiteral("desktopLight"), QJsonObject{
           {QStringLiteral("sourceAlpha"), 1.0},
           {QStringLiteral("samples"), light_samples},
           {QStringLiteral("uniqueTemporalFrames"), unique_light_hashes.size()},
           {QStringLiteral("fiveDistinctTemporalSamplesPassed"), light_temporal_frames_pass},
           {QStringLiteral("aggregateRgbMae"), light_rgb_mae},
           {QStringLiteral("aggregateRgbP95AbsoluteDifference"),
            percentile_95(light_rgb_histogram, light_rgb_count)},
           {QStringLiteral("aggregateRgbMaxAbsoluteDifference"), light_rgb_max},
           {QStringLiteral("aggregateRgbDifferingChannelCount"),
            static_cast<double>(light_differing)},
           {QStringLiteral("aggregateAlphaMae"), light_alpha_mae},
           {QStringLiteral("aggregateAlphaMaxAbsoluteDifference"), light_alpha_max},
           {QStringLiteral("meanSpatial16x16AveragedRgbMae"),
            light_spatial_count == 0 ? 0.0 : light_spatial_sum / light_spatial_count}}},
      {QStringLiteral("transitionVeil"), QJsonObject{
           {QStringLiteral("samples"), veil_samples},
           {QStringLiteral("aggregateRgbMae"), veil_rgb_mae},
           {QStringLiteral("aggregateRgbP95AbsoluteDifference"),
            percentile_95(veil_rgb_histogram, veil_rgb_count)},
           {QStringLiteral("aggregateRgbMaxAbsoluteDifference"), veil_rgb_max},
           {QStringLiteral("aggregateRgbDifferingChannelCount"),
            static_cast<double>(veil_differing)},
           {QStringLiteral("aggregateAlphaMae"), veil_alpha_mae},
           {QStringLiteral("aggregateAlphaMaxAbsoluteDifference"), veil_alpha_max},
           {QStringLiteral("meanSpatial16x16AveragedRgbMae"),
            veil_spatial_count == 0 ? 0.0 : veil_spatial_sum / veil_spatial_count}}},
  };
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  const auto arguments = application.arguments();
  const auto output_directory = arguments.size() > 1
      ? arguments.at(1)
      : QStringLiteral("marketing/native-recreation-v4/evidence/motion-review/light-generator-review/metal-parity");
  const auto result = run(output_directory);
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
  return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

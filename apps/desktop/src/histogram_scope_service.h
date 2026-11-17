#pragma once

#include <algorithm>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVariantList>

namespace photo_editor::desktop {

struct HistogramScopeOptions final {
  int max_edge = 0;
  int columns = 128;
  int levels = 64;
  int vector_samples = 1800;
  double minimum = 0.0;
  double maximum = 1.0;
  QRectF region{0, 0, 1, 1};
};

[[nodiscard]] inline HistogramScopeOptions histogram_scope_options(
    const QString& quality, bool live = false) {
  HistogramScopeOptions options;
  options.max_edge = quality == QStringLiteral("Fast") ? 256
      : quality == QStringLiteral("Precise") ? 1024 : 512;
  options.columns = quality == QStringLiteral("Precise") ? 256 : 128;
  options.levels = quality == QStringLiteral("Precise") ? 128 : 64;
  options.vector_samples = quality == QStringLiteral("Precise") ? 7200 : 1800;
  // Interactive work stays bounded, then refines at the selected quality.
  if (live) options.max_edge = std::min(options.max_edge, 256);
  return options;
}

inline constexpr auto kHistogramScopeServiceContract =
    "histogram-scope-service:v1|histogram:4096-to-256|waveform:128x64|vectorscope:max-1800";

struct HistogramComputationResult final {
  QVariantList red;
  QVariantList green;
  QVariantList blue;
  QVariantList luminance;
  QVariantList red_counts;
  QVariantList green_counts;
  QVariantList blue_counts;
  QVariantList luminance_counts;
  QVariantList waveform_red;
  QVariantList waveform_green;
  QVariantList waveform_blue;
  QVariantList waveform_luminance;
  QVariantList density_red;
  QVariantList density_green;
  QVariantList density_blue;
  QVariantList density_luminance;
  QVariantList vectorscope_points;
  int shadow_clip_count = 0;
  int highlight_clip_count = 0;
  int sample_count = 0;
  int raw_clip_sample_count = 0;
  int raw_shadow_clip_count = 0;
  int raw_highlight_clip_count = 0;
  int generation = 0;
  qint64 elapsed_ms = 0;
  bool full_resolution = false;
  QString render_reason;
  QString interaction_mode;
  int columns = 128;
  int levels = 64;
  int rejected_sample_count = 0;
  int configuration = 0;
  double minimum = 0.0;
  double maximum = 1.0;
};

[[nodiscard]] QString histogram_scope_service_contract();
[[nodiscard]] HistogramComputationResult compute_histogram_scope_data(
    const QImage& image);

[[nodiscard]] HistogramComputationResult compute_histogram_scope_data_with_options(
    const QImage& image, const HistogramScopeOptions& options);

}  // namespace photo_editor::desktop

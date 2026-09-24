#pragma once

#include <QImage>
#include <QString>
#include <QVariantList>

namespace photo_editor::desktop {

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
};

[[nodiscard]] QString histogram_scope_service_contract();
[[nodiscard]] HistogramComputationResult compute_histogram_scope_data(
    const QImage& image);

}  // namespace photo_editor::desktop

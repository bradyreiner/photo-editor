#include "generated_mask_quality.h"

#include "edit_recipe_mask_schema.h"

#include <algorithm>

#include <QStringList>

namespace photo_editor::desktop {

GeneratedMaskRasterStats generated_mask_raster_stats(const QVariantMap& mask) {
  GeneratedMaskRasterStats stats;
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_segmentation_payload(data, width, height)) {
    return stats;
  }
  const auto confidence_data =
      mask.value(QStringLiteral("confidenceData")).toByteArray();
  const auto confidence_width =
      mask.value(QStringLiteral("confidenceWidth")).toInt();
  const auto confidence_height =
      mask.value(QStringLiteral("confidenceHeight")).toInt();
  stats.has_confidence = valid_segmentation_payload(
                             confidence_data,
                             confidence_width,
                             confidence_height) &&
                         confidence_width == width &&
                         confidence_height == height;

  auto alpha_sum = 0.0;
  auto confidence_sum = 0.0;
  auto confidence_weight = 0.0;
  const auto total_pixels = width * height;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = y * width + x;
      const auto alpha = static_cast<unsigned char>(data.at(index));
      if (alpha == 0U) {
        continue;
      }
      ++stats.selected_pixels;
      const auto weight = static_cast<double>(alpha) / 255.0;
      alpha_sum += weight;
      stats.touches_edge = stats.touches_edge || x == 0 || y == 0 ||
                           x == width - 1 || y == height - 1;
      if (stats.has_confidence) {
        const auto confidence =
            static_cast<unsigned char>(confidence_data.at(index));
        confidence_sum +=
            (static_cast<double>(confidence) / 255.0) * weight;
        confidence_weight += weight;
      }
    }
  }
  stats.valid = true;
  stats.coverage = total_pixels > 0
                       ? alpha_sum / static_cast<double>(total_pixels)
                       : 0.0;
  if (stats.has_confidence && confidence_weight > 0.0) {
    stats.mean_confidence = confidence_sum / confidence_weight;
  }
  return stats;
}

QVariantMap generated_mask_quality_report_for_mask(
    const QVariantMap& mask,
    bool stale) {
  const auto kind = normalized_mask_kind(
      mask.value(QStringLiteral("kind")).toString());
  if (!is_ai_segmentation_mask_kind(kind)) {
    return {};
  }
  const auto stats = generated_mask_raster_stats(mask);
  QStringList flags;
  auto score = 100;
  if (!stats.valid || stats.selected_pixels <= 0 || stats.coverage <= 1e-6) {
    flags.append(QStringLiteral("empty"));
    score = 0;
  } else {
    if (stats.coverage < 0.002) {
      flags.append(QStringLiteral("tiny"));
      score -= 45;
    }
    if (stats.coverage > 0.92) {
      flags.append(QStringLiteral("huge"));
      score -= 30;
    }
    const auto edge_contact_expected =
        kind == QStringLiteral("sky") || kind == QStringLiteral("background");
    if (stats.touches_edge && !edge_contact_expected) {
      flags.append(QStringLiteral("clipped"));
      score -= 15;
    }
    if (stats.has_confidence && stats.mean_confidence < 0.55) {
      flags.append(QStringLiteral("low-confidence"));
      score -= 30;
    }
    if (stale) {
      flags.append(QStringLiteral("stale"));
      score -= 25;
    }
    score = std::clamp(score, 0, 100);
  }
  const auto label = score >= 90 ? QStringLiteral("Good")
                    : score >= 70 ? QStringLiteral("Review")
                                  : QStringLiteral("Problem");
  const auto issue_text = flags.isEmpty()
                              ? QStringLiteral("no quality warnings")
                              : flags.join(QStringLiteral(", "));
  return QVariantMap{
      {QStringLiteral("score"), score},
      {QStringLiteral("label"), label},
      {QStringLiteral("flags"), flags},
      {QStringLiteral("coverage"), stats.coverage},
      {QStringLiteral("selectedPixels"), stats.selected_pixels},
      {QStringLiteral("meanConfidence"), stats.mean_confidence},
      {QStringLiteral("summary"),
       QStringLiteral("Quality %1 (%2): %3").arg(score).arg(label, issue_text)},
  };
}

}  // namespace photo_editor::desktop

#pragma once

#include <algorithm>
#include <cmath>

#include <QImage>
#include <QVector>
#include <QtMath>

namespace photo_editor::desktop {

struct AutoHorizonLineCandidate final {
  double angle_degrees = 0.0;
  double center_y = 0.0;
  double score = 0.0;
  double coverage = 0.0;
  double mean_contrast = 0.0;
};

struct AutoHorizonEstimate final {
  double angle_degrees = 0.0;
  double confidence = 0.0;
  bool confident = false;
};

inline double auto_horizon_rgb_distance(QRgb first, QRgb second) {
  const auto red = static_cast<double>(qRed(first) - qRed(second));
  const auto green = static_cast<double>(qGreen(first) - qGreen(second));
  const auto blue = static_cast<double>(qBlue(first) - qBlue(second));
  return std::sqrt(red * red + green * green + blue * blue);
}

inline AutoHorizonEstimate estimate_auto_horizon(const QImage& source_image) {
  if (source_image.isNull() || source_image.width() < 80 || source_image.height() < 60) {
    return {};
  }

  constexpr auto kAnalysisEdge = 640;
  const auto analysis_image = source_image.width() > kAnalysisEdge ||
                                      source_image.height() > kAnalysisEdge
      ? source_image.scaled(kAnalysisEdge, kAnalysisEdge, Qt::KeepAspectRatio, Qt::FastTransformation)
      : source_image;
  const auto pixels = analysis_image.convertToFormat(QImage::Format_RGB32);
  const auto width = pixels.width();
  const auto height = pixels.height();
  if (width < 80 || height < 60) return {};

  QVector<int> sample_xs;
  const auto x_step = std::max(1, width / 320);
  for (int x = x_step / 2; x < width; x += x_step) sample_xs.append(x);
  if (sample_xs.size() < 64) return {};

  const auto center_x = static_cast<double>(width - 1) * 0.5;
  const auto center_margin = static_cast<double>(height) * 0.10;
  const auto y_step = std::max(1, height / 160);
  QVector<AutoHorizonLineCandidate> candidates;
  candidates.reserve(16000);

  // Search only gently sloped lines. The detector is deliberately less
  // ambitious than the manual guide: when a line is not clearly dominant,
  // asking the user to draw one is safer than rotating the whole photograph.
  for (int angle_step = -48; angle_step <= 48; ++angle_step) {
    const auto angle = static_cast<double>(angle_step) * 0.25;
    const auto slope = std::tan(qDegreesToRadians(angle));
    for (int center_y = static_cast<int>(center_margin);
         center_y < height - static_cast<int>(center_margin);
         center_y += y_step) {
      int supported_samples = 0;
      double total_contrast = 0.0;
      for (const auto x : sample_xs) {
        const auto line_y = qRound(static_cast<double>(center_y) +
                                   slope * (static_cast<double>(x) - center_x));
        if (line_y < 4 || line_y >= height - 4) continue;
        const auto* upper_row = reinterpret_cast<const QRgb*>(pixels.constScanLine(line_y - 3));
        const auto* lower_row = reinterpret_cast<const QRgb*>(pixels.constScanLine(line_y + 3));
        const auto contrast = auto_horizon_rgb_distance(upper_row[x], lower_row[x]);
        total_contrast += std::min(contrast, 255.0);
        if (contrast >= 36.0) ++supported_samples;
      }

      const auto sample_count = static_cast<double>(sample_xs.size());
      const auto coverage = static_cast<double>(supported_samples) / sample_count;
      const auto mean_contrast = total_contrast / sample_count;
      const auto score = coverage * 0.72 + (mean_contrast / 255.0) * 0.28;
      candidates.append({angle, static_cast<double>(center_y), score, coverage, mean_contrast});
    }
  }
  if (candidates.isEmpty()) return {};

  std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
    return left.score > right.score;
  });
  const auto best = candidates.constFirst();
  const auto best_slope = std::tan(qDegreesToRadians(best.angle_degrees));
  double second_distinct_score = 0.0;
  for (int index = 1; index < candidates.size(); ++index) {
    const auto& candidate = candidates.at(index);
    const auto candidate_slope = std::tan(qDegreesToRadians(candidate.angle_degrees));
    const auto left_delta = std::abs(
        (candidate.center_y - best.center_y) -
        (candidate_slope - best_slope) * center_x);
    const auto right_delta = std::abs(
        (candidate.center_y - best.center_y) +
        (candidate_slope - best_slope) * (static_cast<double>(width - 1) - center_x));
    if (std::max(left_delta, right_delta) > static_cast<double>(height) * 0.035) {
      second_distinct_score = candidate.score;
      break;
    }
  }

  const auto uniqueness = second_distinct_score <= 0.0
      ? 1.0
      : 1.0 - qBound(0.0, second_distinct_score / std::max(best.score, 1e-6), 1.0);
  const auto contrast_confidence = qBound(0.0, best.mean_contrast / 90.0, 1.0);
  const auto confidence = best.coverage * contrast_confidence *
      (second_distinct_score <= 0.0 ? 1.0 : uniqueness * 2.5);
  const auto confident = best.coverage >= 0.84 && best.mean_contrast >= 48.0 &&
      (second_distinct_score <= 0.0 || best.score >= second_distinct_score * 1.18) &&
      std::abs(best.angle_degrees) <= 12.0;
  return {best.angle_degrees, qBound(0.0, confidence, 1.0), confident};
}

}  // namespace photo_editor::desktop

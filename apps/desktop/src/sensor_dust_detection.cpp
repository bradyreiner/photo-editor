#include "sensor_dust_detection.h"

#include "photo_editor/pipeline/color_transform.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QImage>

namespace photo_editor::desktop {
namespace {

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

double smoothstep(double edge0, double edge1, double value) {
  const auto t = clamp_unit((value - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

double dust_luminance_for_pixel(const QColor& color) {
  const auto red = photo_editor::pipeline::srgb_to_linear(color.redF());
  const auto green = photo_editor::pipeline::srgb_to_linear(color.greenF());
  const auto blue = photo_editor::pipeline::srgb_to_linear(color.blueF());
  return red * 0.2126 + green * 0.7152 + blue * 0.0722;
}

}  // namespace

std::vector<DustSpotCandidate> detect_dust_spots(
    const QImage& image,
    int maximum_spots) {
  std::vector<DustSpotCandidate> spots;
  if (image.isNull() || image.width() < 16 || image.height() < 16 ||
      maximum_spots <= 0) {
    return spots;
  }

  const auto source = image.convertToFormat(QImage::Format_ARGB32);
  const auto width = source.width();
  const auto height = source.height();
  std::vector<double> luminance(
      static_cast<std::size_t>(width * height), 0.0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      luminance[static_cast<std::size_t>(y * width + x)] =
          dust_luminance_for_pixel(QColor::fromRgb(source.pixel(x, y)));
    }
  }

  const auto luma_at = [&](int x, int y) {
    return luminance[static_cast<std::size_t>(y * width + x)];
  };

  std::vector<DustSpotCandidate> candidates;
  candidates.reserve(256);
  constexpr auto kMargin = 7;
  for (int y = kMargin; y < height - kMargin; ++y) {
    for (int x = kMargin; x < width - kMargin; ++x) {
      auto center_sum = 0.0;
      auto center_count = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          center_sum += luma_at(x + dx, y + dy);
          ++center_count;
        }
      }
      const auto center_mean =
          center_sum / static_cast<double>(center_count);

      auto ring_sum = 0.0;
      auto ring_square_sum = 0.0;
      auto ring_count = 0;
      for (int dy = -7; dy <= 7; ++dy) {
        for (int dx = -7; dx <= 7; ++dx) {
          const auto distance = std::hypot(
              static_cast<double>(dx), static_cast<double>(dy));
          if (distance < 3.5 || distance > 7.5) {
            continue;
          }
          const auto value = luma_at(x + dx, y + dy);
          ring_sum += value;
          ring_square_sum += value * value;
          ++ring_count;
        }
      }
      if (ring_count == 0) {
        continue;
      }

      const auto ring_mean = ring_sum / static_cast<double>(ring_count);
      const auto ring_variance = std::max(
          0.0,
          ring_square_sum / static_cast<double>(ring_count) -
              ring_mean * ring_mean);
      const auto ring_texture = std::sqrt(ring_variance);
      const auto contrast = ring_mean - center_mean;
      const auto threshold = std::max(0.035, ring_mean * 0.13);
      if (ring_mean < 0.08 || contrast < threshold || ring_texture > 0.11) {
        continue;
      }

      auto local_minimum = true;
      for (int dy = -2; dy <= 2 && local_minimum; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          if (luma_at(x, y) > luma_at(x + dx, y + dy) + 0.003) {
            local_minimum = false;
            break;
          }
        }
      }
      if (!local_minimum) {
        continue;
      }

      candidates.push_back(DustSpotCandidate{
          .x = x,
          .y = y,
          .radius = std::clamp(2.4 + contrast * 18.0, 2.4, 7.5),
          .score = contrast * ring_mean *
                   (1.0 - std::min(1.0, ring_texture / 0.11)),
      });
    }
  }

  std::sort(
      candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.score > right.score;
      });

  const auto cluster_radius = std::clamp(
      static_cast<double>(std::min(width, height)) * 0.035, 10.0, 36.0);
  for (const auto& candidate : candidates) {
    auto clustered_candidate = candidate;
    auto weighted_x = 0.0;
    auto weighted_y = 0.0;
    auto weight_sum = 0.0;
    auto maximum_cluster_distance = 0.0;
    for (const auto& neighbor : candidates) {
      const auto distance = std::hypot(
          static_cast<double>(candidate.x - neighbor.x),
          static_cast<double>(candidate.y - neighbor.y));
      if (distance > cluster_radius) {
        continue;
      }
      const auto weight = std::max(1e-6, neighbor.score);
      weighted_x += static_cast<double>(neighbor.x) * weight;
      weighted_y += static_cast<double>(neighbor.y) * weight;
      weight_sum += weight;
    }
    if (weight_sum > 0.0) {
      clustered_candidate.x = std::clamp(
          static_cast<int>(std::lround(weighted_x / weight_sum)), 0, width - 1);
      clustered_candidate.y = std::clamp(
          static_cast<int>(std::lround(weighted_y / weight_sum)), 0, height - 1);
      for (const auto& neighbor : candidates) {
        const auto distance_from_cluster = std::hypot(
            static_cast<double>(clustered_candidate.x - neighbor.x),
            static_cast<double>(clustered_candidate.y - neighbor.y));
        const auto distance_from_seed = std::hypot(
            static_cast<double>(candidate.x - neighbor.x),
            static_cast<double>(candidate.y - neighbor.y));
        if (distance_from_seed <= cluster_radius) {
          maximum_cluster_distance =
              std::max(maximum_cluster_distance, distance_from_cluster);
        }
      }
      clustered_candidate.radius = std::clamp(
          std::max(
              clustered_candidate.radius,
              maximum_cluster_distance * 0.65 + 4.0),
          2.4,
          24.0);
    }

    const auto suppressed = std::any_of(
        spots.cbegin(), spots.cend(), [&](const auto& accepted) {
          const auto distance = std::hypot(
              static_cast<double>(clustered_candidate.x - accepted.x),
              static_cast<double>(clustered_candidate.y - accepted.y));
          return distance <
                 std::max(
                     6.0, clustered_candidate.radius + accepted.radius);
        });
    if (suppressed) {
      continue;
    }
    spots.push_back(clustered_candidate);
    if (static_cast<int>(spots.size()) >= maximum_spots) {
      break;
    }
  }
  return spots;
}

QByteArray dust_spot_mask_data(
    int width,
    int height,
    const std::vector<DustSpotCandidate>& spots) {
  if (width <= 0 || height <= 0) {
    return {};
  }
  QByteArray data(width * height, '\0');
  for (const auto& spot : spots) {
    const auto radius = std::max(1.5, spot.radius);
    const auto min_x = std::max(
        0,
        static_cast<int>(
            std::floor(static_cast<double>(spot.x) - radius * 1.35)));
    const auto max_x = std::min(
        width - 1,
        static_cast<int>(
            std::ceil(static_cast<double>(spot.x) + radius * 1.35)));
    const auto min_y = std::max(
        0,
        static_cast<int>(
            std::floor(static_cast<double>(spot.y) - radius * 1.35)));
    const auto max_y = std::min(
        height - 1,
        static_cast<int>(
            std::ceil(static_cast<double>(spot.y) + radius * 1.35)));
    for (int y = min_y; y <= max_y; ++y) {
      for (int x = min_x; x <= max_x; ++x) {
        const auto distance = std::hypot(
            static_cast<double>(x - spot.x),
            static_cast<double>(y - spot.y));
        const auto weight =
            1.0 - smoothstep(0.42, 1.0, distance / radius);
        if (weight <= 1e-6) {
          continue;
        }
        const auto index = y * width + x;
        data[index] = static_cast<char>(std::max(
            static_cast<int>(static_cast<unsigned char>(data.at(index))),
            static_cast<int>(std::lround(clamp_unit(weight) * 255.0))));
      }
    }
  }
  return data;
}

}  // namespace photo_editor::desktop

#include "mask_outline_tracing.h"

#include "brush_outline_geometry.h"
#include "mask_outline_simplification.h"
#include "smart_selection_polygon_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <QHash>
#include <QPair>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QSize>
#include <QVector>

namespace photo_editor::desktop {
namespace {

struct TracedOutlineLoop final {
  QVector<QPoint> points;
  double area_pixels = 0.0;
};

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

bool valid_alpha_raster(const QByteArray& data, int width, int height) {
  if (width <= 1 || height <= 1) {
    return false;
  }
  const auto expected_size =
      static_cast<qint64>(width) * static_cast<qint64>(height);
  return expected_size > 0 && expected_size <= data.size();
}

quint64 grid_key(int x, int y) {
  return (static_cast<quint64>(static_cast<quint32>(x)) << 32) |
         static_cast<quint32>(y);
}

QPoint grid_point_from_key(quint64 key) {
  return QPoint(
      static_cast<int>(static_cast<quint32>(key >> 32)),
      static_cast<int>(static_cast<quint32>(key & 0xffffffffULL)));
}

int cardinal_direction(quint64 start, quint64 end) {
  const auto from = grid_point_from_key(start);
  const auto to = grid_point_from_key(end);
  const auto dx = to.x() - from.x();
  const auto dy = to.y() - from.y();
  if (dx > 0) {
    return 0;
  }
  if (dy > 0) {
    return 1;
  }
  if (dx < 0) {
    return 2;
  }
  return 3;
}

int turn_priority(int previous_direction, int next_direction) {
  const auto delta = (next_direction - previous_direction + 4) % 4;
  if (delta == 1) {
    return 0;
  }
  if (delta == 0) {
    return 1;
  }
  if (delta == 3) {
    return 2;
  }
  return 3;
}

double polygon_area_pixels(const QVector<QPoint>& points) {
  if (points.size() < 4) {
    return 0.0;
  }
  auto twice_area = 0.0;
  for (auto index = 0; index < points.size(); ++index) {
    const auto& current = points.at(index);
    const auto& next = points.at((index + 1) % points.size());
    twice_area += static_cast<double>(current.x()) * static_cast<double>(next.y()) -
                  static_cast<double>(next.x()) * static_cast<double>(current.y());
  }
  return std::abs(twice_area) * 0.5;
}

void append_directed_boundary_edge(
    int start_x,
    int start_y,
    int end_x,
    int end_y,
    QHash<quint64, QVector<quint64>>* adjacency) {
  if (adjacency == nullptr) {
    return;
  }
  adjacency->operator[](grid_key(start_x, start_y)).append(grid_key(end_x, end_y));
}

bool loop_sort_less(const TracedOutlineLoop& left, const TracedOutlineLoop& right) {
  if (std::abs(left.area_pixels - right.area_pixels) > 1e-6) {
    return left.area_pixels > right.area_pixels;
  }
  if (left.points.size() != right.points.size()) {
    return left.points.size() > right.points.size();
  }
  const auto left_first = left.points.isEmpty() ? QPoint() : left.points.first();
  const auto right_first = right.points.isEmpty() ? QPoint() : right.points.first();
  if (left_first.y() != right_first.y()) {
    return left_first.y() < right_first.y();
  }
  return left_first.x() < right_first.x();
}

// Alpha samples use the same node coordinates as the canonical mask sampler:
// normalized x * (width - 1), normalized y * (height - 1). Interpolating the
// crossing on each cell edge avoids quantizing a feathered contour to pixels.
QVariantList interpolated_alpha_contours(const QByteArray& data, int width, int height) {
  const auto* alpha = reinterpret_cast<const unsigned char*>(data.constData());
  const auto peak = *std::max_element(alpha, alpha + static_cast<qsizetype>(width) * height);
  constexpr double noise_floor = 8.0;
  if (peak < noise_floor) return {};
  const double threshold = std::max(noise_floor, static_cast<double>(peak) * 0.5);
  const auto sample = [&](int x, int y) -> double {
    return x < 0 || y < 0 || x >= width || y >= height ? 0.0 : alpha[y * width + x];
  };
  QHash<quint64, QPointF> crossings;
  QHash<quint64, QVector<quint64>> neighbors;
  const auto connect = [&](quint64 a, quint64 b) {
    neighbors[a].append(b);
    neighbors[b].append(a);
  };
  // A virtual zero border closes contours touching the image. Publication
  // clips these virtual samples to the actual image boundary.
  for (int y = -1; y < height; ++y) {
    for (int x = -1; x < width; ++x) {
      const std::array<double, 4> values{sample(x, y), sample(x + 1, y),
                                       sample(x + 1, y + 1), sample(x, y + 1)};
      int state = 0;
      for (int corner = 0; corner < 4; ++corner) {
        if (values[static_cast<std::size_t>(corner)] >= threshold) state |= 1 << corner;
      }
      if (state == 0 || state == 15) continue;
      const std::array<QPointF, 4> points{QPointF(x, y), QPointF(x + 1, y),
                                         QPointF(x + 1, y + 1), QPointF(x, y + 1)};
      std::array<quint64, 4> keys{};
      std::array<int, 4> edges{};
      int edge_count = 0;
      for (int edge = 0; edge < 4; ++edge) {
        const auto next = (edge + 1) % 4;
        if ((values[static_cast<std::size_t>(edge)] >= threshold) ==
            (values[static_cast<std::size_t>(next)] >= threshold)) continue;
        const bool horizontal = edge == 0 || edge == 2;
        const int edge_x = x + (edge == 1 ? 1 : 0);
        const int edge_y = y + (edge == 2 ? 1 : 0);
        const auto key = grid_key(edge_x + 1, edge_y + 1) |
            (horizontal ? (quint64{1} << 63) : 0);
        keys[static_cast<std::size_t>(edge)] = key;
        edges[static_cast<std::size_t>(edge_count++)] = edge;
        if (!crossings.contains(key)) {
          const double t = (threshold - values[static_cast<std::size_t>(edge)]) /
              (values[static_cast<std::size_t>(next)] - values[static_cast<std::size_t>(edge)]);
          crossings.insert(key, points[static_cast<std::size_t>(edge)] +
              (points[static_cast<std::size_t>(next)] - points[static_cast<std::size_t>(edge)]) * t);
        }
      }
      if (edge_count == 2) {
        connect(keys[static_cast<std::size_t>(edges[0])], keys[static_cast<std::size_t>(edges[1])]);
      } else if (edge_count == 4) {
        // Resolve saddles using the bilinear field, rather than arbitrarily
        // joining diagonal regions or losing a narrow hole.
        const double determinant = (values[0] - threshold) * (values[2] - threshold) -
            (values[1] - threshold) * (values[3] - threshold);
        const bool top_right_pair = determinant > 0 || (determinant == 0 && state == 10);
        if (top_right_pair) { connect(keys[0], keys[1]); connect(keys[2], keys[3]); }
        else { connect(keys[0], keys[3]); connect(keys[1], keys[2]); }
      }
    }
  }
  struct Loop { QVariantList path; double area = 0; QPointF first; };
  QVector<Loop> loops;
  QSet<quint64> visited;
  auto starts = neighbors.keys();
  std::sort(starts.begin(), starts.end());
  for (const auto start : starts) {
    if (visited.contains(start)) continue;
    QVector<QPointF> points;
    auto current = start;
    auto previous = start;
    bool closed = false;
    while (!visited.contains(current)) {
      visited.insert(current);
      const auto raw = crossings.value(current);
      const QPointF point(std::clamp(raw.x(), 0.0, static_cast<double>(width - 1)),
                          std::clamp(raw.y(), 0.0, static_cast<double>(height - 1)));
      if (points.isEmpty() || points.last() != point) points.append(point);
      const auto& adjacent = neighbors[current];
      if (adjacent.size() != 2) break;
      const auto next = adjacent[0] == previous ? adjacent[1] : adjacent[0];
      previous = current;
      current = next;
      if (current == start) { closed = true; break; }
    }
    if (!closed || points.size() < 3) continue;
    if (points.first() == points.last()) points.removeLast();
    if (points.size() < 3) continue;
    double twice_area = 0;
    QVector<QPointF> corners;
    corners.reserve(points.size());
    for (qsizetype index = 0; index < points.size(); ++index) {
      const auto& a = points.at((index + points.size() - 1) % points.size());
      const auto& b = points.at(index);
      const auto& c = points.at((index + 1) % points.size());
      twice_area += b.x() * c.y() - c.x() * b.y();
      const auto cross = (b.x() - a.x()) * (c.y() - b.y()) -
                         (b.y() - a.y()) * (c.x() - b.x());
      if (std::abs(cross) > 1e-12) corners.append(b);
    }
    if (corners.size() < 3 || std::abs(twice_area) <= 1e-12) continue;
    loops.append({outline_path_from_pixel_points(corners, QSize(width - 1, height - 1)),
                  std::abs(twice_area) * 0.5, corners.first()});
  }
  std::sort(loops.begin(), loops.end(), [](const Loop& a, const Loop& b) {
    if (a.area != b.area) return a.area > b.area;
    if (a.first.y() != b.first.y()) return a.first.y() < b.first.y();
    return a.first.x() < b.first.x();
  });
  QVariantList paths;
  for (auto& loop : loops) append_outline_path(&paths, loop.path);
  return paths;
}

}  // namespace

QVariantList hull_points_for_segmentation_mask(const QVariantMap& mask) {
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_alpha_raster(data, width, height)) {
    return {};
  }

  const auto stride = std::max(1, std::max(width, height) / 96);
  QVector<QPointF> points;
  points.reserve((width / stride + 1) * (height / stride + 1));
  const auto sample_at = [&](int x, int y) {
    const auto clamped_x = std::clamp(x, 0, width - 1);
    const auto clamped_y = std::clamp(y, 0, height - 1);
    return static_cast<unsigned char>(data.at(clamped_y * width + clamped_x));
  };

  for (int y = 0; y < height; y += stride) {
    for (int x = 0; x < width; x += stride) {
      if (sample_at(x, y) < 96) {
        continue;
      }
      const auto boundary =
          sample_at(x - stride, y) < 96 || sample_at(x + stride, y) < 96 ||
          sample_at(x, y - stride) < 96 || sample_at(x, y + stride) < 96;
      if (!boundary) {
        continue;
      }
      points.append(QPointF(
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5,
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5));
    }
  }

  if (points.size() < 3) {
    QVariantList fallback;
    for (const auto& point : points) {
      QVariantMap point_map;
      point_map.insert(QStringLiteral("x"), clamp_unit(point.x()));
      point_map.insert(QStringLiteral("y"), clamp_unit(point.y()));
      fallback.append(point_map);
    }
    return fallback;
  }

  std::sort(points.begin(), points.end(), [](const QPointF& left, const QPointF& right) {
    if (!qFuzzyCompare(left.x() + 1.0, right.x() + 1.0)) {
      return left.x() < right.x();
    }
    return left.y() < right.y();
  });

  QVector<QPointF> hull;
  hull.reserve(points.size() * 2);
  for (const auto& point : points) {
    while (hull.size() >= 2 &&
           polygon_cross(hull[hull.size() - 2], hull[hull.size() - 1], point) <= 0.0) {
      hull.removeLast();
    }
    hull.append(point);
  }
  const auto lower_size = hull.size();
  for (qsizetype index = points.size() - 2; index >= 0; --index) {
    const auto& point = points[index];
    while (hull.size() > lower_size &&
           polygon_cross(hull[hull.size() - 2], hull[hull.size() - 1], point) <= 0.0) {
      hull.removeLast();
    }
    hull.append(point);
    if (index == 0) {
      break;
    }
  }
  if (!hull.isEmpty()) {
    hull.removeLast();
  }

  QVariantList outline;
  for (const auto& point : hull) {
    QVariantMap point_map;
    point_map.insert(QStringLiteral("x"), clamp_unit(point.x()));
    point_map.insert(QStringLiteral("y"), clamp_unit(point.y()));
    outline.append(point_map);
  }
  return outline;
}

QVariantList bounding_box_points_for_segmentation_mask(const QVariantMap& mask) {
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_alpha_raster(data, width, height)) {
    return {};
  }

  auto min_x = width;
  auto min_y = height;
  auto max_x = -1;
  auto max_y = -1;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (static_cast<unsigned char>(data.at(y * width + x)) < 96) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return {};
  }

  const auto normalized_x = [width](int value) {
    return clamp_unit(static_cast<double>(value) / static_cast<double>(width - 1));
  };
  const auto normalized_y = [height](int value) {
    return clamp_unit(static_cast<double>(value) / static_cast<double>(height - 1));
  };
  QVariantList outline;
  outline.reserve(4);
  const std::array<QPoint, 4> corners{
      QPoint(min_x, min_y), QPoint(max_x, min_y),
      QPoint(max_x, max_y), QPoint(min_x, max_y)};
  for (const auto& corner : corners) {
    QVariantMap point_map;
    point_map.insert(QStringLiteral("x"), normalized_x(corner.x()));
    point_map.insert(QStringLiteral("y"), normalized_y(corner.y()));
    outline.append(point_map);
  }
  return outline;
}

QVariantList traced_outline_paths_for_alpha_mask(
    const QByteArray& data,
    int width,
    int height,
    bool preserve_pixel_edges) {
  if (!valid_alpha_raster(data, width, height)) {
    return {};
  }
  if (!preserve_pixel_edges) return interpolated_alpha_contours(data, width, height);
  const auto sample_at = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return static_cast<unsigned char>(0);
    }
    return static_cast<unsigned char>(data.at(y * width + x));
  };
  const auto pixel_count = static_cast<qsizetype>(width) * static_cast<qsizetype>(height);
  const auto* alpha_begin = reinterpret_cast<const unsigned char*>(data.constData());
  const auto alpha_max = static_cast<int>(*std::max_element(alpha_begin, alpha_begin + pixel_count));
  constexpr auto kNominalOutlineAlphaThreshold = 96;
  constexpr auto kOutlineAlphaNoiseFloor = 8;
  const auto outline_alpha_threshold = std::min(
      kNominalOutlineAlphaThreshold,
      std::max(kOutlineAlphaNoiseFloor, (alpha_max + 1) / 2));

  QHash<quint64, QVector<quint64>> adjacency;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (sample_at(x, y) < outline_alpha_threshold) {
        continue;
      }
      if (sample_at(x, y - 1) < outline_alpha_threshold) {
        append_directed_boundary_edge(x, y, x + 1, y, &adjacency);
      }
      if (sample_at(x + 1, y) < outline_alpha_threshold) {
        append_directed_boundary_edge(x + 1, y, x + 1, y + 1, &adjacency);
      }
      if (sample_at(x, y + 1) < outline_alpha_threshold) {
        append_directed_boundary_edge(x + 1, y + 1, x, y + 1, &adjacency);
      }
      if (sample_at(x - 1, y) < outline_alpha_threshold) {
        append_directed_boundary_edge(x, y + 1, x, y, &adjacency);
      }
    }
  }
  if (adjacency.isEmpty()) {
    return {};
  }

  QSet<QPair<quint64, quint64>> visited_edges;
  QVector<TracedOutlineLoop> loops;
  auto starts = adjacency.keys();
  std::sort(starts.begin(), starts.end());
  for (const auto start : starts) {
    const auto outgoing = adjacency.value(start);
    for (const auto initial_end : outgoing) {
      const QPair<quint64, quint64> initial_edge(start, initial_end);
      if (visited_edges.contains(initial_edge)) {
        continue;
      }
      QVector<QPoint> loop;
      auto current_start = start;
      auto current_end = initial_end;
      auto current_direction = cardinal_direction(current_start, current_end);
      while (true) {
        const QPair<quint64, quint64> edge(current_start, current_end);
        if (visited_edges.contains(edge)) {
          break;
        }
        visited_edges.insert(edge);
        if (loop.isEmpty()) {
          loop.append(grid_point_from_key(current_start));
        }
        loop.append(grid_point_from_key(current_end));
        if (current_end == start) {
          break;
        }

        const auto candidates = adjacency.value(current_end);
        quint64 next_end = 0;
        auto found_next = false;
        auto best_priority = std::numeric_limits<int>::max();
        for (const auto candidate : candidates) {
          if (candidate == current_start ||
              visited_edges.contains(QPair<quint64, quint64>(current_end, candidate))) {
            continue;
          }
          const auto priority =
              turn_priority(current_direction, cardinal_direction(current_end, candidate));
          if (priority < best_priority ||
              (priority == best_priority && (!found_next || candidate < next_end))) {
            best_priority = priority;
            next_end = candidate;
            found_next = true;
          }
        }
        if (!found_next) {
          break;
        }
        current_start = current_end;
        current_end = next_end;
        current_direction = cardinal_direction(current_start, current_end);
      }

      if (loop.size() >= 4 && loop.first() == loop.last()) {
        const auto loop_area = polygon_area_pixels(loop);
        loops.append(TracedOutlineLoop{
            .points = std::move(loop),
            .area_pixels = loop_area,
        });
      }
    }
  }
  if (loops.isEmpty()) {
    return {};
  }
  std::sort(loops.begin(), loops.end(), loop_sort_less);

  QVariantList outline_paths;
  for (auto loop_entry : loops) {
    auto loop = std::move(loop_entry.points);
    if (loop.size() < 4) {
      continue;
    }
    loop.removeLast();
    QVector<QPoint> simplified;
    simplified.reserve(loop.size());
    for (int index = 0; index < loop.size(); ++index) {
      const auto& previous = loop[(index + loop.size() - 1) % loop.size()];
      const auto& current = loop[index];
      const auto& next = loop[(index + 1) % loop.size()];
      const auto collinear =
          (previous.x() == current.x() && current.x() == next.x()) ||
          (previous.y() == current.y() && current.y() == next.y());
      if (!collinear) {
        simplified.append(current);
      }
    }
    if (simplified.size() < 3) {
      continue;
    }

    QVector<QPointF> pixel_points;
    pixel_points.reserve(simplified.size());
    for (const auto& point : simplified) {
      pixel_points.append(QPointF(point));
    }
    // Pixel selections retain every cell boundary, including tiny islands
    // and holes; uniform point subsampling would change the selected region.
    const auto& outline_points = pixel_points;
    const auto outline = outline_path_from_pixel_points(outline_points, QSize(width, height));
    if (outline.size() >= 4) {
      append_outline_path(&outline_paths, outline);
    }
  }
  return outline_paths;
}

QVariantList traced_outline_paths_for_alpha_mask_with_edge_padding(
    const QByteArray& data,
    int width,
    int height,
    bool preserve_pixel_edges) {
  auto outline_paths = traced_outline_paths_for_alpha_mask(data, width, height, preserve_pixel_edges);
  if (!outline_paths.isEmpty() || !valid_alpha_raster(data, width, height)) {
    return outline_paths;
  }

  const auto padded_width = width + 2;
  const auto padded_height = height + 2;
  QByteArray padded_alpha_mask(padded_width * padded_height, '\0');
  for (int y = 0; y < height; ++y) {
    std::memcpy(
        padded_alpha_mask.data() + ((y + 1) * padded_width) + 1,
        data.constData() + (y * width),
        static_cast<std::size_t>(width));
  }

  const auto padded_paths =
      traced_outline_paths_for_alpha_mask(padded_alpha_mask, padded_width, padded_height, preserve_pixel_edges);
  const double padded_x_scale = preserve_pixel_edges ? padded_width : padded_width - 1;
  const double padded_y_scale = preserve_pixel_edges ? padded_height : padded_height - 1;
  const double original_x_scale = preserve_pixel_edges ? width : width - 1;
  const double original_y_scale = preserve_pixel_edges ? height : height - 1;
  QVariantList remapped_paths;
  for (const auto& path_value : padded_paths) {
    QVariantList remapped_path;
    for (const auto& point_value : path_value.toList()) {
      const auto point = point_value.toMap();
      const auto padded_x =
          point.value(QStringLiteral("x")).toDouble() * padded_x_scale;
      const auto padded_y =
          point.value(QStringLiteral("y")).toDouble() * padded_y_scale;
      QVariantMap remapped_point;
      remapped_point.insert(
          QStringLiteral("x"),
          clamp_unit((padded_x - 1.0) / original_x_scale));
      remapped_point.insert(
          QStringLiteral("y"),
          clamp_unit((padded_y - 1.0) / original_y_scale));
      remapped_path.append(remapped_point);
    }
    if (remapped_path.size() >= 4) {
      append_outline_path(&remapped_paths, remapped_path);
    }
  }
  return remapped_paths;
}

}  // namespace photo_editor::desktop

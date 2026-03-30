#include "editor_preview_controller.h"

#include "app_bootstrap.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "vision_segmentation.h"

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <QtMath>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRect>
#include <QRectF>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <limits>
#include <vector>

namespace photo_editor::desktop {

namespace {

constexpr int kDefaultInteractivePreviewEdge = 960;
constexpr int kMinimumInteractivePreviewEdge = 960;
constexpr int kMaximumInteractivePreviewEdge = 1600;
constexpr int kHistogramPreviewEdge = 1024;
constexpr int kFullRenderDebounceMs = 420;
constexpr qint64 kMaxCacheMemoryBytes = 512 * 1024 * 1024;  // 512 MB
constexpr int kMaxEditHistoryDepth = 100;

struct DecodedImageCache final {
  QMutex mutex;
  QHash<QString, QImage> images;
  QList<QString> access_order;  // front = most recently used
  qint64 total_bytes = 0;

  void touch(const QString& key) {
    access_order.removeOne(key);
    access_order.prepend(key);
  }

  void insert(const QString& key, const QImage& image) {
    if (images.contains(key)) {
      total_bytes -= images.value(key).sizeInBytes();
      images.remove(key);
      access_order.removeOne(key);
    }
    const auto image_bytes = image.sizeInBytes();
    while (total_bytes + image_bytes > kMaxCacheMemoryBytes && !access_order.isEmpty()) {
      const auto& evict_key = access_order.last();
      total_bytes -= images.value(evict_key).sizeInBytes();
      images.remove(evict_key);
      access_order.removeLast();
    }
    images.insert(key, image);
    access_order.prepend(key);
    total_bytes += image_bytes;
  }
};

DecodedImageCache& decoded_image_cache() {
  static DecodedImageCache cache;
  return cache;
}

QString decoded_image_cache_key(const QFileInfo& file_info, int max_edge) {
  return QStringLiteral("%1|%2|%3|%4")
      .arg(file_info.absoluteFilePath())
      .arg(max_edge)
      .arg(file_info.size())
      .arg(file_info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch());
}

QString preview_cache_root() {
  const auto cache_root = AppBootstrap::cacheLocation();
  const auto base = cache_root.isEmpty() ? QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-cache")) : cache_root;
  return QDir(base).filePath(QStringLiteral("previews"));
}

QString versioned_file_url(const QString& file_path) {
  const QFileInfo file_info(file_path);
  auto url = QUrl::fromLocalFile(file_info.absoluteFilePath());
  return url.toString();
}

QString with_preview_revision(const QString& source, int revision) {
  if (source.isEmpty()) {
    return {};
  }

  auto url = QUrl(source);
  if (!url.isValid() || url.scheme().isEmpty()) {
    return source;
  }

  QUrlQuery query(url);
  query.removeAllQueryItems(QStringLiteral("rev"));
  query.addQueryItem(QStringLiteral("rev"), QString::number(revision));
  url.setQuery(query);
  return url.toString();
}

bool edit_states_equal(const EditState& left, const EditState& right) {
  return left == right;
}

bool preview_supported(const QString& file_path) {
  const QFileInfo file_info(file_path);
  const auto suffix = file_info.suffix().toLower().toUtf8();
  return QImageReader::supportedImageFormats().contains(suffix);
}

int clamp_channel(double value) {
  return qBound(0, static_cast<int>(qRound(value)), 255);
}

double clamp_signed_percent(double value) {
  return qBound(-100.0, value, 100.0);
}

double clamp_unit(double value) {
  return qBound(0.0, value, 1.0);
}

bool map_bool_value(const QVariantMap& map, const QString& key, bool fallback) {
  return map.contains(key) ? map.value(key).toBool() : fallback;
}

double map_double_value(const QVariantMap& map, const QString& key, double fallback) {
  return map.contains(key) ? map.value(key).toDouble() : fallback;
}

int clamp_preview_target_edge(int edge) {
  if (edge <= 0) {
    return kDefaultInteractivePreviewEdge;
  }
  return qBound(kMinimumInteractivePreviewEdge, edge, kMaximumInteractivePreviewEdge);
}

QSize scaled_size_for_max_edge(const QSize& source_size, int max_edge) {
  if (!source_size.isValid() || max_edge <= 0) {
    return {};
  }
  return source_size.scaled(max_edge, max_edge, Qt::KeepAspectRatio);
}

QByteArray union_segmentation_masks(
    const QByteArray& existing_data,
    int existing_width,
    int existing_height,
    const QByteArray& incoming_data,
    int incoming_width,
    int incoming_height) {
  if (incoming_data.isEmpty() || incoming_width <= 0 || incoming_height <= 0) {
    return existing_data;
  }

  if (existing_data.isEmpty() || existing_width != incoming_width || existing_height != incoming_height) {
    return incoming_data;
  }

  QByteArray merged(existing_data);
  const auto size = std::min(merged.size(), incoming_data.size());
  for (int index = 0; index < size; ++index) {
    merged[index] = static_cast<char>(std::max(
        static_cast<unsigned char>(merged.at(index)),
        static_cast<unsigned char>(incoming_data.at(index))));
  }
  return merged;
}

int instance_id_from_map(
    const QByteArray& instance_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0) {
    return 0;
  }

  const auto sample_instance = [&](double sample_y) {
    const auto x = std::clamp(
        static_cast<int>(std::lround(std::clamp(normalized_x, 0.0, 1.0) *
                                     std::max(width - 1, 0))),
        0,
        std::max(width - 1, 0));
    const auto y = std::clamp(
        static_cast<int>(std::lround(std::clamp(sample_y, 0.0, 1.0) *
                                     std::max(height - 1, 0))),
        0,
        std::max(height - 1, 0));
    return static_cast<int>(
        static_cast<unsigned char>(instance_data.at(y * width + x)));
  };

  const auto direct_instance = sample_instance(normalized_y);
  const auto flipped_instance = sample_instance(1.0 - normalized_y);
  if (direct_instance == 0) {
    return flipped_instance;
  }
  if (flipped_instance == 0 || direct_instance == flipped_instance) {
    return direct_instance;
  }
  return flipped_instance;
}

QVariantMap segmentation_mask_for_instance(
    const QByteArray& instance_data,
    int width,
    int height,
    int instance_id) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0 || instance_id <= 0) {
    return {};
  }

  QByteArray mask_data(width * height, '\0');
  auto has_pixels = false;
  for (int index = 0; index < instance_data.size() && index < mask_data.size(); ++index) {
    if (static_cast<unsigned char>(instance_data.at(index)) ==
        static_cast<unsigned char>(instance_id)) {
      mask_data[index] = static_cast<char>(0xff);
      has_pixels = true;
    }
  }

  if (!has_pixels) {
    return {};
  }

  QVariantMap mask;
  mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  mask.insert(QStringLiteral("segmentationData"), mask_data);
  mask.insert(QStringLiteral("segmentationWidth"), width);
  mask.insert(QStringLiteral("segmentationHeight"), height);
  return mask;
}

double polygon_cross(const QPointF& origin, const QPointF& left, const QPointF& right) {
  return (left.x() - origin.x()) * (right.y() - origin.y()) -
         (left.y() - origin.y()) * (right.x() - origin.x());
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

quint64 directed_edge_key(quint64 start, quint64 end) {
  return start ^ (end + 0x9e3779b97f4a7c15ULL + (start << 6) + (start >> 2));
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

QVariantList hull_points_for_segmentation_mask(const QVariantMap& mask) {
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (data.isEmpty() || width <= 0 || height <= 0) {
    return {};
  }

  const auto stride = std::max(1, std::max(width, height) / 96);
  QVector<QPointF> points;
  points.reserve((width / stride + 1) * (height / stride + 1));
  const auto sample_at = [&](int x, int y) {
    const auto clamped_x = qBound(0, x, width - 1);
    const auto clamped_y = qBound(0, y, height - 1);
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

QVariantList traced_outline_points_for_segmentation_mask(const QVariantMap& mask) {
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (data.isEmpty() || width <= 1 || height <= 1) {
    return {};
  }

  const auto sample_at = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return static_cast<unsigned char>(0);
    }
    return static_cast<unsigned char>(data.at(y * width + x));
  };

  QHash<quint64, QVector<quint64>> adjacency;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (sample_at(x, y) < 96) {
        continue;
      }
      if (sample_at(x, y - 1) < 96) {
        append_directed_boundary_edge(x, y, x + 1, y, &adjacency);
      }
      if (sample_at(x + 1, y) < 96) {
        append_directed_boundary_edge(x + 1, y, x + 1, y + 1, &adjacency);
      }
      if (sample_at(x, y + 1) < 96) {
        append_directed_boundary_edge(x + 1, y + 1, x, y + 1, &adjacency);
      }
      if (sample_at(x - 1, y) < 96) {
        append_directed_boundary_edge(x, y + 1, x, y, &adjacency);
      }
    }
  }

  if (adjacency.isEmpty()) {
    return hull_points_for_segmentation_mask(mask);
  }

  QSet<quint64> visited_edges;
  QVector<QPoint> best_loop;
  const auto starts = adjacency.keys();
  for (const auto start : starts) {
    const auto outgoing = adjacency.value(start);
    for (const auto initial_end : outgoing) {
      const auto initial_edge = directed_edge_key(start, initial_end);
      if (visited_edges.contains(initial_edge)) {
        continue;
      }

      QVector<QPoint> loop;
      auto current_start = start;
      auto current_end = initial_end;
      auto current_direction = cardinal_direction(current_start, current_end);

      while (true) {
        const auto edge = directed_edge_key(current_start, current_end);
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
          if (candidate == current_start) {
            continue;
          }
          const auto candidate_edge = directed_edge_key(current_end, candidate);
          if (visited_edges.contains(candidate_edge)) {
            continue;
          }
          const auto priority =
              turn_priority(current_direction, cardinal_direction(current_end, candidate));
          if (priority < best_priority) {
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

      if (loop.size() >= 4 && loop.first() == loop.last() && loop.size() > best_loop.size()) {
        best_loop = std::move(loop);
      }
    }
  }

  if (best_loop.size() < 4) {
    return hull_points_for_segmentation_mask(mask);
  }

  best_loop.removeLast();
  QVector<QPoint> simplified;
  simplified.reserve(best_loop.size());
  for (int index = 0; index < best_loop.size(); ++index) {
    const auto& previous = best_loop[(index + best_loop.size() - 1) % best_loop.size()];
    const auto& current = best_loop[index];
    const auto& next = best_loop[(index + 1) % best_loop.size()];
    const auto collinear =
        (previous.x() == current.x() && current.x() == next.x()) ||
        (previous.y() == current.y() && current.y() == next.y());
    if (!collinear) {
      simplified.append(current);
    }
  }

  if (simplified.size() < 3) {
    return hull_points_for_segmentation_mask(mask);
  }

  const auto step = std::max(1, static_cast<int>(simplified.size() / 320));
  QVariantList outline;
  for (int index = 0; index < simplified.size(); index += step) {
    const auto& point = simplified[index];
    QVariantMap point_map;
    point_map.insert(
        QStringLiteral("x"),
        clamp_unit(static_cast<double>(point.x()) / static_cast<double>(width)));
    point_map.insert(
        QStringLiteral("y"),
        clamp_unit(static_cast<double>(point.y()) / static_cast<double>(height)));
    outline.append(point_map);
  }

  if (outline.size() < 3) {
    return hull_points_for_segmentation_mask(mask);
  }
  return outline;
}

QVariantList outline_points_for_mask(const QVariantMap& mask) {
  const auto kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
  if (kind == QStringLiteral("polygon")) {
    return mask.value(QStringLiteral("polygonPoints")).toList();
  }
  if (kind == QStringLiteral("subject") || kind == QStringLiteral("object")) {
    return traced_outline_points_for_segmentation_mask(mask);
  }
  return {};
}

QVariantMap curve_point(double x, double y) {
  QVariantMap point;
  point.insert(QStringLiteral("x"), clamp_unit(x));
  point.insert(QStringLiteral("y"), clamp_unit(y));
  return point;
}

double curve_point_coordinate(const QVariant& value, const QString& key, double fallback) {
  const auto point = value.toMap();
  if (!point.contains(key)) {
    return clamp_unit(fallback);
  }
  return clamp_unit(point.value(key).toDouble());
}

QVariantList default_curve_points() {
  return {
      curve_point(0.0, 0.0),
      curve_point(1.0, 1.0),
  };
}

QVariantList parametric_curve_points(double shadows, double darks, double lights, double highlights) {
  const auto point_output = [](double x, double delta) {
    return clamp_unit(x + (delta / 100.0) * 0.34);
  };

  return {
      curve_point(0.0, 0.0),
      curve_point(0.20, point_output(0.20, shadows)),
      curve_point(0.40, point_output(0.40, darks)),
      curve_point(0.65, point_output(0.65, lights)),
      curve_point(0.85, point_output(0.85, highlights)),
      curve_point(1.0, 1.0),
  };
}

bool is_identity_curve_points(const QVariantList& points) {
  if (points.size() != 2) {
    return false;
  }

  return std::abs(curve_point_coordinate(points[0], QStringLiteral("x"), 0.0) - 0.0) <= 1e-6 &&
         std::abs(curve_point_coordinate(points[0], QStringLiteral("y"), 0.0) - 0.0) <= 1e-6 &&
         std::abs(curve_point_coordinate(points[1], QStringLiteral("x"), 1.0) - 1.0) <= 1e-6 &&
         std::abs(curve_point_coordinate(points[1], QStringLiteral("y"), 1.0) - 1.0) <= 1e-6;
}

QVariantList normalize_curve_points(const QVariantList& points) {
  std::vector<QVariantMap> interior_points;
  interior_points.reserve(static_cast<std::size_t>(points.size()));
  for (const auto& value : points) {
    const auto point = value.toMap();
    if (point.isEmpty()) {
      continue;
    }
    interior_points.push_back(curve_point(
        point.value(QStringLiteral("x")).toDouble(),
        point.value(QStringLiteral("y")).toDouble()));
  }

  std::sort(interior_points.begin(), interior_points.end(), [](const QVariantMap& left, const QVariantMap& right) {
    return left.value(QStringLiteral("x")).toDouble() < right.value(QStringLiteral("x")).toDouble();
  });

  QVariantList normalized;
  normalized.append(curve_point(0.0, 0.0));
  auto previous_x = 0.0;
  for (const auto& point : interior_points) {
    auto x = clamp_unit(point.value(QStringLiteral("x")).toDouble());
    auto y = clamp_unit(point.value(QStringLiteral("y")).toDouble());
    if (x <= 0.0 + 1e-6 || x >= 1.0 - 1e-6) {
      continue;
    }

    x = std::max(previous_x + 0.001, x);
    x = std::min(0.999, x);
    if (x >= 0.999) {
      continue;
    }

    normalized.append(curve_point(x, y));
    previous_x = x;
  }
  normalized.append(curve_point(1.0, 1.0));

  if (is_identity_curve_points(normalized)) {
    return {};
  }
  return normalized;
}

std::vector<photo_editor::pipeline::CurvePoint> pipeline_curve_points(const QVariantList& points) {
  std::vector<photo_editor::pipeline::CurvePoint> curve_points;
  curve_points.reserve(static_cast<std::size_t>(points.size()));
  for (const auto& value : points) {
    curve_points.push_back(photo_editor::pipeline::CurvePoint{
        .x = curve_point_coordinate(value, QStringLiteral("x"), 0.0),
        .y = curve_point_coordinate(value, QStringLiteral("y"), 0.0),
    });
  }
  return curve_points;
}

std::vector<photo_editor::pipeline::LocalMask> pipeline_local_masks(const QVariantList& masks) {
  std::vector<photo_editor::pipeline::LocalMask> local_masks;
  local_masks.reserve(static_cast<std::size_t>(masks.size()));
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    photo_editor::pipeline::LocalMask local_mask;
    local_mask.id = mask.value(QStringLiteral("id")).toString().toStdString();
    local_mask.name = mask.value(QStringLiteral("name")).toString().toStdString();
    local_mask.kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString()).toStdString();
    local_mask.operation =
        normalized_mask_operation(mask.value(QStringLiteral("operation")).toString()).toStdString();
    local_mask.retouch_mode = mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower().toStdString();
    local_mask.enabled = map_bool_value(mask, QStringLiteral("enabled"), true);
    local_mask.exposure = map_double_value(mask, QStringLiteral("exposure"), 0.0);
    local_mask.temperature = map_double_value(mask, QStringLiteral("temperature"), 0.0);
    local_mask.hue_shift = map_double_value(mask, QStringLiteral("hueShift"), 0.0);
    local_mask.saturation_shift = map_double_value(mask, QStringLiteral("saturationShift"), 0.0);
    local_mask.colorize_hue = map_double_value(mask, QStringLiteral("colorizeHue"), 0.0);
    local_mask.colorize_strength = map_double_value(mask, QStringLiteral("colorizeStrength"), 0.0);
    local_mask.remove_strength = map_double_value(mask, QStringLiteral("removeStrength"), 0.0);
    local_mask.selection_invert = map_bool_value(mask, QStringLiteral("selectionInvert"), false);
    local_mask.selection_expand = map_double_value(mask, QStringLiteral("selectionExpand"), 0.0);
    local_mask.feather = map_double_value(
        mask, QStringLiteral("feather"), local_mask.kind == "radial" ? 0.25 : 0.22);
    local_mask.rotation = map_double_value(mask, QStringLiteral("rotation"), 0.0);
    if (local_mask.kind == "radial") {
      local_mask.center_x = map_double_value(mask, QStringLiteral("centerX"), 0.5);
      local_mask.center_y = map_double_value(mask, QStringLiteral("centerY"), 0.5);
      local_mask.radius_x = map_double_value(mask, QStringLiteral("radiusX"), 0.25);
      local_mask.radius_y = map_double_value(mask, QStringLiteral("radiusY"), 0.25);
    } else if (local_mask.kind == "linear") {
      local_mask.position = map_double_value(mask, QStringLiteral("position"), 0.5);
    } else if (local_mask.kind == "brush") {
      local_mask.feather = map_double_value(mask, QStringLiteral("feather"), 0.75);
      local_mask.brush_size = map_double_value(mask, QStringLiteral("brushSize"), 0.08);
      local_mask.brush_flow = map_double_value(mask, QStringLiteral("brushFlow"), 1.0);
      local_mask.clone_source_set = map_bool_value(mask, QStringLiteral("cloneSourceSet"), false);
      local_mask.clone_source_x = map_double_value(mask, QStringLiteral("cloneSourceX"), 0.5);
      local_mask.clone_source_y = map_double_value(mask, QStringLiteral("cloneSourceY"), 0.5);
      local_mask.clone_offset_x = map_double_value(mask, QStringLiteral("cloneOffsetX"), 0.0);
      local_mask.clone_offset_y = map_double_value(mask, QStringLiteral("cloneOffsetY"), 0.0);
      local_mask.clone_scale = map_double_value(mask, QStringLiteral("cloneScale"), 1.0);
      local_mask.clone_rotation = map_double_value(mask, QStringLiteral("cloneRotation"), 0.0);
      const auto points = mask.value(QStringLiteral("brushPoints")).toList();
      local_mask.brush_points.reserve(static_cast<std::size_t>(points.size()));
      for (const auto& point_value : points) {
        const auto point = point_value.toMap();
        local_mask.brush_points.push_back(photo_editor::pipeline::BrushPoint{
            .x = map_double_value(point, QStringLiteral("x"), 0.5),
            .y = map_double_value(point, QStringLiteral("y"), 0.5),
            .size = map_double_value(point, QStringLiteral("size"), local_mask.brush_size),
            .flow = map_double_value(point, QStringLiteral("flow"), local_mask.brush_flow),
        });
      }
    } else if (local_mask.kind == "polygon") {
      local_mask.feather = map_double_value(mask, QStringLiteral("feather"), 0.06);
      const auto points = mask.value(QStringLiteral("polygonPoints")).toList();
      local_mask.polygon_points.reserve(static_cast<std::size_t>(points.size()));
      for (const auto& point_value : points) {
        const auto point = point_value.toMap();
        local_mask.polygon_points.push_back(photo_editor::pipeline::CurvePoint{
            .x = map_double_value(point, QStringLiteral("x"), 0.5),
            .y = map_double_value(point, QStringLiteral("y"), 0.5),
        });
      }
    } else if (local_mask.kind == "luminance") {
      local_mask.luminance_min = map_double_value(mask, QStringLiteral("luminanceMin"), 0.2);
      local_mask.luminance_max = map_double_value(mask, QStringLiteral("luminanceMax"), 0.8);
      local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.1);
    } else if (local_mask.kind == "color") {
      local_mask.hue_center = map_double_value(mask, QStringLiteral("hueCenter"), 0.08);
      local_mask.hue_range = map_double_value(mask, QStringLiteral("hueRange"), 0.12);
      local_mask.saturation_min = map_double_value(mask, QStringLiteral("saturationMin"), 0.1);
      local_mask.saturation_max = map_double_value(mask, QStringLiteral("saturationMax"), 1.0);
      local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.08);
    } else if (local_mask.kind == "subject" || local_mask.kind == "object") {
      const auto seg_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
      const auto seg_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
      const auto seg_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
      if (!seg_data.isEmpty() && seg_width > 0 && seg_height > 0) {
        local_mask.segmentation_mask.resize(static_cast<std::size_t>(seg_data.size()));
        std::memcpy(local_mask.segmentation_mask.data(),
                    seg_data.constData(),
                    static_cast<std::size_t>(seg_data.size()));
        local_mask.segmentation_width = seg_width;
        local_mask.segmentation_height = seg_height;
      }
    }
    local_masks.push_back(std::move(local_mask));
    ++ordinal;
  }
  return local_masks;
}

photo_editor::pipeline::ColorAdjustmentState to_pipeline_state(const EditState& state) {
  return photo_editor::pipeline::ColorAdjustmentState{
      .exposure = state.exposure,
      .contrast = state.contrast,
      .highlights = state.highlights,
      .shadows = state.shadows,
      .whites = state.whites,
      .blacks = state.blacks,
      .temperature = state.temperature,
      .tint = state.tint,
      .texture = state.texture,
      .clarity = state.clarity,
      .dehaze = state.dehaze,
      .sharpening = state.sharpening,
      .sharpen_masking = state.sharpen_masking,
      .luminance_nr = state.luminance_nr,
      .color_nr = state.color_nr,
      .lens_distortion = state.lens_distortion,
      .lens_vignette = state.lens_vignette,
      .chromatic_aberration = state.chromatic_aberration,
      .vibrance = state.vibrance,
      .saturation = state.saturation,
      .curve_shadows = state.curve_shadows,
      .curve_darks = state.curve_darks,
      .curve_lights = state.curve_lights,
      .curve_highlights = state.curve_highlights,
      .master_curve_points = pipeline_curve_points(state.master_curve_points),
      .red_curve_points = pipeline_curve_points(state.red_curve_points),
      .green_curve_points = pipeline_curve_points(state.green_curve_points),
      .blue_curve_points = pipeline_curve_points(state.blue_curve_points),
      .color_mixer_channel = state.hsl_channel.toStdString(),
      .hue_adjustment = state.hue_adjustment,
      .saturation_adjustment = state.saturation_adjustment,
      .luminance_adjustment = state.luminance_adjustment,
      .color_mixer_channels = [&]() {
        std::array<photo_editor::pipeline::ColorMixerChannelAdjustment,
                   photo_editor::pipeline::kColorMixerChannelCount>
            channels{};
        for (std::size_t i = 0; i < channels.size(); ++i) {
          channels[i].hue = state.mixer_channels[i].hue;
          channels[i].saturation = state.mixer_channels[i].saturation;
          channels[i].luminance = state.mixer_channels[i].luminance;
        }
        return channels;
      }(),
      .grading_shadows_hue = state.grading_shadows_hue,
      .grading_shadows_saturation = state.grading_shadows_saturation,
      .grading_shadows_luminance = state.grading_shadows_luminance,
      .grading_midtones_hue = state.grading_midtones_hue,
      .grading_midtones_saturation = state.grading_midtones_saturation,
      .grading_midtones_luminance = state.grading_midtones_luminance,
      .grading_highlights_hue = state.grading_highlights_hue,
      .grading_highlights_saturation = state.grading_highlights_saturation,
      .grading_highlights_luminance = state.grading_highlights_luminance,
      .linear_mask_enabled = state.linear_mask_enabled,
      .linear_mask_exposure = state.linear_mask_exposure,
      .linear_mask_temperature = state.linear_mask_temperature,
      .linear_mask_feather = state.linear_mask_feather,
      .linear_mask_position = state.linear_mask_position,
      .linear_mask_rotation = state.linear_mask_rotation,
      .radial_mask_enabled = state.radial_mask_enabled,
      .radial_mask_exposure = state.radial_mask_exposure,
      .radial_mask_temperature = state.radial_mask_temperature,
      .radial_mask_feather = state.radial_mask_feather,
      .radial_mask_center_x = state.radial_mask_center_x,
      .radial_mask_center_y = state.radial_mask_center_y,
      .radial_mask_radius_x = state.radial_mask_radius_x,
      .radial_mask_radius_y = state.radial_mask_radius_y,
      .radial_mask_rotation = state.radial_mask_rotation,
      .local_masks = pipeline_local_masks(state.mask_stack),
  };
}

double srgb_to_linear(double value) { return photo_editor::pipeline::srgb_to_linear(value); }

double linear_to_srgb(double value) { return photo_editor::pipeline::linear_to_srgb(value); }

double channel_weight_for_selection(double hue, const QString& hsl_channel) {
  return photo_editor::pipeline::channel_weight_for_selection(hue, hsl_channel.toStdString());
}

double crop_aspect_ratio(const QString& preset) {
  if (preset == QStringLiteral("Square")) {
    return 1.0;
  }

  if (preset == QStringLiteral("4x5")) {
    return 4.0 / 5.0;
  }

  if (preset == QStringLiteral("3x2")) {
    return 3.0 / 2.0;
  }

  if (preset == QStringLiteral("16x9")) {
    return 16.0 / 9.0;
  }

  return 0.0;
}

bool has_explicit_crop_rect(const EditState& state) {
  return state.crop_rect_width > 0.0 && state.crop_rect_height > 0.0;
}

QRect legacy_crop_rect_for_image(const QSize& size, const QString& preset, double crop_zoom) {
  int crop_width = size.width();
  int crop_height = size.height();

  const auto aspect_ratio = crop_aspect_ratio(preset);
  if (aspect_ratio > 0.0) {
    const auto current_ratio = static_cast<double>(size.width()) / static_cast<double>(size.height());
    if (current_ratio > aspect_ratio) {
      crop_width = static_cast<int>(qRound(static_cast<double>(size.height()) * aspect_ratio));
    } else {
      crop_height = static_cast<int>(qRound(static_cast<double>(size.width()) / aspect_ratio));
    }
  }

  crop_width = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_width) / crop_zoom)));
  crop_height = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_height) / crop_zoom)));

  const auto x = qMax(0, (size.width() - crop_width) / 2);
  const auto y = qMax(0, (size.height() - crop_height) / 2);
  return {x, y, crop_width, crop_height};
}

QRectF normalized_crop_rect_for_state(const QSize& size, const EditState& state) {
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
    return QRectF(0.0, 0.0, 1.0, 1.0);
  }

  if (has_explicit_crop_rect(state)) {
    const auto width = clamp_unit(state.crop_rect_width);
    const auto height = clamp_unit(state.crop_rect_height);
    const auto x = clamp_unit(state.crop_rect_x);
    const auto y = clamp_unit(state.crop_rect_y);
    return QRectF(
        std::min(x, std::max(0.0, 1.0 - width)),
        std::min(y, std::max(0.0, 1.0 - height)),
        width,
        height);
  }

  const auto legacy = legacy_crop_rect_for_image(size, state.crop_preset, state.crop_zoom);
  return QRectF(
      static_cast<double>(legacy.x()) / static_cast<double>(size.width()),
      static_cast<double>(legacy.y()) / static_cast<double>(size.height()),
      static_cast<double>(legacy.width()) / static_cast<double>(size.width()),
      static_cast<double>(legacy.height()) / static_cast<double>(size.height()));
}

QRect crop_rect_for_image(const QSize& size, const EditState& state) {
  if (!size.isValid()) {
    return {};
  }

  const auto normalized = normalized_crop_rect_for_state(size, state);
  const auto x = qBound(0, static_cast<int>(qRound(normalized.x() * static_cast<double>(size.width()))), size.width() - 1);
  const auto y = qBound(0, static_cast<int>(qRound(normalized.y() * static_cast<double>(size.height()))), size.height() - 1);
  const auto width = qBound(1, static_cast<int>(qRound(normalized.width() * static_cast<double>(size.width()))), size.width() - x);
  const auto height = qBound(1, static_cast<int>(qRound(normalized.height() * static_cast<double>(size.height()))), size.height() - y);
  return {x, y, width, height};
}

bool has_visible_adjustments(const EditState& state) {
  return !qFuzzyIsNull(state.exposure) ||
         !qFuzzyIsNull(state.contrast) ||
         !qFuzzyIsNull(state.highlights) ||
         !qFuzzyIsNull(state.shadows) ||
         !qFuzzyIsNull(state.whites) ||
         !qFuzzyIsNull(state.blacks) ||
         !qFuzzyIsNull(state.temperature) ||
         !qFuzzyIsNull(state.tint) ||
         !qFuzzyIsNull(state.texture) ||
         !qFuzzyIsNull(state.clarity) ||
         !qFuzzyIsNull(state.dehaze) ||
         !qFuzzyIsNull(state.vibrance) ||
         !qFuzzyIsNull(state.saturation) ||
         !qFuzzyIsNull(state.lens_distortion) ||
         !qFuzzyIsNull(state.lens_vignette) ||
         !qFuzzyIsNull(state.chromatic_aberration) ||
         !qFuzzyIsNull(state.curve_shadows) ||
         !qFuzzyIsNull(state.curve_darks) ||
         !qFuzzyIsNull(state.curve_lights) ||
         !qFuzzyIsNull(state.curve_highlights) ||
         !state.master_curve_points.isEmpty() ||
         !state.red_curve_points.isEmpty() ||
         !state.green_curve_points.isEmpty() ||
         !state.blue_curve_points.isEmpty() ||
         !qFuzzyIsNull(state.hue_adjustment) ||
         !qFuzzyIsNull(state.saturation_adjustment) ||
         !qFuzzyIsNull(state.luminance_adjustment) ||
         has_explicit_crop_rect(state) ||
         !qFuzzyCompare(state.crop_zoom, 1.0) ||
         state.crop_preset != QStringLiteral("Original") ||
         !state.mask_stack.isEmpty() ||
         state.linear_mask_enabled ||
         !qFuzzyIsNull(state.linear_mask_exposure) ||
         !qFuzzyIsNull(state.linear_mask_temperature) ||
         !qFuzzyCompare(state.linear_mask_feather, 0.22) ||
         !qFuzzyCompare(state.linear_mask_position, 0.5) ||
         !qFuzzyIsNull(state.linear_mask_rotation) ||
         state.radial_mask_enabled ||
         !qFuzzyIsNull(state.radial_mask_exposure) ||
         !qFuzzyIsNull(state.radial_mask_temperature) ||
         !qFuzzyCompare(state.radial_mask_feather, 0.25) ||
         !qFuzzyCompare(state.radial_mask_center_x, 0.5) ||
         !qFuzzyCompare(state.radial_mask_center_y, 0.5) ||
         !qFuzzyCompare(state.radial_mask_radius_x, 0.25) ||
         !qFuzzyCompare(state.radial_mask_radius_y, 0.25) ||
         !qFuzzyIsNull(state.radial_mask_rotation);
}

bool decode_source_image(const QString& file_path, QImage* image, int max_edge) {
  if (!preview_supported(file_path)) {
    return false;
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }

  const auto should_cache = max_edge > 0;
  const auto cache_key = should_cache ? decoded_image_cache_key(source_info, max_edge) : QString{};
  if (should_cache) {
    auto& cache = decoded_image_cache();
    QMutexLocker locker(&cache.mutex);
    const auto cached = cache.images.constFind(cache_key);
    if (cached != cache.images.cend()) {
      *image = cached.value();
      cache.touch(cache_key);
      return !image->isNull();
    }
  }

  QImageReader reader(file_path);
  reader.setAutoTransform(true);
  if (!reader.canRead()) {
    return false;
  }

  if (max_edge > 0) {
    const auto source_size = reader.size();
    if (source_size.isValid()) {
      const auto target_size = scaled_size_for_max_edge(source_size, max_edge);
      if (target_size.isValid() && target_size != source_size) {
        reader.setScaledSize(target_size);
      }
    }
  }

  auto decoded = reader.read();
  if (decoded.isNull()) {
    return false;
  }

  *image = decoded.convertToFormat(QImage::Format_ARGB32);
  if (should_cache && !image->isNull()) {
    auto& cache = decoded_image_cache();
    QMutexLocker locker(&cache.mutex);
    cache.insert(cache_key, *image);
  }
  return !image->isNull();
}

QImage apply_crop(const QImage& image, const EditState& state) {
  return image.copy(crop_rect_for_image(image.size(), state));
}

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
photo_editor::pipeline::metal::MetalPipelineBackend& metal_backend() {
  static photo_editor::pipeline::metal::MetalPipelineBackend backend;
  return backend;
}
#endif

void apply_color_adjustments(QImage* image, const EditState& state) {
  const auto width = image->width();
  const auto height = image->height();
  std::vector<photo_editor::pipeline::ColorPixel> pixels(static_cast<std::size_t>(width * height));

  for (int y = 0; y < height; ++y) {
    const auto* scanline = reinterpret_cast<const QRgb*>(image->constScanLine(y));
    for (int x = 0; x < width; ++x) {
      const QColor color = QColor::fromRgb(scanline[x]);
      pixels[static_cast<std::size_t>(y * width + x)] = photo_editor::pipeline::ColorPixel{
          .red = static_cast<float>(srgb_to_linear(color.redF())),
          .green = static_cast<float>(srgb_to_linear(color.greenF())),
          .blue = static_cast<float>(srgb_to_linear(color.blueF())),
          .alpha = color.alphaF(),
      };
    }
  }

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = metal_backend();
  const auto pipeline_state = to_pipeline_state(state);
  const auto requires_cpu_pipeline =
      std::abs(state.texture) > 1e-6 || std::abs(state.clarity) > 1e-6 || std::abs(state.dehaze) > 1e-6 ||
      std::abs(state.sharpening) > 1e-6 || std::abs(state.sharpen_masking) > 1e-6 ||
      std::abs(state.luminance_nr) > 1e-6 || std::abs(state.color_nr) > 1e-6 ||
      std::abs(state.lens_distortion) > 1e-6 || std::abs(state.lens_vignette) > 1e-6 ||
      std::abs(state.chromatic_aberration) > 1e-6;
  if (metal.available() && pipeline_state.local_masks.empty() && !requires_cpu_pipeline) {
    const auto lut = photo_editor::pipeline::build_color_lut(pipeline_state);
    metal.apply_masks(&pixels, width, height, pipeline_state);
    metal.apply_color_lut(&pixels, width, height, lut);
  } else {
    photo_editor::pipeline::apply_color_adjustments(&pixels, width, height, pipeline_state);
  }
#else
  photo_editor::pipeline::apply_color_adjustments(&pixels, width, height, to_pipeline_state(state));
#endif

  for (int y = 0; y < height; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(image->scanLine(y));
    for (int x = 0; x < width; ++x) {
      auto& pixel = pixels[static_cast<std::size_t>(y * width + x)];
      scanline[x] = qRgba(
          clamp_channel(linear_to_srgb(pixel.red) * 255.0),
          clamp_channel(linear_to_srgb(pixel.green) * 255.0),
          clamp_channel(linear_to_srgb(pixel.blue) * 255.0),
          clamp_channel(clamp_unit(pixel.alpha) * 255.0));
    }
  }
}

QString cache_path_for_selection(const QString& file_path, const EditState& state, bool edited, bool full_resolution) {
  const auto fingerprint = QStringLiteral("%1|%2|%3|%4|%5")
                               .arg(file_path)
                               .arg(QStringLiteral("pipeline-v3"))
                               .arg(edited ? QStringLiteral("edited") : QStringLiteral("reference"))
                               .arg(full_resolution ? QStringLiteral("full") : QStringLiteral("interactive"))
                               .arg(serialize_edit_recipe_json(state));
  const auto digest =
      QCryptographicHash::hash(fingerprint.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QDir(preview_cache_root()).filePath(QStringLiteral("%1.jpg").arg(QString::fromLatin1(digest)));
}

QString cache_path_for_selection(
    const QString& file_path,
    const EditState& state,
    bool edited,
    bool full_resolution,
    bool bypass_crop) {
  return cache_path_for_selection(
      file_path + (bypass_crop ? QStringLiteral("|crop-editing") : QString()),
      state,
      edited,
      full_resolution);
}

RenderResult render_preview_request(const RenderRequest& request) {
  RenderResult result;
  result.full_resolution = request.full_resolution;
  result.generation = request.generation;

  QImage source_image;
  if (!decode_source_image(request.file_path, &source_image, request.max_edge)) {
    result.status = QStringLiteral("Source file unavailable.");
    return result;
  }

  QDir().mkpath(preview_cache_root());

  QImage edited_image = request.bypass_crop ? source_image : apply_crop(source_image, request.state);
  if (has_visible_adjustments(request.state)) {
    apply_color_adjustments(&edited_image, request.state);
  }

  const auto output_path =
      cache_path_for_selection(request.file_path, request.state, true, request.full_resolution, request.bypass_crop);
  QImageWriter writer(output_path, "jpg");
  writer.setQuality(request.jpeg_quality);
  writer.setOptimizedWrite(false);
  if (!writer.write(edited_image)) {
    result.status = QStringLiteral("Unable to write preview cache.");
    return result;
  }

  result.preview_source = versioned_file_url(output_path);
  result.histogram_image =
      edited_image.width() > kHistogramPreviewEdge || edited_image.height() > kHistogramPreviewEdge
          ? edited_image.scaled(
                kHistogramPreviewEdge,
                kHistogramPreviewEdge,
                Qt::KeepAspectRatio,
                Qt::FastTransformation)
          : edited_image;

  if (request.compare_mode) {
    QImage reference_image = request.bypass_crop ? source_image : apply_crop(source_image, request.state);
    const auto reference_output_path =
        cache_path_for_selection(request.file_path, request.state, false, request.full_resolution, request.bypass_crop);
    QImageWriter reference_writer(reference_output_path, "jpg");
    reference_writer.setQuality(request.jpeg_quality);
    reference_writer.setOptimizedWrite(false);
    if (!reference_writer.write(reference_image)) {
      result.status = QStringLiteral("Unable to write reference preview cache.");
      return result;
    }
    result.reference_preview_source = versioned_file_url(reference_output_path);
  }

  result.success = true;
  if (has_visible_adjustments(request.state)) {
    result.status = request.full_resolution ? QStringLiteral("Full-resolution preview ready.")
                                            : QStringLiteral("Interactive preview ready.");
  } else {
    result.status = request.full_resolution ? QStringLiteral("Full-resolution base preview ready.")
                                            : QStringLiteral("Base preview ready.");
  }
  return result;
}

}  // namespace

EditorPreviewController::EditorPreviewController(QObject* parent) : QObject(parent) {
  loadPresets();

  interactive_render_timer_.setSingleShot(true);
  interactive_render_timer_.setInterval(20);
  connect(&interactive_render_timer_, &QTimer::timeout, this, [this]() { renderPreviewStage(false); });

  full_render_timer_.setSingleShot(true);
  full_render_timer_.setInterval(kFullRenderDebounceMs);
  connect(&full_render_timer_, &QTimer::timeout, this, [this]() { renderPreviewStage(true); });

  connect(&interactive_render_watcher_, &QFutureWatcher<RenderResult>::finished, this, [this]() {
    handleRenderFinished(false);
  });
  connect(&full_render_watcher_, &QFutureWatcher<RenderResult>::finished, this, [this]() {
    handleRenderFinished(true);
  });

  persist_timer_.setSingleShot(true);
  persist_timer_.setInterval(350);
  connect(&persist_timer_, &QTimer::timeout, this, &EditorPreviewController::flushPersistedState);
  connect(&persist_process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
    setStatus(QStringLiteral("Edit recipe save is unavailable."));
    refreshDebugInfo();
  });
}

QString EditorPreviewController::previewSource() const { return preview_source_; }
QString EditorPreviewController::referencePreviewSource() const { return reference_preview_source_; }
QVariantList EditorPreviewController::histogramRed() const { return histogram_red_; }
QVariantList EditorPreviewController::histogramGreen() const { return histogram_green_; }
QVariantList EditorPreviewController::histogramBlue() const { return histogram_blue_; }
QVariantList EditorPreviewController::histogramLuminance() const { return histogram_luminance_; }
int EditorPreviewController::previewTargetEdge() const { return clamp_preview_target_edge(preview_target_edge_); }
QString EditorPreviewController::debugInfo() const { return debug_info_; }
QString EditorPreviewController::status() const { return status_; }
QString EditorPreviewController::cameraLabel() const { return camera_label_; }
QString EditorPreviewController::captureDate() const { return capture_date_; }
QString EditorPreviewController::lensModel() const { return lens_model_; }
bool EditorPreviewController::hasAsset() const { return !file_path_.isEmpty() || !fallback_thumbnail_source_.isEmpty(); }
bool EditorPreviewController::subjectMaskAvailable() const { return vision_segmentation_available(); }
bool EditorPreviewController::processing() const { return processing_; }
bool EditorPreviewController::canUndo() const {
  return !asset_id_.isEmpty() && !asset_undo_states_.value(asset_id_).isEmpty();
}
bool EditorPreviewController::canRedo() const {
  return !asset_id_.isEmpty() && !asset_redo_states_.value(asset_id_).isEmpty();
}
bool EditorPreviewController::hasCopiedSettings() const { return has_copied_settings_; }
QStringList EditorPreviewController::presetNames() const { return presets_.keys(); }
bool EditorPreviewController::compareMode() const { return compare_mode_; }
bool EditorPreviewController::cropEditing() const { return crop_editing_; }
double EditorPreviewController::exposure() const { return exposure_; }
double EditorPreviewController::contrast() const { return contrast_; }
double EditorPreviewController::highlights() const { return highlights_; }
double EditorPreviewController::shadows() const { return shadows_; }
double EditorPreviewController::whites() const { return whites_; }
double EditorPreviewController::blacks() const { return blacks_; }
double EditorPreviewController::temperature() const { return temperature_; }
double EditorPreviewController::tint() const { return tint_; }
double EditorPreviewController::texture() const { return texture_; }
double EditorPreviewController::clarity() const { return clarity_; }
double EditorPreviewController::dehaze() const { return dehaze_; }
double EditorPreviewController::sharpening() const { return sharpening_; }
double EditorPreviewController::sharpenMasking() const { return sharpen_masking_; }
double EditorPreviewController::luminanceNr() const { return luminance_nr_; }
double EditorPreviewController::colorNr() const { return color_nr_; }
double EditorPreviewController::lensDistortion() const { return lens_distortion_; }
double EditorPreviewController::lensVignette() const { return lens_vignette_; }
double EditorPreviewController::chromaticAberration() const { return chromatic_aberration_; }
double EditorPreviewController::vibrance() const { return vibrance_; }
double EditorPreviewController::saturation() const { return saturation_; }
double EditorPreviewController::curveShadows() const { return curve_shadows_; }
double EditorPreviewController::curveDarks() const { return curve_darks_; }
double EditorPreviewController::curveLights() const { return curve_lights_; }
double EditorPreviewController::curveHighlights() const { return curve_highlights_; }
QString EditorPreviewController::curveChannel() const { return curve_channel_; }
QVariantList EditorPreviewController::activeCurvePoints() const {
  const auto normalized_channel = curve_channel_.trimmed().isEmpty() ? QStringLiteral("Master") : curve_channel_;
  if (normalized_channel == QStringLiteral("Master")) {
    if (!master_curve_points_.isEmpty()) {
      return master_curve_points_;
    }
    if (!qFuzzyIsNull(curve_shadows_) || !qFuzzyIsNull(curve_darks_) ||
        !qFuzzyIsNull(curve_lights_) || !qFuzzyIsNull(curve_highlights_)) {
      return parametric_curve_points(curve_shadows_, curve_darks_, curve_lights_, curve_highlights_);
    }
    return default_curve_points();
  }
  if (normalized_channel == QStringLiteral("Red")) {
    return red_curve_points_.isEmpty() ? default_curve_points() : red_curve_points_;
  }
  if (normalized_channel == QStringLiteral("Green")) {
    return green_curve_points_.isEmpty() ? default_curve_points() : green_curve_points_;
  }
  return blue_curve_points_.isEmpty() ? default_curve_points() : blue_curve_points_;
}
QString EditorPreviewController::hslChannel() const { return hsl_channel_; }
double EditorPreviewController::hueAdjustment() const { return hue_adjustment_; }
double EditorPreviewController::saturationAdjustment() const { return saturation_adjustment_; }
double EditorPreviewController::luminanceAdjustment() const { return luminance_adjustment_; }
double EditorPreviewController::gradingShadowsHue() const { return grading_shadows_hue_; }
double EditorPreviewController::gradingShadowsSaturation() const { return grading_shadows_saturation_; }
double EditorPreviewController::gradingShadowsLuminance() const { return grading_shadows_luminance_; }
double EditorPreviewController::gradingMidtonesHue() const { return grading_midtones_hue_; }
double EditorPreviewController::gradingMidtonesSaturation() const { return grading_midtones_saturation_; }
double EditorPreviewController::gradingMidtonesLuminance() const { return grading_midtones_luminance_; }
double EditorPreviewController::gradingHighlightsHue() const { return grading_highlights_hue_; }
double EditorPreviewController::gradingHighlightsSaturation() const { return grading_highlights_saturation_; }
double EditorPreviewController::gradingHighlightsLuminance() const { return grading_highlights_luminance_; }
double EditorPreviewController::cropRectX() const {
  return normalized_crop_rect_for_state(sourceImageSize(), currentState()).x();
}
double EditorPreviewController::cropRectY() const {
  return normalized_crop_rect_for_state(sourceImageSize(), currentState()).y();
}
double EditorPreviewController::cropRectWidth() const {
  return normalized_crop_rect_for_state(sourceImageSize(), currentState()).width();
}
double EditorPreviewController::cropRectHeight() const {
  return normalized_crop_rect_for_state(sourceImageSize(), currentState()).height();
}
double EditorPreviewController::cropAspectRatio() const { return crop_aspect_ratio(crop_preset_); }
double EditorPreviewController::cropZoom() const { return crop_zoom_; }
QString EditorPreviewController::cropPreset() const { return crop_preset_; }
QVariantList EditorPreviewController::maskStack() const { return mask_stack_; }
int EditorPreviewController::maskCount() const { return static_cast<int>(mask_stack_.size()); }
int EditorPreviewController::activeMaskIndex() const { return active_mask_index_; }
bool EditorPreviewController::hasActiveMask() const {
  return active_mask_index_ >= 0 && active_mask_index_ < mask_stack_.size();
}
QString EditorPreviewController::activeMaskKind() const {
  return activeMaskDefinition().value(QStringLiteral("kind")).toString();
}
QString EditorPreviewController::activeMaskRetouchMode() const {
  return activeMaskDefinition().value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
}
QString EditorPreviewController::activeMaskName() const {
  return activeMaskDefinition().value(QStringLiteral("name")).toString();
}
QString EditorPreviewController::activeMaskOperation() const {
  return activeMaskDefinition().value(QStringLiteral("operation")).toString();
}
bool EditorPreviewController::activeMaskEnabled() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("enabled"), false);
}
double EditorPreviewController::activeMaskExposure() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("exposure"), 0.0);
}
double EditorPreviewController::activeMaskTemperature() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("temperature"), 0.0);
}
double EditorPreviewController::activeMaskHueShift() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("hueShift"), 0.0);
}
double EditorPreviewController::activeMaskSaturationShift() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("saturationShift"), 0.0);
}
double EditorPreviewController::activeMaskColorizeHue() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("colorizeHue"), 0.0);
}
double EditorPreviewController::activeMaskColorizeStrength() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("colorizeStrength"), 0.0);
}
double EditorPreviewController::activeMaskRemoveStrength() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("removeStrength"), 0.0);
}
bool EditorPreviewController::activeMaskSelectionInverted() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("selectionInvert"), false);
}
double EditorPreviewController::activeMaskSelectionExpand() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("selectionExpand"), 0.0);
}
double EditorPreviewController::activeMaskFeather() const {
  const auto mask = activeMaskDefinition();
  return map_double_value(
      mask, QStringLiteral("feather"),
      activeMaskKind() == QStringLiteral("radial") ? 0.25
      : activeMaskKind() == QStringLiteral("brush") ? 0.75
      : activeMaskKind() == QStringLiteral("polygon") ? 0.06
      : activeMaskKind() == QStringLiteral("subject") || activeMaskKind() == QStringLiteral("object") ? 0.5
      : 0.22);
}
double EditorPreviewController::activeMaskPosition() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("position"), 0.5);
}
double EditorPreviewController::activeMaskRotation() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("rotation"), 0.0);
}
double EditorPreviewController::activeMaskCenterX() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("centerX"), 0.5);
}
double EditorPreviewController::activeMaskCenterY() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("centerY"), 0.5);
}
double EditorPreviewController::activeMaskRadiusX() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("radiusX"), 0.25);
}
double EditorPreviewController::activeMaskRadiusY() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("radiusY"), 0.25);
}
double EditorPreviewController::activeMaskBrushSize() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("brushSize"), 0.08);
}
double EditorPreviewController::activeMaskBrushFlow() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("brushFlow"), 1.0);
}
int EditorPreviewController::activeMaskBrushPointCount() const {
  return static_cast<int>(activeMaskDefinition().value(QStringLiteral("brushPoints")).toList().size());
}
bool EditorPreviewController::activeMaskCloneSourceSet() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("cloneSourceSet"), false);
}
double EditorPreviewController::activeMaskCloneSourceX() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("cloneSourceX"), 0.5);
}
double EditorPreviewController::activeMaskCloneSourceY() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("cloneSourceY"), 0.5);
}
double EditorPreviewController::activeMaskCloneScale() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("cloneScale"), 1.0);
}
double EditorPreviewController::activeMaskCloneRotation() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("cloneRotation"), 0.0);
}
QVariantList EditorPreviewController::activeMaskPolygonPoints() const {
  return activeMaskDefinition().value(QStringLiteral("polygonPoints")).toList();
}
int EditorPreviewController::activeMaskPolygonPointCount() const {
  return static_cast<int>(activeMaskPolygonPoints().size());
}
QVariantList EditorPreviewController::activeMaskOutlinePoints() const {
  return outline_points_for_mask(activeMaskDefinition());
}
bool EditorPreviewController::smartSelectPreviewActive() const {
  return !smart_select_preview_mask_.isEmpty();
}
QVariantList EditorPreviewController::smartSelectPreviewOutlinePoints() const {
  return outline_points_for_mask(smart_select_preview_mask_);
}
double EditorPreviewController::activeMaskLuminanceMin() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("luminanceMin"), 0.2);
}
double EditorPreviewController::activeMaskLuminanceMax() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("luminanceMax"), 0.8);
}
double EditorPreviewController::activeMaskHueCenter() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("hueCenter"), 0.08);
}
double EditorPreviewController::activeMaskHueRange() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("hueRange"), 0.12);
}
double EditorPreviewController::activeMaskSaturationMin() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("saturationMin"), 0.1);
}
double EditorPreviewController::activeMaskSaturationMax() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("saturationMax"), 1.0);
}
double EditorPreviewController::activeMaskRangeFeather() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("rangeFeather"), 0.1);
}
bool EditorPreviewController::whiteBalanceSampling() const { return white_balance_sampling_; }
bool EditorPreviewController::linearMaskEnabled() const { return linear_mask_enabled_; }
double EditorPreviewController::linearMaskExposure() const { return linear_mask_exposure_; }
double EditorPreviewController::linearMaskTemperature() const { return linear_mask_temperature_; }
double EditorPreviewController::linearMaskFeather() const { return linear_mask_feather_; }
double EditorPreviewController::linearMaskPosition() const { return linear_mask_position_; }
double EditorPreviewController::linearMaskRotation() const { return linear_mask_rotation_; }
bool EditorPreviewController::radialMaskEnabled() const { return radial_mask_enabled_; }
double EditorPreviewController::radialMaskExposure() const { return radial_mask_exposure_; }
double EditorPreviewController::radialMaskTemperature() const { return radial_mask_temperature_; }
double EditorPreviewController::radialMaskFeather() const { return radial_mask_feather_; }
double EditorPreviewController::radialMaskCenterX() const { return radial_mask_center_x_; }
double EditorPreviewController::radialMaskCenterY() const { return radial_mask_center_y_; }
double EditorPreviewController::radialMaskRadiusX() const { return radial_mask_radius_x_; }
double EditorPreviewController::radialMaskRadiusY() const { return radial_mask_radius_y_; }
double EditorPreviewController::radialMaskRotation() const { return radial_mask_rotation_; }

void EditorPreviewController::setPreviewTargetEdge(int edge) {
  const auto clamped_edge = clamp_preview_target_edge(edge);
  if (previewTargetEdge() == clamped_edge) {
    return;
  }

  preview_target_edge_ = clamped_edge;
  emit previewTargetEdgeChanged();
  if (hasAsset()) {
    renderPreview();
  }
}

void EditorPreviewController::setCompareMode(bool compare_mode) {
  if (compare_mode_ == compare_mode) {
    return;
  }
  compare_mode_ = compare_mode;
  if (compare_mode_ && crop_editing_) {
    crop_editing_ = false;
    emit controlsChanged();
  }
  emit compareModeChanged();
  if (compare_mode_ && hasAsset()) {
    renderPreview();
  }
}

void EditorPreviewController::setCropEditing(bool crop_editing) {
  if (crop_editing_ == crop_editing) {
    return;
  }
  crop_editing_ = crop_editing;
  if (crop_editing_) {
    if (compare_mode_) {
      compare_mode_ = false;
      emit compareModeChanged();
    }
    if (white_balance_sampling_) {
      white_balance_sampling_ = false;
      emit whiteBalanceSamplingChanged();
    }
  }
  emit controlsChanged();
  if (hasAsset()) {
    renderPreview();
  }
}

void EditorPreviewController::setExposure(double exposure) {
  exposure = qBound(-2.0, exposure, 2.0);
  if (qFuzzyCompare(exposure_, exposure)) {
    return;
  }
  rememberStateForUndo();
  exposure_ = exposure;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setContrast(double contrast) {
  contrast = clamp_signed_percent(contrast);
  if (qFuzzyCompare(contrast_ + 1.0, contrast + 1.0)) {
    return;
  }
  rememberStateForUndo();
  contrast_ = contrast;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHighlights(double highlights) {
  highlights = clamp_signed_percent(highlights);
  if (qFuzzyCompare(highlights_ + 1.0, highlights + 1.0)) {
    return;
  }
  rememberStateForUndo();
  highlights_ = highlights;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setShadows(double shadows) {
  shadows = clamp_signed_percent(shadows);
  if (qFuzzyCompare(shadows_ + 1.0, shadows + 1.0)) {
    return;
  }
  rememberStateForUndo();
  shadows_ = shadows;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setWhites(double whites) {
  whites = clamp_signed_percent(whites);
  if (qFuzzyCompare(whites_ + 1.0, whites + 1.0)) {
    return;
  }
  rememberStateForUndo();
  whites_ = whites;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlacks(double blacks) {
  blacks = clamp_signed_percent(blacks);
  if (qFuzzyCompare(blacks_ + 1.0, blacks + 1.0)) {
    return;
  }
  rememberStateForUndo();
  blacks_ = blacks;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTemperature(double temperature) {
  temperature = clamp_signed_percent(temperature);
  if (qFuzzyCompare(temperature_ + 1.0, temperature + 1.0)) {
    return;
  }
  rememberStateForUndo();
  temperature_ = temperature;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTint(double tint) {
  tint = clamp_signed_percent(tint);
  if (qFuzzyCompare(tint_ + 1.0, tint + 1.0)) {
    return;
  }
  rememberStateForUndo();
  tint_ = tint;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTexture(double texture) {
  texture = clamp_signed_percent(texture);
  if (qFuzzyCompare(texture_ + 1.0, texture + 1.0)) {
    return;
  }
  rememberStateForUndo();
  texture_ = texture;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setClarity(double clarity) {
  clarity = clamp_signed_percent(clarity);
  if (qFuzzyCompare(clarity_ + 1.0, clarity + 1.0)) {
    return;
  }
  rememberStateForUndo();
  clarity_ = clarity;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setDehaze(double dehaze) {
  dehaze = clamp_signed_percent(dehaze);
  if (qFuzzyCompare(dehaze_ + 1.0, dehaze + 1.0)) {
    return;
  }
  rememberStateForUndo();
  dehaze_ = dehaze;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSharpening(double sharpening) {
  sharpening = clamp_signed_percent(sharpening);
  if (qFuzzyCompare(sharpening_ + 1.0, sharpening + 1.0)) {
    return;
  }
  rememberStateForUndo();
  sharpening_ = sharpening;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSharpenMasking(double sharpen_masking) {
  sharpen_masking = clamp_signed_percent(sharpen_masking);
  if (qFuzzyCompare(sharpen_masking_ + 1.0, sharpen_masking + 1.0)) {
    return;
  }
  rememberStateForUndo();
  sharpen_masking_ = sharpen_masking;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLuminanceNr(double luminance_nr) {
  luminance_nr = clamp_signed_percent(luminance_nr);
  if (qFuzzyCompare(luminance_nr_ + 1.0, luminance_nr + 1.0)) {
    return;
  }
  rememberStateForUndo();
  luminance_nr_ = luminance_nr;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorNr(double color_nr) {
  color_nr = clamp_signed_percent(color_nr);
  if (qFuzzyCompare(color_nr_ + 1.0, color_nr + 1.0)) {
    return;
  }
  rememberStateForUndo();
  color_nr_ = color_nr;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLensDistortion(double lens_distortion) {
  lens_distortion = clamp_signed_percent(lens_distortion);
  if (qFuzzyCompare(lens_distortion_ + 1.0, lens_distortion + 1.0)) {
    return;
  }
  rememberStateForUndo();
  lens_distortion_ = lens_distortion;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLensVignette(double lens_vignette) {
  lens_vignette = clamp_signed_percent(lens_vignette);
  if (qFuzzyCompare(lens_vignette_ + 1.0, lens_vignette + 1.0)) {
    return;
  }
  rememberStateForUndo();
  lens_vignette_ = lens_vignette;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChromaticAberration(double chromatic_aberration) {
  chromatic_aberration = qBound(0.0, chromatic_aberration, 100.0);
  if (qFuzzyCompare(chromatic_aberration_ + 1.0, chromatic_aberration + 1.0)) {
    return;
  }
  rememberStateForUndo();
  chromatic_aberration_ = chromatic_aberration;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setVibrance(double vibrance) {
  vibrance = clamp_signed_percent(vibrance);
  if (qFuzzyCompare(vibrance_ + 1.0, vibrance + 1.0)) {
    return;
  }
  rememberStateForUndo();
  vibrance_ = vibrance;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSaturation(double saturation) {
  saturation = clamp_signed_percent(saturation);
  if (qFuzzyCompare(saturation_ + 1.0, saturation + 1.0)) {
    return;
  }
  rememberStateForUndo();
  saturation_ = saturation;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveShadows(double curve_shadows) {
  curve_shadows = clamp_signed_percent(curve_shadows);
  if (qFuzzyCompare(curve_shadows_ + 1.0, curve_shadows + 1.0)) {
    return;
  }
  rememberStateForUndo();
  curve_shadows_ = curve_shadows;
  master_curve_points_.clear();
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveDarks(double curve_darks) {
  curve_darks = clamp_signed_percent(curve_darks);
  if (qFuzzyCompare(curve_darks_ + 1.0, curve_darks + 1.0)) {
    return;
  }
  rememberStateForUndo();
  curve_darks_ = curve_darks;
  master_curve_points_.clear();
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveLights(double curve_lights) {
  curve_lights = clamp_signed_percent(curve_lights);
  if (qFuzzyCompare(curve_lights_ + 1.0, curve_lights + 1.0)) {
    return;
  }
  rememberStateForUndo();
  curve_lights_ = curve_lights;
  master_curve_points_.clear();
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveHighlights(double curve_highlights) {
  curve_highlights = clamp_signed_percent(curve_highlights);
  if (qFuzzyCompare(curve_highlights_ + 1.0, curve_highlights + 1.0)) {
    return;
  }
  rememberStateForUndo();
  curve_highlights_ = curve_highlights;
  master_curve_points_.clear();
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveChannel(const QString& curve_channel) {
  const auto normalized =
      curve_channel.trimmed().isEmpty() ? QStringLiteral("Master") : curve_channel.trimmed();
  if (curve_channel_ == normalized) {
    return;
  }
  curve_channel_ = normalized;
  emit controlsChanged();
}

void EditorPreviewController::setActiveCurvePoints(const QVariantList& active_curve_points) {
  const auto normalized_points = normalize_curve_points(active_curve_points);
  auto* target_points = &blue_curve_points_;
  if (curve_channel_ == QStringLiteral("Master")) {
    target_points = &master_curve_points_;
  } else if (curve_channel_ == QStringLiteral("Red")) {
    target_points = &red_curve_points_;
  } else if (curve_channel_ == QStringLiteral("Green")) {
    target_points = &green_curve_points_;
  }

  if (*target_points == normalized_points) {
    return;
  }

  rememberStateForUndo();
  *target_points = normalized_points;
  if (curve_channel_ == QStringLiteral("Master") && !master_curve_points_.isEmpty()) {
    curve_shadows_ = 0.0;
    curve_darks_ = 0.0;
    curve_lights_ = 0.0;
    curve_highlights_ = 0.0;
  }

  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHslChannel(const QString& hsl_channel) {
  const auto normalized = hsl_channel.isEmpty() ? QStringLiteral("Reds") : hsl_channel;
  if (hsl_channel_ == normalized) {
    return;
  }
  rememberStateForUndo();
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  // Save current adjustments to the outgoing channel slot.
  const auto old_index = kMixerChannelNames.indexOf(hsl_channel_);
  if (old_index >= 0 && old_index < 8) {
    mixer_channels_[static_cast<std::size_t>(old_index)].hue = hue_adjustment_;
    mixer_channels_[static_cast<std::size_t>(old_index)].saturation = saturation_adjustment_;
    mixer_channels_[static_cast<std::size_t>(old_index)].luminance = luminance_adjustment_;
  }
  hsl_channel_ = normalized;
  // Load adjustments from the incoming channel slot.
  const auto new_index = kMixerChannelNames.indexOf(normalized);
  if (new_index >= 0 && new_index < 8) {
    hue_adjustment_ = mixer_channels_[static_cast<std::size_t>(new_index)].hue;
    saturation_adjustment_ = mixer_channels_[static_cast<std::size_t>(new_index)].saturation;
    luminance_adjustment_ = mixer_channels_[static_cast<std::size_t>(new_index)].luminance;
  } else {
    hue_adjustment_ = 0.0;
    saturation_adjustment_ = 0.0;
    luminance_adjustment_ = 0.0;
  }
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHueAdjustment(double hue_adjustment) {
  hue_adjustment = clamp_signed_percent(hue_adjustment);
  if (qFuzzyCompare(hue_adjustment_ + 1.0, hue_adjustment + 1.0)) {
    return;
  }
  rememberStateForUndo();
  hue_adjustment_ = hue_adjustment;
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  const auto ch_index = kMixerChannelNames.indexOf(hsl_channel_);
  if (ch_index >= 0 && ch_index < 8) {
    mixer_channels_[static_cast<std::size_t>(ch_index)].hue = hue_adjustment;
  }
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSaturationAdjustment(double saturation_adjustment) {
  saturation_adjustment = clamp_signed_percent(saturation_adjustment);
  if (qFuzzyCompare(saturation_adjustment_ + 1.0, saturation_adjustment + 1.0)) {
    return;
  }
  rememberStateForUndo();
  saturation_adjustment_ = saturation_adjustment;
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  const auto ch_index = kMixerChannelNames.indexOf(hsl_channel_);
  if (ch_index >= 0 && ch_index < 8) {
    mixer_channels_[static_cast<std::size_t>(ch_index)].saturation = saturation_adjustment;
  }
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLuminanceAdjustment(double luminance_adjustment) {
  luminance_adjustment = clamp_signed_percent(luminance_adjustment);
  if (qFuzzyCompare(luminance_adjustment_ + 1.0, luminance_adjustment + 1.0)) {
    return;
  }
  rememberStateForUndo();
  luminance_adjustment_ = luminance_adjustment;
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  const auto ch_index = kMixerChannelNames.indexOf(hsl_channel_);
  if (ch_index >= 0 && ch_index < 8) {
    mixer_channels_[static_cast<std::size_t>(ch_index)].luminance = luminance_adjustment;
  }
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingShadowsHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_shadows_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_shadows_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingShadowsSaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_shadows_saturation_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_shadows_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingShadowsLuminance(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_shadows_luminance_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_shadows_luminance_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingMidtonesHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_midtones_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_midtones_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingMidtonesSaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_midtones_saturation_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_midtones_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingMidtonesLuminance(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_midtones_luminance_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_midtones_luminance_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingHighlightsHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_highlights_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_highlights_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingHighlightsSaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_highlights_saturation_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_highlights_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingHighlightsLuminance(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_highlights_luminance_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_highlights_luminance_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::updateCropRect(double x, double y, double width, double height) {
  const auto normalized_width = qBound(0.05, width, 1.0);
  const auto normalized_height = qBound(0.05, height, 1.0);
  const auto normalized_x = qBound(0.0, x, 1.0 - normalized_width);
  const auto normalized_y = qBound(0.0, y, 1.0 - normalized_height);

  if (qFuzzyCompare(crop_rect_x_ + 1.0, normalized_x + 1.0) &&
      qFuzzyCompare(crop_rect_y_ + 1.0, normalized_y + 1.0) &&
      qFuzzyCompare(crop_rect_width_ + 1.0, normalized_width + 1.0) &&
      qFuzzyCompare(crop_rect_height_ + 1.0, normalized_height + 1.0)) {
    return;
  }

  rememberStateForUndo();
  crop_rect_x_ = normalized_x;
  crop_rect_y_ = normalized_y;
  crop_rect_width_ = normalized_width;
  crop_rect_height_ = normalized_height;
  persistCurrentState();
  emit controlsChanged();
  if (!crop_editing_) {
    renderPreview();
  }
}

void EditorPreviewController::resetCropRect() {
  if (qFuzzyIsNull(crop_rect_width_) && qFuzzyIsNull(crop_rect_height_)) {
    return;
  }

  rememberStateForUndo();
  crop_rect_x_ = 0.0;
  crop_rect_y_ = 0.0;
  crop_rect_width_ = 0.0;
  crop_rect_height_ = 0.0;
  persistCurrentState();
  emit controlsChanged();
  if (!crop_editing_) {
    renderPreview();
  }
}

void EditorPreviewController::setCropZoom(double crop_zoom) {
  crop_zoom = qBound(1.0, crop_zoom, 3.0);
  if (qFuzzyCompare(crop_zoom_, crop_zoom)) {
    return;
  }
  rememberStateForUndo();
  const auto previous_zoom = crop_zoom_;
  crop_zoom_ = crop_zoom;
  const auto size = sourceImageSize();
  if (size.isValid()) {
    const auto current_rect = normalized_crop_rect_for_state(size, currentState());
    const auto center_x = current_rect.x() + current_rect.width() * 0.5;
    const auto center_y = current_rect.y() + current_rect.height() * 0.5;
    const auto scale = previous_zoom / crop_zoom_;
    const auto width = qBound(0.05, current_rect.width() * scale, 1.0);
    const auto height = qBound(0.05, current_rect.height() * scale, 1.0);
    crop_rect_width_ = width;
    crop_rect_height_ = height;
    crop_rect_x_ = qBound(0.0, center_x - width * 0.5, 1.0 - width);
    crop_rect_y_ = qBound(0.0, center_y - height * 0.5, 1.0 - height);
  }
  persistCurrentState();
  emit controlsChanged();
  if (!crop_editing_) {
    renderPreview();
  }
}

void EditorPreviewController::setCropPreset(const QString& crop_preset) {
  const auto normalized =
      crop_preset.isEmpty() ? QStringLiteral("Original") : crop_preset;
  if (crop_preset_ == normalized) {
    return;
  }
  rememberStateForUndo();
  crop_preset_ = normalized;
  const auto size = sourceImageSize();
  if (size.isValid()) {
    auto rect = normalized_crop_rect_for_state(size, currentState());
    const auto aspect_ratio = crop_aspect_ratio(crop_preset_);
    if (aspect_ratio > 0.0) {
      const auto center_x = rect.x() + rect.width() * 0.5;
      const auto center_y = rect.y() + rect.height() * 0.5;
      auto width = rect.width();
      auto height = rect.height();
      const auto current_ratio = width / std::max(1e-6, height);
      if (current_ratio > aspect_ratio) {
        width = height * aspect_ratio;
      } else {
        height = width / aspect_ratio;
      }
      width = qBound(0.05, width, 1.0);
      height = qBound(0.05, height, 1.0);
      crop_rect_width_ = width;
      crop_rect_height_ = height;
      crop_rect_x_ = qBound(0.0, center_x - width * 0.5, 1.0 - width);
      crop_rect_y_ = qBound(0.0, center_y - height * 0.5, 1.0 - height);
    } else {
      crop_rect_x_ = rect.x();
      crop_rect_y_ = rect.y();
      crop_rect_width_ = rect.width();
      crop_rect_height_ = rect.height();
    }
  }
  persistCurrentState();
  emit controlsChanged();
  if (!crop_editing_) {
    renderPreview();
  }
}

void EditorPreviewController::setActiveMaskIndex(int index) {
  const auto normalized_index =
      mask_stack_.isEmpty()
          ? -1
          : qBound(0, index, static_cast<int>(mask_stack_.size()) - 1);
  if (active_mask_index_ == normalized_index) {
    return;
  }
  active_mask_index_ = normalized_index;
  emit controlsChanged();
}

void EditorPreviewController::setActiveMaskName(const QString& name) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("name"), name.simplified());
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskOperation(const QString& operation) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("operation"), normalized_mask_operation(operation));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskEnabled(bool enabled) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("enabled"), enabled);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskExposure(double local_exposure) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("exposure"), qBound(-2.0, local_exposure, 2.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskTemperature(double local_temperature) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("temperature"), clamp_signed_percent(local_temperature));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskHueShift(double hue_shift) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("hueShift"), clamp_signed_percent(hue_shift));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSaturationShift(double saturation_shift) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("saturationShift"), clamp_signed_percent(saturation_shift));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskColorizeHue(double colorize_hue) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  auto wrapped = colorize_hue - std::floor(colorize_hue);
  if (wrapped < 0.0) {
    wrapped += 1.0;
  }
  mask.insert(QStringLiteral("colorizeHue"), wrapped);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskColorizeStrength(double colorize_strength) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("colorizeStrength"), qBound(0.0, colorize_strength, 100.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRemoveStrength(double remove_strength) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("removeStrength"), qBound(0.0, remove_strength, 100.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSelectionInverted(bool selection_inverted) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("selectionInvert"), selection_inverted);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSelectionExpand(double selection_expand) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("selectionExpand"), clamp_signed_percent(selection_expand));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskFeather(double local_feather) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  const auto minimum =
      activeMaskKind() == QStringLiteral("polygon") ? 0.0 : 0.05;
  const auto maximum =
      activeMaskKind() == QStringLiteral("polygon") ? 0.2 : 1.0;
  mask.insert(QStringLiteral("feather"), qBound(minimum, local_feather, maximum));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskPosition(double local_position) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("linear")) {
    return;
  }
  mask.insert(QStringLiteral("position"), qBound(0.0, local_position, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRotation(double local_rotation) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("rotation"), qBound(-180.0, local_rotation, 180.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskCenterX(double center_x) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("radial")) {
    return;
  }
  mask.insert(QStringLiteral("centerX"), qBound(0.0, center_x, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskCenterY(double center_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("radial")) {
    return;
  }
  mask.insert(QStringLiteral("centerY"), qBound(0.0, center_y, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRadiusX(double radius_x) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("radial")) {
    return;
  }
  mask.insert(QStringLiteral("radiusX"), qBound(0.05, radius_x, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRadiusY(double radius_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("radial")) {
    return;
  }
  mask.insert(QStringLiteral("radiusY"), qBound(0.05, radius_y, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushSize(double brush_size) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushSize"), qBound(0.01, brush_size, 0.35));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushFlow(double brush_flow) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushFlow"), qBound(0.05, brush_flow, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskCloneScale(double clone_scale) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush") ||
      activeMaskRetouchMode() != QStringLiteral("clone")) {
    return;
  }
  mask.insert(QStringLiteral("cloneScale"), qBound(0.25, clone_scale, 4.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskCloneRotation(double clone_rotation) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush") ||
      activeMaskRetouchMode() != QStringLiteral("clone")) {
    return;
  }
  mask.insert(QStringLiteral("cloneRotation"), qBound(-180.0, clone_rotation, 180.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskLuminanceMin(double min_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("luminance")) {
    return;
  }
  mask.insert(QStringLiteral("luminanceMin"), qBound(0.0, min_value, 1.0));
  if (mask.value(QStringLiteral("luminanceMin")).toDouble() > mask.value(QStringLiteral("luminanceMax")).toDouble()) {
    mask.insert(QStringLiteral("luminanceMax"), mask.value(QStringLiteral("luminanceMin")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskLuminanceMax(double max_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("luminance")) {
    return;
  }
  mask.insert(QStringLiteral("luminanceMax"), qBound(0.0, max_value, 1.0));
  if (mask.value(QStringLiteral("luminanceMax")).toDouble() < mask.value(QStringLiteral("luminanceMin")).toDouble()) {
    mask.insert(QStringLiteral("luminanceMin"), mask.value(QStringLiteral("luminanceMax")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskHueCenter(double center) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("color")) {
    return;
  }
  auto wrapped = center - std::floor(center);
  if (wrapped < 0.0) {
    wrapped += 1.0;
  }
  mask.insert(QStringLiteral("hueCenter"), wrapped);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskHueRange(double range) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("color")) {
    return;
  }
  mask.insert(QStringLiteral("hueRange"), qBound(0.02, range, 0.5));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSaturationMin(double min_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("color")) {
    return;
  }
  mask.insert(QStringLiteral("saturationMin"), qBound(0.0, min_value, 1.0));
  if (mask.value(QStringLiteral("saturationMin")).toDouble() > mask.value(QStringLiteral("saturationMax")).toDouble()) {
    mask.insert(QStringLiteral("saturationMax"), mask.value(QStringLiteral("saturationMin")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSaturationMax(double max_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("color")) {
    return;
  }
  mask.insert(QStringLiteral("saturationMax"), qBound(0.0, max_value, 1.0));
  if (mask.value(QStringLiteral("saturationMax")).toDouble() < mask.value(QStringLiteral("saturationMin")).toDouble()) {
    mask.insert(QStringLiteral("saturationMin"), mask.value(QStringLiteral("saturationMax")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRangeFeather(double feather) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() ||
      (activeMaskKind() != QStringLiteral("luminance") && activeMaskKind() != QStringLiteral("color"))) {
    return;
  }
  mask.insert(QStringLiteral("rangeFeather"), qBound(0.0, feather, 0.5));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setLinearMaskEnabled(bool enabled) {
  QVariantMap updates;
  updates.insert(QStringLiteral("enabled"), enabled);
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskExposure(double local_exposure) {
  QVariantMap updates;
  updates.insert(QStringLiteral("exposure"), qBound(-2.0, local_exposure, 2.0));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskTemperature(double local_temperature) {
  QVariantMap updates;
  updates.insert(QStringLiteral("temperature"), clamp_signed_percent(local_temperature));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskFeather(double local_feather) {
  QVariantMap updates;
  updates.insert(QStringLiteral("feather"), qBound(0.05, local_feather, 1.0));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskPosition(double local_position) {
  QVariantMap updates;
  updates.insert(QStringLiteral("position"), qBound(0.0, local_position, 1.0));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskRotation(double local_rotation) {
  QVariantMap updates;
  updates.insert(QStringLiteral("rotation"), qBound(-180.0, local_rotation, 180.0));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setRadialMaskEnabled(bool enabled) {
  QVariantMap updates;
  updates.insert(QStringLiteral("enabled"), enabled);
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskExposure(double local_exposure) {
  QVariantMap updates;
  updates.insert(QStringLiteral("exposure"), qBound(-2.0, local_exposure, 2.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskTemperature(double local_temperature) {
  QVariantMap updates;
  updates.insert(QStringLiteral("temperature"), clamp_signed_percent(local_temperature));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskFeather(double local_feather) {
  QVariantMap updates;
  updates.insert(QStringLiteral("feather"), qBound(0.05, local_feather, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskCenterX(double center_x) {
  QVariantMap updates;
  updates.insert(QStringLiteral("centerX"), qBound(0.0, center_x, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskCenterY(double center_y) {
  QVariantMap updates;
  updates.insert(QStringLiteral("centerY"), qBound(0.0, center_y, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskRadiusX(double radius_x) {
  QVariantMap updates;
  updates.insert(QStringLiteral("radiusX"), qBound(0.05, radius_x, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskRadiusY(double radius_y) {
  QVariantMap updates;
  updates.insert(QStringLiteral("radiusY"), qBound(0.05, radius_y, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskRotation(double local_rotation) {
  QVariantMap updates;
  updates.insert(QStringLiteral("rotation"), qBound(-180.0, local_rotation, 180.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::loadAsset(
    const QString& asset_id,
    const QString& file_path,
    const QString& thumbnail_source,
    const QString& camera_label,
    const QString& capture_date,
    const QString& lens_model,
    const QString& recipe_json) {
  persistCurrentState();
  asset_id_ = asset_id;
  file_path_ = file_path;
  fallback_thumbnail_source_ = thumbnail_source;
  camera_label_ = camera_label;
  capture_date_ = capture_date;
  lens_model_ = lens_model;
  resetSmartSelectInstanceMap();
  restoreStateForAsset(asset_id_, recipe_json);
  setWhiteBalanceSampling(false);
  qInfo().noquote() << "EditorPreviewController::loadAsset"
                    << "asset_id=" << asset_id_
                    << "file_path=" << file_path_
                    << "thumbnail=" << fallback_thumbnail_source_;
  refreshDebugInfo();
  emit assetChanged();
  renderPreview();
}

void EditorPreviewController::undo() {
  if (!canUndo()) {
    return;
  }

  auto& undo_stack = undoStackForAsset(asset_id_);
  const auto previous_state = undo_stack.takeLast();
  auto& redo_stack = redoStackForAsset(asset_id_);
  const auto current_state = currentState();
  if (redo_stack.isEmpty() || !edit_states_equal(redo_stack.back(), current_state)) {
    redo_stack.push_back(current_state);
    if (redo_stack.size() > kMaxEditHistoryDepth) {
      redo_stack.removeFirst();
    }
  }
  applyHistoryState(previous_state);
}

void EditorPreviewController::redo() {
  if (!canRedo()) {
    return;
  }

  auto& redo_stack = redoStackForAsset(asset_id_);
  const auto next_state = redo_stack.takeLast();
  pushUndoState(currentState());
  applyHistoryState(next_state);
}

void EditorPreviewController::resetAdjustments() {
  if (edit_states_equal(currentState(), EditState{})) {
    return;
  }
  rememberStateForUndo();
  exposure_ = 0.0;
  contrast_ = 0.0;
  highlights_ = 0.0;
  shadows_ = 0.0;
  whites_ = 0.0;
  blacks_ = 0.0;
  temperature_ = 0.0;
  tint_ = 0.0;
  texture_ = 0.0;
  clarity_ = 0.0;
  dehaze_ = 0.0;
  sharpening_ = 0.0;
  sharpen_masking_ = 0.0;
  luminance_nr_ = 0.0;
  color_nr_ = 0.0;
  lens_distortion_ = 0.0;
  lens_vignette_ = 0.0;
  chromatic_aberration_ = 0.0;
  vibrance_ = 0.0;
  saturation_ = 0.0;
  curve_shadows_ = 0.0;
  curve_darks_ = 0.0;
  curve_lights_ = 0.0;
  curve_highlights_ = 0.0;
  curve_channel_ = QStringLiteral("Master");
  master_curve_points_.clear();
  red_curve_points_.clear();
  green_curve_points_.clear();
  blue_curve_points_.clear();
  hsl_channel_ = QStringLiteral("Reds");
  hue_adjustment_ = 0.0;
  saturation_adjustment_ = 0.0;
  luminance_adjustment_ = 0.0;
  mixer_channels_ = {};
  grading_shadows_hue_ = 0.0;
  grading_shadows_saturation_ = 0.0;
  grading_shadows_luminance_ = 0.0;
  grading_midtones_hue_ = 0.0;
  grading_midtones_saturation_ = 0.0;
  grading_midtones_luminance_ = 0.0;
  grading_highlights_hue_ = 0.0;
  grading_highlights_saturation_ = 0.0;
  grading_highlights_luminance_ = 0.0;
  crop_rect_x_ = 0.0;
  crop_rect_y_ = 0.0;
  crop_rect_width_ = 0.0;
  crop_rect_height_ = 0.0;
  crop_zoom_ = 1.0;
  crop_preset_ = QStringLiteral("Original");
  mask_stack_.clear();
  active_mask_index_ = -1;
  linear_mask_enabled_ = false;
  linear_mask_exposure_ = 0.0;
  linear_mask_temperature_ = 0.0;
  linear_mask_feather_ = 0.22;
  linear_mask_position_ = 0.5;
  linear_mask_rotation_ = 0.0;
  radial_mask_enabled_ = false;
  radial_mask_exposure_ = 0.0;
  radial_mask_temperature_ = 0.0;
  radial_mask_feather_ = 0.25;
  radial_mask_center_x_ = 0.5;
  radial_mask_center_y_ = 0.5;
  radial_mask_radius_x_ = 0.25;
  radial_mask_radius_y_ = 0.25;
  radial_mask_rotation_ = 0.0;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

QString EditorPreviewController::currentRecipeJson() const { return recipeJsonForState(currentState()); }

QString EditorPreviewController::copiedSettingsRecipeJson() const {
  if (!has_copied_settings_) {
    return {};
  }
  return recipeJsonForState(copied_settings_state_);
}

QString EditorPreviewController::presetRecipeJson(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  const auto iterator = presets_.constFind(normalized_name);
  if (iterator == presets_.cend()) {
    return {};
  }
  return recipeJsonForState(iterator.value());
}

bool EditorPreviewController::copyCurrentSettings() {
  if (asset_id_.isEmpty() && !hasAsset()) {
    return false;
  }

  copied_settings_state_ = currentState();
  if (!has_copied_settings_) {
    has_copied_settings_ = true;
    emit clipboardChanged();
  }
  setStatus(QStringLiteral("Copied current adjustments."));
  return true;
}

bool EditorPreviewController::pasteCopiedSettings(const QStringList& adjustment_groups) {
  if ((asset_id_.isEmpty() && !hasAsset()) || !has_copied_settings_) {
    return false;
  }

  const auto merged_state = merge_edit_recipe_state_by_groups(currentState(), copied_settings_state_, adjustment_groups);
  if (edit_states_equal(merged_state, currentState())) {
    return false;
  }

  rememberStateForUndo();
  applyState(merged_state);
  persistCurrentState();
  renderPreview();
  setStatus(QStringLiteral("Pasted copied adjustments."));
  return true;
}

bool EditorPreviewController::savePreset(const QString& name, bool overwrite) {
  const auto normalized_name = normalizedPresetName(name);
  if (normalized_name.isEmpty() || (asset_id_.isEmpty() && !hasAsset())) {
    return false;
  }

  const auto preset_exists = presets_.contains(normalized_name);
  if (preset_exists && !overwrite) {
    return false;
  }

  const auto names_before = presets_.keys();
  presets_.insert(normalized_name, currentState());
  savePresets();
  if (names_before != presets_.keys()) {
    emit presetsChanged();
  }
  setStatus(!preset_exists
                ? QStringLiteral("Saved preset '%1'.").arg(normalized_name)
                : QStringLiteral("Updated preset '%1'.").arg(normalized_name));
  return true;
}

bool EditorPreviewController::applyPreset(const QString& name, const QStringList& adjustment_groups) {
  if (asset_id_.isEmpty() && !hasAsset()) {
    return false;
  }

  const auto normalized_name = normalizedPresetName(name);
  const auto iterator = presets_.constFind(normalized_name);
  if (iterator == presets_.cend()) {
    return false;
  }

  const auto merged_state = merge_edit_recipe_state_by_groups(currentState(), iterator.value(), adjustment_groups);
  if (edit_states_equal(merged_state, currentState())) {
    return false;
  }

  rememberStateForUndo();
  applyState(merged_state);
  persistCurrentState();
  renderPreview();
  setStatus(QStringLiteral("Applied preset '%1'.").arg(normalized_name));
  return true;
}

bool EditorPreviewController::deletePreset(const QString& name) {
  const auto normalized_name = normalizedPresetName(name);
  if (normalized_name.isEmpty() || !presets_.contains(normalized_name)) {
    return false;
  }

  presets_.remove(normalized_name);
  savePresets();
  emit presetsChanged();
  setStatus(QStringLiteral("Deleted preset '%1'.").arg(normalized_name));
  return true;
}

void EditorPreviewController::addMask(const QString& kind) {
  auto next_stack = normalize_mask_stack(mask_stack_);
  next_stack.append(default_mask_definition(kind, static_cast<int>(next_stack.size()) + 1));
  applyMaskStackChange(next_stack, static_cast<int>(next_stack.size()) - 1);
}

void EditorPreviewController::addSubjectMask() {
  if (file_path_.isEmpty()) {
    setStatus(QStringLiteral("Load an image before creating an AI subject mask."));
    return;
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    qWarning() << "addSubjectMask: failed to decode source image";
    setStatus(QStringLiteral("Unable to decode the source image for AI subject masking."));
    return;
  }

  const auto result = generate_subject_mask(source_image);
  if (!result.success) {
    qWarning() << "addSubjectMask:" << result.error_message;
    setStatus(
        result.error_message.isEmpty()
            ? QStringLiteral("AI subject masking is unavailable.")
            : result.error_message);
    return;
  }

  auto next_stack = normalize_mask_stack(mask_stack_);
  auto mask_def = default_mask_definition(QStringLiteral("subject"), static_cast<int>(next_stack.size()) + 1);
  mask_def.insert(QStringLiteral("feather"), 0.5);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.append(mask_def);
  applyMaskStackChange(next_stack, static_cast<int>(next_stack.size()) - 1);
  setStatus(QStringLiteral("AI subject mask added."));
}

void EditorPreviewController::addObjectMaskAt(double normalized_x, double normalized_y) {
  smartSelectObjectAt(normalized_x, normalized_y, true);
}

void EditorPreviewController::updateSmartSelectPreview(
    double normalized_x,
    double normalized_y) {
  if (!ensureSmartSelectInstanceMap()) {
    resetSmartSelectPreview();
    return;
  }

  const auto instance_id = smartSelectInstanceIdAt(normalized_x, normalized_y);
  if (instance_id <= 0) {
    resetSmartSelectPreview();
    return;
  }

  if (instance_id == smart_select_preview_instance_id_ && !smart_select_preview_mask_.isEmpty()) {
    return;
  }

  const auto preview_mask = smartSelectPreviewMaskForInstance(instance_id);
  if (preview_mask.isEmpty()) {
    resetSmartSelectPreview();
    return;
  }

  smart_select_preview_instance_id_ = instance_id;
  smart_select_preview_mask_ = preview_mask;
  emit smartSelectPreviewChanged();
}

void EditorPreviewController::clearSmartSelectPreview() {
  resetSmartSelectPreview();
}

void EditorPreviewController::smartSelectObjectAt(
    double normalized_x,
    double normalized_y,
    bool create_new) {
  if (file_path_.isEmpty()) {
    setStatus(QStringLiteral("Load an image before picking an AI object mask."));
    return;
  }

  if (!ensureSmartSelectInstanceMap()) {
    qWarning() << "addObjectMaskAt: failed to build instance map";
    setStatus(QStringLiteral("Unable to decode the source image for AI object masking."));
    return;
  }

  const auto instance_id = smartSelectInstanceIdAt(normalized_x, normalized_y);
  if (instance_id <= 0) {
    resetSmartSelectPreview();
    setStatus(QStringLiteral("No selectable object at that point"));
    return;
  }

  updateSmartSelectPreview(normalized_x, normalized_y);

  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    qWarning() << "addObjectMaskAt: failed to decode source image";
    setStatus(QStringLiteral("Unable to decode the source image for AI object masking."));
    return;
  }

  const auto result = generate_object_mask_for_instance(source_image, instance_id);
  if (!result.success) {
    qWarning() << "addObjectMaskAt:" << result.error_message;
    setStatus(
        result.error_message.isEmpty()
            ? QStringLiteral("AI object masking is unavailable.")
            : result.error_message);
    return;
  }

  auto next_stack = normalize_mask_stack(mask_stack_);
  const auto can_extend_active =
      !create_new && hasActiveMask() && activeMaskKind() == QStringLiteral("object");

  if (can_extend_active) {
    auto mask_def = activeMaskDefinition();
    mask_def.insert(
        QStringLiteral("segmentationData"),
        union_segmentation_masks(
            mask_def.value(QStringLiteral("segmentationData")).toByteArray(),
            mask_def.value(QStringLiteral("segmentationWidth")).toInt(),
            mask_def.value(QStringLiteral("segmentationHeight")).toInt(),
            result.mask_data,
            result.width,
            result.height));
    mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
    mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
    if (mask_def.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
      mask_def.insert(QStringLiteral("name"), QStringLiteral("Smart Select"));
    }
    updateActiveMaskDefinition(mask_def);
    setStatus(QStringLiteral("Smart selection expanded."));
    return;
  }

  auto mask_def = default_mask_definition(QStringLiteral("object"), static_cast<int>(next_stack.size()) + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Smart Select %1").arg(next_stack.size() + 1));
  mask_def.insert(QStringLiteral("feather"), 0.35);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.append(mask_def);

  applyMaskStackChange(next_stack, static_cast<int>(next_stack.size()) - 1);
  setStatus(QStringLiteral("Smart selection created."));
}

void EditorPreviewController::startSelectionBrush(bool subtract) {
  auto next_stack = normalize_mask_stack(mask_stack_);
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size())) : static_cast<int>(next_stack.size());

  auto mask_def = default_mask_definition(QStringLiteral("brush"), insert_index + 1);
  mask_def.insert(
      QStringLiteral("name"),
      subtract ? QStringLiteral("Refine Subtract %1").arg(insert_index + 1)
               : QStringLiteral("Refine Add %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), subtract ? QStringLiteral("subtract") : QStringLiteral("add"));
  mask_def.insert(QStringLiteral("brushSize"), 0.06);
  mask_def.insert(QStringLiteral("brushFlow"), 0.85);
  mask_def.insert(QStringLiteral("feather"), 0.82);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(
      subtract ? QStringLiteral("Refine Subtract brush ready. Paint over areas to exclude.")
               : QStringLiteral("Refine Add brush ready. Paint over areas to include."));
}

void EditorPreviewController::startRemoveBrush() {
  auto next_stack = normalize_mask_stack(mask_stack_);
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size())) : static_cast<int>(next_stack.size());

  auto mask_def = default_mask_definition(QStringLiteral("brush"), insert_index + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Remove Brush %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask_def.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
  mask_def.insert(QStringLiteral("removeStrength"), 100.0);
  mask_def.insert(QStringLiteral("brushSize"), 0.08);
  mask_def.insert(QStringLiteral("brushFlow"), 0.92);
  mask_def.insert(QStringLiteral("feather"), 0.86);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(QStringLiteral("Remove Brush ready. Paint over content to remove it."));
}

void EditorPreviewController::startHealBrush() {
  auto next_stack = normalize_mask_stack(mask_stack_);
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());

  auto mask_def = default_mask_definition(QStringLiteral("brush"), insert_index + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Heal Brush %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask_def.insert(QStringLiteral("retouchMode"), QStringLiteral("heal"));
  mask_def.insert(QStringLiteral("removeStrength"), 82.0);
  mask_def.insert(QStringLiteral("brushSize"), 0.07);
  mask_def.insert(QStringLiteral("brushFlow"), 0.78);
  mask_def.insert(QStringLiteral("feather"), 0.80);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(QStringLiteral("Heal Brush ready. Paint over blemishes and distractions."));
}

void EditorPreviewController::startCloneBrush() {
  auto next_stack = normalize_mask_stack(mask_stack_);
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());

  auto mask_def = default_mask_definition(QStringLiteral("brush"), insert_index + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Clone Stamp %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask_def.insert(QStringLiteral("retouchMode"), QStringLiteral("clone"));
  mask_def.insert(QStringLiteral("brushSize"), 0.07);
  mask_def.insert(QStringLiteral("brushFlow"), 0.90);
  mask_def.insert(QStringLiteral("feather"), 0.74);
  mask_def.insert(QStringLiteral("cloneSourceSet"), false);
  mask_def.insert(QStringLiteral("cloneOffsetX"), 0.0);
  mask_def.insert(QStringLiteral("cloneOffsetY"), 0.0);
  mask_def.insert(QStringLiteral("cloneScale"), 1.0);
  mask_def.insert(QStringLiteral("cloneRotation"), 0.0);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(QStringLiteral("Clone Stamp ready. Pick a source point, then paint to copy texture."));
}

void EditorPreviewController::startPolygonSelection() {
  auto next_stack = normalize_mask_stack(mask_stack_);
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());

  auto mask_def = default_mask_definition(QStringLiteral("polygon"), insert_index + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Polygon Select %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask_def.insert(QStringLiteral("feather"), 0.04);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(QStringLiteral("Polygon selection started. Click to place points."));
}

void EditorPreviewController::appendActivePolygonPoint(double normalized_x, double normalized_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("polygon")) {
    return;
  }

  QVariantMap point;
  point.insert(QStringLiteral("x"), qBound(0.0, normalized_x, 1.0));
  point.insert(QStringLiteral("y"), qBound(0.0, normalized_y, 1.0));

  auto points = mask.value(QStringLiteral("polygonPoints")).toList();
  if (!points.isEmpty()) {
    const auto last = points.constLast().toMap();
    const auto dx = point.value(QStringLiteral("x")).toDouble() - last.value(QStringLiteral("x")).toDouble();
    const auto dy = point.value(QStringLiteral("y")).toDouble() - last.value(QStringLiteral("y")).toDouble();
    if (std::hypot(dx, dy) < 0.004) {
      return;
    }
  }
  points.append(point);
  mask.insert(QStringLiteral("polygonPoints"), points);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::removeLastActivePolygonPoint() {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("polygon")) {
    return;
  }
  auto points = mask.value(QStringLiteral("polygonPoints")).toList();
  if (points.isEmpty()) {
    return;
  }
  points.removeLast();
  mask.insert(QStringLiteral("polygonPoints"), points);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::clearActivePolygonPoints() {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("polygon")) {
    return;
  }
  mask.insert(QStringLiteral("polygonPoints"), QVariantList{});
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::duplicateActiveMask() {
  const auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }

  auto next_stack = normalize_mask_stack(mask_stack_);
  next_stack.insert(active_mask_index_ + 1, mask);
  applyMaskStackChange(next_stack, active_mask_index_ + 1);
}

void EditorPreviewController::removeActiveMask() {
  if (!hasActiveMask()) {
    return;
  }

  auto next_stack = normalize_mask_stack(mask_stack_);
  next_stack.removeAt(active_mask_index_);
  const auto next_index =
      next_stack.isEmpty()
          ? -1
          : qBound(0, active_mask_index_, static_cast<int>(next_stack.size()) - 1);
  applyMaskStackChange(next_stack, next_index);
}

void EditorPreviewController::moveActiveMask(int offset) {
  if (!hasActiveMask() || offset == 0) {
    return;
  }

  const auto target_index = qBound(
      0, active_mask_index_ + offset, static_cast<int>(mask_stack_.size()) - 1);
  if (target_index == active_mask_index_) {
    return;
  }

  auto next_stack = normalize_mask_stack(mask_stack_);
  next_stack.move(active_mask_index_, target_index);
  applyMaskStackChange(next_stack, target_index);
}

void EditorPreviewController::addBrushPoint(double normalized_x, double normalized_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }

  const auto clamped_x = qBound(0.0, normalized_x, 1.0);
  const auto clamped_y = qBound(0.0, normalized_y, 1.0);
  const auto retouch_mode = activeMaskRetouchMode();
  auto points = mask.value(QStringLiteral("brushPoints")).toList();
  if (retouch_mode == QStringLiteral("clone") &&
      map_bool_value(mask, QStringLiteral("cloneSourceSet"), false) &&
      points.isEmpty()) {
    mask.insert(
        QStringLiteral("cloneOffsetX"),
        map_double_value(mask, QStringLiteral("cloneSourceX"), 0.5) - clamped_x);
    mask.insert(
        QStringLiteral("cloneOffsetY"),
        map_double_value(mask, QStringLiteral("cloneSourceY"), 0.5) - clamped_y);
  }

  QVariantMap point;
  point.insert(QStringLiteral("x"), clamped_x);
  point.insert(QStringLiteral("y"), clamped_y);
  point.insert(QStringLiteral("size"), activeMaskBrushSize());
  point.insert(QStringLiteral("flow"), activeMaskBrushFlow());

  points.append(point);
  mask.insert(QStringLiteral("brushPoints"), points);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::clearActiveBrushPoints() {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPoints"), QVariantList{});
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskCloneSource(double normalized_x, double normalized_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush") ||
      activeMaskRetouchMode() != QStringLiteral("clone")) {
    return;
  }
  mask.insert(QStringLiteral("cloneSourceSet"), true);
  mask.insert(QStringLiteral("cloneSourceX"), qBound(0.0, normalized_x, 1.0));
  mask.insert(QStringLiteral("cloneSourceY"), qBound(0.0, normalized_y, 1.0));
  mask.insert(QStringLiteral("cloneOffsetX"), 0.0);
  mask.insert(QStringLiteral("cloneOffsetY"), 0.0);
  mask.insert(QStringLiteral("brushPoints"), QVariantList{});
  updateActiveMaskDefinition(mask);
  setStatus(QStringLiteral("Clone source picked. Paint to stamp from that area."));
}

void EditorPreviewController::setWhiteBalanceSampling(bool enabled) {
  if (white_balance_sampling_ == enabled) {
    return;
  }
  if (enabled && crop_editing_) {
    crop_editing_ = false;
    emit controlsChanged();
  }
  white_balance_sampling_ = enabled;
  emit whiteBalanceSamplingChanged();
}

void EditorPreviewController::sampleWhiteBalanceAt(double normalized_x, double normalized_y) {
  if (!white_balance_sampling_) {
    return;
  }

  QImage image;
  if (!decodeSourceImage(&image, 0)) {
    setStatus(QStringLiteral("Unable to decode source for white balance sampling."));
    setWhiteBalanceSampling(false);
    return;
  }

  image = applyCrop(image);

  const auto sample_x = qBound(0, static_cast<int>(qRound(normalized_x * (image.width() - 1))), image.width() - 1);
  const auto sample_y = qBound(0, static_cast<int>(qRound(normalized_y * (image.height() - 1))), image.height() - 1);
  const QColor sampled(image.pixel(sample_x, sample_y));

  const auto red = srgb_to_linear(sampled.redF());
  const auto green = srgb_to_linear(sampled.greenF());
  const auto blue = srgb_to_linear(sampled.blueF());
  const auto average_rb = qMax(1e-6, (red + blue) * 0.5);
  const auto red_blue_balance = std::log2((blue + 1e-6) / (red + 1e-6));
  const auto green_balance = std::log2((green + 1e-6) / average_rb);

  setTemperature(temperature_ + red_blue_balance * 55.0);
  setTint(tint_ + green_balance * 60.0);
  setWhiteBalanceSampling(false);
  setStatus(QStringLiteral("White balance sampled from preview."));
}

void EditorPreviewController::renderPreview() {
  ++render_generation_;
  qInfo().noquote() << "EditorPreviewController::renderPreview"
                    << "file_path=" << file_path_
                    << "has_adjustments=" << hasVisibleAdjustments();
  if (!preview_supported(file_path_)) {
    preview_source_ = fallback_thumbnail_source_;
    reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
    setStatus(hasAsset() ? QStringLiteral("Preview fallback active.") : QStringLiteral("No asset selected."));
    interactive_render_timer_.stop();
    full_render_timer_.stop();
    interactive_request_pending_ = false;
    full_request_pending_ = false;
    updateProcessingState();
    refreshDebugInfo();
    emit previewChanged();
    return;
  }

  const QFileInfo source_info(file_path_);
  if (!source_info.exists() || !source_info.isReadable()) {
    preview_source_ = fallback_thumbnail_source_;
    reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
    setStatus(QStringLiteral("Source file unavailable."));
    interactive_render_timer_.stop();
    full_render_timer_.stop();
    interactive_request_pending_ = false;
    full_request_pending_ = false;
    updateProcessingState();
    refreshDebugInfo();
    emit previewChanged();
    return;
  }

  setStatus(hasVisibleAdjustments() ? QStringLiteral("Rendering interactive preview...") : QStringLiteral("Rendering base preview..."));
  interactive_render_timer_.start();
  full_render_timer_.start();
  updateProcessingState();
}

void EditorPreviewController::renderPreviewStage(bool full_resolution) {
  const auto request = currentRenderRequest(full_resolution);
  if (request.file_path.isEmpty()) {
    updateProcessingState();
    return;
  }

  qInfo().noquote() << "EditorPreviewController::renderPreviewStage"
                    << (full_resolution ? "full" : "interactive")
                    << "file_path=" << request.file_path
                    << "generation=" << request.generation;

  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  auto& pending_request = full_resolution ? pending_full_request_ : pending_interactive_request_;
  auto& pending = full_resolution ? full_request_pending_ : interactive_request_pending_;

  if (full_resolution &&
      (interactive_render_timer_.isActive() || interactive_render_watcher_.isRunning() || interactive_request_pending_)) {
    pending_request = request;
    pending = true;
    updateProcessingState();
    return;
  }

  if (watcher.isRunning()) {
    pending_request = request;
    pending = true;
    updateProcessingState();
    return;
  }

  startRenderStage(full_resolution, request);
}

void EditorPreviewController::handleRenderFinished(bool full_resolution) {
  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  const auto result = watcher.result();
  const auto can_apply_result = [&]() {
    if (full_resolution) {
      return result.generation == render_generation_ && result.generation >= last_full_preview_generation_;
    }
    return result.generation >= last_interactive_preview_generation_;
  }();

  if (can_apply_result) {
    if (!result.success) {
      if (result.generation == render_generation_) {
        preview_source_ = fallback_thumbnail_source_;
        reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
        setStatus(result.status);
      }
    } else {
      ++preview_revision_;
      preview_source_ = with_preview_revision(result.preview_source, preview_revision_);
      reference_preview_source_ =
          with_preview_revision(result.reference_preview_source, preview_revision_);
      setStatus(result.status);
      if (full_resolution) {
        last_full_preview_generation_ = result.generation;
      } else {
        last_interactive_preview_generation_ = result.generation;
      }
    }
    refreshDebugInfo();
    emit previewChanged();

    if (result.success && !result.histogram_image.isNull()) {
      computeHistogram(result.histogram_image);
    }
  }

  auto& pending_request = full_resolution ? pending_full_request_ : pending_interactive_request_;
  auto& pending = full_resolution ? full_request_pending_ : interactive_request_pending_;
  if (pending) {
    pending = false;
    startRenderStage(full_resolution, pending_request);
    return;
  }

  if (!full_resolution && full_request_pending_ && !full_render_watcher_.isRunning()) {
    full_request_pending_ = false;
    startRenderStage(true, pending_full_request_);
    return;
  }

  updateProcessingState();
}

void EditorPreviewController::startRenderStage(bool full_resolution, const RenderRequest& request) {
  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  watcher.setFuture(QtConcurrent::run([request]() { return render_preview_request(request); }));
  updateProcessingState();
}

void EditorPreviewController::updateProcessingState() {
  const auto busy = interactive_render_timer_.isActive() ||
                    full_render_timer_.isActive() ||
                    interactive_render_watcher_.isRunning() ||
                    full_render_watcher_.isRunning() ||
                    interactive_request_pending_ ||
                    full_request_pending_;
  setProcessing(busy);
}

RenderRequest EditorPreviewController::currentRenderRequest(bool full_resolution) const {
  return RenderRequest{
      .file_path = file_path_,
      .fallback_thumbnail_source = fallback_thumbnail_source_,
      .state = currentState(),
      .compare_mode = compare_mode_,
      .bypass_crop = crop_editing_,
      .full_resolution = full_resolution,
      .max_edge = full_resolution ? 0 : previewTargetEdge(),
      .jpeg_quality = full_resolution ? 90 : 78,
      .generation = render_generation_,
  };
}

void EditorPreviewController::refreshDebugInfo() {
  const QFileInfo source_info(file_path_);
  const auto info = QStringLiteral("status=%1 | file=%2 | exists=%3 | readable=%4 | thumb=%5 | preview=%6 | reference=%7")
                        .arg(status_)
                        .arg(file_path_.isEmpty() ? QStringLiteral("<none>") : file_path_)
                        .arg(source_info.exists() ? QStringLiteral("yes") : QStringLiteral("no"))
                        .arg(source_info.isReadable() ? QStringLiteral("yes") : QStringLiteral("no"))
                        .arg(fallback_thumbnail_source_.isEmpty() ? QStringLiteral("<none>") : fallback_thumbnail_source_)
                        .arg(preview_source_.isEmpty() ? QStringLiteral("<none>") : preview_source_)
                        .arg(reference_preview_source_.isEmpty() ? QStringLiteral("<none>") : reference_preview_source_);
  if (debug_info_ == info) {
    return;
  }
  debug_info_ = info;
  emit debugInfoChanged();
}

void EditorPreviewController::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }
  status_ = status;
  emit statusChanged();
}

void EditorPreviewController::setProcessing(bool processing) {
  if (processing_ == processing) {
    return;
  }
  processing_ = processing;
  emit processingChanged();
}

QString EditorPreviewController::cachePathForSelection(bool edited, bool full_resolution) const {
  return cache_path_for_selection(file_path_, currentState(), edited, full_resolution, crop_editing_);
}

QVector<EditState>& EditorPreviewController::undoStackForAsset(const QString& asset_id) {
  return asset_undo_states_[asset_id];
}

QVector<EditState>& EditorPreviewController::redoStackForAsset(const QString& asset_id) {
  return asset_redo_states_[asset_id];
}

void EditorPreviewController::pushUndoState(const EditState& state) {
  if (asset_id_.isEmpty()) {
    return;
  }

  auto& undo_stack = undoStackForAsset(asset_id_);
  if (!undo_stack.isEmpty() && edit_states_equal(undo_stack.back(), state)) {
    return;
  }

  undo_stack.push_back(state);
  if (undo_stack.size() > kMaxEditHistoryDepth) {
    undo_stack.removeFirst();
  }
}

void EditorPreviewController::rememberStateForUndo() {
  if (asset_id_.isEmpty() || applying_history_state_) {
    return;
  }

  pushUndoState(currentState());
  redoStackForAsset(asset_id_).clear();
  emit historyChanged();
}

void EditorPreviewController::clearHistoryForAsset(const QString& asset_id) {
  if (asset_id.isEmpty()) {
    return;
  }

  asset_undo_states_.remove(asset_id);
  asset_redo_states_.remove(asset_id);
}

void EditorPreviewController::applyHistoryState(const EditState& state) {
  applying_history_state_ = true;
  applyState(state);
  persistCurrentState();
  applying_history_state_ = false;
  emit historyChanged();
  renderPreview();
}

void EditorPreviewController::persistCurrentState() {
  if (asset_id_.isEmpty()) {
    return;
  }
  asset_states_.insert(asset_id_, currentState());
  persist_timer_.start();
}

void EditorPreviewController::restoreStateForAsset(const QString& asset_id, const QString& recipe_json) {
  if (asset_id.isEmpty()) {
    applyState(EditState{});
    emit historyChanged();
    return;
  }

  // Track access order and prune oldest assets beyond limit
  constexpr int kMaxTrackedAssets = 20;
  asset_access_order_.removeOne(asset_id);
  asset_access_order_.prepend(asset_id);
  while (asset_access_order_.size() > kMaxTrackedAssets) {
    const auto& evict_id = asset_access_order_.last();
    asset_states_.remove(evict_id);
    asset_undo_states_.remove(evict_id);
    asset_redo_states_.remove(evict_id);
    asset_access_order_.removeLast();
  }

  if (asset_states_.contains(asset_id)) {
    applyState(asset_states_.value(asset_id));
    emit historyChanged();
    return;
  }

  clearHistoryForAsset(asset_id);
  applyState(stateFromRecipeJson(recipe_json));
  emit historyChanged();
}

void EditorPreviewController::applyState(const EditState& state) {
  exposure_ = state.exposure;
  contrast_ = state.contrast;
  highlights_ = state.highlights;
  shadows_ = state.shadows;
  whites_ = state.whites;
  blacks_ = state.blacks;
  temperature_ = state.temperature;
  tint_ = state.tint;
  texture_ = state.texture;
  clarity_ = state.clarity;
  dehaze_ = state.dehaze;
  sharpening_ = state.sharpening;
  sharpen_masking_ = state.sharpen_masking;
  luminance_nr_ = state.luminance_nr;
  color_nr_ = state.color_nr;
  lens_distortion_ = state.lens_distortion;
  lens_vignette_ = state.lens_vignette;
  chromatic_aberration_ = state.chromatic_aberration;
  vibrance_ = state.vibrance;
  saturation_ = state.saturation;
  curve_shadows_ = state.curve_shadows;
  curve_darks_ = state.curve_darks;
  curve_lights_ = state.curve_lights;
  curve_highlights_ = state.curve_highlights;
  master_curve_points_ = state.master_curve_points;
  red_curve_points_ = state.red_curve_points;
  green_curve_points_ = state.green_curve_points;
  blue_curve_points_ = state.blue_curve_points;
  hsl_channel_ = state.hsl_channel;
  hue_adjustment_ = state.hue_adjustment;
  saturation_adjustment_ = state.saturation_adjustment;
  luminance_adjustment_ = state.luminance_adjustment;
  for (std::size_t i = 0; i < 8; ++i) {
    mixer_channels_[i].hue = state.mixer_channels[i].hue;
    mixer_channels_[i].saturation = state.mixer_channels[i].saturation;
    mixer_channels_[i].luminance = state.mixer_channels[i].luminance;
  }
  grading_shadows_hue_ = state.grading_shadows_hue;
  grading_shadows_saturation_ = state.grading_shadows_saturation;
  grading_shadows_luminance_ = state.grading_shadows_luminance;
  grading_midtones_hue_ = state.grading_midtones_hue;
  grading_midtones_saturation_ = state.grading_midtones_saturation;
  grading_midtones_luminance_ = state.grading_midtones_luminance;
  grading_highlights_hue_ = state.grading_highlights_hue;
  grading_highlights_saturation_ = state.grading_highlights_saturation;
  grading_highlights_luminance_ = state.grading_highlights_luminance;
  crop_rect_x_ = state.crop_rect_x;
  crop_rect_y_ = state.crop_rect_y;
  crop_rect_width_ = state.crop_rect_width;
  crop_rect_height_ = state.crop_rect_height;
  crop_zoom_ = state.crop_zoom;
  crop_preset_ = state.crop_preset;
  mask_stack_ = normalize_mask_stack(state.mask_stack);
  active_mask_index_ = mask_stack_.isEmpty()
                           ? -1
                           : qBound(0, active_mask_index_, static_cast<int>(mask_stack_.size()) - 1);
  synchronizeMaskState();
  emit controlsChanged();
}

EditState EditorPreviewController::currentState() const {
  return EditState{
      .exposure = exposure_,
      .contrast = contrast_,
      .highlights = highlights_,
      .shadows = shadows_,
      .whites = whites_,
      .blacks = blacks_,
      .temperature = temperature_,
      .tint = tint_,
      .texture = texture_,
      .clarity = clarity_,
      .dehaze = dehaze_,
      .sharpening = sharpening_,
      .sharpen_masking = sharpen_masking_,
      .luminance_nr = luminance_nr_,
      .color_nr = color_nr_,
      .lens_distortion = lens_distortion_,
      .lens_vignette = lens_vignette_,
      .chromatic_aberration = chromatic_aberration_,
      .vibrance = vibrance_,
      .saturation = saturation_,
      .curve_shadows = curve_shadows_,
      .curve_darks = curve_darks_,
      .curve_lights = curve_lights_,
      .curve_highlights = curve_highlights_,
      .master_curve_points = master_curve_points_,
      .red_curve_points = red_curve_points_,
      .green_curve_points = green_curve_points_,
      .blue_curve_points = blue_curve_points_,
      .hsl_channel = hsl_channel_,
      .hue_adjustment = hue_adjustment_,
      .saturation_adjustment = saturation_adjustment_,
      .luminance_adjustment = luminance_adjustment_,
      .mixer_channels = [this]() {
        std::array<EditRecipeState::MixerChannel, 8> channels{};
        for (std::size_t i = 0; i < 8; ++i) {
          channels[i].hue = mixer_channels_[i].hue;
          channels[i].saturation = mixer_channels_[i].saturation;
          channels[i].luminance = mixer_channels_[i].luminance;
        }
        return channels;
      }(),
      .grading_shadows_hue = grading_shadows_hue_,
      .grading_shadows_saturation = grading_shadows_saturation_,
      .grading_shadows_luminance = grading_shadows_luminance_,
      .grading_midtones_hue = grading_midtones_hue_,
      .grading_midtones_saturation = grading_midtones_saturation_,
      .grading_midtones_luminance = grading_midtones_luminance_,
      .grading_highlights_hue = grading_highlights_hue_,
      .grading_highlights_saturation = grading_highlights_saturation_,
      .grading_highlights_luminance = grading_highlights_luminance_,
      .crop_rect_x = crop_rect_x_,
      .crop_rect_y = crop_rect_y_,
      .crop_rect_width = crop_rect_width_,
      .crop_rect_height = crop_rect_height_,
      .crop_zoom = crop_zoom_,
      .crop_preset = crop_preset_,
      .mask_stack = mask_stack_,
      .linear_mask_enabled = linear_mask_enabled_,
      .linear_mask_exposure = linear_mask_exposure_,
      .linear_mask_temperature = linear_mask_temperature_,
      .linear_mask_feather = linear_mask_feather_,
      .linear_mask_position = linear_mask_position_,
      .linear_mask_rotation = linear_mask_rotation_,
      .radial_mask_enabled = radial_mask_enabled_,
      .radial_mask_exposure = radial_mask_exposure_,
      .radial_mask_temperature = radial_mask_temperature_,
      .radial_mask_feather = radial_mask_feather_,
      .radial_mask_center_x = radial_mask_center_x_,
      .radial_mask_center_y = radial_mask_center_y_,
      .radial_mask_radius_x = radial_mask_radius_x_,
      .radial_mask_radius_y = radial_mask_radius_y_,
      .radial_mask_rotation = radial_mask_rotation_,
  };
}

EditState EditorPreviewController::stateFromRecipeJson(const QString& recipe_json) const {
  return parse_edit_recipe_json(recipe_json);
}

QString EditorPreviewController::recipeJsonForState(const EditState& state) const {
  return serialize_edit_recipe_json(state);
}

QString EditorPreviewController::normalizedPresetName(const QString& name) const {
  return name.simplified();
}

int EditorPreviewController::firstMaskIndexForKind(const QString& kind) const {
  const auto normalized_kind = normalized_mask_kind(kind);
  for (auto index = 0; index < mask_stack_.size(); ++index) {
    const auto mask = maskDefinitionAt(index);
    if (mask.value(QStringLiteral("kind")).toString() == normalized_kind) {
      return index;
    }
  }
  return -1;
}

QVariantMap EditorPreviewController::activeMaskDefinition() const {
  if (!hasActiveMask()) {
    return {};
  }
  return maskDefinitionAt(active_mask_index_);
}

QVariantMap EditorPreviewController::maskDefinitionAt(int index) const {
  if (index < 0 || index >= mask_stack_.size()) {
    return {};
  }
  return normalized_mask_definition(mask_stack_.at(index).toMap(), index + 1);
}

void EditorPreviewController::synchronizeMaskState() {
  if (active_mask_index_ >= static_cast<int>(mask_stack_.size())) {
    active_mask_index_ = mask_stack_.isEmpty() ? -1 : static_cast<int>(mask_stack_.size()) - 1;
  }

  const auto linear_index = firstMaskIndexForKind(QStringLiteral("linear"));
  if (linear_index >= 0) {
    const auto linear_mask = maskDefinitionAt(linear_index);
    linear_mask_enabled_ = map_bool_value(linear_mask, QStringLiteral("enabled"), false);
    linear_mask_exposure_ = map_double_value(linear_mask, QStringLiteral("exposure"), 0.0);
    linear_mask_temperature_ = map_double_value(linear_mask, QStringLiteral("temperature"), 0.0);
    linear_mask_feather_ = map_double_value(linear_mask, QStringLiteral("feather"), 0.22);
    linear_mask_position_ = map_double_value(linear_mask, QStringLiteral("position"), 0.5);
    linear_mask_rotation_ = map_double_value(linear_mask, QStringLiteral("rotation"), 0.0);
  } else {
    linear_mask_enabled_ = false;
    linear_mask_exposure_ = 0.0;
    linear_mask_temperature_ = 0.0;
    linear_mask_feather_ = 0.22;
    linear_mask_position_ = 0.5;
    linear_mask_rotation_ = 0.0;
  }

  const auto radial_index = firstMaskIndexForKind(QStringLiteral("radial"));
  if (radial_index >= 0) {
    const auto radial_mask = maskDefinitionAt(radial_index);
    radial_mask_enabled_ = map_bool_value(radial_mask, QStringLiteral("enabled"), false);
    radial_mask_exposure_ = map_double_value(radial_mask, QStringLiteral("exposure"), 0.0);
    radial_mask_temperature_ = map_double_value(radial_mask, QStringLiteral("temperature"), 0.0);
    radial_mask_feather_ = map_double_value(radial_mask, QStringLiteral("feather"), 0.25);
    radial_mask_center_x_ = map_double_value(radial_mask, QStringLiteral("centerX"), 0.5);
    radial_mask_center_y_ = map_double_value(radial_mask, QStringLiteral("centerY"), 0.5);
    radial_mask_radius_x_ = map_double_value(radial_mask, QStringLiteral("radiusX"), 0.25);
    radial_mask_radius_y_ = map_double_value(radial_mask, QStringLiteral("radiusY"), 0.25);
    radial_mask_rotation_ = map_double_value(radial_mask, QStringLiteral("rotation"), 0.0);
  } else {
    radial_mask_enabled_ = false;
    radial_mask_exposure_ = 0.0;
    radial_mask_temperature_ = 0.0;
    radial_mask_feather_ = 0.25;
    radial_mask_center_x_ = 0.5;
    radial_mask_center_y_ = 0.5;
    radial_mask_radius_x_ = 0.25;
    radial_mask_radius_y_ = 0.25;
    radial_mask_rotation_ = 0.0;
  }
}

bool EditorPreviewController::applyMaskStackChange(
    const QVariantList& stack,
    int active_index,
    bool rerender) {
  const auto normalized_stack = normalize_mask_stack(stack);
  const auto normalized_active_index =
      normalized_stack.isEmpty()
          ? -1
          : qBound(0, active_index, static_cast<int>(normalized_stack.size()) - 1);
  if (normalized_stack == mask_stack_ && normalized_active_index == active_mask_index_) {
    return false;
  }

  rememberStateForUndo();
  mask_stack_ = normalized_stack;
  active_mask_index_ = normalized_active_index;
  synchronizeMaskState();
  persistCurrentState();
  emit controlsChanged();
  if (rerender) {
    renderPreview();
  }
  return true;
}

bool EditorPreviewController::updateMaskAt(int index, const QVariantMap& mask, bool rerender) {
  if (index < 0 || index >= mask_stack_.size()) {
    return false;
  }
  auto next_stack = normalize_mask_stack(mask_stack_);
  next_stack[index] = normalized_mask_definition(mask, index + 1);
  return applyMaskStackChange(next_stack, active_mask_index_, rerender);
}

bool EditorPreviewController::updateActiveMaskDefinition(const QVariantMap& mask, bool rerender) {
  return updateMaskAt(active_mask_index_, mask, rerender);
}

bool EditorPreviewController::updateLegacyKindMask(const QString& kind, const QVariantMap& updates) {
  const auto normalized_kind = normalized_mask_kind(kind);
  auto index = firstMaskIndexForKind(normalized_kind);
  auto next_stack = normalize_mask_stack(mask_stack_);
  QVariantMap mask;
  if (index >= 0) {
    mask = normalized_mask_definition(next_stack.at(index).toMap(), index + 1);
  } else {
    index = static_cast<int>(next_stack.size());
    mask = default_mask_definition(normalized_kind, index + 1);
    next_stack.append(mask);
  }

  for (auto it = updates.cbegin(); it != updates.cend(); ++it) {
    mask.insert(it.key(), it.value());
  }
  next_stack[index] = normalized_mask_definition(mask, index + 1);
  return applyMaskStackChange(next_stack, index);
}

QString EditorPreviewController::presetsFilePath() const {
  return QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("editor-presets.json"));
}

void EditorPreviewController::loadPresets() {
  presets_.clear();

  QFile file(presetsFilePath());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return;
  }

  const auto preset_array = document.object().value(QStringLiteral("presets")).toArray();
  for (const auto& preset_value : preset_array) {
    if (!preset_value.isObject()) {
      continue;
    }

    const auto preset_object = preset_value.toObject();
    const auto name = normalizedPresetName(preset_object.value(QStringLiteral("name")).toString());
    const auto recipe_json = preset_object.value(QStringLiteral("recipeJson")).toString();
    if (name.isEmpty() || recipe_json.trimmed().isEmpty()) {
      continue;
    }

    presets_.insert(name, stateFromRecipeJson(recipe_json));
  }
}

void EditorPreviewController::savePresets() const {
  const QFileInfo destination_info(presetsFilePath());
  QDir().mkpath(destination_info.dir().absolutePath());

  QJsonArray preset_array;
  for (auto iterator = presets_.cbegin(); iterator != presets_.cend(); ++iterator) {
    QJsonObject preset_object;
    preset_object.insert(QStringLiteral("name"), iterator.key());
    preset_object.insert(QStringLiteral("recipeJson"), recipeJsonForState(iterator.value()));
    preset_array.push_back(preset_object);
  }

  QJsonObject root;
  root.insert(QStringLiteral("presets"), preset_array);

  QSaveFile file(presetsFilePath());
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
}

void EditorPreviewController::flushPersistedState() {
  if (asset_id_.isEmpty()) {
    return;
  }

  if (persist_process_.state() != QProcess::NotRunning) {
    persist_timer_.start();
    return;
  }

  const auto recipe_json = recipeJsonForState(currentState());
  persist_process_.setWorkingDirectory(QDir::currentPath());
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    setStatus(environment_error.isEmpty() ? QStringLiteral("Edit recipe save is unavailable.") : environment_error);
    refreshDebugInfo();
    return;
  }
  persist_process_.setProcessEnvironment(environment);
  persist_process_.start(saveRecipeProgram(), saveRecipeArguments(asset_id_, recipe_json));
}

bool EditorPreviewController::hasVisibleAdjustments() const {
  return has_visible_adjustments(currentState());
}

bool EditorPreviewController::decodeSourceImage(QImage* image, int max_edge) const {
  return decode_source_image(file_path_, image, max_edge);
}

QSize EditorPreviewController::sourceImageSize() const {
  if (!preview_supported(file_path_)) {
    return {};
  }

  QImageReader reader(file_path_);
  if (!reader.canRead()) {
    return {};
  }

  return reader.size();
}

QImage EditorPreviewController::applyCrop(const QImage& image) const {
  return apply_crop(image, currentState());
}

void EditorPreviewController::applyColorAdjustments(QImage* image) const {
  apply_color_adjustments(image, currentState());
}

void EditorPreviewController::resetSmartSelectPreview() {
  if (smart_select_preview_instance_id_ == 0 && smart_select_preview_mask_.isEmpty()) {
    return;
  }
  smart_select_preview_instance_id_ = 0;
  smart_select_preview_mask_.clear();
  emit smartSelectPreviewChanged();
}

void EditorPreviewController::resetSmartSelectInstanceMap() {
  smart_select_instance_map_key_.clear();
  smart_select_instance_map_data_.clear();
  smart_select_instance_map_width_ = 0;
  smart_select_instance_map_height_ = 0;
  resetSmartSelectPreview();
}

bool EditorPreviewController::ensureSmartSelectInstanceMap() {
  if (!vision_segmentation_available() || file_path_.isEmpty()) {
    resetSmartSelectInstanceMap();
    return false;
  }

  const auto source_info = QFileInfo(file_path_);
  const auto source_key = decoded_image_cache_key(source_info, 1024);
  if (source_key == smart_select_instance_map_key_ &&
      !smart_select_instance_map_data_.isEmpty() &&
      smart_select_instance_map_width_ > 0 &&
      smart_select_instance_map_height_ > 0) {
    return true;
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    resetSmartSelectInstanceMap();
    return false;
  }

  const auto instance_map = generate_foreground_instance_map(source_image);
  if (!instance_map.success) {
    resetSmartSelectInstanceMap();
    return false;
  }

  smart_select_instance_map_key_ = source_key;
  smart_select_instance_map_data_ = instance_map.instance_data;
  smart_select_instance_map_width_ = instance_map.width;
  smart_select_instance_map_height_ = instance_map.height;
  return true;
}

int EditorPreviewController::smartSelectInstanceIdAt(
    double normalized_x,
    double normalized_y) const {
  return instance_id_from_map(
      smart_select_instance_map_data_,
      smart_select_instance_map_width_,
      smart_select_instance_map_height_,
      qBound(0.0, normalized_x, 1.0),
      qBound(0.0, normalized_y, 1.0));
}

QVariantMap EditorPreviewController::smartSelectPreviewMaskForInstance(int instance_id) const {
  return segmentation_mask_for_instance(
      smart_select_instance_map_data_,
      smart_select_instance_map_width_,
      smart_select_instance_map_height_,
      instance_id);
}

bool EditorPreviewController::channelMatchesSelection(const QColor& color) const {
  return channel_weight_for_selection(color.hslHueF(), hsl_channel_) > 0.0;
}

QString EditorPreviewController::saveRecipeProgram() const {
  if (useBuiltSaveRecipeBinary()) {
    return builtBinaryPath(QStringLiteral("save_edit_recipe"));
  }

  return QStringLiteral("cargo");
}

QStringList EditorPreviewController::saveRecipeArguments(const QString& asset_id, const QString& recipe_json) const {
  if (useBuiltSaveRecipeBinary()) {
    return {asset_id, recipe_json};
  }

  return {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("save_edit_recipe"),
      QStringLiteral("--"),
      asset_id,
      recipe_json,
  };
}

bool EditorPreviewController::useBuiltSaveRecipeBinary() const {
  const QFileInfo binary(builtBinaryPath(QStringLiteral("save_edit_recipe")));
  return binary.exists() && binary.isExecutable();
}

QString EditorPreviewController::builtBinaryPath(const QString& binary_name) const {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

void EditorPreviewController::computeHistogram(const QImage& image) {
  constexpr int kBins = 256;
  std::array<int, kBins> red_bins{};
  std::array<int, kBins> green_bins{};
  std::array<int, kBins> blue_bins{};
  std::array<int, kBins> luma_bins{};

  const auto w = image.width();
  const auto h = image.height();
  for (int y = 0; y < h; ++y) {
    const auto* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < w; ++x) {
      const auto pixel = scanline[x];
      const auto r = qRed(pixel);
      const auto g = qGreen(pixel);
      const auto b = qBlue(pixel);
      ++red_bins[static_cast<std::size_t>(r)];
      ++green_bins[static_cast<std::size_t>(g)];
      ++blue_bins[static_cast<std::size_t>(b)];
      // ITU-R BT.709 luminance
      const auto luma = static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5);
      ++luma_bins[static_cast<std::size_t>(std::clamp(luma, 0, 255))];
    }
  }

  // Normalize to [0, 1] range for display
  const auto max_count = std::max({
      *std::max_element(red_bins.begin(), red_bins.end()),
      *std::max_element(green_bins.begin(), green_bins.end()),
      *std::max_element(blue_bins.begin(), blue_bins.end()),
      *std::max_element(luma_bins.begin(), luma_bins.end()),
  });

  if (max_count <= 0) {
    return;
  }

  const auto scale = 1.0 / static_cast<double>(max_count);
  QVariantList r_list, g_list, b_list, l_list;
  r_list.reserve(kBins);
  g_list.reserve(kBins);
  b_list.reserve(kBins);
  l_list.reserve(kBins);
  for (int i = 0; i < kBins; ++i) {
    r_list.append(static_cast<double>(red_bins[static_cast<std::size_t>(i)]) * scale);
    g_list.append(static_cast<double>(green_bins[static_cast<std::size_t>(i)]) * scale);
    b_list.append(static_cast<double>(blue_bins[static_cast<std::size_t>(i)]) * scale);
    l_list.append(static_cast<double>(luma_bins[static_cast<std::size_t>(i)]) * scale);
  }

  histogram_red_ = std::move(r_list);
  histogram_green_ = std::move(g_list);
  histogram_blue_ = std::move(b_list);
  histogram_luminance_ = std::move(l_list);
  emit histogramChanged();
}

}  // namespace photo_editor::desktop

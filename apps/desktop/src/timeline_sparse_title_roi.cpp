#include "timeline_sparse_title_roi.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QWaitCondition>
#include <QtGlobal>

#include <cmath>
#include <limits>

namespace photo_editor::desktop::timeline_motion::sparse_title_roi {

namespace {

constexpr qint64 kMaximumAlphaSupportPngBytes = 64 * 1024 * 1024;
constexpr qint64 kMaximumAlphaSupportPixels = 4096LL * 2304LL;
constexpr int kMaximumConcurrentAlphaSupportRequests = 2;

struct CachedAlphaSupport final {
  QByteArray sha256;
  QSize image_size;
  QRect bounds;
};

struct AlphaSupportCache final {
  QMutex mutex;
  QWaitCondition completed;
  QHash<QString, CachedAlphaSupport> entries_by_path;
  QHash<QString, QByteArray> latest_sha256_by_path;
  QStringList least_to_most_recent_paths;
  QSet<QString> in_flight;
  int active_requests = 0;
};

struct AlphaSupportRequestLease final {
  AlphaSupportCache& cache;

  explicit AlphaSupportRequestLease(AlphaSupportCache& cache_value)
      : cache(cache_value) {}

  AlphaSupportRequestLease(const AlphaSupportRequestLease&) = delete;
  AlphaSupportRequestLease& operator=(const AlphaSupportRequestLease&) = delete;

  ~AlphaSupportRequestLease() {
    QMutexLocker locker(&cache.mutex);
    --cache.active_requests;
    cache.completed.wakeAll();
  }
};

AlphaSupportCache& alpha_support_cache() {
  static AlphaSupportCache cache;
  return cache;
}

void touch_alpha_support_path(AlphaSupportCache& cache, const QString& path) {
  cache.least_to_most_recent_paths.removeAll(path);
  cache.least_to_most_recent_paths.append(path);
}

void trim_alpha_support_cache(AlphaSupportCache& cache) {
  while (cache.least_to_most_recent_paths.size() > kAlphaSupportCacheCapacity) {
    const auto oldest_path = cache.least_to_most_recent_paths.takeFirst();
    cache.entries_by_path.remove(oldest_path);
    cache.latest_sha256_by_path.remove(oldest_path);
  }
}

PngAlphaSupport cache_hit_result(const CachedAlphaSupport& cached) {
  return {cached.image_size, cached.bounds, true, true, false};
}

}  // namespace

QRect nonzero_alpha_bounds(const QImage& image) {
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    return {};
  }
  if (!image.hasAlphaChannel()) {
    return image.rect();
  }

  auto scan_image = image;
  if (scan_image.format() != QImage::Format_ARGB32 &&
      scan_image.format() != QImage::Format_ARGB32_Premultiplied) {
    scan_image = scan_image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  }
  if (scan_image.isNull()) {
    return {};
  }

  auto minimum_x = scan_image.width();
  auto minimum_y = scan_image.height();
  auto maximum_x = -1;
  auto maximum_y = -1;
  for (auto y = 0; y < scan_image.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgb*>(scan_image.constScanLine(y));
    for (auto x = 0; x < scan_image.width(); ++x) {
      if (qAlpha(row[x]) == 0) {
        continue;
      }
      minimum_x = qMin(minimum_x, x);
      minimum_y = qMin(minimum_y, y);
      maximum_x = qMax(maximum_x, x);
      maximum_y = qMax(maximum_y, y);
    }
  }
  if (maximum_x < minimum_x || maximum_y < minimum_y) {
    return {};
  }
  return QRect(minimum_x, minimum_y, maximum_x - minimum_x + 1,
               maximum_y - minimum_y + 1);
}

PngAlphaSupport png_alpha_support_for_path(const QString& path) {
  const auto canonical_path = QFileInfo(path).canonicalFilePath();
  if (canonical_path.isEmpty()) {
    return {};
  }

  auto& cache = alpha_support_cache();
  {
    QMutexLocker locker(&cache.mutex);
    while (cache.active_requests >= kMaximumConcurrentAlphaSupportRequests) {
      cache.completed.wait(&cache.mutex);
    }
    ++cache.active_requests;
  }
  const AlphaSupportRequestLease request_lease{cache};

  QFile file(canonical_path);
  QByteArray bytes;
  auto readable = file.open(QIODevice::ReadOnly);
  if (readable) {
    const auto file_size = file.size();
    if (file_size <= 0 || file_size > kMaximumAlphaSupportPngBytes) {
      readable = false;
    } else {
      bytes = file.readAll();
      readable = file.error() == QFileDevice::NoError &&
                 static_cast<qint64>(bytes.size()) == file_size;
    }
  }
  if (!readable) {
    return {};
  }
  const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  const auto in_flight_key = canonical_path + QChar(0x1f) +
      QString::fromLatin1(digest.toHex());

  QMutexLocker locker(&cache.mutex);
  const auto current_sha = cache.latest_sha256_by_path.constFind(canonical_path);
  if (current_sha == cache.latest_sha256_by_path.cend() ||
      current_sha.value() != digest) {
    cache.latest_sha256_by_path.insert(canonical_path, digest);
    cache.entries_by_path.remove(canonical_path);
  }
  touch_alpha_support_path(cache, canonical_path);
  trim_alpha_support_cache(cache);

  const auto cached = cache.entries_by_path.constFind(canonical_path);
  if (cached != cache.entries_by_path.cend() && cached->sha256 == digest) {
    return cache_hit_result(cached.value());
  }

  while (true) {
    const auto completed = cache.entries_by_path.constFind(canonical_path);
    if (completed != cache.entries_by_path.cend() && completed->sha256 == digest) {
      touch_alpha_support_path(cache, canonical_path);
      return cache_hit_result(completed.value());
    }
    if (cache.in_flight.contains(in_flight_key)) {
      cache.completed.wait(&cache.mutex);
      continue;
    }
    cache.in_flight.insert(in_flight_key);
    break;
  }
  locker.unlock();

  QBuffer buffer;
  buffer.setData(bytes);
  QImage image;
  if (buffer.open(QIODevice::ReadOnly)) {
    QImageReader reader(&buffer);
    const auto reported_size = reader.size();
    const auto reported_pixels = static_cast<qint64>(reported_size.width()) *
                                 static_cast<qint64>(reported_size.height());
    if (reported_size.width() > 0 && reported_size.height() > 0 &&
        reported_pixels <= kMaximumAlphaSupportPixels) {
      image = reader.read();
    }
  }
  const auto image_size = image.size();
  const auto pixel_count = static_cast<qint64>(image_size.width()) *
                           static_cast<qint64>(image_size.height());
  const auto decoded = !image.isNull() && image_size.isValid() &&
                       !image_size.isEmpty() &&
                       pixel_count <= kMaximumAlphaSupportPixels;
  const auto bounds = decoded ? nonzero_alpha_bounds(image) : QRect{};

  locker.relock();
  cache.in_flight.remove(in_flight_key);
  if (decoded && cache.latest_sha256_by_path.value(canonical_path) == digest) {
    cache.entries_by_path.insert(canonical_path, {digest, image_size, bounds});
    touch_alpha_support_path(cache, canonical_path);
    trim_alpha_support_cache(cache);
  } else if (!decoded &&
             cache.latest_sha256_by_path.value(canonical_path) == digest &&
             !cache.entries_by_path.contains(canonical_path)) {
    cache.latest_sha256_by_path.remove(canonical_path);
    cache.least_to_most_recent_paths.removeAll(canonical_path);
  }
  cache.completed.wakeAll();
  return decoded ? PngAlphaSupport{image_size, bounds, true, false, true}
                 : PngAlphaSupport{};
}

QSize maximum_roi_size(const QRect& alpha_bounds,
                       const QSize& source_canvas,
                       const QVector<QSize>& scaled_canvas_sizes,
                       int support_pixels) {
  if (alpha_bounds.isEmpty() || !source_canvas.isValid() ||
      source_canvas.isEmpty() || scaled_canvas_sizes.isEmpty() ||
      support_pixels < 0 ||
      alpha_bounds.intersected(QRect(QPoint(0, 0), source_canvas)) != alpha_bounds) {
    return {};
  }

  auto maximum_width = qint64{0};
  auto maximum_height = qint64{0};
  const auto right_alpha = static_cast<qint64>(alpha_bounds.x()) +
                           alpha_bounds.width();
  const auto bottom_alpha = static_cast<qint64>(alpha_bounds.y()) +
                            alpha_bounds.height();
  for (const auto& scaled_size : scaled_canvas_sizes) {
    if (!scaled_size.isValid() || scaled_size.isEmpty()) {
      return {};
    }
    const auto scaled_left = static_cast<long double>(alpha_bounds.x()) *
                             static_cast<long double>(scaled_size.width()) /
                             static_cast<long double>(source_canvas.width());
    const auto scaled_top = static_cast<long double>(alpha_bounds.y()) *
                            static_cast<long double>(scaled_size.height()) /
                            static_cast<long double>(source_canvas.height());
    const auto scaled_right = static_cast<long double>(right_alpha) *
                              static_cast<long double>(scaled_size.width()) /
                              static_cast<long double>(source_canvas.width());
    const auto scaled_bottom = static_cast<long double>(bottom_alpha) *
                               static_cast<long double>(scaled_size.height()) /
                               static_cast<long double>(source_canvas.height());
    if (!std::isfinite(scaled_left) || !std::isfinite(scaled_top) ||
        !std::isfinite(scaled_right) || !std::isfinite(scaled_bottom)) {
      return {};
    }
    const auto left = qMax<qint64>(
        0, static_cast<qint64>(std::floor(scaled_left)) - support_pixels);
    const auto top = qMax<qint64>(
        0, static_cast<qint64>(std::floor(scaled_top)) - support_pixels);
    const auto right = static_cast<qint64>(std::ceil(scaled_right)) + support_pixels;
    const auto bottom = static_cast<qint64>(std::ceil(scaled_bottom)) + support_pixels;
    maximum_width = qMax(maximum_width, right - left);
    maximum_height = qMax(maximum_height, bottom - top);
  }
  if (maximum_width <= 0 || maximum_height <= 0 ||
      maximum_width > std::numeric_limits<int>::max() ||
      maximum_height > std::numeric_limits<int>::max()) {
    return {};
  }
  return QSize(static_cast<int>(maximum_width),
               static_cast<int>(maximum_height));
}

bool materially_smaller_than_canvas(const QSize& roi_size,
                                    const QSize& canvas_size) {
  if (!roi_size.isValid() || roi_size.isEmpty() ||
      !canvas_size.isValid() || canvas_size.isEmpty() ||
      roi_size.width() > canvas_size.width() ||
      roi_size.height() > canvas_size.height()) {
    return false;
  }
  const auto roi_pixels = static_cast<long double>(roi_size.width()) *
                          static_cast<long double>(roi_size.height());
  const auto canvas_pixels = static_cast<long double>(canvas_size.width()) *
                             static_cast<long double>(canvas_size.height());
  return roi_pixels * 2.0L < canvas_pixels;
}

QString roi_origin_expression(int alpha_origin,
                              const QString& scaled_extent_expression,
                              int source_extent,
                              int support_pixels) {
  if (alpha_origin < 0 || scaled_extent_expression.trimmed().isEmpty() ||
      source_extent <= 0 || support_pixels < 0) {
    return {};
  }
  return QStringLiteral("max(0,floor((%1)*(%2)/%3)-%4)")
      .arg(alpha_origin)
      .arg(scaled_extent_expression)
      .arg(source_extent)
      .arg(support_pixels);
}

QString overlay_origin_expression(int canvas_extent,
                                  const QString& roi_origin,
                                  const QString& native_crop_origin) {
  if (canvas_extent <= 0 || roi_origin.trimmed().isEmpty() ||
      native_crop_origin.trimmed().isEmpty()) {
    return {};
  }
  return QStringLiteral("(%1+(%2)-floor((%3)+0.5))")
      .arg(canvas_extent)
      .arg(roi_origin)
      .arg(native_crop_origin);
}

}  // namespace photo_editor::desktop::timeline_motion::sparse_title_roi

#include "timeline_sparse_title_roi.h"

#include <QBuffer>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPair>
#include <QRect>
#include <QSize>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QVector>

#include <atomic>
#include <iostream>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

using photo_editor::desktop::timeline_motion::sparse_title_roi::
    materially_smaller_than_canvas;
using photo_editor::desktop::timeline_motion::sparse_title_roi::
    png_alpha_support_for_path;
using photo_editor::desktop::timeline_motion::sparse_title_roi::
    PngAlphaSupport;
using photo_editor::desktop::timeline_motion::sparse_title_roi::
    maximum_roi_size;
using photo_editor::desktop::timeline_motion::sparse_title_roi::
    nonzero_alpha_bounds;
using photo_editor::desktop::timeline_motion::sparse_title_roi::
    overlay_origin_expression;
using photo_editor::desktop::timeline_motion::sparse_title_roi::
    roi_origin_expression;

namespace {

QByteArray encode_png(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }
  return bytes;
}

void append_uint32_be(QByteArray& bytes, quint32 value) {
  bytes.append(static_cast<char>((value >> 24U) & 0xffU));
  bytes.append(static_cast<char>((value >> 16U) & 0xffU));
  bytes.append(static_cast<char>((value >> 8U) & 0xffU));
  bytes.append(static_cast<char>(value & 0xffU));
}

quint32 png_crc32(const QByteArray& bytes) {
  auto crc = quint32{0xffffffffU};
  for (const auto byte : bytes) {
    crc ^= static_cast<quint8>(byte);
    for (auto bit = 0; bit < 8; ++bit) {
      const auto mask = static_cast<quint32>(
          -static_cast<qint32>(crc & 1U));
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return crc ^ 0xffffffffU;
}

QByteArray png_with_exact_size(QByteArray bytes,
                               qsizetype target_size,
                               bool* ok) {
  const auto iend_type = bytes.lastIndexOf(QByteArrayLiteral("IEND"));
  if (bytes.isEmpty() || iend_type < 4 || target_size < bytes.size() + 20) {
    *ok = false;
    return {};
  }
  auto payload = QByteArrayLiteral("Padding");
  payload.append('\0');
  const auto padding_length = target_size - bytes.size() - 12 - payload.size();
  if (padding_length < 0 || padding_length > std::numeric_limits<int>::max()) {
    *ok = false;
    return {};
  }
  payload.append(static_cast<int>(padding_length), 'x');
  const auto chunk_type = QByteArrayLiteral("tEXt");
  QByteArray crc_bytes = chunk_type;
  crc_bytes += payload;
  QByteArray chunk;
  append_uint32_be(chunk, static_cast<quint32>(payload.size()));
  chunk += crc_bytes;
  append_uint32_be(chunk, png_crc32(crc_bytes));
  bytes.insert(iend_type - 4, chunk);
  *ok = bytes.size() == target_size;
  return bytes;
}

bool write_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  if (file.write(bytes) != bytes.size() || !file.flush()) {
    return false;
  }
  file.close();
  return true;
}

bool replace_file_with_same_size_and_mtime(const QString& path,
                                           const QByteArray& bytes,
                                           const QDateTime& modification_time) {
  if (!write_bytes(path, bytes)) {
    return false;
  }
  QFile file(path);
  return file.open(QIODevice::ReadWrite) &&
         file.setFileTime(modification_time, QFileDevice::FileModificationTime);
}

bool test_content_identity_cache() {
  QTemporaryDir directory;
  if (!directory.isValid()) {
    std::cerr << "alpha support cache fixture could not create a temporary directory\n";
    return false;
  }
  const auto path = QDir(directory.path()).filePath(QStringLiteral("same-path.png"));
  QImage first_image(QSize(3840, 2160), QImage::Format_ARGB32);
  first_image.fill(Qt::transparent);
  first_image.setPixel(220, 318, qRgba(255, 30, 20, 120));
  QImage second_image(QSize(3840, 2160), QImage::Format_ARGB32);
  second_image.fill(Qt::transparent);
  second_image.setPixel(3410, 1831, qRgba(20, 80, 255, 240));

  const auto first_base = encode_png(first_image);
  const auto second_base = encode_png(second_image);
  const auto exact_size = qMax(first_base.size(), second_base.size()) + 24;
  bool first_encoded = false;
  bool second_encoded = false;
  const auto first_bytes = png_with_exact_size(
      first_base, exact_size, &first_encoded);
  const auto second_bytes = png_with_exact_size(
      second_base, exact_size, &second_encoded);
  const auto fixed_time =
      QDateTime(QDate(2024, 3, 4), QTime(5, 6, 7), QTimeZone::utc());
  if (!first_encoded || !second_encoded || first_bytes == second_bytes ||
      !replace_file_with_same_size_and_mtime(path, first_bytes, fixed_time)) {
    std::cerr << "PNG fixture could not create distinct equal-size content"
              << " (base=" << first_base.size() << '/' << second_base.size()
              << ", target=" << exact_size
              << ", encoded=" << first_bytes.size() << '/' << second_bytes.size()
              << ", valid=" << first_encoded << '/' << second_encoded << ")\n";
    return false;
  }
  const QFileInfo first_info(path);
  const auto first_support = png_alpha_support_for_path(path);
  const auto repeated_support = png_alpha_support_for_path(path);
  if (!first_support.valid || !first_support.decoded || first_support.cache_hit ||
      first_support.image_size != first_image.size() ||
      first_support.bounds != QRect(220, 318, 1, 1) ||
      !repeated_support.valid || !repeated_support.cache_hit ||
      repeated_support.decoded || repeated_support.bounds != first_support.bounds) {
    std::cerr << "first PNG lookup should decode and the repeated lookup should hit cached bounds\n";
    return false;
  }

  if (!replace_file_with_same_size_and_mtime(path, second_bytes, fixed_time)) {
    std::cerr << "PNG fixture could not replace its same-path content\n";
    return false;
  }
  const QFileInfo second_info(path);
  if (first_info.size() != second_info.size() ||
      first_info.lastModified() != second_info.lastModified()) {
    std::cerr << "PNG replacement must preserve both file size and modification time\n";
    return false;
  }
  const auto changed_support = png_alpha_support_for_path(path);
  if (!changed_support.valid || !changed_support.decoded || changed_support.cache_hit ||
      changed_support.bounds != QRect(3410, 1831, 1, 1)) {
    std::cerr << "same-size, same-mtime changed PNG bytes must invalidate alpha bounds\n";
    return false;
  }

  const auto missing = png_alpha_support_for_path(
      QDir(directory.path()).filePath(QStringLiteral("missing.png")));
  const auto corrupt_path = QDir(directory.path()).filePath(QStringLiteral("corrupt.png"));
  if (!write_bytes(corrupt_path, QByteArrayLiteral("not a PNG"))) {
    std::cerr << "PNG fallback fixture could not write corrupt bytes\n";
    return false;
  }
  const auto corrupt = png_alpha_support_for_path(corrupt_path);
  if (missing.valid || corrupt.valid || !missing.bounds.isEmpty() ||
      !corrupt.bounds.isEmpty()) {
    std::cerr << "missing or undecodable PNGs must request the full-canvas fallback\n";
    return false;
  }
  const auto oversized_path = QDir(directory.path()).filePath(
      QStringLiteral("oversized.png"));
  QFile oversized_file(oversized_path);
  if (!oversized_file.open(QIODevice::WriteOnly) ||
      !oversized_file.resize(64LL * 1024LL * 1024LL + 1LL)) {
    std::cerr << "oversized PNG fixture could not create a sparse file\n";
    return false;
  }
  oversized_file.close();
  const auto oversized = png_alpha_support_for_path(oversized_path);
  if (oversized.valid || !oversized.bounds.isEmpty()) {
    std::cerr << "oversized PNG input must fail open before reading its contents\n";
    return false;
  }
  return true;
}

bool test_bounded_lru() {
  QTemporaryDir directory;
  if (!directory.isValid()) {
    return false;
  }
  QImage image(QSize(2, 2), QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  const auto bytes = encode_png(image);
  const auto first_path = QDir(directory.path()).filePath(QStringLiteral("entry-0.png"));
  for (auto index = 0;
       index <= photo_editor::desktop::timeline_motion::sparse_title_roi::
                    kAlphaSupportCacheCapacity;
       ++index) {
    const auto path = QDir(directory.path()).filePath(
        QStringLiteral("entry-%1.png").arg(index));
    if (!write_bytes(path, bytes)) {
      std::cerr << "LRU fixture could not write its source PNG\n";
      return false;
    }
    const auto support = png_alpha_support_for_path(path);
    if (!support.valid) {
      std::cerr << "LRU fixture PNG did not decode\n";
      return false;
    }
  }
  const auto first_after_eviction = png_alpha_support_for_path(first_path);
  if (!first_after_eviction.valid || !first_after_eviction.decoded ||
      first_after_eviction.cache_hit) {
    std::cerr << "the alpha support cache must evict beyond its configured path capacity\n";
    return false;
  }
  return true;
}

bool test_concurrent_singleflight() {
  QTemporaryDir directory;
  if (!directory.isValid()) {
    return false;
  }
  QImage image(QSize(96, 64), QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  image.fill(QColor(40, 90, 160, 25));
  const auto path = QDir(directory.path()).filePath(QStringLiteral("concurrent.png"));
  if (!image.save(path, "PNG")) {
    return false;
  }
  constexpr int thread_count = 12;
  std::atomic<bool> start{false};
  std::vector<PngAlphaSupport> results(static_cast<std::size_t>(thread_count));
  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(thread_count));
  for (auto index = 0; index < thread_count; ++index) {
    threads.emplace_back([&, index] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      results.at(static_cast<std::size_t>(index)) = png_alpha_support_for_path(path);
    });
  }
  start.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }
  auto decoded_count = 0;
  auto cache_hit_count = 0;
  for (const auto& result : results) {
    if (!result.valid || result.bounds != image.rect()) {
      std::cerr << "concurrent alpha support lookups must return identical bounds\n";
      return false;
    }
    decoded_count += result.decoded ? 1 : 0;
    cache_hit_count += result.cache_hit ? 1 : 0;
  }
  if (decoded_count != 1 || cache_hit_count != thread_count - 1) {
    std::cerr << "concurrent requests for one content identity must share one decode\n";
    return false;
  }
  return true;
}

bool run_actual_png_benchmark(const QString& directory_path) {
  const QDir directory(directory_path);
  const auto png_paths = directory.entryList(
      {QStringLiteral("*.png")}, QDir::Files, QDir::Name);
  constexpr int png_count = 22;
  constexpr int window_count = 5;
  if (!directory.exists() || png_paths.size() < png_count) {
    std::cerr << "benchmark needs a directory with at least 22 PNG title sources\n";
    return false;
  }
  QStringList paths;
  for (auto index = 0; index < png_count; ++index) {
    paths.append(directory.filePath(png_paths.at(index)));
  }

  QElapsedTimer baseline_timer;
  baseline_timer.start();
  QVector<QPair<QSize, QRect>> reference;
  auto baseline_decode_count = 0;
  for (auto window = 0; window < window_count; ++window) {
    QHash<QString, QPair<QSize, QRect>> bounds_by_path;
    for (const auto& path : paths) {
      auto support = bounds_by_path.constFind(path);
      if (support == bounds_by_path.cend()) {
        QImageReader reader(path);
        const auto image = reader.read();
        const auto decoded = qMakePair(image.size(), nonzero_alpha_bounds(image));
        ++baseline_decode_count;
        support = bounds_by_path.insert(path, decoded);
      }
      if (window == 0) {
        reference.append(support.value());
      } else if (reference.at(paths.indexOf(path)) != support.value()) {
        std::cerr << "baseline repeated-window content changed unexpectedly\n";
        return false;
      }
    }
  }
  const auto baseline_ms = baseline_timer.elapsed();

  QElapsedTimer cache_timer;
  cache_timer.start();
  auto cached_decode_count = 0;
  auto cache_hit_count = 0;
  for (auto window = 0; window < window_count; ++window) {
    for (auto index = 0; index < paths.size(); ++index) {
      const auto support = png_alpha_support_for_path(paths.at(index));
      if (!support.valid || support.image_size != reference.at(index).first ||
          support.bounds != reference.at(index).second) {
        std::cerr << "cached title PNG alpha support differs from the uncached baseline\n";
        return false;
      }
      cached_decode_count += support.decoded ? 1 : 0;
      cache_hit_count += support.cache_hit ? 1 : 0;
    }
  }
  const auto cached_ms = cache_timer.elapsed();
  if (baseline_decode_count != png_count * window_count ||
      cached_decode_count != png_count ||
      cache_hit_count != png_count * (window_count - 1)) {
    std::cerr << "benchmark decode/cache-hit counts do not match five four-frame windows\n";
    return false;
  }
  std::cout << "actual_png_benchmark directory=" << directory_path.toStdString()
            << " pngs=" << png_count << " windows=" << window_count
            << " baseline_decodes=" << baseline_decode_count
            << " cached_decodes=" << cached_decode_count
            << " cached_hits=" << cache_hit_count
            << " baseline_ms=" << baseline_ms
            << " cached_ms=" << cached_ms << '\n';
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && QByteArray(argv[1]) == QByteArrayLiteral("--benchmark-directory")) {
    return run_actual_png_benchmark(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
  }
  if (!test_content_identity_cache() || !test_bounded_lru() ||
      !test_concurrent_singleflight()) {
    return 1;
  }

  QImage image(QSize(1920, 1080), QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  image.setPixel(0, 0, qRgba(255, 40, 20, 0));
  image.setPixel(4, 5, qRgba(255, 255, 255, 16));
  image.setPixel(8, 9, qRgba(40, 80, 120, 255));
  const auto alpha_bounds = nonzero_alpha_bounds(image);
  if (alpha_bounds != QRect(4, 5, 5, 5)) {
    std::cerr << "Alpha bounds must include partial coverage and ignore transparent RGB\n";
    return 1;
  }

  const auto scales = QVector<QSize>{QSize(46, 25), QSize(1208, 679),
                                    QSize(1920, 1080)};
  const auto roi_size = maximum_roi_size(alpha_bounds, QSize(1920, 1080), scales);
  if (roi_size != QSize(17, 18) ||
      !materially_smaller_than_canvas(roi_size, QSize(1920, 1080))) {
    std::cerr << "The fixed ROI must include the bicubic support at every sampled scale\n";
    return 1;
  }

  const auto edge_roi_size = maximum_roi_size(
      QRect(1919, 1079, 1, 1), QSize(1920, 1080),
      QVector<QSize>{QSize(46, 25), QSize(1920, 1080)});
  if (edge_roi_size != QSize(17, 17)) {
    std::cerr << "The ROI must preserve support when visible alpha touches the bottom-right canvas edge\n";
    return 1;
  }

  const auto too_large = maximum_roi_size(
      QRect(0, 0, 1920, 1080), QSize(1920, 1080),
      QVector<QSize>{QSize(1920, 1080)});
  if (materially_smaller_than_canvas(too_large, QSize(1920, 1080))) {
    std::cerr << "A nearly full-frame alpha region must stay on the full-canvas path\n";
    return 1;
  }

  const auto sparse_x = roi_origin_expression(1919, "(1920*(frameScale))", 1920);
  const auto overlay_x = overlay_origin_expression(
      1920, sparse_x, "(floor(1920*(frameScale))+3840-1920)/2-(offsetX)");
  if (sparse_x != QStringLiteral("max(0,floor((1919)*((1920*(frameScale)))/1920)-8)") ||
      overlay_x != QStringLiteral(
          "(1920+(%1)-floor(((floor(1920*(frameScale))+3840-1920)/2-(offsetX))+0.5))")
                      .arg(sparse_x)) {
    std::cerr << "The sparse overlay origin must retain the native crop rounding expression\n";
    return 1;
  }

  QImage opaque(QSize(5, 3), QImage::Format_RGB32);
  opaque.fill(Qt::red);
  if (nonzero_alpha_bounds(opaque) != opaque.rect()) {
    std::cerr << "Images without alpha must remain full-frame fallbacks\n";
    return 1;
  }
  return 0;
}

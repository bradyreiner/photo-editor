#include "timeline_mask_frame_cache.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace photo_editor::desktop::timeline_motion {
namespace {

constexpr qint64 kMaximumPixelsPerFrame = 64LL * 1024LL * 1024LL;
constexpr double kSampleTimeTolerance = 1.0e-5;

QMutex& mask_cache_mutex() {
  static QMutex mutex;
  return mutex;
}

bool cancelled(const std::atomic_bool* flag,
               const std::function<bool()>& cancellation_check) {
  return (flag != nullptr && flag->load(std::memory_order_relaxed)) ||
         (cancellation_check && cancellation_check());
}

void set_error(QString* error_message, const QString& message) {
  if (error_message != nullptr) {
    *error_message = message;
  }
}

bool sample_is_valid(const NativeMaskFrameSample& sample,
                     const QString& shape,
                     QString* error_message) {
  if (!std::isfinite(sample.sequence_seconds) ||
      !std::isfinite(sample.left) || !std::isfinite(sample.top) ||
      !std::isfinite(sample.right) || !std::isfinite(sample.bottom) ||
      !std::isfinite(sample.corner_radius) ||
      sample.left < -4.0 || sample.left > 4.0 ||
      sample.top < -4.0 || sample.top > 4.0 ||
      sample.right < -4.0 || sample.right > 4.0 ||
      sample.bottom < -4.0 || sample.bottom > 4.0 ||
      sample.left > sample.right || sample.top > sample.bottom ||
      sample.corner_radius < 0.0 || sample.corner_radius > 0.5 ||
      (shape == QStringLiteral("ellipse") &&
       sample.corner_radius > kSampleTimeTolerance)) {
    set_error(error_message, QStringLiteral("Native mask frame sample is invalid."));
    return false;
  }
  return true;
}

QString frame_name(int index) {
  return QStringLiteral("frame_%1.png").arg(index, 6, 10, QLatin1Char('0'));
}

QString frame_pattern(const QString& directory) {
  return QDir(directory).filePath(QStringLiteral("frame_%06d.png"));
}

qint64 saturating_product(qint64 left, qint64 right) {
  if (left <= 0 || right <= 0) {
    return 0;
  }
  if (left > std::numeric_limits<qint64>::max() / right) {
    return std::numeric_limits<qint64>::max();
  }
  return left * right;
}

bool request_is_valid(const NativeMaskFrameSequenceRequest& request,
                      QString* error_message) {
  const auto pixels = static_cast<qint64>(request.frame_size.width()) *
                      static_cast<qint64>(request.frame_size.height());
  const auto frame_corner = request.purpose == QStringLiteral("frame-corner");
  const auto sequence_mask = request.purpose == QStringLiteral("sequence");
  const auto supported_shape = frame_corner
                                   ? request.shape == QStringLiteral("rounded-rectangle")
                                   : request.shape == QStringLiteral("rectangle") ||
                                         request.shape == QStringLiteral("ellipse");
  if (request.cache_root.trimmed().isEmpty() || !request.frame_size.isValid() ||
      request.frame_size.isEmpty() || pixels <= 0 ||
      !std::isfinite(request.frames_per_second) ||
      request.frames_per_second <= 0.0 || request.samples.isEmpty() ||
      request.samples.size() > 1000000 || (!frame_corner && !sequence_mask) ||
      !supported_shape) {
    set_error(error_message, QStringLiteral("Native mask frame sequence settings are invalid."));
    return false;
  }
  if (pixels > kMaximumPixelsPerFrame) {
    set_error(error_message,
              QStringLiteral("The native mask raster exceeds the supported per-frame pixel limit."));
    return false;
  }
  auto previous_seconds = -std::numeric_limits<double>::infinity();
  for (const auto& sample : request.samples) {
    if (sample.sequence_seconds <= previous_seconds ||
        !sample_is_valid(sample, request.shape, error_message)) {
      set_error(error_message, QStringLiteral("Native mask frame samples are invalid."));
      return false;
    }
    if (std::isfinite(previous_seconds)) {
      const auto frame_delta =
          (sample.sequence_seconds - previous_seconds) * request.frames_per_second;
      if (std::abs(frame_delta - 1.0) > 1.0e-4) {
        set_error(error_message,
                  QStringLiteral("Native mask frame samples must be consecutive output frames."));
        return false;
      }
    }
    previous_seconds = sample.sequence_seconds;
  }
  return true;
}

QJsonObject sample_object(const NativeMaskFrameSample& sample,
                          bool include_sequence_time = true) {
  auto object = QJsonObject{
      {QStringLiteral("left"), sample.left},
      {QStringLiteral("top"), sample.top},
      {QStringLiteral("right"), sample.right},
      {QStringLiteral("bottom"), sample.bottom},
      {QStringLiteral("cornerRadius"), sample.corner_radius},
  };
  if (include_sequence_time) {
    object.insert(QStringLiteral("sequenceSeconds"), sample.sequence_seconds);
  }
  return object;
}

QString request_key(const NativeMaskFrameSequenceRequest& request) {
  QJsonArray samples;
  for (const auto& sample : request.samples) {
    samples.append(sample_object(sample, false));
  }
  const QJsonObject cache_key_object{
      {QStringLiteral("cacheSchema"), 3},
      {QStringLiteral("purpose"), request.purpose},
      {QStringLiteral("shape"), request.shape},
      {QStringLiteral("width"), request.frame_size.width()},
      {QStringLiteral("height"), request.frame_size.height()},
      {QStringLiteral("highPrecision"), request.high_precision},
      {QStringLiteral("samples"), samples},
  };
  const auto bytes = QJsonDocument(cache_key_object).toJson(QJsonDocument::Compact);
  const auto versioned = QByteArrayLiteral("timeline-native-mask-frame-sequence-v3\n") + bytes;
  return QString::fromLatin1(
      QCryptographicHash::hash(versioned, QCryptographicHash::Sha256).toHex());
}

bool manifest_matches(const NativeMaskFrameSequenceRequest& request,
                      const QString& key,
                      const QString& directory,
                      NativeMaskFrameSequenceResult* result) {
  QFile manifest_file(QDir(directory).filePath(QStringLiteral("manifest.json")));
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const auto manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
  const auto expected_depth = request.high_precision ? 16 : 8;
  const auto frame_count = manifest.value(QStringLiteral("frameCount")).toInt(-1);
  const auto encoded_bytes = manifest.value(QStringLiteral("encodedBytes")).toVariant().toLongLong();
  const auto frame_hashes = manifest.value(QStringLiteral("frameSha256")).toArray();
  if (manifest.value(QStringLiteral("cacheSchema")).toInt(-1) != 3 ||
      manifest.value(QStringLiteral("complete")).toBool() != true ||
      manifest.value(QStringLiteral("cacheKey")).toString() != key ||
      manifest.value(QStringLiteral("purpose")).toString() != request.purpose ||
      manifest.value(QStringLiteral("shape")).toString() != request.shape ||
      manifest.value(QStringLiteral("width")).toInt() != request.frame_size.width() ||
      manifest.value(QStringLiteral("height")).toInt() != request.frame_size.height() ||
      manifest.value(QStringLiteral("depth")).toInt() != expected_depth ||
      frame_count != request.samples.size() || frame_hashes.size() != frame_count ||
      manifest.value(QStringLiteral("decodedBytes")).toVariant().toLongLong() !=
          native_mask_frame_sequence_decoded_bytes(request) ||
      encoded_bytes <= 0 ||
      encoded_bytes > kNativeMaskFrameCacheBudgetBytes) {
    return false;
  }
  qint64 actual_encoded_bytes = 0;
  for (int index = 0; index < frame_count; ++index) {
    const auto frame_path = QDir(directory).filePath(frame_name(index));
    const QFileInfo frame_info(frame_path);
    if (!frame_info.isFile() || frame_info.size() <= 0 ||
        frame_info.size() > std::numeric_limits<qint64>::max() - actual_encoded_bytes) {
      return false;
    }
    QFile frame_file(frame_path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!frame_file.open(QIODevice::ReadOnly) || !hash.addData(&frame_file) ||
        hash.result().toHex() != frame_hashes.at(index).toString().toLatin1()) {
      return false;
    }
    actual_encoded_bytes += frame_info.size();
  }
  if (actual_encoded_bytes != encoded_bytes) {
    return false;
  }
  if (result != nullptr) {
    result->ok = true;
    result->cache_hit = true;
    result->cache_key = key;
    result->frame_directory = directory;
    result->frame_pattern = frame_pattern(directory);
    result->frame_count = frame_count;
    result->encoded_bytes = encoded_bytes;
    result->decoded_bytes = native_mask_frame_sequence_decoded_bytes(request);
  }
  return true;
}

QJsonObject manifest_object(const NativeMaskFrameSequenceRequest& request,
                            const QString& key,
                            qint64 encoded_bytes,
                            bool complete,
                            const QJsonArray& frame_hashes) {
  auto manifest = QJsonObject{
      {QStringLiteral("cacheSchema"), 3},
      {QStringLiteral("cacheKey"), key},
      {QStringLiteral("complete"), complete},
      {QStringLiteral("purpose"), request.purpose},
      {QStringLiteral("shape"), request.shape},
      {QStringLiteral("width"), request.frame_size.width()},
      {QStringLiteral("height"), request.frame_size.height()},
      {QStringLiteral("depth"), request.high_precision ? 16 : 8},
      {QStringLiteral("frameCount"), static_cast<int>(request.samples.size())},
      {QStringLiteral("decodedBytes"), native_mask_frame_sequence_decoded_bytes(request)},
      {QStringLiteral("encodedBytes"), encoded_bytes},
  };
  if (complete) {
    manifest.insert(QStringLiteral("frameSha256"), frame_hashes);
  }
  return manifest;
}

bool write_manifest(const QString& directory,
                    const NativeMaskFrameSequenceRequest& request,
                    const QString& key,
                    qint64 encoded_bytes,
                    bool complete,
                    const QJsonArray& frame_hashes) {
  const auto bytes = QJsonDocument(manifest_object(
      request, key, encoded_bytes, complete, frame_hashes)).toJson(QJsonDocument::Compact);
  QSaveFile file(QDir(directory).filePath(QStringLiteral("manifest.json")));
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QImage render_frame(const NativeMaskFrameSequenceRequest& request,
                    const NativeMaskFrameSample& sample,
                    QString* error_message) {
  const auto format = request.high_precision ? QImage::Format_RGBA64
                                             : QImage::Format_RGBA8888;
  QImage image(request.frame_size, format);
  if (image.isNull()) {
    set_error(error_message, QStringLiteral("Unable to allocate a native mask coverage frame."));
    return {};
  }
  const auto width = static_cast<double>(request.frame_size.width());
  const auto height = static_cast<double>(request.frame_size.height());
  const auto width_fraction = sample.right - sample.left;
  const auto height_fraction = sample.bottom - sample.top;
  const auto frame_corner = request.purpose == QStringLiteral("frame-corner");
  const auto ellipse = request.shape == QStringLiteral("ellipse");
  const auto positive_area = width_fraction > 0.0 && height_fraction > 0.0;
  const auto radius = request.purpose == QStringLiteral("frame-corner")
                          ? sample.corner_radius * std::min(width, height)
                          : sample.corner_radius *
                                std::min(width_fraction * width, height_fraction * height);
  const auto ellipse_radius_x = width_fraction * width / 2.0;
  const auto ellipse_radius_y = height_fraction * height / 2.0;
  std::vector<double> horizontal_coordinates(
      static_cast<size_t>(request.frame_size.width()));
  std::vector<double> vertical_coordinates(
      static_cast<size_t>(request.frame_size.height()));
  if (frame_corner) {
    for (int x = 0; x < request.frame_size.width(); ++x) {
      const auto pixel_x = static_cast<double>(x) + 0.5;
      horizontal_coordinates[static_cast<size_t>(x)] =
          std::abs(pixel_x - width / 2.0) - width / 2.0 + radius;
    }
    for (int y = 0; y < request.frame_size.height(); ++y) {
      const auto pixel_y = static_cast<double>(y) + 0.5;
      vertical_coordinates[static_cast<size_t>(y)] =
          std::abs(pixel_y - height / 2.0) - height / 2.0 + radius;
    }
  } else if (ellipse) {
    const auto center_x = (sample.left + sample.right) * width / 2.0;
    const auto center_y = (sample.top + sample.bottom) * height / 2.0;
    const auto safe_radius_x = std::max(ellipse_radius_x, 0.001);
    const auto safe_radius_y = std::max(ellipse_radius_y, 0.001);
    for (int x = 0; x < request.frame_size.width(); ++x) {
      const auto pixel_x = static_cast<double>(x) + 0.5;
      const auto normalized_x = std::abs(pixel_x - center_x) / safe_radius_x;
      horizontal_coordinates[static_cast<size_t>(x)] = normalized_x * normalized_x;
    }
    for (int y = 0; y < request.frame_size.height(); ++y) {
      const auto pixel_y = static_cast<double>(y) + 0.5;
      const auto normalized_y = std::abs(pixel_y - center_y) / safe_radius_y;
      vertical_coordinates[static_cast<size_t>(y)] = normalized_y * normalized_y;
    }
  } else {
    const auto center_x = (sample.left + sample.right) * width / 2.0;
    const auto center_y = (sample.top + sample.bottom) * height / 2.0;
    const auto half_width = width_fraction * width / 2.0;
    const auto half_height = height_fraction * height / 2.0;
    for (int x = 0; x < request.frame_size.width(); ++x) {
      const auto pixel_x = static_cast<double>(x) + 0.5;
      horizontal_coordinates[static_cast<size_t>(x)] =
          std::abs(pixel_x - center_x) - half_width + radius;
    }
    for (int y = 0; y < request.frame_size.height(); ++y) {
      const auto pixel_y = static_cast<double>(y) + 0.5;
      vertical_coordinates[static_cast<size_t>(y)] =
          std::abs(pixel_y - center_y) - half_height + radius;
    }
  }
  for (int y = 0; y < image.height(); ++y) {
    auto* bytes = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      double coverage = 0.0;
      if (positive_area || frame_corner) {
        double signed_distance = 0.0;
        const auto horizontal = horizontal_coordinates[static_cast<size_t>(x)];
        const auto vertical = vertical_coordinates[static_cast<size_t>(y)];
        if (frame_corner || !ellipse) {
          const auto qx = horizontal;
          const auto qy = vertical;
          if (qx <= 0.0 && qy <= 0.0) {
            signed_distance = std::max(qx, qy) - radius;
          } else {
            const auto outside_x = std::max(qx, 0.0);
            const auto outside_y = std::max(qy, 0.0);
            signed_distance =
                std::sqrt((outside_x * outside_x) + (outside_y * outside_y)) +
                std::min(std::max(qx, qy), 0.0) - radius;
          }
        } else {
          signed_distance =
              (std::sqrt(horizontal + vertical) - 1.0) *
              std::min(ellipse_radius_x, ellipse_radius_y);
        }
        coverage = std::clamp(0.5 - signed_distance, 0.0, 1.0);
      }
      if (request.high_precision) {
        const auto alpha = static_cast<quint16>(qBound<qint64>(
            qint64{0}, qRound64(coverage * 65535.0), qint64{65535}));
        auto* pixels = reinterpret_cast<QRgba64*>(bytes);
        pixels[x] = QRgba64::fromRgba64(65535, 65535, 65535, alpha);
      } else {
        const auto alpha = static_cast<uchar>(qBound(0, qRound(coverage * 255.0), 255));
        auto* pixels = bytes + static_cast<qsizetype>(x) * 4;
        pixels[0] = 255;
        pixels[1] = 255;
        pixels[2] = 255;
        pixels[3] = alpha;
      }
    }
  }
  return image;
}

NativeMaskFrameSequenceResult failure_result(const QString& key,
                                             const QString& error,
                                             qint64 elapsed_milliseconds,
                                             NativeMaskFrameSequenceFailure failure =
                                                 NativeMaskFrameSequenceFailure::Io) {
  NativeMaskFrameSequenceResult result;
  result.cache_key = key;
  result.error = error;
  result.elapsed_milliseconds = elapsed_milliseconds;
  result.failure = failure;
  return result;
}

}  // namespace

QString native_mask_frame_sequence_cache_key(
    const NativeMaskFrameSequenceRequest& request) {
  QString error;
  if (!request_is_valid(request, &error)) {
    return {};
  }
  return request_key(request);
}

qint64 native_mask_frame_sequence_decoded_bytes(
    const NativeMaskFrameSequenceRequest& request) {
  const auto pixels = saturating_product(
      request.frame_size.width(), request.frame_size.height());
  const auto bytes_per_pixel = request.high_precision ? 8 : 4;
  return saturating_product(
      saturating_product(pixels, bytes_per_pixel), request.samples.size());
}

QImage render_native_mask_frame(const NativeMaskFrameSequenceRequest& request,
                                const NativeMaskFrameSample& sample,
                                QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  QString validation_error;
  if (!request_is_valid(request, &validation_error)) {
    set_error(error_message, validation_error);
    return {};
  }
  if (!sample_is_valid(sample, request.shape, error_message)) {
    return {};
  }
  return render_frame(request, sample, error_message);
}

QString native_mask_frame_blend_filter(const QString& source_label,
                                       const QString& coverage_label,
                                       const QString& output_label,
                                       bool high_precision,
                                       QString* error_message) {
  static const QRegularExpression label_pattern(QStringLiteral("^[A-Za-z0-9_.:-]+$"));
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (!label_pattern.match(source_label).hasMatch() ||
      !label_pattern.match(coverage_label).hasMatch() ||
      !label_pattern.match(output_label).hasMatch()) {
    set_error(error_message, QStringLiteral("Native mask filter labels are invalid."));
    return {};
  }
  const auto maximum = high_precision ? 65535 : 255;
  return QStringLiteral("[%1][%2]blend=all_expr='A*B/%3':shortest=1[%4]")
      .arg(source_label, coverage_label)
      .arg(maximum)
      .arg(output_label);
}

NativeMaskFrameSequenceResult ensure_native_mask_frame_sequence(
    const NativeMaskFrameSequenceRequest& request,
    const std::atomic_bool* cancellation,
    const std::function<bool()>& cancellation_check,
    bool pin_result) {
  QElapsedTimer timer;
  timer.start();
  QString validation_error;
  if (!request_is_valid(request, &validation_error)) {
    const auto pixel_count = static_cast<qint64>(request.frame_size.width()) *
                             static_cast<qint64>(request.frame_size.height());
    const auto raster_unsupported = request.frame_size.width() > 0 &&
                                    request.frame_size.height() > 0 &&
                                    pixel_count > kMaximumPixelsPerFrame;
    return failure_result({}, validation_error, timer.elapsed(),
                          raster_unsupported
                              ? NativeMaskFrameSequenceFailure::Unsupported
                              : NativeMaskFrameSequenceFailure::InvalidRequest);
  }
  if (cancelled(cancellation, cancellation_check)) {
    return failure_result({}, QStringLiteral("Native mask coverage rendering was cancelled."),
                          timer.elapsed(), NativeMaskFrameSequenceFailure::Cancelled);
  }
  if (!QDir().mkpath(request.cache_root)) {
    return failure_result({}, QStringLiteral("Unable to create the native mask cache directory."),
                          timer.elapsed());
  }

  const auto key = request_key(request);
  const auto root = QDir(request.cache_root).absolutePath();
  const auto destination = QDir(root).filePath(QStringLiteral("mask_%1").arg(key));
  QMutexLocker locker(&mask_cache_mutex());
  NativeMaskFrameSequenceResult cached;
  pin_native_generator_frame_sequence_directory(destination);
  const auto cache_matches = manifest_matches(request, key, destination, &cached);
  if (cache_matches) {
    bool cache_capacity_failure = false;
    QString cache_error;
    if (!touch_native_generator_frame_cache_entry(
            root, destination, &cache_capacity_failure, &cache_error)) {
      unpin_native_generator_frame_sequence_directory(destination);
      return failure_result(
          key,
          cache_error,
          timer.elapsed(),
          cache_capacity_failure ? NativeMaskFrameSequenceFailure::CacheCapacity
                                 : NativeMaskFrameSequenceFailure::Io);
    }
    cached.elapsed_milliseconds = timer.elapsed();
    cached.lease_acquired = pin_result;
    if (!pin_result) {
      unpin_native_generator_frame_sequence_directory(destination);
    }
    return cached;
  }
  const auto destination_exists = QDir(destination).exists();
  unpin_native_generator_frame_sequence_directory(destination);
  if (destination_exists) {
    return failure_result(
        key,
        QStringLiteral("An incomplete native mask cache entry must be cleared before retrying."),
        timer.elapsed(), NativeMaskFrameSequenceFailure::CorruptCache);
  }

  const auto stage_name = QStringLiteral("mask_building_%1_%2")
                              .arg(key, QUuid::createUuid().toString(QUuid::WithoutBraces));
  const auto stage_directory = QDir(root).filePath(stage_name);
  if (!QDir().mkpath(stage_directory)) {
    return failure_result(key, QStringLiteral("Unable to stage native mask coverage frames."),
                          timer.elapsed());
  }
  pin_native_generator_frame_sequence_directory(stage_directory);
  const auto cleanup_stage = [&stage_directory]() {
    QDir(stage_directory).removeRecursively();
    unpin_native_generator_frame_sequence_directory(stage_directory);
  };
  QJsonArray frame_hashes;
  if (!write_manifest(stage_directory, request, key, 0, false, frame_hashes)) {
    cleanup_stage();
    return failure_result(key, QStringLiteral("Unable to reserve a native mask cache entry."),
                          timer.elapsed());
  }

  NativeMaskFrameSequenceResult result;
  result.cache_key = key;
  result.frame_count = static_cast<int>(request.samples.size());
  result.decoded_bytes = native_mask_frame_sequence_decoded_bytes(request);
  for (qsizetype index = 0; index < request.samples.size(); ++index) {
    if (cancelled(cancellation, cancellation_check)) {
      cleanup_stage();
      return failure_result(key,
                            QStringLiteral("Native mask coverage rendering was cancelled."),
                            timer.elapsed(), NativeMaskFrameSequenceFailure::Cancelled);
    }
    QString frame_error;
    const auto image = render_frame(request, request.samples.at(index), &frame_error);
    if (image.isNull()) {
      cleanup_stage();
      return failure_result(key, frame_error.isEmpty()
                                     ? QStringLiteral("The native mask renderer returned no frame.")
                                     : frame_error,
                            timer.elapsed());
    }
    if (cancelled(cancellation, cancellation_check)) {
      cleanup_stage();
      return failure_result(key, QStringLiteral("Native mask coverage rendering was cancelled."),
                            timer.elapsed(), NativeMaskFrameSequenceFailure::Cancelled);
    }
    QByteArray encoded;
    QBuffer buffer(&encoded);
    QImageWriter writer(&buffer, "PNG");
    // Qt uses a 0..100 compression scale, rather than zlib's 0..9.
    // A value of 3 writes almost-raw RGBA frames and overwhelms the cache
    // with large white/transparent masks. Lossless level 25 stays quick.
    writer.setCompression(25);
    if (!buffer.open(QIODevice::WriteOnly) || !writer.write(image) || encoded.isEmpty()) {
      cleanup_stage();
      return failure_result(key, QStringLiteral("Unable to encode a native mask coverage frame."),
                            timer.elapsed());
    }
    if (encoded.size() > std::numeric_limits<qint64>::max() - result.encoded_bytes) {
      cleanup_stage();
      return failure_result(key, QStringLiteral("Native mask cache byte count overflowed."),
                            timer.elapsed());
    }
    const auto reserved_encoded_bytes = result.encoded_bytes + encoded.size();
    QString reservation_error;
    bool cache_capacity_failure = false;
    if (!reserve_native_generator_frame_cache_bytes(
            root, stage_directory, reserved_encoded_bytes,
            &cache_capacity_failure, &reservation_error)) {
      cleanup_stage();
      return failure_result(
          key,
          reservation_error,
          timer.elapsed(),
          cache_capacity_failure ? NativeMaskFrameSequenceFailure::CacheCapacity
                                 : NativeMaskFrameSequenceFailure::Io);
    }
    if (cancelled(cancellation, cancellation_check)) {
      cleanup_stage();
      return failure_result(key, QStringLiteral("Native mask coverage rendering was cancelled."),
                            timer.elapsed(), NativeMaskFrameSequenceFailure::Cancelled);
    }
    const auto frame_path = QDir(stage_directory).filePath(
        frame_name(static_cast<int>(index)));
    QSaveFile frame_file(frame_path);
    if (!frame_file.open(QIODevice::WriteOnly) ||
        frame_file.write(encoded) != encoded.size() || !frame_file.commit()) {
      cleanup_stage();
      return failure_result(key, QStringLiteral("Unable to save a native mask coverage frame."),
                            timer.elapsed());
    }
    frame_hashes.append(QString::fromLatin1(
        QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex()));
    result.encoded_bytes = reserved_encoded_bytes;
  }
  if (cancelled(cancellation, cancellation_check)) {
    cleanup_stage();
    return failure_result(key, QStringLiteral("Native mask coverage rendering was cancelled."),
                          timer.elapsed(), NativeMaskFrameSequenceFailure::Cancelled);
  }
  if (!write_manifest(stage_directory, request, key, result.encoded_bytes, true,
                      frame_hashes)) {
    cleanup_stage();
    return failure_result(key, QStringLiteral("Unable to publish the native mask cache manifest."),
                          timer.elapsed());
  }

  // Pin the final directory before publishing it. Existing generator-cache
  // pruning scans this shared root and respects the same pin table.
  pin_native_generator_frame_sequence_directory(destination);
  QDir parent(root);
  if (!parent.rename(stage_name, QFileInfo(destination).fileName())) {
    unpin_native_generator_frame_sequence_directory(destination);
    cleanup_stage();
    return failure_result(key, QStringLiteral("Unable to publish native mask coverage frames."),
                          timer.elapsed());
  }
  unpin_native_generator_frame_sequence_directory(stage_directory);
  result.ok = true;
  result.frame_directory = destination;
  result.frame_pattern = frame_pattern(destination);
  result.elapsed_milliseconds = timer.elapsed();
  if (pin_result) {
    result.lease_acquired = true;
  } else {
    unpin_native_generator_frame_sequence_directory(destination);
  }
  return result;
}

}  // namespace photo_editor::desktop::timeline_motion

#include "timeline_native_generator_frame_cache.h"
#include "timeline_desktop_light_field.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

#include <atomic>
#include <cstring>
#include <iostream>
#include <utility>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace {

using namespace photo_editor::desktop::timeline_motion;

bool identical_rgba(const QImage& left, const QImage& right) {
  const auto expected = left.convertToFormat(QImage::Format_RGBA8888);
  const auto actual = right.convertToFormat(QImage::Format_RGBA8888);
  if (expected.isNull() || actual.isNull() || expected.size() != actual.size()) {
    return false;
  }
  const auto row_bytes = static_cast<size_t>(expected.width()) * 4U;
  for (int row = 0; row < expected.height(); ++row) {
    if (std::memcmp(expected.constScanLine(row), actual.constScanLine(row), row_bytes) != 0) {
      return false;
    }
  }
  return true;
}

struct PngMetrics final {
  qint64 encode_microseconds = 0;
  qint64 decode_microseconds = 0;
  qint64 bytes = 0;
};

bool benchmark_png_level(const QImage& frame,
                         int compression,
                         const QString& path,
                         PngMetrics* metrics) {
  if (metrics == nullptr) return false;
  QElapsedTimer timer;
  timer.start();
  QImageWriter writer(path, "PNG");
  writer.setCompression(compression);
  const auto rgba = frame.convertToFormat(QImage::Format_RGBA8888);
  if (rgba.isNull() || !writer.write(rgba)) return false;
  metrics->encode_microseconds = timer.nsecsElapsed() / 1000;
  metrics->bytes = QFileInfo(path).size();

  timer.restart();
  const auto decoded = QImageReader(path, "PNG").read();
  metrics->decode_microseconds = timer.nsecsElapsed() / 1000;
  return !decoded.isNull() && identical_rgba(frame, decoded);
}

bool write_reservation_manifest(const QString& directory, qint64 encoded_bytes) {
  if (!QDir().mkpath(directory)) return false;
  const auto json = QJsonDocument(QJsonObject{
      {QStringLiteral("encodedBytes"), static_cast<double>(encoded_bytes)},
  }).toJson(QJsonDocument::Compact);
  QSaveFile manifest(QDir(directory).filePath(QStringLiteral("manifest.json")));
  return manifest.open(QIODevice::WriteOnly) && manifest.write(json) == json.size() &&
         manifest.commit();
}

#ifdef Q_OS_UNIX
bool mutate_manifest_backend_without_changing_size_or_mtime(const QString& path) {
  const auto encoded_path = QFile::encodeName(path);
  struct stat before {};
  if (::stat(encoded_path.constData(), &before) != 0) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  auto bytes = file.readAll();
  const auto original = QByteArrayLiteral("\"backend\":\"cpu\"");
  const auto replacement = QByteArrayLiteral("\"backend\":\"gpu\"");
  if (bytes.size() != before.st_size || bytes.indexOf(original) < 0) {
    return false;
  }
  bytes.replace(original, replacement);
  if (!file.seek(0) || file.write(bytes) != bytes.size() || !file.flush()) {
    return false;
  }
  file.close();

#if defined(__APPLE__)
  const struct timespec original_times[2] = {before.st_atimespec, before.st_mtimespec};
#else
  const struct timespec original_times[2] = {before.st_atim, before.st_mtim};
#endif
  if (::utimensat(AT_FDCWD, encoded_path.constData(), original_times, 0) != 0) {
    return false;
  }
  struct stat after {};
  if (::stat(encoded_path.constData(), &after) != 0 || after.st_size != before.st_size ||
      after.st_dev != before.st_dev || after.st_ino != before.st_ino) {
    return false;
  }
#if defined(__APPLE__)
  const auto modification_time_unchanged =
      after.st_mtimespec.tv_sec == before.st_mtimespec.tv_sec &&
      after.st_mtimespec.tv_nsec == before.st_mtimespec.tv_nsec;
  const auto metadata_time_changed =
      after.st_ctimespec.tv_sec != before.st_ctimespec.tv_sec ||
      after.st_ctimespec.tv_nsec != before.st_ctimespec.tv_nsec;
#else
  const auto modification_time_unchanged =
      after.st_mtim.tv_sec == before.st_mtim.tv_sec &&
      after.st_mtim.tv_nsec == before.st_mtim.tv_nsec;
  const auto metadata_time_changed =
      after.st_ctim.tv_sec != before.st_ctim.tv_sec ||
      after.st_ctim.tv_nsec != before.st_ctim.tv_nsec;
#endif
  return modification_time_unchanged && metadata_time_changed;
}
#endif

bool run_generator_cache_fixture(const QString& temporary_path) {
  const auto frame_size = QSize(1920, 1080);
  constexpr double frame_rate = 30.0;
  constexpr double start_seconds = 8.25;
  constexpr double gather = 0.63;
  const auto image = render_desktop_light_field(
      frame_size, {.time_seconds = start_seconds, .gather = gather, .opacity = 1.0});
  if (image.isNull()) {
    std::cerr << "The native desktop-light fixture did not render.\n";
    return false;
  }

  PngMetrics fast_metrics;
  PngMetrics baseline_metrics;
  if (!benchmark_png_level(image, 11,
          QDir(temporary_path).filePath(QStringLiteral("desktop-light-level-11.png")),
          &fast_metrics) ||
      !benchmark_png_level(image, 50,
          QDir(temporary_path).filePath(QStringLiteral("desktop-light-level-50.png")),
          &baseline_metrics)) {
    std::cerr << "Native generator PNG levels must round-trip exact RGBA bytes.\n";
    return false;
  }
  std::cout << "native_frame_png fixture=desktop-light size=1920x1080"
            << " level11_encode_us=" << fast_metrics.encode_microseconds
            << " level11_decode_us=" << fast_metrics.decode_microseconds
            << " level11_bytes=" << fast_metrics.bytes
            << " level50_encode_us=" << baseline_metrics.encode_microseconds
            << " level50_decode_us=" << baseline_metrics.decode_microseconds
            << " level50_bytes=" << baseline_metrics.bytes
            << " exact_rgba=true\n";

  NativeGeneratorFrameSequenceRequest request;
  request.generator_kind = QStringLiteral("desktop-light");
  request.cache_root = QDir(temporary_path).filePath(QStringLiteral("generator-cache"));
  request.frame_size = frame_size;
  request.frames_per_second = frame_rate;
  request.clip_start_seconds = start_seconds;
  request.clip_duration_seconds = 1.0;
  request.samples = {
      {start_seconds, 0.0, gather, 1.0},
      {start_seconds + 1.0 / frame_rate, 1.0 / frame_rate, gather, 1.0},
      {start_seconds + 2.0 / frame_rate, 2.0 / frame_rate, gather, 1.0},
  };

  const auto scans_before = native_generator_frame_cache_inventory_full_scans_for_testing();
  const auto refreshes_before =
      native_generator_frame_cache_inventory_manifest_refreshes_for_testing();
  std::atomic_bool cancelled{false};
  const auto generated = ensure_native_generator_frame_sequence(request, &cancelled, {}, true);
  if (!generated.ok || generated.cache_hit || !generated.lease_acquired ||
      generated.frame_count != 3 || generated.cpu_frame_count != 3 ||
      generated.metal_frame_count != 0 || generated.backend != QStringLiteral("cpu")) {
    std::cerr << "The 3-frame native generator cache fixture failed: "
              << generated.error.toStdString() << '\n';
    return false;
  }

  NativeGeneratorFrameSequenceLease lease;
  if (!lease.adoptPinnedResult(generated)) {
    std::cerr << "A pinned native sequence should transfer its typed result into the lease.\n";
    return false;
  }
  NativeGeneratorFrameSequenceResult leased_sequence;
  if (!lease.lookup(generated.cache_key, &leased_sequence) ||
      leased_sequence.frame_pattern != generated.frame_pattern ||
      leased_sequence.frame_count != generated.frame_count ||
      lease.lookup(generated.cache_key + QStringLiteral("-stale"), &leased_sequence)) {
    std::cerr << "A generator lease should resolve only its exact typed request key.\n";
    return false;
  }
  const auto duplicate_pin = ensure_native_generator_frame_sequence(
      request, &cancelled, {}, true);
  NativeGeneratorFrameSequenceLease appended_lease;
  if (!duplicate_pin.ok || !duplicate_pin.cache_hit || !duplicate_pin.lease_acquired ||
      !appended_lease.adoptPinnedResult(duplicate_pin)) {
    std::cerr << "A cache hit should yield a second independently owned lease descriptor.\n";
    return false;
  }
  lease.append(std::move(appended_lease));
  NativeGeneratorFrameSequenceLease moved_lease(std::move(lease));
  pin_native_generator_frame_sequence_directory(generated.frame_directory);
  NativeGeneratorFrameSequenceLease assignment_target;
  assignment_target.adoptPinnedDirectory(generated.frame_directory);
  assignment_target = std::move(moved_lease);
  if (!assignment_target.lookup(generated.cache_key, &leased_sequence) ||
      moved_lease.lookup(generated.cache_key, &leased_sequence) ||
      lease.lookup(generated.cache_key, &leased_sequence)) {
    std::cerr << "Moving and appending a lease should preserve only its live descriptors.\n";
    return false;
  }
  lease = std::move(assignment_target);
  if (!lease.lookup(generated.cache_key, &leased_sequence) ||
      assignment_target.lookup(generated.cache_key, &leased_sequence)) {
    std::cerr << "Move assignment should transfer the pinned descriptor and clear its source.\n";
    return false;
  }
  qint64 observed_bytes = 0;
  for (qsizetype index = 0; index < request.samples.size(); ++index) {
    const auto& sample = request.samples.at(index);
    const auto expected = render_desktop_light_field(
        frame_size,
        {.time_seconds = sample.sequence_seconds * sample.time_scale,
         .gather = sample.gather,
         .opacity = 1.0});
    const auto path = QDir(generated.frame_directory)
                          .filePath(QStringLiteral("frame_%1.png")
                                        .arg(index, 6, 10, QLatin1Char('0')));
    QImageReader reader(path, "PNG");
    const auto decoded = reader.read();
    if (expected.isNull() || decoded.isNull() || !identical_rgba(expected, decoded)) {
      std::cerr << "A cached native generator frame differs from its rendered RGBA pixels.\n";
      return false;
    }
    const auto frame_bytes = QFileInfo(path).size();
    const auto comparison_path = QDir(temporary_path).filePath(
        QStringLiteral("cache-comparison-%1.png").arg(index));
    PngMetrics comparison_metrics;
    if (!benchmark_png_level(expected, 11, comparison_path, &comparison_metrics) ||
        frame_bytes != comparison_metrics.bytes) {
      std::cerr << "The native generator cache must use the selected lossless PNG level.\n";
      return false;
    }
    observed_bytes += frame_bytes;
  }
  if (observed_bytes != generated.encoded_bytes ||
      native_generator_frame_cache_inventory_full_scans_for_testing() != scans_before + 1) {
    std::cerr << "The initial cache publication must account exact bytes after one inventory scan.\n";
    return false;
  }

  const auto unchanged_manifest_skips_before =
      native_generator_frame_cache_inventory_manifest_unchanged_skips_for_testing();
  for (int hit = 0; hit < 5; ++hit) {
    const auto cached = ensure_native_generator_frame_sequence(request);
    if (!cached.ok || !cached.cache_hit || cached.cache_key != generated.cache_key) {
      std::cerr << "A repeated native generator cache lookup should remain a verified hit.\n";
      return false;
    }
  }
  if (native_generator_frame_cache_inventory_full_scans_for_testing() != scans_before + 1) {
    std::cerr << "Repeated verified cache hits must reuse the current budget inventory.\n";
    return false;
  }
  if (native_generator_frame_cache_inventory_manifest_unchanged_skips_for_testing() !=
      unchanged_manifest_skips_before + 5) {
    std::cerr << "Unchanged manifest stamps should reuse their verified digests.\n";
    return false;
  }

#ifdef Q_OS_UNIX
  const auto manifest_path = QDir(generated.frame_directory)
                                 .filePath(QStringLiteral("manifest.json"));
  const auto refreshes_before_same_size_change =
      native_generator_frame_cache_inventory_manifest_refreshes_for_testing();
  if (!mutate_manifest_backend_without_changing_size_or_mtime(manifest_path)) {
    std::cerr << "Unable to build the same-size, mtime-restored manifest regression.\n";
    return false;
  }
  const auto after_same_size_change = ensure_native_generator_frame_sequence(request);
  if (!after_same_size_change.ok || !after_same_size_change.cache_hit ||
      after_same_size_change.backend != QStringLiteral("gpu") ||
      native_generator_frame_cache_inventory_manifest_refreshes_for_testing() !=
          refreshes_before_same_size_change + 1) {
    std::cerr << "A same-size manifest edit with restored mtime must be detected via stat identity.\n";
    return false;
  }
#endif

  const auto external_entry = QDir(request.cache_root).filePath(QStringLiteral("external-cache"));
  if (!write_reservation_manifest(external_entry, 1)) {
    std::cerr << "Unable to create an external cache inventory invalidation fixture.\n";
    return false;
  }
  const auto after_external_create = ensure_native_generator_frame_sequence(request);
  if (!after_external_create.ok || !after_external_create.cache_hit ||
      native_generator_frame_cache_inventory_full_scans_for_testing() != scans_before + 2) {
    std::cerr << "A new on-disk cache directory must invalidate the inventory.\n";
    return false;
  }

  pin_native_generator_frame_sequence_directory(external_entry);
  if (!write_reservation_manifest(external_entry,
                                  kNativeGeneratorFrameCacheBudgetBytes)) {
    std::cerr << "Unable to update the external cache manifest fixture.\n";
    return false;
  }
  const auto refreshes_before_external_change =
      native_generator_frame_cache_inventory_manifest_refreshes_for_testing();
  const auto skips_before_external_change =
      native_generator_frame_cache_inventory_manifest_unchanged_skips_for_testing();
  const auto over_budget = ensure_native_generator_frame_sequence(request);
  if (over_budget.ok ||
      !over_budget.error.contains(QStringLiteral("2 GiB"), Qt::CaseInsensitive) ||
      !QDir(generated.frame_directory).exists() || !QDir(external_entry).exists() ||
      native_generator_frame_cache_inventory_manifest_refreshes_for_testing() !=
          refreshes_before_external_change + 1 ||
      native_generator_frame_cache_inventory_manifest_unchanged_skips_for_testing() !=
          skips_before_external_change + 1) {
    unpin_native_generator_frame_sequence_directory(external_entry);
    std::cerr << "A changed pinned manifest must refresh byte accounting and fail closed at budget.\n";
    return false;
  }
  unpin_native_generator_frame_sequence_directory(external_entry);
  const auto after_unpin = ensure_native_generator_frame_sequence(request);
  if (!after_unpin.ok || !after_unpin.cache_hit || QDir(external_entry).exists()) {
    std::cerr << "The LRU should evict an unpinned cache entry after the blocker is released.\n";
    return false;
  }
  lease.clear();
  if (lease.lookup(generated.cache_key, &leased_sequence) ||
      native_generator_frame_sequence_cache_busy()) {
    std::cerr << "Clearing a native generator lease must release pins and discard descriptors.\n";
    return false;
  }
  std::cout << "native_frame_cache frames=" << generated.frame_count
            << " bytes=" << generated.encoded_bytes
            << " first_elapsed_ms=" << generated.elapsed_milliseconds
            << " inventory_scans="
            << (native_generator_frame_cache_inventory_full_scans_for_testing() - scans_before)
            << " manifest_refreshes="
            << (native_generator_frame_cache_inventory_manifest_refreshes_for_testing() -
                refreshes_before)
            << " exact_rgba=true pinned_budget=true\n";
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    std::cerr << "Unable to create a temporary native generator cache fixture.\n";
    return 1;
  }
  return run_generator_cache_fixture(temporary.path()) ? 0 : 1;
}

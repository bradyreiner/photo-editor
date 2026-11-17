#include "timeline_mask_frame_cache.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>

using namespace photo_editor::desktop::timeline_motion;

namespace {

bool run_rgba64_blend_pixel_check(const QString& ffmpeg,
                                 const QString& temporary_path) {
  NativeMaskFrameSequenceRequest request;
  request.cache_root = QDir(temporary_path).filePath(QStringLiteral("cache"));
  request.purpose = QStringLiteral("sequence");
  request.shape = QStringLiteral("rectangle");
  request.frame_size = QSize(4, 1);
  request.frames_per_second = 1.0;
  request.high_precision = true;
  request.samples = {
      {0.0, 0.375, 0.0, 1.0, 1.0, 0.0},
      {1.0, 0.625, 0.0, 1.0, 1.0, 0.0},
      {2.0, -4.0, -4.0, 4.0, 4.0, 0.0},
  };
  NativeGeneratorFrameSequenceLease lease;
  auto sequence = ensure_native_mask_frame_sequence(request, nullptr, {}, true);
  if (!sequence.ok || !sequence.lease_acquired) {
    std::cerr << "RGBA64 mask sequence preparation failed: "
              << sequence.error.toStdString() << '\n';
    return false;
  }
  lease.adoptPinnedDirectory(sequence.frame_directory);
  if (!native_generator_frame_sequence_cache_busy() || sequence.frame_count != 3 ||
      sequence.decoded_bytes != 3 * 4 * 1 * 8) {
    std::cerr << "The mask cache should report pinned RGBA64 source bytes and active ownership\n";
    return false;
  }
  const auto cached = ensure_native_mask_frame_sequence(request, nullptr, {}, true);
  if (cached.lease_acquired) {
    lease.adoptPinnedDirectory(cached.frame_directory);
  }
  if (!cached.ok || !cached.cache_hit || !cached.lease_acquired ||
      cached.cache_key != sequence.cache_key) {
    std::cerr << "Native mask cache should return and pin a verified cache hit\n";
    return false;
  }

  QImage source(QSize(4, 1), QImage::Format_RGBA64);
  for (auto x = 0; x < source.width(); ++x) {
    auto* pixel = reinterpret_cast<QRgba64*>(source.scanLine(0)) + x;
    *pixel = QRgba64::fromRgba64(10000, 20000, 30000, 50000);
  }
  const auto source_path = QDir(temporary_path).filePath(QStringLiteral("source.png"));
  const auto raw_path = QDir(temporary_path).filePath(QStringLiteral("blended.rgba64"));
  if (!source.save(source_path)) {
    std::cerr << "Unable to encode the RGBA64 test source\n";
    return false;
  }
  QString error;
  const auto filter = native_mask_frame_blend_filter(
      QStringLiteral("src"), QStringLiteral("coverage"), QStringLiteral("out"),
      true, &error);
  if (filter.isEmpty() || !error.isEmpty()) {
    std::cerr << "RGBA64 mask blend filter was rejected: " << error.toStdString() << '\n';
    return false;
  }

  QProcess render;
  render.start(ffmpeg, {
      QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-loop"), QStringLiteral("1"),
      QStringLiteral("-framerate"), QStringLiteral("1"),
      QStringLiteral("-i"), source_path,
      QStringLiteral("-framerate"), QStringLiteral("1"),
      QStringLiteral("-start_number"), QStringLiteral("0"),
      QStringLiteral("-i"), sequence.frame_pattern,
      QStringLiteral("-filter_complex"),
      QStringLiteral("[0:v]format=rgba64le[src];[1:v]format=rgba64le[coverage];") +
          filter,
      QStringLiteral("-map"), QStringLiteral("[out]"),
      QStringLiteral("-frames:v"), QStringLiteral("3"),
      QStringLiteral("-pix_fmt"), QStringLiteral("rgba64le"),
      QStringLiteral("-f"), QStringLiteral("rawvideo"),
      QStringLiteral("-y"), raw_path,
  });
  if (!render.waitForFinished(30000) ||
      render.exitStatus() != QProcess::NormalExit || render.exitCode() != 0) {
    std::cerr << "RGBA64 cached-mask composition failed: "
              << render.readAllStandardError().toStdString() << '\n';
    return false;
  }
  QFile raw_file(raw_path);
  if (!raw_file.open(QIODevice::ReadOnly)) {
    std::cerr << "RGBA64 composition output could not be read\n";
    return false;
  }
  const auto raw = raw_file.readAll();
  constexpr qsizetype kBytesPerPixel = 8;
  constexpr qsizetype kPixelsPerFrame = 4;
  constexpr qsizetype kFrameBytes = kBytesPerPixel * kPixelsPerFrame;
  if (raw.size() != 3 * kFrameBytes) {
    std::cerr << "RGBA64 composition returned the wrong frame size\n";
    return false;
  }
  const auto sample_channel = [&raw](qsizetype frame, qsizetype x, qsizetype channel) {
    const auto offset = frame * kFrameBytes + x * kBytesPerPixel + channel * 2;
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(raw.constData() + offset));
  };
  for (qsizetype frame = 0; frame < 3; ++frame) {
    for (qsizetype x = 0; x < 4; ++x) {
      if (sample_channel(frame, x, 0) != 10000 ||
          sample_channel(frame, x, 1) != 20000 ||
          sample_channel(frame, x, 2) != 30000) {
        std::cerr << "The cached white mask changed source RGB at frame " << frame
                  << ", pixel " << x << '\n';
        return false;
      }
      auto expected_mask = 65535;
      if (frame == 0 && x == 0) {
        expected_mask = 0;
      } else if (frame == 0 && x == 1) {
        expected_mask = 32768;
      } else if (frame == 1 && x < 2) {
        expected_mask = 0;
      } else if (frame == 1 && x == 2) {
        expected_mask = 32768;
      }
      const auto expected_alpha = static_cast<quint16>(std::floor(
          (50000.0 * static_cast<double>(expected_mask) / 65535.0) + 0.5));
      if (sample_channel(frame, x, 3) != expected_alpha) {
        std::cerr << "The RGBA64 mask blend must preserve all 16 alpha bits; frame "
                  << frame << ", pixel " << x << " expected " << expected_alpha
                  << " got " << sample_channel(frame, x, 3) << '\n';
        return false;
      }
    }
  }
  lease.clear();
  if (native_generator_frame_sequence_cache_busy()) {
    std::cerr << "The native mask source lease should release every pin\n";
    return false;
  }
  QFile corrupt(QDir(sequence.frame_directory).filePath(QStringLiteral("frame_000000.png")));
  if (!corrupt.open(QIODevice::ReadWrite) || !corrupt.seek(0) ||
      corrupt.write("X", 1) != 1 || !corrupt.flush()) {
    std::cerr << "Unable to create a damaged-cache regression fixture\n";
    return false;
  }
  corrupt.close();
  const auto damaged = ensure_native_mask_frame_sequence(request, nullptr, {}, true);
  if (damaged.ok || damaged.lease_acquired ||
      damaged.failure != NativeMaskFrameSequenceFailure::CorruptCache ||
      !damaged.error.contains(QStringLiteral("incomplete"), Qt::CaseInsensitive)) {
    std::cerr << "A cache with a changed frame hash must fail closed without a lease\n";
    return false;
  }
  return true;
}

bool run_rgba8_blend_pixel_check(const QString& ffmpeg,
                                 const QString& temporary_path) {
  NativeMaskFrameSequenceRequest request;
  request.cache_root = QDir(temporary_path).filePath(QStringLiteral("rgba8-cache"));
  request.purpose = QStringLiteral("sequence");
  request.shape = QStringLiteral("rectangle");
  request.frame_size = QSize(4, 1);
  request.frames_per_second = 30.0;
  request.samples = {{0.0, 0.375, 0.0, 1.0, 1.0, 0.0}};
  const auto sequence = ensure_native_mask_frame_sequence(request, nullptr, {}, false);
  if (!sequence.ok || sequence.lease_acquired) {
    std::cerr << "RGBA8 mask sequence preparation failed: "
              << sequence.error.toStdString() << '\n';
    return false;
  }

  QImage source(QSize(4, 1), QImage::Format_RGBA8888);
  for (auto x = 0; x < source.width(); ++x) {
    auto* pixel = source.scanLine(0) + x * 4;
    pixel[0] = 100;
    pixel[1] = 80;
    pixel[2] = 60;
    pixel[3] = 200;
  }
  const auto source_path = QDir(temporary_path).filePath(QStringLiteral("source8.png"));
  const auto raw_path = QDir(temporary_path).filePath(QStringLiteral("blended8.rgba"));
  if (!source.save(source_path)) {
    std::cerr << "Unable to encode the RGBA8 test source\n";
    return false;
  }
  QString error;
  const auto filter = native_mask_frame_blend_filter(
      QStringLiteral("src"), QStringLiteral("coverage"), QStringLiteral("out"),
      false, &error);
  if (filter.isEmpty() || !error.isEmpty()) {
    std::cerr << "RGBA8 mask blend filter was rejected: " << error.toStdString() << '\n';
    return false;
  }
  QProcess render;
  render.start(ffmpeg, {
      QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-loop"), QStringLiteral("1"),
      QStringLiteral("-framerate"), QStringLiteral("30"),
      QStringLiteral("-i"), source_path,
      QStringLiteral("-framerate"), QStringLiteral("30"),
      QStringLiteral("-start_number"), QStringLiteral("0"),
      QStringLiteral("-i"), sequence.frame_pattern,
      QStringLiteral("-filter_complex"),
      QStringLiteral("[0:v]format=rgba[src];[1:v]format=rgba[coverage];") + filter,
      QStringLiteral("-map"), QStringLiteral("[out]"),
      QStringLiteral("-frames:v"), QStringLiteral("1"),
      QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
      QStringLiteral("-f"), QStringLiteral("rawvideo"),
      QStringLiteral("-y"), raw_path,
  });
  if (!render.waitForFinished(30000) ||
      render.exitStatus() != QProcess::NormalExit || render.exitCode() != 0) {
    std::cerr << "RGBA8 cached-mask composition failed: "
              << render.readAllStandardError().toStdString() << '\n';
    return false;
  }
  QFile raw_file(raw_path);
  if (!raw_file.open(QIODevice::ReadOnly)) {
    std::cerr << "RGBA8 composition output could not be read\n";
    return false;
  }
  const auto raw = raw_file.readAll();
  if (raw.size() != 16) {
    std::cerr << "RGBA8 composition returned the wrong frame size\n";
    return false;
  }
  const auto expected_alpha = std::array<uchar, 4>{0, 100, 200, 200};
  for (qsizetype x = 0; x < 4; ++x) {
    const auto offset = x * 4;
    if (static_cast<uchar>(raw.at(offset)) != 100 ||
        static_cast<uchar>(raw.at(offset + 1)) != 80 ||
        static_cast<uchar>(raw.at(offset + 2)) != 60 ||
        static_cast<uchar>(raw.at(offset + 3)) != expected_alpha[static_cast<size_t>(x)]) {
      std::cerr << "RGBA8 mask blend changed source RGB or produced wrong alpha at x="
                << x << '\n';
      return false;
    }
  }
  return true;
}

bool check_cancelled_mask_sequence_is_not_published(const QString& temporary_path) {
  NativeMaskFrameSequenceRequest request;
  request.cache_root = QDir(temporary_path).filePath(QStringLiteral("cancelled-cache"));
  request.purpose = QStringLiteral("sequence");
  request.shape = QStringLiteral("rectangle");
  request.frame_size = QSize(4, 4);
  request.samples = {{0.0, 0.25, 0.25, 0.75, 0.75, 0.0}};
  std::atomic_bool cancelled{true};
  const auto result = ensure_native_mask_frame_sequence(request, &cancelled, {}, true);
  if (result.ok || result.lease_acquired ||
      result.failure != NativeMaskFrameSequenceFailure::Cancelled ||
      !result.error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive) ||
      QDir(request.cache_root).exists()) {
    std::cerr << "A cancelled mask request must leave no published or leased output\n";
    return false;
  }
  return true;
}

bool check_pinned_cache_capacity_is_reported(const QString& temporary_path) {
  NativeMaskFrameSequenceRequest request;
  request.cache_root = QDir(temporary_path).filePath(QStringLiteral("full-cache"));
  request.purpose = QStringLiteral("sequence");
  request.shape = QStringLiteral("rectangle");
  request.frame_size = QSize(4, 4);
  request.samples = {{0.0, 0.25, 0.25, 0.75, 0.75, 0.0}};
  const auto blocker = QDir(request.cache_root).filePath(QStringLiteral("pinned-existing"));
  if (!QDir().mkpath(blocker)) {
    std::cerr << "Unable to create an isolated full-cache fixture\n";
    return false;
  }
  const auto manifest = QJsonDocument(QJsonObject{
      {QStringLiteral("encodedBytes"),
       static_cast<double>(kNativeMaskFrameCacheBudgetBytes)},
  }).toJson(QJsonDocument::Compact);
  QSaveFile manifest_file(QDir(blocker).filePath(QStringLiteral("manifest.json")));
  if (!manifest_file.open(QIODevice::WriteOnly) ||
      manifest_file.write(manifest) != manifest.size() || !manifest_file.commit()) {
    std::cerr << "Unable to reserve the isolated full-cache fixture\n";
    return false;
  }
  pin_native_generator_frame_sequence_directory(blocker);
  const auto result = ensure_native_mask_frame_sequence(request, nullptr, {}, true);
  unpin_native_generator_frame_sequence_directory(blocker);
  if (result.ok || result.lease_acquired ||
      result.failure != NativeMaskFrameSequenceFailure::CacheCapacity) {
    std::cerr << "A pinned full cache must report capacity without publishing mask output\n";
    return false;
  }
  const auto entries = QDir(request.cache_root).entryList(
      QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  if (entries != QStringList{QStringLiteral("pinned-existing")}) {
    std::cerr << "Capacity fallback must remove its staged mask frames and preserve pinned input\n";
    return false;
  }
  return true;
}

bool check_degenerate_and_off_canvas_masks() {
  NativeMaskFrameSequenceRequest corner_request;
  corner_request.cache_root = QStringLiteral("/private/tmp/mask-corner-test-only");
  corner_request.purpose = QStringLiteral("frame-corner");
  corner_request.shape = QStringLiteral("rounded-rectangle");
  corner_request.frame_size = QSize(10, 8);
  corner_request.samples = {{0.0, 0.0, 0.0, 1.0, 1.0, 0.25}};
  const auto rounded = render_native_mask_frame(
      corner_request, corner_request.samples.first());
  if (rounded.isNull() || rounded.pixelColor(0, 0).alpha() <= 0 ||
      rounded.pixelColor(0, 0).alpha() >= 255 ||
      rounded.pixelColor(5, 4).alpha() != 255) {
    std::cerr << "The frame-corner raster should preserve antialiased rounded edges\n";
    return false;
  }

  NativeMaskFrameSequenceRequest sequence_request;
  sequence_request.cache_root = QStringLiteral("/private/tmp/mask-test-only");
  sequence_request.purpose = QStringLiteral("sequence");
  sequence_request.shape = QStringLiteral("ellipse");
  sequence_request.frame_size = QSize(4, 4);
  sequence_request.samples = {{0.0, 0.5, 0.5, 0.5, 0.5, 0.0}};
  auto empty = render_native_mask_frame(sequence_request, sequence_request.samples.first());
  if (empty.isNull()) {
    return false;
  }
  for (auto y = 0; y < empty.height(); ++y) {
    for (auto x = 0; x < empty.width(); ++x) {
      if (empty.pixelColor(x, y).alpha() != 0) {
        std::cerr << "A collapsed sequence ellipse must be fully transparent\n";
        return false;
      }
    }
  }
  sequence_request.samples = {{0.0, -4.0, -4.0, 4.0, 4.0, 0.0}};
  auto covering = render_native_mask_frame(sequence_request, sequence_request.samples.first());
  if (covering.isNull()) {
    return false;
  }
  for (auto y = 0; y < covering.height(); ++y) {
    for (auto x = 0; x < covering.width(); ++x) {
      if (covering.pixelColor(x, y).alpha() != 255) {
        std::cerr << "An off-canvas ellipse covering the output should stay opaque\n";
        return false;
      }
    }
  }
  return true;
}

}  // namespace

bool run_timeline_mask_frame_cache_pixel_test() {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  QTemporaryDir temporary;
  if (ffmpeg.isEmpty() || !temporary.isValid()) {
    std::cerr << "FFmpeg and an isolated folder are required for mask precision verification\n";
    return false;
  }
  if (!check_degenerate_and_off_canvas_masks()) {
    return false;
  }
  if (!check_cancelled_mask_sequence_is_not_published(temporary.path())) {
    return false;
  }
  if (!check_pinned_cache_capacity_is_reported(temporary.path())) {
    return false;
  }
  if (!run_rgba64_blend_pixel_check(ffmpeg, temporary.path())) {
    return false;
  }
  if (!run_rgba8_blend_pixel_check(ffmpeg, temporary.path())) {
    return false;
  }
  std::cout << "Native mask cache coverage and RGBA8/RGBA64 composition passed\n";
  return true;
}

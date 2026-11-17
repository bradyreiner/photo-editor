#include "timeline_native_generator_frame_cache.h"
#include "timeline_vector_path.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstring>
#include <iostream>

namespace {

using namespace photo_editor::desktop::timeline_motion;

constexpr auto kFixtureClipId = "09f96b88-90d6-4796-920f-cb9c969ca160";
constexpr int kOutputFrameCount = 11;
constexpr double kFrameRate = 30.0;
constexpr double kWindowStartOffsetSeconds = 3.0;

bool same_rgba(const QImage& left, const QImage& right) {
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

bool load_fixture_vector_path(const QString& project_path,
                              QVariantMap* vector_path,
                              double* clip_start_seconds,
                              double* trim_start_seconds,
                              double* clip_duration_seconds) {
  if (vector_path == nullptr || clip_start_seconds == nullptr ||
      trim_start_seconds == nullptr || clip_duration_seconds == nullptr) {
    return false;
  }
  QFile file(project_path);
  if (!file.open(QIODevice::ReadOnly)) {
    std::cerr << "Unable to open P15 generator fixture: "
              << project_path.toStdString() << '\n';
    return false;
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    std::cerr << "P15 generator fixture is not valid JSON.\n";
    return false;
  }
  const auto tracks = document.object().value(QStringLiteral("tracks")).toArray();
  for (const auto& track_value : tracks) {
    const auto clips = track_value.toObject().value(QStringLiteral("clips")).toArray();
    for (const auto& clip_value : clips) {
      const auto clip = clip_value.toObject();
      if (clip.value(QStringLiteral("clipId")).toString() !=
          QString::fromLatin1(kFixtureClipId)) {
        continue;
      }
      const auto vector = clip.value(QStringLiteral("generatorParameters"))
                              .toObject()
                              .value(QStringLiteral("vectorPath"))
                              .toObject();
      const auto start = clip.value(QStringLiteral("startSeconds")).toDouble(-1.0);
      const auto trim_start = clip.value(QStringLiteral("trimStartSeconds")).toDouble(-1.0);
      const auto duration = clip.value(QStringLiteral("trimEndSeconds")).toDouble(-1.0);
      if (vector.isEmpty() || start < 0.0 || trim_start < 0.0 || duration <= 0.0) {
        std::cerr << "P15 generator fixture fields are incomplete.\n";
        return false;
      }
      *vector_path = vector.toVariantMap();
      *clip_start_seconds = start;
      *trim_start_seconds = trim_start;
      *clip_duration_seconds = duration;
      return true;
    }
  }
  std::cerr << "The selected P15 animated vector-path clip was not found.\n";
  return false;
}

bool prepare_inventory_fixture(const QString& source_root,
                               const QString& target_root,
                               const NativeGeneratorFrameSequenceResult& generated,
                               int* copied_manifest_count,
                               qint64* copied_manifest_bytes) {
  if (copied_manifest_count == nullptr || copied_manifest_bytes == nullptr ||
      !QDir().mkpath(target_root)) {
    return false;
  }
  *copied_manifest_count = 0;
  *copied_manifest_bytes = 0;
  const QDir source_directory(source_root);
  const auto names = source_directory.entryList(
      QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const auto& name : names) {
    if (name.contains(QStringLiteral(".partial-")) || name == generated.cache_key) {
      continue;
    }
    const auto source_manifest = source_directory.filePath(
        name + QStringLiteral("/manifest.json"));
    const QFileInfo source_info(source_manifest);
    if (!source_info.isFile()) {
      continue;
    }
    const auto destination_directory = QDir(target_root).filePath(name);
    if (!QDir().mkpath(destination_directory) ||
        !QFile::copy(source_manifest,
                     QDir(destination_directory).filePath(QStringLiteral("manifest.json")))) {
      return false;
    }
    ++*copied_manifest_count;
    *copied_manifest_bytes += source_info.size();
  }

  const auto destination_directory = QDir(target_root).filePath(generated.cache_key);
  if (!QDir().mkpath(destination_directory)) {
    return false;
  }
  const QDir generated_directory(generated.frame_directory);
  for (const auto& name : generated_directory.entryList(QDir::Files, QDir::Name)) {
    if (!QFile::copy(generated_directory.filePath(name),
                     QDir(destination_directory).filePath(name))) {
      return false;
    }
  }
  const auto generated_manifest = QDir(destination_directory).filePath(
      QStringLiteral("manifest.json"));
  const QFileInfo generated_manifest_info(generated_manifest);
  if (!generated_manifest_info.isFile()) {
    return false;
  }
  ++*copied_manifest_count;
  *copied_manifest_bytes += generated_manifest_info.size();
  return true;
}

bool run_fixture(const QString& project_path, const QString& inventory_source_root) {
  QTemporaryDir temporary_directory;
  if (!temporary_directory.isValid()) {
    std::cerr << "Unable to create the generator fixture cache directory.\n";
    return false;
  }

  NativeGeneratorFrameSequenceRequest request;
  request.generator_kind = QStringLiteral("vector-path");
  request.cache_root = QDir(temporary_directory.path()).filePath(QStringLiteral("cache"));
  request.frame_size = QSize(1920, 1080);
  request.frames_per_second = kFrameRate;
  double trim_start_seconds = 0.0;
  if (!load_fixture_vector_path(project_path, &request.vector_path_parameters,
                                &request.clip_start_seconds, &trim_start_seconds,
                                &request.clip_duration_seconds)) {
    return false;
  }
  if (request.vector_path_parameters.value(QStringLiteral("commands")).toList().size() != 80 ||
      request.vector_path_parameters.value(QStringLiteral("frames")).toList().size() != 181 ||
      request.clip_start_seconds != 226.0 || trim_start_seconds != 0.0 ||
      request.clip_duration_seconds != 6.0) {
    std::cerr << "The P15 waveform fixture changed from the audited 80-command, 181-sample clip.\n";
    return false;
  }
  const auto first_sequence_seconds =
      request.clip_start_seconds + kWindowStartOffsetSeconds;
  request.samples.reserve(kOutputFrameCount);
  for (int index = 0; index < kOutputFrameCount; ++index) {
    const auto sequence_seconds = first_sequence_seconds +
        static_cast<double>(index) / kFrameRate;
    const auto local_seconds = trim_start_seconds +
        (sequence_seconds - request.clip_start_seconds);
    request.samples.push_back(NativeGeneratorFrameSample{
        .sequence_seconds = sequence_seconds,
        .local_seconds = local_seconds,
        .gather = 0.0,
        .time_scale = 1.0,
    });
  }

  QElapsedTimer reference_timer;
  reference_timer.start();
  QVector<QImage> reference_frames;
  reference_frames.reserve(request.samples.size());
  for (const auto& sample : request.samples) {
    auto image = render_vector_path_frame(
        request.frame_size, request.vector_path_parameters, sample.local_seconds);
    if (image.isNull()) {
      std::cerr << "The P15 reference vector-path renderer returned no frame.\n";
      return false;
    }
    reference_frames.push_back(std::move(image));
  }
  const auto reference_elapsed_ms = reference_timer.elapsed();

  const auto generated = ensure_native_generator_frame_sequence(request);
  if (!generated.ok || generated.cache_hit || generated.frame_count != kOutputFrameCount ||
      generated.backend != QStringLiteral("cpu")) {
    std::cerr << "The prepared native vector-path sequence failed: "
              << generated.error.toStdString() << '\n';
    return false;
  }
  for (int index = 0; index < kOutputFrameCount; ++index) {
    const auto frame_path = QDir(generated.frame_directory).filePath(
        QStringLiteral("frame_%1.png").arg(index, 6, 10, QLatin1Char('0')));
    QImageReader reader(frame_path, "PNG");
    const auto actual = reader.read();
    if (actual.isNull() || !same_rgba(reference_frames.at(index), actual)) {
      std::cerr << "A prepared P15 vector-path frame differs from the reference RGBA pixels.\n";
      return false;
    }
  }

  const auto cached = ensure_native_generator_frame_sequence(request);
  if (!cached.ok || !cached.cache_hit || cached.cache_key != generated.cache_key) {
    std::cerr << "The exact P15 vector-path sequence should be reusable after publication.\n";
    return false;
  }

  auto malformed_request = request;
  malformed_request.cache_root = QDir(temporary_directory.path())
                                     .filePath(QStringLiteral("malformed-cache"));
  auto malformed_frames = malformed_request.vector_path_parameters
                              .value(QStringLiteral("frames")).toList();
  if (malformed_frames.size() < 2) {
    std::cerr << "The P15 fixture no longer contains its animated frame samples.\n";
    return false;
  }
  // This first keyframe is outside the 3-second request window. It must still
  // be validated; optimizing away repeated validation cannot weaken admission.
  auto malformed_first = malformed_frames.first().toMap();
  malformed_first.insert(QStringLiteral("coordinates"), QVariantList{});
  malformed_frames[0] = malformed_first;
  malformed_request.vector_path_parameters.insert(QStringLiteral("frames"),
                                                   malformed_frames);
  const auto malformed = ensure_native_generator_frame_sequence(malformed_request);
  if (malformed.ok || malformed.error.isEmpty() ||
      QDir(malformed_request.cache_root).exists()) {
    std::cerr << "An unused malformed P15 keyframe must still fail before cache creation.\n";
    return false;
  }

  int inventory_manifest_count = 0;
  qint64 inventory_manifest_bytes = 0;
  if (!inventory_source_root.isEmpty()) {
    const auto inventory_root = QDir(temporary_directory.path())
                                    .filePath(QStringLiteral("inventory-cache"));
    if (!prepare_inventory_fixture(inventory_source_root, inventory_root, generated,
                                   &inventory_manifest_count,
                                   &inventory_manifest_bytes) ||
        inventory_manifest_count < 400) {
      std::cerr << "Unable to stage the measured native generator manifest inventory.\n";
      return false;
    }
    auto inventory_request = request;
    inventory_request.cache_root = inventory_root;
    for (int iteration = 0; iteration < 3; ++iteration) {
      const auto unchanged_skips_before =
          native_generator_frame_cache_inventory_manifest_unchanged_skips_for_testing();
      QElapsedTimer inventory_timer;
      inventory_timer.start();
      const auto inventory_hit = ensure_native_generator_frame_sequence(inventory_request);
      if (!inventory_hit.ok || !inventory_hit.cache_hit ||
          inventory_hit.cache_key != generated.cache_key) {
        std::cerr << "The large native generator manifest inventory should remain a hit.\n";
        return false;
      }
      const auto unchanged_skips_after =
          native_generator_frame_cache_inventory_manifest_unchanged_skips_for_testing();
      if (iteration > 0 && unchanged_skips_after <= unchanged_skips_before) {
        std::cerr << "A repeated unchanged manifest inventory should reuse verified digests.\n";
        return false;
      }
      std::cout << "inventory_refresh iteration=" << iteration + 1
                << " manifests=" << inventory_manifest_count
                << " manifest_bytes=" << inventory_manifest_bytes
                << " unchanged_skip_delta="
                << unchanged_skips_after - unchanged_skips_before
                << " total_ms=" << inventory_timer.elapsed() << '\n';
    }
  }

  std::cout << "p15_vector_path clip=" << kFixtureClipId
            << " output_frames=" << request.samples.size()
            << " commands=" << request.vector_path_parameters
                                      .value(QStringLiteral("commands")).toList().size()
            << " animation_frames=" << request.vector_path_parameters
                                            .value(QStringLiteral("frames")).toList().size()
            << " reference_render_ms=" << reference_elapsed_ms
            << " prepared_sequence_ms=" << generated.elapsed_milliseconds
            << " encoded_bytes=" << generated.encoded_bytes
            << " exact_rgba=true cache_hit=true malformed_unused_frame_rejected=true\n";
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("PHOTO_EDITOR_TIMELINE_EXPORT_TIMINGS", "1");
  QCoreApplication app(argc, argv);
  if (argc != 2 && argc != 3) {
    std::cerr << "Usage: timeline_native_generator_vector_cache_test <P15 project JSON> [manifest source root]\n";
    return 2;
  }
  const auto inventory_source = argc == 3 ? QString::fromLocal8Bit(argv[2]) : QString{};
  return run_fixture(QString::fromLocal8Bit(argv[1]), inventory_source) ? 0 : 1;
}

#include "timeline_sam2_runtime.h"

#include "timeline_motion_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {

constexpr auto kWorkerResource = ":/photo-editor/runtime/timeline-sam2-worker.py";
constexpr qsizetype kMaximumOutputBytes = 256 * 1024 * 1024;
constexpr int kMaximumCorrectionFrames = 64;
constexpr int kMaximumCorrectionPointsPerFrame = 256;

TimelineSam2Result result_with_status(TimelineSam2Status status, const QString& message) {
  TimelineSam2Result result;
  result.status = status;
  result.message = message;
  return result;
}

QString configured_value(const QString& override_value, const char* variable, const QString& fallback = {}) {
  if (!override_value.trimmed().isEmpty()) {
    return override_value.trimmed();
  }
  const auto environment_value = qEnvironmentVariable(variable).trimmed();
  return environment_value.isEmpty() ? fallback : environment_value;
}

QString find_python(const QString& configured_python) {
  const auto local = QDir(QFileInfo(QString::fromLocal8Bit(__FILE__)).absolutePath()).filePath(
      QStringLiteral("../../../.photo-editor/tracking/sam2-runtime/venv/bin/python"));
  const QString requested = configured_value(configured_python, "PHOTO_EDITOR_SAM2_PYTHON",
      QFileInfo(local).isExecutable() ? QFileInfo(local).absoluteFilePath() : QStringLiteral("python3"));
  if (QFileInfo(requested).isAbsolute() || requested.contains(QLatin1Char('/'))) {
    const QFileInfo info(requested);
    return info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString{};
  }
  return QStandardPaths::findExecutable(requested);
}

QString checkpoint_path(const QString& configured_checkpoint) {
  const auto local = QDir(QFileInfo(QString::fromLocal8Bit(__FILE__)).absolutePath()).filePath(
      QStringLiteral("../../../.photo-editor/tracking/sam2-runtime/sam2.1_hiera_tiny.pt"));
  const QString configured = configured_value(configured_checkpoint, "PHOTO_EDITOR_SAM2_CHECKPOINT",
      QFileInfo(local).isFile() ? QFileInfo(local).absoluteFilePath() : QString{});
  if (configured.isEmpty()) {
    return {};
  }
  const QString path = QDir::isAbsolutePath(configured)
                           ? configured
                           : QDir::home().filePath(configured);
  const QFileInfo info(QDir::cleanPath(path));
  return info.isFile() ? info.absoluteFilePath() : QString{};
}

QString model_config(const QString& configured_config) {
  return configured_value(
      configured_config,
      "PHOTO_EDITOR_SAM2_CONFIG",
      QStringLiteral("configs/sam2.1/sam2.1_hiera_t.yaml"));
}

QString model_device(const QString& configured_device) {
  return configured_value(configured_device, "PHOTO_EDITOR_SAM2_DEVICE", QStringLiteral("cpu"));
}

QString worker_from_resource(QString* error_message) {
  QFile resource(QString::fromLatin1(kWorkerResource));
  if (!resource.open(QIODevice::ReadOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The packaged SAM2 worker resource is missing");
    }
    return {};
  }
  const auto configured_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR").trimmed();
  const QString runtime_directory =
      (configured_root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : configured_root) +
      QStringLiteral("/runtime");
  if (!QDir().mkpath(runtime_directory)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not create the local SAM2 worker directory");
    }
    return {};
  }
  const QString destination = runtime_directory + QStringLiteral("/timeline-sam2-worker.py");
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not write the packaged SAM2 worker: %1").arg(output.errorString());
    }
    return {};
  }
  const QByteArray bytes = resource.readAll();
  if (output.write(bytes) != bytes.size() || !output.commit()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not extract the packaged SAM2 worker: %1").arg(output.errorString());
    }
    return {};
  }
  QFile::setPermissions(
      destination,
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser);
  return destination;
}

QStringList worker_arguments(const TimelineSam2Request& request,
                             const QString& config,
                             const QString& checkpoint,
                             const QString& device,
                             const QString& corrections_path) {
  const QRectF& box = request.seed_box_top_left;
  QStringList arguments{
      QStringLiteral("--frames-dir"), QDir(request.frames_directory).absolutePath(),
      QStringLiteral("--mask-dir"), QDir(request.output_directory).absolutePath(),
      QStringLiteral("--frame-count"), QString::number(request.frame_count),
      QStringLiteral("--seed-box"), QStringLiteral("%1,%2,%3,%4")
                                         .arg(box.x(), 0, 'g', 17)
                                         .arg(box.y(), 0, 'g', 17)
                                         .arg(box.width(), 0, 'g', 17)
                                         .arg(box.height(), 0, 'g', 17),
      QStringLiteral("--config"), config,
      QStringLiteral("--checkpoint"), checkpoint,
      QStringLiteral("--device"), device,
  };
  if (!corrections_path.isEmpty())
    arguments << QStringLiteral("--corrections-json") << corrections_path;
  return arguments;
}

bool validate_correction_prompts(const TimelineSam2Request& request, QString* error_message) {
  const auto reject = [error_message](const QString& message) {
    if (error_message != nullptr)
      *error_message = message;
    return false;
  };
  if (request.correction_prompts.size() > kMaximumCorrectionFrames)
    return reject(QStringLiteral("SAM2 corrections may target at most 64 source frames"));
  QSet<int> frame_indices;
  for (const auto& prompt_value : request.correction_prompts) {
    const auto prompt = prompt_value.toMap();
    bool frame_ok = false;
    const double frame_number = prompt.value(QStringLiteral("frameIndex")).toDouble(&frame_ok);
    if (!frame_ok || !std::isfinite(frame_number) || std::floor(frame_number) != frame_number ||
        frame_number < 0 || frame_number >= request.frame_count)
      return reject(QStringLiteral("SAM2 correction frame indices must be integers inside the staged source range"));
    const int frame_index = static_cast<int>(frame_number);
    if (frame_indices.contains(frame_index))
      return reject(QStringLiteral("SAM2 correction frames must not contain duplicate frame indices"));
    frame_indices.insert(frame_index);
    const auto points = prompt.value(QStringLiteral("points")).toList();
    if (points.isEmpty() || points.size() > kMaximumCorrectionPointsPerFrame)
      return reject(QStringLiteral("Each SAM2 correction frame must contain 1..256 points"));
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      bool x_ok = false;
      bool y_ok = false;
      bool label_ok = false;
      const double x = point.value(QStringLiteral("x")).toDouble(&x_ok);
      const double y = point.value(QStringLiteral("y")).toDouble(&y_ok);
      const double label = point.value(QStringLiteral("label")).toDouble(&label_ok);
      if (!x_ok || !y_ok || !label_ok || !std::isfinite(x) || !std::isfinite(y) ||
          x < 0 || x > 1 || y < 0 || y > 1 || (label != 0 && label != 1))
        return reject(QStringLiteral("SAM2 correction points need finite normalized coordinates and binary labels"));
    }
  }
  if (error_message != nullptr)
    error_message->clear();
  return true;
}

QJsonArray correction_prompts_json(const QVariantList& prompts) {
  QJsonArray serialized;
  for (const auto& prompt_value : prompts) {
    const auto prompt = prompt_value.toMap();
    QJsonArray points;
    for (const auto& point_value : prompt.value(QStringLiteral("points")).toList()) {
      const auto point = point_value.toMap();
      points.append(QJsonObject{
          {QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble()},
          {QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble()},
          {QStringLiteral("label"), point.value(QStringLiteral("label")).toInt()},
      });
    }
    serialized.append(QJsonObject{
        {QStringLiteral("frameIndex"), prompt.value(QStringLiteral("frameIndex")).toInt()},
        {QStringLiteral("points"), points},
    });
  }
  return serialized;
}

void remove_expected_outputs(const TimelineSam2Request& request) {
  QDir directory(request.output_directory);
  for (int index = 0; index < request.frame_count; ++index) {
    directory.remove(QStringLiteral("mask_%1.png").arg(index, 6, 10, QLatin1Char('0')));
  }
}

bool validate_output_masks(
    const TimelineSam2Request& request,
    qsizetype maximum_output_bytes,
    QStringList* mask_paths,
    QString* error_message) {
  qsizetype total_bytes = 0;
  QStringList paths;
  bool any_coverage_in_sequence = false;
  paths.reserve(request.frame_count);
  for (int index = 0; index < request.frame_count; ++index) {
    const QString path = QDir(request.output_directory).filePath(
        QStringLiteral("mask_%1.png").arg(index, 6, 10, QLatin1Char('0')));
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable() || info.size() <= 0 ||
        info.size() > maximum_output_bytes - total_bytes) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("SAM2 did not produce a complete mask sequence within the output limit");
      }
      return false;
    }
    total_bytes += info.size();
    QImage image(path);
    if (image.isNull() || image.width() <= 0 || image.height() <= 0 ||
        image.width() > 640 || image.height() > 640 ||
        static_cast<qint64>(image.width()) * image.height() > 640 * 640) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("SAM2 returned an unreadable or oversized mask at frame %1").arg(index);
      }
      return false;
    }
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y) {
      const auto* row = rgba.constScanLine(y);
      for (int x = 0; x < rgba.width(); ++x) {
        const qsizetype offset = static_cast<qsizetype>(x) * 4;
        if (row[offset] != 255 || row[offset + 1] != 255 || row[offset + 2] != 255) {
          if (error_message != nullptr) {
            *error_message = QStringLiteral("SAM2 masks must use white RGB with coverage in alpha");
          }
          return false;
        }
        if (row[offset + 3] != 0)
          any_coverage_in_sequence = true;
      }
    }
    paths.append(path);
  }
  if (!any_coverage_in_sequence) {
    if (error_message != nullptr)
      *error_message = QStringLiteral("SAM2 returned an empty object mask for the entire source shot");
    return false;
  }
  if (mask_paths != nullptr) {
    *mask_paths = paths;
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

}  // namespace

bool timelineSam2Available(
    QString* unavailable_reason,
    const TimelineSam2Options& options) {
  const QString override_program = options.worker_executable_override.trimmed();
  if (!override_program.isEmpty()) {
    const QFileInfo worker(override_program);
    if (!worker.isFile() || !worker.isExecutable()) {
      if (unavailable_reason != nullptr) {
        *unavailable_reason = QStringLiteral("Injected SAM2 worker executable does not exist or is not executable");
      }
      return false;
    }
    if (unavailable_reason != nullptr) {
      unavailable_reason->clear();
    }
    return true;
  }

  const QString python = find_python(options.python_executable);
  if (python.isEmpty()) {
    if (unavailable_reason != nullptr) {
      *unavailable_reason = QStringLiteral("Python for SAM2 was not found; set PHOTO_EDITOR_SAM2_PYTHON");
    }
    return false;
  }
  if (checkpoint_path(options.checkpoint_path).isEmpty()) {
    if (unavailable_reason != nullptr) {
      *unavailable_reason = QStringLiteral("A local SAM2 checkpoint is required; set PHOTO_EDITOR_SAM2_CHECKPOINT");
    }
    return false;
  }
  if (!QFile::exists(QString::fromLatin1(kWorkerResource))) {
    if (unavailable_reason != nullptr) {
      *unavailable_reason = QStringLiteral("The packaged SAM2 worker resource is missing");
    }
    return false;
  }
  const QString device = model_device(options.device);
  if (device != QStringLiteral("cpu") && device != QStringLiteral("mps")) {
    if (unavailable_reason != nullptr) {
      *unavailable_reason = QStringLiteral("SAM2 device must be cpu or mps");
    }
    return false;
  }
  const QString config = model_config(options.config_path);
  if (QFileInfo(config).isAbsolute() && !QFileInfo(config).isFile()) {
    if (unavailable_reason != nullptr) {
      *unavailable_reason = QStringLiteral("Configured SAM2 model config does not exist: %1").arg(config);
    }
    return false;
  }
  if (unavailable_reason != nullptr) {
    unavailable_reason->clear();
  }
  return true;
}

TimelineSam2Result runTimelineSam2Tracking(
    const TimelineSam2Request& request,
    const TimelineSam2Options& options) {
  if (request.frame_count < 1 || request.frame_count > 180 ||
      request.frames_directory.trimmed().isEmpty() || request.output_directory.trimmed().isEmpty() ||
      !timelineNormalizedBoxIsValid(request.seed_box_top_left)) {
    return result_with_status(
        TimelineSam2Status::InvalidInput,
        QStringLiteral("SAM2 requires 1..180 frames, staged directories, and a valid normalized seed box"));
  }
  QString correction_error;
  if (!validate_correction_prompts(request, &correction_error))
    return result_with_status(TimelineSam2Status::InvalidInput, correction_error);
  if (options.timeout_ms <= 0 || options.timeout_ms > 60 * 60 * 1000 ||
      options.maximum_output_bytes <= 0 || options.maximum_output_bytes > kMaximumOutputBytes) {
    return result_with_status(
        TimelineSam2Status::InvalidInput,
        QStringLiteral("SAM2 timeout or output limit is outside the supported range"));
  }
  const QFileInfo frames_info(request.frames_directory);
  if (!frames_info.isDir() || !frames_info.isReadable()) {
    return result_with_status(TimelineSam2Status::InvalidInput,
                              QStringLiteral("Staged SAM2 frame directory is unavailable"));
  }
  const QStringList staged_jpegs =
      QDir(request.frames_directory)
          .entryList({QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                      QStringLiteral("*.JPG"), QStringLiteral("*.JPEG")},
                     QDir::Files, QDir::Name);
  if (staged_jpegs.size() != request.frame_count) {
    return result_with_status(
        TimelineSam2Status::InvalidInput,
        QStringLiteral("SAM2 frame directory must contain exactly %1 JPEG frames")
            .arg(request.frame_count));
  }
  for (int index = 0; index < request.frame_count; ++index) {
    const QString input = QDir(request.frames_directory).filePath(
        QStringLiteral("%1.jpg").arg(index, 6, 10, QLatin1Char('0')));
    const QString expected_name = QFileInfo(input).fileName();
    if (staged_jpegs.at(index) != expected_name ||
        !QFileInfo(input).isFile() || !QFileInfo(input).isReadable()) {
      return result_with_status(
          TimelineSam2Status::InvalidInput,
          QStringLiteral("Staged JPEG sequence must use readable numeric filenames beginning at 000000.jpg: %1")
              .arg(input));
    }
  }
  QDir output_directory(request.output_directory);
  if (!output_directory.exists() && !QDir().mkpath(request.output_directory)) {
    return result_with_status(TimelineSam2Status::InvalidInput,
                              QStringLiteral("Could not create SAM2 output directory"));
  }
  if (!QDir(request.output_directory).isEmpty(QDir::NoDotAndDotDot | QDir::AllEntries)) {
    return result_with_status(TimelineSam2Status::InvalidInput,
                              QStringLiteral("SAM2 output directory must be empty"));
  }

  QString unavailable_reason;
  if (!timelineSam2Available(&unavailable_reason, options)) {
    return result_with_status(TimelineSam2Status::Unavailable, unavailable_reason);
  }
  if (options.cancelled != nullptr && options.cancelled->load(std::memory_order_relaxed)) {
    return result_with_status(TimelineSam2Status::Cancelled,
                              QStringLiteral("SAM2 timeline analysis was cancelled"));
  }

  QString worker_path;
  QString python_path;
  QStringList arguments;
  QTemporaryFile corrections_file;
  QString corrections_path;
  if (!request.correction_prompts.isEmpty()) {
    corrections_file.setFileTemplate(QDir(QDir::tempPath()).filePath(
        QStringLiteral("looksy-sam2-corrections-XXXXXX")));
    if (!corrections_file.open())
      return result_with_status(TimelineSam2Status::Failed,
                                QStringLiteral("Could not create the temporary SAM2 correction prompt file"));
    const QJsonObject corrections_object{
        {QStringLiteral("frames"), correction_prompts_json(request.correction_prompts)}};
    const QByteArray corrections_bytes = QJsonDocument(corrections_object).toJson(QJsonDocument::Compact);
    if (corrections_file.write(corrections_bytes) != corrections_bytes.size() || !corrections_file.flush())
      return result_with_status(TimelineSam2Status::Failed,
                                QStringLiteral("Could not write the temporary SAM2 correction prompt file"));
    corrections_path = corrections_file.fileName();
  }
  if (!options.worker_executable_override.trimmed().isEmpty()) {
    worker_path = QFileInfo(options.worker_executable_override).absoluteFilePath();
    arguments = worker_arguments(
        request,
        model_config(options.config_path),
        checkpoint_path(options.checkpoint_path),
        model_device(options.device),
        corrections_path);
  } else {
    QString extract_error;
    worker_path = worker_from_resource(&extract_error);
    if (worker_path.isEmpty()) {
      return result_with_status(TimelineSam2Status::Unavailable, extract_error);
    }
    python_path = find_python(options.python_executable);
    arguments = {QStringLiteral("-u"), worker_path};
    arguments.append(worker_arguments(
        request,
        model_config(options.config_path),
        checkpoint_path(options.checkpoint_path),
        model_device(options.device),
        corrections_path));
  }

  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  if (python_path.isEmpty()) {
    process.start(worker_path, arguments, QIODevice::ReadOnly);
  } else {
    process.start(python_path, arguments, QIODevice::ReadOnly);
  }
  if (!process.waitForStarted(5000)) {
    return result_with_status(TimelineSam2Status::Unavailable,
                              QStringLiteral("Could not start the optional SAM2 worker: %1").arg(process.errorString()));
  }

  QElapsedTimer timer;
  timer.start();
  QByteArray stdout_buffer;
  QByteArray stderr_buffer;
  QString worker_error;
  QString worker_status;
  bool complete_event_seen = false;
  int completed_frames = 0;
  const auto handle_line = [&](const QByteArray& raw_line) {
    const auto document = QJsonDocument::fromJson(raw_line.trimmed());
    if (!document.isObject()) {
      return;
    }
    const auto event = document.object();
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("progress")) {
      completed_frames = event.value(QStringLiteral("completed")).toInt(completed_frames);
      const int total = event.value(QStringLiteral("total")).toInt(request.frame_count);
      if (options.progress) {
        options.progress(std::clamp(completed_frames, 0, request.frame_count),
                         std::clamp(total, 0, request.frame_count));
      }
    } else if (type == QStringLiteral("complete")) {
      complete_event_seen = event.value(QStringLiteral("frame_count")).toInt(-1) == request.frame_count;
    } else if (type == QStringLiteral("error")) {
      worker_error = event.value(QStringLiteral("message")).toString();
      worker_status = event.value(QStringLiteral("status")).toString();
    }
  };

  while (process.state() != QProcess::NotRunning) {
    if (options.cancelled != nullptr && options.cancelled->load(std::memory_order_relaxed)) {
      process.terminate();
      if (!process.waitForFinished(300)) {
        process.kill();
        process.waitForFinished(3000);
      }
      remove_expected_outputs(request);
      return result_with_status(TimelineSam2Status::Cancelled,
                                QStringLiteral("SAM2 timeline analysis was cancelled"));
    }
    if (timer.elapsed() >= options.timeout_ms) {
      process.kill();
      process.waitForFinished(3000);
      remove_expected_outputs(request);
      return result_with_status(TimelineSam2Status::TimedOut,
                                QStringLiteral("SAM2 timeline analysis exceeded its time limit"));
    }
    process.waitForReadyRead(50);
    stdout_buffer += process.readAllStandardOutput();
    stderr_buffer += process.readAllStandardError();
    qsizetype newline = -1;
    while ((newline = stdout_buffer.indexOf('\n')) >= 0) {
      handle_line(stdout_buffer.left(newline));
      stdout_buffer.remove(0, newline + 1);
    }
  }
  stdout_buffer += process.readAllStandardOutput();
  stderr_buffer += process.readAllStandardError();
  if (!stdout_buffer.trimmed().isEmpty()) {
    handle_line(stdout_buffer);
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (worker_status == QStringLiteral("unavailable")) {
      return result_with_status(TimelineSam2Status::Unavailable, worker_error);
    }
    return result_with_status(
        TimelineSam2Status::Failed,
        !worker_error.isEmpty() ? worker_error
                                : QStringLiteral("SAM2 worker failed: %1")
                                      .arg(QString::fromLocal8Bit(stderr_buffer).trimmed()));
  }
  if (!complete_event_seen) {
    return result_with_status(TimelineSam2Status::Failed,
                              QStringLiteral("SAM2 worker exited without a complete result event"));
  }

  TimelineSam2Result result;
  QString output_error;
  if (!validate_output_masks(
          request, options.maximum_output_bytes, &result.mask_paths, &output_error)) {
    result.status = TimelineSam2Status::OutputInvalid;
    result.message = output_error;
    return result;
  }
  result.status = TimelineSam2Status::Success;
  result.output_directory = QDir(request.output_directory).absolutePath();
  return result;
}

}  // namespace photo_editor::desktop

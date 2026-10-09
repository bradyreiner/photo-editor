#include "timeline_surface_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <limits>

namespace photo_editor::desktop {
namespace {
constexpr int kMaxFrames = 900;
constexpr int kTimeoutMs = 300000;
constexpr qsizetype kMaxProtocolBytes = 4 * 1024 * 1024;
constexpr auto kResource = ":/photo-editor/runtime/timeline-surface-worker.py";

QString python_path() {
  QString requested = qEnvironmentVariable("PHOTO_EDITOR_SURFACE_PYTHON").trimmed();
  if (requested.isEmpty()) {
    const QString local = QDir(QFileInfo(QString::fromLocal8Bit(__FILE__)).absolutePath()).filePath(
        QStringLiteral("../../../.photo-editor/tracking/surface-runtime/venv/bin/python"));
    if (QFileInfo(local).isExecutable()) return QFileInfo(local).absoluteFilePath();
    requested = QStringLiteral("python3");
  }
  if (requested.contains(QLatin1Char('/')) || QFileInfo(requested).isAbsolute()) {
    const QFileInfo info(requested);
    return info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString{};
  }
  return QStandardPaths::findExecutable(requested);
}

QString locate_worker_path() {
  const QString requested = qEnvironmentVariable("PHOTO_EDITOR_SURFACE_WORKER").trimmed();
  if (!requested.isEmpty()) {
    const QFileInfo info(requested);
    return info.isFile() ? info.absoluteFilePath() : QString{};
  }
  const QString source_dir = QFileInfo(QString::fromLocal8Bit(__FILE__)).absolutePath();
  const QString source_worker = QDir(source_dir).filePath(QStringLiteral("../../../scripts/timeline_surface_worker.py"));
  if (QFileInfo(source_worker).isFile()) return QFileInfo(source_worker).absoluteFilePath();
  if (!QFile::exists(kResource)) return {};
  const QString destination = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                              QStringLiteral("/runtime/timeline-surface-worker.py");
  if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return {};
  QFile resource(kResource);
  if (!resource.open(QIODevice::ReadOnly)) return {};
  QSaveFile out(destination);
  if (!out.open(QIODevice::WriteOnly)) return {};
  const QByteArray bytes = resource.readAll();
  if (out.write(bytes) != bytes.size() || !out.commit()) return {};
  return destination;
}

QString worker_path() {
  const QString override_path = qEnvironmentVariable("PHOTO_EDITOR_SURFACE_WORKER").trimmed();
  if (!override_path.isEmpty()) {
    const QFileInfo override_info(override_path);
    return override_info.isFile() ? override_info.absoluteFilePath() : QString{};
  }
  static const QString cached = locate_worker_path();
  return cached;
}

bool python_has_opencv(const QString& python, QString* reason) {
  static QMutex mutex;
  static QHash<QString, QString> failures;
  static QHash<QString, bool> results;
  QMutexLocker lock(&mutex);
  if (results.contains(python)) {
    if (!results.value(python) && reason) *reason = failures.value(python);
    return results.value(python);
  }
  QProcess probe;
  probe.setProcessChannelMode(QProcess::SeparateChannels);
  probe.start(python, {QStringLiteral("-c"), QStringLiteral("import cv2, numpy")}, QIODevice::ReadOnly);
  bool available = probe.waitForStarted(3000) && probe.waitForFinished(10000) &&
                   probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
  QString detail;
  if (!available) {
    detail = QString::fromLocal8Bit(probe.readAllStandardError()).trimmed();
    if (detail.isEmpty()) detail = QStringLiteral("OpenCV and NumPy are unavailable; install them in the local surface runtime");
    results.insert(python, false);
    failures.insert(python, detail);
    if (reason) *reason = detail;
    return false;
  }
  results.insert(python, true);
  if (reason) reason->clear();
  return true;
}

bool finite_number(const QJsonValue& value) {
  return value.isDouble() && std::isfinite(value.toDouble());
}

bool validate_complete(const QJsonObject& obj, const QVariantList& requested_times, QVariantMap* result, QString* error) {
  const auto count = requested_times.size();
  if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("complete") ||
      obj.value(QStringLiteral("samples")).toArray().size() != count ||
      !finite_number(obj.value(QStringLiteral("width"))) ||
      !finite_number(obj.value(QStringLiteral("height"))) ||
      obj.value(QStringLiteral("width")).toInt() < 2 || obj.value(QStringLiteral("height")).toInt() < 2 ||
      obj.value(QStringLiteral("width")).toInt() > 960 || obj.value(QStringLiteral("height")).toInt() > 960 ||
      obj.value(QStringLiteral("width")).toDouble() != obj.value(QStringLiteral("width")).toInt() ||
      obj.value(QStringLiteral("height")).toDouble() != obj.value(QStringLiteral("height")).toInt()) {
    *error = QStringLiteral("Surface worker returned invalid dimensions or an incomplete result");
    return false;
  }
  QVariantList samples;
  const auto array = obj.value(QStringLiteral("samples")).toArray();
  double previous = -1.0;
  for (qsizetype i = 0; i < array.size(); ++i) {
    const auto item = array.at(i).toObject();
    const auto time = item.value(QStringLiteral("sourceSeconds"));
    const auto confidence = item.value(QStringLiteral("confidence"));
    const auto inliers = item.value(QStringLiteral("inliers"));
    const auto error_px = item.value(QStringLiteral("errorPx"));
    const auto quad = item.value(QStringLiteral("quad")).toArray();
    if (!finite_number(time) || time.toDouble() != requested_times.at(i).toDouble() ||
        time.toDouble() <= previous || !finite_number(confidence) ||
        confidence.toDouble() < 0.30 || confidence.toDouble() > 1 || !finite_number(inliers) ||
        inliers.toInt() < 10 || inliers.toDouble() != inliers.toInt() ||
        !finite_number(error_px) || error_px.toDouble() < 0 || error_px.toDouble() > 3 || quad.size() != 4) {
      *error = QStringLiteral("Surface worker returned invalid sample metadata");
      return false;
    }
    QVariantList points;
    for (const auto& point_value : quad) {
      const auto point = point_value.toObject();
      const double px = point.value(QStringLiteral("x")).toDouble();
      const double py = point.value(QStringLiteral("y")).toDouble();
      if (!finite_number(point.value(QStringLiteral("x"))) || !finite_number(point.value(QStringLiteral("y"))) ||
          px < -0.15 || px > 1.15 || py < -0.15 || py > 1.15) {
        *error = QStringLiteral("Surface worker returned a non-finite or implausibly distant corner");
        return false;
      }
      points.push_back(QVariantMap{{QStringLiteral("x"), px}, {QStringLiteral("y"), py}});
    }
    previous = time.toDouble();
    const double fw = obj.value(QStringLiteral("width")).toDouble();
    const double fh = obj.value(QStringLiteral("height")).toDouble();
    double xy[4][2]{};
    for (int j = 0; j < 4; ++j) {
      const auto p = points.at(j).toMap();
      xy[j][0] = p.value(QStringLiteral("x")).toDouble() * fw;
      xy[j][1] = p.value(QStringLiteral("y")).toDouble() * fh;
    }
    const auto cross = [&xy](int a, int b, int c) {
      return (xy[b][0]-xy[a][0])*(xy[c][1]-xy[a][1]) - (xy[b][1]-xy[a][1])*(xy[c][0]-xy[a][0]);
    };
    const double area2 = xy[0][0]*xy[1][1] + xy[1][0]*xy[3][1] + xy[3][0]*xy[2][1] + xy[2][0]*xy[0][1] -
                         xy[1][0]*xy[0][1] - xy[3][0]*xy[1][1] - xy[2][0]*xy[3][1] - xy[0][0]*xy[2][1];
    if (cross(0,1,3) <= 0 || cross(1,3,2) <= 0 || cross(3,2,0) <= 0 || cross(2,0,1) <= 0 ||
        area2 < fw*fh*0.0002 || area2 > fw*fh*8.0) {
      *error = QStringLiteral("Surface worker returned a degenerate, concave, or flipped quad");
      return false;
    }
    samples.push_back(QVariantMap{{QStringLiteral("sourceSeconds"), previous},
                                  {QStringLiteral("quad"), points},
                                  {QStringLiteral("confidence"), confidence.toDouble()},
                                  {QStringLiteral("inliers"), inliers.toInt()},
                                  {QStringLiteral("errorPx"), error_px.toDouble()}});
  }
  *result = {{QStringLiteral("success"), true}, {QStringLiteral("message"), QString{}},
             {QStringLiteral("samples"), samples}, {QStringLiteral("width"), obj.value(QStringLiteral("width")).toInt()},
             {QStringLiteral("height"), obj.value(QStringLiteral("height")).toInt()},
             {QStringLiteral("backend"), obj.value(QStringLiteral("backend")).toString()}};
  return true;
}

QVariantMap failure(const QString& message) {
  return {{QStringLiteral("success"), false}, {QStringLiteral("message"), message},
          {QStringLiteral("samples"), QVariantList{}}};
}
}  // namespace

bool timelineSurfaceAvailable(QString* reason) {
  const QString python = python_path();
  if (python.isEmpty()) {
    if (reason) *reason = QStringLiteral("Python was not found; set PHOTO_EDITOR_SURFACE_PYTHON");
    return false;
  }
  if (worker_path().isEmpty()) {
    if (reason) *reason = QStringLiteral("Surface tracking worker is unavailable");
    return false;
  }
  QString runtime_reason;
  if (!python_has_opencv(python, &runtime_reason)) {
    if (reason) *reason = runtime_reason;
    return false;
  }
  if (reason) reason->clear();
  return true;
}

QVariantMap trackTimelineSurface(const QString& frames_directory,
                                 const QVariantList& times,
                                 const QVariantMap& seed,
                                 std::atomic_bool* cancelled,
                                 std::function<void(int, int)> progress) {
  const qsizetype requested_count = times.size();
  if (requested_count < 2 || requested_count > kMaxFrames || frames_directory.trimmed().isEmpty())
    return failure(QStringLiteral("Surface tracking requires 2..900 staged frames"));
  const int count = static_cast<int>(requested_count);
  double last = -1;
  for (const auto& value : times) {
    bool ok = false;
    const double t = value.toDouble(&ok);
    if (!ok || !std::isfinite(t) || t < 0 || t <= last) return failure(QStringLiteral("Source times must be finite, nonnegative, and increasing"));
    last = t;
  }
  const auto seed_number = [&seed](const QString& key) {
    bool ok = false;
    const double value = seed.value(key).toDouble(&ok);
    return ok ? value : std::numeric_limits<double>::quiet_NaN();
  };
  const double x = seed_number(QStringLiteral("x"));
  const double y = seed_number(QStringLiteral("y"));
  const double w = seed_number(QStringLiteral("width"));
  const double h = seed_number(QStringLiteral("height"));
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) ||
      x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > 1 || y + h > 1)
    return failure(QStringLiteral("Seed rectangle must be normalized and inside the frame"));
  const QFileInfo dir(frames_directory);
  if (!dir.isDir() || !dir.isReadable()) return failure(QStringLiteral("Staged frame directory is unavailable"));
  const QDir frames(frames_directory);
  for (int i = 0; i < count; ++i) {
    const QString path = frames.filePath(QStringLiteral("%1.png").arg(i, 6, 10, QLatin1Char('0')));
    if (!QFileInfo(path).isFile() || !QFileInfo(path).isReadable()) return failure(QStringLiteral("Missing staged PNG frame %1").arg(i));
  }
  QString unavailable;
  if (!timelineSurfaceAvailable(&unavailable)) return failure(unavailable);
  if (cancelled && cancelled->load(std::memory_order_relaxed)) return failure(QStringLiteral("Surface tracking cancelled"));

  QTemporaryDir request_dir;
  if (!request_dir.isValid()) return failure(QStringLiteral("Could not create temporary surface request directory"));
  QJsonArray time_array;
  for (const auto& value : times) time_array.push_back(value.toDouble());
  QJsonObject request{{QStringLiteral("framesDirectory"), QFileInfo(frames_directory).absoluteFilePath()},
                      {QStringLiteral("times"), time_array}, {QStringLiteral("seed"), QJsonObject::fromVariantMap(seed)}};
  const QString request_path = request_dir.filePath(QStringLiteral("request.json"));
  QFile request_file(request_path);
  if (!request_file.open(QIODevice::WriteOnly) || request_file.write(QJsonDocument(request).toJson(QJsonDocument::Compact)) < 0)
    return failure(QStringLiteral("Could not write surface request"));
  request_file.close();

  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(python_path(), {QStringLiteral("-u"), worker_path(), QStringLiteral("--request"), request_path}, QIODevice::ReadOnly);
  if (!process.waitForStarted(5000)) return failure(QStringLiteral("Could not start surface worker: %1").arg(process.errorString()));
  QElapsedTimer timer;
  timer.start();
  QByteArray output;
  qsizetype received_bytes = 0;
  QString worker_message;
  QString protocol_error;
  QVariantMap result;
  bool complete = false;
  int last_progress = 0;
  const auto handle_line = [&](const QByteArray& raw_line) {
    const auto document = QJsonDocument::fromJson(raw_line.trimmed());
    if (!document.isObject()) {
      protocol_error = QStringLiteral("Surface worker emitted invalid JSON");
      return;
    }
    const auto obj = document.object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("progress")) {
      const int done = obj.value(QStringLiteral("completed")).toInt(-1);
      const int total = obj.value(QStringLiteral("total")).toInt(-1);
      if (complete || done < last_progress || done < 0 || done > count || total != count ||
          obj.value(QStringLiteral("completed")).toDouble() != done || obj.value(QStringLiteral("total")).toDouble() != total) {
        protocol_error = QStringLiteral("Surface worker emitted invalid progress");
        return;
      }
      last_progress = done;
      if (progress) progress(done, total);
    } else if (type == QStringLiteral("error")) {
      if (!worker_message.isEmpty() || complete) {
        protocol_error = QStringLiteral("Surface worker emitted contradictory result events");
        return;
      }
      worker_message = obj.value(QStringLiteral("message")).toString();
    } else if (type == QStringLiteral("complete")) {
      if (complete || !worker_message.isEmpty()) {
        protocol_error = QStringLiteral("Surface worker emitted duplicate or contradictory completion");
        return;
      }
      QString validation_error;
      if (!validate_complete(obj, times, &result, &validation_error)) {
        protocol_error = validation_error;
        return;
      }
      complete = true;
    } else {
      protocol_error = QStringLiteral("Surface worker emitted an unknown event");
    }
  };
  const auto consume_lines = [&]() {
    qsizetype newline = -1;
    while ((newline = output.indexOf('\n')) >= 0) {
      const QByteArray line = output.left(newline);
      output.remove(0, newline + 1);
      handle_line(line);
      if (!protocol_error.isEmpty()) return false;
    }
    return true;
  };
  while (process.state() != QProcess::NotRunning) {
    if (cancelled && cancelled->load(std::memory_order_relaxed)) {
      process.terminate();
      if (!process.waitForFinished(250)) { process.kill(); process.waitForFinished(2000); }
      return failure(QStringLiteral("Surface tracking cancelled"));
    }
    if (timer.elapsed() > kTimeoutMs) {
      process.kill(); process.waitForFinished(2000);
      return failure(QStringLiteral("Surface tracking exceeded its 300 second time limit"));
    }
    process.waitForReadyRead(50);
    const QByteArray chunk = process.readAllStandardOutput();
    received_bytes += chunk.size();
    if (received_bytes > kMaxProtocolBytes) { process.kill(); process.waitForFinished(1000); return failure(QStringLiteral("Surface worker protocol exceeded its size limit")); }
    output += chunk;
    if (output.size() > kMaxProtocolBytes || !consume_lines()) {
      process.kill(); process.waitForFinished(1000);
      return failure(protocol_error.isEmpty() ? QStringLiteral("Surface worker protocol record exceeded its size limit") : protocol_error);
    }
  }
  const QByteArray final_chunk = process.readAllStandardOutput();
  received_bytes += final_chunk.size();
  if (received_bytes > kMaxProtocolBytes) return failure(QStringLiteral("Surface worker protocol exceeded its size limit"));
  output += final_chunk;
  if (output.size() > kMaxProtocolBytes || !consume_lines()) {
    return failure(protocol_error.isEmpty() ? QStringLiteral("Surface worker protocol record exceeded its size limit") : protocol_error);
  }
  if (!output.trimmed().isEmpty()) {
    handle_line(output);
    output.clear();
  }
  if (!protocol_error.isEmpty()) return failure(protocol_error);
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !complete)
    return failure(worker_message.isEmpty() ? QStringLiteral("Surface worker failed without a complete result") : worker_message);
  return result;
}
}  // namespace photo_editor::desktop

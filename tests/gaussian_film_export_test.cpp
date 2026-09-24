#include "gaussian_film_export.h"
#include "gaussian_saved_views.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QtConcurrent>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <cmath>
#include <algorithm>

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto encoder = QStandardPaths::findExecutable("ffmpeg");
  const auto probe = QStandardPaths::findExecutable("ffprobe");
  if (encoder.isEmpty() || probe.isEmpty()) { std::cerr << "FFmpeg/ffprobe unavailable\n"; return 77; }
  if (app.arguments().size() == 3) {
    const auto project = app.arguments()[1]; const auto movie = app.arguments()[2];
    QFile receipt_file(movie + ".camera-path.json");
    if (!receipt_file.open(QIODevice::ReadOnly) || receipt_file.size() > 1024 * 1024) return 1;
    const auto receipt = QJsonDocument::fromJson(receipt_file.readAll()).toVariant().toMap();
    GaussianCameraPath path;
    path.segment_seconds = receipt.value("segmentSeconds").toDouble(); path.fps = receipt.value("fps").toInt();
    for (const auto& value : receipt.value("keyframes").toList()) {
      const auto camera = gaussian_saved_camera(value.toMap()); if (!camera) return 1; path.cameras.push_back(*camera);
    }
    const QSize size(receipt.value("width").toInt(), receipt.value("height").toInt());
    const int count = receipt.value("frameCount").toInt();
    const qint64 frame_bytes = static_cast<qint64>(size.width()) * size.height() * 3;
    if (receipt.value("sampleTime").toString() != "frameIndex / fps" ||
        path.cameras.size() < 2 || count < 2 || count > 120 || path.frameCount() != count ||
        size.width() < 2 || size.height() < 2 || frame_bytes * count > 128 * 1024 * 1024) return 1;
    const auto scene = load_gaussian_scene(QDir(project).filePath("trained/scene.ply"), QDir(project).filePath("dataset/sparse"),
        receipt.value("artifactSha256").toString());
    if (!scene.scene) return 1;
    auto renderer = make_gaussian_scene_renderer(); if (!renderer) return 1;
    QProcess decoder;
    decoder.start(encoder, {"-v", "error", "-i", movie, "-vf", "scale=in_color_matrix=bt709:in_range=tv:out_range=pc",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"});
    if (!decoder.waitForFinished(15000) || decoder.exitCode() != 0) return 1;
    const auto decoded = decoder.readAllStandardOutput();
    if (decoded.size() != frame_bytes * count) return 1;
    double minimum = 100, total = 0;
    for (int index = 0; index < count; ++index) {
      const auto frame = renderer->render(scene.scene, path.sample(static_cast<double>(index) / path.fps).resized(size), true);
      if (!frame.error.isEmpty() || frame.image.isNull()) return 1;
      const auto expected = frame.image.convertToFormat(QImage::Format_RGB888);
      double squared_error = 0;
      for (int y = 0; y < size.height(); ++y) for (int x = 0; x < size.width() * 3; ++x) {
        const auto offset = frame_bytes * index + static_cast<qint64>(y) * size.width() * 3 + x;
        const double difference = static_cast<unsigned char>(decoded[offset]) - expected.constScanLine(y)[x];
        squared_error += difference * difference;
      }
      const double psnr = squared_error == 0 ? 100 : 10 * std::log10(255.0 * 255.0 * static_cast<double>(frame_bytes) / squared_error);
      minimum = std::min(minimum, psnr); total += psnr;
    }
    std::cout << "Decoded frame parity: frames=" << count << " minimum RGB PSNR=" << minimum << " mean=" << total / count << '\n';
    return minimum >= 35 ? 0 : 1;
  }
  QTemporaryDir directory;
  auto scene = std::make_shared<GaussianSceneData>(); scene->artifact_sha256 = QString(64, 'a');
  GaussianPoint point; point.center_opacity = {0, 0, 3, 1}; point.covariance_a = {.3F, 0, 0, .3F}; point.covariance_b = {0, .3F, 0, 0};
  point.sh[0] = 1.0F; point.sh[1] = .2F; point.sh[2] = -.4F; scene->points = {point};
  GaussianCamera a; a.name = "First"; a.image_size = {65, 65}; a.fx = a.fy = 60; a.cx = a.cy = 32.5F;
  auto b = a; b.position = {.3F, 0, 0}; scene->cameras = {a};
  GaussianCameraPath path; path.cameras = {a, b}; path.view_ids = {"a", "b"}; path.segment_seconds = .5;
  const auto destination = QDir(directory.path()).filePath("film.mp4");
  const auto progress = std::make_shared<GaussianFilmProgress>();
  const auto result = export_gaussian_film(scene, path, destination, 256, encoder, progress);
  if (!result.value("ok").toBool() || progress->completed != 12) { std::cerr << QJsonDocument::fromVariant(result).toJson().constData(); return 1; }
  QProcess inspect; inspect.start(probe, {"-v", "error", "-count_frames", "-show_streams", "-of", "json", destination});
  if (!inspect.waitForFinished(10000) || inspect.exitCode() != 0) return 1;
  const auto stream = QJsonDocument::fromJson(inspect.readAllStandardOutput()).toVariant().toMap().value("streams").toList().value(0).toMap();
  std::cout << QJsonDocument::fromVariant(stream).toJson().constData() << std::flush;
  if (stream.value("nb_read_frames").toInt() != 12 || stream.value("width").toInt() != 256 || stream.value("height").toInt() != 256 ||
      stream.value("r_frame_rate").toString() != "24/1" || stream.value("pix_fmt").toString() != "yuv420p" ||
      stream.value("color_transfer").toString() != "iec61966-2-1" || stream.value("color_space").toString() != "bt709" ||
      stream.value("color_range").toString() != "tv") return 1;
  const auto bytes = QFileInfo(destination).size();
  if (export_gaussian_film(scene, path, destination, 256, encoder, progress).value("ok").toBool() || QFileInfo(destination).size() != bytes) return 1;
  const auto canceled = std::make_shared<GaussianFilmProgress>(); canceled->canceled = true;
  const auto canceled_path = QDir(directory.path()).filePath("canceled.mp4");
  if (!export_gaussian_film(scene, path, canceled_path, 256, encoder, canceled).value("canceled").toBool() || QFileInfo::exists(canceled_path)) return 1;
  path.segment_seconds = 10;
  const auto active = std::make_shared<GaussianFilmProgress>();
  auto future = QtConcurrent::run([&] { return export_gaussian_film(scene, path, canceled_path, 256, encoder, active); });
  QElapsedTimer timeout; timeout.start();
  while (!future.isFinished() && timeout.elapsed() < 10000) {
    if (active->completed.load() >= 1) active->canceled = true;
    app.processEvents(); QThread::msleep(1);
  }
  active->canceled = true; future.waitForFinished();
  if (!future.result().value("canceled").toBool() || active->completed.load() < 1 || active->completed.load() >= path.frameCount() ||
      timeout.elapsed() >= 5000 || QFileInfo::exists(canceled_path) || QFileInfo::exists(canceled_path + ".camera-path.json")) {
    std::cerr << "Active cancellation failed: " << QJsonDocument::fromVariant(future.result()).toJson().constData(); return 1;
  }
  std::cout << "Active cancellation after " << active->completed.load() << " frames in " << timeout.elapsed() << " ms\n";
  for (const bool collide_receipt : {false, true}) {
    const auto late_path = directory.filePath(collide_receipt ? "late-receipt.mp4" : "late-movie.mp4");
    const auto collision = late_path + (collide_receipt ? ".camera-path.json" : "");
    const auto late_progress = std::make_shared<GaussianFilmProgress>();
    auto late = QtConcurrent::run([&] {
      return export_gaussian_film(scene, path, late_path, 256, encoder, late_progress);
    });
    QElapsedTimer deadline; deadline.start();
    while (!late.isFinished() && late_progress->completed.load() < 1 && deadline.elapsed() < 10000)
      QThread::msleep(1);
    bool claimed = false;
    if (!late.isFinished() && late_progress->completed.load() >= 1) {
      QFile occupant(collision);
      claimed = occupant.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
          occupant.write("preserve", 8) == 8 && occupant.flush();
    }
    while (!late.isFinished() && deadline.elapsed() < 20000) QThread::msleep(1);
    if (!late.isFinished()) late_progress->canceled = true;
    late.waitForFinished();
    QFile occupant(collision);
    const auto rejected = late.result();
    if (!claimed || rejected.value("ok").toBool() || rejected.value("canceled").toBool() ||
        rejected.value("receiptRetained").toBool() || !occupant.open(QIODevice::ReadOnly) ||
        occupant.readAll() != "preserve" ||
        QFileInfo::exists(collide_receipt ? late_path : late_path + ".camera-path.json")) {
      std::cerr << "Late export collision failed: " << QJsonDocument::fromVariant(rejected).toJson().constData();
      return 1;
    }
  }
  const auto missing = export_gaussian_film(scene, path, canceled_path, 256, "/nonexistent/ffmpeg", progress);
  if (missing.value("ok").toBool() || QFileInfo::exists(canceled_path)) return 1;
  std::cout << "12 encoded frames, frame rate/dimensions/color tags, no overwrite, preflight and active cancellation passed\n";
}

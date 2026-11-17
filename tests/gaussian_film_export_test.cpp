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
#include <cstring>
#include <thread>

using namespace photo_editor::desktop;
#include "gaussian_frame_sequence_test.h"
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (app.arguments().value(1) == "--png-sequence-only") return gaussian_frame_sequence_regression() ? 0 : 1;
  const auto encoder = QStandardPaths::findExecutable("ffmpeg");
  const auto probe = QStandardPaths::findExecutable("ffprobe");
  const bool sequence_request = QFileInfo(app.arguments().value(2)).isDir() || app.arguments().value(3) == "--export-sequence";
  const bool export_request = app.arguments().value(3) == "--export-sequence" || app.arguments().value(3) == "--export-mp4";
  if (!sequence_request && (encoder.isEmpty() || probe.isEmpty())) { std::cerr << "FFmpeg/ffprobe unavailable\n"; return 77; }
  if (app.arguments().size() == 3 || (app.arguments().size() == 5 && (app.arguments()[3] == "--replay-proof-dir" || export_request)) ||
      (app.arguments().size() == 7 && export_request && app.arguments()[5] == "--edge")) {
    const auto project = app.arguments()[1]; const auto movie = app.arguments()[2];
    const bool sequence = QFileInfo(movie).isDir();
    QFile receipt_file(sequence ? QDir(movie).filePath("camera-path.json") : movie + ".camera-path.json");
    if (!receipt_file.open(QIODevice::ReadOnly) || receipt_file.size() > 32 * 1024 * 1024) return 1;
    const auto receipt = QJsonDocument::fromJson(receipt_file.readAll()).toVariant().toMap();
    const auto sequence_frames = receipt.value("frames").toList();
    if (sequence && (receipt.value("schema") != "framehouse-gaussian-frame-sequence-v1" || receipt.value("framePattern") != "frame-%06d.png")) return 1;
    GaussianCameraPath path;
    path.segment_seconds = receipt.value("segmentSeconds").toDouble(); path.fps = receipt.value("fps").toInt();
    for (const auto& value : receipt.value("keyframes").toList()) {
      const auto camera = gaussian_saved_camera(value.toMap()); if (!camera) return 1; path.cameras.push_back(*camera);
    }
    if (receipt.contains("sceneObjects")) {
      const auto composition = gaussian_scene_composition(receipt.value("sceneObjects").toMap());
      if (!composition) return 1;
      const auto resolved = resolve_gaussian_composition_resources(project,*composition);
      if (!resolved.composition) { std::cerr << "Frozen scene resources: " << resolved.error.toStdString() << '\n'; return 1; }
      for (auto& camera : path.cameras) camera.composition = resolved.composition;
    }
    if (receipt.contains("alphaModel") && (path.cameras.empty() || receipt.value("alphaModel") != gaussian_alpha_model_name(gaussian_alpha_model(path.cameras.front())))) return 1;
    const QSize size(receipt.value("width").toInt(), receipt.value("height").toInt());
    if (size.width() < 2 || size.height() < 2 || size.width() > 8192 || size.height() > 8192) return 1;
    const int count = receipt.value("frameCount").toInt();
    const qint64 frame_bytes = static_cast<qint64>(size.width()) * size.height() * 3;
    if (receipt.value("sampleTime").toString() != "frameIndex / fps" ||
        path.cameras.size() < 2 || count < 2 || count > (sequence ? 36000 : 120) || path.frameCount() != count ||
        (sequence ? sequence_frames.size() != count : frame_bytes * count > 128 * 1024 * 1024)) return 1;
    QFile project_file(QDir(project).filePath("scene.json"));
    if (!project_file.open(QIODevice::ReadOnly) || project_file.size() > 1024*1024) return 1;
    const auto imported = QJsonDocument::fromJson(project_file.readAll()).toVariant().toMap().value("importedArtifact").toBool();
    const auto scene = load_gaussian_scene(QDir(project).filePath("trained/scene.ply"), imported ? QString() : QDir(project).filePath("dataset/sparse"),
        receipt.value("artifactSha256").toString());
    if (!scene.scene) return 1;
    if (export_request) {
      bool valid_edge = true; const int edge = app.arguments().size() == 7 ? app.arguments()[6].toInt(&valid_edge) : std::max(size.width(),size.height());
      if (!valid_edge) return 1;
      const auto progress = std::make_shared<GaussianFilmProgress>();
      std::jthread reporter([progress,count](std::stop_token stop) {
        int previous = -1;
        while (!stop.stop_requested()) {
          const int completed = progress->completed.load();
          if (completed != previous) { std::cerr << "Rendered " << completed << '/' << count << " frames\n"; previous = completed; }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      });
      const bool png = app.arguments()[3] == "--export-sequence";
      const auto result = export_gaussian_film(scene.scene,path,app.arguments()[4],edge,png ? QString() : encoder,
          progress,{},png ? GaussianFilmFormat::PngSequence : GaussianFilmFormat::Mp4);
      reporter.request_stop();
      const auto output = result.value("receipt").toMap();
      std::cout << QJsonDocument::fromVariant(QVariantMap{{"ok",result.value("ok")},{"outputPath",result.value("outputPath")},
          {"frameCount",output.value("frameCount")},{"width",output.value("width")},{"height",output.value("height")},{"error",result.value("error")}}).toJson(QJsonDocument::Compact).constData() << '\n';
      return result.value("ok").toBool() ? 0 : 1;
    }
    const auto evidence = app.arguments().value(3) == "--replay-proof-dir" ? app.arguments()[4] : QString();
    if (!evidence.isEmpty() && (!QFileInfo(evidence).isAbsolute() || QFileInfo::exists(evidence) ||
        !QDir().mkpath(evidence+"/expected") || !QDir().mkpath(evidence+"/decoded"))) return 1;
    auto renderer = make_gaussian_scene_renderer(); if (!renderer) return 1;
    QByteArray decoded;
    if (!sequence) {
      QProcess decoder;
      decoder.start(encoder, {"-v", "error", "-i", movie, "-vf", "scale=in_color_matrix=bt709:in_range=tv:out_range=pc",
          "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"});
      if (!decoder.waitForFinished(15000) || decoder.exitCode() != 0) return 1;
      decoded = decoder.readAllStandardOutput();
      if (decoded.size() != frame_bytes * count) return 1;
    }
    double minimum = 100, total = 0; int maximum_error = 0; QVariantList samples;
    for (int index = 0; index < count; ++index) {
      const auto frame = renderer->render(scene.scene, path.sample(static_cast<double>(index) / path.fps).resized(size), true);
      if (!frame.error.isEmpty() || frame.image.isNull()) return 1;
      const auto expected = frame.image.convertToFormat(QImage::Format_RGB888);
      QImage actual;
      if (sequence) {
        const auto entry = sequence_frames[index].toMap(); const auto filename = QStringLiteral("frame-%1.png").arg(index,6,10,QChar('0'));
        if (entry.value("file") != filename || entry.value("frame").toInt() != index) return 1;
        const auto file_path = QDir(movie).filePath(filename); QFile file(file_path); QCryptographicHash hash(QCryptographicHash::Sha256);
        if (QFileInfo(file_path).isSymLink() || !file.open(QIODevice::ReadOnly) || !hash.addData(&file) ||
            entry.value("sha256") != QString::fromLatin1(hash.result().toHex())) return 1;
        actual = QImage(file_path);
        if (actual.size() != size || actual.colorSpace() != QColorSpace(QColorSpace::SRgb)) return 1;
        actual = actual.convertToFormat(QImage::Format_RGB888); QCryptographicHash pixels(QCryptographicHash::Sha256);
        for (int row = 0; row < actual.height(); ++row) pixels.addData(QByteArrayView(reinterpret_cast<const char*>(actual.constScanLine(row)),actual.width()*3));
        if (entry.value("rgbSha256") != QString::fromLatin1(pixels.result().toHex())) return 1;
      } else actual = QImage(reinterpret_cast<const uchar*>(decoded.constData()+frame_bytes*index),size.width(),size.height(),size.width()*3,QImage::Format_RGB888);
      if (!evidence.isEmpty()) {
        const auto name = QStringLiteral("/frame-%1.png").arg(index,4,10,QChar('0'));
        if (!expected.save(evidence+"/expected"+name) || !actual.save(evidence+"/decoded"+name)) return 1;
      }
      double squared_error = 0;
      for (int y = 0; y < size.height(); ++y) for (int x = 0; x < size.width() * 3; ++x) {
        const int delta = actual.constScanLine(y)[x] - expected.constScanLine(y)[x];
        maximum_error = std::max(maximum_error,std::abs(delta));
        const double difference = delta;
        squared_error += difference * difference;
      }
      const double psnr = squared_error == 0 ? 100 : 10 * std::log10(255.0 * 255.0 * static_cast<double>(frame_bytes) / squared_error);
      minimum = std::min(minimum, psnr); total += psnr;
      samples.append(QVariantMap{{"frame",index},{"rgbPsnrDb",psnr}});
    }
    if (!evidence.isEmpty()) {
      QFile report(evidence+"/receipt.json"); const auto bytes = QJsonDocument::fromVariant(QVariantMap{{"passed",sequence ? maximum_error == 0 : minimum >= 35},
          {"minimumRgbPsnrDb",minimum},{"meanRgbPsnrDb",total/count},{"requiredMinimumRgbPsnrDb",sequence ? 100 : 35},{"maximumChannelError",maximum_error},{"frames",samples},
          {"movie",movie},{"alphaModel",receipt.value("alphaModel")},{"scope",sequence ? "Decoded PNG and integrity hashes versus native replay; exact RGB required" : "Decoded MP4 versus native replay of frozen camera/composition receipt"}}).toJson();
      if (!report.open(QIODevice::WriteOnly|QIODevice::NewOnly) || report.write(bytes) != bytes.size()) return 1;
    }
    std::cout << "Decoded frame parity: frames=" << count << " minimum RGB PSNR=" << minimum << " mean=" << total / count << " max channel error=" << maximum_error << '\n';
    return (sequence ? maximum_error == 0 : minimum >= 35) ? 0 : 1;
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
  const auto encoding = result.value("receipt").toMap().value("encoding").toMap();
  if (encoding.value("codec") != "libx264" || encoding.value("pixelFormat") != "yuv420p" || encoding.value("preset") != "medium" || encoding.value("crf") != 10) return 1;
  QProcess inspect; inspect.start(probe, {"-v", "error", "-count_frames", "-show_streams", "-of", "json", destination});
  if (!inspect.waitForFinished(10000) || inspect.exitCode() != 0) return 1;
  const auto stream = QJsonDocument::fromJson(inspect.readAllStandardOutput()).toVariant().toMap().value("streams").toList().value(0).toMap();
  std::cout << QJsonDocument::fromVariant(stream).toJson().constData() << std::flush;
  if (stream.value("nb_read_frames").toInt() != 12 || stream.value("width").toInt() != 256 || stream.value("height").toInt() != 256 ||
      stream.value("r_frame_rate").toString() != "24/1" || stream.value("pix_fmt").toString() != "yuv420p" ||
      stream.value("color_transfer").toString() != "iec61966-2-1" || stream.value("color_space").toString() != "bt709" ||
      stream.value("color_range").toString() != "tv") return 1;
  // An autofocus film resolves each pose and records the actual focus used.
  // Decode its pixels and replay the recorded manual cameras independently.
  for (const auto profile : {GaussianAlphaModel::Brush999,GaussianAlphaModel::Inria99}) {
    auto af_path = path;
    auto composition = std::make_shared<GaussianSceneComposition>(); composition->alpha_model = profile;
    for (auto& camera : af_path.cameras) camera.composition = composition;
    for (auto& c : af_path.cameras) { c.photo.lens = {1,0.15F,0,0,0,16}; c.photo.lens.auto_focus = true; }
    af_path.cameras[1].position = {0,0,1};
    af_path.cameras[0].photo.tone = {0.8F,0.4F}; af_path.cameras[1].photo.tone = {1.6F,0.9F};
    const auto af_destination = QDir(directory.path()).filePath(profile == GaussianAlphaModel::Brush999 ? "autofocus.mp4" : "autofocus-inria.mp4");
    const auto af_result = export_gaussian_film(scene,af_path,af_destination,256,encoder,std::make_shared<GaussianFilmProgress>());
    const auto af_receipt = af_result.value("receipt").toMap(); const auto af_samples = af_receipt.value("autofocus").toMap().value("samples").toList();
    const auto expected_schema = profile == GaussianAlphaModel::Brush999 ? "framehouse-gaussian-film-v3" : "framehouse-gaussian-film-v4";
    if (!af_result.value("ok").toBool() || af_receipt.value("schema") != expected_schema || af_receipt.value("alphaModel") != gaussian_alpha_model_name(profile) || af_samples.size() != 12 ||
        af_samples[0].toMap().value("focusDistance").toDouble() != 3 || af_samples.back().toMap().value("focusDistance").toDouble() >= 2.1) return 1;
    const auto af_keyframes = af_receipt.value("keyframes").toList();
    if (af_keyframes.size() != 2 || af_keyframes[0].toMap().value("photo").toMap().value("tone").toMap() != af_path.cameras[0].photo.tone.description() ||
        af_keyframes[1].toMap().value("photo").toMap().value("tone").toMap() != af_path.cameras[1].photo.tone.description()) return 1;
    QProcess af_decoder;
    af_decoder.start(encoder,{"-v","error","-i",af_destination,"-vf","scale=in_color_matrix=bt709:in_range=tv:out_range=pc","-f","rawvideo","-pix_fmt","rgb24","pipe:1"});
    if (!af_decoder.waitForFinished(15000) || af_decoder.exitCode() != 0) return 1;
    const auto af_decoded = af_decoder.readAllStandardOutput(); constexpr int af_frame_bytes = 256*256*3;
    if (af_decoded.size() != af_frame_bytes*12) return 1;
    auto af_renderer = make_gaussian_scene_renderer(); double af_minimum_psnr = 100;
    for (int index = 0; index < 12; ++index) {
      const auto sample = af_samples[index].toMap();
      if (sample.value("frame").toInt() != index || !sample.value("locked").toBool()) return 1;
      auto c = af_path.sample(static_cast<double>(index)/24).resized({256,256});
      c.photo.lens.auto_focus = false; c.photo.lens.focus_distance = sample.value("focusDistance").toFloat();
      const auto frame = af_renderer->render(scene,c,true); if (!frame.error.isEmpty()) return 1;
      const auto expected = frame.image.convertToFormat(QImage::Format_RGB888); double squared = 0;
      for (int y = 0; y < 256; ++y) for (int x = 0; x < 256*3; ++x) {
        const double d = static_cast<unsigned char>(af_decoded[index*af_frame_bytes+y*256*3+x])-expected.constScanLine(y)[x]; squared += d*d;
      }
      const double psnr = squared == 0 ? 100 : 10*std::log10(255.0*255.0*af_frame_bytes/squared); af_minimum_psnr = std::min(af_minimum_psnr,psnr);
    }
    if (af_minimum_psnr < 35) return 1;
    std::cout << "Autofocus/tone film (" << gaussian_alpha_model_name(profile).toStdString() << "): 12 changing resolved focuses and interpolated tones; decoded/manual replay minimum PSNR=" << af_minimum_psnr << '\n';
  }
  auto mixed = path; auto original_profile = std::make_shared<GaussianSceneComposition>(); original_profile->alpha_model = GaussianAlphaModel::Inria99;
  mixed.cameras[1].composition = original_profile;
  const auto mixed_output = directory.filePath("mixed.mp4");
  if (export_gaussian_film(scene,mixed,mixed_output,256,encoder,std::make_shared<GaussianFilmProgress>()).value("ok").toBool() || QFileInfo::exists(mixed_output)) return 1;
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

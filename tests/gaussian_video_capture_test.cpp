#include "gaussian_video_capture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"});
  const auto ffprobe = QStandardPaths::findExecutable("ffprobe", {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"});
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) { std::cerr << "FFmpeg/FFprobe unavailable\n"; return 77; }
  auto canceled = std::make_shared<std::atomic_bool>(false);
  auto log = std::make_shared<GaussianProcessLog>();
  const auto args = app.arguments();
  if (args.size() == 6 && args[1] == "--prepare") {
    if (!QFileInfo(args[3]).isAbsolute() || QFileInfo::exists(args[3]) || !QDir().mkpath(args[3])) return 1;
    const auto result = prepare_gaussian_video(args[2], args[3], args[4].toInt(), args[5].toInt(), ffmpeg, ffprobe, canceled, log);
    std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.value("ok").toBool() ? 0 : 1;
  }
  QTemporaryDir root;
  const auto source = root.filePath("capture.mp4");
  const auto generated = run_gaussian_process(ffmpeg, {"-hide_banner", "-loglevel", "error", "-nostdin", "-n",
      "-f", "lavfi", "-i", "testsrc2=size=96x64:rate=24", "-t", "2", "-vf", "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709", "-c:v", "libx264", "-threads", "1",
      "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709", source},
      root.path(), QProcessEnvironment::systemEnvironment(), canceled, log);
  if (generated.value("exitCode").toInt() != 0) { std::cerr << log->tail.toStdString(); return 1; }
  QFile original(source); if (!original.open(QIODevice::ReadOnly)) return 1;
  const auto before = original.readAll(); original.close();
  const auto project = root.filePath("project"); QDir().mkpath(project);
  const auto result = prepare_gaussian_video(source, project, 512, 16, ffmpeg, ffprobe, canceled, log);
  if (!result.value("ok").toBool() || result.value("frameCount").toInt() != 16 ||
      result.value("sourceSha256").toString().size() != 64 || !QFileInfo::exists(QDir(project).filePath("video-capture.json"))) {
    auto debug_log = std::make_shared<GaussianProcessLog>();
    run_gaussian_process(ffprobe, {"-v", "error", "-show_streams", "-of", "json", source}, root.path(), QProcessEnvironment::systemEnvironment(), canceled, debug_log);
    std::cerr << QJsonDocument::fromVariant(result).toJson().constData() << debug_log->tail.toStdString(); return 1;
  }
  const auto frames = result.value("frames").toStringList();
  if (QImage(frames.front()).size() != QSize(96,64) || QImage(frames.last()).isNull()) return 1;
  if (prepare_gaussian_video(source, project, 512, 16, ffmpeg, ffprobe, canceled, log).value("ok").toBool()) return 1;
  // A lossless neutral Y'=126 patch verifies the Rec.709-to-sRGB transfer,
  // independently of the extractor's filter string or metadata receipt.
  const auto gray_source = root.filePath("gray.mp4");
  const auto gray_generated = run_gaussian_process(ffmpeg, {"-hide_banner", "-loglevel", "error", "-nostdin", "-n",
      "-f", "lavfi", "-i", "nullsrc=size=96x64:rate=24,geq=lum=126:cb=128:cr=128,setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709",
      "-t", "2", "-c:v", "libx264", "-crf", "0", "-threads", "1", gray_source},
      root.path(), QProcessEnvironment::systemEnvironment(), canceled, log);
  const auto gray_project = root.filePath("gray"); QDir().mkpath(gray_project);
  const auto gray_result = prepare_gaussian_video(gray_source, gray_project, 512, 16, ffmpeg, ffprobe, canceled, log);
  if (gray_generated.value("exitCode").toInt() != 0 || !gray_result.value("ok").toBool()) {
    std::cerr << QJsonDocument::fromVariant(gray_result).toJson().constData(); return 1;
  }
  const double encoded = (126.0 - 16) / 219;
  const double linear = std::pow((encoded + 0.099) / 1.099, 1 / 0.45);
  const int expected = qRound(255 * (1.055 * std::pow(linear, 1 / 2.4) - 0.055));
  const auto gray = QImage(gray_result.value("frames").toStringList().front()).pixelColor(48, 32);
  if (std::abs(gray.red()-expected) > 2 || std::abs(gray.green()-expected) > 2 || std::abs(gray.blue()-expected) > 2) {
    std::cerr << "Video transfer mismatch: expected " << expected << ", got " << gray.red() << ',' << gray.green() << ',' << gray.blue(); return 1;
  }
  const auto dense_source = root.filePath("dense.mp4");
  const auto dense_generated = run_gaussian_process(ffmpeg, {"-hide_banner", "-loglevel", "error", "-nostdin", "-n",
      "-f", "lavfi", "-i", "testsrc2=size=96x64:rate=240", "-t", "4", "-vf",
      "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709", "-c:v", "libx264", "-threads", "1", dense_source},
      root.path(), QProcessEnvironment::systemEnvironment(), canceled, log);
  const auto dense_project = root.filePath("dense"); QDir().mkpath(dense_project);
  if (prepare_gaussian_video(dense_source, dense_project, 256, 1921, ffmpeg, ffprobe, canceled, log).value("ok").toBool() ||
      QFileInfo::exists(QDir(dense_project).filePath("video-frames"))) return 1;
  const auto dense_result = prepare_gaussian_video(dense_source, dense_project, 256, 960, ffmpeg, ffprobe, canceled, log);
  if (dense_generated.value("exitCode").toInt() != 0 || !dense_result.value("ok").toBool() ||
      dense_result.value("frameCount").toInt() != 960) {
    std::cerr << "Dense video extraction failed: " << dense_result.value("error").toString().toStdString(); return 1;
  }
  canceled->store(true);
  const auto canceled_project = root.filePath("canceled"); QDir().mkpath(canceled_project);
  if (prepare_gaussian_video(source, canceled_project, 512, 16, ffmpeg, ffprobe, canceled, log).value("ok").toBool() ||
      QFileInfo::exists(QDir(canceled_project).filePath("video-frames"))) return 1;
  if (!original.open(QIODevice::ReadOnly) || original.readAll() != before) return 1;
  std::cout << "Real SDR video extraction, analytical neutral transfer, bounded frame count, no upscaling, provenance, no overwrite and cancellation pass.\n";
  return 0;
}

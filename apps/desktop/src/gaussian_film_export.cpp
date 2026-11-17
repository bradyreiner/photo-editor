#include "gaussian_film_export.h"
#include "gaussian_film_publication.h"
#include "gaussian_saved_views.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QImageWriter>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {
// libpng writes through this device in bounded chunks. Cancellation can stop
// compression as well as GPU rendering, without exposing a partial sequence.
class SequenceOutput final : public QIODevice {
 public:
  SequenceOutput(QFile& file,const std::shared_ptr<GaussianFilmProgress>& progress) : file_(file),progress_(progress) { open(QIODevice::WriteOnly); }
  QByteArray sha256() const { return hash_.result().toHex(); }
  bool isSequential() const override { return true; }
 protected:
  qint64 readData(char*,qint64) override { return -1; }
  qint64 writeData(const char* data,qint64 size) override {
    if (progress_->canceled.load()) return -1;
    const auto written = file_.write(data,size);
    if (written > 0) hash_.addData(QByteArrayView(data,static_cast<qsizetype>(written)));
    return written;
  }
 private:
  QFile& file_;
  std::shared_ptr<GaussianFilmProgress> progress_;
  QCryptographicHash hash_{QCryptographicHash::Sha256};
};
}
QVariantMap export_gaussian_film(const std::shared_ptr<const GaussianSceneData>& scene,
    const GaussianCameraPath& path, const QString& destination, int maximum_edge,
    const QString& ffmpeg, const std::shared_ptr<GaussianFilmProgress>& progress,
    std::optional<GaussianFilmSceneTime> scene_time, GaussianFilmFormat format) {
  const auto fail = [](const QString& error) { return QVariantMap{{"ok", false}, {"error", error}}; };
  if (format != GaussianFilmFormat::Mp4 && format != GaussianFilmFormat::PngSequence) return fail("Unknown film output format.");
  const bool sequence = format == GaussianFilmFormat::PngSequence;
  if (!scene || !progress || path.cameras.size() < 2 || !std::isfinite(path.duration()) || path.duration() < .5 ||
      path.duration() > 600 || (path.fps != 24 && path.fps != 25 && path.fps != 30 && path.fps != 60) ||
      maximum_edge < 256 || maximum_edge > (sequence ? 8192 : 4096) || (!sequence && ffmpeg.isEmpty()) || destination.contains(QChar::Null) || !QFileInfo(destination).isAbsolute() ||
      (!sequence && QFileInfo(destination).suffix().toLower() != "mp4") || QFileInfo(destination).fileName().isEmpty() ||
      QFileInfo::exists(destination) || QFileInfo(destination).isSymLink() || (!sequence && (QFileInfo::exists(destination + ".camera-path.json") ||
      QFileInfo(destination + ".camera-path.json").isSymLink()))) return fail(sequence ? "Choose a new frame-sequence folder, a valid path and an edge of 256–8192 pixels." :
          "Choose a new MP4 destination, a valid path and an edge of 256–4096 pixels. FFmpeg is required.");
  for (const auto& camera : path.cameras) {
    if (!gaussian_saved_camera(camera.description())) return fail("Invalid film camera.");
    if (!valid_gaussian_alpha_model(gaussian_alpha_model(camera)) || gaussian_alpha_model(camera) != gaussian_alpha_model(path.cameras.front()))
      return fail("Film cameras must share the source renderer convention.");
    if (camera.photo.lens.auto_focus != path.cameras.front().photo.lens.auto_focus) return fail("Film shots must use the same focus mode.");
    if (camera.photo.lens.auto_focus && camera.physical && path.cameras.front().physical) {
      auto current = camera.physical->settings, first = path.cameras.front().physical->settings;
      current.focus_distance_m = first.focus_distance_m;
      if (current != first) return fail("Autofocus films must share the same physical lens and scene scale.");
    } else if (camera.photo.lens.auto_focus && camera.physical.has_value() != path.cameras.front().physical.has_value())
      return fail("Autofocus films must use the same camera model throughout.");
  }
  if(scene->temporal.empty()==scene_time.has_value()) return fail("Temporal films require an explicit scene-time interval; static films must not supply one.");
  if(scene_time) {
    const auto first=scene->time_origin_seconds+scene->local_time_range[0];
    const auto last=scene->time_origin_seconds+scene->local_time_range[1];
    if(!std::isfinite(first) || !std::isfinite(last) || first>=last ||
        !std::isfinite(scene_time->start_seconds) || !std::isfinite(scene_time->end_seconds) ||
        scene_time->start_seconds<first || scene_time->start_seconds>last ||
        scene_time->end_seconds<first || scene_time->end_seconds>last)
      return fail("Film scene times must remain inside the artifact interval.");
  }
  if (progress->canceled.load()) return {{"ok", false}, {"canceled", true}};
  QTemporaryDir scratch(QDir(QFileInfo(destination).absolutePath()).filePath(".framehouse-film-XXXXXX"));
  if (!scratch.isValid()) return fail("Cannot create temporary film output beside the destination.");
  const auto movie = QDir(scratch.path()).filePath("film.mp4");
  QSize size = path.cameras.front().image_size;
  size.scale(maximum_edge, maximum_edge, Qt::KeepAspectRatio);
  if (!sequence) { size.setWidth(std::max(2, size.width() / 2 * 2)); size.setHeight(std::max(2, size.height() / 2 * 2)); }
  QProcess encoder;
  encoder.setProcessChannelMode(QProcess::MergedChannels);
  constexpr int film_crf = 10;
  const QStringList arguments{"-hide_banner", "-loglevel", "error", "-nostdin", "-n", "-f", "rawvideo",
      "-pixel_format", "rgb24", "-video_size", QString("%1x%2").arg(size.width()).arg(size.height()),
      "-framerate", QString::number(path.fps), "-i", "pipe:0", "-an", "-c:v", "libx264", "-preset", "medium",
      "-crf", QString::number(film_crf), "-threads", "4", "-vf",
      "scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuv420p,setparams=range=limited:color_primaries=bt709:color_trc=iec61966-2-1:colorspace=bt709",
      "-color_primaries", "bt709", "-color_trc", "iec61966-2-1", "-colorspace", "bt709", "-color_range", "tv",
      "-movflags", "+faststart", movie};
  if (!sequence) {
    encoder.start(ffmpeg, arguments);
    if (!encoder.waitForStarted(5000)) return fail("Cannot start FFmpeg for film export.");
  }
  QByteArray diagnostic;
  const auto drain = [&] { diagnostic = (diagnostic + encoder.readAll()).right(4096); };
  const auto stop = [&] { if (!sequence) { encoder.kill(); encoder.waitForFinished(3000); drain(); } };
  const auto canceled = [&]() -> QVariantMap { stop(); return {{"ok", false}, {"canceled", true}}; };
  auto renderer = make_gaussian_scene_renderer();
  if (!renderer) { stop(); return fail("Native Gaussian film rendering requires Metal on macOS."); }
  QVariantList autofocus_samples;
  QVariantList sequence_frames;
  const bool autofocus = path.cameras.front().photo.lens.auto_focus;
  const int count = path.frameCount();
  for (int index = 0; index < count; ++index) {
    if (progress->canceled.load()) return canceled();
    const auto camera = path.sample(static_cast<double>(index) / path.fps).resized(size);
    const auto seconds=scene_time ? std::optional<double>{std::lerp(scene_time->start_seconds,scene_time->end_seconds,
        static_cast<double>(index)/path.fps/path.duration())} : std::optional<double>{};
    const GaussianRenderOptions options{std::shared_ptr<std::atomic_bool>(progress, &progress->canceled), {}, false};
    const auto frame = renderer->renderWithOptions(scene, camera, true, seconds, options);
    if (frame.canceled || progress->canceled.load()) return canceled();
    if (!frame.error.isEmpty() || frame.image.isNull() || frame.image.size() != size) { stop(); return fail("Film frame failed: " + frame.error); }
    if (autofocus) {
      const auto focus = frame.metrics.value("autofocus").toMap();
      if (focus.isEmpty() || frame.metrics.value("resolvedCamera").toMap().isEmpty()) { stop(); return fail("Film autofocus did not report the resolved camera."); }
      QVariantMap sample{{"frame",index},{"focusDistance",focus.value("resolvedFocusDistance")},{"locked",focus.value("locked")}};
      if (focus.contains("resolvedFocusDistanceM")) sample.insert("focusDistanceM",focus.value("resolvedFocusDistanceM"));
      if (focus.contains("reason")) sample.insert("reason",focus.value("reason"));
      autofocus_samples.append(sample);
    }
    const auto image = frame.image.convertToFormat(QImage::Format_RGB888);
    if (sequence) {
      const auto name = QStringLiteral("frame-%1.png").arg(index,6,10,QChar('0'));
      QFile file(QDir(scratch.path()).filePath(name));
      if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return fail("Cannot stage frame sequence image.");
      QCryptographicHash pixels(QCryptographicHash::Sha256);
      for (int row = 0; row < image.height(); ++row) {
        if (progress->canceled.load()) return canceled();
        pixels.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(row)),image.width()*3));
      }
      SequenceOutput sink(file,progress); QImageWriter writer(&sink,"PNG");
      if (!writer.write(image) || !file.flush()) {
        if (progress->canceled.load()) return canceled();
        return fail("Cannot write frame sequence PNG: " + writer.errorString());
      }
      sink.close(); file.close();
      sequence_frames.append(QVariantMap{{"frame",index},{"file",name},{"seconds",static_cast<double>(index)/path.fps},
          {"sha256",QString::fromLatin1(sink.sha256())},{"rgbSha256",QString::fromLatin1(pixels.result().toHex())}});
      progress->completed.store(index+1);
      continue;
    }
    // One bounded frame in the pipe at a time, excluding QImage row padding.
    for (int row = 0; row < image.height(); ++row) {
      if (encoder.write(reinterpret_cast<const char*>(image.constScanLine(row)), image.width() * 3) != image.width() * 3) {
        stop(); return fail("Film encoder stopped accepting pixels: " + QString::fromUtf8(diagnostic));
      }
    }
    QElapsedTimer stalled; stalled.start();
    while (encoder.bytesToWrite() > 0) {
      if (progress->canceled.load()) return canceled();
      encoder.waitForBytesWritten(100); drain();
      if (encoder.state() == QProcess::NotRunning || stalled.elapsed() > 15000) {
        stop(); return fail("Film encoder stalled or failed: " + QString::fromUtf8(diagnostic));
      }
    }
    progress->completed.store(index + 1);
  }
  if (!sequence) {
    encoder.closeWriteChannel();
    QElapsedTimer finishing; finishing.start();
    while (encoder.state() != QProcess::NotRunning) {
      if (progress->canceled.load()) return canceled();
      encoder.waitForFinished(100); drain();
      if (finishing.elapsed() > 60000) { stop(); return fail("Film encoder finalization timed out."); }
    }
    drain();
    if (encoder.exitStatus() != QProcess::NormalExit || encoder.exitCode() != 0 || QFileInfo(movie).size() == 0)
      return fail("Film encoding failed: " + QString::fromUtf8(diagnostic));
  }
  if (progress->canceled.load()) return {{"ok", false}, {"canceled", true}};
  QVariantList cameras; for (const auto& camera : path.cameras) cameras.append(camera.description());
  QVariantMap receipt{{"schema", "framehouse-gaussian-film-v1"}, {"artifactSha256", scene->artifact_sha256},
      {"keyframes", cameras}, {"viewIds", path.view_ids}, {"segmentSeconds", path.segment_seconds},
      {"fps", path.fps}, {"frameCount", count}, {"durationSeconds", static_cast<double>(count) / path.fps},
      {"width", size.width()}, {"height", size.height()}, {"interpolation", "smoothstep-position-intrinsics-shortest-slerp-v1"},
      {"sampleTime", "frameIndex / fps"},
      {"encoding",QVariantMap{{"codec","libx264"},{"crf",film_crf},{"preset","medium"},{"pixelFormat","yuv420p"}}},
      {"colorContract", "sRGB transfer and BT709 primaries; BT709 limited-range YUV420"}, {"outputPath", destination}};
  if (std::any_of(path.cameras.begin(),path.cameras.end(),[](const auto& camera) { return camera.physical.has_value(); }))
    receipt.insert("schema","framehouse-gaussian-film-v2");
  if (autofocus) {
    receipt.insert("schema","framehouse-gaussian-film-v3");
    receipt.insert("autofocus",QVariantMap{{"mode","auto-centre"},{"model","unfiltered-chief-ray-opacity-median-v1"},
        {"sampleTime","frameIndex / fps"},{"fallback","authored-focus-distance"},{"samples",autofocus_samples}});
  }
  receipt.insert("alphaModel",gaussian_alpha_model_name(gaussian_alpha_model(path.cameras.front())));
  if (gaussian_alpha_model(path.cameras.front()) != GaussianAlphaModel::Brush999) receipt.insert("schema","framehouse-gaussian-film-v4");
  if(scene_time) receipt.insert("sceneTime",QVariantMap{{"mapping","linear-scene-time-v1"},
      {"startSeconds",scene_time->start_seconds},{"endSeconds",scene_time->end_seconds},
      {"nominalDurationSeconds",path.duration()},{"timeUnit","seconds"},
      {"sampleTime","startSeconds + (endSeconds - startSeconds) * (frameIndex / fps) / nominalDurationSeconds"},
      {"endpoint","exclusive"}});
  if (!scene->cleanup_receipt.isEmpty()) receipt.insert("sceneCleanup", scene->cleanup_receipt);
  if (path.cameras.front().composition && path.cameras.front().composition->description() != GaussianSceneComposition{}.description())
    receipt.insert("sceneObjects", path.cameras.front().composition->description());
  if (sequence) {
    receipt.insert("schema","framehouse-gaussian-frame-sequence-v1");
    receipt.insert("encoding",QVariantMap{{"codec","png"},{"pixelFormat","rgb8"},{"lossless",true}});
    receipt.insert("colorContract","8-bit sRGB rendered RGB; no chroma subsampling; output effects baked in");
    receipt.insert("framePattern","frame-%06d.png"); receipt.insert("firstFrame",0); receipt.insert("frames",sequence_frames);
  }
  const auto staged_receipt = QDir(scratch.path()).filePath("camera-path.json");
  QFile sidecar(staged_receipt);
  const auto bytes = QJsonDocument::fromVariant(receipt).toJson();
  if (!sidecar.open(QIODevice::WriteOnly | QIODevice::NewOnly) || sidecar.write(bytes) != bytes.size() || !sidecar.flush())
    return fail("Cannot stage film camera receipt. No final output was published.");
  sidecar.close();
  // Cancellation is accepted until the short, noninterruptible publication.
  if (progress->canceled.load()) return {{"ok", false}, {"canceled", true}};
  const auto publication = sequence ? publish_gaussian_frame_sequence(scratch.path(),destination) : publish_gaussian_film_files(movie, staged_receipt, destination);
  if (!publication.ok) return {{"ok", false}, {"error", publication.error},
      {"receiptRetained", publication.receipt_retained}};
  if (sequence) scratch.setAutoRemove(false);
  return {{"ok", true}, {"outputPath", destination}, {"receipt", receipt},{"outputFormat",sequence ? "png-sequence" : "mp4"}};
}
}  // namespace photo_editor::desktop

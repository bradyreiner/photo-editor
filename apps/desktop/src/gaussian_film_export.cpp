#include "gaussian_film_export.h"
#include "gaussian_film_publication.h"
#include "gaussian_saved_views.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
QVariantMap export_gaussian_film(const std::shared_ptr<const GaussianSceneData>& scene,
    const GaussianCameraPath& path, const QString& destination, int maximum_edge,
    const QString& ffmpeg, const std::shared_ptr<GaussianFilmProgress>& progress,
    std::optional<GaussianFilmSceneTime> scene_time) {
  const auto fail = [](const QString& error) { return QVariantMap{{"ok", false}, {"error", error}}; };
  if (!scene || !progress || path.cameras.size() < 2 || !std::isfinite(path.duration()) || path.duration() < .5 ||
      path.duration() > 600 || (path.fps != 24 && path.fps != 25 && path.fps != 30 && path.fps != 60) ||
      maximum_edge < 256 || maximum_edge > 4096 || ffmpeg.isEmpty() || !QFileInfo(destination).isAbsolute() ||
      QFileInfo(destination).suffix().toLower() != "mp4" || QFileInfo::exists(destination) ||
      QFileInfo(destination).isSymLink() || QFileInfo::exists(destination + ".camera-path.json") ||
      QFileInfo(destination + ".camera-path.json").isSymLink()) return fail("Choose a new MP4 destination, a valid path and an edge of 256–4096 pixels. FFmpeg is required.");
  for (const auto& camera : path.cameras) if (!gaussian_saved_camera(camera.description())) return fail("Invalid film camera.");
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
  size.setWidth(std::max(2, size.width() / 2 * 2)); size.setHeight(std::max(2, size.height() / 2 * 2));
  QProcess encoder;
  encoder.setProcessChannelMode(QProcess::MergedChannels);
  const QStringList arguments{"-hide_banner", "-loglevel", "error", "-nostdin", "-n", "-f", "rawvideo",
      "-pixel_format", "rgb24", "-video_size", QString("%1x%2").arg(size.width()).arg(size.height()),
      "-framerate", QString::number(path.fps), "-i", "pipe:0", "-an", "-c:v", "libx264", "-preset", "medium",
      "-crf", "18", "-threads", "4", "-vf",
      "scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuv420p,setparams=range=limited:color_primaries=bt709:color_trc=iec61966-2-1:colorspace=bt709",
      "-color_primaries", "bt709", "-color_trc", "iec61966-2-1", "-colorspace", "bt709", "-color_range", "tv",
      "-movflags", "+faststart", movie};
  encoder.start(ffmpeg, arguments);
  if (!encoder.waitForStarted(5000)) return fail("Cannot start FFmpeg for film export.");
  QByteArray diagnostic;
  const auto drain = [&] { diagnostic = (diagnostic + encoder.readAll()).right(4096); };
  const auto stop = [&] { encoder.kill(); encoder.waitForFinished(3000); drain(); };
  const auto canceled = [&]() -> QVariantMap { stop(); return {{"ok", false}, {"canceled", true}}; };
  auto renderer = make_gaussian_scene_renderer();
  if (!renderer) { stop(); return fail("Native Gaussian film rendering requires Metal on macOS."); }
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
    const auto image = frame.image.convertToFormat(QImage::Format_RGB888);
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
  if (progress->canceled.load()) return {{"ok", false}, {"canceled", true}};
  QVariantList cameras; for (const auto& camera : path.cameras) cameras.append(camera.description());
  QVariantMap receipt{{"schema", "framehouse-gaussian-film-v1"}, {"artifactSha256", scene->artifact_sha256},
      {"keyframes", cameras}, {"viewIds", path.view_ids}, {"segmentSeconds", path.segment_seconds},
      {"fps", path.fps}, {"frameCount", count}, {"durationSeconds", static_cast<double>(count) / path.fps},
      {"width", size.width()}, {"height", size.height()}, {"interpolation", "smoothstep-position-intrinsics-shortest-slerp-v1"},
      {"sampleTime", "frameIndex / fps"},
      {"colorContract", "sRGB transfer and BT709 primaries; BT709 limited-range YUV420"}, {"outputPath", destination}};
  if (std::any_of(path.cameras.begin(),path.cameras.end(),[](const auto& camera) { return camera.physical.has_value(); }))
    receipt.insert("schema","framehouse-gaussian-film-v2");
  if(scene_time) receipt.insert("sceneTime",QVariantMap{{"mapping","linear-scene-time-v1"},
      {"startSeconds",scene_time->start_seconds},{"endSeconds",scene_time->end_seconds},
      {"nominalDurationSeconds",path.duration()},{"timeUnit","seconds"},
      {"sampleTime","startSeconds + (endSeconds - startSeconds) * (frameIndex / fps) / nominalDurationSeconds"},
      {"endpoint","exclusive"}});
  const auto staged_receipt = QDir(scratch.path()).filePath("camera-path.json");
  QFile sidecar(staged_receipt);
  const auto bytes = QJsonDocument::fromVariant(receipt).toJson();
  if (!sidecar.open(QIODevice::WriteOnly | QIODevice::NewOnly) || sidecar.write(bytes) != bytes.size() || !sidecar.flush())
    return fail("Cannot stage film camera receipt. No final output was published.");
  sidecar.close();
  // Cancellation is accepted until the short, noninterruptible publication.
  if (progress->canceled.load()) return {{"ok", false}, {"canceled", true}};
  const auto publication = publish_gaussian_film_files(movie, staged_receipt, destination);
  if (!publication.ok) return {{"ok", false}, {"error", publication.error},
      {"receiptRetained", publication.receipt_retained}};
  return {{"ok", true}, {"outputPath", destination}, {"receipt", receipt}};
}
}  // namespace photo_editor::desktop

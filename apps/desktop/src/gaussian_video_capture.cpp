#include "gaussian_video_capture.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStorageInfo>
#include <cmath>

namespace photo_editor::desktop {
QVariantMap prepare_gaussian_video(const QString& source, const QString& project, int maximum_edge,
    int frame_count, const QString& ffmpeg, const QString& ffprobe,
    const std::shared_ptr<std::atomic_bool>& canceled, const std::shared_ptr<GaussianProcessLog>& log) {
  const auto fail = [](const QString& message) { return QVariantMap{{"ok", false}, {"error", message}}; };
  const QFileInfo before(source);
  const QDir root(project);
  if (!canceled || !log || !before.isFile() || before.isSymLink() || !root.isAbsolute() || !root.exists() ||
      maximum_edge < 256 || maximum_edge > 4096 || frame_count < 16 || frame_count > 1920)
    return fail("Invalid video preparation request.");
  auto probe_log = std::make_shared<GaussianProcessLog>();
  const auto probe = run_gaussian_process(ffprobe, {"-v", "error", "-protocol_whitelist", "file,pipe", "-select_streams", "v:0", "-show_entries",
      "stream=width,height,duration,color_space,color_transfer,color_primaries:stream_side_data=rotation:format=duration",
      "-of", "json", source}, project, QProcessEnvironment::systemEnvironment(), canceled, probe_log);
  if (canceled->load()) return fail("Video preparation canceled; original is unchanged.");
  if (probe.value("exitCode").toInt() != 0 || probe.value("failedToStart").toBool())
    return fail("Cannot read this video. " + probe_log->tail.right(2048));
  const auto metadata = QJsonDocument::fromJson(probe_log->tail.toUtf8()).toVariant().toMap();
  const auto streams = metadata.value("streams").toList();
  if (streams.isEmpty()) return fail("This file has no readable video stream.");
  const auto stream = streams.front().toMap();
  const auto duration = stream.value("duration", metadata.value("format").toMap().value("duration")).toDouble();
  const int width = stream.value("width").toInt(), height = stream.value("height").toInt();
  if (!std::isfinite(duration) || duration < 2 || duration > 1800 || width < 16 || height < 16 || width > 8192 || height > 8192)
    return fail("Choose a video between 2 seconds and 30 minutes, with dimensions up to 8192 pixels.");
  const auto transfer = stream.value("color_transfer").toString();
  const auto primaries = stream.value("color_primaries").toString();
  const auto matrix = stream.value("color_space").toString();
  // Never silently tag HDR or unknown camera-log values as sRGB. This first
  // bridge supports explicitly tagged Rec.709/sRGB SDR captures only.
  if ((transfer != "bt709" && transfer != "iec61966-2-1") || primaries != "bt709" || matrix != "bt709")
    return fail(QString("Unsupported video color (%1 / %2 / %3). Export a tagged SDR Rec.709 version first; the original will not be altered.").arg(primaries, matrix, transfer));
  const auto frames_dir = root.filePath("video-frames");
  if (QFileInfo::exists(frames_dir)) return fail("Video frame folder already exists; it will not be overwritten.");
  const QStorageInfo storage(project);
  const qint64 estimated_bytes = static_cast<qint64>(maximum_edge) * maximum_edge * 3 * frame_count * 3;
  if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < estimated_bytes + 1024LL * 1024 * 1024)
    return fail("Not enough free space for video frames, prepared images and training. Choose fewer frames or a smaller image edge.");
  QFile input(source);
  if (!input.open(QIODevice::ReadOnly)) return fail("Cannot read the source video.");
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!input.atEnd()) {
    if (canceled->load()) return fail("Video preparation canceled; original is unchanged.");
    const auto bytes = input.read(4 * 1024 * 1024);
    if (bytes.isEmpty()) return fail("Cannot hash the complete source video.");
    hash.addData(bytes);
  }
  input.close();
  if (!root.mkdir("video-frames")) return fail("Cannot create the video frame folder.");
  const auto rate = QString::number(static_cast<double>(frame_count) / duration, 'g', 15);
  const auto filter = QString("fps=%1:start_time=0:round=near,scale=w='min(%2,iw)':h='min(%2,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2:flags=lanczos,"
      "colorspace=all=bt709:trc=srgb:format=yuv444p,format=rgb24").arg(rate).arg(maximum_edge);
  const auto extraction = run_gaussian_process(ffmpeg, {"-hide_banner", "-loglevel", "error", "-nostdin", "-n",
      "-threads", "2", "-protocol_whitelist", "file,pipe", "-i", source, "-map", "0:v:0", "-an", "-sn", "-dn", "-filter_threads", "2",
      "-vf", filter, "-frames:v", QString::number(frame_count), "-threads", "2", "-compression_level", "3",
      "-progress", "pipe:1", "-nostats", QDir(frames_dir).filePath("frame_%06d.png")},
      project, QProcessEnvironment::systemEnvironment(), canceled, log);
  if (canceled->load()) return fail("Video preparation canceled; partial frames retained and original unchanged.");
  if (extraction.value("exitCode").toInt() != 0 || extraction.value("failedToStart").toBool())
    return fail("Video frame extraction failed. " + log->tail.right(2048));
  const QFileInfo after(source);
  if (after.size() != before.size() || after.lastModified() != before.lastModified())
    return fail("The source video changed during preparation. Partial frames are retained; please start again.");
  QStringList frames;
  QVariantList samples;
  for (const auto& frame : QDir(frames_dir).entryInfoList({"frame_*.png"}, QDir::Files, QDir::Name)) {
    if (frame.isSymLink() || frame.size() == 0) return fail("Invalid extracted video frame.");
    const auto index = frames.size();
    frames.append(frame.canonicalFilePath());
    samples.append(QVariantMap{{"path", "video-frames/" + frame.fileName()},
        {"sampleTimeSeconds", static_cast<double>(index) * duration / frame_count}});
  }
  if (frames.size() < 8 || frames.size() > frame_count) return fail("The clip did not yield enough frames for reconstruction.");
  QVariantMap result{{"ok", true}, {"schema", "framehouse-video-capture-v1"}, {"source", source},
      {"sourceSha256", QString::fromLatin1(hash.result().toHex())}, {"originalsModified", false},
      {"durationSeconds", duration}, {"sourceWidth", width}, {"sourceHeight", height},
      {"sourceMetadata", stream}, {"frames", frames}, {"frameCount", frames.size()}, {"samples", samples},
      {"selection", "uniform-time-sampling-v1; nearest decoded frame, not blur-ranked or exact source PTS"},
      {"colorContract", "tagged-SDR-Rec709-to-sRGB; no HDR tonemapping; display-referred"},
      {"ffmpegFilter", filter}, {"rotation", "ffmpeg-autorotate"}};
  QSaveFile receipt(root.filePath("video-capture.json"));
  const auto bytes = QJsonDocument::fromVariant(result).toJson();
  if (!receipt.open(QIODevice::WriteOnly) || receipt.write(bytes) != bytes.size() || !receipt.commit())
    return fail("Cannot save video capture provenance.");
  return result;
}
}

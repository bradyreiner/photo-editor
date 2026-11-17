#include "gaussian_video_capture.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QImageReader>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <cmath>
#include <vector>

namespace photo_editor::desktop {
namespace {
// A focus ranking at one fixed proxy scale, not an image-quality or motion
// detector. Locked capture exposure is still necessary: noise, illumination
// changes and moving subjects can affect this local edge statistic.
double focus_score(const QString& path) {
  QImageReader reader(path);
  const auto size = reader.size();
  if (size.width() < 3 || size.height() < 3 || size.width() > 320 || size.height() > 320) return -1;
  const auto image = reader.read().convertToFormat(QImage::Format_RGB32);
  if (image.isNull()) return -1;
  std::vector<double> luma(static_cast<size_t>(size.width()*size.height()));
  for (int y = 0; y < size.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < size.width(); ++x)
      luma[static_cast<size_t>(y*size.width()+x)] = (0.2126*qRed(row[x])+0.7152*qGreen(row[x])+0.0722*qBlue(row[x]))/255;
  }
  double sum = 0, squared = 0;
  for (int y = 1; y+1 < size.height(); ++y) for (int x = 1; x+1 < size.width(); ++x) {
    const auto i = static_cast<size_t>(y*size.width()+x), stride = static_cast<size_t>(size.width());
    const auto laplacian = 4*luma[i]-luma[i-1]-luma[i+1]-luma[i-stride]-luma[i+stride];
    sum += laplacian; squared += laplacian*laplacian;
  }
  const auto count = static_cast<double>((size.width()-2)*(size.height()-2));
  return std::max(0.0,squared/count-(sum/count)*(sum/count));
}
}
QVariantMap prepare_gaussian_video(const QString& source, const QString& project, int maximum_edge,
    int frame_count, const QString& ffmpeg, const QString& ffprobe,
    const std::shared_ptr<std::atomic_bool>& canceled, const std::shared_ptr<GaussianProcessLog>& log,
    bool prefer_sharp_frames) {
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
  const qint64 proxy_bytes = prefer_sharp_frames ? static_cast<qint64>(320)*320*3*frame_count*3 : 0;
  const qint64 estimated_bytes = static_cast<qint64>(maximum_edge) * maximum_edge * 3 * frame_count * 3+proxy_bytes;
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
  const int candidates_per_interval = prefer_sharp_frames ? 3 : 1;
  const auto rate = QString::number(static_cast<double>(frame_count*candidates_per_interval) / duration, 'g', 15);
  const auto prefix = QString("fps=%1:start_time=0:round=near,").arg(rate);
  const auto pixels = [](int edge) { return QString("scale=w='min(%1,iw)':h='min(%1,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2:flags=lanczos,"
      "colorspace=all=bt709:trc=srgb:format=yuv444p,format=rgb24").arg(edge); };
  QString selection;
  QVariantList candidate_records;
  std::vector<int> selected_indices;
  if (prefer_sharp_frames) {
    QTemporaryDir proxies(root.filePath(".video-focus-XXXXXX"));
    if (!proxies.isValid()) return fail("Cannot prepare video focus samples.");
    const auto extraction = run_gaussian_process(ffmpeg, {"-hide_banner", "-loglevel", "error", "-nostdin", "-n",
        "-threads", "2", "-protocol_whitelist", "file,pipe", "-i", source, "-map", "0:v:0", "-an", "-sn", "-dn", "-filter_threads", "2",
        "-vf", prefix+pixels(320), "-frames:v", QString::number(frame_count*3), "-threads", "2", "-compression_level", "3",
        "-fps_mode", "passthrough", QDir(proxies.path()).filePath("frame_%06d.png")},
        project,QProcessEnvironment::systemEnvironment(),canceled,log);
    if (canceled->load()) return fail("Video focus selection canceled; original is unchanged.");
    if (extraction.value("exitCode").toInt() != 0 || extraction.value("failedToStart").toBool()) return fail("Cannot analyze video focus. "+log->tail.right(2048));
    const auto candidates = QDir(proxies.path()).entryInfoList({"frame_*.png"},QDir::Files,QDir::Name);
    // Do not silently drop an interval or train on an incomplete selection.
    if (candidates.size() != frame_count*3) return fail("Video did not yield all focus intervals. Choose fewer frames or use uniform sampling.");
    QStringList expressions;
    for (int interval = 0; interval < frame_count; ++interval) {
      int best = interval*3; double best_score = -1;
      for (int offset = 0; offset < 3; ++offset) {
        if (canceled->load()) return fail("Video focus selection canceled; original is unchanged.");
        const int index = interval*3+offset; const auto& frame = candidates[index];
        if (frame.isSymLink() || !frame.isFile()) return fail("Invalid video focus sample.");
        const auto score = focus_score(frame.absoluteFilePath());
        if (score < 0 || !std::isfinite(score)) return fail("Cannot read a video focus sample.");
        QFile bytes(frame.absoluteFilePath()); if (!bytes.open(QIODevice::ReadOnly)) return fail("Cannot read video focus provenance.");
        candidate_records.append(QVariantMap{{"interval",interval},{"candidateIndex",index},{"sampleTimeSeconds",static_cast<double>(index)*duration/(frame_count*3)},
            {"focusScore",score},{"proxySha256",QString::fromLatin1(QCryptographicHash::hash(bytes.readAll(),QCryptographicHash::Sha256).toHex())}});
        if (score > best_score) { best = index; best_score = score; }
      }
      selected_indices.push_back(best); expressions.append(QString("eq(n,%1)").arg(best));
    }
    selection = "select='"+expressions.join('+')+"',";
  }
  const auto filter = prefix+selection+pixels(maximum_edge);
  QStringList output_options;
  if (prefer_sharp_frames) output_options = {"-fps_mode","passthrough"};
  QStringList extraction_args{"-hide_banner", "-loglevel", "error", "-nostdin", "-n",
      "-threads", "2", "-protocol_whitelist", "file,pipe", "-i", source, "-map", "0:v:0", "-an", "-sn", "-dn", "-filter_threads", "2",
      "-vf", filter, "-frames:v", QString::number(frame_count), "-threads", "2", "-compression_level", "3",
      "-progress", "pipe:1", "-nostats"};
  extraction_args.append(output_options); extraction_args.append(QDir(frames_dir).filePath("frame_%06d.png"));
  const auto extraction = run_gaussian_process(ffmpeg, extraction_args,
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
    if (index >= frame_count) return fail("Video produced more frames than requested.");
    frames.append(frame.canonicalFilePath());
    const auto selected = prefer_sharp_frames ? selected_indices[static_cast<size_t>(index)] : index;
    QVariantMap sample{{"path", "video-frames/" + frame.fileName()},
        {"sampleTimeSeconds", static_cast<double>(selected) * duration / (frame_count*candidates_per_interval)}};
    if (prefer_sharp_frames) {
      sample.insert("candidateIndex",selected); sample.insert("interval",index);
      sample.insert("focusScore",candidate_records[selected].toMap().value("focusScore"));
      QFile bytes(frame.absoluteFilePath()); if (!bytes.open(QIODevice::ReadOnly)) return fail("Cannot hash selected video frame.");
      sample.insert("frameSha256",QString::fromLatin1(QCryptographicHash::hash(bytes.readAll(),QCryptographicHash::Sha256).toHex()));
    }
    samples.append(sample);
  }
  if (frames.size() < 8 || frames.size() > frame_count) return fail("The clip did not yield enough frames for reconstruction.");
  if (prefer_sharp_frames && frames.size() != frame_count) return fail("Selected video frames are incomplete; reconstruction was not started.");
  QVariantMap result{{"ok", true}, {"schema", "framehouse-video-capture-v1"}, {"source", source},
      {"sourceSha256", QString::fromLatin1(hash.result().toHex())}, {"originalsModified", false},
      {"durationSeconds", duration}, {"sourceWidth", width}, {"sourceHeight", height},
      {"sourceMetadata", stream}, {"frames", frames}, {"frameCount", frames.size()}, {"samples", samples},
      {"selection", "uniform-time-sampling-v1; nearest decoded frame, not blur-ranked or exact source PTS"},
      {"colorContract", "tagged-SDR-Rec709-to-sRGB; no HDR tonemapping; display-referred"},
      {"ffmpegFilter", filter}, {"rotation", "ffmpeg-autorotate"}};
  if (prefer_sharp_frames) {
    result.insert("schema","framehouse-video-capture-v2");
    result.insert("selection","sharpest-of-three-per-interval-v1; nearest decoded candidates, not exact source PTS");
    result.insert("focusMeasure","sRGB-luma-Laplacian-variance-max320-v1");
    result.insert("focusCandidates",candidate_records); result.insert("candidateCount",candidate_records.size());
    result.insert("selectionScope","One frame per time interval; focus ranking does not certify capture coverage, static geometry, alignment or reconstruction quality.");
  }
  QSaveFile receipt(root.filePath("video-capture.json"));
  const auto bytes = QJsonDocument::fromVariant(result).toJson();
  if (!receipt.open(QIODevice::WriteOnly) || receipt.write(bytes) != bytes.size() || !receipt.commit())
    return fail("Cannot save video capture provenance.");
  return result;
}
}

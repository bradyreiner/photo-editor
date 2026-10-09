#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <iostream>
#include "timeline_document_controller.h"

namespace photo_editor::desktop {
struct TimelineHistoryTestAccess final {
  static QVariantMap seedTrackAndCutout(TimelineDocumentController& c, const QString& directory) {
    auto* source = c.clipAt(c.clipRefAt(0));
    const auto signature = source->source_identity_signature;
    source->source_size = QSize(64, 64);
    source->duration_seconds = source->trim_end_seconds = 1.0;
    source->trim_start_seconds = source->start_seconds = 0.0;
    source->foreground_cutout = {{QStringLiteral("directory"), directory},
        {QStringLiteral("enabled"), true}, {QStringLiteral("sourceSignature"), signature},
        {QStringLiteral("trimStart"), 0.0}, {QStringLiteral("trimEnd"), 1.0},
        {QStringLiteral("fps"), 4.0}, {QStringLiteral("frameCount"), 4}};
    QVariantList samples;
    for (int i = 0; i < 4; ++i) samples.append(QVariantMap{
        {QStringLiteral("sourceSeconds"), i / 4.0}, {QStringLiteral("x"), 0.1 + i * 0.1},
        {QStringLiteral("y"), 0.2}, {QStringLiteral("width"), 0.2}, {QStringLiteral("height"), 0.2},
        {QStringLiteral("confidence"), 0.9}});
    source->motion_tracking = {{QStringLiteral("sourceSignature"), signature},
        {QStringLiteral("trimStart"), 0.0}, {QStringLiteral("trimEnd"), 1.0},
        {QStringLiteral("samples"), samples}};
    auto* target = c.clipAt(c.clipRefAt(1));
    target->start_seconds = 0;
    target->duration_seconds = target->trim_end_seconds = 1;
    target->frame_offset_x = 0.3;
    c.emitAllState();
    return {{QStringLiteral("clipId"), source->clip_id}};
  }
  static QJsonObject document(TimelineDocumentController& c) { return c.documentObject(); }
  static void seedSurface(TimelineDocumentController& c, int count = 4, double fps = 4.0) {
    auto* source = c.clipAt(c.clipRefAt(0));
    source->duration_seconds = source->trim_end_seconds = count / fps;
    QVariantList samples;
    for (int i = 0; i < count; ++i) {
      QVariantList quad;
      for (const auto& p : {QPointF(0, 0), QPointF(1, 0), QPointF(0, 1), QPointF(1, 1)})
        quad.append(QVariantMap{{QStringLiteral("x"), count > 4 ? 0.1 + p.x() * 0.8 + i * 0.0001 : p.x()}, {QStringLiteral("y"), p.y()}});
      samples.append(QVariantMap{{QStringLiteral("sourceSeconds"), i / fps}, {QStringLiteral("quad"), quad},
          {QStringLiteral("confidence"), 1.0}, {QStringLiteral("inliers"), 80}, {QStringLiteral("errorPx"), 0.0}});
    }
    source->motion_tracking.insert(QStringLiteral("surfaceTrack"), QVariantMap{
        {QStringLiteral("sourceSignature"), source->source_identity_signature}, {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("solveId"), QStringLiteral("occlusion-fixture")}, {QStringLiteral("width"), 64}, {QStringLiteral("height"), 64},
        {QStringLiteral("trimStart"), 0.0}, {QStringLiteral("trimEnd"), count / fps}, {QStringLiteral("fps"), fps},
        {QStringLiteral("frameCount"), count}, {QStringLiteral("samples"), samples}});
    c.clipAt(c.clipRefAt(1))->frame_offset_x = 0;
    c.emitAllState();
  }
  static void useGraphicTarget(TimelineDocumentController& c, const QString& path) {
    auto* target = c.clipAt(c.clipRefAt(1));
    target->media_type = QStringLiteral("photo");
    target->file_path = path;
    target->source_size = QSize(64, 64);
    target->frame_mode = QStringLiteral("fit");
    target->output_variant_overrides.clear();
    c.emitAllState();
  }
  static QStringList render(TimelineDocumentController& c, const QString& path, QString* error, double range_end = -1, double range_start = 0) {
    return c.timelineRenderArguments(path, error, false, QStringLiteral("master"), {}, {}, 0, range_end < 0 ? -1 : range_start, range_end);
  }
  static bool hasPerspectiveFilter(const QStringList& arguments) {
    if (arguments.join(' ').contains(QStringLiteral("perspective="))) return true;
    const auto index = arguments.indexOf(QStringLiteral("-filter_complex_script"));
    if (index < 0 || index + 1 >= arguments.size()) return false;
    QFile graph(arguments[index + 1]);
    return graph.open(QIODevice::ReadOnly) && graph.readAll().contains("perspective=");
  }
  static void write(TimelineDocumentController& c, const QString& path) {
    QFile f(path); (void)f.open(QIODevice::WriteOnly); (void)f.write(QJsonDocument(c.documentObject()).toJson());
  }
  static bool preserveScenePreview(TimelineDocumentController& c, const QString& root, const QString& mode = QStringLiteral("sceneCamera")) {
    auto* source = c.clipAt(c.clipRefAt(0));
    auto metadata = source->motion_tracking.value(mode).toMap();
    auto frames = metadata.value(QStringLiteral("previewFrames")).toList();
    const auto directory = QDir(root).filePath(QStringLiteral("preview-frames"));
    if (!QDir().mkpath(directory)) return false;
    for (int index = 0; index < frames.size(); ++index) {
      auto frame = frames[index].toMap();
      const auto path = QDir(directory).filePath(QStringLiteral("%1.png").arg(index, 6, 10, QLatin1Char('0')));
      if (QFileInfo::exists(path) && !QFile::remove(path)) return false;
      if (!QFile::copy(frame.value(QStringLiteral("path")).toString(), path)) return false;
      frame.insert(QStringLiteral("path"), path);
      frames[index] = frame;
    }
    metadata.insert(QStringLiteral("previewFrames"), frames);
    metadata.insert(QStringLiteral("directory"), directory);
    source->motion_tracking.insert(mode, metadata);
    if (!frames.isEmpty()) source->poster_source = QUrl::fromLocalFile(frames.first().toMap().value(QStringLiteral("path")).toString()).toString();
    return !frames.isEmpty();
  }
  static bool preserveRotoscope(TimelineDocumentController& c, const QString& root) {
    auto* source = c.clipAt(c.clipRefAt(0));
    auto metadata = source->foreground_cutout;
    const auto mask_directory = QDir(root).filePath(QStringLiteral("masks"));
    const auto preview_directory = QDir(root).filePath(QStringLiteral("preview-frames"));
    if (!QDir().mkpath(mask_directory) || !QDir().mkpath(preview_directory)) return false;
    for (int index = 0; index < metadata.value(QStringLiteral("frameCount")).toInt(); ++index) {
      const auto name = QStringLiteral("mask_%1.png").arg(index, 6, 10, QLatin1Char('0'));
      const auto destination = QDir(mask_directory).filePath(name);
      if (QFileInfo::exists(destination) && !QFile::remove(destination)) return false;
      if (!QFile::copy(QDir(metadata.value(QStringLiteral("directory")).toString()).filePath(name), destination)) return false;
    }
    auto frames = metadata.value(QStringLiteral("previewFrames")).toList();
    for (int index = 0; index < frames.size(); ++index) {
      auto frame = frames[index].toMap();
      const auto destination = QDir(preview_directory).filePath(QStringLiteral("%1.png").arg(index, 6, 10, QLatin1Char('0')));
      if (QFileInfo::exists(destination) && !QFile::remove(destination)) return false;
      if (!QFile::copy(frame.value(QStringLiteral("path")).toString(), destination)) return false;
      frame.insert(QStringLiteral("path"), destination); frames[index] = frame;
    }
    metadata.insert(QStringLiteral("directory"), mask_directory);
    metadata.insert(QStringLiteral("previewFrames"), frames);
    source->foreground_cutout = metadata;
    c.emitAllState();
    return !frames.isEmpty();
  }
};
}
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir temp;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temp.path().toUtf8());
  qputenv("PHOTO_EDITOR_TEST_DISABLE_MEDIA_HYDRATION", "1");
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  auto check = [](bool ok, const char* message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
  };
  if (!check(temp.isValid() && !ffmpeg.isEmpty(), "Fixture requires a writable temp directory and FFmpeg")) return 1;
  const auto video = temp.filePath(QStringLiteral("source.mp4"));
  QProcess fixture;
  fixture.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
      QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=c=red:s=64x64:r=4:d=50"),
      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video});
  if (!fixture.waitForFinished(10000) || fixture.exitCode() != 0) return 1;
  const auto matte_dir = temp.filePath(QStringLiteral("matte")); QDir().mkpath(matte_dir);
  for (int i = 0; i < 4; ++i) {
    QImage mask(64, 64, QImage::Format_ARGB32); mask.fill(Qt::transparent);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 32; ++x) mask.setPixel(x, y, qRgba(255, 255, 255, 255));
    if (!mask.save(QDir(matte_dir).filePath(QStringLiteral("mask_%1.png").arg(i, 6, 10, QLatin1Char('0'))))) return 1;
  }
  TimelineDocumentController c;
  c.createTimelineWithSettings({{QStringLiteral("width"), 64}, {QStringLiteral("height"), 64},
      {QStringLiteral("frameRate"), 4.0}});
  c.addClipFromGallery(QStringLiteral("video:tracking"), video, QStringLiteral("Tracked source"), 1.0, {});
  c.addTitleClip();
  if (!check(c.clipCount() == 2, "Source/title fixture not created")) return 1;
  const auto source = TimelineHistoryTestAccess::seedTrackAndCutout(c, matte_dir);
  c.selectClip(1);
  if (!check(c.attachMotionTrackToSelectedClip(source.value(QStringLiteral("clipId")).toString()), "Could not attach motion")) return 1;
  const auto keys = c.currentClip().value(QStringLiteral("keyframes")).toList();
  if (!check(keys.size() == 8, "Tracking did not create two editable keys per frame")) return 1;
  if (!check(!c.attachMotionTrackToSelectedClip(source.value(QStringLiteral("clipId")).toString()), "Existing animation was silently overwritten")) return 1;
  c.undo();
  if (!check(c.currentClip().value(QStringLiteral("keyframes")).toList().isEmpty(), "Undo failed to remove tracked animation")) return 1;
  c.redo();
  if (!check(c.currentClip().value(QStringLiteral("keyframes")).toList().size() == 8, "Redo failed to restore tracked animation")) return 1;
  const auto document_path = temp.filePath(QStringLiteral("timeline.json"));
  TimelineHistoryTestAccess::write(c, document_path);
  TimelineDocumentController reopened;
  if (!check(reopened.loadTimeline(document_path), "Could not reopen tracked document")) return 1;
  reopened.selectClip(0);
  if (!check(reopened.currentClip().value(QStringLiteral("foregroundCutoutReady")).toBool(), "Saved matte not ready after reopen")) return 1;
  if (!check(reopened.motionTrackSources().size() == 1, "Saved motion samples did not survive reopen")) { std::cerr << QJsonDocument::fromVariant(reopened.motionTrackSources()).toJson().toStdString(); return 1; }
  if (!check(reopened.setSelectedClipForegroundCutout(false), "Could not disable cutout")) return 1;
  if (!check(!reopened.currentClip().value(QStringLiteral("foregroundCutoutEnabled")).toBool(), "Cutout toggle did not update clip")) return 1;
  reopened.undo();
  if (!check(reopened.currentClip().value(QStringLiteral("foregroundCutoutEnabled")).toBool(), "Cutout toggle undo failed")) return 1;
  // Hide title for a decoded pixel check of the actual matte export.
  reopened.selectClip(1); reopened.removeSelectedClip();
  const auto output = temp.filePath(QStringLiteral("cutout.mp4")); QString error;
  const auto args = TimelineHistoryTestAccess::render(reopened, output, &error);
  if (!check(!args.isEmpty(), error.toStdString().c_str())) return 1;
  if (!check(args.join(' ').contains(QStringLiteral("alphamerge")), "Export omitted foreground matte")) return 1;
  QProcess render; render.start(ffmpeg, args);
  if (!render.waitForFinished(30000) || render.exitCode() != 0) {
    std::cerr << render.readAllStandardError().toStdString() << '\n'; return 1;
  }
  QProcess decode; decode.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-i"), output, QStringLiteral("-frames:v"), QStringLiteral("1"),
      QStringLiteral("-f"), QStringLiteral("image2pipe"), QStringLiteral("-vcodec"), QStringLiteral("png"), QStringLiteral("pipe:1")});
  if (!decode.waitForFinished(10000) || decode.exitCode() != 0) return 1;
  const auto pixels = QImage::fromData(decode.readAllStandardOutput());
  const auto left = pixels.pixelColor(pixels.width() / 4, pixels.height() / 2);
  const auto right = pixels.pixelColor(pixels.width() * 3 / 4, pixels.height() / 2);
  if (!check(left.red() > 150 && right.red() < 60, "Decoded export did not cut out the masked half")) return 1;
  QFile::remove(QDir(matte_dir).filePath(QStringLiteral("mask_000001.png")));
  error.clear();
  if (!check(TimelineHistoryTestAccess::render(reopened, temp.filePath(QStringLiteral("missing.mp4")), &error).isEmpty() &&
      error.contains(QStringLiteral("missing"), Qt::CaseInsensitive), "Export silently accepted an incomplete matte")) return 1;
  if (!QFile::copy(QDir(matte_dir).filePath(QStringLiteral("mask_000000.png")), QDir(matte_dir).filePath(QStringLiteral("mask_000001.png")))) return 1;
  const auto graphic_path = temp.filePath(QStringLiteral("graphic.png"));
  QImage graphic(64, 64, QImage::Format_ARGB32); graphic.fill(QColor(QStringLiteral("#ff00ff")));
  if (!graphic.save(graphic_path)) return 1;
  TimelineDocumentController occlusion;
  occlusion.createTimelineWithSettings({{QStringLiteral("width"), 64}, {QStringLiteral("height"), 64}, {QStringLiteral("frameRate"), 4.0}});
  occlusion.addClipFromGallery(QStringLiteral("video:occlusion"), video, QStringLiteral("Occlusion source"), 1.0, {});
  occlusion.addTitleClip();
  const auto occlusion_source = TimelineHistoryTestAccess::seedTrackAndCutout(occlusion, matte_dir).value(QStringLiteral("clipId")).toString();
  TimelineHistoryTestAccess::seedSurface(occlusion);
  TimelineHistoryTestAccess::useGraphicTarget(occlusion, graphic_path);
  occlusion.selectClip(0); occlusion.setSelectedClipForegroundCutout(false);
  occlusion.selectClip(1);
  if (!check(occlusion.attachSurfaceTrackToSelectedClip(occlusion_source), occlusion.trackingStatus().toStdString().c_str())) return 1;
  if (!check(occlusion.setSelectedSceneAttachmentOcclusion(true), "Could not enable attachment occlusion")) return 1;
  occlusion.undo();
  if (!check(!occlusion.currentClip().value(QStringLiteral("sceneAttachment")).toMap().value(QStringLiteral("occlusionEnabled")).toBool(), "Occlusion undo failed")) return 1;
  occlusion.redo();
  if (!check(occlusion.sceneAttachmentOcclusionFrame(occlusion.currentClip().value(QStringLiteral("clipId")).toString(), 0.25).value(QStringLiteral("ready")).toBool(), "Exact source-time occlusion lookup failed")) return 1;
  const auto occlusion_document = temp.filePath(QStringLiteral("occlusion.json"));
  TimelineHistoryTestAccess::write(occlusion, occlusion_document);
  TimelineDocumentController occlusion_reopened;
  if (!check(occlusion_reopened.loadTimeline(occlusion_document), "Occlusion save/reopen failed")) return 1;
  occlusion_reopened.selectClip(0);
  if (!check(!occlusion_reopened.currentClip().value(QStringLiteral("foregroundCutoutEnabled")).toBool(), "Occlusion unexpectedly cut out the source")) return 1;
  const auto occlusion_output = temp.filePath(QStringLiteral("occlusion.mp4")); error.clear();
  const auto occlusion_args = TimelineHistoryTestAccess::render(occlusion_reopened, occlusion_output, &error);
  if (!check(!occlusion_args.isEmpty(), error.toStdString().c_str())) return 1;
  QProcess occlusion_render; occlusion_render.start(ffmpeg, occlusion_args);
  if (!occlusion_render.waitForFinished(30000) || occlusion_render.exitCode() != 0) {
    std::cerr << occlusion_render.readAllStandardError().toStdString(); return 1;
  }
  QProcess occlusion_decode; occlusion_decode.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), occlusion_output,
      QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-f"), QStringLiteral("image2pipe"), QStringLiteral("-vcodec"), QStringLiteral("png"), QStringLiteral("pipe:1")});
  if (!occlusion_decode.waitForFinished(10000) || occlusion_decode.exitCode() != 0) return 1;
  const auto occluded_pixels = QImage::fromData(occlusion_decode.readAllStandardOutput());
  if (!check(!occluded_pixels.isNull(), "Occlusion export did not decode")) return 1;
  const auto occluded_left = occluded_pixels.pixelColor(16, 32), occluded_right = occluded_pixels.pixelColor(48, 32);
  if (!check(occluded_left.red() > 150 && occluded_left.blue() < 60 && occluded_right.red() > 150 && occluded_right.blue() > 150,
      "Attached graphic did not pass behind foreground matte while source stayed complete")) return 1;
  QFile::remove(QDir(matte_dir).filePath(QStringLiteral("mask_000001.png"))); error.clear();
  if (!check(TimelineHistoryTestAccess::render(occlusion_reopened, temp.filePath(QStringLiteral("missing-occlusion.mp4")), &error).isEmpty(),
      "Occlusion export silently accepted a missing intermediate mask")) return 1;
  // Alternating masks reveal a one-frame timing error that static masks hide.
  for (int i = 0; i < 4; ++i) {
    QImage mask(64, 64, QImage::Format_ARGB32);
    mask.fill(i % 2 == 0 ? Qt::transparent : Qt::white);
    if (!mask.save(QDir(matte_dir).filePath(QStringLiteral("mask_%1.png").arg(i, 6, 10, QLatin1Char('0'))))) return 1;
  }
  for (double rate : {0.5, 1.0, 2.0}) {
    occlusion.selectClip(0); occlusion.setSelectedClipPlaybackRate(rate);
    occlusion.selectClip(1);
    if (!check(occlusion.attachSurfaceTrackToSelectedClip(occlusion_source) && occlusion.setSelectedSceneAttachmentOcclusion(true),
        "Could not reattach the retimed occlusion fixture")) return 1;
    const auto timed_output = temp.filePath(QStringLiteral("timed-%1.mp4").arg(rate));
    const auto timed_args = TimelineHistoryTestAccess::render(occlusion, timed_output, &error, 0.9 / rate, 0.2 / rate);
    if (!check(!timed_args.isEmpty(), error.toStdString().c_str())) return 1;
    QProcess timed_render; timed_render.start(ffmpeg, timed_args);
    if (!timed_render.waitForFinished(30000) || timed_render.exitCode() != 0) {
      std::cerr << timed_render.readAllStandardError().toStdString(); return 1;
    }
    QProcess timed_decode; timed_decode.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-i"), timed_output, QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"), QStringLiteral("pipe:1")});
    if (!timed_decode.waitForFinished(10000) || timed_decode.exitCode() != 0) return 1;
    const auto raw = timed_decode.readAllStandardOutput();
    const auto frames = raw.size() / (64 * 64 * 3);
    if (!check(frames >= 1, "Retimed occlusion output had no frames")) return 1;
    for (qsizetype frame = 0; frame < frames; ++frame) {
      const auto source_seconds = 0.2 + static_cast<double>(frame) / 4.0 * rate;
      if (source_seconds >= 0.9) break;
      const auto index = static_cast<int>(qFloor(source_seconds * 4.0 + 1.0e-7));
      const auto blue = static_cast<unsigned char>(raw.at(frame * 64 * 64 * 3 + (32 * 64 + 16) * 3 + 2));
      if (!check(index % 2 == 0 ? blue > 150 : blue < 60,
          "Fractional-range retimed occlusion export disagreed with preview's source-frame lookup")) return 1;
    }
  }
  occlusion.selectClip(0); occlusion.setSelectedClipPlaybackRate(1.0);
  occlusion.selectClip(1); occlusion.setSelectedSceneAttachmentOcclusion(false);
  TimelineHistoryTestAccess::seedSurface(occlusion, 200);
  if (!check(occlusion.attachSurfaceTrackToSelectedClip(occlusion_source), "Could not attach extended surface track")) return 1;
  const auto extended_output = temp.filePath(QStringLiteral("extended-graph.mp4")); error.clear();
  const auto extended_args = TimelineHistoryTestAccess::render(occlusion, extended_output, &error);
  const auto script_index = extended_args.indexOf(QStringLiteral("-filter_complex_script"));
  if (!check(script_index >= 0 && script_index + 1 < extended_args.size() &&
      QFileInfo(extended_args[script_index + 1]).size() > 32768, "Extended perspective track omitted its durable graph sidecar")) return 1;
  QProcess extended_render; extended_render.start(ffmpeg, extended_args);
  if (!extended_render.waitForFinished(30000) || extended_render.exitCode() != 0) {
    std::cerr << extended_render.readAllStandardError().toStdString(); return 1;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_SURFACE_NATIVE")) {
    TimelineDocumentController surface;
    QVariantMap real_request;
    const auto request_path = qEnvironmentVariable("PHOTO_EDITOR_SURFACE_REQUEST_FIXTURE");
    if (!request_path.isEmpty()) { QFile request_file(request_path); if (!request_file.open(QIODevice::ReadOnly)) return 1; real_request = QJsonDocument::fromJson(request_file.readAll()).object().toVariantMap(); }
    const auto real_times = real_request.value(QStringLiteral("times")).toList();
    const auto fps = real_times.size() > 1 ? 1.0 / (real_times[1].toDouble() - real_times[0].toDouble()) : 8.0;
    const auto trim_start = real_times.isEmpty() ? 0.0 : real_times.first().toDouble();
    const auto trim_end = real_times.isEmpty() ? 2.0 : real_times.last().toDouble() + 1.0 / fps;
    surface.createTimelineWithSettings({{QStringLiteral("width"), real_times.isEmpty() ? 640 : 960}, {QStringLiteral("height"), real_times.isEmpty() ? 480 : 540}, {QStringLiteral("frameRate"), fps}});
    surface.addClipFromGallery(QStringLiteral("video:surface"), qEnvironmentVariable("PHOTO_EDITOR_SURFACE_VIDEO_FIXTURE"), QStringLiteral("Tracked surface"), trim_end, {});
    surface.setCachePressureLevelForTesting(QStringLiteral("normal"));
    surface.setBackgroundWorkIntent(true, false);
    QElapsedTimer hydration; hydration.start();
    while ((hydration.elapsed() < 1000 || surface.mediaAnalysisBusy()) && hydration.elapsed() < 30000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    if (surface.mediaAnalysisBusy()) std::cerr << QJsonDocument::fromVariant(surface.timelineBackgroundJobs()).toJson().toStdString() << QJsonDocument::fromVariant(surface.currentClip()).toJson().toStdString();
    if (!real_times.isEmpty()) { surface.setSelectedTrimStartSeconds(trim_start); surface.setSelectedTrimEndSeconds(trim_end); }
    const auto seed = real_request.isEmpty() ? QVariantMap{{QStringLiteral("x"), .35}, {QStringLiteral("y"), .3}, {QStringLiteral("width"), .28}, {QStringLiteral("height"), .4}} : real_request.value(QStringLiteral("seed")).toMap();
    if (!check(surface.analyzeSelectedClipSurface(seed), surface.trackingStatus().toStdString().c_str())) return 1;
    QElapsedTimer solve; solve.start();
    while (surface.trackingBusy() && solve.elapsed() < 130000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    const auto sources = surface.surfaceTrackSources();
    if (!check(!surface.trackingBusy() && sources.size() == 1, surface.trackingStatus().toStdString().c_str())) return 1;
    const auto source_id = sources.first().toMap().value(QStringLiteral("clipId")).toString();
    surface.addTitleClip();
    surface.updateSelectedTitle({{QStringLiteral("text"), QStringLiteral("ON THE SURFACE")}, {QStringLiteral("color"), QStringLiteral("#ff00ee")}, {QStringLiteral("fontSize"), 72.0}});
    if (!check(surface.attachSurfaceTrackToSelectedClip(source_id), surface.trackingStatus().toStdString().c_str())) return 1;
    if (!check(surface.currentClip().value(QStringLiteral("sceneAttachmentReady")).toBool(), "Surface attachment not ready")) return 1;
    const auto original = surface.currentClip().value(QStringLiteral("sceneAttachment"));
    if (!check(surface.setSelectedSceneAttachmentPlacement({{QStringLiteral("widthScale"), 1.2}, {QStringLiteral("rotationDegrees"), 8.0}}), "Surface placement edit failed")) return 1;
    surface.undo();
    if (!check(surface.currentClip().value(QStringLiteral("sceneAttachment")) == original, "Surface placement undo failed")) return 1;
    surface.redo();
    const auto evidence = qEnvironmentVariable("PHOTO_EDITOR_SURFACE_PROOF_DIR");
    if (!evidence.isEmpty()) {
      if (!check(QDir().mkpath(evidence) && TimelineHistoryTestAccess::preserveScenePreview(surface, evidence, QStringLiteral("surfaceTrack")), "Could not preserve surface preview cache")) return 1;
      const auto output = QDir(evidence).filePath(QStringLiteral("surface-title-placement.mp4"));
      QString error;
      const auto args = TimelineHistoryTestAccess::render(surface, output, &error);
      if (!check(!args.isEmpty() && TimelineHistoryTestAccess::hasPerspectiveFilter(args), error.toStdString().c_str())) return 1;
      QProcess render; render.start(ffmpeg, args);
      if (!render.waitForFinished(60000) || render.exitCode() != 0) { std::cerr << render.readAllStandardError().toStdString(); return 1; }
      const auto attached = QDir(evidence).filePath(QStringLiteral("editable-surface-timeline.json"));
      TimelineHistoryTestAccess::write(surface, attached);
      TimelineDocumentController reopened_surface;
      if (!check(reopened_surface.loadTimeline(attached), "Surface save/reopen failed")) return 1;
      reopened_surface.selectClip(1);
      if (!check(reopened_surface.currentClip().value(QStringLiteral("sceneAttachmentReady")).toBool(), "Reopened surface attachment stale")) return 1;
      if (!check(surface.detachSelectedSceneAttachment(), "Surface detach failed")) return 1;
      TimelineHistoryTestAccess::write(surface, QDir(evidence).filePath(QStringLiteral("native-surface-fixture.json")));
      surface.undo();
      if (!check(surface.currentClip().value(QStringLiteral("sceneAttachmentReady")).toBool(), "Surface detach undo failed")) return 1;
    }
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_SCENE_NATIVE")) {
    const auto scene_video = qEnvironmentVariable("PHOTO_EDITOR_SCENE_VIDEO_FIXTURE");
    TimelineDocumentController scene;
    scene.createTimelineWithSettings({{QStringLiteral("width"), 640}, {QStringLiteral("height"), 480}, {QStringLiteral("frameRate"), 8.0}});
    scene.addClipFromGallery(QStringLiteral("video:scene"), scene_video, QStringLiteral("Parallax scene"), 2.0, {});
    scene.setCachePressureLevelForTesting(QStringLiteral("normal"));
    scene.setBackgroundWorkIntent(true, false);
    QElapsedTimer hydration; hydration.start();
    while ((hydration.elapsed() < 1000 || scene.mediaAnalysisBusy()) && hydration.elapsed() < 30000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    if (!check(scene.analyzeSelectedClipSceneCamera(), scene.trackingStatus().toStdString().c_str())) return 1;
    QElapsedTimer solve; solve.start();
    while (scene.trackingBusy() && solve.elapsed() < 240000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    const auto sources = scene.sceneTrackSources();
    if (!check(!scene.trackingBusy() && sources.size() == 1, scene.trackingStatus().toStdString().c_str())) return 1;
    const auto source_row = sources.first().toMap();
    const auto visible = source_row.value(QStringLiteral("points")).toList();
    QStringList ids;
    for (const auto& desired : {QPointF(0.43, 0.48), QPointF(0.62, 0.48), QPointF(0.43, 0.66)}) {
      double best = 10;
      QString closest;
      for (const auto& value : visible) {
        const auto point = value.toMap(); const auto id = point.value(QStringLiteral("id")).toString();
        const auto dx = point.value(QStringLiteral("x")).toDouble() - desired.x();
        const auto dy = point.value(QStringLiteral("y")).toDouble() - desired.y();
        if (!ids.contains(id) && dx * dx + dy * dy < best) { best = dx * dx + dy * dy; closest = id; }
      }
      ids.append(closest);
    }
    scene.addTitleClip();
    scene.updateSelectedTitle({{QStringLiteral("text"), QStringLiteral("IN THE SCENE")},
        {QStringLiteral("color"), QStringLiteral("#ff00ee")}, {QStringLiteral("fontSize"), 72.0}});
    if (!check(scene.attachSceneTrackToSelectedClip(source_row.value(QStringLiteral("clipId")).toString(), ids), scene.trackingStatus().toStdString().c_str())) return 1;
    if (!check(scene.currentClip().value(QStringLiteral("sceneAttachmentReady")).toBool(), "Scene attachment was not ready")) return 1;
    const auto original_attachment = scene.currentClip().value(QStringLiteral("sceneAttachment"));
    if (!check(scene.setSelectedSceneAttachmentPlacement({{QStringLiteral("widthScale"), 2.0}}), "Plane size editing failed")) return 1;
    scene.undo();
    if (!check(scene.currentClip().value(QStringLiteral("sceneAttachment")) == original_attachment, "Plane edit undo failed")) return 1;
    scene.redo();
    scene.seekSequenceSeconds(0);
    const auto first_quad = scene.currentClip().value(QStringLiteral("sceneAttachmentQuad")).toList();
    scene.seekSequenceSeconds(1.875);
    const auto last_quad = scene.currentClip().value(QStringLiteral("sceneAttachmentQuad")).toList();
    if (!check(first_quad.size() == 4 && last_quad.size() == 4 && first_quad != last_quad, "The 3D plane did not move through the reconstructed camera path")) return 1;
    const auto scene_proof_directory = qEnvironmentVariable("PHOTO_EDITOR_SCENE_PROOF_DIR");
    if (!scene_proof_directory.isEmpty() && !check(TimelineHistoryTestAccess::preserveScenePreview(scene, scene_proof_directory), "Unable to preserve scene preview cache")) return 1;
    const auto preview = scene.scenePreviewFrame();
    if (!check(preview.value(QStringLiteral("sourceSeconds")).toDouble() == 1.875 &&
        QFileInfo::exists(preview.value(QStringLiteral("path")).toString()), "Scene preview source-time lookup failed")) return 1;
    const auto scene_path = temp.filePath(QStringLiteral("scene.json")); TimelineHistoryTestAccess::write(scene, scene_path);
    if (!scene_proof_directory.isEmpty()) {
      QDir().mkpath(scene_proof_directory);
      TimelineHistoryTestAccess::write(scene, QDir(scene_proof_directory).filePath(QStringLiteral("editable-scene-timeline.json")));
    }
    TimelineDocumentController scene_reopened;
    if (!check(scene_reopened.loadTimeline(scene_path), "Scene document reopen failed")) return 1;
    scene_reopened.seekSequenceSeconds(1.875);
    if (!check(scene_reopened.scenePreviewFrame() == preview, "Scene preview cache failed to survive reopen")) return 1;
    scene_reopened.selectClip(1);
    if (!check(scene_reopened.currentClip().value(QStringLiteral("sceneAttachmentReady")).toBool(), "Scene plane failed to survive reopen")) {
      std::cerr << QJsonDocument::fromVariant(scene_reopened.currentClip()).toJson().toStdString();
      return 1;
    }
    const auto scene_output = temp.filePath(QStringLiteral("scene-placement.mp4"));
    const auto scene_args = TimelineHistoryTestAccess::render(scene_reopened, scene_output, &error);
    if (!check(!scene_args.isEmpty() && scene_args.join(' ').contains(QStringLiteral("perspective=")), error.toStdString().c_str())) return 1;
    QProcess scene_render; scene_render.start(ffmpeg, scene_args);
    if (!scene_render.waitForFinished(60000) || scene_render.exitCode() != 0) { std::cerr << scene_render.readAllStandardError().toStdString(); return 1; }
    QVector<double> centroids;
    for (const auto& time : {QStringLiteral("0"), QStringLiteral("1.875")}) {
      QProcess frame_decode;
      frame_decode.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-ss"), time,
          QStringLiteral("-i"), scene_output, QStringLiteral("-frames:v"), QStringLiteral("1"),
          QStringLiteral("-f"), QStringLiteral("image2pipe"), QStringLiteral("-vcodec"), QStringLiteral("png"), QStringLiteral("pipe:1")});
      if (!frame_decode.waitForFinished(10000) || frame_decode.exitCode() != 0) return 1;
      const auto frame = QImage::fromData(frame_decode.readAllStandardOutput());
      if (!check(!frame.isNull(), "Could not decode the scene placement export")) return 1;
      double sum = 0; int pixels_count = 0;
      for (int y = 0; y < frame.height(); ++y) for (int x = 0; x < frame.width(); ++x) {
        const auto color = frame.pixelColor(x, y);
        if (color.red() > 190 && color.green() < 65 && color.blue() > 140) { sum += x; ++pixels_count; }
      }
      if (!check(pixels_count > 40, "Scene-attached magenta title did not appear in the decoded export")) return 1;
      centroids.append(sum / pixels_count);
      const auto proof = qEnvironmentVariable("PHOTO_EDITOR_SCENE_PROOF_DIR");
      if (!proof.isEmpty()) { QDir().mkpath(proof); (void)frame.save(QDir(proof).filePath(QStringLiteral("scene-title-%1.png").arg(time))); }
    }
    if (!check(qAbs(centroids[0] - centroids[1]) > 10, "Exported title did not follow scene camera parallax")) return 1;
    const auto proof = qEnvironmentVariable("PHOTO_EDITOR_SCENE_PROOF_DIR");
    if (!proof.isEmpty()) {
      const auto proof_output = QDir(proof).filePath(QStringLiteral("scene-title-placement.mp4"));
      if (QFileInfo::exists(proof_output) && !QFile::remove(proof_output)) return 1;
      if (!QFile::copy(scene_output, proof_output)) return 1;
      TimelineHistoryTestAccess::write(scene, QDir(proof).filePath(QStringLiteral("editable-scene-timeline.json")));
    }
    if (!check(scene.detachSelectedSceneAttachment(), "Scene detach failed")) return 1;
    scene.undo();
    if (!check(scene.currentClip().value(QStringLiteral("sceneAttachmentEnabled")).toBool(), "Detach undo failed")) return 1;
    std::cout << "real COLMAP controller solve/3-point plane/edit/undo/reopen/decoded parallax title export passed; title x " << centroids[0] << " -> " << centroids[1] << '\n';
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_SAM2_NATIVE")) {
    const auto portrait = qEnvironmentVariable("PHOTO_EDITOR_TRACKING_FOREGROUND_FIXTURE");
    const auto proof_dir = qEnvironmentVariable("PHOTO_EDITOR_TRACKING_PROOF_DIR");
    if (!proof_dir.isEmpty() && !QDir().mkpath(proof_dir)) return 1;
    const auto portrait_video = proof_dir.isEmpty() ? temp.filePath(QStringLiteral("portrait.mp4"))
        : QDir(proof_dir).filePath(QStringLiteral("portrait-pan-source.mp4"));
    QProcess make_portrait;
    make_portrait.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-loop"), QStringLiteral("1"),
        QStringLiteral("-i"), portrait, QStringLiteral("-vf"), QStringLiteral("scale=450:512,fps=4,crop=410:512:x=8*n:y=0"),
        QStringLiteral("-t"), QStringLiteral("1"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), portrait_video});
    if (!make_portrait.waitForFinished(10000) || make_portrait.exitCode() != 0) return 1;
    TimelineDocumentController temporal;
    temporal.createTimelineWithSettings({{QStringLiteral("width"), 410}, {QStringLiteral("height"), 512}, {QStringLiteral("frameRate"), 4.0}});
    temporal.addClipFromGallery(QStringLiteral("video:portrait"), portrait_video, QStringLiteral("SAM2 portrait"), 1.0, {});
    temporal.setCachePressureLevelForTesting(QStringLiteral("normal"));
    temporal.setBackgroundWorkIntent(true, false);
    QElapsedTimer hydration; hydration.start();
    while ((hydration.elapsed() < 1000 || temporal.mediaAnalysisBusy()) && hydration.elapsed() < 10000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    if (!check(temporal.analyzeSelectedClipCutout(), temporal.trackingStatus().toStdString().c_str())) return 1;
    QElapsedTimer native_cutout; native_cutout.start();
    while (temporal.trackingBusy() && native_cutout.elapsed() < 30000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    if (!check(!temporal.trackingBusy() && temporal.currentClip().value(QStringLiteral("foregroundCutoutReady")).toBool(), temporal.trackingStatus().toStdString().c_str())) return 1;
    const auto native_metadata = temporal.currentClip().value(QStringLiteral("foregroundCutout"));
    if (!check(temporal.analyzeSelectedClipCutout(), "Could not start cancellation proof")) return 1;
    temporal.cancelTrackingAnalysis();
    QElapsedTimer cancelled; cancelled.start();
    while (temporal.trackingBusy() && cancelled.elapsed() < 10000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    if (!check(!temporal.trackingBusy() && temporal.currentClip().value(QStringLiteral("foregroundCutout")) == native_metadata, "Cancelled analysis changed the prior matte")) return 1;
    if (!check(temporal.rotoscopingAvailable(), "Optional SAM2 prerequisites unavailable")) return 1;
    const QVariantMap seed{{QStringLiteral("x"), 0.14}, {QStringLiteral("y"), 0.02}, {QStringLiteral("width"), 0.72}, {QStringLiteral("height"), 0.95}};
    if (!check(temporal.analyzeSelectedClipRotoscope(seed), temporal.trackingStatus().toStdString().c_str())) return 1;
    QElapsedTimer analysis; analysis.start();
    while (temporal.trackingBusy() && analysis.elapsed() < 60000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    if (!check(!temporal.trackingBusy() && temporal.currentClip().value(QStringLiteral("foregroundCutoutReady")).toBool(), temporal.trackingStatus().toStdString().c_str())) return 1;
    if (!check(temporal.currentClip().value(QStringLiteral("foregroundCutout")).toMap().value(QStringLiteral("backend")) == QStringLiteral("sam2-temporal"), temporal.trackingStatus().toStdString().c_str())) return 1;
    temporal.seekSequenceSeconds(0.5);
    const auto correction_frame = temporal.rotoscopeCorrectionFrame();
    if (!check(correction_frame.value(QStringLiteral("success")).toBool() && correction_frame.value(QStringLiteral("frameIndex")).toInt() == 2 &&
        correction_frame.value(QStringLiteral("sourceSeconds")).toDouble() == 0.5, "Rotoscope correction did not use the exact decoded source frame")) return 1;
    const auto before_correction = temporal.currentClip().value(QStringLiteral("foregroundCutout"));
    if (!check(temporal.correctSelectedRotoscope(QVariantList{
        QVariantMap{{QStringLiteral("x"), 0.50}, {QStringLiteral("y"), 0.25}, {QStringLiteral("label"), 1}},
        QVariantMap{{QStringLiteral("x"), 0.48}, {QStringLiteral("y"), 0.65}, {QStringLiteral("label"), 1}},
        QVariantMap{{QStringLiteral("x"), 0.04}, {QStringLiteral("y"), 0.04}, {QStringLiteral("label"), 0}}}), "Could not start corrected SAM2 propagation")) return 1;
    QElapsedTimer correction_wait; correction_wait.start();
    while (temporal.trackingBusy() && correction_wait.elapsed() < 60000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    if (!check(!temporal.trackingBusy() && temporal.currentClip().value(QStringLiteral("foregroundCutoutReady")).toBool() &&
        temporal.currentClip().value(QStringLiteral("foregroundCutout")).toMap().value(QStringLiteral("seed")).toMap().value(QStringLiteral("correctionPrompts")).toList().size() == 1,
        temporal.trackingStatus().toStdString().c_str())) return 1;
    temporal.undo();
    if (!check(temporal.currentClip().value(QStringLiteral("foregroundCutout")) == before_correction, "Rotoscope correction undo did not restore the prior matte")) return 1;
    temporal.redo();
    if (!proof_dir.isEmpty()) {
      QDir().mkpath(proof_dir);
      const auto masks = temporal.currentClip().value(QStringLiteral("foregroundCutout")).toMap().value(QStringLiteral("directory")).toString();
      for (int index : {0, 3}) {
        const auto name = QStringLiteral("mask_%1.png").arg(index, 6, 10, QLatin1Char('0'));
        if (!check(QFile::copy(QDir(masks).filePath(name), QDir(proof_dir).filePath(name)), "Could not preserve model proof masks")) return 1;
      }
      if (!check(TimelineHistoryTestAccess::preserveRotoscope(temporal, proof_dir), "Could not preserve editable model matte and preview frames")) return 1;
      temporal.setSelectedClipForegroundCutout(false);
      TimelineHistoryTestAccess::write(temporal, QDir(proof_dir).filePath(QStringLiteral("editable-roto-timeline.json")));
      temporal.setSelectedClipForegroundCutout(true);
    }
    const auto temporal_path = temp.filePath(QStringLiteral("temporal.json")); TimelineHistoryTestAccess::write(temporal, temporal_path);
    TimelineDocumentController temporal_reopened;
    if (!check(temporal_reopened.loadTimeline(temporal_path), "Temporal project reopen failed")) return 1;
    temporal_reopened.selectClip(0);
    if (!check(temporal_reopened.currentClip().value(QStringLiteral("foregroundCutoutReady")).toBool(), "Temporal matte failed to survive reopen")) return 1;
    const auto temporal_output = temp.filePath(QStringLiteral("temporal.mp4"));
    const auto temporal_args = TimelineHistoryTestAccess::render(temporal_reopened, temporal_output, &error);
    if (!check(!temporal_args.isEmpty(), error.toStdString().c_str())) return 1;
    QProcess temporal_render; temporal_render.start(ffmpeg, temporal_args);
    if (!temporal_render.waitForFinished(30000) || temporal_render.exitCode() != 0) {
      std::cerr << temporal_render.readAllStandardError().toStdString(); return 1;
    }
    if (!proof_dir.isEmpty() && !check(QFile::copy(temporal_output, QDir(proof_dir).filePath(QStringLiteral("portrait-pan-cutout.mp4"))), "Could not preserve model render")) return 1;
    std::cout << "real SAM2 controller analysis/reopen/export passed in " << analysis.elapsed() << " ms\n";
  }
  std::cout << "tracking attach/undo/redo/reopen and decoded matte export passed\n";
  return 0;
}

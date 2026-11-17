#include "app_bootstrap.h"
#include "native_window_test_input.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"
#include "timeline_memory_autorelease_pool.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPointer>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVector>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <mach/mach.h>
#include <mach/task_info.h>
#include <malloc/malloc.h>

namespace photo_editor::desktop {
struct TimelinePlaybackFrameTestAccess final {
  static QString preferredSourceUrl(
      const TimelinePlaybackController& controller) {
    return controller.preferredSourceUrl();
  }

  static QVariantMap nativePlayerDiagnostics(
      const TimelinePlaybackController& controller) {
    const auto sink_frame = controller.video_sink_.videoFrame();
    const auto output_item = controller.video_frame_item_.data();
    return {
        {QStringLiteral("nativeMediaEnabled"), controller.native_media_enabled_},
        {QStringLiteral("playerMediaStatus"), static_cast<int>(controller.player_.mediaStatus())},
        {QStringLiteral("playerError"), static_cast<int>(controller.player_.error())},
        {QStringLiteral("playerErrorString"), controller.player_.errorString()},
        {QStringLiteral("controllerErrorString"), controller.errorString()},
        {QStringLiteral("playerSource"), controller.player_.source().toString()},
        {QStringLiteral("playerPositionMs"), controller.player_.position()},
        {QStringLiteral("playerDurationMs"), controller.player_.duration()},
        {QStringLiteral("playerIsSeekable"), controller.player_.isSeekable()},
        {QStringLiteral("playerPlaybackState"), static_cast<int>(controller.player_.playbackState())},
        {QStringLiteral("sinkFrameValid"), sink_frame.isValid()},
        {QStringLiteral("sinkFrameTimestampUs"), sink_frame.isValid() ? sink_frame.startTime() : -1},
        {QStringLiteral("sinkFrameWidth"), sink_frame.isValid() ? sink_frame.width() : 0},
        {QStringLiteral("sinkFrameHeight"), sink_frame.isValid() ? sink_frame.height() : 0},
        {QStringLiteral("outputItemHasWindow"), output_item != nullptr && output_item->window() != nullptr},
        {QStringLiteral("outputWindowExposed"), output_item != nullptr && output_item->window() != nullptr && output_item->window()->isExposed()},
        {QStringLiteral("outputWindowActive"), output_item != nullptr && output_item->window() != nullptr && output_item->window()->isActive()},
        {QStringLiteral("awaitingMediaSource"), controller.awaiting_media_source_},
        {QStringLiteral("awaitingPositionSync"), controller.awaiting_position_sync_},
        {QStringLiteral("pendingPositionMs"), controller.pending_position_ms_},
        {QStringLiteral("awaitedPositionMs"), controller.awaited_position_ms_},
        {QStringLiteral("nativePrerollActive"), controller.native_paused_preroll_active_},
        {QStringLiteral("nativePrerollStatus"), controller.native_paused_preroll_status_},
        {QStringLiteral("nativePrerollTimeoutCount"), controller.native_paused_preroll_timeout_count_},
    };
  }
};
}  // namespace photo_editor::desktop

namespace {

using photo_editor::desktop::TimelineDocumentController;
using photo_editor::desktop::TimelinePlaybackController;
using photo_editor::desktop::TimelinePlaybackFrameTestAccess;

constexpr int kClipCount = 96;
constexpr int kCycleCount = 12;
constexpr int kPlaybackSliceMs = 4000;
constexpr int kMaxWaitMs = 10000;
constexpr qint64 kFrameTimestampToleranceUs = 100'000;

bool cycle_autorelease_pool_enabled() {
  // Normal UI calls run within a native event's pool. This polling harness
  // invokes media APIs directly between event-pump iterations, so supply the
  // same lifetime boundary. Opt out only to reproduce the old diagnostic.
  return !qEnvironmentVariableIsSet("PHOTO_EDITOR_TIMELINE_DISABLE_CYCLE_AUTORELEASE_POOL");
}

struct CodecFixture final {
  QString name;
  QString filename;
  QString video_encoder;
  QString audio_encoder;
  QString pixel_format;
  QString color;
  int frequency_hz = 440;
  QColor expected_color;
};

struct MetricSummary final {
  double median_ms = 0.0;
  double p95_ms = 0.0;
  double max_ms = 0.0;
};

int fail(const QString& message) {
  std::cerr << "timeline_sustained_media_lifecycle: "
            << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

void pump_events(int milliseconds = 10) {
  QCoreApplication::processEvents(QEventLoop::AllEvents, milliseconds);
  QThread::msleep(2);
}

void wait_ms(int milliseconds) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < milliseconds) {
    pump_events();
  }
}

void drain_deferred_delete_events() {
  // processEvents() in this harness runs inside a local polling loop rather
  // than QApplication::exec(). Qt leaves DeferredDelete events queued in that
  // pattern, so deliver them explicitly and repeat in case destructors enqueue
  // additional deferred deletes.
  for (int pass = 0; pass < 3; ++pass) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

std::optional<quint64> resident_bytes() {
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  const auto result = task_info(
      mach_task_self(),
      MACH_TASK_BASIC_INFO,
      reinterpret_cast<task_info_t>(&info),
      &count);
  if (result != KERN_SUCCESS) {
    return std::nullopt;
  }
  return static_cast<quint64>(info.resident_size);
}

QJsonObject memory_diagnostics() {
  malloc_statistics_t allocation{};
  // nullptr sums all zones, including the nano allocator. Reserved heap pages
  // must not be mistaken for allocations still owned by application objects.
  malloc_zone_statistics(nullptr, &allocation);
  QJsonObject value{
      {QStringLiteral("mallocLiveBytes"), static_cast<qint64>(allocation.size_in_use)},
      {QStringLiteral("mallocReservedBytes"), static_cast<qint64>(allocation.size_allocated)},
      {QStringLiteral("mallocLiveBlocks"), static_cast<qint64>(allocation.blocks_in_use)},
  };
  task_vm_info_data_t vm{};
  mach_msg_type_number_t vm_count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&vm), &vm_count) == KERN_SUCCESS) {
    value.insert(QStringLiteral("physicalFootprintBytes"), static_cast<qint64>(vm.phys_footprint));
    value.insert(QStringLiteral("vmInternalBytes"), static_cast<qint64>(vm.internal));
    value.insert(QStringLiteral("vmExternalBytes"), static_cast<qint64>(vm.external));
    value.insert(QStringLiteral("vmReusableBytes"), static_cast<qint64>(vm.reusable));
    value.insert(QStringLiteral("vmCompressedBytes"), static_cast<qint64>(vm.compressed));
    value.insert(QStringLiteral("vmMediaFootprintBytes"), static_cast<qint64>(vm.ledger_tag_media_footprint));
    value.insert(QStringLiteral("vmGraphicsFootprintBytes"), static_cast<qint64>(vm.ledger_tag_graphics_footprint));
  }
  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t thread_count = 0;
  if (task_threads(mach_task_self(), &threads, &thread_count) == KERN_SUCCESS) {
    value.insert(QStringLiteral("threadCount"), static_cast<qint64>(thread_count));
    for (mach_msg_type_number_t index = 0; index < thread_count; ++index) {
      static_cast<void>(mach_port_deallocate(mach_task_self(), threads[index]));
    }
    static_cast<void>(vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
        static_cast<vm_size_t>(thread_count) * sizeof(thread_t)));
  }
  return value;
}

void capture_memory_profile(int cycle) {
  const auto root = QString::fromUtf8(qgetenv("PHOTO_EDITOR_TIMELINE_MEMORY_PROFILE_DIR"));
  if (root.isEmpty() || (cycle != 0 && cycle != 2 && cycle != 4 && cycle != kCycleCount - 1)) return;
  if (!QDir().mkpath(root)) return;
  const auto pid = QString::number(QCoreApplication::applicationPid());
  for (const auto& tool : {QStringLiteral("vmmap"), QStringLiteral("heap"), QStringLiteral("sample")}) {
    QProcess process;
    process.setProgram(QStringLiteral("/usr/bin/") + tool);
    if (tool == QStringLiteral("vmmap")) {
      process.setArguments({QStringLiteral("-summary"), pid});
    } else if (tool == QStringLiteral("heap")) {
      process.setArguments({QStringLiteral("--noContent"), QStringLiteral("-s"), QStringLiteral("-z"), pid});
    } else {
      process.setArguments({pid, QStringLiteral("1"), QStringLiteral("10")});
    }
    process.start();
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
      process.kill();
      static_cast<void>(process.waitForFinished(3000));
    }
    QFile output(QDir(root).filePath(QStringLiteral("cycle-%1-%2.txt").arg(cycle).arg(tool)));
    if (output.open(QIODevice::WriteOnly)) {
      output.write(process.readAllStandardOutput());
      output.write(process.readAllStandardError());
      output.write(QByteArrayLiteral("\nexitCode=") + QByteArray::number(process.exitCode()) + '\n');
    }
  }
}

MetricSummary summarize(std::vector<double> samples) {
  MetricSummary summary;
  if (samples.empty()) {
    return summary;
  }
  std::sort(samples.begin(), samples.end());
  const auto percentile = [&samples](double fraction) {
    const auto index = static_cast<std::size_t>(std::ceil(
        fraction * static_cast<double>(samples.size()))) - 1U;
    return samples.at(std::min(index, samples.size() - 1U));
  };
  summary.median_ms = percentile(0.50);
  summary.p95_ms = percentile(0.95);
  summary.max_ms = samples.back();
  return summary;
}

bool run_process(
    const QString& executable,
    const QStringList& arguments,
    int timeout_ms,
    QString* error = nullptr) {
  QProcess process;
  process.setProgram(executable);
  process.setArguments(arguments);
  process.start();
  if (!process.waitForStarted(5000) || !process.waitForFinished(timeout_ms)) {
    process.kill();
    process.waitForFinished(3000);
    if (error != nullptr) {
      *error = QStringLiteral("%1 failed to finish: %2")
                   .arg(executable, QString::fromUtf8(process.readAllStandardError()));
    }
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error != nullptr) {
      *error = QStringLiteral("%1 exited %2: %3")
                   .arg(executable)
                   .arg(process.exitCode())
                   .arg(QString::fromUtf8(process.readAllStandardError()));
    }
    return false;
  }
  return true;
}

bool create_fixture_media(const QString& root, QStringList* paths) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return false;
  }
  const QVector<CodecFixture> fixtures{
      {QStringLiteral("h264"), QStringLiteral("source-h264.mp4"),
       QStringLiteral("libx264"), QStringLiteral("aac"),
       QStringLiteral("yuv420p"), QStringLiteral("red"), 440,
       QColor(220, 32, 32)},
      {QStringLiteral("hevc"), QStringLiteral("source-hevc.mp4"),
       QStringLiteral("libx265"), QStringLiteral("aac"),
       QStringLiteral("yuv420p"), QStringLiteral("green"), 660,
       QColor(32, 190, 48)},
      {QStringLiteral("prores"), QStringLiteral("source-prores.mov"),
       QStringLiteral("prores_ks"), QStringLiteral("pcm_s16le"),
       QStringLiteral("yuv422p10le"), QStringLiteral("blue"), 880,
       QColor(32, 64, 220)},
  };
  for (const auto& fixture : fixtures) {
    const auto path = QDir(root).filePath(fixture.filename);
    QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
        QStringLiteral("error"), QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=%1:s=1280x720:r=30:d=3")
            .arg(fixture.color),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("sine=frequency=%1:sample_rate=48000:duration=3")
            .arg(fixture.frequency_hz),
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-map"), QStringLiteral("1:a:0"),
        QStringLiteral("-shortest"), QStringLiteral("-c:v"),
        fixture.video_encoder};
    if (fixture.video_encoder == QStringLiteral("libx264")) {
      args << QStringLiteral("-preset") << QStringLiteral("ultrafast")
           << QStringLiteral("-crf") << QStringLiteral("18");
    } else if (fixture.video_encoder == QStringLiteral("libx265")) {
      args << QStringLiteral("-preset") << QStringLiteral("ultrafast")
           << QStringLiteral("-crf") << QStringLiteral("24")
           << QStringLiteral("-x265-params")
           << QStringLiteral("log-level=error:pools=1:frame-threads=1")
           << QStringLiteral("-tag:v") << QStringLiteral("hvc1");
    } else {
      args << QStringLiteral("-profile:v") << QStringLiteral("0");
    }
    args << QStringLiteral("-pix_fmt") << fixture.pixel_format
         << QStringLiteral("-c:a") << fixture.audio_encoder
         << QStringLiteral("-ar") << QStringLiteral("48000")
         << QStringLiteral("-ac") << QStringLiteral("2");
    if (fixture.filename.endsWith(QStringLiteral(".mp4"))) {
      args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }
    args << path;
    QString error;
    if (!run_process(ffmpeg, args, 60000, &error)) {
      std::cerr << error.toStdString() << '\n';
      return false;
    }
    QFile file(path);
    if (!file.exists() || file.size() < 4096) {
      return false;
    }
    paths->push_back(path);
  }
  return paths->size() == 3;
}

QImage processed_image(TimelinePlaybackController& playback) {
  std::unique_ptr<QQuickImageProvider> provider(playback.createImageProvider());
  if (!provider) {
    return {};
  }
  QSize size;
  return provider->requestImage(QString(), &size, QSize());
}

QColor center_color(TimelinePlaybackController& playback) {
  const auto image = processed_image(playback);
  return image.isNull()
      ? QColor{}
      : image.pixelColor(image.width() / 2, image.height() / 2);
}

int dominant_channel(const QColor& color) {
  if (!color.isValid()) {
    return -1;
  }
  constexpr auto kIdentityMargin = 24;
  if (color.red() > color.green() + kIdentityMargin &&
      color.red() > color.blue() + kIdentityMargin) {
    return 0;
  }
  if (color.green() > color.red() + kIdentityMargin &&
      color.green() > color.blue() + kIdentityMargin) {
    return 1;
  }
  if (color.blue() > color.red() + kIdentityMargin &&
      color.blue() > color.green() + kIdentityMargin) {
    return 2;
  }
  return -1;
}

bool color_matches(const QColor& actual, const QColor& expected) {
  // This only identifies which generated source reached the Program. It is
  // not a pixel-fidelity comparison; codec/output parity has separate gates.
  return dominant_channel(actual) == dominant_channel(expected);
}

bool frame_ready_at_playhead(
    TimelineDocumentController& document,
    TimelinePlaybackController& playback,
    const QString& expected_source,
    const QColor& expected_color,
    bool require_pixel_match) {
  if (playback.activeSourceUrl() != expected_source ||
      !playback.playbackResourceDiagnostics()
           .value(QStringLiteral("playerMediaReady")).toBool() ||
      playback.processedFrameSource().isEmpty() ||
      playback.playbackResourceDiagnostics()
          .value(QStringLiteral("frameProcessingInFlight")).toBool()) {
    return false;
  }
  const auto image = processed_image(playback);
  if (image.isNull() || image.width() < 32 || image.height() < 18) {
    return false;
  }
  const auto program_clip = document.programClip();
  const auto expected_source_seconds =
      program_clip.value(QStringLiteral("trimStartSeconds")).toDouble() +
      qMax(0.0, document.sequencePlayheadSeconds() -
                    program_clip.value(QStringLiteral("startSeconds")).toDouble()) *
          program_clip.value(QStringLiteral("effectivePlaybackRate"), 1.0).toDouble();
  const auto timestamp = playback.processedFrameTimestampUs();
  const auto expected_timestamp_us =
      qRound64(expected_source_seconds * 1'000'000.0);
  const auto timestamp_matches = timestamp >= 0 &&
      qAbs(timestamp - expected_timestamp_us) <= kFrameTimestampToleranceUs;
  const auto color_matches_frame = color_matches(
      image.pixelColor(image.width() / 2, image.height() / 2), expected_color);
  return timestamp_matches && (!require_pixel_match || color_matches_frame);
}

bool wait_for_frame(
    TimelineDocumentController& document,
    TimelinePlaybackController& playback,
    const QString& expected_source,
    const QColor& expected_color,
    bool require_pixel_match = true,
    int timeout_ms = kMaxWaitMs) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < timeout_ms) {
    if (frame_ready_at_playhead(
            document, playback, expected_source, expected_color,
            require_pixel_match)) {
      return true;
    }
    pump_events();
  }
  return frame_ready_at_playhead(
      document, playback, expected_source, expected_color,
      require_pixel_match);
}

bool await_media_analysis(TimelineDocumentController& document, int timeout_ms) {
  QElapsedTimer elapsed;
  elapsed.start();
  while ((document.mediaAnalysisBusy() || document.incrementalLoadBusy()) &&
         elapsed.elapsed() < timeout_ms) {
    pump_events();
  }
  return !document.mediaAnalysisBusy() && !document.incrementalLoadBusy();
}

bool await_proxies(TimelineDocumentController& document, int timeout_ms) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < timeout_ms) {
    auto ready = 0;
    auto terminal = 0;
    for (const auto& value : document.clips()) {
      const auto clip = value.toMap();
      const auto status = clip.value(QStringLiteral("proxyStatus")).toString();
      if (status == QStringLiteral("ready")) {
        ++ready;
      } else if (status == QStringLiteral("failed")) {
        return false;
      }
      if (status == QStringLiteral("ready") || status == QStringLiteral("failed")) {
        ++terminal;
      }
    }
    if (ready >= 3 && terminal == document.clipCount()) {
      return true;
    }
    pump_events();
  }
  return false;
}

quint64 directory_bytes(const QString& path) {
  quint64 bytes = 0U;
  QDirIterator iterator(
      path,
      QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
      QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const auto size = iterator.fileInfo().size();
    if (size > 0) {
      bytes += static_cast<quint64>(size);
    }
  }
  return bytes;
}

void append_sample(
    QFile& artifact,
    int cycle,
    const QString& stage,
    TimelineDocumentController* document,
    TimelinePlaybackController* playback,
    double elapsed_ms,
    const QString& detail = QString()) {
  const auto rss = resident_bytes();
  QJsonObject value{
      {QStringLiteral("schema"), QStringLiteral("timeline-sustained-lifecycle:v1")},
      {QStringLiteral("cycle"), cycle},
      {QStringLiteral("stage"), stage},
      {QStringLiteral("elapsedMs"), elapsed_ms},
      {QStringLiteral("rssBytes"), static_cast<qint64>(rss.value_or(0U))},
      {QStringLiteral("rssAvailable"), rss.has_value()},
      {QStringLiteral("detail"), detail}};
  if (stage.startsWith(QStringLiteral("post-")) ||
      stage.startsWith(QStringLiteral("fixture-")) ||
      stage.endsWith(QStringLiteral("timeout"))) {
    value.insert(QStringLiteral("memory"), memory_diagnostics());
  }
  value.insert(QStringLiteral("cycleAutoreleasePool"),
               cycle_autorelease_pool_enabled());
  if (document != nullptr) {
    value.insert(QStringLiteral("clipCount"), document->clipCount());
    value.insert(QStringLiteral("timelineSeconds"), document->totalDurationSecondsValue());
    value.insert(QStringLiteral("playheadSeconds"), document->sequencePlayheadSeconds());
    value.insert(QStringLiteral("audioTracks"), document->audioTrackCount());
    value.insert(QStringLiteral("renderCacheStatus"), document->renderCacheStatus());
    value.insert(QStringLiteral("renderCacheReadyRanges"), document->renderCacheReadyRangeCount());
    const auto clip = document->programClip();
    value.insert(QStringLiteral("programClipId"), clip.value(QStringLiteral("clipId")).toString());
    value.insert(QStringLiteral("proxyStatus"), clip.value(QStringLiteral("proxyStatus")).toString());
    value.insert(QStringLiteral("proxyEnabled"), clip.value(QStringLiteral("proxyEnabled")).toBool());
    value.insert(QStringLiteral("usingProxy"), clip.value(QStringLiteral("usingProxy")).toBool());
    value.insert(QStringLiteral("programClipMediaType"), clip.value(QStringLiteral("mediaType")).toString());
    value.insert(QStringLiteral("programClipFilePath"), clip.value(QStringLiteral("filePath")).toString());
    value.insert(QStringLiteral("programClipProxyPath"), clip.value(QStringLiteral("proxyPath")).toString());
    value.insert(QStringLiteral("programClipStartSeconds"), clip.value(QStringLiteral("startSeconds")).toDouble());
    value.insert(QStringLiteral("programClipTrimStartSeconds"), clip.value(QStringLiteral("trimStartSeconds")).toDouble());
    const auto program_rate = clip.value(QStringLiteral("effectivePlaybackRate"), 1.0).toDouble();
    const auto expected_source_seconds =
        clip.value(QStringLiteral("trimStartSeconds")).toDouble() +
        qMax(0.0, document->sequencePlayheadSeconds() -
                      clip.value(QStringLiteral("startSeconds")).toDouble()) *
            program_rate;
    value.insert(QStringLiteral("expectedFrameTimestampUs"),
                 qRound64(expected_source_seconds * 1'000'000.0));
    value.insert(QStringLiteral("frameTimestampToleranceUs"),
                 kFrameTimestampToleranceUs);
  }
  if (playback != nullptr) {
    const auto resources = playback->playbackResourceDiagnostics();
    value.insert(QStringLiteral("activeSourceUrl"), playback->activeSourceUrl());
    value.insert(QStringLiteral("processedFrameTimestampUs"), playback->processedFrameTimestampUs());
    value.insert(QStringLiteral("processedFrameSource"), playback->processedFrameSource());
    value.insert(QStringLiteral("droppedFrames"), playback->droppedFrameCount());
    value.insert(QStringLiteral("audioRmsLeft"), playback->audioRmsLeft());
    value.insert(QStringLiteral("audioRmsRight"), playback->audioRmsRight());
    value.insert(QStringLiteral("resources"), QJsonObject::fromVariantMap(resources));
    value.insert(QStringLiteral("nativePlayer"), QJsonObject::fromVariantMap(
        photo_editor::desktop::TimelinePlaybackFrameTestAccess::nativePlayerDiagnostics(*playback)));
    const auto color = center_color(*playback);
    value.insert(QStringLiteral("centerRgb"), QJsonArray{color.red(), color.green(), color.blue()});
    value.insert(QStringLiteral("centerColorIdentity"), dominant_channel(color));
    value.insert(QStringLiteral("centerColorCheck"), QStringLiteral("dominant source channel only; not pixel parity"));
  }
  if (stage == QStringLiteral("fixture-ready") ||
      stage == QStringLiteral("post-close-rss")) {
    const auto cache_path = QString::fromUtf8(qgetenv("PHOTO_EDITOR_CACHE_DIR"));
    value.insert(QStringLiteral("cacheRootBytes"), static_cast<qint64>(directory_bytes(cache_path)));
  }
  if (artifact.isOpen()) {
    artifact.write(QJsonDocument(value).toJson(QJsonDocument::Compact));
    artifact.write("\n");
    artifact.flush();
  }
}

}  // namespace

int main(int argc, char** argv) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    std::cerr << "SKIP: ffmpeg is not installed\n";
    return 77;
  }
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("cocoa"));
  qunsetenv("QT_MEDIA_BACKEND");
  qunsetenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");

  QString artifact_path;
  for (int index = 1; index + 1 < argc; ++index) {
    if (QByteArray(argv[index]) == QByteArrayLiteral("--artifact")) {
      artifact_path = QString::fromLocal8Bit(argv[index + 1]);
      break;
    }
  }
  QFile artifact;
  if (!artifact_path.isEmpty()) {
    if (!QDir().mkpath(QFileInfo(artifact_path).absolutePath())) {
      return fail(QStringLiteral("unable to create artifact directory"));
    }
    artifact.setFileName(artifact_path);
    if (!artifact.open(QIODevice::WriteOnly | QIODevice::Append)) {
      return fail(QStringLiteral("unable to open lifecycle JSONL artifact"));
    }
  }
  const auto initial_rss = resident_bytes();
  if (!initial_rss.has_value()) {
    return fail(QStringLiteral("Mach task RSS sampling is unavailable"));
  }

  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail(QStringLiteral("temporary workspace could not be created"));
  }
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", QDir(workspace.path()).filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", QDir(workspace.path()).filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", QDir(workspace.path()).filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_TIMELINE_DOCUMENTS_DIR", QDir(workspace.path()).filePath(QStringLiteral("documents")).toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgresql://127.0.0.1:1/lifecycle_fixture"));
  qunsetenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("TimelineSustainedMediaLifecycleTest"));

  QStringList source_paths;
  if (!create_fixture_media(workspace.path(), &source_paths)) {
    return fail(QStringLiteral("failed to generate the H.264, HEVC and ProRes fixtures"));
  }
  const QVector<CodecFixture> fixtures{
      {QStringLiteral("h264"), QStringLiteral("source-h264.mp4"),
       QStringLiteral("libx264"), QStringLiteral("aac"), QStringLiteral("yuv420p"),
       QStringLiteral("red"), 440, QColor(220, 32, 32)},
      {QStringLiteral("hevc"), QStringLiteral("source-hevc.mp4"),
       QStringLiteral("libx265"), QStringLiteral("aac"), QStringLiteral("yuv420p"),
       QStringLiteral("green"), 660, QColor(32, 190, 48)},
      {QStringLiteral("prores"), QStringLiteral("source-prores.mov"),
       QStringLiteral("prores_ks"), QStringLiteral("pcm_s16le"), QStringLiteral("yuv422p10le"),
       QStringLiteral("blue"), 880, QColor(32, 64, 220)},
  };
  append_sample(artifact, -1, QStringLiteral("fixture-ready"), nullptr, nullptr, 0.0);

  TimelineDocumentController seed;
  seed.createTimeline(QStringLiteral("Sustained Mixed Codec Lifecycle"));
  for (int index = 0; index < kClipCount; ++index) {
    const auto fixture_index = index % fixtures.size();
    seed.addClipFromGallery(
        QStringLiteral("video:lifecycle-%1").arg(index),
        source_paths.at(fixture_index),
        QStringLiteral("%1 clip %2").arg(fixtures.at(fixture_index).name).arg(index + 1),
        1.0,
        QStringLiteral("poster://lifecycle/%1").arg(index));
  }
  if (seed.clipCount() != kClipCount || seed.totalDurationSecondsValue() < 80.0 ||
      seed.audioTrackCount() < 1) {
    return fail(QStringLiteral("fixture timeline did not build the expected large audio/video sequence"));
  }
  // Keep an authored effect on a later clip so clip 0 can exercise both
  // explicit original and proxy routes during the repeated lifecycle cycles.
  seed.selectClip(3);
  seed.addSelectedClipEffect(QStringLiteral("exposure"));
  seed.setSelectedClipEffectAmount(QStringLiteral("exposure"), 0.18);
  if (!await_media_analysis(seed, 30000) ||
      seed.generateAllClipProxies() != 3 || !await_proxies(seed, 120000)) {
    return fail(QStringLiteral("the three source-revision proxies did not reach ready state"));
  }
  if (!seed.saveTimeline()) {
    return fail(QStringLiteral("fixture timeline could not be saved"));
  }
  const auto document_path = seed.savedDocumentPath();
  const auto total_duration = seed.totalDurationSecondsValue();
  if (document_path.isEmpty() || !QFileInfo::exists(document_path)) {
    return fail(QStringLiteral("saved fixture document path is missing"));
  }
  seed.closeTimeline();
  append_sample(artifact, -1, QStringLiteral("fixture-saved"), nullptr, nullptr, 0.0, document_path);

  QQuickWindow window;
  window.resize(960, 540);
  auto* output = new QQuickItem(window.contentItem());
  output->setWidth(960.0);
  output->setHeight(540.0);
  window.show();
  activate_native_test_window(&window);
  QElapsedTimer exposure;
  exposure.start();
  while (!window.isExposed() && exposure.elapsed() < 10000) {
    pump_events();
  }
  if (!window.isExposed()) {
    std::cerr << "SKIP: Cocoa Timeline output window did not become exposed\n";
    return 77;
  }

  std::vector<double> load_ms;
  std::vector<double> first_frame_ms;
  std::vector<double> scrub_ms;
  std::vector<double> play_slice_ms;
  std::vector<quint64> post_close_rss;
  std::vector<quint64> post_close_deferred_delete_drained_rss;
  QSet<QString> observed_source_routes;
  auto min_audio_rms = std::numeric_limits<double>::max();
  auto max_audio_rms = 0.0;
  bool lifecycle_objects_destroyed = true;
  const auto scrub_targets = QVector<double>{
      0.75, 3.75, 4.25, 11.75, 23.75, 47.75, 71.75, total_duration - 0.25};

  for (int cycle = 0; cycle < kCycleCount; ++cycle) {
    bool cycle_completed = false;
    QPointer<TimelineDocumentController> document_lifetime;
    QPointer<TimelinePlaybackController> playback_lifetime;
    // Construct before the document so this checkpoint runs after its destructor,
    // not merely after closeTimeline() while its instance caches are still alive.
    const auto after_document_destruction = qScopeGuard([&] {
      if (!cycle_completed) return;
      lifecycle_objects_destroyed = lifecycle_objects_destroyed &&
          document_lifetime.isNull() && playback_lifetime.isNull();
      drain_deferred_delete_events();
      append_sample(artifact, cycle, QStringLiteral("post-document-destroyed-rss"),
                    nullptr, nullptr, 0.0,
                    QStringLiteral("documentAndPlaybackDestroyed=%1")
                        .arg(document_lifetime.isNull() && playback_lifetime.isNull()));
      capture_memory_profile(cycle);
    });
    TimelineMemoryAutoreleasePool cycle_pool(cycle_autorelease_pool_enabled());
    TimelineDocumentController document;
    document_lifetime = &document;
    QElapsedTimer load_timer;
    load_timer.start();
    if (!document.loadTimeline(document_path) ||
        !await_media_analysis(document, 30000) ||
        !await_proxies(document, 30000)) {
      return fail(QStringLiteral("cycle %1 could not load and hydrate the saved Timeline")
                      .arg(cycle));
    }
    const auto load_elapsed = static_cast<double>(load_timer.elapsed());
    load_ms.push_back(load_elapsed);
    if (document.clipCount() != kClipCount ||
        document.totalDurationSecondsValue() < 80.0) {
      return fail(QStringLiteral("cycle %1 reopened an incomplete sequence").arg(cycle));
    }

    // Alternate decoded-original and generated-proxy paths in the same process.
    const auto use_proxy = (cycle % 2) == 0;
    const auto first_indices = QVector<int>{0, 1, 2, 3};
    for (const auto index : first_indices) {
      document.selectClip(index);
      const auto clip = document.currentClip();
      const auto proxy_ready = clip.value(QStringLiteral("proxyStatus")).toString() ==
          QStringLiteral("ready");
      const auto proxy_enabled = clip.value(QStringLiteral("proxyEnabled")).toBool();
      if (!proxy_ready || (proxy_enabled != use_proxy && !document.toggleSelectedClipProxy())) {
        return fail(QStringLiteral("cycle %1 could not switch source/proxy playback")
                        .arg(cycle));
      }
    }
    if (!document.saveTimeline()) {
      return fail(QStringLiteral("cycle %1 could not persist proxy selection").arg(cycle));
    }
    document.selectClip(0);
    document.seekSequenceSeconds(0.75);
    const auto first_program_clip = document.programClip();
    if (first_program_clip.value(QStringLiteral("filePath")).toString() !=
        source_paths.at(0)) {
      append_sample(artifact, cycle, QStringLiteral("initial-program-source-mismatch"),
                    &document, nullptr, load_elapsed,
                    QStringLiteral("expectedFile=%1 programFile=%2")
                        .arg(source_paths.at(0),
                             first_program_clip.value(QStringLiteral("filePath")).toString()));
      return fail(QStringLiteral("cycle %1 did not position Program on the first H.264 fixture")
                      .arg(cycle));
    }

    auto playback = std::make_unique<TimelinePlaybackController>(&document);
    playback_lifetime = playback.get();
    playback->setPreviewQualityMode(
        use_proxy ? QStringLiteral("proxy") : QStringLiteral("full"));
    playback->attachVideoOutput(output);
    const auto resource_grace_ms = playback->playbackResourceDiagnostics()
        .value(QStringLiteral("resourceReleaseGraceMs"), 2000).toInt();
    const auto expected_source =
        TimelinePlaybackFrameTestAccess::preferredSourceUrl(*playback);
    QElapsedTimer first_frame_timer;
    first_frame_timer.start();
    if (!wait_for_frame(document, *playback, expected_source,
                        fixtures.at(0).expected_color, true)) {
      const auto image = processed_image(*playback);
      const auto color = image.isNull()
          ? QColor{}
          : image.pixelColor(image.width() / 2, image.height() / 2);
      append_sample(artifact, cycle, QStringLiteral("initial-frame-timeout"),
                    &document, playback.get(), static_cast<double>(first_frame_timer.elapsed()),
                    QStringLiteral("expectedSource=%1 expectedColor=%2,%3,%4 actualColor=%5,%6,%7")
                        .arg(expected_source)
                        .arg(fixtures.at(0).expected_color.red())
                        .arg(fixtures.at(0).expected_color.green())
                        .arg(fixtures.at(0).expected_color.blue())
                        .arg(color.red())
                        .arg(color.green())
                        .arg(color.blue()));
      if (!window.isExposed()) {
        std::cerr << "SKIP: native Timeline window lost exposure during first-frame qualification\n";
        return 77;
      }
      return fail(QStringLiteral("cycle %1 did not decode its initial H.264 frame").arg(cycle));
    }
    first_frame_ms.push_back(static_cast<double>(first_frame_timer.elapsed()));
    const auto proxy_source = document.proxyPlaybackSourceUrl();
    const auto active_source = playback->activeSourceUrl();
    if (!proxy_source.isEmpty() && active_source == proxy_source) {
      observed_source_routes.insert(QStringLiteral("proxy"));
    } else if (active_source == document.originalPlaybackSourceUrl()) {
      observed_source_routes.insert(QStringLiteral("original"));
    }
    const auto expected_route = use_proxy ? QStringLiteral("proxy")
                                         : QStringLiteral("original");
    if (active_source != expected_source ||
        !observed_source_routes.contains(expected_route)) {
      append_sample(artifact, cycle, QStringLiteral("initial-source-route-mismatch"),
                    &document, playback.get(),
                    static_cast<double>(first_frame_timer.elapsed()),
                    QStringLiteral("expectedRoute=%1 expectedSource=%2 activeSource=%3")
                        .arg(expected_route, expected_source, active_source));
      return fail(QStringLiteral("cycle %1 did not exercise its requested %2 source route")
                      .arg(cycle).arg(expected_route));
    }
    append_sample(artifact, cycle, QStringLiteral("loaded-first-frame"),
                  &document, playback.get(), load_elapsed + first_frame_timer.elapsed(),
                  expected_route);

    for (int target_index = 0; target_index < scrub_targets.size(); ++target_index) {
      const auto target = qBound(0.0, scrub_targets.at(target_index), total_duration - 0.05);
      const auto clip_index = static_cast<int>(std::floor(target));
      const auto fixture_index = clip_index % fixtures.size();
      QElapsedTimer scrub_timer;
      scrub_timer.start();
      playback->setInteractionActive(true);
      playback->seekSequenceSeconds(target);
      playback->setInteractionActive(false);
      const auto expected_source_url =
          TimelinePlaybackFrameTestAccess::preferredSourceUrl(*playback);
      if (!wait_for_frame(document, *playback, expected_source_url,
                          fixtures.at(fixture_index).expected_color, true)) {
        append_sample(artifact, cycle, QStringLiteral("scrub-frame-timeout"),
                      &document, playback.get(), static_cast<double>(scrub_timer.elapsed()),
                      QStringLiteral("requestedTarget=%1 expectedSource=%2 codec=%3")
                          .arg(target, 0, 'f', 3).arg(expected_source_url, fixtures.at(fixture_index).name));
        if (!window.isExposed()) {
          std::cerr << "SKIP: native Timeline window lost exposure during scrub qualification\n";
          return 77;
        }
        return fail(QStringLiteral("cycle %1 scrub %2 did not publish the matching decoded codec frame")
                        .arg(cycle).arg(target, 0, 'f', 2));
      }
      if (qAbs(document.sequencePlayheadSeconds() - target) > 0.1) {
        append_sample(artifact, cycle, QStringLiteral("scrub-target-moved"),
                      &document, playback.get(), static_cast<double>(scrub_timer.elapsed()),
                      QStringLiteral("requestedTarget=%1").arg(target, 0, 'f', 3));
        return fail(QStringLiteral("The scrub fixture did not retain its requested target"));
      }
      const auto elapsed = static_cast<double>(scrub_timer.elapsed());
      scrub_ms.push_back(elapsed);
      append_sample(artifact, cycle, QStringLiteral("scrub-ready"),
                    &document, playback.get(), elapsed,
                    QStringLiteral("target=%1 codec=%2")
                        .arg(target, 0, 'f', 2)
                        .arg(fixtures.at(fixture_index).name));
    }

    const auto playback_start_seconds = 44.25;
    playback->seekSequenceSeconds(playback_start_seconds);
    const auto playback_start_clip = static_cast<int>(std::floor(playback_start_seconds));
    const auto playback_start_source =
        TimelinePlaybackFrameTestAccess::preferredSourceUrl(*playback);
    if (!wait_for_frame(document, *playback, playback_start_source,
                        fixtures.at(playback_start_clip % fixtures.size()).expected_color,
                        true)) {
      return fail(QStringLiteral("cycle %1 could not prepare the sustained playback boundary")
                      .arg(cycle));
    }
    const auto start_playhead = document.sequencePlayheadSeconds();
    QElapsedTimer playback_timer;
    playback_timer.start();
    playback->play();
    auto next_sample_ms = 0;
    QSet<int> playback_color_identities;
    while (playback_timer.elapsed() < kPlaybackSliceMs) {
      pump_events();
      if (playback_timer.elapsed() >= next_sample_ms) {
        const auto color = center_color(*playback);
        if (color.isValid()) {
          playback_color_identities.insert(dominant_channel(color));
        }
        const auto live_rms = qMax(playback->audioRmsLeft(), playback->audioRmsRight());
        min_audio_rms = qMin(min_audio_rms, live_rms);
        max_audio_rms = qMax(max_audio_rms, live_rms);
        append_sample(artifact, cycle, QStringLiteral("playing"),
                      &document, playback.get(),
                      static_cast<double>(playback_timer.elapsed()));
        next_sample_ms += 250;
      }
    }
    playback->pause();
    const auto playback_elapsed = static_cast<double>(playback_timer.elapsed());
    play_slice_ms.push_back(playback_elapsed);
    const auto advanced = document.sequencePlayheadSeconds() - start_playhead;
    if (advanced < 2.0 || playback_color_identities.size() < 2) {
      return fail(QStringLiteral("cycle %1 playback did not advance over real clip boundaries")
                      .arg(cycle));
    }
    const auto rms = qMax(playback->audioRmsLeft(), playback->audioRmsRight());
    min_audio_rms = qMin(min_audio_rms, rms);
    max_audio_rms = qMax(max_audio_rms, rms);
    append_sample(artifact, cycle, QStringLiteral("play-slice-complete"),
                  &document, playback.get(), playback_elapsed,
                  QStringLiteral("advanced=%1")
                      .arg(advanced, 0, 'f', 3));

    playback->setWorkspaceActive(false);
    QElapsedTimer suspend_timer;
    suspend_timer.start();
    while (!playback->playbackResourceDiagnostics()
                .value(QStringLiteral("hibernated")).toBool() &&
           suspend_timer.elapsed() < resource_grace_ms + 5000) {
      pump_events();
    }
    const auto suspended = playback->playbackResourceDiagnostics();
    if (!suspended.value(QStringLiteral("hibernated")).toBool() ||
        suspended.value(QStringLiteral("playerSourceLoaded")).toBool() ||
        suspended.value(QStringLiteral("processedFrameImagePresent")).toBool()) {
      return fail(QStringLiteral("cycle %1 retained decoder/frame resources after suspension")
                      .arg(cycle));
    }
    playback->setWorkspaceActive(true);
    const auto resume_seconds = document.sequencePlayheadSeconds();
    const auto resume_clip = static_cast<int>(std::floor(resume_seconds));
    if (!wait_for_frame(
            document, *playback,
            TimelinePlaybackFrameTestAccess::preferredSourceUrl(*playback),
                        fixtures.at(resume_clip % fixtures.size()).expected_color,
                        true)) {
      return fail(QStringLiteral("cycle %1 did not restore decoded media after hibernation")
                      .arg(cycle));
    }
    append_sample(artifact, cycle, QStringLiteral("suspended-closed"),
                  &document, playback.get(),
                  static_cast<double>(suspend_timer.elapsed()));
    // Scope destruction releases the real QMediaPlayer and its native decoder.
    playback->setWorkspaceActive(false);
    playback.reset();
    document.closeTimeline();
    wait_ms(250);
    const auto rss = resident_bytes();
    if (!rss.has_value()) {
      return fail(QStringLiteral("RSS sampling failed after a lifecycle cycle"));
    }
    post_close_rss.push_back(*rss);
    append_sample(artifact, cycle, QStringLiteral("post-close-rss"),
                  nullptr, nullptr, 0.0,
                  QStringLiteral("rssBytes=%1").arg(*rss));

    drain_deferred_delete_events();
    const auto drained_rss = resident_bytes();
    if (!drained_rss.has_value()) {
      return fail(QStringLiteral("RSS sampling failed after DeferredDelete delivery"));
    }
    post_close_deferred_delete_drained_rss.push_back(*drained_rss);
    append_sample(artifact, cycle,
                  QStringLiteral("post-close-deferred-delete-drained-rss"),
                  nullptr, nullptr, 0.0,
                  QStringLiteral("rssBytes=%1").arg(*drained_rss));
    cycle_completed = true;
  }

  const auto scrub_summary = summarize(scrub_ms);
  if (!lifecycle_objects_destroyed) {
    return fail(QStringLiteral("cycle retained document or playback QObject after destruction"));
  }
  const auto load_summary = summarize(load_ms);
  const auto frame_summary = summarize(first_frame_ms);
  const auto play_summary = summarize(play_slice_ms);
  const auto first_rss = post_close_rss.empty() ? 0U : post_close_rss.front();
  const auto last_rss = post_close_rss.empty() ? 0U : post_close_rss.back();
  const auto rss_growth = last_rss >= first_rss ? last_rss - first_rss : 0U;
  const auto first_deferred_delete_drained_rss =
      post_close_deferred_delete_drained_rss.empty()
          ? 0U
          : post_close_deferred_delete_drained_rss.front();
  const auto last_deferred_delete_drained_rss =
      post_close_deferred_delete_drained_rss.empty()
          ? 0U
          : post_close_deferred_delete_drained_rss.back();
  const auto deferred_delete_drained_rss_growth =
      last_deferred_delete_drained_rss >= first_deferred_delete_drained_rss
          ? last_deferred_delete_drained_rss - first_deferred_delete_drained_rss
          : 0U;
  if (!observed_source_routes.contains(QStringLiteral("proxy")) ||
      !observed_source_routes.contains(QStringLiteral("original"))) {
    return fail(QStringLiteral("the sustained lifecycle cycles did not observe both original and proxy routes"));
  }
  QJsonArray observed_route_values;
  for (const auto& route : observed_source_routes) {
    observed_route_values.append(route);
  }
  QJsonObject summary{
      {QStringLiteral("schema"), QStringLiteral("timeline-sustained-lifecycle-summary:v1")},
      {QStringLiteral("cycles"), kCycleCount},
      {QStringLiteral("clips"), kClipCount},
      {QStringLiteral("timelineSeconds"), total_duration},
      {QStringLiteral("codecs"), QJsonArray{QStringLiteral("h264"), QStringLiteral("hevc"), QStringLiteral("prores")} },
      {QStringLiteral("observedSourceRoutes"), observed_route_values},
      {QStringLiteral("rssAfterCloseFirstBytes"), static_cast<qint64>(first_rss)},
      {QStringLiteral("rssAfterCloseLastBytes"), static_cast<qint64>(last_rss)},
      {QStringLiteral("rssAfterCloseGrowthBytes"), static_cast<qint64>(rss_growth)},
      {QStringLiteral("rssAfterDeferredDeleteDrainFirstBytes"),
       static_cast<qint64>(first_deferred_delete_drained_rss)},
      {QStringLiteral("rssAfterDeferredDeleteDrainLastBytes"),
       static_cast<qint64>(last_deferred_delete_drained_rss)},
      {QStringLiteral("rssAfterDeferredDeleteDrainGrowthBytes"),
       static_cast<qint64>(deferred_delete_drained_rss_growth)},
      {QStringLiteral("maxAudioRms"), max_audio_rms},
      {QStringLiteral("minObservedAudioRms"), min_audio_rms},
      {QStringLiteral("loadMedianMs"), load_summary.median_ms},
      {QStringLiteral("loadP95Ms"), load_summary.p95_ms},
      {QStringLiteral("firstFrameMedianMs"), frame_summary.median_ms},
      {QStringLiteral("scrubMedianMs"), scrub_summary.median_ms},
      {QStringLiteral("scrubP95Ms"), scrub_summary.p95_ms},
      {QStringLiteral("scrubMaxMs"), scrub_summary.max_ms},
      {QStringLiteral("playSliceMedianMs"), play_summary.median_ms},
      {QStringLiteral("initialRssBytes"), static_cast<qint64>(*initial_rss)},
  };
  if (artifact.isOpen()) {
    artifact.write(QJsonDocument(summary).toJson(QJsonDocument::Compact));
    artifact.write("\n");
    artifact.flush();
  }
  std::cout << QJsonDocument(summary).toJson(QJsonDocument::Compact).constData() << '\n';
  if (max_audio_rms <= 0.00001) {
    std::cerr << "SKIP: native audio buffers were unavailable during the playback window\n";
    return 77;
  }
  if (post_close_rss.size() != kCycleCount) {
    return fail(QStringLiteral("not all post-close memory samples were collected"));
  }
  if (post_close_deferred_delete_drained_rss.size() != kCycleCount) {
    return fail(QStringLiteral("not all post-close DeferredDelete-drained memory samples were collected"));
  }
  return EXIT_SUCCESS;
}

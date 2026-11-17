// Staged Round 13 diagnostic. The root agent owns CMake integration/builds.
// This never creates a Cocoa window or interacts with the active application.

#include <atomic>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iostream>
#include <mutex>

#include <QApplication>
#include <QAudioOutput>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QTemporaryDir>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtMultimedia/QAudioBufferOutput>

#include <CoreFoundation/CoreFoundation.h>
#include "timeline_memory_autorelease_pool.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

namespace photo_editor::desktop {

struct TimelinePlaybackFrameTestAccess {
  static QVideoSink* sink(TimelinePlaybackController& controller) {
    return &controller.video_sink_;
  }

  static QMediaPlayer* player(TimelinePlaybackController& controller) {
    return &controller.player_;
  }

  static QVariantMap snapshot(const TimelinePlaybackController& controller) {
    const auto frame = controller.video_sink_.videoFrame();
    const auto* item = controller.video_frame_item_.data();
    const auto* window = item == nullptr ? nullptr : item->window();
    return {
        {QStringLiteral("nativeMediaEnabled"), controller.native_media_enabled_},
        {QStringLiteral("mediaStatus"), static_cast<int>(controller.player_.mediaStatus())},
        {QStringLiteral("playerError"), static_cast<int>(controller.player_.error())},
        {QStringLiteral("playerErrorString"), controller.player_.errorString()},
        {QStringLiteral("playerPositionMs"), controller.player_.position()},
        {QStringLiteral("playerDurationMs"), controller.player_.duration()},
        {QStringLiteral("playerSeekable"), controller.player_.isSeekable()},
        {QStringLiteral("playerPlaybackState"), static_cast<int>(controller.player_.playbackState())},
        {QStringLiteral("playerSource"), controller.player_.source().toString()},
        {QStringLiteral("sinkFrameValid"), frame.isValid()},
        {QStringLiteral("sinkFrameTimestampUs"), frame.isValid() ? frame.startTime() : -1},
        {QStringLiteral("sinkFrameWidth"), frame.isValid() ? frame.width() : 0},
        {QStringLiteral("sinkFrameHeight"), frame.isValid() ? frame.height() : 0},
        {QStringLiteral("outputWindowPresent"), window != nullptr},
        {QStringLiteral("outputWindowExposed"), window != nullptr && window->isExposed()},
        {QStringLiteral("prerollActive"), controller.native_paused_preroll_active_},
        {QStringLiteral("prerollFirstFrameSeen"), controller.native_paused_preroll_first_frame_seen_},
        {QStringLiteral("prerollSeekIssued"), controller.native_paused_preroll_seek_issued_},
        {QStringLiteral("prerollSeekCount"), controller.native_paused_preroll_seek_count_},
        {QStringLiteral("prerollTargetMs"), controller.native_paused_preroll_target_ms_},
        {QStringLiteral("prerollAckPositionMs"), controller.native_paused_preroll_ack_position_ms_},
        {QStringLiteral("prerollStartCount"), controller.native_paused_preroll_start_count_},
        {QStringLiteral("prerollTimeoutCount"), controller.native_paused_preroll_timeout_count_},
        {QStringLiteral("prerollStatus"), controller.native_paused_preroll_status_},
        {QStringLiteral("audioOutputDetached"), controller.native_audio_buffer_output_detached_},
        {QStringLiteral("audioOutputSuspended"), controller.audio_output_.outputSuspended()},
        {QStringLiteral("awaitingMediaSource"), controller.awaiting_media_source_},
        {QStringLiteral("awaitingPositionSync"), controller.awaiting_position_sync_},
        {QStringLiteral("awaitedPositionMs"), controller.awaited_position_ms_},
        {QStringLiteral("pendingPositionMs"), controller.pending_position_ms_},
    };
  }
};

}  // namespace photo_editor::desktop

namespace {

using photo_editor::desktop::TimelineDocumentController;
using photo_editor::desktop::TimelinePlaybackController;
using photo_editor::desktop::TimelinePlaybackFrameTestAccess;

constexpr int kFrameWaitMs = 12000;
constexpr int kPrerollWaitMs = 12000;
constexpr int kCodecSwitchCycles = 12;

struct FrameReceipt final {
  std::atomic_int signal_count{0};
  std::atomic_int valid_frame_count{0};
  mutable std::mutex mutex;
  qint64 last_timestamp_us = -1;
  int last_width = 0;
  int last_height = 0;
  int last_pixel_format = -1;
};

int fail(const QString& message) {
  std::cerr << "timeline_prores_windowless_probe: " << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

bool wait_for(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    TimelineMemoryAutoreleasePool event_pool(true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
    if (predicate()) return true;
    QThread::msleep(2);
  }
  TimelineMemoryAutoreleasePool final_event_pool(true);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
  return predicate();
}

bool timeline_derived_work_idle(const TimelineDocumentController& document) {
  const auto readiness = document.agentExportSnapshotReadiness();
  const auto work = readiness.value(QStringLiteral("derivedWork")).toMap();
  return !document.mediaAnalysisBusy() && !document.incrementalLoadBusy() &&
      !work.value(QStringLiteral("sourceIntegrityActive")).toBool() &&
      work.value(QStringLiteral("sourceIntegrityQueueCount")).toInt() == 0 &&
      !work.value(QStringLiteral("waveformActive")).toBool() &&
      work.value(QStringLiteral("waveformQueueCount")).toInt() == 0 &&
      !work.value(QStringLiteral("posterActive")).toBool() &&
      work.value(QStringLiteral("posterQueueCount")).toInt() == 0;
}

bool create_video(
    const QString& ffmpeg,
    const QString& output_path,
    const QString& video_codec,
    const QString& pixel_format,
    const QString& audio_codec,
    const QString& color,
    bool include_audio = true) {
  QProcess process;
  QStringList arguments{
      QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("color=c=%1:s=320x180:r=24:d=5").arg(color)};
  if (include_audio) {
    arguments << QStringLiteral("-f") << QStringLiteral("lavfi")
              << QStringLiteral("-i")
              << QStringLiteral("sine=frequency=440:sample_rate=48000:duration=5");
  }
  arguments << QStringLiteral("-map") << QStringLiteral("0:v:0")
            << QStringLiteral("-c:v") << video_codec
            << QStringLiteral("-pix_fmt") << pixel_format;
  if (include_audio) {
    arguments << QStringLiteral("-map") << QStringLiteral("1:a:0")
              << QStringLiteral("-c:a") << audio_codec
              << QStringLiteral("-shortest");
  }
  arguments << output_path;
  if (video_codec == QStringLiteral("prores_ks")) {
    arguments.insert(arguments.size() - 1, QStringLiteral("-profile:v"));
    arguments.insert(arguments.size() - 1, QStringLiteral("2"));
    arguments.insert(arguments.size() - 1, QStringLiteral("-vendor"));
    arguments.insert(arguments.size() - 1, QStringLiteral("apl0"));
  }
  process.start(ffmpeg, arguments);
  return process.waitForFinished(30000) &&
      process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
      QFileInfo(output_path).isReadable() && QFileInfo(output_path).size() > 0;
}

QJsonArray frame_sample_array(const FrameReceipt& receipt) {
  QJsonArray result;
  std::lock_guard lock(receipt.mutex);
  result.append(QJsonObject{
      {QStringLiteral("signalCount"), receipt.signal_count.load(std::memory_order_relaxed)},
      {QStringLiteral("validFrameCount"), receipt.valid_frame_count.load(std::memory_order_relaxed)},
      {QStringLiteral("lastTimestampUs"), receipt.last_timestamp_us},
      {QStringLiteral("lastWidth"), receipt.last_width},
      {QStringLiteral("lastHeight"), receipt.last_height},
      {QStringLiteral("lastPixelFormat"), receipt.last_pixel_format},
  });
  return result;
}

QJsonObject direct_player_probe(
    const QString& source_path,
    const QString& codec,
    bool attach_then_detach_buffer_output = false) {
  TimelineMemoryAutoreleasePool attempt_pool(true);
  QAudioOutput muted_output;
  QAudioBufferOutput buffer_output;
  QVideoSink sink;
  QMediaPlayer player;
  muted_output.setMuted(true);
  player.setAudioOutput(&muted_output);
  if (attach_then_detach_buffer_output) {
    player.setAudioBufferOutput(&buffer_output);
  }
  player.setVideoSink(&sink);
  FrameReceipt receipt;
  QObject::connect(
      &sink, &QVideoSink::videoFrameChanged, &sink,
      [&receipt](const QVideoFrame& frame) {
        receipt.signal_count.fetch_add(1, std::memory_order_relaxed);
        if (!frame.isValid()) return;
        receipt.valid_frame_count.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard lock(receipt.mutex);
        receipt.last_timestamp_us = frame.startTime();
        receipt.last_width = frame.width();
        receipt.last_height = frame.height();
        receipt.last_pixel_format = static_cast<int>(frame.pixelFormat());
      },
      Qt::DirectConnection);

  QElapsedTimer timer;
  timer.start();
  player.setSource(QUrl::fromLocalFile(source_path));
  const auto loaded = wait_for(
      [&player]() {
        return player.mediaStatus() == QMediaPlayer::LoadedMedia ||
               player.mediaStatus() == QMediaPlayer::BufferedMedia ||
               player.error() != QMediaPlayer::NoError;
      },
      12000);
  const auto load_elapsed_ms = timer.elapsed();
  if (loaded && player.error() == QMediaPlayer::NoError &&
      attach_then_detach_buffer_output) {
    player.setAudioBufferOutput(nullptr);
  }
  if (loaded && player.error() == QMediaPlayer::NoError) player.play();
  const auto frame_received = loaded && player.error() == QMediaPlayer::NoError &&
      wait_for(
          [&receipt]() { return receipt.valid_frame_count.load(std::memory_order_relaxed) > 0; },
          kFrameWaitMs);
  const auto snapshot = QJsonObject{
      {QStringLiteral("route"), QStringLiteral("direct-QMediaPlayer-QVideoSink")},
      {QStringLiteral("codec"), codec},
      {QStringLiteral("audioBufferOutputSequence"),
       attach_then_detach_buffer_output ? QStringLiteral("attached-then-detached-before-play")
                                        : QStringLiteral("unset")},
      {QStringLiteral("source"), QUrl::fromLocalFile(source_path).toString()},
      {QStringLiteral("loaded"), loaded},
      {QStringLiteral("loadElapsedMs"), load_elapsed_ms},
      {QStringLiteral("mediaStatus"), static_cast<int>(player.mediaStatus())},
      {QStringLiteral("error"), static_cast<int>(player.error())},
      {QStringLiteral("errorString"), player.errorString()},
      {QStringLiteral("positionMs"), player.position()},
      {QStringLiteral("durationMs"), player.duration()},
      {QStringLiteral("seekable"), player.isSeekable()},
      {QStringLiteral("frameReceived"), frame_received},
      {QStringLiteral("frameReceipt"), frame_sample_array(receipt).first().toObject()},
  };
  player.stop();
  return snapshot;
}

QJsonObject run_controller_attempt(
    TimelineDocumentController& document,
    TimelinePlaybackController& playback,
    FrameReceipt& receipt,
    int cycle,
    int clip_index,
    double sequence_target_seconds,
    const QString& codec,
    bool output_window_exposed) {
  TimelineMemoryAutoreleasePool attempt_pool(true);
  const auto diagnostics_before = playback.playbackResourceDiagnostics();
  const auto start_count_before = diagnostics_before
      .value(QStringLiteral("nativePrerollStartCount")).toULongLong();
  const auto expected_source_url = QUrl::fromLocalFile(
      document.clips().at(clip_index).toMap()
          .value(QStringLiteral("filePath")).toString()).toString();
  const auto active_source_before = playback.activeSourceUrl();
  const auto active_before = diagnostics_before
      .value(QStringLiteral("nativePrerollActive")).toBool() &&
      active_source_before == expected_source_url;
  document.selectClip(clip_index);
  document.seekSequenceSeconds(sequence_target_seconds);
  const auto callback_count_before = receipt.signal_count.load(std::memory_order_relaxed);
  QElapsedTimer timer;
  timer.start();
  auto diagnostics = diagnostics_before;
  auto first_frame_seen = diagnostics_before
      .value(QStringLiteral("nativePrerollFirstFrameSeen")).toBool();
  auto seek_issued = diagnostics_before
      .value(QStringLiteral("nativePrerollSeekIssued")).toBool();
  auto maximum_seek_count = diagnostics_before
      .value(QStringLiteral("nativePrerollSeekCount")).toInt();
  auto last_target_timestamp_us = diagnostics_before
      .value(QStringLiteral("nativePrerollLastTargetTimestampUs")).toLongLong();
  const auto settled = wait_for(
      [&]() {
        diagnostics = playback.playbackResourceDiagnostics();
        first_frame_seen = first_frame_seen ||
            diagnostics.value(QStringLiteral("nativePrerollFirstFrameSeen")).toBool();
        seek_issued = seek_issued ||
            diagnostics.value(QStringLiteral("nativePrerollSeekIssued")).toBool();
        maximum_seek_count = qMax(
            maximum_seek_count,
            diagnostics.value(QStringLiteral("nativePrerollSeekCount")).toInt());
        last_target_timestamp_us = diagnostics
            .value(QStringLiteral("nativePrerollLastTargetTimestampUs"))
            .toLongLong();
        const auto started = diagnostics.value(QStringLiteral("nativePrerollStartCount"))
                                 .toULongLong() > start_count_before || active_before;
        const auto status = diagnostics.value(QStringLiteral("nativePrerollStatus")).toString();
        const auto terminal = status == QStringLiteral("decoded target frame") ||
                              status == QStringLiteral("timed out");
        return (started && terminal &&
                !diagnostics.value(QStringLiteral("nativePrerollActive")).toBool()) ||
               (!output_window_exposed &&
                status.startsWith(QStringLiteral("skipped:")));
      },
      kPrerollWaitMs);
  diagnostics = playback.playbackResourceDiagnostics();
  const auto callback_count_after = receipt.signal_count.load(std::memory_order_relaxed);
  return {
      {QStringLiteral("route"), QStringLiteral("TimelinePlaybackController-native-preroll")},
      {QStringLiteral("cycle"), cycle},
      {QStringLiteral("codec"), codec},
      {QStringLiteral("expectedSourceUrl"), expected_source_url},
      {QStringLiteral("outputWindowExposed"), output_window_exposed},
      {QStringLiteral("activeSourceUrl"), playback.activeSourceUrl()},
      {QStringLiteral("targetSequenceSeconds"), sequence_target_seconds},
      {QStringLiteral("settledWithinWait"), settled},
      {QStringLiteral("elapsedMs"), timer.elapsed()},
      {QStringLiteral("frameCallbacksBefore"), callback_count_before},
      {QStringLiteral("frameCallbacksAfter"), callback_count_after},
      {QStringLiteral("newFrameCallbacks"), callback_count_after - callback_count_before},
      {QStringLiteral("firstFrameSeenDuringAttempt"), first_frame_seen},
      {QStringLiteral("seekIssuedDuringAttempt"), seek_issued},
      {QStringLiteral("maximumSeekCount"), maximum_seek_count},
      {QStringLiteral("lastTargetTimestampUs"), last_target_timestamp_us},
      {QStringLiteral("frameReceipt"), frame_sample_array(receipt).first().toObject()},
      {QStringLiteral("diagnostics"), QJsonObject::fromVariantMap(diagnostics)},
      {QStringLiteral("rawPlayerState"), QJsonObject::fromVariantMap(TimelinePlaybackFrameTestAccess::snapshot(playback))},
  };
}

void emit_row(const QJsonObject& row) {
  std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("software"));
  qunsetenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");
  TimelineMemoryAutoreleasePool native_pool(true);
  QApplication app(argc, argv);

  QTemporaryDir workspace;
  if (!workspace.isValid()) return fail(QStringLiteral("temporary workspace is unavailable"));
  const auto app_data_root = workspace.filePath(QStringLiteral("app-data"));
  const auto cache_root = workspace.filePath(QStringLiteral("cache"));
  const auto settings_root = workspace.filePath(QStringLiteral("settings"));
  if (!QDir().mkpath(app_data_root) || !QDir().mkpath(cache_root) ||
      !QDir().mkpath(settings_root)) {
    return fail(QStringLiteral("isolated runtime directories could not be created"));
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data_root.toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", app_data_root.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgresql://127.0.0.1:1/looksy"));
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyRound13Lifecycle"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("round13.looksy.test"));
  QCoreApplication::setApplicationName(QStringLiteral("ProResWindowlessProbe"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_root);
  QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings_root);

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
    std::cerr << "SKIP: ffmpeg and ffprobe are required to create the media fixtures\n";
    return 77;
  }
  const auto h264_path = workspace.filePath(QStringLiteral("control-h264.mp4"));
  const auto prores_path = workspace.filePath(QStringLiteral("original-prores.mov"));
  const auto silent_prores_path = workspace.filePath(QStringLiteral("silent-prores.mov"));
  if (!create_video(ffmpeg, h264_path, QStringLiteral("libx264"),
                    QStringLiteral("yuv420p"), QStringLiteral("aac"), QStringLiteral("red")) ||
      !create_video(ffmpeg, prores_path, QStringLiteral("prores_ks"),
                    QStringLiteral("yuv422p10le"), QStringLiteral("pcm_s16le"),
                    QStringLiteral("green")) ||
      !create_video(ffmpeg, silent_prores_path, QStringLiteral("prores_ks"),
                    QStringLiteral("yuv422p10le"), QString{}, QStringLiteral("blue"), false)) {
    return fail(QStringLiteral("ffmpeg could not create the H.264, ProRes audio, and silent ProRes fixtures"));
  }

  TimelineDocumentController document;
  document.createTimeline(QStringLiteral("Windowless ProRes Preroll Probe"));
  document.addClipFromGallery(
      QStringLiteral("video:h264-control"), h264_path, QStringLiteral("H.264 control"),
      5.0, QString{});
  document.addClipFromGallery(
      QStringLiteral("video:original-prores"), prores_path,
      QStringLiteral("Original ProRes"), 5.0, QString{});
  if (document.clipCount() != 2) {
    return fail(QStringLiteral("the H.264-to-ProRes Timeline fixture was not constructed"));
  }
  if (!wait_for([&document]() { return timeline_derived_work_idle(document); }, 30000)) {
    return fail(QStringLiteral("source analysis/integrity/waveform work did not settle before native playback"));
  }

  QQuickWindow output_window;
  output_window.resize(640, 360);
  auto* output_item = new QQuickItem(output_window.contentItem());
  output_item->setWidth(640.0);
  output_item->setHeight(360.0);
  output_window.show();
  const auto exposure_waited = wait_for([&output_window]() { return output_window.isExposed(); }, 3000);
  const auto output_exposed = exposure_waited && output_window.isExposed();
  emit_row({
      {QStringLiteral("phase"), QStringLiteral("offscreen-window-exposure")},
      {QStringLiteral("qpaPlatform"), QGuiApplication::platformName()},
      {QStringLiteral("windowExposed"), output_exposed},
      {QStringLiteral("physicalDisplayClaim"), false},
  });

  FrameReceipt receipt;
  document.selectClip(0);
  document.seekSequenceSeconds(0.75);
  TimelinePlaybackController playback(&document);
  auto* sink = TimelinePlaybackFrameTestAccess::sink(playback);
  const auto connection = QObject::connect(
      sink, &QVideoSink::videoFrameChanged, sink,
      [&receipt](const QVideoFrame& frame) {
        receipt.signal_count.fetch_add(1, std::memory_order_relaxed);
        if (!frame.isValid()) return;
        receipt.valid_frame_count.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard lock(receipt.mutex);
        receipt.last_timestamp_us = frame.startTime();
        receipt.last_width = frame.width();
        receipt.last_height = frame.height();
        receipt.last_pixel_format = static_cast<int>(frame.pixelFormat());
      },
      Qt::DirectConnection);
  if (!connection) return fail(QStringLiteral("the production QVideoSink callback could not be observed"));
  playback.attachVideoOutput(output_item);
  const bool native_ready = wait_for([&]() {
    const auto* player = TimelinePlaybackFrameTestAccess::player(playback);
    const auto status = player->mediaStatus();
    return player->source() == QUrl::fromLocalFile(h264_path) &&
        (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) &&
        player->isSeekable() && player->duration() > 750;
  }, 12000);
  emit_row({{QStringLiteral("phase"), QStringLiteral("native-setup")},
      {QStringLiteral("ready"), native_ready},
      {QStringLiteral("state"), QJsonObject::fromVariantMap(TimelinePlaybackFrameTestAccess::snapshot(playback))}});

  if (output_exposed && native_ready) {
    for (int cycle = 0; cycle < kCodecSwitchCycles; ++cycle) {
      emit_row(run_controller_attempt(document, playback, receipt, cycle, 0, 0.75,
                                      QStringLiteral("h264"), output_exposed));
      emit_row(run_controller_attempt(document, playback, receipt, cycle, 1, 5.75,
                                      QStringLiteral("prores-422-10bit"), output_exposed));
    }
  } else {
    emit_row({
        {QStringLiteral("phase"), QStringLiteral("production-preroll-skipped")},
        {QStringLiteral("reason"), QStringLiteral("offscreen QQuickWindow did not become exposed")},
        {QStringLiteral("productionPathExercised"), false},
    });
  }

  emit_row(direct_player_probe(h264_path, QStringLiteral("h264")));
  emit_row(direct_player_probe(prores_path, QStringLiteral("prores-422-10bit")));
  emit_row(direct_player_probe(prores_path, QStringLiteral("prores-422-10bit"), true));
  emit_row(direct_player_probe(silent_prores_path, QStringLiteral("prores-422-10bit-no-audio")));
  const auto final_controller_state = TimelinePlaybackFrameTestAccess::snapshot(playback);
  const auto final_public_diagnostics = playback.playbackResourceDiagnostics();
  const auto production_preroll_started =
      final_public_diagnostics.value(QStringLiteral("nativePrerollStartCount")).toULongLong() > 0;
  emit_row({
      {QStringLiteral("phase"), QStringLiteral("controller-final-state")},
      {QStringLiteral("productionPathExercised"), production_preroll_started},
      {QStringLiteral("productionPrerollStartCount"),
       final_public_diagnostics.value(QStringLiteral("nativePrerollStartCount")).toLongLong()},
      {QStringLiteral("finalState"), QJsonObject::fromVariantMap(final_controller_state)},
  });

  if (!output_exposed || !production_preroll_started) {
    std::cerr << "SKIP: the offscreen run did not start production paused preroll; direct backend controls were recorded, no production-branch conclusion is available\n";
    return 77;
  }
  return EXIT_SUCCESS;
}

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtMultimedia/QVideoFrame>

#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"
#include "timeline_audio_output.h"
#include "app_bootstrap.h"
#include "native_window_test_input.h"

namespace {

using photo_editor::desktop::TimelineDocumentController;
using photo_editor::desktop::TimelinePlaybackController;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool nearly_equal(double left, double right, double tolerance = 0.0001) {
  return std::abs(left - right) <= tolerance;
}

double stereo_rms(const QVector<float>& samples, int skip_frames = 0) {
  const auto start = qMin(samples.size(), qMax(0, skip_frames * 2));
  auto sum = 0.0;
  auto count = 0;
  for (auto index = start; index < samples.size(); ++index) {
    const auto sample = static_cast<double>(samples.at(index));
    sum += sample * sample;
    ++count;
  }
  return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

double stereo_component_rms(
    const QVector<float>& samples,
    bool side,
    int skip_frames = 0) {
  const auto start_frame = qMin(samples.size() / 2, qMax(0, skip_frames));
  auto sum = 0.0;
  auto count = 0;
  for (auto frame = start_frame; frame * 2 + 1 < samples.size(); ++frame) {
    const auto left = static_cast<double>(samples.at(frame * 2));
    const auto right = static_cast<double>(samples.at(frame * 2 + 1));
    const auto component = side ? (left - right) * 0.5 : (left + right) * 0.5;
    sum += component * component;
    ++count;
  }
  return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

double mid_tone_amplitude(
    const QVector<float>& samples,
    double frequency,
    int skip_frames = 0) {
  constexpr auto kSampleRate = 48000.0;
  const auto start_frame = qMin(samples.size() / 2, qMax(0, skip_frames));
  auto sine_sum = 0.0;
  auto cosine_sum = 0.0;
  auto count = 0;
  for (auto frame = start_frame; frame * 2 + 1 < samples.size(); ++frame) {
    const auto mid = (static_cast<double>(samples.at(frame * 2)) +
                      static_cast<double>(samples.at(frame * 2 + 1))) * 0.5;
    const auto phase = 2.0 * 3.14159265358979323846 * frequency *
                       static_cast<double>(frame) / kSampleRate;
    sine_sum += mid * std::sin(phase);
    cosine_sum += mid * std::cos(phase);
    ++count;
  }
  return count > 0
             ? 2.0 * std::sqrt(sine_sum * sine_sum + cosine_sum * cosine_sum) /
                   static_cast<double>(count)
             : 0.0;
}

QString touch_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return {};
  }
  file.write("placeholder");
  file.close();
  return path;
}

QString create_test_video(
    const QString& path,
    const QString& color,
    double duration_seconds = 0.25) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return touch_file(path);
  }

  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-y"),
          QStringLiteral("-f"),
          QStringLiteral("lavfi"),
          QStringLiteral("-i"),
          QStringLiteral("color=c=%1:s=160x90:r=24:d=%2")
              .arg(color)
              .arg(duration_seconds, 0, 'f', 3),
          QStringLiteral("-an"),
          QStringLiteral("-c:v"),
          QStringLiteral("libx264"),
          QStringLiteral("-pix_fmt"),
          QStringLiteral("yuv420p"),
          path,
      });
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit ||
      process.exitCode() != 0) {
    return {};
  }
  return path;
}

QString create_test_audio(
    const QString& path,
    int frequency_hz,
    double duration_seconds) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return {};
  }
  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("sine=frequency=%1:sample_rate=48000:duration=%2")
              .arg(frequency_hz)
              .arg(duration_seconds, 0, 'f', 3),
          QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path,
      });
  if (!process.waitForFinished(30000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }
  return path;
}

QString create_red_then_blue_test_video(const QString& path) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return {};
  }

  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-y"),
          QStringLiteral("-f"),
          QStringLiteral("lavfi"),
          QStringLiteral("-i"),
          QStringLiteral("color=c=red:s=160x90:r=24:d=0.5"),
          QStringLiteral("-f"),
          QStringLiteral("lavfi"),
          QStringLiteral("-i"),
          QStringLiteral("color=c=blue:s=160x90:r=24:d=1.5"),
          QStringLiteral("-f"),
          QStringLiteral("lavfi"),
          QStringLiteral("-i"),
          QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
          QStringLiteral("-filter_complex"),
          QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0,format=yuv420p[v];[2:a]volume=0.4[a]"),
          QStringLiteral("-map"),
          QStringLiteral("[v]"),
          QStringLiteral("-map"),
          QStringLiteral("[a]"),
          QStringLiteral("-c:v"),
          QStringLiteral("libx264"),
          QStringLiteral("-c:a"),
          QStringLiteral("aac"),
          QStringLiteral("-b:a"),
          QStringLiteral("96k"),
          QStringLiteral("-shortest"),
          QStringLiteral("-pix_fmt"),
          QStringLiteral("yuv420p"),
          QStringLiteral("-g"),
          QStringLiteral("12"),
          path,
      });
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit ||
      process.exitCode() != 0) {
    return {};
  }
  return path;
}

}  // namespace

namespace photo_editor::desktop {

struct TimelinePlaybackFrameTestAccess {
  static bool interactionActive(const TimelinePlaybackController& controller) {
    return controller.interaction_active_;
  }

  static QVector<float> decodedProgramTone(
      TimelinePlaybackController& controller,
      bool render_cache,
      bool proxy) {
    controller.using_render_cache_ = render_cache;
    controller.using_proxy_ = proxy;
    controller.audio_output_.resetForDiscontinuity();
    controller.updateAudioVolume(-1);

    constexpr auto sample_rate = 48000;
    constexpr auto frame_count = 4800;
    constexpr auto pi = 3.14159265358979323846;
    QVector<float> decoded_samples;
    decoded_samples.reserve(frame_count * 2);
    for (auto frame = 0; frame < frame_count; ++frame) {
      const auto sample = static_cast<float>(
          0.20 * std::sin(2.0 * pi * 440.0 * frame / sample_rate));
      decoded_samples.push_back(sample);
      decoded_samples.push_back(sample);
    }
    return controller.audio_output_.processStereoSamplesForTesting(decoded_samples);
  }

  static void retainPreviewResources(TimelinePlaybackController& controller) {
    QImage image(16, 16, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    const auto frame = QVideoFrame(image);
    controller.video_sink_.setVideoFrame(frame);
    controller.latest_pending_frame_ = frame;
    controller.has_pending_video_frame_ = true;
    controller.pending_seek_video_frame_ = frame;
    controller.frame_image_state_->setImage(image);
    controller.processed_frame_available_ = true;
  }

  static void deliverLateDecoderFrame(TimelinePlaybackController& controller) {
    QImage image(16, 16, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    controller.video_sink_.setVideoFrame(QVideoFrame(image));
  }

  static QVariantMap nativePlayerDiagnostics(
      const TimelinePlaybackController& controller) {
    const auto sink_frame = controller.video_sink_.videoFrame();
    const auto output_item = controller.video_frame_item_.data();
    return {
        {QStringLiteral("nativeMediaEnabled"), controller.native_media_enabled_},
        {QStringLiteral("playerMediaStatus"),
         static_cast<int>(controller.player_.mediaStatus())},
        {QStringLiteral("playerError"),
         static_cast<int>(controller.player_.error())},
        {QStringLiteral("playerErrorString"), controller.player_.errorString()},
        {QStringLiteral("controllerErrorString"), controller.errorString()},
        {QStringLiteral("playerSource"), controller.player_.source().toString()},
        {QStringLiteral("playerDurationMs"), controller.player_.duration()},
        {QStringLiteral("playerIsSeekable"), controller.player_.isSeekable()},
        {QStringLiteral("workspaceActive"), controller.workspace_active_},
        {QStringLiteral("hibernated"), controller.playback_hibernated_},
        {QStringLiteral("nativePrerollActive"),
         controller.native_paused_preroll_active_},
        {QStringLiteral("nativePrerollFirstFrameSeen"),
         controller.native_paused_preroll_first_frame_seen_},
        {QStringLiteral("nativePrerollSeekIssued"),
         controller.native_paused_preroll_seek_issued_},
        {QStringLiteral("nativePrerollSeekQueued"),
         controller.native_paused_preroll_seek_queued_},
        {QStringLiteral("nativePrerollRetargetPending"),
         controller.native_paused_preroll_retarget_pending_},
        {QStringLiteral("nativePrerollSeekCount"),
         controller.native_paused_preroll_seek_count_},
        {QStringLiteral("nativePrerollSeekAcknowledged"),
         controller.native_paused_preroll_seek_acknowledged_},
        {QStringLiteral("nativePrerollAckPositionMs"),
         controller.native_paused_preroll_ack_position_ms_},
        {QStringLiteral("nativePrerollFrameGeneration"),
         static_cast<qulonglong>(
             controller.native_paused_preroll_frame_generation_.load(
                 std::memory_order_acquire))},
        {QStringLiteral("nativePrerollPlayPending"),
         controller.native_paused_preroll_play_pending_},
        {QStringLiteral("nativePrerollTargetMs"),
         controller.native_paused_preroll_target_ms_},
        {QStringLiteral("nativePrerollLastTargetTimestampUs"),
         controller.native_paused_preroll_last_target_timestamp_us_},
        {QStringLiteral("nativePrerollStartCount"),
         controller.native_paused_preroll_start_count_},
        {QStringLiteral("nativePrerollTimeoutCount"),
         controller.native_paused_preroll_timeout_count_},
        {QStringLiteral("nativePrerollStatus"),
         controller.native_paused_preroll_status_},
        {QStringLiteral("audioBufferOutputDetached"),
         controller.native_audio_buffer_output_detached_},
        {QStringLiteral("audioOutputSuspended"),
         controller.audio_output_.outputSuspended()},
        {QStringLiteral("awaitingMediaSource"), controller.awaiting_media_source_},
        {QStringLiteral("awaitingPositionSync"), controller.awaiting_position_sync_},
        {QStringLiteral("awaitedPositionMs"), controller.awaited_position_ms_},
        {QStringLiteral("pendingPositionMs"), controller.pending_position_ms_},
        {QStringLiteral("pendingSeekFrameRetained"),
         controller.pending_seek_video_frame_.isValid()},
        {QStringLiteral("pendingSeekFramePtsUs"),
         controller.pending_seek_video_frame_.startTime()},
        {QStringLiteral("sinkFramePtsUs"), sink_frame.startTime()},
        {QStringLiteral("playerPositionMs"), controller.player_.position()},
        {QStringLiteral("outputItemAttached"), output_item != nullptr},
        {QStringLiteral("outputItemHasWindow"),
         output_item != nullptr && output_item->window() != nullptr},
        {QStringLiteral("outputWindowExposed"),
         output_item != nullptr && output_item->window() != nullptr &&
             output_item->window()->isExposed()},
        {QStringLiteral("documentSequencePlaying"),
         !controller.timeline_document_.isNull() &&
             controller.timeline_document_->sequencePlaying()},
        {QStringLiteral("scenePreviewCacheActive"),
         controller.scene_preview_cache_active_},
        {QStringLiteral("frameProcessingActive"), controller.frame_processing_active_},
        {QStringLiteral("frameProcessingWatcherRunning"),
         controller.frame_processing_watcher_.isRunning()},
        {QStringLiteral("frameProcessingMetrics"),
         controller.frame_processing_metrics_},
        {QStringLiteral("colorGradeError"), controller.colorGradeError()},
    };
  }

  static QColor processedFrameCenterColor(
      const TimelinePlaybackController& controller) {
    const auto image = controller.frame_image_state_->requestImage(nullptr, {});
    return image.isNull()
        ? QColor{}
        : image.pixelColor(image.width() / 2, image.height() / 2);
  }
};

}  // namespace photo_editor::desktop

int main(int argc, char** argv) {
  const auto native_cocoa_playback_test =
      argc > 1 &&
      (QByteArray(argv[1]) == QByteArrayLiteral("--native-cocoa") ||
       QByteArray(argv[1]) == QByteArrayLiteral("--native-cocoa-audio"));
  const auto native_cocoa_audio_qualification_test =
      argc > 1 &&
      QByteArray(argv[1]) == QByteArrayLiteral("--native-cocoa-audio");
  auto native_audio_activity_observed = false;
  auto native_audio_qualification_left_rms = 0.0;
  auto native_audio_qualification_right_rms = 0.0;
  if (native_cocoa_playback_test) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("cocoa"));
    qunsetenv("QT_MEDIA_BACKEND");
  } else {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  }
  qputenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS", QByteArrayLiteral("1"));
  QApplication app(argc, argv);

  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("temporary workspace should be created");
  }

  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", workspace.filePath(QStringLiteral("app-data")).toUtf8());

  const auto first_video = create_test_video(
      workspace.filePath(QStringLiteral("playback-01.mp4")), QStringLiteral("red"));
  const auto second_video = create_test_video(
      workspace.filePath(QStringLiteral("playback-02.mp4")), QStringLiteral("blue"));
  if (first_video.isEmpty() || second_video.isEmpty()) {
    return fail("playback placeholder files should be created");
  }

  TimelineDocumentController document;
  document.createTimeline(QStringLiteral("Playback Timeline"));
  document.addClipFromGallery(QStringLiteral("video:1"), first_video, QStringLiteral("First"), 4.0, QStringLiteral("poster://one"));
  document.addClipFromGallery(QStringLiteral("video:2"), second_video, QStringLiteral("Second"), 5.0, QStringLiteral("poster://two"));
  document.selectClip(0);

  TimelinePlaybackController playback(&document);
  if (!playback.hasVideoSource()) {
    return fail("native playback controller should expose the current clip source");
  }

  const auto first_visible_source = playback.activeSourceUrl();
  auto initial_prefetch_plan = playback.prefetchPlan();
  if (initial_prefetch_plan.isEmpty() ||
      initial_prefetch_plan.first().toMap().value(QStringLiteral("reason")).toString() !=
          QStringLiteral("forward-cut") ||
      initial_prefetch_plan.first().toMap().value(QStringLiteral("sourceUrl")).toString() !=
          QUrl::fromLocalFile(second_video).toString() ||
      initial_prefetch_plan.first().toMap().value(QStringLiteral("protectedSourceUrl")).toString() !=
          first_visible_source) {
    return fail("playback prefetch should protect the visible frame while warming the next cut");
  }
  playback.seekSequenceSeconds(0.5);
  playback.play();
  const auto document_before_suspend = document.deterministicDocumentHash();
  playback.setWorkspaceActive(false);
  const auto paused_playhead = document.sequencePlayheadSeconds();
  QEventLoop suspended_loop;
  QTimer::singleShot(40, &suspended_loop, &QEventLoop::quit);
  suspended_loop.exec();
  if (playback.workspaceActive() || document.sequencePlaying() ||
      !nearly_equal(document.sequencePlayheadSeconds(), paused_playhead) ||
      !playback.prefetchPlan().isEmpty() || !playback.prefetchedSourceUrl().isEmpty() ||
      document.deterministicDocumentHash() != document_before_suspend) {
    return fail("Leaving Timeline should pause clocks and prefetch without changing the document");
  }
  playback.play();
  document.playSequence();
  if (document.sequencePlaying())
    return fail("Hidden Timeline must reject playback even through document commands");
  playback.setWorkspaceActive(true);
  QEventLoop resumed_loop;
  QTimer::singleShot(30, &resumed_loop, &QEventLoop::quit);
  resumed_loop.exec();
  if (!playback.workspaceActive() || document.sequencePlaying() ||
      !nearly_equal(document.sequencePlayheadSeconds(), paused_playhead) ||
      playback.activeSourceUrl() != first_visible_source || playback.prefetchPlan().isEmpty()) {
    return fail("Returning to Timeline should retain its source and playhead and stay paused");
  }

  {
    constexpr auto kHeadlessVariable =
        "PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS";
    const auto was_headless_set = qEnvironmentVariableIsSet(kHeadlessVariable);
    const auto previous_headless_value = qgetenv(kHeadlessVariable);
    qunsetenv(kHeadlessVariable);
    [[maybe_unused]] const auto restore_headless = qScopeGuard([&]() {
      if (was_headless_set) {
        qputenv(kHeadlessVariable, previous_headless_value);
      } else {
        qunsetenv(kHeadlessVariable);
      }
    });

    const auto native_first_video = create_red_then_blue_test_video(
        workspace.filePath(QStringLiteral("native-playback-01.mp4")));
    const auto native_second_video = create_test_video(
        workspace.filePath(QStringLiteral("native-playback-02.mp4")),
        QStringLiteral("blue"),
        2.0);
    if (native_first_video.isEmpty() || native_second_video.isEmpty() ||
        QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
      return fail("ffmpeg should create decodable media for the playback lifecycle proof");
    }

    TimelineDocumentController native_document;
    native_document.createTimeline(QStringLiteral("Native Playback Lifecycle"));
    native_document.addClipFromGallery(
        QStringLiteral("video:native-1"),
        native_first_video,
        QStringLiteral("First"),
        4.0,
        QStringLiteral("poster://one"));
    native_document.addClipFromGallery(
        QStringLiteral("video:native-2"),
        native_second_video,
        QStringLiteral("Second"),
        5.0,
        QStringLiteral("poster://two"));
    native_document.selectClip(1);
    native_document.seekSequenceSeconds(0.75);
    const auto pre_settle_document_hash = native_document.deterministicDocumentHash();
    const auto task_clip_state = [&native_document]() {
      QVariantList result;
      for (const auto& value : native_document.clips()) {
        const auto clip = value.toMap();
        result.push_back(QVariantMap{
            {QStringLiteral("clipId"), clip.value(QStringLiteral("clipId"))},
            {QStringLiteral("filePath"), clip.value(QStringLiteral("filePath"))},
            {QStringLiteral("durationSeconds"), clip.value(QStringLiteral("durationSeconds"))},
            {QStringLiteral("durationProvisional"), clip.value(QStringLiteral("durationProvisional"))},
            {QStringLiteral("startSeconds"), clip.value(QStringLiteral("startSeconds"))},
            {QStringLiteral("trimStartSeconds"), clip.value(QStringLiteral("trimStartSeconds"))},
            {QStringLiteral("trimEndSeconds"), clip.value(QStringLiteral("trimEndSeconds"))},
            {QStringLiteral("hasAudio"), clip.value(QStringLiteral("hasAudio"))},
            {QStringLiteral("sourceWidth"), clip.value(QStringLiteral("sourceWidth"))},
            {QStringLiteral("sourceHeight"), clip.value(QStringLiteral("sourceHeight"))},
        });
      }
      return result;
    };
    const auto pre_settle_clip_state = task_clip_state();
    auto preparation = native_document.prepareAgentTaskDocumentForAutomation();
    if (!preparation.value(QStringLiteral("ok")).toBool()) {
      return fail("native task preparation should accept the active Timeline");
    }
    QElapsedTimer task_settle_timer;
    task_settle_timer.start();
    while (!preparation.value(QStringLiteral("ready")).toBool() &&
           task_settle_timer.elapsed() < 5000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(5);
      preparation = native_document.prepareAgentTaskDocumentForAutomation();
    }
    if (!preparation.value(QStringLiteral("ready")).toBool() ||
        native_document.deterministicDocumentHash() != pre_settle_document_hash ||
        task_clip_state() != pre_settle_clip_state) {
      return fail("task preparation should settle cancelable media/cache work without changing timeline content");
    }
    native_document.releaseAgentTaskDocumentForAutomation();
    QQuickWindow native_window;
    QQuickItem headless_native_output;
    QQuickItem* native_output = &headless_native_output;
    if (native_cocoa_playback_test) {
      native_window.resize(640, 360);
      native_output = new QQuickItem(native_window.contentItem());
      native_output->setWidth(640.0);
      native_output->setHeight(360.0);
      native_window.show();
      activate_native_test_window(&native_window);
      QElapsedTimer window_exposure_timer;
      window_exposure_timer.start();
      while (!native_window.isExposed() &&
             window_exposure_timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(10);
      }
      if (!native_window.isExposed()) {
        return fail("the Cocoa native playback proof should expose its video output window before decoding");
      }
    } else {
      native_output->setWidth(640.0);
      native_output->setHeight(360.0);
    }
    TimelinePlaybackController native_playback(&native_document);
    native_playback.attachVideoOutput(native_output);

    const auto source_hydration_settled = [&native_document]() {
      if (!native_document.agentTaskDocumentReady() ||
          native_document.mediaAnalysisBusy() ||
          native_document.incrementalLoadBusy()) {
        return false;
      }
      for (const auto& clip_value : native_document.clips()) {
        const auto clip = clip_value.toMap();
        const auto integrity_status =
            clip.value(QStringLiteral("sourceIntegrityStatus")).toString();
        if (integrity_status.isEmpty() ||
            integrity_status == QStringLiteral("pending") ||
            integrity_status == QStringLiteral("verifying")) {
          return false;
        }
        const auto analysis_status =
            clip.value(QStringLiteral("mediaAnalysisStatus")).toString();
        if (analysis_status == QStringLiteral("queued") ||
            analysis_status == QStringLiteral("analyzing") ||
            analysis_status == QStringLiteral("pending")) {
          return false;
        }
        if (clip.value(QStringLiteral("hasAudio")).toBool() &&
            clip.value(QStringLiteral("waveformSource")).toString().isEmpty()) {
          return false;
        }
      }
      return true;
    };
    QElapsedTimer hydration_timer;
    hydration_timer.start();
    while (!source_hydration_settled() && hydration_timer.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(10);
    }
    if (!source_hydration_settled()) {
      return fail("media analysis and source integrity should settle before baselining the preserved timeline document");
    }

    const auto expected_native_source = QUrl::fromLocalFile(native_first_video).toString();
    const auto initial_resources = native_playback.playbackResourceDiagnostics();
    if (!initial_resources.value(QStringLiteral("playerSourceLoaded")).toBool() ||
        !initial_resources.value(QStringLiteral("prefetchPlayerSourceLoaded")).toBool() ||
        native_playback.activeSourceUrl() != expected_native_source) {
      return fail("native playback should load the Program source and warm the next clip before suspension");
    }

    const auto expected_program_source_seconds = [&native_document]() {
      const auto program_clip = native_document.programClip();
      const auto sequence_offset = qMax(
          0.0,
          native_document.sequencePlayheadSeconds() -
              program_clip.value(QStringLiteral("startSeconds")).toDouble());
      const auto playback_rate = program_clip
                                     .value(QStringLiteral("effectivePlaybackRate"),
                                            program_clip.value(
                                                QStringLiteral("playbackRate"), 1.0))
                                     .toDouble();
      return program_clip.value(QStringLiteral("trimStartSeconds")).toDouble() +
          sequence_offset * playback_rate;
    };
    const auto native_frame_matches_playhead =
        [&expected_program_source_seconds, &native_playback]() {
      const auto resources = native_playback.playbackResourceDiagnostics();
      const auto center_color =
          photo_editor::desktop::TimelinePlaybackFrameTestAccess::
              processedFrameCenterColor(native_playback);
      const auto expected_source_timestamp_us =
          qRound64(expected_program_source_seconds() * 1'000'000.0);
      const auto actual_source_timestamp_us =
          native_playback.processedFrameTimestampUs();
      return resources.value(QStringLiteral("playerMediaReady")).toBool() &&
          resources.value(QStringLiteral("videoSinkFrameRetained")).toBool() &&
          resources.value(QStringLiteral("processedFrameImagePresent")).toBool() &&
          resources.value(QStringLiteral("processedFrameImageBytes")).toULongLong() > 0U &&
          !native_playback.processedFrameSource().isEmpty() &&
          !resources.value(QStringLiteral("frameProcessingInFlight")).toBool() &&
          resources.value(QStringLiteral("audioBufferOutputDetached")).toBool() &&
          resources.value(QStringLiteral("audioOutputSuspended")).toBool() &&
          native_playback.audioRmsLeft() == 0.0 &&
          native_playback.audioRmsRight() == 0.0 &&
          !resources.value(QStringLiteral("nativePrerollActive")).toBool() &&
          center_color.blue() > center_color.red() + 80 &&
          actual_source_timestamp_us >= 0 &&
          qAbs(actual_source_timestamp_us - expected_source_timestamp_us) <= 100'000;
    };
    const auto wait_for_native_frame = [&native_frame_matches_playhead]() {
      QElapsedTimer timer;
      timer.start();
      while (!native_frame_matches_playhead() && timer.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
      }
      return native_frame_matches_playhead();
    };
    const auto log_native_frame_mismatch = [&native_document,
                                            &native_playback,
                                            &expected_program_source_seconds](
                                               const char* phase) {
      const auto program_clip = native_document.programClip();
      const auto resources = native_playback.playbackResourceDiagnostics();
      const auto expected_source_timestamp_us = qRound64(
          expected_program_source_seconds() * 1'000'000.0);
      const auto actual_source_timestamp_us =
          native_playback.processedFrameTimestampUs();
      const auto center_color =
          photo_editor::desktop::TimelinePlaybackFrameTestAccess::
              processedFrameCenterColor(native_playback);
      std::cerr << phase
                << " frame mismatch: programClip="
                << program_clip.value(QStringLiteral("clipId")).toString().toStdString()
                << " expectedSourceSeconds=" << expected_program_source_seconds()
                << " expectedTimestampUs=" << expected_source_timestamp_us
                << " processedTimestampUs=" << actual_source_timestamp_us
                << " deltaUs="
                << (actual_source_timestamp_us - expected_source_timestamp_us)
                << " toleranceUs=100000"
                << " sequencePlayhead=" << native_document.sequencePlayheadSeconds()
                << " selectedClipPlayhead=" << native_document.currentClipPlayheadSeconds()
                << " mediaReady="
                << resources.value(QStringLiteral("playerMediaReady")).toBool()
                << " sinkFrame="
                << resources.value(QStringLiteral("videoSinkFrameRetained")).toBool()
                << " processedImage="
                << resources.value(QStringLiteral("processedFrameImagePresent")).toBool()
                << " centerRgb=" << center_color.red() << ','
                << center_color.green() << ',' << center_color.blue()
                << '\n';
    };
    if (native_cocoa_playback_test && !wait_for_native_frame()) {
      const auto player_diagnostics =
          photo_editor::desktop::TimelinePlaybackFrameTestAccess::nativePlayerDiagnostics(
              native_playback);
      log_native_frame_mismatch("initial native playback");
      std::cerr << "native player diagnostics: "
                << QJsonDocument::fromVariant(player_diagnostics)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("native playback should decode and process a frame at the retained source playhead before suspension");
    }
    if (native_cocoa_playback_test) {
      QEventLoop initial_frame_settle_loop;
      QTimer::singleShot(100, &initial_frame_settle_loop, &QEventLoop::quit);
      initial_frame_settle_loop.exec();
    } else {
      photo_editor::desktop::TimelinePlaybackFrameTestAccess::retainPreviewResources(
          native_playback);
    }

    const auto document_hash_before_suspend =
        native_document.deterministicDocumentHash();
    const auto selected_clips_before_suspend = native_document.selectedClipIds();
    const auto can_undo_before_suspend = native_document.canUndo();
    const auto playhead_before_suspend =
        native_document.sequencePlayheadSeconds();
    native_playback.setWorkspaceActive(false);
    auto grace_resources = native_playback.playbackResourceDiagnostics();
    if (grace_resources.value(QStringLiteral("hibernated")).toBool() ||
        !grace_resources.value(QStringLiteral("playerSourceLoaded")).toBool() ||
        grace_resources.value(QStringLiteral("prefetchPlayerSourceLoaded")).toBool() ||
        grace_resources.value(QStringLiteral("retainedVideoFrameCount")).toInt() <= 0 ||
        !grace_resources.value(QStringLiteral("processedFrameImagePresent")).toBool() ||
        grace_resources.value(QStringLiteral("processedFrameImageBytes")).toULongLong() == 0U) {
      return fail("the native Program source and displayed frame should survive the bounded workspace-switch grace");
    }

    const auto grace_ms = grace_resources
                              .value(QStringLiteral("resourceReleaseGraceMs"))
                              .toInt();
    if (grace_ms <= 0) {
      return fail("playback resource diagnostics should report the bounded release grace");
    }
    const auto wait_for_ms = [](int milliseconds) {
      QEventLoop loop;
      QTimer::singleShot(milliseconds, Qt::PreciseTimer, &loop, &QEventLoop::quit);
      loop.exec();
    };

    wait_for_ms(grace_ms - 300);
    native_playback.setWorkspaceActive(true);
    wait_for_ms(400);
    auto rapid_resume_resources = native_playback.playbackResourceDiagnostics();
    if (rapid_resume_resources.value(QStringLiteral("hibernated")).toBool() ||
        !rapid_resume_resources.value(QStringLiteral("playerSourceLoaded")).toBool() ||
        !native_playback.workspaceActive() ||
        native_playback.activeSourceUrl() != expected_native_source) {
      return fail("reactivating before grace expiry should cancel the stale hibernation and retain the native decoder source");
    }

    native_playback.setWorkspaceActive(false);
    QElapsedTimer release_elapsed;
    release_elapsed.start();
    wait_for_ms(grace_ms - 50);
    if (native_playback.playbackResourceDiagnostics().value(QStringLiteral("hibernated")).toBool()) {
      return fail("inactive decoder resources must survive the full advertised grace period");
    }
    wait_for_ms(qMax(0, grace_ms + 100 - static_cast<int>(release_elapsed.elapsed())));
    const auto hibernated_resources =
        native_playback.playbackResourceDiagnostics();
    if (!hibernated_resources.value(QStringLiteral("hibernated")).toBool() ||
        hibernated_resources.value(QStringLiteral("playerSourceLoaded")).toBool() ||
        hibernated_resources.value(QStringLiteral("prefetchPlayerSourceLoaded")).toBool() ||
        hibernated_resources.value(QStringLiteral("retainedVideoFrameCount")).toInt() != 0 ||
        hibernated_resources.value(QStringLiteral("processedFrameImagePresent")).toBool() ||
        hibernated_resources.value(QStringLiteral("processedFrameImageBytes")).toULongLong() != 0U ||
        hibernated_resources.value(QStringLiteral("videoGradeRenderSessionCacheCount")).toInt() != 0 ||
        !native_playback.hasVideoSource() ||
        native_playback.activeSourceUrl() != expected_native_source) {
      std::cerr << "hibernated resource diagnostics: "
                << QJsonDocument::fromVariant(hibernated_resources)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("hibernating an inactive workspace should unload decoder and frame resources while retaining the Program source identity");
    }
    photo_editor::desktop::TimelinePlaybackFrameTestAccess::deliverLateDecoderFrame(
        native_playback);
    QCoreApplication::processEvents();
    const auto after_late_frame_resources =
        native_playback.playbackResourceDiagnostics();
    if (!after_late_frame_resources.value(QStringLiteral("hibernated")).toBool() ||
        after_late_frame_resources.value(QStringLiteral("retainedVideoFrameCount")).toInt() != 0 ||
        after_late_frame_resources.value(QStringLiteral("processedFrameImageBytes")).toULongLong() != 0U) {
      return fail("late decoder frame callbacks should be discarded after hibernation instead of retaining a hidden preview frame");
    }
    const auto document_hash_after_hibernate =
        native_document.deterministicDocumentHash();
    const auto selected_clips_after_hibernate = native_document.selectedClipIds();
    const auto can_undo_after_hibernate = native_document.canUndo();
    const auto playhead_after_hibernate =
        native_document.sequencePlayheadSeconds();
    const auto document_was_preserved =
        document_hash_after_hibernate == document_hash_before_suspend &&
        selected_clips_after_hibernate == selected_clips_before_suspend &&
        can_undo_after_hibernate == can_undo_before_suspend &&
        nearly_equal(playhead_after_hibernate, playhead_before_suspend) &&
        !native_document.sequencePlaying();
    if (!document_was_preserved) {
      std::cerr << "document preservation diagnostics: hashBefore="
                << document_hash_before_suspend.toStdString()
                << " hashAfter=" << document_hash_after_hibernate.toStdString()
                << " selectionEqual="
                << (selected_clips_after_hibernate == selected_clips_before_suspend)
                << " canUndoBefore=" << can_undo_before_suspend
                << " canUndoAfter=" << can_undo_after_hibernate
                << " playheadBefore=" << playhead_before_suspend
                << " playheadAfter=" << playhead_after_hibernate
                << " mediaAnalysisBusy=" << native_document.mediaAnalysisBusy()
                << " incrementalLoadBusy=" << native_document.incrementalLoadBusy()
                << '\n';
      return fail("hibernation should preserve document edits, selection, and the paused playhead");
    }

    if (native_cocoa_playback_test) {
      native_window.show();
      activate_native_test_window(&native_window);
      native_window.requestUpdate();
      QElapsedTimer resume_window_exposure_timer;
      resume_window_exposure_timer.start();
      while (!native_window.isExposed() &&
             resume_window_exposure_timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(10);
      }
      if (!native_window.isExposed()) {
        const auto* focus_window = QGuiApplication::focusWindow();
        std::cerr << "native output exposure diagnostics: visible="
                  << native_window.isVisible()
                  << " active=" << native_window.isActive()
                  << " visibility=" << static_cast<int>(native_window.visibility())
                  << " appState="
                  << static_cast<int>(QGuiApplication::applicationState())
                  << " focusWindow=" << (focus_window != nullptr)
                  << " activeSource="
                  << native_playback.activeSourceUrl().toStdString()
                  << " resources="
                  << QJsonDocument::fromVariant(
                         native_playback.playbackResourceDiagnostics())
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return fail("the Cocoa native output window should be exposed again before decoder resume");
      }
    }

    native_playback.setWorkspaceActive(true);
    const auto resumed_resources =
        native_playback.playbackResourceDiagnostics();
    if (resumed_resources.value(QStringLiteral("hibernated")).toBool() ||
        !resumed_resources.value(QStringLiteral("playerSourceLoaded")).toBool() ||
        native_playback.activeSourceUrl() != expected_native_source ||
        !nearly_equal(
            native_document.sequencePlayheadSeconds(), playhead_before_suspend) ||
        native_document.selectedClipIds() != selected_clips_before_suspend) {
      return fail("reactivating a hibernated workspace should reopen the same Program source at its retained playhead and selection");
    }
    native_playback.play();
    const auto early_play_resources =
        native_playback.playbackResourceDiagnostics();
    if (!native_playback.playbackStartPending() ||
        !early_play_resources.value(QStringLiteral("nativePrerollPlayPending")).toBool() ||
        native_document.sequencePlaying()) {
      return fail("an immediate Play request during hibernated-source loading should wait for the decoded target frame");
    }
    native_playback.pause();
    if (native_playback.playbackStartPending() ||
        native_document.sequencePlaying()) {
      return fail("Pause should cancel an early deferred Play without starting the retained document clock");
    }
    if (native_cocoa_playback_test && !wait_for_native_frame()) {
      const auto player_diagnostics =
          photo_editor::desktop::TimelinePlaybackFrameTestAccess::nativePlayerDiagnostics(
              native_playback);
      log_native_frame_mismatch("resumed native playback");
      std::cerr << "native player diagnostics: "
                << QJsonDocument::fromVariant(player_diagnostics)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << '\n';
      return fail("native playback should decode and process the retained source playhead again after hibernation");
    }
    if (native_cocoa_playback_test &&
        (native_document.deterministicDocumentHash() !=
            document_hash_before_suspend ||
        native_document.selectedClipIds() != selected_clips_before_suspend ||
        native_document.canUndo() != can_undo_before_suspend ||
        !nearly_equal(
            native_document.sequencePlayheadSeconds(), playhead_before_suspend) ||
        native_document.sequencePlaying())) {
      return fail("decoding the resumed Program frame should not alter the document, selection, history, or playhead");
    }

    if (native_cocoa_playback_test) {
      const auto wait_for_native_preroll =
          [&native_playback](bool expected_active, int timeout_ms) {
        QElapsedTimer timer;
        timer.start();
        while (native_playback.playbackResourceDiagnostics()
                       .value(QStringLiteral("nativePrerollActive"))
                       .toBool() != expected_active &&
               timer.elapsed() < timeout_ms) {
          QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
          QThread::msleep(5);
        }
        return native_playback.playbackResourceDiagnostics()
                   .value(QStringLiteral("nativePrerollActive"))
                   .toBool() == expected_active;
      };
      const auto expose_native_window = [&native_window]() {
        native_window.show();
        activate_native_test_window(&native_window);
        native_window.requestUpdate();
        QElapsedTimer timer;
        timer.start();
        while (!native_window.isExposed() && timer.elapsed() < 10000) {
          QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
          QThread::msleep(10);
        }
        return native_window.isExposed();
      };

      if (native_playback.audioRmsLeft() != 0.0 ||
          native_playback.audioRmsRight() != 0.0 ||
          !native_playback.playbackResourceDiagnostics()
               .value(QStringLiteral("audioOutputSuspended"))
               .toBool()) {
        return fail("native paused preroll should keep the real tone silent before an explicit Play handoff");
      }

      native_playback.setWorkspaceActive(false);
      wait_for_ms(grace_ms + 100);
      if (!native_playback.playbackResourceDiagnostics()
               .value(QStringLiteral("hibernated"))
               .toBool()) {
        return fail("the scrub and Play regression should begin from a released native decoder");
      }
      if (!expose_native_window()) {
        return fail("the Cocoa output window should be exposed before the scrub and Play regression");
      }
      native_playback.setWorkspaceActive(true);
      if (!wait_for_native_preroll(true, 10000)) {
        return fail("reactivation should begin silent native preroll for the scrub and Play regression");
      }
      auto scrub_resources = native_playback.playbackResourceDiagnostics();
      if (scrub_resources.value(QStringLiteral("nativePrerollTargetMs")).toLongLong() != 750) {
        return fail("the resumed preroll should initially target the retained 750 ms source frame");
      }
      QElapsedTimer initial_seek_timer;
      initial_seek_timer.start();
      while ((!scrub_resources.value(QStringLiteral("nativePrerollSeekIssued")).toBool() ||
              scrub_resources.value(QStringLiteral("nativePrerollSeekCount")).toInt() < 1) &&
             initial_seek_timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
        scrub_resources = native_playback.playbackResourceDiagnostics();
      }
      if (!scrub_resources.value(QStringLiteral("nativePrerollSeekIssued")).toBool() ||
          scrub_resources.value(QStringLiteral("nativePrerollSeekCount")).toInt() < 1) {
        return fail("the native warm-up should issue an initial seek before the scrub retarget");
      }
      const auto seeks_before_user_scrub =
          scrub_resources.value(QStringLiteral("nativePrerollSeekCount")).toInt();
      const auto frame_source_before_user_scrub =
          native_playback.processedFrameSource();

      native_playback.setInteractionActive(true);
      native_playback.seekSequenceSeconds(0.6);
      native_playback.seekSequenceSeconds(0.45);
      native_playback.seekSequenceSeconds(0.1);
      native_playback.play();
      const auto pending_play_resources =
          native_playback.playbackResourceDiagnostics();
      if (native_document.sequencePlaying() ||
          !pending_play_resources.value(QStringLiteral("nativePrerollPlayPending")).toBool() ||
          !pending_play_resources.value(QStringLiteral("audioBufferOutputDetached")).toBool() ||
          !pending_play_resources.value(QStringLiteral("audioOutputSuspended")).toBool() ||
          native_playback.audioRmsLeft() != 0.0 ||
          native_playback.audioRmsRight() != 0.0) {
        return fail("an immediate Play request during scrub preroll should remain deferred and keep the test tone silent");
      }

      QElapsedTimer live_scrub_frame_timer;
      live_scrub_frame_timer.start();
      scrub_resources = pending_play_resources;
      while ((native_playback.processedFrameSource() ==
                  frame_source_before_user_scrub ||
              !scrub_resources.value(QStringLiteral("processedFrameImagePresent")).toBool() ||
              qAbs(native_playback.processedFrameTimestampUs() - 100'000) > 100'000) &&
             live_scrub_frame_timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
        scrub_resources = native_playback.playbackResourceDiagnostics();
      }
      if (!photo_editor::desktop::TimelinePlaybackFrameTestAccess::interactionActive(
              native_playback) ||
          native_playback.processedFrameSource() == frame_source_before_user_scrub ||
          !scrub_resources.value(QStringLiteral("processedFrameImagePresent")).toBool() ||
          qAbs(native_playback.processedFrameTimestampUs() - 100'000) > 100'000 ||
          !scrub_resources.value(QStringLiteral("nativePrerollActive")).toBool() ||
          !scrub_resources.value(QStringLiteral("nativePrerollPlayPending")).toBool() ||
          !scrub_resources.value(QStringLiteral("audioBufferOutputDetached")).toBool() ||
          !scrub_resources.value(QStringLiteral("audioOutputSuspended")).toBool() ||
          native_playback.audioRmsLeft() != 0.0 ||
          native_playback.audioRmsRight() != 0.0) {
        std::cerr << "native live scrub diagnostics: timestampUs="
                  << native_playback.processedFrameTimestampUs()
                  << " resources="
                  << QJsonDocument::fromVariant(scrub_resources)
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return fail("scrubbing should publish the latest decoded target while interaction remains active and keep deferred Play silent");
      }
      native_playback.setInteractionActive(false);

      QElapsedTimer retarget_timer;
      retarget_timer.start();
      scrub_resources = native_playback.playbackResourceDiagnostics();
      while ((scrub_resources.value(QStringLiteral("nativePrerollSeekCount")).toInt() <=
                  seeks_before_user_scrub ||
              scrub_resources.value(QStringLiteral("nativePrerollTargetMs")).toLongLong() != 100) &&
             retarget_timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
        scrub_resources = native_playback.playbackResourceDiagnostics();
      }
      if (scrub_resources.value(QStringLiteral("nativePrerollSeekCount")).toInt() <=
              seeks_before_user_scrub ||
          scrub_resources.value(QStringLiteral("nativePrerollTargetMs")).toLongLong() != 100 ||
          !scrub_resources.value(QStringLiteral("nativePrerollPlayPending")).toBool() ||
          !scrub_resources.value(QStringLiteral("nativePrerollActive")).toBool() ||
          scrub_resources.value(QStringLiteral("nativePrerollTimeoutCount")).toInt() != 0) {
        std::cerr << "native scrub retarget diagnostics: "
                  << QJsonDocument::fromVariant(scrub_resources)
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return fail("a scrub during preroll should issue a bounded retarget to the latest 100 ms position after any backend retries");
      }

      QTimer pause_after_deferred_play;
      pause_after_deferred_play.setSingleShot(true);
      QObject::connect(
          &pause_after_deferred_play,
          &QTimer::timeout,
          &native_playback,
          [&native_playback]() { native_playback.pause(); });
      auto saw_deferred_play_start = false;
      const auto playback_state_connection = QObject::connect(
          &native_document,
          &TimelineDocumentController::playbackStateChanged,
          &native_playback,
          [&native_document,
           &pause_after_deferred_play,
           &saw_deferred_play_start]() {
            if (native_document.sequencePlaying()) {
              saw_deferred_play_start = true;
              if (!pause_after_deferred_play.isActive()) {
                pause_after_deferred_play.start(250);
              }
            }
          });
      auto deferred_play_handoff_observed = false;
      QElapsedTimer deferred_play_timer;
      deferred_play_timer.start();
      while (!deferred_play_handoff_observed &&
             deferred_play_timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
        scrub_resources = native_playback.playbackResourceDiagnostics();
        const auto accepted_target_timestamp_us = scrub_resources
            .value(QStringLiteral("nativePrerollLastTargetTimestampUs"))
            .toLongLong();
        const auto center_color =
            photo_editor::desktop::TimelinePlaybackFrameTestAccess::
                processedFrameCenterColor(native_playback);
        deferred_play_handoff_observed = saw_deferred_play_start &&
            !scrub_resources.value(QStringLiteral("nativePrerollActive")).toBool() &&
            qAbs(accepted_target_timestamp_us - 100'000) <= 100'000 &&
            scrub_resources.value(QStringLiteral("processedFrameImagePresent")).toBool() &&
            !scrub_resources.value(QStringLiteral("frameProcessingInFlight")).toBool() &&
            center_color.red() > center_color.blue() + 80 &&
            !scrub_resources.value(QStringLiteral("audioBufferOutputDetached")).toBool() &&
            !scrub_resources.value(QStringLiteral("audioOutputSuspended")).toBool();
      }
      if (!deferred_play_handoff_observed) {
        pause_after_deferred_play.stop();
        QObject::disconnect(playback_state_connection);
        std::cerr << "native deferred Play diagnostics: "
                  << QJsonDocument::fromVariant(scrub_resources)
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return fail("deferred Play should start only after the latest red frame is decoded, then reattach the native audio output");
      }
      pause_after_deferred_play.stop();
      QObject::disconnect(playback_state_connection);
      native_playback.pause();
      const auto post_play_resources =
          native_playback.playbackResourceDiagnostics();
      if (native_document.sequencePlaying() ||
          native_document.selectedClipIds() != selected_clips_before_suspend ||
          native_document.canUndo() != can_undo_before_suspend ||
          post_play_resources.value(QStringLiteral("audioBufferOutputDetached")).toBool() ||
          post_play_resources.value(QStringLiteral("audioOutputSuspended")).toBool() ||
          !post_play_resources.value(QStringLiteral("processedFrameImagePresent")).toBool() ||
          post_play_resources.value(QStringLiteral("processedFrameImageBytes")).toULongLong() == 0U) {
        std::cerr << "native post-Play diagnostics: playing="
                  << native_document.sequencePlaying()
                  << " selectionEqual="
                  << (native_document.selectedClipIds() == selected_clips_before_suspend)
                  << " canUndoBefore=" << can_undo_before_suspend
                  << " canUndoAfter=" << native_document.canUndo()
                  << " resources="
                  << QJsonDocument::fromVariant(post_play_resources)
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return fail("the explicit Play handoff should advance only after the target frame, retain its decoded pause image, and leave selection and undo history intact");
      }

      // A second toggle while Play is waiting for a decoded target acts as
      // Pause. Deactivation and Program source changes must also clear that
      // deferred intent so no stale frame callback can start playback later.
      native_playback.setWorkspaceActive(false);
      wait_for_ms(grace_ms + 100);
      if (!expose_native_window()) {
        return fail("the Cocoa output window should be exposed before the Play cancellation regression");
      }
      native_playback.setWorkspaceActive(true);
      if (!wait_for_native_preroll(true, 10000)) {
        return fail("the Play cancellation regression should start a fresh bounded native preroll");
      }
      native_playback.play();
      if (!native_playback.playbackResourceDiagnostics()
               .value(QStringLiteral("nativePrerollPlayPending"))
               .toBool() ||
          native_document.sequencePlaying()) {
        return fail("a Play pressed during paused native preroll should be deferred");
      }
      native_playback.togglePlayback();
      auto cancellation_resources =
          native_playback.playbackResourceDiagnostics();
      if (cancellation_resources.value(QStringLiteral("nativePrerollPlayPending")).toBool() ||
          native_document.sequencePlaying()) {
        return fail("a second playback toggle should cancel a queued Play while keeping the document paused");
      }
      native_playback.play();
      if (!native_playback.playbackResourceDiagnostics()
               .value(QStringLiteral("nativePrerollPlayPending"))
               .toBool()) {
        return fail("a fresh explicit Play request should be pending before workspace deactivation");
      }
      native_playback.setWorkspaceActive(false);
      if (native_playback.playbackResourceDiagnostics()
              .value(QStringLiteral("nativePrerollPlayPending"))
              .toBool() ||
          native_document.sequencePlaying()) {
        return fail("workspace deactivation should cancel deferred playback before any stale frame can resume the document");
      }
      native_playback.setWorkspaceActive(true);
      if (!wait_for_native_preroll(true, 10000) ||
          native_document.sequencePlaying() ||
          native_playback.playbackResourceDiagnostics()
              .value(QStringLiteral("nativePrerollPlayPending"))
              .toBool()) {
        return fail("reactivation after cancellation should warm a paused frame without restoring the cancelled Play intent");
      }
      native_playback.play();
      native_playback.seekSequenceSeconds(4.5);
      cancellation_resources = native_playback.playbackResourceDiagnostics();
      if (cancellation_resources.value(QStringLiteral("nativePrerollPlayPending")).toBool() ||
          native_document.sequencePlaying() ||
          native_playback.activeSourceUrl() !=
              QUrl::fromLocalFile(native_second_video).toString()) {
        return fail("switching the Program source during deferred Play should cancel the old source resume request");
      }
      QElapsedTimer stale_play_timer;
      stale_play_timer.start();
      while (stale_play_timer.elapsed() < 150) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      }
      if (native_document.sequencePlaying()) {
        return fail("a stale decoder callback must not start playback after the Program source changes");
      }

      if (native_cocoa_audio_qualification_test) {
        native_playback.seekSequenceSeconds(0.25);
        QElapsedTimer tone_source_timer;
        tone_source_timer.start();
        const auto tone_source_url = QUrl::fromLocalFile(native_first_video).toString();
        while ((native_playback.activeSourceUrl() != tone_source_url ||
                !native_playback.playbackResourceDiagnostics()
                     .value(QStringLiteral("playerMediaReady"))
                     .toBool()) &&
               tone_source_timer.elapsed() < 10000) {
          QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
          QThread::msleep(5);
        }
        if (native_playback.activeSourceUrl() != tone_source_url ||
            !native_playback.playbackResourceDiagnostics()
                 .value(QStringLiteral("playerMediaReady"))
                 .toBool()) {
          return fail("the tone fixture should load in the separate native audio qualification");
        }
        const auto sink_bytes_before_qualification = native_playback.playbackResourceDiagnostics()
            .value(QStringLiteral("audioDecoder")).toMap()
            .value(QStringLiteral("sinkWrittenBytes")).toULongLong();
        native_playback.play();
        QElapsedTimer audio_qualification_timer;
        audio_qualification_timer.start();
        while (audio_qualification_timer.elapsed() < 1200 &&
               !native_audio_activity_observed) {
          QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
          const auto left_rms = native_playback.audioRmsLeft();
          const auto right_rms = native_playback.audioRmsRight();
          native_audio_qualification_left_rms =
              qMax(native_audio_qualification_left_rms, left_rms);
          native_audio_qualification_right_rms =
              qMax(native_audio_qualification_right_rms, right_rms);
          native_audio_activity_observed =
              native_document.sequencePlaying() && left_rms > 0.001 &&
              right_rms > 0.001 && native_playback.playbackResourceDiagnostics()
                  .value(QStringLiteral("audioDecoder")).toMap()
                  .value(QStringLiteral("sinkWrittenBytes")).toULongLong() >=
                      sink_bytes_before_qualification + 96000 &&
              audio_qualification_timer.elapsed() >= 400;
          QThread::msleep(5);
        }
        native_playback.pause();
        std::cerr << "native audio decode/sink diagnostics: maxLeftRms="
                  << native_audio_qualification_left_rms
                  << " maxRightRms=" << native_audio_qualification_right_rms << ' '
                  << QJsonDocument::fromVariant(native_playback.playbackResourceDiagnostics()
                         .value(QStringLiteral("audioDecoder")))
                         .toJson(QJsonDocument::Compact).toStdString() << '\n';
      }
    }
  }

  playback.setLikelyNextEditSeconds(4.5);
  QEventLoop likely_edit_loop;
  QTimer::singleShot(30, &likely_edit_loop, &QEventLoop::quit);
  likely_edit_loop.exec();
  if (playback.prefetchPlan().isEmpty() ||
      playback.prefetchPlan().first().toMap().value(QStringLiteral("reason")).toString() !=
          QStringLiteral("likely-next-edit") ||
      playback.activeSourceUrl() != first_visible_source ||
      playback.prefetchedSourceUrl() == first_visible_source) {
    return fail("likely edit prefetch must use the separate warm player without evicting Program");
  }
  playback.setLikelyNextEditSeconds(-1.0);
  playback.seekSequenceSeconds(6.0);
  const auto second_visible_source = playback.activeSourceUrl();
  playback.shuttleReverse();
  QEventLoop reverse_prefetch_loop;
  QTimer::singleShot(30, &reverse_prefetch_loop, &QEventLoop::quit);
  reverse_prefetch_loop.exec();
  if (playback.prefetchDirection() >= 0.0 || playback.prefetchPlan().isEmpty() ||
      playback.prefetchPlan().first().toMap().value(QStringLiteral("reason")).toString() !=
          QStringLiteral("reverse-cut") ||
      playback.prefetchPlan().first().toMap().value(QStringLiteral("sourceUrl")).toString() !=
          first_visible_source ||
      playback.activeSourceUrl() != second_visible_source) {
    return fail("reverse shuttle should reverse prefetch direction while retaining the visible source");
  }
  playback.stopShuttle();
  playback.setPlaybackLoop(0.5, 8.0);
  QEventLoop loop_prefetch_loop;
  QTimer::singleShot(30, &loop_prefetch_loop, &QEventLoop::quit);
  loop_prefetch_loop.exec();
  auto found_loop_start = false;
  for (const auto& value : playback.prefetchPlan()) {
    if (value.toMap().value(QStringLiteral("reason")).toString() ==
            QStringLiteral("loop-start") &&
        value.toMap().value(QStringLiteral("sourceUrl")).toString() == first_visible_source) {
      found_loop_start = true;
      break;
    }
  }
  if (!found_loop_start || playback.activeSourceUrl() != second_visible_source) {
    return fail("loop scheduling should warm the opposite boundary without replacing Program");
  }
  playback.clearPlaybackLoop();
  if (!playback.playReviewRange(2.0, 4.0, 1.0, 0.5, false) ||
      !playback.playbackRangeActive() || playback.playbackRangeLooping() ||
      !nearly_equal(playback.playbackRangeStart(), 1.0) ||
      !nearly_equal(playback.playbackRangeEnd(), 4.5) ||
      !document.sequencePlaying() ||
      !nearly_equal(document.sequencePlayheadSeconds(), 1.0)) {
    return fail("saved review playback should include explicit preroll and postroll while exposing exact active boundaries");
  }
  playback.stopReviewPlayback();
  if (playback.playbackRangeActive() || document.sequencePlaying()) {
    return fail("stopping a review range should clear its playback boundary and pause transport");
  }
  if (!playback.playInToOut(0.1, 0.2)) {
    return fail("Play In to Out should start a one-shot exact range");
  }
  QEventLoop in_out_loop;
  QTimer::singleShot(350, &in_out_loop, &QEventLoop::quit);
  in_out_loop.exec();
  if (document.sequencePlaying() || playback.playbackRangeActive() ||
      std::abs(document.sequencePlayheadSeconds() - 0.2) > 0.04) {
    return fail("one-shot In-to-Out playback should stop on the exact Out boundary without running to sequence end");
  }
  if (!playback.playReviewRange(0.1, 0.2, 0.0, 0.0, true)) {
    return fail("looped range playback should start from its exact In boundary");
  }
  QEventLoop range_loop;
  QTimer::singleShot(350, &range_loop, &QEventLoop::quit);
  range_loop.exec();
  if (!document.sequencePlaying() || !playback.playbackRangeActive() ||
      !playback.playbackRangeLooping() ||
      document.sequencePlayheadSeconds() < 0.09 ||
      document.sequencePlayheadSeconds() > 0.21) {
    return fail("looped review playback should wrap within its exact range rather than pausing at Out");
  }
  playback.stopReviewPlayback();
  if (!playback.reviewFromEditPoint(4.0, 0.5, 0.75) ||
      !nearly_equal(playback.playbackRangeStart(), 3.5) ||
      !nearly_equal(playback.playbackRangeEnd(), 4.75) ||
      playback.playbackRangeLooping()) {
    return fail("Review from Edit Point should derive a one-shot preroll/postroll window around the boundary");
  }
  playback.stopReviewPlayback();
  playback.seekSequenceSeconds(0.0);

  photo_editor::desktop::TimelineAudioOutput panned_output;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  if (panned_output.bufferOutput()->format().isValid()) {
    return fail("decoded-buffer output should retain each source's native sample rate, channel layout, and sample format");
  }
#endif
  if (!panned_output.bufferFormat().isValid()) {
    return fail("audio output should expose a valid fallback format");
  }
  const QVector<float> stereo_samples = {0.50F, 0.50F, -0.50F, -0.50F};
  const auto centered_samples = panned_output.processStereoSamplesForTesting(stereo_samples);
  if (centered_samples.size() != stereo_samples.size() ||
      !nearly_equal(centered_samples.at(0), 0.50, 0.001) ||
      !nearly_equal(centered_samples.at(1), 0.50, 0.001) ||
      !nearly_equal(panned_output.peakLeft(), 0.50, 0.001) ||
      !nearly_equal(panned_output.peakRight(), 0.50, 0.001) ||
      panned_output.truePeakLeft() + 0.0001 < panned_output.peakLeft() ||
      panned_output.truePeakRight() + 0.0001 < panned_output.peakRight() ||
      !nearly_equal(panned_output.rmsLeft(), 0.50, 0.001) ||
      !nearly_equal(panned_output.rmsRight(), 0.50, 0.001) ||
      panned_output.phaseCorrelation() < 0.99 ||
      panned_output.phasePoints().isEmpty()) {
    return fail("center pan should preserve stereo samples and publish meters plus decoded stereo phase data");
  }
  panned_output.resetMeters();
  QAudioFormat surround_format;
  surround_format.setSampleRate(48000);
  surround_format.setChannelCount(6);
  surround_format.setChannelConfig(QAudioFormat::ChannelConfigSurround5Dot1);
  surround_format.setSampleFormat(QAudioFormat::Float);
  const QVector<float> decoded_surround_frame = {
      0.10F, 0.20F, 0.15F, 0.90F, 0.25F, -0.10F};
  const QByteArray decoded_surround_bytes(
      reinterpret_cast<const char*>(decoded_surround_frame.constData()),
      decoded_surround_frame.size() * static_cast<qsizetype>(sizeof(float)));
  const QAudioBuffer decoded_surround_buffer(decoded_surround_bytes, surround_format, 0);
  if (!decoded_surround_buffer.isValid()) {
    return fail("the source-layout 5.1 decoder buffer should be valid");
  }
  panned_output.bufferOutput()->audioBufferReceived(decoded_surround_buffer);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  const auto expected_surround_left = 0.10 + (0.15 + 0.25) * 0.7071067811865476;
  const auto expected_surround_right = 0.20 + (0.15 - 0.10) * 0.7071067811865476;
  if (!nearly_equal(panned_output.peakLeft(), expected_surround_left, 0.001) ||
      !nearly_equal(panned_output.peakRight(), expected_surround_right, 0.001) ||
      !nearly_equal(panned_output.rmsLeft(), expected_surround_left, 0.001) ||
      !nearly_equal(panned_output.rmsRight(), expected_surround_right, 0.001) ||
      panned_output.truePeakLeft() + 0.0001 < panned_output.peakLeft() ||
      panned_output.truePeakRight() + 0.0001 < panned_output.peakRight()) {
    return fail("native-layout 5.1 decoder buffers should reach the stereo monitor with center and surrounds mapped once and LFE omitted");
  }
  photo_editor::desktop::TimelineAudioOutput anti_phase_output;
  const QVector<float> anti_phase_samples = {
      0.50F, -0.50F, -0.50F, 0.50F, 0.25F, -0.25F, -0.25F, 0.25F,
  };
  Q_UNUSED(anti_phase_output.processStereoSamplesForTesting(anti_phase_samples));
  if (anti_phase_output.phaseCorrelation() > -0.99 ||
      anti_phase_output.phasePoints().size() != 4) {
    return fail("the phase scope should expose bounded post-processing stereo samples and anti-phase correlation");
  }
  panned_output.resetMeters();
  const QVector<float> intersample_peak_samples = {
      -0.95F, -0.95F, 0.95F, 0.95F,
       0.95F,  0.95F, -0.95F, -0.95F,
  };
  Q_UNUSED(panned_output.processStereoSamplesForTesting(intersample_peak_samples));
  if (panned_output.peakLeft() > 0.951 ||
      panned_output.truePeakLeft() <= panned_output.peakLeft() ||
      !panned_output.clipping()) {
    return fail("four-times cubic oversampling should detect an intersample true-peak overload above sample peak");
  }
  panned_output.resetMeters();
  if (panned_output.peakLeft() != 0.0 || panned_output.truePeakLeft() != 0.0 ||
      panned_output.rmsLeft() != 0.0 || panned_output.phaseCorrelation() != 0.0 ||
      !panned_output.phasePoints().isEmpty() || panned_output.clipping()) {
    return fail("meter reset should clear peak, true-peak, RMS, phase, and overload hold together");
  }

  photo_editor::desktop::TimelineAudioOutput channel_output;
  const QVector<float> asymmetric_stereo_samples = {
      0.80F, 0.20F, -0.60F, -0.10F,
  };
  channel_output.setChannelMode(QStringLiteral("mono-left"));
  auto mapped_samples =
      channel_output.processStereoSamplesForTesting(asymmetric_stereo_samples);
  if (!nearly_equal(mapped_samples.at(0), 0.80, 0.001) ||
      !nearly_equal(mapped_samples.at(1), 0.80, 0.001) ||
      !nearly_equal(mapped_samples.at(2), -0.60, 0.001) ||
      !nearly_equal(mapped_samples.at(3), -0.60, 0.001)) {
    return fail("mono-left monitoring should route source channel one to both Program channels");
  }
  channel_output.setChannelMode(QStringLiteral("mono-right"));
  mapped_samples =
      channel_output.processStereoSamplesForTesting(asymmetric_stereo_samples);
  if (!nearly_equal(mapped_samples.at(0), 0.20, 0.001) ||
      !nearly_equal(mapped_samples.at(1), 0.20, 0.001) ||
      !nearly_equal(mapped_samples.at(2), -0.10, 0.001) ||
      !nearly_equal(mapped_samples.at(3), -0.10, 0.001)) {
    return fail("mono-right monitoring should route source channel two to both Program channels");
  }
  channel_output.setChannelMode(QStringLiteral("stereo"));
  channel_output.setSwapChannels(true);
  channel_output.setInvertLeftPhase(true);
  channel_output.setInvertRightPhase(true);
  mapped_samples =
      channel_output.processStereoSamplesForTesting(asymmetric_stereo_samples);
  if (!nearly_equal(mapped_samples.at(0), -0.20, 0.001) ||
      !nearly_equal(mapped_samples.at(1), -0.80, 0.001) ||
      !nearly_equal(mapped_samples.at(2), 0.10, 0.001) ||
      !nearly_equal(mapped_samples.at(3), 0.60, 0.001)) {
    return fail("Program monitoring should apply channel swap and independent polarity inversion before processing");
  }
  channel_output.setChannelMode(QStringLiteral("unsupported-mode"));
  if (channel_output.channelMode() != QStringLiteral("source")) {
    return fail("unsupported channel interpretations should safely fall back to Source");
  }
  panned_output.setPan(1.0);
  const auto right_samples = panned_output.processStereoSamplesForTesting(stereo_samples);
  if (qAbs(right_samples.at(0)) > 0.001 || right_samples.at(1) < 0.70F ||
      right_samples.at(1) > 0.72F) {
    return fail("full-right pan should silence left and apply the shared center-referenced pan law");
  }
  panned_output.setPan(0.0);
  panned_output.setVolume(0.25);
  const auto attenuated_samples =
      panned_output.processStereoSamplesForTesting(stereo_samples);
  if (!nearly_equal(attenuated_samples.at(0), 0.125, 0.001) ||
      !nearly_equal(attenuated_samples.at(1), 0.125, 0.001)) {
    return fail("decoded-buffer preview should apply the exact linear gain supplied by clip, track, and fade state");
  }
  panned_output.setPan(1.0);
  panned_output.setVolume(2.0);
  const auto clipped_samples = panned_output.processStereoSamplesForTesting(stereo_samples);
  Q_UNUSED(clipped_samples);
  if (!panned_output.clipping()) {
    return fail("the decoded-buffer output should latch clipping when panned gain exceeds full scale");
  }

  QVector<float> low_tone_samples;
  low_tone_samples.reserve(48000 * 2);
  for (auto frame = 0; frame < 48000; ++frame) {
    const auto sample = static_cast<float>(
        0.05 * std::sin(2.0 * 3.14159265358979323846 * 120.0 * frame / 48000.0));
    low_tone_samples.push_back(sample);
    low_tone_samples.push_back(sample);
  }
  photo_editor::desktop::TimelineAudioOutput flat_eq_output;
  const auto flat_low_tone = flat_eq_output.processStereoSamplesForTesting(low_tone_samples);
  photo_editor::desktop::TimelineAudioOutput boosted_eq_output;
  boosted_eq_output.setEqLowDb(12.0);
  const auto boosted_low_tone = boosted_eq_output.processStereoSamplesForTesting(low_tone_samples);
  if (stereo_rms(boosted_low_tone, 4096) < stereo_rms(flat_low_tone, 4096) * 3.0) {
    return fail("decoded-buffer preview should apply the real low-band EQ before metering");
  }

  QVector<float> loud_samples(48000 * 2, 0.8F);
  photo_editor::desktop::TimelineAudioOutput compressed_output;
  compressed_output.setCompressorEnabled(true);
  compressed_output.setCompressorThresholdDb(-18.0);
  compressed_output.setCompressorRatio(8.0);
  const auto compressed_samples = compressed_output.processStereoSamplesForTesting(loud_samples);
  if (stereo_rms(compressed_samples, 24000) > 0.30) {
    return fail("decoded-buffer preview compressor should reduce sustained audio above threshold");
  }

  QVector<float> contaminated_dialogue;
  contaminated_dialogue.reserve(48000 * 2);
  for (auto frame = 0; frame < 48000; ++frame) {
    const auto voice = 0.18 * std::sin(
        2.0 * 3.14159265358979323846 * 440.0 * frame / 48000.0);
    const auto hum = 0.15 * std::sin(
        2.0 * 3.14159265358979323846 * 60.0 * frame / 48000.0);
    const auto side_noise = 0.12 * std::sin(
        2.0 * 3.14159265358979323846 * 1837.0 * frame / 48000.0);
    contaminated_dialogue.push_back(static_cast<float>(voice + hum + side_noise));
    contaminated_dialogue.push_back(static_cast<float>(voice + hum - side_noise));
  }
  photo_editor::desktop::TimelineAudioOutput cleaned_dialogue_output;
  cleaned_dialogue_output.setDialogueCleanupEnabled(true);
  cleaned_dialogue_output.setVoiceIsolationAmount(0.80);
  cleaned_dialogue_output.setNoiseReductionAmount(0.0);
  cleaned_dialogue_output.setDereverbAmount(0.0);
  cleaned_dialogue_output.setHumRemovalAmount(1.0);
  cleaned_dialogue_output.setHumFrequencyHz(60);
  cleaned_dialogue_output.setAmbiencePreservation(0.0);
  const auto cleaned_dialogue =
      cleaned_dialogue_output.processStereoSamplesForTesting(contaminated_dialogue);
  const auto original_side = stereo_component_rms(contaminated_dialogue, true, 12000);
  const auto cleaned_side = stereo_component_rms(cleaned_dialogue, true, 12000);
  const auto original_hum = mid_tone_amplitude(contaminated_dialogue, 60.0, 12000);
  const auto cleaned_hum = mid_tone_amplitude(cleaned_dialogue, 60.0, 12000);
  const auto original_voice = mid_tone_amplitude(contaminated_dialogue, 440.0, 12000);
  const auto cleaned_voice = mid_tone_amplitude(cleaned_dialogue, 440.0, 12000);
  if (cleaned_side > original_side * 0.45 ||
      cleaned_hum > original_hum * 0.25 ||
      cleaned_voice < original_voice * 0.75) {
    return fail("dialogue cleanup preview should isolate centered voice and reject 60 Hz hum without erasing speech");
  }

  QVector<float> low_level_noise;
  low_level_noise.reserve(48000 * 2);
  for (auto frame = 0; frame < 48000; ++frame) {
    const auto noise = static_cast<float>(
        0.010 * std::sin(
            2.0 * 3.14159265358979323846 * 1733.0 * frame / 48000.0));
    low_level_noise.push_back(noise);
    low_level_noise.push_back(-noise);
  }
  photo_editor::desktop::TimelineAudioOutput reduced_noise_output;
  reduced_noise_output.setDialogueCleanupEnabled(true);
  reduced_noise_output.setVoiceIsolationAmount(0.0);
  reduced_noise_output.setNoiseReductionAmount(1.0);
  reduced_noise_output.setDereverbAmount(0.0);
  reduced_noise_output.setHumRemovalAmount(0.0);
  reduced_noise_output.setArtifactSmoothing(0.85);
  reduced_noise_output.setAmbiencePreservation(0.0);
  const auto reduced_noise =
      reduced_noise_output.processStereoSamplesForTesting(low_level_noise);
  if (stereo_rms(reduced_noise, 24000) > stereo_rms(low_level_noise, 24000) * 0.65) {
    return fail("dialogue cleanup preview should suppress stable low-level noise with smoothed gain changes");
  }

  photo_editor::desktop::TimelineAudioOutput ducked_output;
  ducked_output.setDuckingGainDb(-12.0);
  const QVector<float> duck_source = {0.5F, 0.5F, -0.5F, -0.5F};
  const auto ducked_samples = ducked_output.processStereoSamplesForTesting(duck_source);
  if (!nearly_equal(ducked_samples.at(0), 0.1256, 0.002) ||
      !nearly_equal(ducked_samples.at(1), 0.1256, 0.002)) {
    return fail("decoded-buffer preview should apply the document's dialogue duck gain");
  }

  if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
    // Resource-pressure behavior has focused controller coverage. Keep this
    // playback/proxy proof deterministic across loaded CI hosts.
    document.setCachePressureLevelForTesting(QStringLiteral("normal"));
    document.generateSelectedClipProxy();
    QElapsedTimer proxy_wait;
    proxy_wait.start();
    while (proxy_wait.elapsed() < 30000 &&
           document.currentClip().value(QStringLiteral("proxyStatus")).toString() !=
               QStringLiteral("ready") &&
           document.currentClip().value(QStringLiteral("proxyStatus")).toString() !=
               QStringLiteral("failed")) {
      app.processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
    const auto proxied_clip = document.currentClip();
    const auto original_source = proxied_clip.value(QStringLiteral("originalSourceUrl")).toString();
    const auto proxy_source = proxied_clip.value(QStringLiteral("proxySourceUrl")).toString();
    if (proxied_clip.value(QStringLiteral("proxyStatus")).toString() != QStringLiteral("ready") ||
        proxy_source.isEmpty()) {
      std::cerr << "proxy status="
                << proxied_clip.value(QStringLiteral("proxyStatus")).toString().toStdString()
                << " error="
                << proxied_clip.value(QStringLiteral("proxyError")).toString().toStdString()
                << " pressure="
                << document.cacheBudgetState()
                       .value(QStringLiteral("pressureLevel")).toString().toStdString()
                << " priority="
                << QJsonDocument::fromVariant(document.backgroundWorkPriorityState())
                       .toJson(QJsonDocument::Compact).toStdString()
                << " jobs="
                << QJsonDocument::fromVariant(document.timelineBackgroundJobs())
                       .toJson(QJsonDocument::Compact).toStdString()
                << '\n';
      return fail("adaptive playback fixture should generate a real ready proxy");
    }
    if (playback.previewQualityMode() != QStringLiteral("auto") ||
        playback.activeSourceUrl() != original_source || playback.usingProxy() ||
        playback.effectivePreviewQuality() != QStringLiteral("original") ||
        playback.adaptiveQualityActive() || playback.playbackHealth() != QStringLiteral("ready") ||
        !nearly_equal(playback.bufferProgress(), 1.0)) {
      return fail("auto quality should retain original media while playback is paused");
    }
    playback.setInteractionActive(true);
    if (playback.activeSourceUrl() != proxy_source || !playback.usingProxy() ||
        playback.effectivePreviewQuality() != QStringLiteral("proxy") ||
        !playback.adaptiveQualityActive()) {
      return fail("auto quality should switch to a ready proxy during active interaction");
    }
    playback.setInteractionActive(false);
    if (playback.activeSourceUrl() != original_source || playback.usingProxy()) {
      return fail("auto quality should restore original media when interaction ends");
    }
    playback.setPreviewQualityMode(QStringLiteral("proxy"));
    if (playback.activeSourceUrl() != proxy_source || !playback.usingProxy() ||
        playback.adaptiveQualityActive()) {
      return fail("proxy quality should prefer the ready proxy");
    }
    playback.setPreviewQualityMode(QStringLiteral("full"));
    if (playback.activeSourceUrl() != original_source || playback.usingProxy()) {
      return fail("full quality should always use original media");
    }
    playback.setPreviewQualityMode(QStringLiteral("half"));
    if (playback.activeSourceUrl() != original_source || playback.usingProxy() ||
        playback.effectivePreviewQuality() != QStringLiteral("half")) {
      return fail("half quality should retain originals while publishing the requested Program resolution");
    }
    playback.setPreviewQualityMode(QStringLiteral("quarter"));
    if (playback.activeSourceUrl() != original_source || playback.usingProxy() ||
        playback.effectivePreviewQuality() != QStringLiteral("quarter")) {
      return fail("quarter quality should retain originals while publishing the requested Program resolution");
    }
    playback.setPreviewQualityMode(QStringLiteral("auto"));

    const auto cache_ranges = document.renderCacheRanges();
    if (cache_ranges.isEmpty()) {
      return fail("adaptive playback fixture should expose deterministic cache ranges");
    }
    const auto cache_signature =
        cache_ranges.first().toMap().value(QStringLiteral("signature")).toString();
    const auto cache_directory = QDir(photo_editor::desktop::AppBootstrap::cacheLocation())
                                     .filePath(QStringLiteral("timeline-render-cache"));
    QDir().mkpath(cache_directory);
    const auto timeline_id = document.timelineAssetId().mid(
        QStringLiteral("timeline:").size());
    const auto cache_path =
        QDir(cache_directory).filePath(
            QStringLiteral("%1-%2.mp4").arg(timeline_id, cache_signature));
    if (touch_file(cache_path).isEmpty()) {
      return fail("adaptive playback cache placeholder should be created");
    }
    for (const auto& quality : {QStringLiteral("half"), QStringLiteral("quarter")}) {
      playback.setPreviewQualityMode(quality);
      const auto expected_scale = quality == QStringLiteral("half") ? 0.5 : 0.25;
      const auto expected_cache_source = QUrl::fromLocalFile(cache_path).toString();
      if (!playback.usingRenderCache() || playback.usingProxy() ||
          playback.activeSourceUrl() != expected_cache_source ||
          !nearly_equal(playback.effectivePreviewScale(), expected_scale)) {
        return fail("reduced-resolution preview should reuse the ready composite at the requested scale");
      }
      playback.play();
      playback.pause();
      if (!playback.usingRenderCache() ||
          playback.activeSourceUrl() != expected_cache_source) {
        return fail("pausing reduced-resolution preview should retain the cached composite source");
      }
    }
    playback.setSourceCropPreviewEnabled(true);
    if (playback.usingRenderCache() || playback.activeSourceUrl() != original_source) {
      return fail("source-crop authoring must bypass a ready final composite to expose the full original");
    }
    playback.setSourceCropPreviewEnabled(false);
    if (!playback.usingRenderCache()) {
      return fail("leaving crop authoring should restore the ready reduced-resolution composite");
    }
    document.addSelectedClipEffect(QStringLiteral("Exposure"));
    if (playback.usingRenderCache() || playback.activeSourceUrl() != original_source ||
        !nearly_equal(playback.effectivePreviewScale(), 0.25)) {
      return fail("editing reduced-resolution preview should fall back from a stale composite without changing scale");
    }
    document.undo();
    if (!playback.usingRenderCache() ||
        playback.activeSourceUrl() != QUrl::fromLocalFile(cache_path).toString()) {
      return fail("undoing the edit should restore the matching reduced-resolution composite");
    }
    if (!QFile::remove(cache_path)) {
      return fail("the reduced-resolution cache fixture should be removable");
    }
    playback.setPreviewQualityMode(QStringLiteral("half"));
    if (playback.usingRenderCache() || playback.activeSourceUrl() != original_source ||
        !nearly_equal(playback.effectivePreviewScale(), 0.5)) {
      return fail("a missing reduced-resolution cache should fall back to the original at the requested scale");
    }
    if (touch_file(cache_path).isEmpty()) {
      return fail("the reduced-resolution cache fixture should be restored");
    }
    playback.setPreviewQualityMode(QStringLiteral("full"));
    if (playback.usingRenderCache() || playback.activeSourceUrl() != original_source) {
      return fail("full preview should retain original media even when a reduced-resolution cache exists");
    }
    playback.setPreviewQualityMode(QStringLiteral("auto"));
    playback.setInteractionActive(true);
    if (!playback.usingRenderCache() || playback.usingProxy() ||
        playback.activeSourceUrl() != QUrl::fromLocalFile(cache_path).toString() ||
        playback.effectivePreviewQuality() != QStringLiteral("render-cache") ||
        !playback.adaptiveQualityActive()) {
      return fail("Auto playback should prefer a valid composited range over per-clip proxies");
    }
    playback.play();
    playback.recordPlaybackTimingSample(0, 150);
    if (playback.adaptiveDetailLevel() != QStringLiteral("half") ||
        !nearly_equal(playback.effectivePreviewScale(), 0.5) ||
        playback.adaptiveQualityReason().isEmpty() ||
        !playback.adaptiveQualityActive()) {
      return fail("Auto quality should react to measured timing lag with a disclosed temporary half-resolution Program preview");
    }
    playback.recordPlaybackTimingSample(0, 150);
    playback.recordPlaybackTimingSample(0, 150);
    if (playback.adaptiveDetailLevel() != QStringLiteral("quarter") ||
        !nearly_equal(playback.effectivePreviewScale(), 0.25)) {
      return fail("repeated playback pressure should step Auto detail down to quarter resolution without changing export media");
    }
    for (auto sample = 0; sample < 12; ++sample) {
      playback.recordPlaybackTimingSample(50, 50);
    }
    if (playback.adaptiveDetailLevel() != QStringLiteral("half")) {
      return fail("adaptive playback should require a healthy hysteresis window before restoring one quality step");
    }
    for (auto sample = 0; sample < 12; ++sample) {
      playback.recordPlaybackTimingSample(50, 50);
    }
    if (playback.adaptiveDetailLevel() != QStringLiteral("full") ||
        !nearly_equal(playback.effectivePreviewScale(), 1.0)) {
      return fail("sustained healthy playback should restore full requested Program detail");
    }
    playback.recordPlaybackTimingSample(0, 150);
    const auto cache_diagnostic = playback.playbackDiagnostics();
    if (cache_diagnostic.value(QStringLiteral("cause")).toString() !=
            QStringLiteral("cache") ||
        cache_diagnostic.value(QStringLiteral("clipId")).toString().isEmpty() ||
        !cache_diagnostic.value(QStringLiteral("action")).toString().contains(
            QStringLiteral("rebuild the preview cache"))) {
      return fail("dropped-frame diagnostics should identify the exact clip and active render-cache condition");
    }
    playback.pause();
    if (playback.adaptiveDetailLevel() != QStringLiteral("full") ||
        !playback.adaptiveQualityReason().isEmpty() ||
        !nearly_equal(playback.effectivePreviewScale(), 1.0)) {
      return fail("pausing playback should immediately restore the requested full Auto detail and clear its temporary reason");
    }
    playback.resetPlaybackDiagnostics();
    QFile::remove(cache_path);
    playback.setInteractionActive(false);
  }

  document.selectClip(0);
  document.addSelectedClipEffect(QStringLiteral("Exposure"));
  playback.play();
  playback.recordPlaybackTimingSample(0, 150);
  const auto effect_diagnostic = playback.playbackDiagnostics();
  if (effect_diagnostic.value(QStringLiteral("cause")).toString() !=
          QStringLiteral("effect") ||
      effect_diagnostic.value(QStringLiteral("detail")).toString() !=
          QStringLiteral("Exposure") ||
      !effect_diagnostic.value(QStringLiteral("action")).toString().contains(
          QStringLiteral("bypass 'Exposure'"))) {
    return fail("dropped-frame diagnostics should name the active effect and an actionable bypass/cache remedy");
  }
  playback.pause();
  document.removeSelectedClipEffect(QStringLiteral("Exposure"));
  playback.resetPlaybackDiagnostics();
  playback.play();
  playback.recordPlaybackTimingSample(0, 150);
  const auto decoder_diagnostic = playback.playbackDiagnostics();
  if (decoder_diagnostic.value(QStringLiteral("cause")).toString() !=
          QStringLiteral("decoder") ||
      decoder_diagnostic.value(QStringLiteral("decoder")).toString().isEmpty() ||
      decoder_diagnostic.value(QStringLiteral("sourcePath")).toString() != first_video) {
    return fail("dropped-frame diagnostics should identify the decoder and exact active source");
  }
  playback.pause();

  const auto program_audio_video = create_red_then_blue_test_video(
      workspace.filePath(QStringLiteral("program-audio-selection.mp4")));
  const auto voiceover_audio = create_test_audio(
      workspace.filePath(QStringLiteral("voiceover-selection.wav")), 880, 2.0);
  if (program_audio_video.isEmpty() || voiceover_audio.isEmpty()) {
    return fail("Program and voiceover audio-routing fixtures should contain decoded tones");
  }
  TimelineDocumentController audio_routing_document;
  audio_routing_document.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Program Audio Selection Routing")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 240},
      {QStringLiteral("frameRate"), 30.0},
  });
  audio_routing_document.addClipFromGallery(
      QStringLiteral("video:program-audio-selection"), program_audio_video,
      QStringLiteral("Program Video"), 2.0, QString{});
  audio_routing_document.setSelectedClipAudioGain(-6.0);
  audio_routing_document.setSelectedClipPlaybackRate(1.5);
  if (!nearly_equal(
          audio_routing_document.programClip().value(QStringLiteral("playbackRate")).toDouble(),
          1.5)) {
    return fail("the Program video should accept a distinct playback rate for source routing");
  }
  if (!audio_routing_document.addIndependentAudioTrack(QStringLiteral("voiceover"))) {
    return fail("an independent voiceover track should be added for selection routing");
  }
  audio_routing_document.seekSequenceSeconds(0.75);
  audio_routing_document.addClipFromGallery(
      QStringLiteral("audio:voiceover-selection"), voiceover_audio,
      QStringLiteral("Voiceover Tone"), 2.0, QString{});
  auto voiceover_clip_index = -1;
  auto voiceover_track_row = -1;
  const auto program_clip_id = audio_routing_document.programClip()
                                   .value(QStringLiteral("clipId")).toString();
  auto voiceover_clip_id = QString{};
  const auto clip_values = audio_routing_document.clips();
  for (auto index = 0; index < clip_values.size(); ++index) {
    const auto clip = clip_values.at(index).toMap();
    if (clip.value(QStringLiteral("filePath")).toString() == voiceover_audio) {
      voiceover_clip_index = index;
      voiceover_clip_id = clip.value(QStringLiteral("clipId")).toString();
      break;
    }
  }
  const auto track_values = audio_routing_document.sequenceTracks();
  for (auto row = 0; row < track_values.size(); ++row) {
    const auto track = track_values.at(row).toMap();
    if (track.value(QStringLiteral("audioRole")).toString() == QStringLiteral("voiceover")) {
      voiceover_track_row = row;
      break;
    }
  }
  if (program_clip_id.isEmpty() || voiceover_clip_id.isEmpty() ||
      voiceover_clip_index < 0 || voiceover_track_row < 0 ||
      !audio_routing_document.setTrackAudioSetting(
          voiceover_track_row, QStringLiteral("audioEqLowDb"), 12.0) ||
      !audio_routing_document.setTrackAutomationValue(
          voiceover_track_row, QStringLiteral("audioGainDb"), -12.0)) {
    return fail("voiceover routing state should include a distinct clip gain and track processor graph");
  }
  const auto program_source_at_voiceover_start =
      audio_routing_document.programClipPlayheadSeconds();
  audio_routing_document.selectClip(voiceover_clip_index);
  audio_routing_document.setSelectedClipAudioGain(-24.0);
  if (audio_routing_document.programClip().value(QStringLiteral("clipId")).toString() !=
          program_clip_id ||
      !nearly_equal(
          audio_routing_document.programClipPlayheadSeconds(),
          program_source_at_voiceover_start)) {
    return fail("selecting a standalone voiceover should leave the video on Program");
  }
  audio_routing_document.selectClip(0);
  audio_routing_document.seekSequenceSeconds(0.75);
  TimelinePlaybackController audio_routing_playback(&audio_routing_document);
  if (!audio_routing_document.generateSelectedClipProxy()) {
    return fail("the active Program video should accept a playback-proxy request");
  }
  QElapsedTimer proxy_ready_timer;
  proxy_ready_timer.start();
  while (audio_routing_document.programClip()
                 .value(QStringLiteral("proxyStatus")).toString() != QStringLiteral("ready") &&
         proxy_ready_timer.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  if (audio_routing_document.programClip()
              .value(QStringLiteral("proxyStatus")).toString() != QStringLiteral("ready")) {
    return fail("the Program audio selection fixture should finish its small proxy encode");
  }
  audio_routing_playback.setPreviewQualityMode(QStringLiteral("clip"));
  if (!audio_routing_playback.usingProxy()) {
    return fail("clip preview should decode the generated Program proxy");
  }
  const auto compare_selection_waveforms = [&audio_routing_document, &audio_routing_playback,
                                            &program_clip_id,
                                            program_source_at_voiceover_start,
                                            voiceover_clip_index](
                                               bool render_cache, bool proxy) {
    audio_routing_document.selectClip(0);
    audio_routing_document.seekSequenceSeconds(0.75);
    const auto decoded_source_url = audio_routing_playback.activeSourceUrl();
    const auto video_selected =
        photo_editor::desktop::TimelinePlaybackFrameTestAccess::decodedProgramTone(
            audio_routing_playback, render_cache, proxy);
    audio_routing_document.selectClip(voiceover_clip_index);
    const auto voiceover_selected =
        photo_editor::desktop::TimelinePlaybackFrameTestAccess::decodedProgramTone(
            audio_routing_playback, render_cache, proxy);
    if (audio_routing_document.programClip()
            .value(QStringLiteral("clipId")).toString() != program_clip_id ||
        audio_routing_playback.activeSourceUrl() != decoded_source_url ||
        !nearly_equal(
            audio_routing_document.programClipPlayheadSeconds(),
            program_source_at_voiceover_start) ||
        video_selected.size() != voiceover_selected.size() ||
        !nearly_equal(
            audio_routing_playback.activePlaybackRate(), render_cache ? 1.0 : 1.5)) {
      return false;
    }
    auto maximum_difference = 0.0;
    for (auto index = 0; index < video_selected.size(); ++index) {
      maximum_difference = qMax(
          maximum_difference,
          qAbs(static_cast<double>(video_selected.at(index)) -
               static_cast<double>(voiceover_selected.at(index))));
    }
    return maximum_difference <= 1.0e-6 && stereo_rms(video_selected) > 0.02;
  };
  if (!compare_selection_waveforms(false, audio_routing_playback.usingProxy())) {
    return fail("selecting a different audio clip should not change the decoded Program video waveform while proxy playback is active");
  }

  audio_routing_document.selectClip(0);
  if (!audio_routing_document.generateRenderCache()) {
    return fail("the same audio-routing fixture should accept its final preview-cache render");
  }
  QElapsedTimer render_cache_ready_timer;
  render_cache_ready_timer.start();
  while (audio_routing_document.renderCachePlaybackSourceUrl().isEmpty() &&
         render_cache_ready_timer.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  audio_routing_playback.setPreviewQualityMode(QStringLiteral("half"));
  if (audio_routing_document.renderCachePlaybackSourceUrl().isEmpty() ||
      !audio_routing_playback.usingRenderCache() ||
      !compare_selection_waveforms(true, false)) {
    return fail("selection should preserve cached Program audio while the rendered composite is active");
  }
  audio_routing_playback.setPreviewQualityMode(QStringLiteral("clip"));
  audio_routing_document.selectClip(0);
  audio_routing_document.toggleTrackMuted(1);
  const auto muted_program_audio =
      photo_editor::desktop::TimelinePlaybackFrameTestAccess::decodedProgramTone(
          audio_routing_playback, false, audio_routing_playback.usingProxy());
  audio_routing_document.toggleTrackMuted(1);
  audio_routing_document.toggleTrackSolo(voiceover_track_row);
  const auto soloed_out_program_audio =
      photo_editor::desktop::TimelinePlaybackFrameTestAccess::decodedProgramTone(
          audio_routing_playback, false, audio_routing_playback.usingProxy());
  audio_routing_document.toggleTrackSolo(voiceover_track_row);
  if (stereo_rms(muted_program_audio) > 1.0e-6 ||
      stereo_rms(soloed_out_program_audio) > 1.0e-6) {
    return fail("the Program video audio route should honor its own track mute and the active audio solo set");
  }

  const auto offline_diagnostic_path =
      touch_file(workspace.filePath(QStringLiteral("diagnostic-offline.mp4")));
  TimelineDocumentController offline_diagnostic_document;
  offline_diagnostic_document.createTimeline(QStringLiteral("Offline Diagnostics"));
  offline_diagnostic_document.addClipFromGallery(
      QStringLiteral("video:offline-diagnostic"), offline_diagnostic_path,
      QStringLiteral("Offline Diagnostic Clip"), 2.0, QString{});
  QFile::remove(offline_diagnostic_path);
  TimelinePlaybackController offline_diagnostic_playback(&offline_diagnostic_document);
  offline_diagnostic_playback.play();
  offline_diagnostic_playback.recordPlaybackTimingSample(0, 150);
  const auto storage_diagnostic =
      offline_diagnostic_playback.playbackDiagnostics();
  if (storage_diagnostic.value(QStringLiteral("cause")).toString() !=
          QStringLiteral("storage") ||
      storage_diagnostic.value(QStringLiteral("clipLabel")).toString() !=
          QStringLiteral("Offline Diagnostic Clip") ||
      !storage_diagnostic.value(QStringLiteral("action")).toString().contains(
          QStringLiteral("Batch Relink"))) {
    return fail("dropped-frame diagnostics should identify offline storage and route directly to batch relink");
  }
  offline_diagnostic_playback.pause();

  playback.seekSequenceSeconds(2.5);
  if (document.currentIndex() != 0 ||
      !nearly_equal(document.sequencePlayheadSeconds(), 2.5) ||
      !nearly_equal(document.currentClipPlayheadSeconds(), 2.5)) {
    return fail("sequence seeking should update native timeline playheads through playback controller");
  }

  playback.play();
  if (!document.sequencePlaying()) {
    return fail("playback controller should start native sequence playback");
  }

  playback.pause();
  if (document.sequencePlaying()) {
    return fail("playback controller should pause native sequence playback");
  }

  playback.seekSequenceSeconds(1.0);
  playback.shuttleForward();
  if (!document.sequencePlaying() || !playback.shuttling() ||
      !nearly_equal(playback.shuttleRate(), 1.0)) {
    return fail("L should begin forward shuttle playback at 1x");
  }
  playback.shuttleForward();
  playback.shuttleForward();
  playback.shuttleForward();
  playback.shuttleForward();
  if (!nearly_equal(playback.shuttleRate(), 8.0)) {
    return fail("repeated L presses should climb and cap the forward shuttle at 8x");
  }
  QEventLoop forward_shuttle_wait;
  QTimer::singleShot(45, &forward_shuttle_wait, &QEventLoop::quit);
  forward_shuttle_wait.exec();
  playback.stopShuttle();
  if (document.sequencePlaying() || playback.shuttling() ||
      !nearly_equal(playback.shuttleRate(), 0.0) ||
      document.sequencePlayheadSeconds() <= 1.15) {
    return fail("8x shuttle should advance the authoritative sequence clock and K should stop it");
  }

  playback.seekSequenceSeconds(2.0);
  playback.shuttleReverse();
  playback.shuttleReverse();
  if (!document.sequencePlaying() || !nearly_equal(playback.shuttleRate(), -2.0)) {
    return fail("repeated J presses should start reverse playback and step to -2x");
  }
  QEventLoop reverse_shuttle_wait;
  QTimer::singleShot(55, &reverse_shuttle_wait, &QEventLoop::quit);
  reverse_shuttle_wait.exec();
  playback.stopShuttle();
  if (document.sequencePlayheadSeconds() >= 1.95) {
    return fail("reverse shuttle should move the authoritative sequence clock backward");
  }
  playback.seekSequenceSeconds(1.5);
  playback.shuttleForward();
  playback.shuttleForward();
  playback.shuttleReverse();
  if (!document.sequencePlaying() || !playback.shuttling() ||
      !nearly_equal(playback.shuttleRate(), -1.0)) {
    return fail("J should reverse an active forward shuttle immediately at the first reverse speed");
  }
  playback.shuttleForward();
  if (!document.sequencePlaying() || !nearly_equal(playback.shuttleRate(), 1.0)) {
    return fail("L should reverse an active reverse shuttle immediately without an intermediate stop");
  }
  playback.stopShuttle();
  playback.shuttleForward();
  playback.stepFrames(1);
  if (document.sequencePlaying() || !nearly_equal(playback.shuttleRate(), 0.0)) {
    return fail("frame stepping should stop shuttle playback before moving exactly one frame");
  }

  playback.seekSequenceSeconds(0.10);
  playback.shuttleReverse();
  playback.shuttleReverse();
  playback.shuttleReverse();
  playback.shuttleReverse();
  QEventLoop reverse_boundary_wait;
  QTimer::singleShot(45, &reverse_boundary_wait, &QEventLoop::quit);
  reverse_boundary_wait.exec();
  if (document.sequencePlaying() || playback.shuttling() ||
      !nearly_equal(playback.shuttleRate(), 0.0) ||
      !nearly_equal(document.sequencePlayheadSeconds(), 0.0, 0.01)) {
    return fail("reverse shuttle should stop cleanly at sequence start without running a pinned timer");
  }

  playback.seekSequenceSeconds(document.totalDurationSecondsValue());
  playback.shuttleReverse();
  if (!document.sequencePlaying() || playback.shuttleRate() >= 0.0 ||
      document.sequencePlayheadSeconds() < document.totalDurationSecondsValue() - 0.10) {
    return fail("J at sequence end should shuttle backward from the final frame instead of wrapping to start");
  }
  playback.stopShuttle();

  TimelineDocumentController rate_document;
  rate_document.createTimeline(QStringLiteral("Rate Truth Timeline"));
  rate_document.addClipFromGallery(
      QStringLiteral("video:rate"), first_video, QStringLiteral("Fast Clip"), 4.0,
      QStringLiteral("poster://rate"));
  rate_document.setSelectedClipPlaybackRate(2.0);
  TimelinePlaybackController rate_playback(&rate_document);
  if (!nearly_equal(rate_playback.activePlaybackRate(), 2.0) ||
      !nearly_equal(rate_document.totalDurationSecondsValue(), 2.0)) {
    return fail("the Program player rate and sequence duration should share one speed mapping");
  }
  rate_playback.seekSequenceSeconds(1.25);
  if (!nearly_equal(rate_document.currentClipPlayheadSeconds(), 2.5)) {
    return fail("sequence seeking should map through playback rate into source time");
  }
  rate_playback.seekCurrentClipSeconds(3.0);
  if (!nearly_equal(rate_document.sequencePlayheadSeconds(), 1.5)) {
    return fail("source seeking should map back through playback rate into sequence time");
  }
  if (!rate_document.applySelectedClipRetimePreset(QStringLiteral("reverse"))) {
    return fail("the playback fixture should accept reverse retiming");
  }
  rate_playback.seekSequenceSeconds(0.5);
  if (!nearly_equal(rate_playback.activePlaybackRate(), -2.0) ||
      !nearly_equal(rate_document.currentClipPlayheadSeconds(), 3.0)) {
    return fail("Program playback should publish the signed segment rate and seek reverse source time exactly");
  }
  rate_playback.seekSequenceSeconds(1.0);
  if (!rate_document.applySelectedClipRetimePreset(QStringLiteral("freeze-frame"))) {
    return fail("the playback fixture should accept a freeze frame");
  }
  rate_playback.seekSequenceSeconds(1.5);
  if (!nearly_equal(rate_playback.activePlaybackRate(), 0.0) ||
      !rate_document.currentClip().value(QStringLiteral("retimeAudioMuted")).toBool()) {
    return fail("Program playback should hold and silence the exact freeze segment");
  }

  playback.seekSequenceSeconds(4.25);
  if (document.currentIndex() != 1 ||
      !nearly_equal(document.sequencePlayheadSeconds(), 4.25) ||
      !nearly_equal(document.currentClipPlayheadSeconds(), 0.25)) {
    return fail("sequence seeks past the first clip should remap onto the next clip");
  }

  playback.stepFrames(1);
  if (document.sequencePlaying()) {
    return fail("frame stepping through the playback controller should keep playback paused");
  }
  if (document.currentClipPlayheadSeconds() <= 0.25) {
    return fail("frame stepping should advance the current clip playhead");
  }

  document.addVideoTrack();
  document.selectClip(1);
  document.moveSelectedClipToTrackRow(1, 1.0);
  playback.seekSequenceSeconds(1.5);
  if (document.currentClip().value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V2") ||
      !nearly_equal(document.currentClipPlayheadSeconds(), 0.5)) {
    return fail("playback seeking should prefer the higher active video track at the current sequence time");
  }

  TimelineDocumentController gap_document;
  gap_document.createTimeline(QStringLiteral("Gap Playback Timeline"));
  gap_document.addClipFromGallery(QStringLiteral("video:gap-1"), first_video, QStringLiteral("Before Gap"), 4.0, QStringLiteral("poster://gap-one"));
  gap_document.addClipFromGallery(QStringLiteral("video:gap-2"), second_video, QStringLiteral("After Gap"), 4.0, QStringLiteral("poster://gap-two"));
  gap_document.selectClip(1);
  gap_document.moveSelectedClipToTrackRow(0, 6.0);
  TimelinePlaybackController gap_playback(&gap_document);

  gap_playback.seekSequenceSeconds(4.5);
  if (gap_document.currentIndex() != -1 || !gap_document.playbackSourceUrl().isEmpty()) {
    return fail("seeking into a timeline gap should expose a black program interval without selecting media");
  }
  gap_playback.play();
  if (!gap_document.sequencePlaying()) {
    return fail("sequence playback should be able to start inside a timeline gap");
  }
  gap_document.syncGapPlaybackPosition(5.25);
  if (!gap_document.sequencePlaying() || gap_document.currentIndex() != -1 ||
      !nearly_equal(gap_document.sequencePlayheadSeconds(), 5.25)) {
    return fail("gap playback should advance the sequence clock without inventing a selected clip");
  }
  gap_document.syncGapPlaybackPosition(6.25);
  if (!gap_document.sequencePlaying() || gap_document.currentIndex() != 1 ||
      !nearly_equal(gap_document.currentClipPlayheadSeconds(), 0.25)) {
    return fail("gap playback should resume the next active clip at its real sequence time");
  }

  gap_playback.seekSequenceSeconds(3.9);
  gap_playback.play();
  if (!gap_document.advancePlaybackAfterClipEnd() || gap_document.currentIndex() != -1 ||
      !gap_document.sequencePlaying() || !nearly_equal(gap_document.sequencePlayheadSeconds(), 4.0)) {
    return fail("clip-boundary playback should enter a real gap instead of skipping directly to the next clip");
  }
  gap_playback.pause();

  TimelineDocumentController clock_document;
  clock_document.createTimeline(QStringLiteral("Authoritative Clock Timeline"));
  clock_document.addClipFromGallery(
      QStringLiteral("video:clock-1"), first_video, QStringLiteral("Clock One"), 0.06,
      QStringLiteral("poster://clock-one"));
  clock_document.addClipFromGallery(
      QStringLiteral("video:clock-2"), second_video, QStringLiteral("Clock Two"), 0.12,
      QStringLiteral("poster://clock-two"));
  TimelinePlaybackController clock_playback(&clock_document);
  clock_playback.seekSequenceSeconds(0.0);
  clock_playback.play();
  QEventLoop clock_wait;
  QTimer::singleShot(100, &clock_wait, &QEventLoop::quit);
  clock_wait.exec();
  clock_playback.pause();
  if (clock_document.currentIndex() != 1 || clock_document.sequencePlayheadSeconds() < 0.075 ||
      clock_document.sequencePlayheadSeconds() > 0.16) {
    return fail("the authoritative sequence clock should advance across a playable clip boundary in real time");
  }

  playback.pause();
  playback.seekSequenceSeconds(4.5);
  const auto recovery_hash = document.deterministicDocumentHash();
  const auto recovery_source = playback.activeSourceUrl();
  const auto recovery_playhead = document.sequencePlayheadSeconds();
  const QStringList recoverable_losses = {
      QStringLiteral("decoder"),
      QStringLiteral("gpu"),
      QStringLiteral("audio-device"),
      QStringLiteral("external-display"),
  };
  for (const auto& loss : recoverable_losses) {
    playback.recoverRuntimeFailure(loss, QStringLiteral("Injected test loss."));
    if (!playback.runtimeRecoveryBusy() || playback.runtimeRecoveryKind() != loss) {
      return fail("runtime loss should immediately publish one truthful bounded recovery state");
    }
    app.processEvents(QEventLoop::AllEvents, 20);
    if (playback.runtimeRecoveryBusy() ||
        !playback.runtimeRecoveryStatus().contains(QStringLiteral("without changing the edit")) ||
        document.deterministicDocumentHash() != recovery_hash ||
        playback.activeSourceUrl() != recovery_source ||
        !nearly_equal(document.sequencePlayheadSeconds(), recovery_playhead)) {
      return fail("decoder, GPU, audio-device, and display recovery should preserve project, source, playhead, and clear busy state");
    }
  }
  if (playback.runtimeRecoveryCount() != static_cast<int>(recoverable_losses.size())) {
    return fail("runtime recovery should count each completed device-loss intervention");
  }

  if (native_cocoa_audio_qualification_test &&
      !native_audio_activity_observed) {
    std::cerr << "native audio qualification diagnostics: maxLeftRms="
              << native_audio_qualification_left_rms
              << " maxRightRms=" << native_audio_qualification_right_rms
              << '\n';
    return fail("native Cocoa playback should deliver the fixture tone through TimelineAudioOutput to the audio sink after explicit Play");
  }

  return EXIT_SUCCESS;
}

#include "timeline_document_controller.h"
#include "timeline_processed_frame_cache.h"
#include "timeline_playback_controller.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QSemaphore>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>
#include <functional>
#include <iostream>

namespace photo_editor::desktop {
// Inject decoded frames at the existing sink boundary without a decoder, device,
// or timing-dependent movie fixture. Processing and publication remain real.
struct TimelinePlaybackFrameTestAccess {
  static void attachWithCachedFrame(
      TimelinePlaybackController& controller, QQuickItem* output, const QColor& color) {
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(color);
    const auto was_native = controller.native_media_enabled_;
    controller.native_media_enabled_ = true;
    controller.video_sink_.setVideoFrame(QVideoFrame(image));
    controller.attachVideoOutput(output);
    controller.native_media_enabled_ = was_native;
  }
  static void submit(TimelinePlaybackController& controller, const QColor& color) {
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(color);
    const auto was_native = controller.native_media_enabled_;
    controller.native_media_enabled_ = true;
    controller.handleVideoFrame(QVideoFrame(image));
    controller.native_media_enabled_ = was_native;
  }
  static void submitAt(
      TimelinePlaybackController& controller,
      const QColor& color,
      qint64 timestamp_us) {
    QImage image(640, 360, QImage::Format_RGBA8888);
    image.fill(color);
    auto frame = QVideoFrame(image);
    frame.setStartTime(timestamp_us);
    const auto was_native = controller.native_media_enabled_;
    controller.native_media_enabled_ = true;
    controller.handleVideoFrame(frame);
    controller.native_media_enabled_ = was_native;
  }
  static void submitScenePreviewAt(
      TimelinePlaybackController& controller,
      const QColor& color,
      qint64 source_timestamp_us) {
    QImage image(640, 360, QImage::Format_RGBA8888);
    image.fill(color);
    auto frame = QVideoFrame(image);
    frame.setStartTime(source_timestamp_us);
    controller.scene_preview_cache_active_ = true;
    controller.scene_preview_frame_key_ =
        QStringLiteral("test-scene-preview|%1").arg(source_timestamp_us);
    controller.awaiting_media_source_ = true;
    controller.awaiting_position_sync_ = true;
    controller.injecting_scene_preview_frame_ = true;
    controller.handleVideoFrame(frame);
    controller.injecting_scene_preview_frame_ = false;
  }
  static void deactivateScenePreview(TimelinePlaybackController& controller) {
    controller.scene_preview_cache_active_ = false;
    controller.awaiting_media_source_ = false;
    controller.awaiting_position_sync_ = false;
    controller.invalidateFrameProcessing();
    controller.clearProcessedFrame();
  }
  static void submitWithoutTimestamp(
      TimelinePlaybackController& controller,
      const QColor& color) {
    QImage image(640, 360, QImage::Format_RGBA8888);
    image.fill(color);
    const auto was_native = controller.native_media_enabled_;
    controller.native_media_enabled_ = true;
    auto frame = QVideoFrame(image);
    frame.setStartTime(-1);
    controller.handleVideoFrame(frame);
    controller.native_media_enabled_ = was_native;
  }
  static void setSinkFrame(TimelinePlaybackController& controller, const QColor& color) {
    QImage image(64, 64, QImage::Format_RGBA8888);
    image.fill(color);
    const auto was_native = controller.native_media_enabled_;
    controller.native_media_enabled_ = false;
    auto frame = QVideoFrame(image);
    frame.setStartTime(-1);
    controller.video_sink_.setVideoFrame(frame);
    controller.native_media_enabled_ = was_native;
  }
  static void syncPausedSameSourceWithCachedFrame(
      TimelinePlaybackController& controller,
      QQuickItem* output,
      const QColor& color) {
    controller.native_media_enabled_ = false;
    controller.attachVideoOutput(output);
    QImage image(640, 360, QImage::Format_RGBA8888);
    image.fill(color);
    auto frame = QVideoFrame(image);
    frame.setStartTime(-1);
    controller.video_sink_.setVideoFrame(frame);
    controller.current_source_url_ = controller.preferredSourceUrl();
    controller.active_video_frame_color_key_ = QStringLiteral("stale-test-settings");
    controller.awaiting_media_source_ = false;
    controller.awaiting_position_sync_ = false;
    controller.processed_frame_available_ = true;
    controller.native_media_enabled_ = true;
    controller.syncToDocument();
  }
  static void prepareSamePositionPausedSeek(
      TimelinePlaybackController& controller,
      QQuickItem* output,
      const QColor& color,
      qint64 timestamp_us) {
    controller.native_media_enabled_ = false;
    controller.attachVideoOutput(output);
    controller.syncToDocument();
    controller.awaiting_media_source_ = false;
    controller.awaiting_position_sync_ = false;
    QImage image(640, 360, QImage::Format_RGBA8888);
    image.fill(color);
    auto frame = QVideoFrame(image);
    frame.setStartTime(timestamp_us);
    controller.video_sink_.setVideoFrame(frame);
    controller.processed_frame_available_ = false;
    controller.processed_frame_timestamp_us_ = -1;
    controller.native_media_enabled_ = true;
  }
  static void submitAndCacheAt(
      TimelinePlaybackController& controller,
      const QColor& color,
      qint64 timestamp_us) {
    QImage image(640, 360, QImage::Format_RGBA8888);
    image.fill(color);
    auto frame = QVideoFrame(image);
    frame.setStartTime(timestamp_us);
    controller.native_media_enabled_ = false;
    controller.video_sink_.setVideoFrame(frame);
    controller.native_media_enabled_ = true;
    controller.handleVideoFrame(frame);
  }
  static void submitAndCacheWithoutTimestamp(
      TimelinePlaybackController& controller,
      const QColor& color) {
    QImage image(640, 360, QImage::Format_RGBA8888);
    image.fill(color);
    auto frame = QVideoFrame(image);
    frame.setStartTime(-1);
    controller.native_media_enabled_ = false;
    controller.video_sink_.setVideoFrame(frame);
    controller.native_media_enabled_ = true;
    controller.handleVideoFrame(frame);
  }
  static void seekDocumentClock(
      TimelinePlaybackController& controller,
      double sequence_seconds) {
    const auto was_native = controller.native_media_enabled_;
    controller.native_media_enabled_ = false;
    if (!controller.timeline_document_.isNull())
      controller.timeline_document_->seekSequenceSeconds(sequence_seconds);
    controller.native_media_enabled_ = was_native;
  }
  static void beginPositionSync(TimelinePlaybackController& controller, qint64 position) {
    const auto was_native = controller.native_media_enabled_;
    controller.native_media_enabled_ = true;
    controller.invalidateFrameProcessing();
    controller.pending_position_ms_ = -1;
    controller.awaited_position_ms_ = position;
    controller.awaiting_media_source_ = false;
    controller.awaiting_position_sync_ = true;
    controller.native_media_enabled_ = was_native;
  }
  static void queuePosition(TimelinePlaybackController& controller, qint64 position) {
    controller.pending_position_ms_ = position;
  }
  static bool acknowledgePosition(TimelinePlaybackController& controller, qint64 position) {
    const auto was_native = controller.native_media_enabled_;
    controller.native_media_enabled_ = true;
    const auto invoked = QMetaObject::invokeMethod(
        &controller.player_, "positionChanged", Qt::DirectConnection,
        Q_ARG(qint64, position));
    controller.native_media_enabled_ = was_native;
    return invoked;
  }
  static bool awaitingPositionSync(const TimelinePlaybackController& controller) {
    return controller.awaiting_position_sync_;
  }
  static qint64 awaitedPosition(const TimelinePlaybackController& controller) {
    return controller.awaited_position_ms_;
  }
  static bool hasSeekCandidate(const TimelinePlaybackController& controller) {
    return controller.pending_seek_video_frame_.isValid();
  }
  static bool nativeEnabled(const TimelinePlaybackController& controller) {
    return controller.native_media_enabled_;
  }
  static void changeGrade(TimelinePlaybackController& controller) {
    controller.active_video_frame_color_key_ = QStringLiteral("fixture-previous-grade");
    controller.syncToDocument();
  }
  static void changeSource(TimelinePlaybackController& controller) {
    controller.current_source_url_ = QStringLiteral("fixture-previous-source");
    controller.native_media_enabled_ = true;
    controller.syncToDocument(); // Switch to the empty document's source.
    controller.native_media_enabled_ = false;
  }
  static std::shared_ptr<std::atomic<int>> epoch(const TimelinePlaybackController& controller) {
    return controller.latest_frame_work_generation_;
  }
  static bool idle(const TimelinePlaybackController& controller) {
    return !controller.frame_processing_active_ && !controller.has_pending_video_frame_;
  }
  static bool pending(const TimelinePlaybackController& controller) {
    return controller.has_pending_video_frame_;
  }
  static QImage image(const TimelinePlaybackController& controller) {
    return controller.frame_image_state_->requestImage(nullptr, {});
  }
  static QSize targetSize(const TimelinePlaybackController& controller) {
    return controller.frameTargetSize();
  }
  static QString sourceRevision(const TimelinePlaybackController& controller) {
    return controller.active_video_source_revision_;
  }
  static qint64 processedSourceRevision(
      const TimelinePlaybackController& controller) {
    return controller.frame_source_revision_;
  }
  static void sync(TimelinePlaybackController& controller) {
    controller.syncToDocument();
  }
};
}
using namespace photo_editor::desktop;
namespace {
bool wait_until(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}
struct WorkerGate {
  QSemaphore entered, release;
  QFuture<void> worker;
  bool released = false;
  WorkerGate() {
    worker = QtConcurrent::run([this] { entered.release(); release.acquire(); });
    if (!entered.tryAcquire(1, 5000)) qFatal("Frame fixture worker did not start");
  }
  void open() { if (!released) { released = true; release.release(); } }
  ~WorkerGate() { open(); worker.waitForFinished(); }
};
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS", "1");
  QApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("No temporary fixture directory");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temporary.path().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temporary.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", temporary.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_TIMELINE_EFFECT_PRESET_PATH",
          temporary.filePath(QStringLiteral("timeline-effect-presets.json")).toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/frame_fixture");
  {
    TimelinePlaybackFrameImageState image_state;
    QImage native_image(64, 32, QImage::Format_RGBA8888);
    native_image.fill(Qt::cyan);
    image_state.setImage(native_image);
    QSize reported_size;
    const auto high_resolution_request = image_state.requestImage(
        &reported_size, QSize(128, 64));
    const auto low_resolution_request = image_state.requestImage(
        &reported_size, QSize(32, 16));
    if (high_resolution_request.size() != native_image.size() ||
        low_resolution_request.size() != QSize(32, 16) ||
        reported_size != native_image.size()) {
      return fail("Program image requests should downscale for quality modes but leave low-resolution sources at native size");
    }
  }
  {
    TimelineProcessedFrameCache bounded_cache(128U, 2U);
    QImage red(4, 4, QImage::Format_RGBA8888);
    red.fill(Qt::red);
    QImage green(4, 4, QImage::Format_RGBA8888);
    green.fill(Qt::green);
    QImage blue(4, 4, QImage::Format_RGBA8888);
    blue.fill(Qt::blue);
    QImage oversized(8, 8, QImage::Format_RGBA8888);
    oversized.fill(Qt::white);
    if (!bounded_cache.insert(QStringLiteral("red"), red) ||
        !bounded_cache.insert(QStringLiteral("green"), green) ||
        bounded_cache.find(QStringLiteral("red")).pixelColor(0, 0) != QColor(Qt::red) ||
        !bounded_cache.insert(QStringLiteral("blue"), blue) ||
        !bounded_cache.find(QStringLiteral("green")).isNull() ||
        bounded_cache.insert(QStringLiteral("oversized"), oversized)) {
      return fail("the processed-frame cache should enforce byte/entry limits and evict least-recently-used frames");
    }
    const auto statistics = bounded_cache.statistics();
    if (statistics.entries != 2U || statistics.resident_bytes != 128U ||
        statistics.hits != 1U || statistics.misses != 1U ||
        statistics.evictions != 1U) {
      return fail("processed-frame cache accounting should match bounded LRU hits, misses, bytes, and evictions");
    }
  }
  auto* pool = QThreadPool::globalInstance();
  const auto old_limit = pool->maxThreadCount();
  pool->setMaxThreadCount(1);
  const auto restore_pool = qScopeGuard([&] { pool->waitForDone(); pool->setMaxThreadCount(old_limit); });
  TimelineDocumentController document;
  {
    const auto cache_disabled_was_set = qEnvironmentVariableIsSet(
        "PHOTO_EDITOR_TIMELINE_FRAME_CACHE_DISABLE");
    const auto previous_cache_disabled = qgetenv(
        "PHOTO_EDITOR_TIMELINE_FRAME_CACHE_DISABLE");
    qunsetenv("PHOTO_EDITOR_TIMELINE_FRAME_CACHE_DISABLE");
    const auto restore_cache_mode = qScopeGuard([&] {
      if (cache_disabled_was_set) {
        qputenv("PHOTO_EDITOR_TIMELINE_FRAME_CACHE_DISABLE",
                previous_cache_disabled);
      } else {
        qunsetenv("PHOTO_EDITOR_TIMELINE_FRAME_CACHE_DISABLE");
      }
    });
    {
      const auto plain_source_path =
          temporary.filePath(QStringLiteral("frame-cache-ungraded-source.mp4"));
      QFile plain_source_file(plain_source_path);
      if (!plain_source_file.open(QIODevice::WriteOnly) ||
          plain_source_file.write("ungraded cache fixture") <= 0) {
        return fail("the ungraded processed-frame fixture source should be written");
      }
      plain_source_file.close();
      TimelineDocumentController plain_document;
      plain_document.createTimeline(QStringLiteral("Ungraded Frame Cache"));
      plain_document.addClipFromGallery(
          QStringLiteral("video:frame-cache-ungraded"), plain_source_path,
          QStringLiteral("Ungraded Frame Cache Source"), 1.0,
          QStringLiteral("poster://frame-cache-ungraded"));
      plain_document.selectClip(0);
      TimelinePlaybackController plain_playback(&plain_document);
      QQuickItem plain_output;
      plain_output.setSize(QSizeF(640, 360));
      plain_playback.attachVideoOutput(&plain_output);
      QVector<qint64> published_timestamps;
      QVector<bool> published_timestamp_estimates;
      QObject::connect(&plain_playback,
                       &TimelinePlaybackController::processedFrameSourceChanged, [&] {
        if (!plain_playback.processedFrameSource().isEmpty()) {
          published_timestamps.append(plain_playback.processedFrameTimestampUs());
          published_timestamp_estimates.append(
              plain_playback.processedFrameTimestampEstimated());
        }
      });
      TimelinePlaybackFrameTestAccess::submitAt(
          plain_playback, QColor(40, 130, 210), 3'000'000);
      if (!wait_until([&] {
            return TimelinePlaybackFrameTestAccess::idle(plain_playback) &&
                   plain_playback.frameProcessingMetrics()
                           .value(QStringLiteral("sampleCount"))
                           .toULongLong() > 0U;
          })) {
        return fail("the ungraded Program frame should complete before cache insertion is checked");
      }
      if (published_timestamps.isEmpty() || published_timestamps.last() != 3'000'000 ||
          plain_playback.processedFrameTimestampEstimated() ||
          published_timestamp_estimates.isEmpty() ||
          published_timestamp_estimates.last())
        return fail("The image publication signal should expose its decoded timestamp atomically");
      const auto plain_first_image =
          TimelinePlaybackFrameTestAccess::image(plain_playback);
      const auto plain_first_hash = QCryptographicHash::hash(
          QByteArray(reinterpret_cast<const char*>(plain_first_image.constBits()),
                     plain_first_image.sizeInBytes()),
          QCryptographicHash::Sha256);
      const auto plain_first_metrics = plain_playback.frameProcessingMetrics();
      if (!plain_first_metrics.value(QStringLiteral("frameCacheEnabled")).toBool() ||
          !plain_first_metrics.value(QStringLiteral("frameCacheKeyEligible")).toBool() ||
          !plain_first_metrics.value(QStringLiteral("frameCacheLookupPerformed")).toBool() ||
          plain_first_metrics.value(QStringLiteral("frameCacheHit")).toBool() ||
          plain_first_metrics.value(QStringLiteral("frameCacheEntries")).toULongLong() == 0U) {
        std::cerr << "Ungraded cache eligibility: keyEligible="
                  << plain_first_metrics.value(QStringLiteral("frameCacheKeyEligible")).toBool()
                  << " lookupPerformed="
                  << plain_first_metrics.value(QStringLiteral("frameCacheLookupPerformed")).toBool()
                  << " timestampUs="
                  << plain_first_metrics.value(QStringLiteral("frameCacheFrameTimestampUs"))
                         .toLongLong()
                  << " unavailableReason='"
                  << plain_first_metrics.value(QStringLiteral("frameCacheKeyUnavailableReason"))
                         .toString().toStdString()
                  << "' entries="
                  << plain_first_metrics.value(QStringLiteral("frameCacheEntries")).toULongLong()
                  << '\n';
        return fail("an ungraded exact-PTS frame should be eligible and inserted into the bounded cache");
      }
      const auto previous_plain_samples =
          plain_first_metrics.value(QStringLiteral("sampleCount")).toULongLong();
      TimelinePlaybackFrameTestAccess::submitAt(
          plain_playback, QColor(40, 130, 210), 3'000'000);
      if (!wait_until([&] {
            const auto metrics = plain_playback.frameProcessingMetrics();
            return TimelinePlaybackFrameTestAccess::idle(plain_playback) &&
                   metrics.value(QStringLiteral("sampleCount")).toULongLong() >
                       previous_plain_samples &&
                   metrics.value(QStringLiteral("frameCacheHit")).toBool();
          })) {
        return fail("repeating an ungraded exact-PTS frame should reuse the inserted preview");
      }
      const auto plain_reused_image =
          TimelinePlaybackFrameTestAccess::image(plain_playback);
      const auto plain_reused_hash = QCryptographicHash::hash(
          QByteArray(reinterpret_cast<const char*>(plain_reused_image.constBits()),
                     plain_reused_image.sizeInBytes()),
          QCryptographicHash::Sha256);
      if (plain_first_image.size() != QSize(640, 360) ||
          plain_reused_hash != plain_first_hash || published_timestamps.size() < 2 ||
          published_timestamps.last() != 3'000'000 ||
          plain_playback.processedFrameTimestampEstimated() ||
          published_timestamp_estimates.size() < 2 ||
          published_timestamp_estimates.last()) {
        return fail("ungraded cache reuse should preserve exact preview pixels and output dimensions");
      }
      plain_playback.attachVideoOutput(nullptr);
      if (!plain_playback.processedFrameSource().isEmpty() ||
          plain_playback.processedFrameTimestampUs() != -1 ||
          plain_playback.processedFrameTimestampEstimated())
        return fail("Clearing the displayed image should clear its timestamp and estimate flag too");
    }
    const auto source_path = temporary.filePath(QStringLiteral("frame-cache-source.mp4"));
    QFile source_file(source_path);
    if (!source_file.open(QIODevice::WriteOnly) || source_file.write("cache fixture") <= 0) {
      return fail("the processed-frame cache fixture source should be written");
    }
    source_file.close();
    TimelineDocumentController cache_document;
    // This fixture drives playback synchronization explicitly; document
    // notifications from setup must not race synthetic frame submissions.
    const QSignalBlocker cache_document_signal_blocker(&cache_document);
    static_cast<void>(cache_document_signal_blocker);
    cache_document.createTimeline(QStringLiteral("Processed Frame Cache"));
    cache_document.addClipFromGallery(
        QStringLiteral("video:frame-cache"), source_path,
        QStringLiteral("Frame Cache Source"), 1.0,
        QStringLiteral("poster://frame-cache"));
    cache_document.selectClip(0);
    QString cache_test_effect_id;
    for (const auto& preset_value : cache_document.effectStackPresets()) {
      const auto preset = preset_value.toMap();
      if (preset.value(QStringLiteral("name")).toString() ==
          QStringLiteral("Silver Monochrome")) {
        cache_test_effect_id =
            preset.value(QStringLiteral("presetId")).toString();
        break;
      }
    }
    if (cache_test_effect_id.isEmpty() ||
        !cache_document.applyEffectStackPreset(
            cache_test_effect_id, QStringList{}, true)) {
      return fail("the cache fixture should use the real Silver Monochrome Program effect");
    }
    TimelinePlaybackController cache_playback(&cache_document);
    QQuickItem cache_output;
    cache_output.setSize(QSizeF(640, 360));
    cache_playback.attachVideoOutput(&cache_output);
    TimelinePlaybackFrameTestAccess::submitWithoutTimestamp(
        cache_playback, QColor(24, 96, 220));
    if (!wait_until([&] {
          return !cache_playback.processedFrameSource().isEmpty() &&
                 TimelinePlaybackFrameTestAccess::idle(cache_playback);
        })) {
      return fail("the first non-PTS decoded Program frame should complete before cache reuse is measured");
    }
    const auto first_image = TimelinePlaybackFrameTestAccess::image(cache_playback);
    const auto first_hash = QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(first_image.constBits()),
                   first_image.sizeInBytes()),
        QCryptographicHash::Sha256);
    const auto first_metrics = cache_playback.frameProcessingMetrics();
    if (first_image.size() != QSize(640, 360) ||
        first_metrics.value(QStringLiteral("frameCacheHit")).toBool() ||
        !first_metrics.value(QStringLiteral("frameCacheEnabled")).toBool()) {
      return fail("a first non-PTS frame should render normally and enter the enabled cache by decoded content identity");
    }
    if (!first_metrics.value(QStringLiteral("frameCacheKeyEligible")).toBool() ||
        first_metrics.value(QStringLiteral("frameCacheIdentityKind")).toString() !=
            QStringLiteral("decoded-content")) {
      return fail("a frame without decoder PTS should use its decoded content as cache identity");
    }
    TimelinePlaybackFrameTestAccess::submitWithoutTimestamp(
        cache_playback, QColor(24, 96, 220));
    if (!wait_until([&] {
          return TimelinePlaybackFrameTestAccess::idle(cache_playback) &&
                 cache_playback.frameProcessingMetrics()
                         .value(QStringLiteral("frameCacheHit")).toBool();
        })) {
      return fail("repeating identical decoded pixels without PTS should reuse the processed frame");
    }
    const auto reused_image = TimelinePlaybackFrameTestAccess::image(cache_playback);
    const auto reused_hash = QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(reused_image.constBits()),
                   reused_image.sizeInBytes()),
        QCryptographicHash::Sha256);
    const auto reused_metrics = cache_playback.frameProcessingMetrics();
    if (reused_hash != first_hash ||
        reused_metrics.value(QStringLiteral("lastFrameTotalMs")).toDouble() >=
            first_metrics.value(QStringLiteral("lastFrameTotalMs")).toDouble() ||
        reused_metrics.value(QStringLiteral("frameCacheHits")).toULongLong() < 1U ||
        reused_metrics.value(QStringLiteral("frameCacheEntries")).toULongLong() == 0U ||
        reused_metrics.value(QStringLiteral("frameCacheBytes")).toULongLong() >
            128ULL * 1024ULL * 1024ULL) {
      return fail("cache reuse must preserve exact published pixels and report finite cache residency");
    }
    const auto before_different_content =
        reused_metrics.value(QStringLiteral("sampleCount")).toULongLong();
    const auto before_different_content_revision =
        TimelinePlaybackFrameTestAccess::processedSourceRevision(cache_playback);
    TimelinePlaybackFrameTestAccess::submitWithoutTimestamp(
        cache_playback, QColor(240, 180, 12));
    if (!wait_until([&] {
          return TimelinePlaybackFrameTestAccess::idle(cache_playback) &&
                 !cache_playback.processedFrameSource().isEmpty() &&
                 TimelinePlaybackFrameTestAccess::processedSourceRevision(
                     cache_playback) > before_different_content_revision;
        })) {
      const auto after_metrics = cache_playback.frameProcessingMetrics();
      std::cerr << "No-PTS content completion: beforeSample="
                << before_different_content
                << " afterSample="
                << after_metrics.value(QStringLiteral("sampleCount")).toULongLong()
                << " sourceRevision=" << before_different_content_revision << "->"
                << TimelinePlaybackFrameTestAccess::processedSourceRevision(cache_playback)
                << " cacheHit="
                << after_metrics.value(QStringLiteral("frameCacheHit")).toBool()
                << " keyEligible="
                << after_metrics.value(QStringLiteral("frameCacheKeyEligible")).toBool()
                << " identity='"
                << after_metrics.value(QStringLiteral("frameCacheIdentityKind"))
                       .toString().toStdString()
                << "' reason='"
                << after_metrics.value(QStringLiteral("frameCacheKeyUnavailableReason"))
                       .toString().toStdString()
                << "' sourceKey='"
                << after_metrics.value(QStringLiteral("sourceKey"))
                       .toString().toStdString()
                << "' idle=" << TimelinePlaybackFrameTestAccess::idle(cache_playback)
                << '\n';
      return fail("a different decoded frame without PTS should complete before collision safety is checked");
    }
    const auto different_content_metrics = cache_playback.frameProcessingMetrics();
    const auto different_content_image =
        TimelinePlaybackFrameTestAccess::image(cache_playback);
    const auto different_content_hash = QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(different_content_image.constBits()),
                   different_content_image.sizeInBytes()),
        QCryptographicHash::Sha256);
    if (different_content_metrics.value(QStringLiteral("frameCacheHit")).toBool() ||
        different_content_hash == first_hash) {
      return fail("different decoded content without PTS must miss and preserve its distinct pixels");
    }
    const auto before_original_content_revision =
        TimelinePlaybackFrameTestAccess::processedSourceRevision(cache_playback);
    TimelinePlaybackFrameTestAccess::submitWithoutTimestamp(
        cache_playback, QColor(24, 96, 220));
    if (!wait_until([&] {
          const auto metrics = cache_playback.frameProcessingMetrics();
          return TimelinePlaybackFrameTestAccess::idle(cache_playback) &&
                 !cache_playback.processedFrameSource().isEmpty() &&
                 TimelinePlaybackFrameTestAccess::processedSourceRevision(
                     cache_playback) > before_original_content_revision &&
                 metrics.value(QStringLiteral("frameCacheHit")).toBool();
        })) {
      return fail("returning to an earlier decoded-content identity should reuse its bounded cached preview");
    }
    const auto submit_same_content = [&](QVariantMap& measured) {
      const auto previous_metrics = cache_playback.frameProcessingMetrics();
      const auto previous_samples = previous_metrics
                                        .value(QStringLiteral("sampleCount"))
                                        .toULongLong();
      const auto previous_source_key =
          previous_metrics.value(QStringLiteral("sourceKey")).toString();
      TimelinePlaybackFrameTestAccess::submitWithoutTimestamp(
          cache_playback, QColor(24, 96, 220));
      return wait_until([&] {
        const auto metrics = cache_playback.frameProcessingMetrics();
        const auto sample_count =
            metrics.value(QStringLiteral("sampleCount")).toULongLong();
        const auto source_key =
            metrics.value(QStringLiteral("sourceKey")).toString();
        const auto completed_sample = sample_count > previous_samples ||
            (!source_key.isEmpty() && source_key != previous_source_key &&
             sample_count > 0U);
        if (!TimelinePlaybackFrameTestAccess::idle(cache_playback) ||
            !completed_sample) {
          return false;
        }
        measured = metrics;
        return true;
      });
    };
    const auto expect_same_content_cache_miss = [&](const QSize& expected_size) {
      QVariantMap metrics;
      if (!submit_same_content(metrics)) {
        return false;
      }
      return !metrics.value(QStringLiteral("frameCacheHit")).toBool() &&
             TimelinePlaybackFrameTestAccess::image(cache_playback).size() ==
                 expected_size;
    };

    const auto original_source_revision =
        TimelinePlaybackFrameTestAccess::sourceRevision(cache_playback);
    if (!source_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        source_file.write("replacement media revision with a different size") <= 0) {
      return fail("the same-path source replacement fixture should be rewritten");
    }
    source_file.close();
    TimelinePlaybackFrameTestAccess::sync(cache_playback);
    const auto replacement_source_revision =
        TimelinePlaybackFrameTestAccess::sourceRevision(cache_playback);
    QVariantMap replacement_metrics;
    const auto replacement_processed = submit_same_content(replacement_metrics);
    const auto replacement_image =
        TimelinePlaybackFrameTestAccess::image(cache_playback);
    if (original_source_revision == replacement_source_revision) {
      std::cerr << "Same-path source revision did not change: before='"
                << original_source_revision.toStdString() << "' after='"
                << replacement_source_revision.toStdString() << "'\n";
      return fail("the bound source revision should change after a same-path replacement");
    }
    if (!replacement_processed) {
      std::cerr << "Same-path replacement frame did not complete; metrics="
                << replacement_metrics.value(QStringLiteral("sampleCount"))
                       .toString()
                       .toStdString()
                << '\n';
      return fail("the replacement-source content-identified frame should complete");
    }
    if (replacement_metrics.value(QStringLiteral("frameCacheHit")).toBool()) {
      std::cerr << "Same-path replacement unexpectedly hit the cache; source revision='"
                << replacement_source_revision.toStdString()
                << "' cache hits="
                << replacement_metrics.value(QStringLiteral("frameCacheHits"))
                       .toULongLong()
                << " misses="
                << replacement_metrics.value(QStringLiteral("frameCacheMisses"))
                       .toULongLong()
                << '\n';
      return fail("a same-path source replacement should miss the old processed frame");
    }
    if (replacement_image.size() != QSize(640, 360)) {
      std::cerr << "Same-path replacement output size was "
                << replacement_image.width() << 'x'
                << replacement_image.height() << "\n";
      return fail("the replacement-source frame should retain its 640x360 target");
    }
    const auto clip_id = cache_document.programClip()
                             .value(QStringLiteral("clipId"))
                             .toString();
    const auto grade_change =
        cache_document.setClipShotMatchGainsForAutomation(
            clip_id, 1.35, 1.0, 1.0);
    if (!grade_change.value(QStringLiteral("ok")).toBool()) {
      return fail("the processed-frame fixture should accept a real shot-match grade change");
    }
    TimelinePlaybackFrameTestAccess::sync(cache_playback);
    if (!expect_same_content_cache_miss(QSize(640, 360))) {
      return fail("changing the Program grade should invalidate a content-identified processed frame");
    }

    cache_playback.setTimelineEffectsBypassed(true);
    if (!expect_same_content_cache_miss(QSize(640, 360))) {
      return fail("bypassing the Program effect should invalidate a content-identified processed frame");
    }

    cache_playback.setPreviewQualityMode(QStringLiteral("half"));
    if (TimelinePlaybackFrameTestAccess::targetSize(cache_playback) !=
        QSize(320, 180) ||
        !expect_same_content_cache_miss(QSize(320, 180))) {
      return fail("half preview quality should process at half the attached 640x360 item size and miss the full-size cache entry");
    }
    cache_playback.setPreviewQualityMode(QStringLiteral("quarter"));
    if (TimelinePlaybackFrameTestAccess::targetSize(cache_playback) !=
        QSize(160, 90) ||
        !expect_same_content_cache_miss(QSize(160, 90))) {
      return fail("quarter preview quality should process at quarter the attached 640x360 item size and miss the half-size cache entry");
    }
    cache_playback.setPreviewQualityMode(QStringLiteral("full"));
    std::cout << "Decoded-content frame reuse: first totalMs="
              << first_metrics.value(QStringLiteral("lastFrameTotalMs")).toDouble()
              << " reused totalMs="
              << reused_metrics.value(QStringLiteral("lastFrameTotalMs")).toDouble()
              << " lookupMs="
              << reused_metrics.value(QStringLiteral("lastFrameCacheLookupMs")).toDouble()
              << " residentBytes="
              << reused_metrics.value(QStringLiteral("frameCacheBytes")).toULongLong()
              << " pixelSha256=" << reused_hash.toHex().toStdString() << '\n';

    // A composited range already contains the clip grade and effects. Reduced
    // preview modes must resize these pixels without applying that work twice.
    cache_playback.setTimelineEffectsBypassed(false);
    const auto ranges = cache_document.renderCacheRanges();
    if (ranges.isEmpty()) return fail("the reduced composite fixture needs a cache range");
    const auto cache_directory = temporary.filePath(QStringLiteral("cache/timeline-render-cache"));
    if (!QDir().mkpath(cache_directory)) return fail("the composite cache directory should be created");
    const auto timeline_id = cache_document.timelineAssetId().mid(QStringLiteral("timeline:").size());
    const auto signature = ranges.first().toMap().value(QStringLiteral("signature")).toString();
    QFile composite_file(QDir(cache_directory).filePath(
        QStringLiteral("%1-%2.mp4").arg(timeline_id, signature)));
    if (!composite_file.open(QIODevice::WriteOnly) || composite_file.write("composite fixture") <= 0) {
      return fail("the reduced composite fixture should be written");
    }
    composite_file.close();
    for (const auto& quality : {QStringLiteral("half"), QStringLiteral("quarter")}) {
      cache_playback.setPreviewQualityMode(quality);
      QVariantMap metrics;
      if (!cache_playback.usingRenderCache() || !submit_same_content(metrics)) {
        return fail("reduced-resolution composite frames should reach Program processing");
      }
      const auto image = TimelinePlaybackFrameTestAccess::image(cache_playback);
      const auto expected_size = quality == QStringLiteral("half")
                                     ? QSize(320, 180) : QSize(160, 90);
      if (image.size() != expected_size || image.pixelColor(0, 0) != QColor(24, 96, 220)) {
        return fail("reduced composite frames should preserve cached colors without applying the monochrome effect or grade again");
      }
    }
  }
  {
    const auto source_path =
        temporary.filePath(QStringLiteral("same-path-lut-source.mp4"));
    QFile source_file(source_path);
    if (!source_file.open(QIODevice::WriteOnly) ||
        source_file.write("same path LUT source") <= 0) {
      return fail("the same-path LUT source should be written");
    }
    source_file.close();
    const auto lut_path = temporary.filePath(QStringLiteral("same-path-look.cube"));
    const QByteArray blue_lut(
        "TITLE \"Blue\"\nLUT_3D_SIZE 2\n"
        "0 0 1\n0 0 1\n0 0 1\n0 0 1\n"
        "0 0 1\n0 0 1\n0 0 1\n0 0 1\n");
    const QByteArray red_lut(
        "TITLE \"Red!\"\nLUT_3D_SIZE 2\n"
        "1 0 0\n1 0 0\n1 0 0\n1 0 0\n"
        "1 0 0\n1 0 0\n1 0 0\n1 0 0\n");
    QFile lut_file(lut_path);
    if (blue_lut.size() != red_lut.size() ||
        !lut_file.open(QIODevice::WriteOnly) ||
        lut_file.write(blue_lut) != blue_lut.size()) {
      return fail("the first same-path CUBE LUT should be written at the expected size");
    }
    lut_file.close();
    TimelineDocumentController lut_document;
    // The fixture explicitly syncs the playback controller after setup; keep
    // document setup notifications from racing that synthetic first frame.
    const QSignalBlocker lut_document_signal_blocker(&lut_document);
    static_cast<void>(lut_document_signal_blocker);
    lut_document.createTimeline(QStringLiteral("Same-Path LUT Revision"));
    lut_document.addClipFromGallery(
        QStringLiteral("video:same-path-lut"), source_path,
        QStringLiteral("Same-Path LUT Source"), 1.0,
        QStringLiteral("poster://same-path-lut"));
    const auto clip_id = lut_document.programClip()
                             .value(QStringLiteral("clipId")).toString();
    if (!lut_document.setClipColorLutForAutomation(clip_id, lut_path)
             .value(QStringLiteral("ok")).toBool()) {
      return fail("the timeline Program should accept the same-path CUBE LUT fixture");
    }
    TimelinePlaybackController lut_playback(&lut_document);
    QQuickItem lut_output;
    lut_output.setSize(QSizeF(64, 64));
    lut_playback.attachVideoOutput(&lut_output);
    const auto submit_lut_frame = [&](QVariantMap* completed_metrics = nullptr) {
      const auto before = lut_playback.frameProcessingMetrics();
      const auto before_count = before.value(QStringLiteral("sampleCount")).toULongLong();
      const auto before_key = before.value(QStringLiteral("sourceKey")).toString();
      TimelinePlaybackFrameTestAccess::submitWithoutTimestamp(
          lut_playback, QColor(Qt::white));
      if (!wait_until([&] {
            const auto metrics = lut_playback.frameProcessingMetrics();
            const auto sample_count =
                metrics.value(QStringLiteral("sampleCount")).toULongLong();
            const auto source_key =
                metrics.value(QStringLiteral("sourceKey")).toString();
            const auto completed = sample_count > before_count ||
                (!source_key.isEmpty() && source_key != before_key &&
                 sample_count > 0U);
            if (!TimelinePlaybackFrameTestAccess::idle(lut_playback) || !completed) {
              return false;
            }
            if (completed_metrics != nullptr) {
              *completed_metrics = metrics;
            }
            return true;
          })) {
        return QImage{};
      }
      return TimelinePlaybackFrameTestAccess::image(lut_playback);
    };
    QVariantMap blue_metrics;
    const auto blue_image = submit_lut_frame(&blue_metrics);
    if (blue_image.size() != QSize(64, 36)) {
      std::cerr << "Same-path LUT first output size=" << blue_image.width() << 'x'
                << blue_image.height() << " source='"
                << lut_playback.processedFrameSource().toStdString()
                << "' metrics sampleCount="
                << blue_metrics.value(QStringLiteral("sampleCount")).toULongLong()
                << " cacheHit="
                << blue_metrics.value(QStringLiteral("frameCacheHit")).toBool()
                << " reason='"
                << blue_metrics.value(QStringLiteral("frameCacheKeyUnavailableReason"))
                       .toString().toStdString()
                << "'\n";
      return fail("the first same-path LUT revision should fit the 16:9 source inside the 64x64 preview");
    }
    const auto blue_pixel = blue_image.pixelColor(32, 18);
    if (blue_image.isNull() || blue_pixel.blue() < blue_pixel.red() + 100 ||
        blue_metrics.value(QStringLiteral("frameCacheHit")).toBool()) {
      return fail("the first same-path LUT revision should render its blue transform through a cache miss");
    }
    if (!lut_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        lut_file.write(red_lut) != red_lut.size()) {
      return fail("the same-path CUBE LUT should be replaceable with equal-size red content");
    }
    lut_file.close();
    TimelinePlaybackFrameTestAccess::sync(lut_playback);
    QVariantMap red_metrics;
    const auto red_image = submit_lut_frame(&red_metrics);
    if (red_image.size() != QSize(64, 36)) {
      std::cerr << "Same-path LUT replacement output size=" << red_image.width()
                << 'x' << red_image.height() << " source='"
                << lut_playback.processedFrameSource().toStdString()
                << "' metrics sampleCount="
                << red_metrics.value(QStringLiteral("sampleCount")).toULongLong()
                << " cacheHit="
                << red_metrics.value(QStringLiteral("frameCacheHit")).toBool()
                << " reason='"
                << red_metrics.value(QStringLiteral("frameCacheKeyUnavailableReason"))
                       .toString().toStdString()
                << "'\n";
      return fail("the replacement same-path LUT should preserve 16:9 fitting inside the 64x64 preview");
    }
    const auto red_pixel = red_image.pixelColor(32, 18);
    if (red_image.isNull() || red_pixel.red() < red_pixel.blue() + 100 ||
        red_metrics.value(QStringLiteral("frameCacheHit")).toBool()) {
      return fail("a same-path equal-size LUT replacement should invalidate the grade identity, cache miss, and update Program pixels");
    }
  }
  {
    const auto source_path =
        temporary.filePath(QStringLiteral("paused-refresh-source.mp4"));
    QFile source_file(source_path);
    if (!source_file.open(QIODevice::WriteOnly) ||
        source_file.write("paused refresh fixture") <= 0) {
      return fail("the paused preview refresh source should be written");
    }
    source_file.close();
    TimelineDocumentController refresh_document;
    refresh_document.createTimeline(QStringLiteral("Paused Preview Refresh"));
    refresh_document.addClipFromGallery(
        QStringLiteral("video:paused-refresh"), source_path,
        QStringLiteral("Paused Refresh Source"), 1.0,
        QStringLiteral("poster://paused-refresh"));
    TimelinePlaybackController refresh_playback(&refresh_document);
    QQuickItem refresh_output;
    refresh_output.setSize(QSizeF(640, 360));
    TimelinePlaybackFrameTestAccess::syncPausedSameSourceWithCachedFrame(
        refresh_playback, &refresh_output, QColor(46, 132, 214));
    if (!wait_until([&] {
          return !refresh_playback.processedFrameSource().isEmpty() &&
                 TimelinePlaybackFrameTestAccess::idle(refresh_playback);
        })) {
      return fail("a paused same-source settings change should reprocess the cached sink frame after position synchronization");
    }
    const auto refreshed_image =
        TimelinePlaybackFrameTestAccess::image(refresh_playback);
    if (refreshed_image.isNull() || refreshed_image.size() != QSize(640, 360)) {
      return fail("the paused same-source refresh should publish a full-size processed Program frame");
    }
  }
  {
    const auto source_path =
        temporary.filePath(QStringLiteral("same-position-seek-source.mp4"));
    QFile source_file(source_path);
    if (!source_file.open(QIODevice::WriteOnly) ||
        source_file.write("same-position seek fixture") <= 0) {
      return fail("the same-position paused seek fixture source should be written");
    }
    source_file.close();
    TimelineDocumentController seek_document;
    seek_document.createTimeline(QStringLiteral("Same Position Paused Seek"));
    seek_document.addClipFromGallery(
        QStringLiteral("video:same-position-seek"), source_path,
        QStringLiteral("Same Position Seek Source"), 1.0,
        QStringLiteral("poster://same-position-seek"));
    seek_document.selectClip(0);
    TimelinePlaybackController seek_playback(&seek_document);
    QQuickItem seek_output;
    seek_output.setSize(QSizeF(640, 360));
    TimelinePlaybackFrameTestAccess::prepareSamePositionPausedSeek(
        seek_playback, &seek_output, QColor(200, 35, 25), 0);
    {
      WorkerGate gate;
      TimelinePlaybackFrameTestAccess::submitAndCacheAt(
          seek_playback, QColor(200, 35, 25), 0);
      if (TimelinePlaybackFrameTestAccess::idle(seek_playback))
        return fail("the injected first-frame work should remain incomplete during the same-position seek");
      seek_playback.seekSequenceSeconds(0.0);
      if (TimelinePlaybackFrameTestAccess::awaitingPositionSync(seek_playback))
        return fail("a seek to the already-loaded zero position should not wait for a decoder acknowledgment");
      gate.open();
      if (!wait_until([&] {
            return TimelinePlaybackFrameTestAccess::idle(seek_playback) &&
                   seek_playback.processedFrameTimestampUs() == 0 &&
                   !seek_playback.processedFrameSource().isEmpty();
          })) {
        return fail("an invalidated incomplete frame should be recovered from the sink after a same-position paused seek without a new decoder signal");
      }
      const auto recovered = TimelinePlaybackFrameTestAccess::image(seek_playback);
      if (recovered.isNull() || recovered.pixelColor(320, 180).red() < 180)
        return fail("the recovered same-position frame should publish the sink's current pixels");
    }
  }
  {
    const auto source_path =
        temporary.filePath(QStringLiteral("unknown-pts-source-clock.mp4"));
    QFile source_file(source_path);
    if (!source_file.open(QIODevice::WriteOnly) ||
        source_file.write("unknown PTS source clock fixture") <= 0) {
      return fail("the unknown-PTS source clock fixture should be written");
    }
    source_file.close();
    TimelineDocumentController clock_document;
    clock_document.createTimeline(QStringLiteral("Unknown PTS Source Clock"));
    clock_document.addClipFromGallery(
        QStringLiteral("video:unknown-pts-source-clock"), source_path,
        QStringLiteral("Unknown PTS Source Clock"), 2.0,
        QStringLiteral("poster://unknown-pts-source-clock"));
    clock_document.selectClip(0);
    TimelinePlaybackController clock_playback(&clock_document);
    QQuickItem clock_output;
    clock_output.setSize(QSizeF(640, 360));
    clock_playback.attachVideoOutput(&clock_output);
    QVector<qint64> published_source_times;
    QVector<bool> published_source_time_estimates;
    QObject::connect(&clock_playback,
                     &TimelinePlaybackController::processedFrameSourceChanged,
                     [&] {
      if (!clock_playback.processedFrameSource().isEmpty()) {
        published_source_times.append(clock_playback.processedFrameTimestampUs());
        published_source_time_estimates.append(
            clock_playback.processedFrameTimestampEstimated());
      }
    });
    TimelinePlaybackFrameTestAccess::seekDocumentClock(clock_playback, 0.25);
    {
      WorkerGate gate;
      TimelinePlaybackFrameTestAccess::submitAndCacheWithoutTimestamp(
          clock_playback, QColor(180, 30, 20));
      if (TimelinePlaybackFrameTestAccess::idle(clock_playback))
        return fail("the unknown-PTS first frame should wait behind the test worker gate");
      TimelinePlaybackFrameTestAccess::seekDocumentClock(clock_playback, 0.75);
      TimelinePlaybackFrameTestAccess::submitAndCacheWithoutTimestamp(
          clock_playback, QColor(20, 80, 190));
      TimelinePlaybackFrameTestAccess::seekDocumentClock(clock_playback, 0.95);
      gate.open();
      if (!wait_until([&] {
            return TimelinePlaybackFrameTestAccess::idle(clock_playback) &&
                   published_source_times.size() >= 2;
          })) {
        return fail("unknown-PTS active and queued frames should both publish their arrival-time source clock");
      }
    }
    if (published_source_times.size() != 2 ||
        published_source_times.at(0) != 250'000 ||
        published_source_times.at(1) != 750'000 ||
        published_source_time_estimates.size() != 2 ||
        !published_source_time_estimates.at(0) ||
        !published_source_time_estimates.at(1) ||
        clock_playback.processedFrameTimestampUs() != 750'000 ||
        !clock_playback.processedFrameTimestampEstimated() ||
        clock_document.sequencePlaying()) {
      return fail("unknown-PTS publication should preserve each queued arrival clock atomically while the sequence remains paused");
    }
  }
  {
    const auto source_path = temporary.filePath(QStringLiteral("effect-preview-source.mp4"));
    QFile source_file(source_path);
    if (!source_file.open(QIODevice::WriteOnly) || source_file.write("fixture") <= 0) {
      return fail("the timeline video effect source should be written");
    }
    source_file.close();
    TimelineDocumentController effect_document;
    effect_document.createTimeline(QStringLiteral("Built-in Effect Preview"));
    effect_document.addClipFromGallery(
        QStringLiteral("video:effect-preview"), source_path,
        QStringLiteral("Effect Preview Source"), 1.0, QStringLiteral("poster://effect-preview"));
    QString silver_monochrome_id;
    for (const auto& preset_value : effect_document.effectStackPresets()) {
      const auto preset = preset_value.toMap();
      if (preset.value(QStringLiteral("name")).toString() == QStringLiteral("Silver Monochrome")) {
        silver_monochrome_id = preset.value(QStringLiteral("presetId")).toString();
        break;
      }
    }
    if (silver_monochrome_id.isEmpty() ||
        !effect_document.applyEffectStackPreset(silver_monochrome_id, QStringList{}, true)) {
      return fail("the built-in Silver Monochrome look should apply to a timeline clip");
    }
    TimelinePlaybackController effect_preview(&effect_document);
    QQuickItem effect_output;
    effect_output.setSize(QSizeF(64, 64));
    effect_preview.attachVideoOutput(&effect_output);
    TimelinePlaybackFrameTestAccess::submit(effect_preview, QColor(220, 35, 18));
    if (!wait_until([&] {
          return !effect_preview.processedFrameSource().isEmpty() &&
                 TimelinePlaybackFrameTestAccess::idle(effect_preview);
        })) {
      return fail("the built-in look should process and publish a Program preview frame");
    }
    const auto rendered = TimelinePlaybackFrameTestAccess::image(effect_preview);
    const auto sample = rendered.pixelColor(32, 32);
    if (rendered.size() != QSize(64, 64) ||
        qAbs(sample.red() - sample.green()) > 6 ||
        qAbs(sample.green() - sample.blue()) > 6) {
      return fail("the Silver Monochrome look should render grayscale pixels through the real preview frame path");
    }
    const auto proof_directory =
        qEnvironmentVariable("PHOTO_EDITOR_TIMELINE_EFFECT_PROOF_DIR").trimmed();
    if (!proof_directory.isEmpty()) {
      if (!QDir().mkpath(proof_directory) ||
          !rendered.save(QDir(proof_directory).filePath(
              QStringLiteral("silver-monochrome-program-preview.png")))) {
        return fail("the opt-in Silver Monochrome Program preview capture should be saved");
      }
    }
  }
  {
    TimelinePlaybackController late_playback(&document);
    QQuickItem late_output;
    late_output.setSize(QSizeF(64, 64));
    TimelinePlaybackFrameTestAccess::attachWithCachedFrame(late_playback, &late_output, Qt::green);
    if (!wait_until([&] { return !late_playback.processedFrameSource().isEmpty() &&
                               TimelinePlaybackFrameTestAccess::idle(late_playback); }) ||
        TimelinePlaybackFrameTestAccess::image(late_playback).pixelColor(32, 32).green() < 200) {
      return fail("A decoder frame received before the monitor attaches should populate its paused preview");
    }
    late_playback.attachVideoOutput(nullptr);
    if (!late_playback.processedFrameSource().isEmpty()) {
      return fail("A cleared preview should expose an empty source rather than a transparent frame URL");
    }
  }
  TimelinePlaybackController playback(&document);
  QQuickItem output;
  output.setSize(QSizeF(64, 64));
  playback.attachVideoOutput(&output);
  QVector<QColor> presented;
  QObject::connect(&playback, &TimelinePlaybackController::processedFrameSourceChanged, [&] {
    const auto image = TimelinePlaybackFrameTestAccess::image(playback);
    if (image.size() == QSize(64, 64)) presented.append(image.pixelColor(32, 32));
  });
  int ticks = 0;
  QTimer heartbeat;
  heartbeat.setInterval(1);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start();
  {
    const auto headless_was_set = qEnvironmentVariableIsSet(
        "PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");
    const auto previous_headless_value = qgetenv(
        "PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");
    qunsetenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");
    [[maybe_unused]] const auto restore_headless = qScopeGuard([&] {
      if (headless_was_set)
        qputenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS", previous_headless_value);
      else
        qunsetenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");
    });
    {
      QQuickItem seek_output;
      seek_output.setSize(QSizeF(64, 64));
      TimelinePlaybackController seek_playback(&document);
      if (!TimelinePlaybackFrameTestAccess::nativeEnabled(seek_playback))
        return fail("Paused seek scheduling fixture must connect the native QMediaPlayer signal path");
      seek_playback.attachVideoOutput(&seek_output);
      QVector<QColor> seek_presented;
      QObject::connect(&seek_playback,
                       &TimelinePlaybackController::processedFrameSourceChanged,
                       [&] {
        const auto image = TimelinePlaybackFrameTestAccess::image(seek_playback);
        if (image.size() == QSize(64, 64))
          seek_presented.append(image.pixelColor(32, 32));
      });

      seek_playback.attachVideoOutput(nullptr);
      TimelinePlaybackFrameTestAccess::setSinkFrame(seek_playback, Qt::red);
      const auto previous = seek_presented.size();
      TimelinePlaybackFrameTestAccess::beginPositionSync(seek_playback, 300);
      seek_playback.attachVideoOutput(&seek_output);
      if (!TimelinePlaybackFrameTestAccess::acknowledgePosition(seek_playback, 300) ||
          !wait_until([&] { return TimelinePlaybackFrameTestAccess::idle(seek_playback); }) ||
          seek_presented.size() != previous ||
          !seek_playback.processedFrameSource().isEmpty()) {
        return fail("A frame cached before a paused seek must not be republished as the seek result");
      }

      TimelinePlaybackFrameTestAccess::beginPositionSync(seek_playback, 600);
      TimelinePlaybackFrameTestAccess::submit(seek_playback, Qt::green);
      if (!TimelinePlaybackFrameTestAccess::hasSeekCandidate(seek_playback) ||
          !TimelinePlaybackFrameTestAccess::acknowledgePosition(seek_playback, 600) ||
          !wait_until([&] { return TimelinePlaybackFrameTestAccess::idle(seek_playback); }) ||
          seek_presented.size() != previous + 1 || seek_presented.last().green() < 200) {
        return fail("A fresh paused-seek frame arriving before position acknowledgment should publish after acknowledgment");
      }

      TimelinePlaybackFrameTestAccess::beginPositionSync(seek_playback, 900);
      TimelinePlaybackFrameTestAccess::submit(seek_playback, Qt::yellow);
      TimelinePlaybackFrameTestAccess::queuePosition(seek_playback, 900);
      const auto before_converged_queue = seek_presented.size();
      if (!TimelinePlaybackFrameTestAccess::acknowledgePosition(seek_playback, 900) ||
          !wait_until([&] { return TimelinePlaybackFrameTestAccess::idle(seek_playback); }) ||
          seek_presented.size() != before_converged_queue + 1 ||
          seek_presented.last().red() < 200 || seek_presented.last().green() < 200 ||
          TimelinePlaybackFrameTestAccess::awaitingPositionSync(seek_playback)) {
        return fail("A queued seek matching its acknowledgment should consume the candidate without waiting for another position signal");
      }

      TimelinePlaybackFrameTestAccess::beginPositionSync(seek_playback, 1200);
      TimelinePlaybackFrameTestAccess::submit(seek_playback, Qt::red);
      TimelinePlaybackFrameTestAccess::queuePosition(seek_playback, 1500);
      const auto before_superseded = seek_presented.size();
      if (!TimelinePlaybackFrameTestAccess::acknowledgePosition(seek_playback, 1200) ||
          !TimelinePlaybackFrameTestAccess::awaitingPositionSync(seek_playback) ||
          TimelinePlaybackFrameTestAccess::awaitedPosition(seek_playback) != 1500 ||
          TimelinePlaybackFrameTestAccess::hasSeekCandidate(seek_playback) ||
          seek_presented.size() != before_superseded) {
        return fail("A superseded paused-seek candidate must be discarded while the newest target remains pending");
      }
      TimelinePlaybackFrameTestAccess::submit(seek_playback, Qt::blue);
      if (!TimelinePlaybackFrameTestAccess::acknowledgePosition(seek_playback, 1500) ||
          !wait_until([&] { return TimelinePlaybackFrameTestAccess::idle(seek_playback); }) ||
          seek_presented.size() != before_superseded + 1 ||
          seek_presented.last().blue() < 200) {
        return fail("The latest queued paused seek should publish only its own candidate frame");
      }
    }
  }
  for (int round = 0; round < 5; ++round) {
    WorkerGate gate;
    const auto previous = presented.size();
    const auto dropped = playback.droppedFrameCount();
    TimelinePlaybackFrameTestAccess::submit(playback, Qt::red);
    for (int frame = 0; frame < 59; ++frame)
      TimelinePlaybackFrameTestAccess::submit(playback, Qt::green);
    TimelinePlaybackFrameTestAccess::submit(playback, Qt::blue);
    const auto previous_ticks = ticks;
    if (!wait_until([&] { return ticks >= previous_ticks + 3; })) return fail("Frame pressure blocked the event loop");
    if (!TimelinePlaybackFrameTestAccess::pending(playback)) return fail("Missing newest pending frame");
    gate.open();
    if (!wait_until([&] { return TimelinePlaybackFrameTestAccess::idle(playback); })) return fail("Frame work did not drain");
    if (presented.size() != previous + 2 || presented.at(previous).red() < 200 || presented.last().blue() < 200)
      return fail("New arrivals starved active-frame presentation or superseded pending frames were rendered");
    if (playback.droppedFrameCount() - dropped != 59) return fail("Only overwritten pending frames should count as dropped");
  }
  for (int invalidation = 0; invalidation < 6; ++invalidation) {
    playback.attachVideoOutput(&output);
    WorkerGate gate;
    TimelinePlaybackFrameTestAccess::submit(playback, Qt::red);
    TimelinePlaybackFrameTestAccess::submit(playback, Qt::green);
    const auto previous = presented.size();
    switch (invalidation) {
      case 0: playback.seekSequenceSeconds(0.0); break;
      case 1: playback.attachVideoOutput(nullptr); break;
      case 2: TimelinePlaybackFrameTestAccess::changeGrade(playback); break;
      case 3: TimelinePlaybackFrameTestAccess::changeSource(playback); break;
      case 4: playback.stepFrames(1); break;
      case 5: playback.setWorkspaceActive(false); break;
    }
    if (TimelinePlaybackFrameTestAccess::pending(playback)) return fail("Invalidation retained a stale pending frame");
    gate.open();
    if (!wait_until([&] { return TimelinePlaybackFrameTestAccess::idle(playback); }) || presented.size() != previous)
      return fail("A stale frame published after seek, detach, grade/source change, or frame step");
    if (invalidation == 5) {
      TimelinePlaybackFrameTestAccess::submit(playback, Qt::green);
      if (!TimelinePlaybackFrameTestAccess::idle(playback) || presented.size() != previous)
        return fail("A hidden workspace accepted a decoded frame");
      playback.setWorkspaceActive(true);
    }
  }
  playback.attachVideoOutput(&output);
  TimelinePlaybackFrameTestAccess::submit(playback, Qt::blue);
  if (!wait_until([&] { return TimelinePlaybackFrameTestAccess::idle(playback); }) || presented.last().blue() < 200)
    return fail("Playback did not resume after invalidation");
  const auto previous = presented.size();
  bool submitted_from_callback = false;
  const auto reentrant = QObject::connect(&playback, &TimelinePlaybackController::processedFrameSourceChanged, [&] {
    if (!submitted_from_callback) {
      submitted_from_callback = true;
      TimelinePlaybackFrameTestAccess::submit(playback, Qt::blue);
    }
  });
  TimelinePlaybackFrameTestAccess::submit(playback, Qt::red);
  if (!wait_until([&] { return TimelinePlaybackFrameTestAccess::idle(playback); }) || presented.size() != previous + 2)
    return fail("Publication callback lost or concurrently started a frame");
  QObject::disconnect(reentrant);
  {
    WorkerGate gate;
    auto disposable = std::make_unique<TimelinePlaybackController>(&document);
    disposable->attachVideoOutput(&output);
    const auto epoch = TimelinePlaybackFrameTestAccess::epoch(*disposable);
    const auto before = epoch->load();
    TimelinePlaybackFrameTestAccess::submit(*disposable, Qt::red);
    disposable.reset();
    if (epoch->load() == before) return fail("Controller destruction did not cancel frame work");
    gate.open();
    pool->waitForDone();
  }
  {
    TimelinePlaybackController scene_playback(&document);
    scene_playback.attachVideoOutput(&output);
    QVector<qint64> scene_timestamps;
    QVector<bool> scene_timestamp_estimates;
    QObject::connect(
        &scene_playback,
        &TimelinePlaybackController::processedFrameSourceChanged,
        [&] {
          if (!scene_playback.processedFrameSource().isEmpty()) {
            scene_timestamps.append(scene_playback.processedFrameTimestampUs());
            scene_timestamp_estimates.append(
                scene_playback.processedFrameTimestampEstimated());
          }
        });
    constexpr qint64 kTrimmedSourceFrameTimestampUs = 3'456'000;
    TimelinePlaybackFrameTestAccess::submitScenePreviewAt(
        scene_playback, QColor(212, 42, 118), kTrimmedSourceFrameTimestampUs);
    if (!wait_until([&] {
          return TimelinePlaybackFrameTestAccess::idle(scene_playback) &&
                 scene_playback.processedFrameTimestampUs() ==
                     kTrimmedSourceFrameTimestampUs;
        })) {
      return fail("A cached scene frame should bypass the native seek handshake and publish at its original trimmed-source time");
    }
    const auto scene_image = TimelinePlaybackFrameTestAccess::image(scene_playback);
    const auto scene_hash = QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(scene_image.constBits()),
                   scene_image.sizeInBytes()),
        QCryptographicHash::Sha256);
    const auto scene_revision = TimelinePlaybackFrameTestAccess::processedSourceRevision(
        scene_playback);
    TimelinePlaybackFrameTestAccess::submit(scene_playback, QColor(24, 208, 76));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const auto image_after_native_arrival =
        TimelinePlaybackFrameTestAccess::image(scene_playback);
    const auto image_after_native_hash = QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(image_after_native_arrival.constBits()),
                   image_after_native_arrival.sizeInBytes()),
        QCryptographicHash::Sha256);
    if (scene_playback.processedFrameTimestampUs() !=
            kTrimmedSourceFrameTimestampUs ||
        scene_playback.processedFrameTimestampEstimated() ||
        scene_timestamps.isEmpty() ||
        scene_timestamps.last() != kTrimmedSourceFrameTimestampUs ||
        scene_timestamp_estimates.isEmpty() ||
        scene_timestamp_estimates.last() ||
        TimelinePlaybackFrameTestAccess::processedSourceRevision(scene_playback) !=
            scene_revision ||
        scene_hash != image_after_native_hash) {
      return fail("Scene cache frames should retain exact source timestamps and ignore mismatched Qt Darwin native frames");
    }
    TimelinePlaybackFrameTestAccess::deactivateScenePreview(scene_playback);
  }
  std::cout << "Five overloaded rounds presented active + latest pending frames; 295 pending replacements counted; seek/step/detach/grade/source rejected stale output; reentrant publication, destruction cancellation, and heartbeat passed.\n";
  return 0;
}

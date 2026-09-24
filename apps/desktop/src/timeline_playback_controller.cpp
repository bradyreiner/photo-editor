#include "timeline_playback_controller.h"

#include "timeline_document_controller.h"

#include <QUrl>
#include <QtMath>

namespace photo_editor::desktop {

namespace {

constexpr qint64 kSeekToleranceMs = 20;
constexpr qint64 kPositionSyncToleranceMs = 250;
constexpr double kMaximumShuttleRate = 8.0;

}  // namespace

TimelinePlaybackController::TimelinePlaybackController(
    TimelineDocumentController* timeline_document,
    QObject* parent)
    : QObject(parent), timeline_document_(timeline_document) {
  native_media_enabled_ = !qEnvironmentVariableIsSet("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");
  if (native_media_enabled_) {
    player_.setAudioBufferOutput(audio_output_.bufferOutput());
  }
  connect(&audio_output_, &TimelineAudioOutput::meterChanged, this, [this]() {
    emit stateChanged();
  });
  connect(
      &audio_output_,
      &TimelineAudioOutput::outputDeviceRecoveryRequested,
      this,
      [this](const QString& detail) {
        recoverRuntimeFailure(QStringLiteral("audio-device"), detail);
      });
  gap_timer_.setInterval(15);
  gap_timer_.setTimerType(Qt::PreciseTimer);
  connect(&gap_timer_, &QTimer::timeout, this, [this]() { tickGapPlayback(); });
  prefetch_timer_.setSingleShot(true);
  prefetch_timer_.setInterval(18);
  connect(
      &prefetch_timer_,
      &QTimer::timeout,
      this,
      &TimelinePlaybackController::refreshPrefetchPlan);
  runtime_recovery_timer_.setSingleShot(true);
  runtime_recovery_timer_.setInterval(0);
  connect(&runtime_recovery_timer_, &QTimer::timeout, this, [this]() {
    if (!runtime_recovery_busy_) {
      return;
    }
    if (runtime_recovery_kind_ == QStringLiteral("decoder") ||
        runtime_recovery_kind_ == QStringLiteral("gpu")) {
      syncToDocument();
      refreshPrefetchPlan();
    }
    const auto state_preserved = timeline_document_.isNull() ||
                                 timeline_document_->deterministicDocumentHash() ==
                                     runtime_recovery_document_hash_;
    runtime_recovery_busy_ = false;
    runtime_recovery_status_ = state_preserved
                                   ? QStringLiteral("Recovered %1 without changing the edit.")
                                         .arg(runtime_recovery_kind_)
                                   : QStringLiteral("Recovery stopped because project state changed unexpectedly.");
    emit stateChanged();
  });

  connect(
      timeline_document_,
      &TimelineDocumentController::playbackStateChanged,
      this,
      [this]() {
        syncToDocument();
        schedulePrefetchRefresh();
      });
  connect(
      timeline_document_,
      &TimelineDocumentController::selectionChanged,
      this,
      [this]() {
        syncToDocument();
        schedulePrefetchRefresh();
      });
  connect(
      timeline_document_,
      &TimelineDocumentController::renderCacheChanged,
      this,
      [this]() {
        syncToDocument();
        schedulePrefetchRefresh();
      });
  if (native_media_enabled_) {
    connect(
        &player_,
        &QMediaPlayer::mediaStatusChanged,
        this,
        [this](QMediaPlayer::MediaStatus status) {
          if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia ||
              status == QMediaPlayer::BufferingMedia || status == QMediaPlayer::StalledMedia) {
            setErrorString(QString{});
            if (pending_position_ms_ >= 0) {
              awaited_position_ms_ = pending_position_ms_;
              awaiting_position_sync_ = true;
              player_.setPosition(pending_position_ms_);
              pending_position_ms_ = -1;
            }
            awaiting_media_source_ = false;
            if (play_when_ready_) {
              play_when_ready_ = false;
              player_.play();
            }
          }
          if (status == QMediaPlayer::StalledMedia) {
            applyAdaptivePlaybackPressure(
                QStringLiteral("the media backend stalled"), true);
            publishPlaybackDiagnostic(
                QStringLiteral("stalled"), 1,
                QStringLiteral("The media backend reported stalled playback."));
          }
          emit stateChanged();
        });
    connect(
        &player_,
        &QMediaPlayer::bufferProgressChanged,
        this,
        [this](float progress) {
          if (progress < 0.25F) {
            applyAdaptivePlaybackPressure(
                QStringLiteral("the playback buffer is low"), false);
          }
          emit stateChanged();
        });
    connect(
        &player_,
        &QMediaPlayer::positionChanged,
        this,
        [this](qint64 position) {
          if (timeline_document_.isNull()) {
            return;
          }

          if (awaiting_media_source_) {
            return;
          }

          // QMediaPlayer can deliver a final positionChanged signal from the
          // previous source after setSource(). Do not let that old clip-local
          // timestamp advance the newly selected timeline clip.
          if (awaiting_position_sync_) {
            if (qAbs(position - awaited_position_ms_) > kPositionSyncToleranceMs) {
              return;
            }
            awaiting_position_sync_ = false;
            awaited_position_ms_ = -1;
          }

          if (player_.playbackState() == QMediaPlayer::PlayingState) {
            if (playback_sample_clock_.isValid() &&
                last_playback_sample_position_ms_ >= 0) {
              recordPlaybackTimingSample(
                  qAbs(position - last_playback_sample_position_ms_),
                  playback_sample_clock_.restart());
            } else {
              playback_sample_clock_.start();
            }
            last_playback_sample_position_ms_ = position;
          }

          updateAudioVolume(position);
        });
    connect(
        &player_,
        &QMediaPlayer::errorOccurred,
        this,
        [this](QMediaPlayer::Error, const QString& error_string) {
          const auto detail = error_string.trimmed();
          setErrorString(detail);
          publishPlaybackDiagnostic(QStringLiteral("decoder"), 1, detail);
          recoverRuntimeFailure(QStringLiteral("decoder"), detail);
        });
  }

  syncToDocument();
  refreshPrefetchPlan();
}

bool TimelinePlaybackController::hasVideoSource() const {
  return !current_source_url_.isEmpty();
}

bool TimelinePlaybackController::mediaReady() const {
  if (!native_media_enabled_) {
    return hasVideoSource();
  }
  return player_.mediaStatus() == QMediaPlayer::LoadedMedia || player_.mediaStatus() == QMediaPlayer::BufferedMedia ||
         player_.mediaStatus() == QMediaPlayer::BufferingMedia || player_.mediaStatus() == QMediaPlayer::StalledMedia;
}

QString TimelinePlaybackController::errorString() const {
  return error_string_;
}

QString TimelinePlaybackController::previewQualityMode() const {
  return preview_quality_mode_;
}

QString TimelinePlaybackController::activeSourceUrl() const {
  return current_source_url_;
}

bool TimelinePlaybackController::usingProxy() const {
  return using_proxy_;
}

bool TimelinePlaybackController::usingRenderCache() const {
  return using_render_cache_;
}

QString TimelinePlaybackController::effectivePreviewQuality() const {
  if (current_source_url_.isEmpty()) {
    return QStringLiteral("none");
  }
  if (using_render_cache_) {
    return QStringLiteral("render-cache");
  }
  if (using_proxy_) {
    return QStringLiteral("proxy");
  }
  if (preview_quality_mode_ == QStringLiteral("half") ||
      preview_quality_mode_ == QStringLiteral("quarter")) {
    return preview_quality_mode_;
  }
  return QStringLiteral("original");
}

QString TimelinePlaybackController::playbackHealth() const {
  if (!error_string_.isEmpty()) {
    return QStringLiteral("error");
  }
  if (!hasVideoSource()) {
    return QStringLiteral("idle");
  }
  if (!native_media_enabled_) {
    return QStringLiteral("ready");
  }
  switch (player_.mediaStatus()) {
    case QMediaPlayer::StalledMedia:
      return QStringLiteral("stalled");
    case QMediaPlayer::LoadingMedia:
    case QMediaPlayer::BufferingMedia:
      return QStringLiteral("buffering");
    case QMediaPlayer::InvalidMedia:
      return QStringLiteral("error");
    default:
      return QStringLiteral("ready");
  }
}

double TimelinePlaybackController::bufferProgress() const {
  if (!native_media_enabled_) {
    return hasVideoSource() ? 1.0 : 0.0;
  }
  return qBound(0.0, static_cast<double>(player_.bufferProgress()), 1.0);
}

bool TimelinePlaybackController::adaptiveQualityActive() const {
  if (preview_quality_mode_ != QStringLiteral("auto") || timeline_document_.isNull()) {
    return false;
  }
  const auto responsiveness_needed =
      interaction_active_ || timeline_document_->sequencePlaying();
  return responsiveness_needed &&
         (using_render_cache_ || using_proxy_ ||
          adaptive_detail_level_ != QStringLiteral("full"));
}

QString TimelinePlaybackController::adaptiveDetailLevel() const {
  return adaptive_detail_level_;
}

QString TimelinePlaybackController::adaptiveQualityReason() const {
  return adaptive_quality_reason_;
}

double TimelinePlaybackController::effectivePreviewScale() const {
  if (preview_quality_mode_ == QStringLiteral("quarter")) {
    return 0.25;
  }
  if (preview_quality_mode_ == QStringLiteral("half")) {
    return 0.5;
  }
  if (preview_quality_mode_ == QStringLiteral("auto")) {
    if (adaptive_detail_level_ == QStringLiteral("quarter")) {
      return 0.25;
    }
    if (adaptive_detail_level_ == QStringLiteral("half")) {
      return 0.5;
    }
  }
  return 1.0;
}

bool TimelinePlaybackController::playbackRangeActive() const {
  return playback_range_active_;
}

bool TimelinePlaybackController::playbackRangeLooping() const {
  return playback_range_active_ && playback_range_looping_;
}

double TimelinePlaybackController::playbackRangeStart() const {
  return playback_range_active_ ? loop_start_seconds_ : -1.0;
}

double TimelinePlaybackController::playbackRangeEnd() const {
  return playback_range_active_ ? loop_end_seconds_ : -1.0;
}

bool TimelinePlaybackController::interactionActive() const {
  return interaction_active_;
}

double TimelinePlaybackController::activePlaybackRate() const {
  return active_playback_rate_;
}

double TimelinePlaybackController::shuttleRate() const {
  return shuttle_rate_;
}

bool TimelinePlaybackController::shuttling() const {
  return !qFuzzyIsNull(shuttle_rate_) && timeline_document_ != nullptr &&
         timeline_document_->sequencePlaying();
}

QVariantList TimelinePlaybackController::prefetchPlan() const {
  return prefetch_plan_;
}

QString TimelinePlaybackController::prefetchedSourceUrl() const {
  return prefetched_source_url_;
}

double TimelinePlaybackController::prefetchDirection() const {
  return prefetch_direction_;
}

double TimelinePlaybackController::audioPeakLeft() const {
  return audio_output_.peakLeft();
}

double TimelinePlaybackController::audioPeakRight() const {
  return audio_output_.peakRight();
}

double TimelinePlaybackController::audioTruePeakLeft() const {
  return audio_output_.truePeakLeft();
}

double TimelinePlaybackController::audioTruePeakRight() const {
  return audio_output_.truePeakRight();
}

double TimelinePlaybackController::audioRmsLeft() const {
  return audio_output_.rmsLeft();
}

double TimelinePlaybackController::audioRmsRight() const {
  return audio_output_.rmsRight();
}

bool TimelinePlaybackController::audioClipping() const {
  return audio_output_.clipping();
}

void TimelinePlaybackController::resetAudioMeters() {
  audio_output_.resetMeters();
  emit stateChanged();
}

QVariantMap TimelinePlaybackController::playbackDiagnostics() const {
  return playback_diagnostics_;
}

int TimelinePlaybackController::droppedFrameCount() const {
  return dropped_frame_count_;
}

void TimelinePlaybackController::resetPlaybackDiagnostics() {
  playback_diagnostics_.clear();
  dropped_frame_count_ = 0;
  playback_sample_clock_.invalidate();
  last_playback_sample_position_ms_ = -1;
  emit stateChanged();
}

void TimelinePlaybackController::recordPlaybackTimingSample(
    qint64 position_delta_ms,
    qint64 wall_delta_ms) {
  if (timeline_document_.isNull() || !timeline_document_->sequencePlaying() ||
      wall_delta_ms < 20) {
    return;
  }
  const auto timing_lag =
      wall_delta_ms >= 50 && position_delta_ms < wall_delta_ms * 3 / 5;
  if (preview_quality_mode_ == QStringLiteral("auto")) {
    if (timing_lag) {
      applyAdaptivePlaybackPressure(QStringLiteral("decoded frames are behind"), false);
    } else {
      recordAdaptiveHealthySample();
    }
  }
  if (!timing_lag) {
    return;
  }
  const auto frame_duration_ms =
      1000.0 / qMax(1.0, timeline_document_->framesPerSecond());
  const auto estimated = qMax(
      1,
      qRound((static_cast<double>(wall_delta_ms - position_delta_ms)) /
             frame_duration_ms));
  publishPlaybackDiagnostic(QStringLiteral("timing-lag"), estimated);
}

void TimelinePlaybackController::applyAdaptivePlaybackPressure(
    const QString& reason,
    bool severe) {
  if (preview_quality_mode_ != QStringLiteral("auto") ||
      timeline_document_.isNull() || !timeline_document_->sequencePlaying()) {
    return;
  }
  ++adaptive_slow_sample_count_;
  adaptive_healthy_sample_count_ = 0;
  auto next_level = adaptive_detail_level_;
  if (severe || adaptive_slow_sample_count_ >= 3) {
    next_level = QStringLiteral("quarter");
  } else if (adaptive_detail_level_ == QStringLiteral("full")) {
    next_level = QStringLiteral("half");
  }
  const auto normalized_reason = reason.simplified().left(160);
  if (next_level == adaptive_detail_level_ &&
      normalized_reason == adaptive_quality_reason_) {
    return;
  }
  adaptive_detail_level_ = next_level;
  adaptive_quality_reason_ = normalized_reason;
  emit stateChanged();
}

void TimelinePlaybackController::recordAdaptiveHealthySample() {
  if (preview_quality_mode_ != QStringLiteral("auto") ||
      adaptive_detail_level_ == QStringLiteral("full")) {
    adaptive_slow_sample_count_ = 0;
    return;
  }
  adaptive_slow_sample_count_ = 0;
  ++adaptive_healthy_sample_count_;
  if (adaptive_healthy_sample_count_ < 12) {
    return;
  }
  adaptive_healthy_sample_count_ = 0;
  adaptive_detail_level_ = adaptive_detail_level_ == QStringLiteral("quarter")
                               ? QStringLiteral("half")
                               : QStringLiteral("full");
  if (adaptive_detail_level_ == QStringLiteral("full")) {
    adaptive_quality_reason_.clear();
  } else {
    adaptive_quality_reason_ = QStringLiteral("playback load is recovering");
  }
  emit stateChanged();
}

void TimelinePlaybackController::resetAdaptivePlaybackDetail() {
  adaptive_slow_sample_count_ = 0;
  adaptive_healthy_sample_count_ = 0;
  adaptive_quality_reason_.clear();
  adaptive_detail_level_ = QStringLiteral("full");
}

void TimelinePlaybackController::publishPlaybackDiagnostic(
    const QString& trigger,
    int dropped_frames,
    const QString& detail) {
  if (timeline_document_.isNull()) {
    return;
  }
  const auto clip = timeline_document_->programClip();
  const auto media_online = clip.value(QStringLiteral("mediaOnline"), true).toBool();
  const auto effects = clip.value(QStringLiteral("effectStack")).toList();
  const auto source_path = clip.value(QStringLiteral("filePath")).toString();
  QString cause;
  QString summary;
  QString action;
  QString affected_detail;
  if (!media_online) {
    cause = QStringLiteral("storage");
    summary = QStringLiteral("Source media is offline or the storage volume disconnected.");
    action = QStringLiteral("Open Batch Relink and reconnect this source before playback.");
    affected_detail = source_path;
  } else if (using_render_cache_) {
    cause = QStringLiteral("cache");
    summary = QStringLiteral("The active render-cache range could not feed frames in time.");
    action = QStringLiteral("Clear and rebuild the preview cache for this range.");
    affected_detail = timeline_document_->renderCachePlaybackSourceUrl();
  } else if (!effects.isEmpty()) {
    cause = QStringLiteral("effect");
    const auto effect_name = effects.first().toString();
    summary = QStringLiteral("The active clip effect stack is the most likely playback bottleneck.");
    action = QStringLiteral("Temporarily bypass '%1' or render a preview cache.").arg(effect_name);
    affected_detail = effect_name;
  } else if (trigger == QStringLiteral("decoder") || !error_string_.isEmpty()) {
    cause = QStringLiteral("decoder");
    summary = QStringLiteral("The decoder failed while reading the active clip.");
    action = QStringLiteral("Retry playback; if it repeats, transcode or relink this source.");
    affected_detail = detail;
  } else if (bufferProgress() < 0.25 || trigger == QStringLiteral("stalled")) {
    cause = QStringLiteral("storage");
    summary = QStringLiteral("Media buffering fell behind the sequence clock.");
    action = QStringLiteral("Use a proxy or move this source to faster local storage.");
    affected_detail = source_path;
  } else {
    cause = QStringLiteral("decoder");
    summary = QStringLiteral("Decoded frames arrived too slowly for the sequence rate.");
    action = QStringLiteral("Use Auto or Proxy quality, or transcode the active source.");
    affected_detail = source_path;
  }
  dropped_frame_count_ += qMax(1, dropped_frames);
  playback_diagnostics_ = QVariantMap{
      {QStringLiteral("schema"), QStringLiteral("timeline-playback-diagnostics:v1")},
      {QStringLiteral("cause"), cause},
      {QStringLiteral("summary"), summary},
      {QStringLiteral("action"), action},
      {QStringLiteral("detail"), affected_detail},
      {QStringLiteral("clipId"), clip.value(QStringLiteral("clipId"))},
      {QStringLiteral("clipLabel"), clip.value(QStringLiteral("displayLabel"))},
      {QStringLiteral("trackLabel"), clip.value(QStringLiteral("trackLabel"))},
      {QStringLiteral("sourcePath"), source_path},
      {QStringLiteral("sequenceSeconds"), timeline_document_->sequencePlayheadSeconds()},
      {QStringLiteral("decoder"), QStringLiteral("Qt Multimedia / platform decoder")},
      {QStringLiteral("effectiveQuality"), effectivePreviewQuality()},
      {QStringLiteral("bufferProgress"), bufferProgress()},
      {QStringLiteral("droppedFrameCount"), dropped_frame_count_},
      {QStringLiteral("recoverable"), true},
  };
  emit stateChanged();
}

bool TimelinePlaybackController::runtimeRecoveryBusy() const {
  return runtime_recovery_busy_;
}

QString TimelinePlaybackController::runtimeRecoveryKind() const {
  return runtime_recovery_kind_;
}

QString TimelinePlaybackController::runtimeRecoveryStatus() const {
  return runtime_recovery_status_;
}

int TimelinePlaybackController::runtimeRecoveryCount() const {
  return runtime_recovery_count_;
}

void TimelinePlaybackController::setPreviewQualityMode(const QString& mode) {
  const auto normalized = mode.trimmed().toLower();
  const auto accepted = normalized == QStringLiteral("full") ||
                        normalized == QStringLiteral("half") ||
                        normalized == QStringLiteral("quarter") ||
                        normalized == QStringLiteral("proxy") ||
                        normalized == QStringLiteral("clip")
                            ? normalized
                            : QStringLiteral("auto");
  if (preview_quality_mode_ == accepted) {
    return;
  }
  preview_quality_mode_ = accepted;
  resetAdaptivePlaybackDetail();
  syncToDocument();
}

void TimelinePlaybackController::setInteractionActive(bool active) {
  if (interaction_active_ == active) {
    return;
  }
  interaction_active_ = active;
  syncToDocument();
}

void TimelinePlaybackController::attachVideoOutput(QObject* output) {
  if (native_media_enabled_) {
    player_.setVideoOutput(output);
  }
}

void TimelinePlaybackController::play() {
  if (timeline_document_.isNull()) {
    return;
  }

  shuttle_rate_ = 1.0;
  timeline_document_->playSequence();
  gap_start_sequence_seconds_ = timeline_document_->sequencePlayheadSeconds();
  gap_elapsed_.restart();
}

void TimelinePlaybackController::pause() {
  if (timeline_document_.isNull()) {
    return;
  }

  shuttle_rate_ = 0.0;
  timeline_document_->pauseSequence();
  resetAdaptivePlaybackDetail();
  syncToDocument();
}

void TimelinePlaybackController::togglePlayback() {
  if (timeline_document_.isNull()) {
    return;
  }

  if (timeline_document_->sequencePlaying()) {
    stopShuttle();
  } else {
    play();
  }
}

void TimelinePlaybackController::shuttleReverse() {
  if (timeline_document_.isNull() || timeline_document_->clipCount() == 0) {
    return;
  }

  if (timeline_document_->sequencePlayheadSeconds() >=
      timeline_document_->totalDurationSecondsValue() - 0.0001) {
    timeline_document_->seekSequenceSeconds(
        qMax(0.0,
             timeline_document_->totalDurationSecondsValue() -
                 1.0 / qMax(1.0, timeline_document_->framesPerSecond())));
  }
  shuttle_rate_ = timeline_document_->sequencePlaying() && shuttle_rate_ < 0.0
                      ? -qMin(kMaximumShuttleRate, qAbs(shuttle_rate_) * 2.0)
                      : -1.0;
  timeline_document_->playSequence();
  gap_start_sequence_seconds_ = timeline_document_->sequencePlayheadSeconds();
  gap_elapsed_.restart();
  syncToDocument();
}

void TimelinePlaybackController::stopShuttle() {
  pause();
}

void TimelinePlaybackController::shuttleForward() {
  if (timeline_document_.isNull() || timeline_document_->clipCount() == 0) {
    return;
  }

  shuttle_rate_ = timeline_document_->sequencePlaying() && shuttle_rate_ > 0.0
                      ? qMin(kMaximumShuttleRate, shuttle_rate_ * 2.0)
                      : 1.0;
  timeline_document_->playSequence();
  gap_start_sequence_seconds_ = timeline_document_->sequencePlayheadSeconds();
  gap_elapsed_.restart();
  syncToDocument();
}

void TimelinePlaybackController::stepFrames(int direction) {
  if (timeline_document_.isNull()) {
    return;
  }

  shuttle_rate_ = 0.0;
  timeline_document_->pauseSequence();
  timeline_document_->stepSequenceFrames(direction == 0 ? 0 : direction > 0 ? 1 : -1);
  syncToDocument();
}

void TimelinePlaybackController::seekSequenceSeconds(double seconds) {
  if (timeline_document_.isNull()) {
    return;
  }

  shuttle_rate_ = 0.0;
  timeline_document_->seekSequenceSeconds(seconds);
  syncToDocument();
}

void TimelinePlaybackController::seekCurrentClipSeconds(double seconds) {
  if (timeline_document_.isNull()) {
    return;
  }

  shuttle_rate_ = 0.0;
  timeline_document_->seekCurrentClipSeconds(seconds);
  syncToDocument();
}

void TimelinePlaybackController::setPlaybackLoop(double start_seconds, double end_seconds) {
  if (timeline_document_.isNull()) {
    return;
  }
  const auto duration = timeline_document_->totalDurationSecondsValue();
  const auto start = qBound(0.0, start_seconds, duration);
  const auto end = qBound(0.0, end_seconds, duration);
  if (end <= start) {
    clearPlaybackLoop();
    return;
  }
  loop_start_seconds_ = start;
  loop_end_seconds_ = end;
  playback_range_active_ = true;
  playback_range_looping_ = true;
  schedulePrefetchRefresh();
  emit stateChanged();
}

void TimelinePlaybackController::clearPlaybackLoop() {
  loop_start_seconds_ = -1.0;
  loop_end_seconds_ = -1.0;
  playback_range_active_ = false;
  playback_range_looping_ = false;
  schedulePrefetchRefresh();
  emit stateChanged();
}

bool TimelinePlaybackController::playReviewRange(
    double start_seconds,
    double end_seconds,
    double preroll_seconds,
    double postroll_seconds,
    bool loop) {
  if (timeline_document_.isNull()) {
    return false;
  }
  const auto duration = timeline_document_->totalDurationSecondsValue();
  const auto range_start = qBound(0.0, start_seconds, duration);
  const auto range_end = qBound(0.0, end_seconds, duration);
  if (range_end <= range_start) {
    return false;
  }
  loop_start_seconds_ = qMax(0.0, range_start - qMax(0.0, preroll_seconds));
  loop_end_seconds_ = qMin(duration, range_end + qMax(0.0, postroll_seconds));
  playback_range_active_ = loop_end_seconds_ > loop_start_seconds_;
  playback_range_looping_ = loop;
  timeline_document_->seekSequenceSeconds(loop_start_seconds_);
  shuttle_rate_ = 1.0;
  timeline_document_->playSequence();
  gap_start_sequence_seconds_ = loop_start_seconds_;
  gap_elapsed_.restart();
  schedulePrefetchRefresh();
  emit stateChanged();
  return playback_range_active_;
}

bool TimelinePlaybackController::playInToOut(
    double start_seconds,
    double end_seconds) {
  return playReviewRange(start_seconds, end_seconds, 0.0, 0.0, false);
}

bool TimelinePlaybackController::reviewFromEditPoint(
    double edit_seconds,
    double preroll_seconds,
    double postroll_seconds) {
  if (timeline_document_.isNull()) {
    return false;
  }
  const auto duration = timeline_document_->totalDurationSecondsValue();
  const auto edit = qBound(0.0, edit_seconds, duration);
  const auto start = qMax(0.0, edit - qMax(0.0, preroll_seconds));
  const auto end = qMin(duration, edit + qMax(0.0, postroll_seconds));
  return playReviewRange(start, end, 0.0, 0.0, false);
}

void TimelinePlaybackController::stopReviewPlayback() {
  pause();
  clearPlaybackLoop();
}

void TimelinePlaybackController::setLikelyNextEditSeconds(double seconds) {
  const auto normalized = seconds < 0.0 ? -1.0 : seconds;
  if (qFuzzyCompare(likely_next_edit_seconds_ + 1.0, normalized + 1.0)) {
    return;
  }
  likely_next_edit_seconds_ = normalized;
  schedulePrefetchRefresh();
}

void TimelinePlaybackController::recoverRuntimeFailure(
    const QString& kind,
    const QString& detail) {
  const auto normalized = kind.trimmed().toLower();
  const auto supported = normalized == QStringLiteral("decoder") ||
                         normalized == QStringLiteral("gpu") ||
                         normalized == QStringLiteral("audio-device") ||
                         normalized == QStringLiteral("external-display");
  if (!supported || runtime_recovery_busy_) {
    return;
  }

  runtime_recovery_busy_ = true;
  runtime_recovery_kind_ = normalized;
  runtime_recovery_status_ = detail.trimmed().isEmpty()
                                 ? QStringLiteral("Recovering %1…").arg(normalized)
                                 : QStringLiteral("Recovering %1: %2")
                                       .arg(normalized, detail.trimmed());
  runtime_recovery_document_hash_ = timeline_document_.isNull()
                                        ? QString{}
                                        : timeline_document_->deterministicDocumentHash();
  ++runtime_recovery_count_;

  if (native_media_enabled_ &&
      (normalized == QStringLiteral("decoder") ||
       normalized == QStringLiteral("gpu"))) {
    player_.pause();
    player_.setSource(QUrl());
    prefetch_player_.stop();
    prefetch_player_.setSource(QUrl());
    current_source_url_.clear();
    prefetched_source_url_.clear();
    awaiting_media_source_ = false;
    awaiting_position_sync_ = false;
  }
  if (normalized == QStringLiteral("audio-device")) {
    audio_output_.recoverOutputDevice();
  }
  emit runtimeRecoveryRequested(normalized);
  emit stateChanged();
  runtime_recovery_timer_.start();
}

void TimelinePlaybackController::schedulePrefetchRefresh() {
  // Playback ticks arrive every ~15 ms. Restarting an 18 ms debounce on every
  // tick would starve prefetch forever, so retain the first pending deadline.
  if (!prefetch_timer_.isActive()) {
    prefetch_timer_.start();
  }
}

void TimelinePlaybackController::refreshPrefetchPlan() {
  if (timeline_document_.isNull()) {
    prefetch_plan_.clear();
    prefetched_source_url_.clear();
    return;
  }
  prefetch_direction_ = shuttle_rate_ < 0.0 ? -1.0 : 1.0;
  auto next_plan = timeline_document_->playbackPrefetchCandidates(
      timeline_document_->sequencePlayheadSeconds(),
      prefetch_direction_,
      loop_start_seconds_,
      loop_end_seconds_,
      likely_next_edit_seconds_,
      6);
  QVariantList protected_plan;
  for (const auto& value : next_plan) {
    auto candidate = value.toMap();
    const auto source_url = candidate.value(QStringLiteral("sourceUrl")).toString();
    if (source_url.isEmpty() || source_url == current_source_url_) {
      continue;
    }
    candidate.insert(QStringLiteral("protectedSourceUrl"), current_source_url_);
    protected_plan.push_back(candidate);
  }
  prefetch_plan_ = protected_plan;
  const auto next_source = prefetch_plan_.isEmpty()
                               ? QString{}
                               : prefetch_plan_.first()
                                     .toMap()
                                     .value(QStringLiteral("sourceUrl"))
                                     .toString();
  if (prefetched_source_url_ != next_source) {
    prefetched_source_url_ = next_source;
    if (native_media_enabled_) {
      prefetch_player_.stop();
      prefetch_player_.setSource(QUrl(prefetched_source_url_));
      if (!prefetch_plan_.isEmpty()) {
        const auto position_ms = qRound64(
            prefetch_plan_.first()
                .toMap()
                .value(QStringLiteral("sourceSeconds"))
                .toDouble() *
            1000.0);
        prefetch_player_.setPosition(qMax<qint64>(0, position_ms));
      }
    }
  }
  emit stateChanged();
}

void TimelinePlaybackController::syncToDocument() {
  if (timeline_document_.isNull()) {
    return;
  }
  if (!timeline_document_->sequencePlaying() &&
      adaptive_detail_level_ != QStringLiteral("full")) {
    resetAdaptivePlaybackDetail();
  }

  const auto next_source_url = preferredSourceUrl();
  const auto proxy_source_url = timeline_document_->proxyPlaybackSourceUrl();
  const auto render_cache_source_url = timeline_document_->renderCachePlaybackSourceUrl();
  using_render_cache_ = !render_cache_source_url.isEmpty() &&
                        next_source_url == render_cache_source_url;
  render_cache_range_start_seconds_ = using_render_cache_
                                          ? timeline_document_->renderCachePlaybackStartSeconds()
                                          : 0.0;
  using_proxy_ = !proxy_source_url.isEmpty() && next_source_url == proxy_source_url;
  const auto current_clip = timeline_document_->currentClip();
  active_playback_rate_ = using_render_cache_
                              ? 1.0
                              : qBound(
                                    -8.0,
                                    current_clip
                                        .value(
                                            QStringLiteral("effectivePlaybackRate"),
                                            current_clip.value(QStringLiteral("playbackRate"), 1.0))
                                        .toDouble(),
                                    8.0);
  const auto target_position_ms =
      qRound64((using_render_cache_
                    ? timeline_document_->sequencePlayheadSeconds() -
                          render_cache_range_start_seconds_
                    : timeline_document_->currentClipPlayheadSeconds()) *
               1000.0);

  if (!native_media_enabled_) {
    current_source_url_ = next_source_url;
    if (timeline_document_->sequencePlaying()) {
      startGapPlayback();
    } else {
      stopGapPlayback();
    }
    setErrorString(QString{});
    emit stateChanged();
    return;
  }

  updateAudioVolume(target_position_ms);
  const auto forward_shuttle_multiplier = shuttle_rate_ > 0.0 ? shuttle_rate_ : 1.0;
  player_.setPlaybackRate(
      qMax(0.01, qAbs(active_playback_rate_)) * forward_shuttle_multiplier);
  if (timeline_document_->sequencePlaying()) {
    startGapPlayback();
  } else {
    stopGapPlayback();
  }

  if (current_source_url_ != next_source_url) {
    current_source_url_ = next_source_url;
    playback_sample_clock_.invalidate();
    last_playback_sample_position_ms_ = -1;
    pending_position_ms_ = target_position_ms;
    awaited_position_ms_ = target_position_ms;
    awaiting_media_source_ = !current_source_url_.isEmpty();
    awaiting_position_sync_ = !current_source_url_.isEmpty();
    play_when_ready_ = timeline_document_->sequencePlaying() && shuttle_rate_ >= 0.0;
    setErrorString(QString{});

    if (current_source_url_.isEmpty()) {
      awaited_position_ms_ = -1;
      awaiting_media_source_ = false;
      awaiting_position_sync_ = false;
      player_.stop();
      player_.setSource(QUrl());
      if (timeline_document_->sequencePlaying()) {
        startGapPlayback();
      } else {
        stopGapPlayback();
      }
    } else {
      player_.setSource(QUrl(current_source_url_));
    }

    emit stateChanged();
    return;
  }

  if (current_source_url_.isEmpty()) {
    awaited_position_ms_ = -1;
    awaiting_media_source_ = false;
    awaiting_position_sync_ = false;
    player_.stop();
    if (timeline_document_->sequencePlaying()) {
      startGapPlayback();
    } else {
      stopGapPlayback();
    }
    setErrorString(QString{});
    emit stateChanged();
    return;
  }

  if (awaiting_media_source_) {
    pending_position_ms_ = target_position_ms;
    awaited_position_ms_ = target_position_ms;
    awaiting_position_sync_ = true;
    play_when_ready_ = timeline_document_->sequencePlaying() && shuttle_rate_ >= 0.0;
    emit stateChanged();
    return;
  }

  if (qAbs(player_.position() - target_position_ms) > kSeekToleranceMs) {
    awaited_position_ms_ = target_position_ms;
    awaiting_position_sync_ = true;
    player_.setPosition(target_position_ms);
  }

  if (timeline_document_->sequencePlaying() && shuttle_rate_ >= 0.0 &&
      active_playback_rate_ > 0.0001) {
    if (player_.mediaStatus() == QMediaPlayer::LoadedMedia || player_.mediaStatus() == QMediaPlayer::BufferedMedia ||
        player_.mediaStatus() == QMediaPlayer::BufferingMedia || player_.mediaStatus() == QMediaPlayer::StalledMedia) {
      player_.play();
    } else {
      play_when_ready_ = true;
    }
  } else if (player_.playbackState() == QMediaPlayer::PlayingState) {
    player_.pause();
  }

  emit stateChanged();
}

QString TimelinePlaybackController::preferredSourceUrl() const {
  if (timeline_document_.isNull()) {
    return {};
  }

  const auto original = timeline_document_->originalPlaybackSourceUrl();
  const auto proxy = timeline_document_->proxyPlaybackSourceUrl();
  if (preview_quality_mode_ == QStringLiteral("full") ||
      preview_quality_mode_ == QStringLiteral("half") ||
      preview_quality_mode_ == QStringLiteral("quarter")) {
    return original;
  }
  if (preview_quality_mode_ == QStringLiteral("proxy")) {
    return proxy.isEmpty() ? original : proxy;
  }
  if (preview_quality_mode_ == QStringLiteral("clip")) {
    return timeline_document_->playbackSourceUrl();
  }
  const auto needs_responsive_source = interaction_active_ || timeline_document_->sequencePlaying();
  const auto render_cache = timeline_document_->renderCachePlaybackSourceUrl();
  if (needs_responsive_source && !render_cache.isEmpty()) {
    return render_cache;
  }
  return needs_responsive_source && !proxy.isEmpty() ? proxy : original;
}

void TimelinePlaybackController::startGapPlayback() {
  if (timeline_document_.isNull() || gap_timer_.isActive()) {
    return;
  }

  gap_start_sequence_seconds_ = timeline_document_->sequencePlayheadSeconds();
  gap_elapsed_.restart();
  gap_timer_.start();
}

void TimelinePlaybackController::stopGapPlayback() {
  gap_timer_.stop();
  gap_elapsed_.invalidate();
}

void TimelinePlaybackController::tickGapPlayback() {
  if (timeline_document_.isNull() || !timeline_document_->sequencePlaying()) {
    stopGapPlayback();
    return;
  }

  const auto elapsed_seconds = static_cast<double>(gap_elapsed_.elapsed()) / 1000.0;
  const auto effective_rate = qFuzzyIsNull(shuttle_rate_) ? 1.0 : shuttle_rate_;
  const auto target_seconds = gap_start_sequence_seconds_ + elapsed_seconds * effective_rate;
  if (playback_range_active_ && effective_rate > 0.0 &&
      target_seconds >= loop_end_seconds_) {
    if (playback_range_looping_) {
      timeline_document_->syncGapPlaybackPosition(loop_start_seconds_);
      gap_start_sequence_seconds_ = loop_start_seconds_;
      gap_elapsed_.restart();
      emit stateChanged();
      return;
    }
    timeline_document_->syncGapPlaybackPosition(loop_end_seconds_);
    shuttle_rate_ = 0.0;
    timeline_document_->pauseSequence();
    playback_range_active_ = false;
    playback_range_looping_ = false;
    loop_start_seconds_ = -1.0;
    loop_end_seconds_ = -1.0;
    stopGapPlayback();
    schedulePrefetchRefresh();
    emit stateChanged();
    return;
  }
  const auto bounded_seconds = qBound(
      0.0, target_seconds, timeline_document_->totalDurationSecondsValue());
  timeline_document_->syncGapPlaybackPosition(bounded_seconds);
  if ((effective_rate < 0.0 && target_seconds <= 0.0) ||
      (effective_rate > 0.0 &&
       target_seconds >= timeline_document_->totalDurationSecondsValue())) {
    shuttle_rate_ = 0.0;
    timeline_document_->pauseSequence();
    stopGapPlayback();
    emit stateChanged();
  }
}

void TimelinePlaybackController::updateAudioVolume(qint64 source_position_ms) {
  Q_UNUSED(source_position_ms)
  if (timeline_document_.isNull()) {
    return;
  }
  if (using_render_cache_) {
    audio_output_.setVolume(1.0);
    audio_output_.setPanEnabled(false);
    audio_output_.setEqLowDb(0.0);
    audio_output_.setEqMidDb(0.0);
    audio_output_.setEqHighDb(0.0);
    audio_output_.setCompressorEnabled(false);
    audio_output_.setDuckingGainDb(0.0);
    return;
  }
  audio_output_.setPanEnabled(true);
  const auto clip = timeline_document_->currentClip();
  audio_output_.setEqLowDb(clip.value(QStringLiteral("trackAudioEqLowDb"), 0.0).toDouble());
  audio_output_.setEqMidDb(clip.value(QStringLiteral("trackAudioEqMidDb"), 0.0).toDouble());
  audio_output_.setEqHighDb(clip.value(QStringLiteral("trackAudioEqHighDb"), 0.0).toDouble());
  audio_output_.setCompressorEnabled(
      clip.value(QStringLiteral("trackAudioCompressorEnabled"), false).toBool());
  audio_output_.setCompressorThresholdDb(
      clip.value(QStringLiteral("trackAudioCompressorThresholdDb"), -18.0).toDouble());
  audio_output_.setCompressorRatio(
      clip.value(QStringLiteral("trackAudioCompressorRatio"), 4.0).toDouble());
  audio_output_.setCompressorMakeupDb(
      clip.value(QStringLiteral("trackAudioCompressorMakeupDb"), 0.0).toDouble());
  audio_output_.setDuckingGainDb(
      clip.value(QStringLiteral("trackAudioDuckingGainDb"), 0.0).toDouble());
  const auto enabled = (!clip.contains(QStringLiteral("audioPlaybackEnabled")) ||
                        clip.value(QStringLiteral("audioPlaybackEnabled")).toBool()) &&
                       !clip.value(QStringLiteral("retimeAudioMuted"), false).toBool();
  if (!enabled || clip.isEmpty()) {
    audio_output_.setVolume(0.0);
    return;
  }

  const auto fade_in = clip.value(QStringLiteral("fadeInSeconds")).toDouble();
  const auto fade_out = clip.value(QStringLiteral("fadeOutSeconds")).toDouble();
  const auto timeline_offset = qMax(
      0.0,
      timeline_document_->sequencePlayheadSeconds() -
          clip.value(QStringLiteral("startSeconds")).toDouble());
  const auto timeline_duration = qMax(
      0.0, clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble());
  auto envelope = 1.0;
  if (fade_in > 0.0) {
    envelope = qMin(envelope, qBound(0.0, timeline_offset / fade_in, 1.0));
  }
  if (fade_out > 0.0) {
    envelope = qMin(
        envelope, qBound(0.0, (timeline_duration - timeline_offset) / fade_out, 1.0));
  }
  const auto total_gain_db = clip.value(QStringLiteral("audioGainDb")).toDouble() +
                             clip.value(QStringLiteral("trackAudioGainDb")).toDouble();
  const auto linear_gain = qPow(10.0, total_gain_db / 20.0);
  audio_output_.setPan(clip.value(QStringLiteral("trackAudioPan")).toDouble());
  audio_output_.setVolume(qBound(0.0, linear_gain * envelope, 1.0));
}

void TimelinePlaybackController::setErrorString(const QString& error_string) {
  if (error_string_ == error_string) {
    return;
  }

  error_string_ = error_string;
  emit stateChanged();
}

}  // namespace photo_editor::desktop

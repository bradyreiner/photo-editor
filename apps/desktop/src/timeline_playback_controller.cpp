#include "timeline_playback_controller.h"

#include "timeline_document_controller.h"

#include <QUrl>

namespace photo_editor::desktop {

namespace {

constexpr qint64 kSeekToleranceMs = 20;

}  // namespace

TimelinePlaybackController::TimelinePlaybackController(
    TimelineDocumentController* timeline_document,
    QObject* parent)
    : QObject(parent), timeline_document_(timeline_document) {
  player_.setAudioOutput(&audio_output_);
  audio_output_.setVolume(1.0);

  connect(
      timeline_document_,
      &TimelineDocumentController::playbackStateChanged,
      this,
      [this]() { syncToDocument(); });
  connect(
      timeline_document_,
      &TimelineDocumentController::selectionChanged,
      this,
      [this]() { syncToDocument(); });
  connect(
      &player_,
      &QMediaPlayer::mediaStatusChanged,
      this,
      [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia ||
            status == QMediaPlayer::BufferingMedia || status == QMediaPlayer::StalledMedia) {
          setErrorString(QString{});
          if (pending_position_ms_ >= 0) {
            player_.setPosition(pending_position_ms_);
            pending_position_ms_ = -1;
          }
          if (play_when_ready_) {
            play_when_ready_ = false;
            player_.play();
          }
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

        timeline_document_->syncPlaybackPosition(static_cast<double>(position) / 1000.0);
        const auto current_clip = timeline_document_->currentClip();
        const auto trim_end_seconds = current_clip.value(QStringLiteral("trimEndSeconds")).toDouble();
        if (trim_end_seconds <= 0.0) {
          return;
        }

        if (static_cast<double>(position) / 1000.0 >= trim_end_seconds) {
          if (timeline_document_->sequencePlaying() && timeline_document_->advancePlaybackAfterClipEnd()) {
            syncToDocument();
            return;
          }
          timeline_document_->pauseSequence();
        }
      });
  connect(
      &player_,
      &QMediaPlayer::errorOccurred,
      this,
      [this](QMediaPlayer::Error, const QString& error_string) {
        setErrorString(error_string.trimmed());
      });

  syncToDocument();
}

bool TimelinePlaybackController::hasVideoSource() const {
  return !current_source_url_.isEmpty();
}

bool TimelinePlaybackController::mediaReady() const {
  return player_.mediaStatus() == QMediaPlayer::LoadedMedia || player_.mediaStatus() == QMediaPlayer::BufferedMedia ||
         player_.mediaStatus() == QMediaPlayer::BufferingMedia || player_.mediaStatus() == QMediaPlayer::StalledMedia;
}

QString TimelinePlaybackController::errorString() const {
  return error_string_;
}

void TimelinePlaybackController::attachVideoOutput(QObject* output) {
  player_.setVideoOutput(output);
}

void TimelinePlaybackController::play() {
  if (timeline_document_.isNull()) {
    return;
  }

  timeline_document_->playSequence();
}

void TimelinePlaybackController::pause() {
  if (timeline_document_.isNull()) {
    return;
  }

  timeline_document_->pauseSequence();
}

void TimelinePlaybackController::togglePlayback() {
  if (timeline_document_.isNull()) {
    return;
  }

  if (timeline_document_->sequencePlaying()) {
    pause();
  } else {
    play();
  }
}

void TimelinePlaybackController::stepFrames(int direction) {
  if (timeline_document_.isNull()) {
    return;
  }

  timeline_document_->stepSequenceFrames(direction == 0 ? 0 : direction > 0 ? 1 : -1);
}

void TimelinePlaybackController::seekSequenceSeconds(double seconds) {
  if (timeline_document_.isNull()) {
    return;
  }

  timeline_document_->seekSequenceSeconds(seconds);
}

void TimelinePlaybackController::seekCurrentClipSeconds(double seconds) {
  if (timeline_document_.isNull()) {
    return;
  }

  timeline_document_->seekCurrentClipSeconds(seconds);
}

void TimelinePlaybackController::syncToDocument() {
  if (timeline_document_.isNull()) {
    return;
  }

  const auto next_source_url = timeline_document_->playbackSourceUrl();
  const auto target_position_ms =
      qRound64(timeline_document_->currentClipPlayheadSeconds() * 1000.0);
  const auto current_clip = timeline_document_->currentClip();
  const auto audio_playback_enabled =
      !current_clip.contains(QStringLiteral("audioPlaybackEnabled")) ||
      current_clip.value(QStringLiteral("audioPlaybackEnabled")).toBool();
  audio_output_.setVolume(audio_playback_enabled ? 1.0 : 0.0);

  if (current_source_url_ != next_source_url) {
    current_source_url_ = next_source_url;
    pending_position_ms_ = target_position_ms;
    play_when_ready_ = timeline_document_->sequencePlaying();
    setErrorString(QString{});

    if (current_source_url_.isEmpty()) {
      player_.stop();
      player_.setSource(QUrl());
    } else {
      player_.setSource(QUrl(current_source_url_));
    }

    emit stateChanged();
    return;
  }

  if (current_source_url_.isEmpty()) {
    player_.stop();
    setErrorString(QString{});
    emit stateChanged();
    return;
  }

  if (qAbs(player_.position() - target_position_ms) > kSeekToleranceMs) {
    player_.setPosition(target_position_ms);
  }

  if (timeline_document_->sequencePlaying()) {
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

void TimelinePlaybackController::setErrorString(const QString& error_string) {
  if (error_string_ == error_string) {
    return;
  }

  error_string_ = error_string;
  emit stateChanged();
}

}  // namespace photo_editor::desktop

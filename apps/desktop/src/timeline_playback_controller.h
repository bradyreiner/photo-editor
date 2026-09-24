#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <QtMultimedia/QMediaPlayer>

#include "timeline_audio_output.h"

namespace photo_editor::desktop {

class TimelineDocumentController;

class TimelinePlaybackController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool hasVideoSource READ hasVideoSource NOTIFY stateChanged)
  Q_PROPERTY(bool mediaReady READ mediaReady NOTIFY stateChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
  Q_PROPERTY(QString previewQualityMode READ previewQualityMode WRITE setPreviewQualityMode NOTIFY stateChanged)
  Q_PROPERTY(QString activeSourceUrl READ activeSourceUrl NOTIFY stateChanged)
  Q_PROPERTY(bool usingProxy READ usingProxy NOTIFY stateChanged)
  Q_PROPERTY(bool usingRenderCache READ usingRenderCache NOTIFY stateChanged)
  Q_PROPERTY(QString effectivePreviewQuality READ effectivePreviewQuality NOTIFY stateChanged)
  Q_PROPERTY(QString playbackHealth READ playbackHealth NOTIFY stateChanged)
  Q_PROPERTY(double bufferProgress READ bufferProgress NOTIFY stateChanged)
  Q_PROPERTY(bool adaptiveQualityActive READ adaptiveQualityActive NOTIFY stateChanged)
  Q_PROPERTY(QString adaptiveDetailLevel READ adaptiveDetailLevel NOTIFY stateChanged)
  Q_PROPERTY(QString adaptiveQualityReason READ adaptiveQualityReason NOTIFY stateChanged)
  Q_PROPERTY(double effectivePreviewScale READ effectivePreviewScale NOTIFY stateChanged)
  Q_PROPERTY(bool playbackRangeActive READ playbackRangeActive NOTIFY stateChanged)
  Q_PROPERTY(bool playbackRangeLooping READ playbackRangeLooping NOTIFY stateChanged)
  Q_PROPERTY(double playbackRangeStart READ playbackRangeStart NOTIFY stateChanged)
  Q_PROPERTY(double playbackRangeEnd READ playbackRangeEnd NOTIFY stateChanged)
  Q_PROPERTY(bool interactionActive READ interactionActive WRITE setInteractionActive NOTIFY stateChanged)
  Q_PROPERTY(double activePlaybackRate READ activePlaybackRate NOTIFY stateChanged)
  Q_PROPERTY(double shuttleRate READ shuttleRate NOTIFY stateChanged)
  Q_PROPERTY(bool shuttling READ shuttling NOTIFY stateChanged)
  Q_PROPERTY(QVariantList prefetchPlan READ prefetchPlan NOTIFY stateChanged)
  Q_PROPERTY(QString prefetchedSourceUrl READ prefetchedSourceUrl NOTIFY stateChanged)
  Q_PROPERTY(double prefetchDirection READ prefetchDirection NOTIFY stateChanged)
  Q_PROPERTY(double audioPeakLeft READ audioPeakLeft NOTIFY stateChanged)
  Q_PROPERTY(double audioPeakRight READ audioPeakRight NOTIFY stateChanged)
  Q_PROPERTY(double audioTruePeakLeft READ audioTruePeakLeft NOTIFY stateChanged)
  Q_PROPERTY(double audioTruePeakRight READ audioTruePeakRight NOTIFY stateChanged)
  Q_PROPERTY(double audioRmsLeft READ audioRmsLeft NOTIFY stateChanged)
  Q_PROPERTY(double audioRmsRight READ audioRmsRight NOTIFY stateChanged)
  Q_PROPERTY(bool audioClipping READ audioClipping NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap playbackDiagnostics READ playbackDiagnostics NOTIFY stateChanged)
  Q_PROPERTY(int droppedFrameCount READ droppedFrameCount NOTIFY stateChanged)
  Q_PROPERTY(bool runtimeRecoveryBusy READ runtimeRecoveryBusy NOTIFY stateChanged)
  Q_PROPERTY(QString runtimeRecoveryKind READ runtimeRecoveryKind NOTIFY stateChanged)
  Q_PROPERTY(QString runtimeRecoveryStatus READ runtimeRecoveryStatus NOTIFY stateChanged)
  Q_PROPERTY(int runtimeRecoveryCount READ runtimeRecoveryCount NOTIFY stateChanged)

 public:
  explicit TimelinePlaybackController(TimelineDocumentController* timeline_document, QObject* parent = nullptr);

  [[nodiscard]] bool hasVideoSource() const;
  [[nodiscard]] bool mediaReady() const;
  [[nodiscard]] QString errorString() const;
  [[nodiscard]] QString previewQualityMode() const;
  [[nodiscard]] QString activeSourceUrl() const;
  [[nodiscard]] bool usingProxy() const;
  [[nodiscard]] bool usingRenderCache() const;
  [[nodiscard]] QString effectivePreviewQuality() const;
  [[nodiscard]] QString playbackHealth() const;
  [[nodiscard]] double bufferProgress() const;
  [[nodiscard]] bool adaptiveQualityActive() const;
  [[nodiscard]] QString adaptiveDetailLevel() const;
  [[nodiscard]] QString adaptiveQualityReason() const;
  [[nodiscard]] double effectivePreviewScale() const;
  [[nodiscard]] bool playbackRangeActive() const;
  [[nodiscard]] bool playbackRangeLooping() const;
  [[nodiscard]] double playbackRangeStart() const;
  [[nodiscard]] double playbackRangeEnd() const;
  [[nodiscard]] bool interactionActive() const;
  [[nodiscard]] double activePlaybackRate() const;
  [[nodiscard]] double shuttleRate() const;
  [[nodiscard]] bool shuttling() const;
  [[nodiscard]] QVariantList prefetchPlan() const;
  [[nodiscard]] QString prefetchedSourceUrl() const;
  [[nodiscard]] double prefetchDirection() const;
  [[nodiscard]] double audioPeakLeft() const;
  [[nodiscard]] double audioPeakRight() const;
  [[nodiscard]] double audioTruePeakLeft() const;
  [[nodiscard]] double audioTruePeakRight() const;
  [[nodiscard]] double audioRmsLeft() const;
  [[nodiscard]] double audioRmsRight() const;
  [[nodiscard]] bool audioClipping() const;
  [[nodiscard]] QVariantMap playbackDiagnostics() const;
  [[nodiscard]] int droppedFrameCount() const;
  [[nodiscard]] bool runtimeRecoveryBusy() const;
  [[nodiscard]] QString runtimeRecoveryKind() const;
  [[nodiscard]] QString runtimeRecoveryStatus() const;
  [[nodiscard]] int runtimeRecoveryCount() const;

  void setPreviewQualityMode(const QString& mode);
  void setInteractionActive(bool active);

  Q_INVOKABLE void attachVideoOutput(QObject* output);
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void togglePlayback();
  Q_INVOKABLE void resetAudioMeters();
  Q_INVOKABLE void shuttleReverse();
  Q_INVOKABLE void stopShuttle();
  Q_INVOKABLE void shuttleForward();
  Q_INVOKABLE void stepFrames(int direction);
  Q_INVOKABLE void seekSequenceSeconds(double seconds);
  Q_INVOKABLE void seekCurrentClipSeconds(double seconds);
  Q_INVOKABLE void setPlaybackLoop(double start_seconds, double end_seconds);
  Q_INVOKABLE void clearPlaybackLoop();
  Q_INVOKABLE bool playReviewRange(
      double start_seconds,
      double end_seconds,
      double preroll_seconds,
      double postroll_seconds,
      bool loop);
  Q_INVOKABLE bool playInToOut(double start_seconds, double end_seconds);
  Q_INVOKABLE bool reviewFromEditPoint(
      double edit_seconds,
      double preroll_seconds,
      double postroll_seconds);
  Q_INVOKABLE void stopReviewPlayback();
  Q_INVOKABLE void setLikelyNextEditSeconds(double seconds);
  Q_INVOKABLE void recoverRuntimeFailure(
      const QString& kind,
      const QString& detail = QString());
  Q_INVOKABLE void resetPlaybackDiagnostics();
  void recordPlaybackTimingSample(qint64 position_delta_ms, qint64 wall_delta_ms);

 signals:
  void stateChanged();
  void runtimeRecoveryRequested(const QString& kind);

 private:
  void syncToDocument();
  void startGapPlayback();
  void stopGapPlayback();
  void tickGapPlayback();
  void updateAudioVolume(qint64 source_position_ms);
  void setErrorString(const QString& error_string);
  void schedulePrefetchRefresh();
  void refreshPrefetchPlan();
  void publishPlaybackDiagnostic(
      const QString& trigger,
      int dropped_frames,
      const QString& detail = QString());
  void applyAdaptivePlaybackPressure(const QString& reason, bool severe);
  void recordAdaptiveHealthySample();
  void resetAdaptivePlaybackDetail();
  [[nodiscard]] QString preferredSourceUrl() const;

  QPointer<TimelineDocumentController> timeline_document_;
  TimelineAudioOutput audio_output_;
  QMediaPlayer player_;
  QMediaPlayer prefetch_player_;
  QString current_source_url_;
  QString preview_quality_mode_ = QStringLiteral("auto");
  QString error_string_;
  qint64 pending_position_ms_ = -1;
  qint64 awaited_position_ms_ = -1;
  bool awaiting_media_source_ = false;
  bool awaiting_position_sync_ = false;
  bool play_when_ready_ = false;
  bool native_media_enabled_ = true;
  bool interaction_active_ = false;
  bool using_proxy_ = false;
  bool using_render_cache_ = false;
  double active_playback_rate_ = 1.0;
  double shuttle_rate_ = 0.0;
  double render_cache_range_start_seconds_ = 0.0;
  QVariantList prefetch_plan_;
  QString prefetched_source_url_;
  double prefetch_direction_ = 1.0;
  double loop_start_seconds_ = -1.0;
  double loop_end_seconds_ = -1.0;
  bool playback_range_active_ = false;
  bool playback_range_looping_ = false;
  double likely_next_edit_seconds_ = -1.0;
  QTimer prefetch_timer_;
  QTimer runtime_recovery_timer_;
  QTimer gap_timer_;
  QElapsedTimer gap_elapsed_;
  QElapsedTimer playback_sample_clock_;
  qint64 last_playback_sample_position_ms_ = -1;
  double gap_start_sequence_seconds_ = 0.0;
  bool runtime_recovery_busy_ = false;
  QString runtime_recovery_kind_;
  QString runtime_recovery_status_;
  QString runtime_recovery_document_hash_;
  int runtime_recovery_count_ = 0;
  QVariantMap playback_diagnostics_;
  int dropped_frame_count_ = 0;
  QString adaptive_detail_level_ = QStringLiteral("full");
  QString adaptive_quality_reason_;
  int adaptive_slow_sample_count_ = 0;
  int adaptive_healthy_sample_count_ = 0;
};

}  // namespace photo_editor::desktop

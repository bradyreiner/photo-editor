#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QSize>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <atomic>
#include <memory>

#include <QtMultimedia/QMediaPlayer>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoSink>

#include "timeline_audio_output.h"

namespace photo_editor::desktop {

class TimelineDocumentController;
struct TimelineVideoGradeRenderSession;

struct TimelinePlaybackEffectParameters final {
  struct SpatialColorEffect final {
    QString name;
    double amount = 0.0;
    QString mask_shape = QStringLiteral("ellipse");
    double mask_center_x = 0.5;
    double mask_center_y = 0.5;
    double mask_width = 0.65;
    double mask_height = 0.65;
    double mask_feather = 0.04;
    bool mask_inverted = false;
  };

  double exposure = 0.0;
  double contrast = 0.0;
  double saturation = 0.0;
  double black_and_white = 0.0;
  double fade = 0.0;
  double punch = 0.0;
  double blur = 0.0;
  bool blur_mask_enabled = false;
  QString blur_mask_shape = QStringLiteral("ellipse");
  double blur_mask_center_x = 0.5;
  double blur_mask_center_y = 0.5;
  double blur_mask_width = 0.65;
  double blur_mask_height = 0.65;
  double blur_mask_feather = 0.04;
  bool blur_mask_inverted = false;
  QVector<SpatialColorEffect> spatial_color_effects;
};

struct TimelinePlaybackAdjustmentGrade final {
  QString clip_id;
  QString recipe_json;
  double opacity = 1.0;
  QVariantMap animated_parameters;
  std::shared_ptr<TimelineVideoGradeRenderSession> session;
};

struct TimelinePlaybackFrameImageState final {
  void setImage(QImage image);
  [[nodiscard]] QImage requestImage(QSize* size, const QSize& requested_size) const;

 private:
  mutable QMutex mutex_;
  QImage image_;
};

struct TimelineProcessedVideoFrame final {
  QImage image;
  bool cache_enabled = false;
  bool cache_hit = false;
  bool cache_key_eligible = false;
  bool cache_lookup_performed = false;
  qint64 frame_timestamp_us = -1;
  QString cache_identity_kind;
  QString cache_key_unavailable_reason;
  double cache_lookup_ms = 0.0;
  double total_ms = 0.0;
};

class TimelinePlaybackImageProvider final : public QQuickImageProvider {
 public:
  explicit TimelinePlaybackImageProvider(
      std::shared_ptr<TimelinePlaybackFrameImageState> image_state);
  [[nodiscard]] QImage requestImage(
      const QString& id,
      QSize* size,
      const QSize& requested_size) override;

 private:
  std::shared_ptr<TimelinePlaybackFrameImageState> image_state_;
};

class TimelinePlaybackController final : public QObject {
  friend struct TimelinePlaybackFrameTestAccess;
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
  Q_PROPERTY(bool workspaceActive READ workspaceActive WRITE setWorkspaceActive NOTIFY stateChanged)
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
  Q_PROPERTY(QVariantMap frameProcessingMetrics READ frameProcessingMetrics NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap colorCacheDiagnostics READ colorCacheDiagnostics NOTIFY stateChanged)
  Q_PROPERTY(int droppedFrameCount READ droppedFrameCount NOTIFY stateChanged)
  Q_PROPERTY(bool runtimeRecoveryBusy READ runtimeRecoveryBusy NOTIFY stateChanged)
  Q_PROPERTY(QString runtimeRecoveryKind READ runtimeRecoveryKind NOTIFY stateChanged)
  Q_PROPERTY(QString runtimeRecoveryStatus READ runtimeRecoveryStatus NOTIFY stateChanged)
  Q_PROPERTY(int runtimeRecoveryCount READ runtimeRecoveryCount NOTIFY stateChanged)
  Q_PROPERTY(QString processedFrameSource READ processedFrameSource NOTIFY processedFrameSourceChanged)
  Q_PROPERTY(qint64 processedFrameTimestampUs READ processedFrameTimestampUs NOTIFY processedFrameSourceChanged)
  Q_PROPERTY(bool processedFrameTimestampEstimated READ processedFrameTimestampEstimated NOTIFY processedFrameSourceChanged)
  Q_PROPERTY(QString colorGradeError READ colorGradeError NOTIFY stateChanged)
  Q_PROPERTY(QString colorSignalProcessingWarning READ colorSignalProcessingWarning NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap colorSignalMetadata READ colorSignalMetadata NOTIFY colorSignalMetadataChanged)

 public:
  explicit TimelinePlaybackController(TimelineDocumentController* timeline_document, QObject* parent = nullptr);

  ~TimelinePlaybackController() override;

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
  [[nodiscard]] QVariantMap frameProcessingMetrics() const;
  [[nodiscard]] QVariantMap colorCacheDiagnostics() const;
  [[nodiscard]] int droppedFrameCount() const;
  [[nodiscard]] bool runtimeRecoveryBusy() const;
  [[nodiscard]] QString runtimeRecoveryKind() const;
  [[nodiscard]] QString runtimeRecoveryStatus() const;
  [[nodiscard]] int runtimeRecoveryCount() const;
  [[nodiscard]] QString processedFrameSource() const;
  [[nodiscard]] qint64 processedFrameTimestampUs() const;
  [[nodiscard]] bool processedFrameTimestampEstimated() const;
  [[nodiscard]] QString colorGradeError() const;
  [[nodiscard]] QString colorSignalProcessingWarning() const;
  [[nodiscard]] QVariantMap colorSignalMetadata() const;
  [[nodiscard]] QQuickImageProvider* createImageProvider();

  void setPreviewQualityMode(const QString& mode);
  void setInteractionActive(bool active);
  [[nodiscard]] bool workspaceActive() const;
  void setWorkspaceActive(bool active);

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
  Q_INVOKABLE void setTimelineEffectsBypassed(bool bypassed);
  Q_INVOKABLE void recoverRuntimeFailure(
      const QString& kind,
      const QString& detail = QString());
  Q_INVOKABLE void resetPlaybackDiagnostics();
  void recordPlaybackTimingSample(qint64 position_delta_ms, qint64 wall_delta_ms);

 signals:
  void stateChanged();
  void processedFrameSourceChanged();
  void colorSignalMetadataChanged();
  void runtimeRecoveryRequested(const QString& kind);

 private:
  void syncToDocument();
  void startGapPlayback();
  void stopGapPlayback();
  void tickGapPlayback();
  void syncScenePreviewFrame();
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
  [[nodiscard]] QSize frameTargetSize() const;
  void handleVideoFrame(const QVideoFrame& frame);
  void startFrameProcessing(
      const QVideoFrame& frame,
      quint64 generation,
      int work_generation,
      qint64 source_timestamp_us,
      bool source_timestamp_estimated,
      bool scene_preview_frame = false,
      const QString& scene_preview_identity = {});
  void reprocessCurrentPausedFrameIfNeeded();
  void invalidateFrameProcessing();
  void clearPendingSeekFrame();
  void publishProcessedFrame(
      QImage image,
      qint64 timestamp_us,
      bool timestamp_estimated);
  void clearProcessedFrame();
  void setColorSignalMetadata(QVariantMap metadata);

  QPointer<TimelineDocumentController> timeline_document_;
  TimelineAudioOutput audio_output_;
  QVideoSink video_sink_;
  QMediaPlayer player_;
  QMediaPlayer prefetch_player_;
  QFutureWatcher<TimelineProcessedVideoFrame> frame_processing_watcher_;
  QVideoFrame latest_pending_frame_;
  qint64 pending_frame_source_timestamp_us_ = -1;
  bool pending_frame_source_timestamp_estimated_ = false;
  bool pending_frame_is_scene_preview_ = false;
  QString pending_scene_preview_identity_;
  QVideoFrame pending_seek_video_frame_;
  quint64 position_sync_generation_ = 0U;
  quint64 pending_seek_frame_generation_ = 0U;
  QPointer<QQuickItem> video_frame_item_;
  std::shared_ptr<TimelinePlaybackFrameImageState> frame_image_state_ =
      std::make_shared<TimelinePlaybackFrameImageState>();
  QString current_source_url_;
  QString active_video_source_identity_;
  QString active_video_source_revision_;
  QString active_video_input_transform_;
  QString processing_video_input_transform_;
  QString active_video_creative_lut_path_;
  QString active_video_grade_recipe_json_;
  QVariantMap active_video_grade_parameters_;
  std::shared_ptr<TimelineVideoGradeRenderSession> active_video_grade_session_;
  QVector<TimelinePlaybackAdjustmentGrade> active_video_adjustment_grades_;
  QHash<QString, std::shared_ptr<TimelineVideoGradeRenderSession>>
      video_grade_render_sessions_;
  QString color_grade_error_;
  QString color_signal_processing_warning_;
  QVariantMap color_signal_metadata_;
  QString active_video_frame_color_key_;
  QString processing_video_frame_color_key_;
  QVariantMap active_video_foreground_cutout_clip_;
  QString active_video_foreground_cutout_key_;
  QString processing_video_foreground_cutout_key_;
  double active_video_shot_match_red_gain_ = 1.0;
  double active_video_shot_match_green_gain_ = 1.0;
  double active_video_shot_match_blue_gain_ = 1.0;
  TimelinePlaybackEffectParameters active_video_effects_;
  int active_sequence_width_ = 0;
  int active_sequence_height_ = 0;
  QString preview_quality_mode_ = QStringLiteral("auto");
  QString error_string_;
  qint64 frame_source_revision_ = 0;
  qint64 processed_frame_timestamp_us_ = -1;
  bool processed_frame_timestamp_estimated_ = false;
  bool processed_frame_available_ = false;
  quint64 video_frame_generation_ = 0U;
  quint64 processing_frame_generation_ = 0U;
  quint64 pending_frame_generation_ = 0U;
  qint64 processing_frame_source_timestamp_us_ = -1;
  bool processing_frame_source_timestamp_estimated_ = false;
  bool processing_frame_is_scene_preview_ = false;
  QString processing_scene_preview_identity_;
  int processing_work_generation_ = 0;
  int pending_work_generation_ = 0;
  std::shared_ptr<std::atomic<int>> latest_frame_work_generation_ =
      std::make_shared<std::atomic<int>>(0);
  qint64 pending_position_ms_ = -1;
  qint64 awaited_position_ms_ = -1;
  bool awaiting_media_source_ = false;
  bool awaiting_position_sync_ = false;
  bool play_when_ready_ = false;
  bool native_media_enabled_ = true;
  bool timeline_effects_bypassed_ = false;
  bool frame_processing_active_ = false;
  bool has_pending_video_frame_ = false;
  bool interaction_active_ = false;
  bool workspace_active_ = true;
  bool using_proxy_ = false;
  bool using_render_cache_ = false;
  bool scene_preview_cache_active_ = false;
  bool injecting_scene_preview_frame_ = false;
  QString scene_preview_frame_key_;
  QString scene_preview_error_;
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
  QVariantMap frame_processing_metrics_;
  int frame_processing_metric_samples_ = 0;
  int dropped_frame_count_ = 0;
  QString adaptive_detail_level_ = QStringLiteral("full");
  QString adaptive_quality_reason_;
  int adaptive_slow_sample_count_ = 0;
  int adaptive_healthy_sample_count_ = 0;
};

}  // namespace photo_editor::desktop

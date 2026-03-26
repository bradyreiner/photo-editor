#pragma once

#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace photo_editor::desktop {

class TimelineDocumentController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList sequenceTracks READ sequenceTracks NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantMap currentClip READ currentClip NOTIFY selectionChanged)
  Q_PROPERTY(int clipCount READ clipCount NOTIFY clipsChanged)
  Q_PROPERTY(int currentIndex READ currentIndex NOTIFY selectionChanged)
  Q_PROPERTY(int trackCount READ trackCount NOTIFY sequenceChanged)
  Q_PROPERTY(int videoTrackCount READ videoTrackCount NOTIFY sequenceChanged)
  Q_PROPERTY(int audioTrackCount READ audioTrackCount NOTIFY sequenceChanged)
  Q_PROPERTY(double framesPerSecond READ framesPerSecond CONSTANT)
  Q_PROPERTY(QString sequenceFormatLabel READ sequenceFormatLabel NOTIFY sequenceChanged)
  Q_PROPERTY(QString timelineName READ timelineName WRITE setTimelineName NOTIFY timelineNameChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
  Q_PROPERTY(QString totalDurationText READ totalDurationText NOTIFY clipsChanged)
  Q_PROPERTY(double totalDurationSeconds READ totalDurationSecondsValue NOTIFY clipsChanged)
  Q_PROPERTY(double sequencePlayheadSeconds READ sequencePlayheadSeconds NOTIFY playbackStateChanged)
  Q_PROPERTY(double currentClipPlayheadSeconds READ currentClipPlayheadSeconds NOTIFY playbackStateChanged)
  Q_PROPERTY(bool sequencePlaying READ sequencePlaying NOTIFY playbackStateChanged)
  Q_PROPERTY(QString playbackSourceUrl READ playbackSourceUrl NOTIFY playbackStateChanged)
  Q_PROPERTY(QString previewSource READ previewSource NOTIFY previewSourceChanged)
  Q_PROPERTY(QString savedDocumentPath READ savedDocumentPath NOTIFY savedPathsChanged)
  Q_PROPERTY(QString savedRenderPath READ savedRenderPath NOTIFY savedPathsChanged)

 public:
  explicit TimelineDocumentController(QObject* parent = nullptr);

  [[nodiscard]] QVariantList clips() const;
  [[nodiscard]] QVariantList sequenceTracks() const;
  [[nodiscard]] QVariantMap currentClip() const;
  [[nodiscard]] int clipCount() const;
  [[nodiscard]] int currentIndex() const;
  [[nodiscard]] int trackCount() const;
  [[nodiscard]] int videoTrackCount() const;
  [[nodiscard]] int audioTrackCount() const;
  [[nodiscard]] double framesPerSecond() const;
  [[nodiscard]] QString sequenceFormatLabel() const;
  [[nodiscard]] QString timelineName() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] bool dirty() const;
  [[nodiscard]] QString totalDurationText() const;
  [[nodiscard]] double totalDurationSecondsValue() const;
  [[nodiscard]] double sequencePlayheadSeconds() const;
  [[nodiscard]] double currentClipPlayheadSeconds() const;
  [[nodiscard]] bool sequencePlaying() const;
  [[nodiscard]] QString playbackSourceUrl() const;
  [[nodiscard]] QString previewSource() const;
  [[nodiscard]] QString savedDocumentPath() const;
  [[nodiscard]] QString savedRenderPath() const;

  void setTimelineName(const QString& timeline_name);

  Q_INVOKABLE void createTimeline(const QString& timeline_name = QString());
  Q_INVOKABLE bool loadTimeline(const QString& document_path);
  Q_INVOKABLE bool saveTimeline();
  Q_INVOKABLE void selectClip(int index);
  Q_INVOKABLE void addClipFromGallery(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      const QString& poster_source = QString());
  Q_INVOKABLE void insertClipFromGallery(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      double trim_start_seconds,
      double trim_end_seconds,
      const QString& poster_source = QString());
  Q_INVOKABLE void overwriteClipFromGallery(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      double trim_start_seconds,
      double trim_end_seconds,
      const QString& poster_source = QString());
  Q_INVOKABLE void addVideoTrack();
  Q_INVOKABLE void moveSelectedClipToTrackRow(int track_row_index, double start_seconds);
  Q_INVOKABLE void removeSelectedClip();
  Q_INVOKABLE void moveSelectedBackward();
  Q_INVOKABLE void moveSelectedForward();
  Q_INVOKABLE void setSelectedTrimStartSeconds(double seconds);
  Q_INVOKABLE void setSelectedTrimEndSeconds(double seconds);
  Q_INVOKABLE void trimSelectedClipStartToSequenceSeconds(double seconds, bool ripple = false);
  Q_INVOKABLE void trimSelectedClipEndToSequenceSeconds(double seconds, bool ripple = false);
  Q_INVOKABLE void setSelectedFrameMode(const QString& frame_mode);
  Q_INVOKABLE void setSelectedFrameScale(double frame_scale);
  Q_INVOKABLE void setSelectedFrameOffsetX(double frame_offset_x);
  Q_INVOKABLE void setSelectedFrameOffsetY(double frame_offset_y);
  Q_INVOKABLE double sourceDurationSecondsForFile(const QString& file_path) const;
  Q_INVOKABLE bool splitSelectedClip(double split_seconds);
  Q_INVOKABLE void playSequence();
  Q_INVOKABLE void pauseSequence();
  Q_INVOKABLE void seekSequenceSeconds(double seconds);
  Q_INVOKABLE void seekCurrentClipSeconds(double seconds);
  Q_INVOKABLE void stepSequenceFrames(int direction);
  Q_INVOKABLE void syncPlaybackPosition(double clip_seconds);
  Q_INVOKABLE bool advancePlaybackAfterClipEnd();

 signals:
  void clipsChanged();
  void sequenceChanged();
  void selectionChanged();
  void timelineNameChanged();
  void statusChanged();
  void dirtyChanged();
  void previewSourceChanged();
  void savedPathsChanged();
  void playbackStateChanged();

 private:
  enum class TrackKind {
    Video,
    Audio,
  };

  struct Clip final {
    QString clip_id;
    QString asset_id;
    QString file_path;
    QString display_label;
    QString poster_source;
    QString linked_group_id;
    double duration_seconds = 0.0;
    double trim_start_seconds = 0.0;
    double trim_end_seconds = 0.0;
    double start_seconds = 0.0;
    bool has_audio = false;
    QString waveform_source;
    QString frame_mode = QStringLiteral("fit");
    double frame_scale = 1.0;
    double frame_offset_x = 0.0;
    double frame_offset_y = 0.0;
  };

  struct Track final {
    QString track_id;
    QString display_label;
    TrackKind kind = TrackKind::Video;
    int lane_index = 0;
    bool locked = false;
    bool muted = false;
    bool solo = false;
    QVector<Clip> clips;
  };

  struct ClipRef final {
    int track_index = -1;
    int clip_index = -1;
    [[nodiscard]] bool isValid() const { return track_index >= 0 && clip_index >= 0; }
  };

  [[nodiscard]] QVariantMap clipMap(int index) const;
  [[nodiscard]] QVariantMap clipMap(const Clip& clip, int flattened_index, const Track& track) const;
  [[nodiscard]] QVariantMap trackMap(const Track& track, int track_index) const;
  [[nodiscard]] bool isVideoFile(const QString& file_path) const;
  [[nodiscard]] QVector<ClipRef> orderedVideoClipRefs() const;
  [[nodiscard]] ClipRef clipRefAt(int index) const;
  [[nodiscard]] ClipRef selectedClipRef() const;
  [[nodiscard]] const Clip* clipAt(int index) const;
  [[nodiscard]] Clip* clipAt(int index);
  [[nodiscard]] const Clip* clipAt(const ClipRef& ref) const;
  [[nodiscard]] Clip* clipAt(const ClipRef& ref);
  [[nodiscard]] const Track* trackAt(int track_index) const;
  [[nodiscard]] Track* trackAt(int track_index);
  [[nodiscard]] const Track* primaryVideoTrack() const;
  [[nodiscard]] Track* primaryVideoTrack();
  [[nodiscard]] const Track* videoTrackForLane(int lane_index) const;
  [[nodiscard]] Track* videoTrackForLane(int lane_index);
  [[nodiscard]] const Track* videoTrackForRowIndex(int track_row_index) const;
  [[nodiscard]] Track* videoTrackForRowIndex(int track_row_index);
  [[nodiscard]] Track* defaultTargetVideoTrack();
  [[nodiscard]] const Track* pairedAudioTrack(const Track& video_track) const;
  [[nodiscard]] Track* pairedAudioTrack(const Track& video_track);
  [[nodiscard]] ClipRef activeVideoClipRefForTime(double seconds) const;
  [[nodiscard]] ClipRef nextActiveVideoClipRefAfter(double seconds) const;
  [[nodiscard]] int flattenedIndexForClipId(const QString& clip_id) const;
  [[nodiscard]] int nextLaneIndex() const;
  [[nodiscard]] double clipTrimmedDuration(const Clip& clip) const;
  [[nodiscard]] double clipTrimmedDuration(int index) const;
  [[nodiscard]] double clipEndSeconds(const Clip& clip) const;
  [[nodiscard]] double clipTimelineOffsetSeconds(int index) const;
  [[nodiscard]] double trackEndSeconds(const Track& track) const;
  [[nodiscard]] double totalDurationSeconds() const;
  [[nodiscard]] double clipLocalSecondsForSequenceTime(const Clip& clip, double seconds) const;
  [[nodiscard]] QString formatDurationText(double seconds) const;
  [[nodiscard]] double probeVideoDurationSeconds(const QString& file_path) const;
  [[nodiscard]] bool probeVideoHasAudio(const QString& file_path) const;
  [[nodiscard]] QString ensureVideoPoster(const QString& file_path, const QString& cache_key = QString()) const;
  [[nodiscard]] QString ensureAudioWaveform(const QString& file_path, const QString& cache_key = QString()) const;
  [[nodiscard]] QSize probeVideoSize(const QString& file_path) const;
  [[nodiscard]] bool renderTimelineVideo(const QString& output_path) const;
  [[nodiscard]] bool persistDocument(const QString& document_path) const;
  [[nodiscard]] QString clipVideoFilter(const Clip& clip, int input_index, int output_index, const QSize& target_size) const;
  [[nodiscard]] Clip buildGalleryClip(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      double trim_start_seconds,
      double trim_end_seconds,
      const QString& poster_source) const;
  void shiftTrackClipsFrom(Track* track, double sequence_seconds, double delta_seconds, const QString& excluded_clip_id = QString());
  void splitTrackClipAtSequenceSeconds(Track* track, int clip_index, double sequence_seconds);
  void overwriteTrackRange(Track* track, double range_start_seconds, double range_end_seconds);
  void initializeDefaultTracks();
  void appendTrackPair();
  void syncAudioTracks();
  void refreshSequenceSizeFromContent();
  void updateCurrentIndexFromSelection();
  void ensureCurrentIndexValid();
  void setStatus(const QString& status);
  void setDirty(bool dirty);
  void clampPlaybackState();
  void emitAllState();

  QVector<Track> tracks_;
  QString timeline_id_;
  QString timeline_name_ = QStringLiteral("Untitled Timeline");
  QString status_ = QStringLiteral("Timeline ready.");
  QString preview_source_;
  QString saved_document_path_;
  QString saved_render_path_;
  QString saved_poster_path_;
  QString created_at_;
  QString selected_clip_id_;
  QSize sequence_size_ = QSize(1080, 1350);
  int current_index_ = -1;
  bool dirty_ = false;
  double sequence_playhead_seconds_ = 0.0;
  double current_clip_playhead_seconds_ = 0.0;
  bool sequence_playing_ = false;
};

}  // namespace photo_editor::desktop

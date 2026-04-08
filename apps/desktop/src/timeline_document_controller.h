#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace photo_editor::desktop {

class TimelineDocumentController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool hasActiveTimeline READ hasActiveTimeline NOTIFY timelineStateChanged)
  Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList sequenceTracks READ sequenceTracks NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantMap currentClip READ currentClip NOTIFY selectionChanged)
  Q_PROPERTY(int clipCount READ clipCount NOTIFY clipsChanged)
  Q_PROPERTY(int currentIndex READ currentIndex NOTIFY selectionChanged)
  Q_PROPERTY(int trackCount READ trackCount NOTIFY sequenceChanged)
  Q_PROPERTY(int videoTrackCount READ videoTrackCount NOTIFY sequenceChanged)
  Q_PROPERTY(int audioTrackCount READ audioTrackCount NOTIFY sequenceChanged)
  Q_PROPERTY(double framesPerSecond READ framesPerSecond NOTIFY sequenceChanged)
  Q_PROPERTY(int sequenceWidth READ sequenceWidth NOTIFY sequenceChanged)
  Q_PROPERTY(int sequenceHeight READ sequenceHeight NOTIFY sequenceChanged)
  Q_PROPERTY(int audioSampleRate READ audioSampleRate NOTIFY sequenceChanged)
  Q_PROPERTY(QString projectBinding READ projectBinding NOTIFY sequenceChanged)
  Q_PROPERTY(QString colorSpace READ colorSpace NOTIFY sequenceChanged)
  Q_PROPERTY(QString dataLevels READ dataLevels NOTIFY sequenceChanged)
  Q_PROPERTY(QString exportContainer READ exportContainer NOTIFY sequenceChanged)
  Q_PROPERTY(QString exportCodec READ exportCodec NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantMap sequenceSettings READ sequenceSettings NOTIFY sequenceChanged)
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
  Q_PROPERTY(QString timelineAssetId READ timelineAssetId NOTIFY savedPathsChanged)
  Q_PROPERTY(QString activeTool READ activeTool WRITE setActiveTool NOTIFY editorStateChanged)
  Q_PROPERTY(QString editMode READ editMode WRITE setEditMode NOTIFY editorStateChanged)
  Q_PROPERTY(bool linkedSelectionEnabled READ linkedSelectionEnabled WRITE setLinkedSelectionEnabled NOTIFY editorStateChanged)
  Q_PROPERTY(QVariantList sequenceMarkers READ sequenceMarkers NOTIFY markersChanged)
  Q_PROPERTY(QVariantMap sourceClip READ sourceClip NOTIFY sourceMonitorChanged)
  Q_PROPERTY(bool hasSourceClip READ hasSourceClip NOTIFY sourceMonitorChanged)
  Q_PROPERTY(double sourceInSeconds READ sourceInSeconds NOTIFY sourceMonitorChanged)
  Q_PROPERTY(double sourceOutSeconds READ sourceOutSeconds NOTIFY sourceMonitorChanged)
  Q_PROPERTY(QStringList selectedClipIds READ selectedClipIds NOTIFY selectionChanged)
  Q_PROPERTY(int selectedClipCount READ selectedClipCount NOTIFY selectionChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)

 public:
  explicit TimelineDocumentController(QObject* parent = nullptr);

  [[nodiscard]] bool hasActiveTimeline() const;
  [[nodiscard]] QVariantList clips() const;
  [[nodiscard]] QVariantList sequenceTracks() const;
  [[nodiscard]] QVariantMap currentClip() const;
  [[nodiscard]] int clipCount() const;
  [[nodiscard]] int currentIndex() const;
  [[nodiscard]] int trackCount() const;
  [[nodiscard]] int videoTrackCount() const;
  [[nodiscard]] int audioTrackCount() const;
  [[nodiscard]] double framesPerSecond() const;
  [[nodiscard]] int sequenceWidth() const;
  [[nodiscard]] int sequenceHeight() const;
  [[nodiscard]] int audioSampleRate() const;
  [[nodiscard]] QString projectBinding() const;
  [[nodiscard]] QString colorSpace() const;
  [[nodiscard]] QString dataLevels() const;
  [[nodiscard]] QString exportContainer() const;
  [[nodiscard]] QString exportCodec() const;
  [[nodiscard]] QVariantMap sequenceSettings() const;
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
  [[nodiscard]] QString timelineAssetId() const;
  [[nodiscard]] QString activeTool() const;
  [[nodiscard]] QString editMode() const;
  [[nodiscard]] bool linkedSelectionEnabled() const;
  [[nodiscard]] QVariantList sequenceMarkers() const;
  [[nodiscard]] QVariantMap sourceClip() const;
  [[nodiscard]] bool hasSourceClip() const;
  [[nodiscard]] double sourceInSeconds() const;
  [[nodiscard]] double sourceOutSeconds() const;
  [[nodiscard]] QStringList selectedClipIds() const;
  [[nodiscard]] int selectedClipCount() const;
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;

  void setTimelineName(const QString& timeline_name);
  void setActiveTool(const QString& active_tool);
  void setEditMode(const QString& edit_mode);
  void setLinkedSelectionEnabled(bool enabled);

  Q_INVOKABLE QVariantList recentTimelines() const;
  Q_INVOKABLE void createTimeline(const QString& timeline_name = QString());
  Q_INVOKABLE void createTimelineWithSettings(const QVariantMap& settings);
  Q_INVOKABLE void closeTimeline();
  Q_INVOKABLE bool loadTimeline(const QString& document_path);
  Q_INVOKABLE bool saveTimeline();
  [[nodiscard]] bool renderCurrentTimeline(const QString& output_path) const;
  [[nodiscard]] QString generateVideoPoster(const QString& file_path, const QString& cache_key = QString()) const;
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
  Q_INVOKABLE void liftSelectedClip();
  Q_INVOKABLE void extractSelectedClip();
  Q_INVOKABLE void liftSelectedClips();
  Q_INVOKABLE void extractSelectedClips();
  Q_INVOKABLE void removeSelectedClip();
  Q_INVOKABLE void moveSelectedBackward();
  Q_INVOKABLE void moveSelectedForward();
  Q_INVOKABLE void toggleTrackLocked(int track_row_index);
  Q_INVOKABLE void toggleTrackMuted(int track_row_index);
  Q_INVOKABLE void toggleTrackSolo(int track_row_index);
  Q_INVOKABLE void toggleTrackVisibility(int track_row_index);
  Q_INVOKABLE void toggleTrackTargeted(int track_row_index);
  Q_INVOKABLE void setSelectedTrimStartSeconds(double seconds);
  Q_INVOKABLE void setSelectedTrimEndSeconds(double seconds);
  Q_INVOKABLE void trimSelectedClipStartToSequenceSeconds(double seconds, bool ripple = false);
  Q_INVOKABLE void trimSelectedClipEndToSequenceSeconds(double seconds, bool ripple = false);
  Q_INVOKABLE bool rollSelectedClipStartToSequenceSeconds(double seconds);
  Q_INVOKABLE bool rollSelectedClipEndToSequenceSeconds(double seconds);
  Q_INVOKABLE bool slipSelectedClipBySeconds(double delta_seconds);
  Q_INVOKABLE void setSelectedFrameMode(const QString& frame_mode);
  Q_INVOKABLE void setSelectedFrameScale(double frame_scale);
  Q_INVOKABLE void setSelectedFrameOffsetX(double frame_offset_x);
  Q_INVOKABLE void setSelectedFrameOffsetY(double frame_offset_y);
  Q_INVOKABLE void setSelectedClipOpacity(double opacity);
  Q_INVOKABLE void setSelectedClipAudioGain(double gain_db);
  Q_INVOKABLE void setSelectedClipFadeInSeconds(double seconds);
  Q_INVOKABLE void setSelectedClipFadeOutSeconds(double seconds);
  Q_INVOKABLE void setSelectedClipPlaybackRate(double playback_rate);
  Q_INVOKABLE void addSelectedClipEffect(const QString& effect_name);
  Q_INVOKABLE void removeSelectedClipEffect(const QString& effect_name);
  Q_INVOKABLE void assignSelectedClipTransition(
      const QString& edge,
      const QString& transition_name,
      double duration_seconds = 0.35);
  Q_INVOKABLE void clearSelectedClipTransition(const QString& edge);
  Q_INVOKABLE double sourceDurationSecondsForFile(const QString& file_path) const;
  Q_INVOKABLE bool splitSelectedClip(double split_seconds);
  Q_INVOKABLE void loadSourceClipFromGallery(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      const QString& poster_source = QString());
  Q_INVOKABLE void clearSourceClip();
  Q_INVOKABLE void setSourceInSeconds(double seconds);
  Q_INVOKABLE void setSourceOutSeconds(double seconds);
  Q_INVOKABLE void clearSourceRange();
  Q_INVOKABLE void appendSourceClipToTimeline();
  Q_INVOKABLE void insertSourceClipAtPlayhead();
  Q_INVOKABLE void overwriteSourceClipAtPlayhead();
  Q_INVOKABLE QString addSequenceMarker(const QString& label = QString());
  Q_INVOKABLE void moveSequenceMarker(const QString& marker_id, double seconds);
  Q_INVOKABLE void renameSequenceMarker(const QString& marker_id, const QString& label);
  Q_INVOKABLE void setSequenceMarkerNote(const QString& marker_id, const QString& note);
  Q_INVOKABLE void setSequenceMarkerStatus(const QString& marker_id, const QString& status);
  Q_INVOKABLE void setSequenceMarkerCompleted(const QString& marker_id, bool completed);
  Q_INVOKABLE void seekSequenceMarker(const QString& marker_id);
  Q_INVOKABLE bool seekNextSequenceMarker();
  Q_INVOKABLE bool seekPreviousSequenceMarker();
  Q_INVOKABLE bool seekNextEditPoint();
  Q_INVOKABLE bool seekPreviousEditPoint();
  Q_INVOKABLE void removeSequenceMarker(const QString& marker_id);
  Q_INVOKABLE void playSequence();
  Q_INVOKABLE void pauseSequence();
  Q_INVOKABLE void seekSequenceSeconds(double seconds);
  Q_INVOKABLE void seekCurrentClipSeconds(double seconds);
  Q_INVOKABLE void stepSequenceFrames(int direction);
  Q_INVOKABLE void syncPlaybackPosition(double clip_seconds);
  Q_INVOKABLE bool advancePlaybackAfterClipEnd();

  Q_INVOKABLE void toggleClipSelection(int index);
  Q_INVOKABLE void selectClipRange(int from_index, int to_index);
  Q_INVOKABLE void selectAllClips();
  Q_INVOKABLE void deselectAllClips();
  Q_INVOKABLE void removeSelectedClips();

  Q_INVOKABLE void beginContinuousEdit();
  Q_INVOKABLE void endContinuousEdit();

  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();

signals:
  void timelineStateChanged();
  void clipsChanged();
  void sequenceChanged();
  void selectionChanged();
  void timelineNameChanged();
  void statusChanged();
  void dirtyChanged();
  void previewSourceChanged();
  void savedPathsChanged();
  void playbackStateChanged();
  void editorStateChanged();
  void sourceMonitorChanged();
  void markersChanged();
  void undoStateChanged();

 private:
  enum class TrackKind {
    Video,
    Audio,
  };

  struct Clip final {
    QString clip_id;
    QString asset_id;
    QString media_type = QStringLiteral("video");
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
    double opacity = 1.0;
    double audio_gain_db = 0.0;
    double fade_in_seconds = 0.0;
    double fade_out_seconds = 0.0;
    double playback_rate = 1.0;
    QStringList effect_stack;
    QString transition_in_name;
    QString transition_out_name;
    double transition_in_seconds = 0.0;
    double transition_out_seconds = 0.0;
  };

  struct Track final {
    QString track_id;
    QString display_label;
    TrackKind kind = TrackKind::Video;
    int lane_index = 0;
    bool locked = false;
    bool muted = false;
    bool solo = false;
    bool visible = true;
    bool targeted = false;
    QVector<Clip> clips;
  };

  struct Marker final {
    QString marker_id;
    QString label;
    QString note;
    QString status = QStringLiteral("todo");
    bool completed = false;
    QString color = QStringLiteral("#dfb8a8");
    double seconds = 0.0;
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
  [[nodiscard]] bool isAudioFile(const QString& file_path) const;
  [[nodiscard]] bool isImageFile(const QString& file_path) const;
  [[nodiscard]] QString mediaTypeForFile(const QString& file_path) const;
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
  [[nodiscard]] const Track* audioTrackForLane(int lane_index) const;
  [[nodiscard]] Track* audioTrackForLane(int lane_index);
  [[nodiscard]] const Track* videoTrackForRowIndex(int track_row_index) const;
  [[nodiscard]] Track* videoTrackForRowIndex(int track_row_index);
  [[nodiscard]] Track* defaultTargetVideoTrack();
  [[nodiscard]] Track* defaultTargetAudioTrack();
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
  [[nodiscard]] QVariantMap markerMap(const Marker& marker) const;
  [[nodiscard]] double probeVideoDurationSeconds(const QString& file_path) const;
  [[nodiscard]] double probeAudioDurationSeconds(const QString& file_path) const;
  [[nodiscard]] bool probeVideoHasAudio(const QString& file_path) const;
  [[nodiscard]] QString ensureVideoPoster(const QString& file_path, const QString& cache_key = QString()) const;
  [[nodiscard]] QString ensureAudioWaveform(const QString& file_path, const QString& cache_key = QString()) const;
  [[nodiscard]] QSize probeVideoSize(const QString& file_path) const;
  [[nodiscard]] bool renderTimelineVideo(const QString& output_path) const;
  [[nodiscard]] QJsonObject documentObject() const;
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
  void shiftAllTrackClipsFrom(double sequence_seconds, double delta_seconds, const QString& excluded_clip_id = QString());
  void shiftMarkersFrom(double sequence_seconds, double delta_seconds);
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
  void emitEditorState();
  void emitSourceMonitorState();
  void emitMarkersState();
  [[nodiscard]] bool isClipSelected(const QString& clip_id) const;
  void pushUndoSnapshot();

  struct Snapshot final {
    QVector<Track> tracks;
    QVector<Marker> markers;
    QString selected_clip_id;
    QStringList selected_clip_ids;
    double sequence_playhead_seconds = 0.0;
  };

  static constexpr int kMaxUndoDepth = 50;

  bool has_active_timeline_ = false;
  bool continuous_edit_active_ = false;
  QVector<Track> tracks_;
  QVector<Marker> markers_;
  QString timeline_id_;
  QString timeline_name_ = QStringLiteral("Untitled Timeline");
  QString status_ = QStringLiteral("Create a timeline to begin editing.");
  QString preview_source_;
  QString saved_document_path_;
  QString saved_render_path_;
  QString saved_poster_path_;
  QString created_at_;
  QString selected_clip_id_;
  QStringList selected_clip_ids_;
  QVector<Snapshot> undo_stack_;
  QVector<Snapshot> redo_stack_;
  QSize sequence_size_ = QSize(1080, 1350);
  double sequence_frame_rate_ = 30.0;
  int audio_sample_rate_ = 48000;
  QString project_binding_;
  QString color_space_ = QStringLiteral("Rec.709");
  QString data_levels_ = QStringLiteral("Legal");
  QString export_container_ = QStringLiteral("mp4");
  QString export_codec_ = QStringLiteral("h264");
  int current_index_ = -1;
  bool dirty_ = false;
  QTimer autosave_timer_;
  double sequence_playhead_seconds_ = 0.0;
  double current_clip_playhead_seconds_ = 0.0;
  bool sequence_playing_ = false;
  QString active_tool_ = QStringLiteral("select");
  QString edit_mode_ = QStringLiteral("insert");
  bool linked_selection_enabled_ = true;
  Clip source_clip_;
  bool has_source_clip_ = false;
  double source_in_seconds_ = 0.0;
  double source_out_seconds_ = 0.0;
};

}  // namespace photo_editor::desktop

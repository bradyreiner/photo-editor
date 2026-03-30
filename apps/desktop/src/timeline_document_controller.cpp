#include "timeline_document_controller.h"

#include "app_bootstrap.h"
#include "timeline_asset_store.h"

#include <algorithm>
#include <limits>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSize>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace photo_editor::desktop {

namespace {

constexpr double kDefaultFramesPerSecond = 30.0;
constexpr int kDefaultSequenceWidth = 1080;
constexpr int kDefaultSequenceHeight = 1350;
constexpr int kDefaultAudioSampleRate = 48000;
constexpr double kDefaultStillDurationSeconds = 4.0;
constexpr double kBoundaryEpsilon = 0.001;

const QStringList& video_extensions() {
  static const QStringList kExtensions = {
      QStringLiteral("m4v"),
      QStringLiteral("mov"),
      QStringLiteral("mp4"),
      QStringLiteral("mpeg"),
      QStringLiteral("mpg"),
      QStringLiteral("webm"),
  };
  return kExtensions;
}

const QStringList& audio_extensions() {
  static const QStringList kExtensions = {
      QStringLiteral("aac"),
      QStringLiteral("aif"),
      QStringLiteral("aiff"),
      QStringLiteral("flac"),
      QStringLiteral("m4a"),
      QStringLiteral("mp3"),
      QStringLiteral("ogg"),
      QStringLiteral("opus"),
      QStringLiteral("wav"),
  };
  return kExtensions;
}

const QStringList& image_extensions() {
  static const QStringList kExtensions = {
      QStringLiteral("avif"),
      QStringLiteral("bmp"),
      QStringLiteral("gif"),
      QStringLiteral("heic"),
      QStringLiteral("heif"),
      QStringLiteral("jpeg"),
      QStringLiteral("jpg"),
      QStringLiteral("png"),
      QStringLiteral("tif"),
      QStringLiteral("tiff"),
      QStringLiteral("webp"),
  };
  return kExtensions;
}

QString normalized_path(const QString& path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString poster_cache_root() {
  const auto cache_root = AppBootstrap::cacheLocation();
  const auto base = cache_root.isEmpty() ? QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-cache")) : cache_root;
  return QDir(base).filePath(QStringLiteral("timeline-posters"));
}

QString waveform_cache_root() {
  const auto cache_root = AppBootstrap::cacheLocation();
  const auto base = cache_root.isEmpty() ? QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-cache")) : cache_root;
  return QDir(base).filePath(QStringLiteral("timeline-waveforms"));
}

double clamp_non_negative(double value) {
  return qMax(0.0, value);
}

QString default_timeline_name(const QString& requested_name) {
  const auto trimmed = requested_name.simplified();
  return trimmed.isEmpty() ? QStringLiteral("Untitled Timeline") : trimmed;
}

QString normalized_color_space(const QString& color_space) {
  const auto simplified = color_space.simplified();
  return simplified.isEmpty() ? QStringLiteral("Rec.709") : simplified;
}

QString normalized_data_levels(const QString& data_levels) {
  const auto simplified = data_levels.simplified();
  if (simplified.compare(QStringLiteral("full"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Full");
  }
  return QStringLiteral("Legal");
}

QString normalized_export_container(const QString& export_container) {
  const auto normalized = export_container.trimmed().toLower();
  if (normalized == QStringLiteral("mov")) {
    return QStringLiteral("mov");
  }
  return QStringLiteral("mp4");
}

QString normalized_export_codec(const QString& export_codec) {
  const auto normalized = export_codec.trimmed().toLower();
  if (normalized == QStringLiteral("prores")) {
    return QStringLiteral("prores");
  }
  if (normalized == QStringLiteral("aac")) {
    return QStringLiteral("aac");
  }
  return QStringLiteral("h264");
}

QString normalized_tool_name(const QString& tool) {
  const auto normalized = tool.trimmed().toLower();
  static const QStringList kAllowed = {
      QStringLiteral("select"),
      QStringLiteral("razor"),
      QStringLiteral("ripple"),
      QStringLiteral("roll"),
      QStringLiteral("slip"),
      QStringLiteral("slide"),
      QStringLiteral("hand"),
      QStringLiteral("zoom"),
  };
  return kAllowed.contains(normalized) ? normalized : QStringLiteral("select");
}

QString normalized_edit_mode(const QString& mode) {
  const auto normalized = mode.trimmed().toLower();
  return normalized == QStringLiteral("overwrite") ? normalized : QStringLiteral("insert");
}

QString normalized_effect_name(const QString& effect_name) {
  return effect_name.simplified();
}

QString normalized_transition_edge(const QString& edge) {
  const auto normalized = edge.trimmed().toLower();
  return normalized == QStringLiteral("in") ? normalized : QStringLiteral("out");
}

QString normalized_marker_status(const QString& status) {
  const auto normalized = status.trimmed().toLower();
  if (normalized == QStringLiteral("review") ||
      normalized == QStringLiteral("approved") ||
      normalized == QStringLiteral("exported")) {
    return normalized;
  }
  return QStringLiteral("todo");
}

template <typename TrackContainer>
void sort_track_clips(TrackContainer* track) {
  if (track == nullptr) {
    return;
  }

  std::sort(
      track->clips.begin(),
      track->clips.end(),
      [](const auto& left, const auto& right) {
        if (!qFuzzyCompare(left.start_seconds + 1.0, right.start_seconds + 1.0)) {
          return left.start_seconds < right.start_seconds;
        }
        return left.display_label < right.display_label;
      });
}

}  // namespace

TimelineDocumentController::TimelineDocumentController(QObject* parent) : QObject(parent) {
  autosave_timer_.setInterval(30000);
  connect(&autosave_timer_, &QTimer::timeout, this, [this]() {
    if (!dirty_ || !has_active_timeline_ || saved_document_path_.isEmpty()) {
      return;
    }
    const auto autosave_path = saved_document_path_ + QStringLiteral(".autosave");
    (void)persistDocument(autosave_path);
  });
  connect(this, &TimelineDocumentController::dirtyChanged, this, [this]() {
    if (dirty_ && has_active_timeline_ && !saved_document_path_.isEmpty()) {
      autosave_timer_.start();
    } else {
      autosave_timer_.stop();
    }
  });
}

bool TimelineDocumentController::hasActiveTimeline() const {
  return has_active_timeline_;
}

QVariantList TimelineDocumentController::clips() const {
  QVariantList values;
  const auto ordered_refs = orderedVideoClipRefs();
  values.reserve(ordered_refs.size());
  for (int index = 0; index < ordered_refs.size(); ++index) {
    values.push_back(clipMap(index));
  }
  return values;
}

QVariantList TimelineDocumentController::sequenceTracks() const {
  QVariantList values;
  values.reserve(tracks_.size());
  for (int index = 0; index < tracks_.size(); ++index) {
    values.push_back(trackMap(tracks_.at(index), index));
  }
  return values;
}

QVariantMap TimelineDocumentController::currentClip() const {
  return clipMap(current_index_);
}

int TimelineDocumentController::clipCount() const {
  return static_cast<int>(orderedVideoClipRefs().size());
}

int TimelineDocumentController::currentIndex() const {
  return current_index_;
}

int TimelineDocumentController::trackCount() const {
  return static_cast<int>(tracks_.size());
}

int TimelineDocumentController::videoTrackCount() const {
  auto count = 0;
  for (const auto& track : tracks_) {
    if (track.kind == TrackKind::Video) {
      ++count;
    }
  }
  return count;
}

int TimelineDocumentController::audioTrackCount() const {
  auto count = 0;
  for (const auto& track : tracks_) {
    if (track.kind == TrackKind::Audio) {
      ++count;
    }
  }
  return count;
}

double TimelineDocumentController::framesPerSecond() const {
  return sequence_frame_rate_;
}

int TimelineDocumentController::sequenceWidth() const {
  return sequence_size_.width();
}

int TimelineDocumentController::sequenceHeight() const {
  return sequence_size_.height();
}

int TimelineDocumentController::audioSampleRate() const {
  return audio_sample_rate_;
}

QString TimelineDocumentController::projectBinding() const {
  return project_binding_;
}

QString TimelineDocumentController::colorSpace() const {
  return color_space_;
}

QString TimelineDocumentController::dataLevels() const {
  return data_levels_;
}

QString TimelineDocumentController::exportContainer() const {
  return export_container_;
}

QString TimelineDocumentController::exportCodec() const {
  return export_codec_;
}

QVariantMap TimelineDocumentController::sequenceSettings() const {
  QVariantMap map;
  map.insert(QStringLiteral("width"), sequence_size_.width());
  map.insert(QStringLiteral("height"), sequence_size_.height());
  map.insert(QStringLiteral("frameRate"), sequence_frame_rate_);
  map.insert(QStringLiteral("audioSampleRate"), audio_sample_rate_);
  map.insert(QStringLiteral("projectBinding"), project_binding_);
  map.insert(QStringLiteral("colorSpace"), color_space_);
  map.insert(QStringLiteral("dataLevels"), data_levels_);
  map.insert(QStringLiteral("exportContainer"), export_container_);
  map.insert(QStringLiteral("exportCodec"), export_codec_);
  return map;
}

QString TimelineDocumentController::sequenceFormatLabel() const {
  if (!has_active_timeline_) {
    return QStringLiteral("No active sequence");
  }
  return QStringLiteral("%1 x %2  •  %3 fps  •  %4 tracks")
      .arg(sequence_size_.width())
      .arg(sequence_size_.height())
      .arg(QString::number(framesPerSecond(), 'f', 0))
      .arg(trackCount());
}

QString TimelineDocumentController::timelineName() const {
  return timeline_name_;
}

QString TimelineDocumentController::status() const {
  return status_;
}

bool TimelineDocumentController::dirty() const {
  return dirty_;
}

QString TimelineDocumentController::totalDurationText() const {
  return formatDurationText(totalDurationSeconds());
}

double TimelineDocumentController::totalDurationSecondsValue() const {
  return totalDurationSeconds();
}

double TimelineDocumentController::sequencePlayheadSeconds() const {
  return sequence_playhead_seconds_;
}

double TimelineDocumentController::currentClipPlayheadSeconds() const {
  return current_clip_playhead_seconds_;
}

bool TimelineDocumentController::sequencePlaying() const {
  return sequence_playing_;
}

QString TimelineDocumentController::playbackSourceUrl() const {
  const auto* clip = clipAt(selectedClipRef());
  return clip == nullptr || clip->media_type != QStringLiteral("video")
             ? QString{}
             : QUrl::fromLocalFile(clip->file_path).toString();
}

QString TimelineDocumentController::previewSource() const {
  return preview_source_;
}

QString TimelineDocumentController::savedDocumentPath() const {
  return saved_document_path_;
}

QString TimelineDocumentController::savedRenderPath() const {
  return saved_render_path_;
}

QString TimelineDocumentController::timelineAssetId() const {
  return timeline_id_.isEmpty() ? QString{} : QStringLiteral("timeline:%1").arg(timeline_id_);
}

QString TimelineDocumentController::activeTool() const {
  return active_tool_;
}

QString TimelineDocumentController::editMode() const {
  return edit_mode_;
}

bool TimelineDocumentController::linkedSelectionEnabled() const {
  return linked_selection_enabled_;
}

QVariantList TimelineDocumentController::sequenceMarkers() const {
  QVariantList values;
  values.reserve(markers_.size());
  for (const auto& marker : markers_) {
    values.push_back(markerMap(marker));
  }
  return values;
}

QVariantMap TimelineDocumentController::sourceClip() const {
  if (!has_source_clip_) {
    QVariantMap map;
    map.insert(QStringLiteral("clipId"), QString{});
    map.insert(QStringLiteral("mediaType"), QStringLiteral("video"));
    map.insert(QStringLiteral("displayLabel"), QString{});
    map.insert(QStringLiteral("posterSource"), QString{});
    map.insert(QStringLiteral("waveformSource"), QString{});
    map.insert(QStringLiteral("sourceUrl"), QString{});
    map.insert(QStringLiteral("durationSeconds"), 0.0);
    map.insert(QStringLiteral("durationText"), QStringLiteral("0:00"));
    map.insert(QStringLiteral("trimStartSeconds"), 0.0);
    map.insert(QStringLiteral("trimEndSeconds"), 0.0);
    map.insert(QStringLiteral("trimmedDurationSeconds"), 0.0);
    map.insert(QStringLiteral("trimmedDurationText"), QStringLiteral("0:00"));
    return map;
  }

  QVariantMap map;
  map.insert(QStringLiteral("clipId"), source_clip_.clip_id);
  map.insert(QStringLiteral("assetId"), source_clip_.asset_id);
  map.insert(QStringLiteral("mediaType"), source_clip_.media_type);
  map.insert(QStringLiteral("filePath"), source_clip_.file_path);
  map.insert(QStringLiteral("displayLabel"), source_clip_.display_label);
  map.insert(QStringLiteral("posterSource"), source_clip_.poster_source);
  map.insert(QStringLiteral("waveformSource"), source_clip_.waveform_source);
  map.insert(
      QStringLiteral("sourceUrl"),
      source_clip_.media_type == QStringLiteral("photo") ? QString{} : QUrl::fromLocalFile(source_clip_.file_path).toString());
  map.insert(QStringLiteral("durationSeconds"), source_clip_.duration_seconds);
  map.insert(QStringLiteral("durationText"), formatDurationText(source_clip_.duration_seconds));
  map.insert(QStringLiteral("trimStartSeconds"), source_in_seconds_);
  map.insert(QStringLiteral("trimEndSeconds"), source_out_seconds_);
  map.insert(QStringLiteral("trimmedDurationSeconds"), qMax(0.0, source_out_seconds_ - source_in_seconds_));
  map.insert(QStringLiteral("trimmedDurationText"), formatDurationText(qMax(0.0, source_out_seconds_ - source_in_seconds_)));
  return map;
}

bool TimelineDocumentController::hasSourceClip() const {
  return has_source_clip_;
}

double TimelineDocumentController::sourceInSeconds() const {
  return source_in_seconds_;
}

double TimelineDocumentController::sourceOutSeconds() const {
  return source_out_seconds_;
}

void TimelineDocumentController::setTimelineName(const QString& timeline_name) {
  const auto normalized = default_timeline_name(timeline_name);
  if (timeline_name_ == normalized) {
    return;
  }

  timeline_name_ = normalized;
  emit timelineNameChanged();
  setDirty(true);
}

void TimelineDocumentController::setActiveTool(const QString& active_tool) {
  const auto normalized = normalized_tool_name(active_tool);
  if (active_tool_ == normalized) {
    return;
  }

  active_tool_ = normalized;
  emitEditorState();
  setDirty(true);
}

void TimelineDocumentController::setEditMode(const QString& edit_mode) {
  const auto normalized = normalized_edit_mode(edit_mode);
  if (edit_mode_ == normalized) {
    return;
  }

  edit_mode_ = normalized;
  emitEditorState();
  setDirty(true);
}

void TimelineDocumentController::setLinkedSelectionEnabled(bool enabled) {
  if (linked_selection_enabled_ == enabled) {
    return;
  }

  linked_selection_enabled_ = enabled;
  emitEditorState();
  setDirty(true);
}

void TimelineDocumentController::createTimeline(const QString& timeline_name) {
  QVariantMap settings;
  settings.insert(QStringLiteral("timelineName"), timeline_name);
  createTimelineWithSettings(settings);
}

void TimelineDocumentController::createTimelineWithSettings(const QVariantMap& settings) {
  initializeDefaultTracks();
  has_active_timeline_ = true;
  timeline_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  timeline_name_ = default_timeline_name(settings.value(QStringLiteral("timelineName")).toString());
  created_at_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  selected_clip_id_.clear();
  preview_source_.clear();
  saved_document_path_.clear();
  saved_render_path_.clear();
  saved_poster_path_.clear();
  sequence_size_ = QSize(
      qMax(16, settings.value(QStringLiteral("width"), kDefaultSequenceWidth).toInt()),
      qMax(16, settings.value(QStringLiteral("height"), kDefaultSequenceHeight).toInt()));
  sequence_frame_rate_ = qMax(1.0, settings.value(QStringLiteral("frameRate"), kDefaultFramesPerSecond).toDouble());
  audio_sample_rate_ = qMax(8000, settings.value(QStringLiteral("audioSampleRate"), kDefaultAudioSampleRate).toInt());
  project_binding_ = settings.value(QStringLiteral("projectBinding")).toString().simplified();
  color_space_ = normalized_color_space(settings.value(QStringLiteral("colorSpace")).toString());
  data_levels_ = normalized_data_levels(settings.value(QStringLiteral("dataLevels")).toString());
  export_container_ = normalized_export_container(settings.value(QStringLiteral("exportContainer")).toString());
  export_codec_ = normalized_export_codec(settings.value(QStringLiteral("exportCodec")).toString());
  if (export_codec_ == QStringLiteral("prores")) {
    export_container_ = QStringLiteral("mov");
  }
  current_index_ = -1;
  sequence_playhead_seconds_ = 0.0;
  current_clip_playhead_seconds_ = 0.0;
  sequence_playing_ = false;
  active_tool_ = QStringLiteral("select");
  edit_mode_ = QStringLiteral("insert");
  linked_selection_enabled_ = true;
  markers_.clear();
  has_source_clip_ = false;
  source_clip_ = Clip{};
  source_in_seconds_ = 0.0;
  source_out_seconds_ = 0.0;
  emitAllState();
  setDirty(false);
  setStatus(QStringLiteral("New timeline ready. Add media from the gallery."));
}

QVariantList TimelineDocumentController::recentTimelines() const {
  QVariantList result;
  const auto records = loadTimelineAssetRecords();
  for (const auto& record : records) {
    QVariantMap entry;
    entry[QStringLiteral("timelineId")] = record.timeline_id;
    entry[QStringLiteral("name")] = record.name;
    entry[QStringLiteral("documentPath")] = record.document_path;
    entry[QStringLiteral("createdAt")] = record.created_at;
    entry[QStringLiteral("updatedAt")] = record.updated_at;
    entry[QStringLiteral("durationSeconds")] = record.duration_seconds;
    result.append(entry);
  }
  return result;
}

void TimelineDocumentController::closeTimeline() {
  has_active_timeline_ = false;
  tracks_.clear();
  markers_.clear();
  timeline_id_.clear();
  timeline_name_ = QStringLiteral("Untitled Timeline");
  preview_source_.clear();
  saved_document_path_.clear();
  saved_render_path_.clear();
  saved_poster_path_.clear();
  created_at_.clear();
  selected_clip_id_.clear();
  project_binding_.clear();
  sequence_size_ = QSize(kDefaultSequenceWidth, kDefaultSequenceHeight);
  sequence_frame_rate_ = kDefaultFramesPerSecond;
  audio_sample_rate_ = kDefaultAudioSampleRate;
  color_space_ = QStringLiteral("Rec.709");
  data_levels_ = QStringLiteral("Legal");
  export_container_ = QStringLiteral("mp4");
  export_codec_ = QStringLiteral("h264");
  current_index_ = -1;
  sequence_playhead_seconds_ = 0.0;
  current_clip_playhead_seconds_ = 0.0;
  sequence_playing_ = false;
  active_tool_ = QStringLiteral("select");
  edit_mode_ = QStringLiteral("insert");
  linked_selection_enabled_ = true;
  has_source_clip_ = false;
  source_clip_ = Clip{};
  source_in_seconds_ = 0.0;
  source_out_seconds_ = 0.0;
  emitAllState();
  setDirty(false);
  setStatus(QStringLiteral("Create a timeline to begin editing."));
}

bool TimelineDocumentController::loadTimeline(const QString& document_path) {
  QFile file(document_path);
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    setStatus(QStringLiteral("The selected timeline document is unavailable."));
    return false;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    setStatus(QStringLiteral("The selected timeline document is invalid."));
    return false;
  }

  const auto object = document.object();
  const auto timeline_id = object.value(QStringLiteral("timelineId")).toString().trimmed();
  if (timeline_id.isEmpty()) {
    setStatus(QStringLiteral("The selected timeline document is missing its id."));
    return false;
  }

  QVector<Track> next_video_tracks;
  QHash<int, Track> next_audio_track_states;
  const auto track_values = object.value(QStringLiteral("tracks")).toArray();
  if (!track_values.isEmpty()) {
    auto lane_index = 0;
    for (const auto& track_value : track_values) {
      if (!track_value.isObject()) {
        continue;
      }

      const auto track_object = track_value.toObject();
      const auto kind = track_object.value(QStringLiteral("kind")).toString().trimmed().toLower();
      if (kind == QStringLiteral("audio")) {
        Track audio_track;
        audio_track.kind = TrackKind::Audio;
        audio_track.lane_index = track_object.value(QStringLiteral("laneIndex")).toInt(lane_index);
        audio_track.track_id = track_object.value(QStringLiteral("trackId")).toString().trimmed();
        audio_track.display_label = track_object.value(QStringLiteral("displayLabel")).toString().trimmed();
        audio_track.locked = track_object.value(QStringLiteral("locked")).toBool();
        audio_track.muted = track_object.value(QStringLiteral("muted")).toBool();
        audio_track.solo = track_object.value(QStringLiteral("solo")).toBool();
        audio_track.visible = track_object.value(QStringLiteral("visible")).toBool(true);
        audio_track.targeted = track_object.value(QStringLiteral("targeted")).toBool(audio_track.lane_index == 0);
        const auto clip_values = track_object.value(QStringLiteral("clips")).toArray();
        for (const auto& clip_value : clip_values) {
          if (!clip_value.isObject()) {
            continue;
          }
          const auto clip_object = clip_value.toObject();
          Clip clip;
          clip.clip_id = clip_object.value(QStringLiteral("clipId")).toString().trimmed();
          if (clip.clip_id.isEmpty()) {
            clip.clip_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
          }
          clip.asset_id = clip_object.value(QStringLiteral("assetId")).toString().trimmed();
          clip.media_type =
              clip_object.value(QStringLiteral("mediaType")).toString().trimmed().toLower();
          if (clip.media_type.isEmpty()) {
            clip.media_type = QStringLiteral("audio");
          }
          clip.file_path = clip_object.value(QStringLiteral("filePath")).toString().trimmed();
          clip.display_label = clip_object.value(QStringLiteral("displayLabel")).toString().trimmed();
          clip.poster_source = clip_object.value(QStringLiteral("posterSource")).toString().trimmed();
          clip.waveform_source = clip_object.value(QStringLiteral("waveformSource")).toString().trimmed();
          clip.linked_group_id = clip_object.value(QStringLiteral("linkedGroupId")).toString().trimmed();
          if (clip.linked_group_id.isEmpty()) {
            clip.linked_group_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
          }
          clip.duration_seconds = clip_object.value(QStringLiteral("durationSeconds")).toDouble();
          clip.trim_start_seconds = clamp_non_negative(clip_object.value(QStringLiteral("trimStartSeconds")).toDouble());
          clip.trim_end_seconds = clamp_non_negative(
              clip_object.value(QStringLiteral("trimEndSeconds")).toDouble(clip.duration_seconds));
          clip.start_seconds = clamp_non_negative(clip_object.value(QStringLiteral("startSeconds")).toDouble());
          clip.has_audio = clip_object.value(QStringLiteral("hasAudio")).toBool(clip.media_type == QStringLiteral("audio"));
          clip.frame_mode = QStringLiteral("fit");
          clip.frame_scale = 1.0;
          clip.opacity = 1.0;
          clip.playback_rate = 1.0;
          if (clip.duration_seconds <= 0.0 && !clip.file_path.isEmpty()) {
            clip.duration_seconds =
                clip.media_type == QStringLiteral("audio") ? probeAudioDurationSeconds(clip.file_path)
                                                           : probeVideoDurationSeconds(clip.file_path);
          }
          if (clip.trim_end_seconds <= 0.0) {
            clip.trim_end_seconds = clip.duration_seconds;
          }
          if (clip.waveform_source.isEmpty() && clip.has_audio && !clip.file_path.isEmpty()) {
            clip.waveform_source = ensureAudioWaveform(clip.file_path);
          }
          if (clip.poster_source.isEmpty() && clip.media_type == QStringLiteral("audio")) {
            clip.poster_source = clip.waveform_source;
          }
          if (!clip.file_path.isEmpty()) {
            audio_track.clips.push_back(clip);
          }
        }
        next_audio_track_states.insert(audio_track.lane_index, audio_track);
        continue;
      }

      Track track;
      track.kind = TrackKind::Video;
      track.lane_index = lane_index;
      track.track_id = track_object.value(QStringLiteral("trackId")).toString().trimmed();
      if (track.track_id.isEmpty()) {
        track.track_id = QStringLiteral("video-%1").arg(lane_index + 1);
      }
      track.display_label = track_object.value(QStringLiteral("displayLabel")).toString().trimmed();
      if (track.display_label.isEmpty()) {
        track.display_label = QStringLiteral("V%1").arg(lane_index + 1);
      }
      track.locked = track_object.value(QStringLiteral("locked")).toBool();
      track.muted = track_object.value(QStringLiteral("muted")).toBool();
      track.solo = track_object.value(QStringLiteral("solo")).toBool();
      track.visible = track_object.value(QStringLiteral("visible")).toBool(true);
      track.targeted = track_object.value(QStringLiteral("targeted")).toBool(lane_index == 0);

      const auto clip_values = track_object.value(QStringLiteral("clips")).toArray();
      auto sequential_start = 0.0;
      track.clips.reserve(clip_values.size());
      for (const auto& clip_value : clip_values) {
        if (!clip_value.isObject()) {
          continue;
        }

        const auto clip_object = clip_value.toObject();
        Clip clip;
        clip.clip_id = clip_object.value(QStringLiteral("clipId")).toString().trimmed();
        if (clip.clip_id.isEmpty()) {
          clip.clip_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        clip.asset_id = clip_object.value(QStringLiteral("assetId")).toString().trimmed();
        clip.media_type =
            clip_object.value(QStringLiteral("mediaType")).toString().trimmed().toLower();
        if (clip.media_type.isEmpty()) {
          clip.media_type = mediaTypeForFile(clip_object.value(QStringLiteral("filePath")).toString().trimmed());
          if (clip.media_type.isEmpty()) {
            clip.media_type = QStringLiteral("video");
          }
        }
        clip.file_path = clip_object.value(QStringLiteral("filePath")).toString().trimmed();
        clip.display_label = clip_object.value(QStringLiteral("displayLabel")).toString().trimmed();
        clip.poster_source = clip_object.value(QStringLiteral("posterSource")).toString().trimmed();
        clip.linked_group_id = clip_object.value(QStringLiteral("linkedGroupId")).toString().trimmed();
        if (clip.linked_group_id.isEmpty()) {
          clip.linked_group_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        clip.duration_seconds = clip_object.value(QStringLiteral("durationSeconds")).toDouble();
        clip.trim_start_seconds = clamp_non_negative(clip_object.value(QStringLiteral("trimStartSeconds")).toDouble());
        clip.trim_end_seconds = clamp_non_negative(
            clip_object.value(QStringLiteral("trimEndSeconds")).toDouble(clip.duration_seconds));
        clip.start_seconds = clamp_non_negative(
            clip_object.value(QStringLiteral("startSeconds")).toDouble(sequential_start));
        clip.has_audio = clip_object.value(QStringLiteral("hasAudio")).toBool();
        clip.waveform_source = clip_object.value(QStringLiteral("waveformSource")).toString().trimmed();
        clip.frame_mode = clip_object.value(QStringLiteral("frameMode")).toString().trimmed().toLower();
        if (clip.frame_mode.isEmpty()) {
          clip.frame_mode = QStringLiteral("fit");
        }
        clip.frame_scale = qMax(1.0, clip_object.value(QStringLiteral("frameScale")).toDouble(1.0));
        clip.frame_offset_x = qBound(-1.0, clip_object.value(QStringLiteral("frameOffsetX")).toDouble(), 1.0);
        clip.frame_offset_y = qBound(-1.0, clip_object.value(QStringLiteral("frameOffsetY")).toDouble(), 1.0);
        clip.opacity = qBound(0.0, clip_object.value(QStringLiteral("opacity")).toDouble(1.0), 1.0);
        clip.audio_gain_db = qBound(-36.0, clip_object.value(QStringLiteral("audioGainDb")).toDouble(), 12.0);
        clip.fade_in_seconds = clamp_non_negative(clip_object.value(QStringLiteral("fadeInSeconds")).toDouble());
        clip.fade_out_seconds = clamp_non_negative(clip_object.value(QStringLiteral("fadeOutSeconds")).toDouble());
        clip.playback_rate = qBound(0.25, clip_object.value(QStringLiteral("playbackRate")).toDouble(1.0), 4.0);
        const auto effect_values = clip_object.value(QStringLiteral("effectStack")).toArray();
        for (const auto& effect_value : effect_values) {
          const auto effect_name = normalized_effect_name(effect_value.toString());
          if (!effect_name.isEmpty() && !clip.effect_stack.contains(effect_name)) {
            clip.effect_stack.push_back(effect_name);
          }
        }
        clip.transition_in_name =
            normalized_effect_name(clip_object.value(QStringLiteral("transitionInName")).toString());
        clip.transition_out_name =
            normalized_effect_name(clip_object.value(QStringLiteral("transitionOutName")).toString());
        clip.transition_in_seconds =
            clamp_non_negative(clip_object.value(QStringLiteral("transitionInSeconds")).toDouble());
        clip.transition_out_seconds =
            clamp_non_negative(clip_object.value(QStringLiteral("transitionOutSeconds")).toDouble());

        if (clip.duration_seconds <= 0.0 && !clip.file_path.isEmpty()) {
          if (clip.media_type == QStringLiteral("photo")) {
            clip.duration_seconds = kDefaultStillDurationSeconds;
          } else if (clip.media_type == QStringLiteral("audio")) {
            clip.duration_seconds = probeAudioDurationSeconds(clip.file_path);
          } else {
            clip.duration_seconds = probeVideoDurationSeconds(clip.file_path);
          }
        }
        if (clip.trim_end_seconds <= 0.0) {
          clip.trim_end_seconds = clip.duration_seconds;
        }
        if (clip.poster_source.isEmpty() && !clip.file_path.isEmpty()) {
          if (clip.media_type == QStringLiteral("photo")) {
            clip.poster_source = QUrl::fromLocalFile(clip.file_path).toString();
          } else if (clip.media_type == QStringLiteral("video")) {
            clip.poster_source = ensureVideoPoster(clip.file_path);
          }
        }
        if (clip.waveform_source.isEmpty() && clip.has_audio && !clip.file_path.isEmpty()) {
          clip.waveform_source = ensureAudioWaveform(clip.file_path);
        }
        if (!clip.file_path.isEmpty()) {
          track.clips.push_back(clip);
          sequential_start = clipEndSeconds(clip);
        }
      }

      sort_track_clips(&track);
      next_video_tracks.push_back(track);
      ++lane_index;
    }
  }

  if (next_video_tracks.isEmpty()) {
    Track track;
    track.kind = TrackKind::Video;
    track.lane_index = 0;
    track.track_id = QStringLiteral("video-1");
    track.display_label = QStringLiteral("V1");
    track.visible = true;
    track.targeted = true;

    const auto clip_values = object.value(QStringLiteral("clips")).toArray();
    auto sequential_start = 0.0;
    track.clips.reserve(clip_values.size());
    for (const auto& value : clip_values) {
      if (!value.isObject()) {
        continue;
      }

      const auto clip_object = value.toObject();
      Clip clip;
      clip.clip_id = clip_object.value(QStringLiteral("clipId")).toString().trimmed();
      if (clip.clip_id.isEmpty()) {
        clip.clip_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      }
      clip.asset_id = clip_object.value(QStringLiteral("assetId")).toString().trimmed();
      clip.media_type =
          clip_object.value(QStringLiteral("mediaType")).toString().trimmed().toLower();
      clip.file_path = clip_object.value(QStringLiteral("filePath")).toString().trimmed();
      if (clip.media_type.isEmpty()) {
        clip.media_type = mediaTypeForFile(clip.file_path);
        if (clip.media_type.isEmpty()) {
          clip.media_type = QStringLiteral("video");
        }
      }
      clip.display_label = clip_object.value(QStringLiteral("displayLabel")).toString().trimmed();
      clip.poster_source = clip_object.value(QStringLiteral("posterSource")).toString().trimmed();
      clip.linked_group_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      clip.duration_seconds = clip_object.value(QStringLiteral("durationSeconds")).toDouble();
      clip.trim_start_seconds = clamp_non_negative(clip_object.value(QStringLiteral("trimStartSeconds")).toDouble());
      clip.trim_end_seconds = clamp_non_negative(
          clip_object.value(QStringLiteral("trimEndSeconds")).toDouble(clip.duration_seconds));
      clip.start_seconds = sequential_start;
      clip.has_audio =
          clip.media_type == QStringLiteral("video") && !clip.file_path.isEmpty() && probeVideoHasAudio(clip.file_path);
      clip.frame_mode = QStringLiteral("fit");
      clip.frame_scale = 1.0;
      clip.frame_offset_x = 0.0;
      clip.frame_offset_y = 0.0;
      clip.opacity = 1.0;
      clip.audio_gain_db = 0.0;
      clip.fade_in_seconds = 0.0;
      clip.fade_out_seconds = 0.0;
      clip.playback_rate = 1.0;
      const auto effect_values = clip_object.value(QStringLiteral("effectStack")).toArray();
      for (const auto& effect_value : effect_values) {
        const auto effect_name = normalized_effect_name(effect_value.toString());
        if (!effect_name.isEmpty() && !clip.effect_stack.contains(effect_name)) {
          clip.effect_stack.push_back(effect_name);
        }
      }
      clip.transition_in_name =
          normalized_effect_name(clip_object.value(QStringLiteral("transitionInName")).toString());
      clip.transition_out_name =
          normalized_effect_name(clip_object.value(QStringLiteral("transitionOutName")).toString());
      clip.transition_in_seconds =
          clamp_non_negative(clip_object.value(QStringLiteral("transitionInSeconds")).toDouble());
      clip.transition_out_seconds =
          clamp_non_negative(clip_object.value(QStringLiteral("transitionOutSeconds")).toDouble());

      if (clip.duration_seconds <= 0.0 && !clip.file_path.isEmpty()) {
        if (clip.media_type == QStringLiteral("photo")) {
          clip.duration_seconds = kDefaultStillDurationSeconds;
        } else {
          clip.duration_seconds = probeVideoDurationSeconds(clip.file_path);
        }
      }
      if (clip.poster_source.isEmpty() && !clip.file_path.isEmpty()) {
        if (clip.media_type == QStringLiteral("photo")) {
          clip.poster_source = QUrl::fromLocalFile(clip.file_path).toString();
        } else {
          clip.poster_source = ensureVideoPoster(clip.file_path);
        }
      }
      if (clip.waveform_source.isEmpty() && clip.has_audio && !clip.file_path.isEmpty()) {
        clip.waveform_source = ensureAudioWaveform(clip.file_path);
      }
      if (!clip.file_path.isEmpty()) {
        track.clips.push_back(clip);
        sequential_start = clipEndSeconds(clip);
      }
    }

    next_video_tracks.push_back(track);
  }

  tracks_.clear();
  for (auto& video_track : next_video_tracks) {
    tracks_.push_back(video_track);
    Track audio_track;
    audio_track.kind = TrackKind::Audio;
    audio_track.lane_index = video_track.lane_index;
    audio_track.track_id = QStringLiteral("audio-%1").arg(video_track.lane_index + 1);
    audio_track.display_label = QStringLiteral("A%1").arg(video_track.lane_index + 1);
    if (next_audio_track_states.contains(video_track.lane_index)) {
      const auto saved_audio_track = next_audio_track_states.value(video_track.lane_index);
      audio_track.locked = saved_audio_track.locked;
      audio_track.muted = saved_audio_track.muted;
      audio_track.solo = saved_audio_track.solo;
      audio_track.visible = saved_audio_track.visible;
      audio_track.targeted = saved_audio_track.targeted;
    } else {
      audio_track.visible = video_track.visible;
      audio_track.targeted = video_track.targeted;
    }
    tracks_.push_back(audio_track);
  }
  syncAudioTracks();

  const auto format_object = object.value(QStringLiteral("sequenceFormat")).toObject();
  const auto loaded_width = format_object.value(QStringLiteral("width")).toInt();
  const auto loaded_height = format_object.value(QStringLiteral("height")).toInt();
  if (loaded_width > 0 && loaded_height > 0) {
    sequence_size_ = QSize(loaded_width, loaded_height);
  } else {
    refreshSequenceSizeFromContent();
  }
  sequence_frame_rate_ = qMax(
      1.0,
      format_object.value(QStringLiteral("frameRate")).toDouble(kDefaultFramesPerSecond));
  audio_sample_rate_ = qMax(
      8000,
      format_object.value(QStringLiteral("audioSampleRate")).toInt(kDefaultAudioSampleRate));
  project_binding_ = object.value(QStringLiteral("projectBinding")).toString().simplified();
  color_space_ = normalized_color_space(object.value(QStringLiteral("colorSpace")).toString());
  data_levels_ = normalized_data_levels(object.value(QStringLiteral("dataLevels")).toString());
  export_container_ = normalized_export_container(object.value(QStringLiteral("exportContainer")).toString());
  export_codec_ = normalized_export_codec(object.value(QStringLiteral("exportCodec")).toString());
  if (export_codec_ == QStringLiteral("prores")) {
    export_container_ = QStringLiteral("mov");
  }

  has_active_timeline_ = true;
  timeline_id_ = timeline_id;
  timeline_name_ = default_timeline_name(object.value(QStringLiteral("name")).toString());
  created_at_ = object.value(QStringLiteral("createdAt")).toString().trimmed();
  active_tool_ = normalized_tool_name(object.value(QStringLiteral("activeTool")).toString());
  edit_mode_ = normalized_edit_mode(object.value(QStringLiteral("editMode")).toString());
  linked_selection_enabled_ = object.value(QStringLiteral("linkedSelectionEnabled")).toBool(true);
  markers_.clear();
  const auto marker_values = object.value(QStringLiteral("markers")).toArray();
  markers_.reserve(marker_values.size());
  for (const auto& marker_value : marker_values) {
    if (!marker_value.isObject()) {
      continue;
    }
    const auto marker_object = marker_value.toObject();
    Marker marker;
    marker.marker_id = marker_object.value(QStringLiteral("markerId")).toString().trimmed();
    if (marker.marker_id.isEmpty()) {
      marker.marker_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    marker.label = marker_object.value(QStringLiteral("label")).toString().trimmed();
    marker.note = marker_object.value(QStringLiteral("note")).toString().trimmed();
    marker.status = normalized_marker_status(marker_object.value(QStringLiteral("status")).toString());
    marker.completed = marker_object.value(QStringLiteral("completed")).toBool(false);
    marker.color = marker_object.value(QStringLiteral("color")).toString().trimmed();
    if (marker.color.isEmpty()) {
      marker.color = QStringLiteral("#dfb8a8");
    }
    marker.seconds = qBound(0.0, marker_object.value(QStringLiteral("seconds")).toDouble(), totalDurationSeconds());
    markers_.push_back(marker);
  }
  has_source_clip_ = false;
  source_clip_ = Clip{};
  source_in_seconds_ = 0.0;
  source_out_seconds_ = 0.0;
  selected_clip_id_.clear();
  saved_document_path_ = normalized_path(document_path);
  saved_render_path_ = object.value(QStringLiteral("renderPath")).toString().trimmed();
  saved_poster_path_ = object.value(QStringLiteral("posterPath")).toString().trimmed();
  preview_source_ = !saved_poster_path_.isEmpty() ? QUrl::fromLocalFile(saved_poster_path_).toString() : QString{};
  current_index_ = -1;
  sequence_playhead_seconds_ = 0.0;
  current_clip_playhead_seconds_ = 0.0;
  sequence_playing_ = false;
  ensureCurrentIndexValid();
  if (const auto* clip = clipAt(current_index_); clip != nullptr) {
    current_clip_playhead_seconds_ = clip->trim_start_seconds;
    preview_source_ = clip->poster_source;
  }
  emitAllState();
  setDirty(false);
  setStatus(QStringLiteral("Loaded timeline '%1'.").arg(timeline_name_));
  return true;
}

bool TimelineDocumentController::saveTimeline() {
  if (!has_active_timeline_) {
    setStatus(QStringLiteral("Create a timeline before saving."));
    return false;
  }
  if (clipCount() == 0) {
    setStatus(QStringLiteral("Add at least one clip before saving a timeline."));
    return false;
  }

  QDir().mkpath(timelineDocumentsRootPath());
  QDir().mkpath(timelineRendersRootPath());
  QDir().mkpath(timelinePostersRootPath());

  if (timeline_id_.isEmpty()) {
    timeline_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  if (created_at_.isEmpty()) {
    created_at_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  }

  saved_document_path_ = QDir(timelineDocumentsRootPath()).filePath(QStringLiteral("%1.json").arg(timeline_id_));
  saved_render_path_ = QDir(timelineRendersRootPath()).filePath(
      QStringLiteral("%1.%2").arg(timeline_id_, export_container_));
  saved_poster_path_ = QDir(timelinePostersRootPath()).filePath(QStringLiteral("%1.jpg").arg(timeline_id_));

  if (!renderTimelineVideo(saved_render_path_)) {
    return false;
  }

  const auto poster_source = ensureVideoPoster(saved_render_path_, timeline_id_);
  if (!poster_source.isEmpty()) {
    saved_poster_path_ = QUrl(poster_source).toLocalFile();
    preview_source_ = poster_source;
  }

  if (!persistDocument(saved_document_path_)) {
    setStatus(QStringLiteral("Unable to save the timeline document."));
    return false;
  }

  auto records = loadTimelineAssetRecords();
  auto updated_existing = false;
  const auto updated_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  for (auto& record : records) {
    if (record.timeline_id != timeline_id_) {
      continue;
    }

    record.asset_id = QStringLiteral("timeline:%1").arg(timeline_id_);
    record.name = timeline_name_;
    record.document_path = saved_document_path_;
    record.render_path = saved_render_path_;
    record.poster_path = saved_poster_path_;
    record.duration_seconds = totalDurationSeconds();
    if (record.created_at.isEmpty()) {
      record.created_at = created_at_;
    }
    record.updated_at = updated_at;
    updated_existing = true;
    break;
  }

  if (!updated_existing) {
    TimelineAssetRecord record;
    record.asset_id = QStringLiteral("timeline:%1").arg(timeline_id_);
    record.timeline_id = timeline_id_;
    record.name = timeline_name_;
    record.document_path = saved_document_path_;
    record.render_path = saved_render_path_;
    record.poster_path = saved_poster_path_;
    record.duration_seconds = totalDurationSeconds();
    record.created_at = created_at_;
    record.updated_at = updated_at;
    records.push_back(record);
  }

  if (!saveTimelineAssetRecords(records)) {
    setStatus(QStringLiteral("Timeline rendered, but the library index could not be updated."));
    return false;
  }

  emit previewSourceChanged();
  emit savedPathsChanged();
  setDirty(false);
  setStatus(QStringLiteral("Saved timeline '%1' to the gallery.").arg(timeline_name_));
  return true;
}

void TimelineDocumentController::selectClip(int index) {
  if (index < 0) {
    selected_clip_id_.clear();
    selected_clip_ids_.clear();
    current_index_ = -1;
    emit selectionChanged();
    emit clipsChanged();
    emit sequenceChanged();
    return;
  }

  const auto ref = clipRefAt(index);
  const auto* clip = clipAt(ref);
  if (clip == nullptr) {
    return;
  }

  selected_clip_id_ = clip->clip_id;
  selected_clip_ids_.clear();
  selected_clip_ids_.append(clip->clip_id);
  current_index_ = index;
  preview_source_ = clip->poster_source;
  current_clip_playhead_seconds_ = clip->trim_start_seconds;
  sequence_playhead_seconds_ = clip->start_seconds;
  sequence_playing_ = false;
  emit selectionChanged();
  emit previewSourceChanged();
  emit playbackStateChanged();
}

void TimelineDocumentController::addClipFromGallery(
    const QString& asset_id,
    const QString& file_path,
    const QString& display_label,
    double duration_seconds,
    const QString& poster_source) {
  if (!has_active_timeline_) {
    setStatus(QStringLiteral("Create a timeline before adding media."));
    return;
  }
  pushUndoSnapshot();

  auto clip = buildGalleryClip(asset_id, file_path, display_label, duration_seconds, 0.0, 0.0, poster_source);
  if (clip.duration_seconds <= 0.0) {
    setStatus(QStringLiteral("Unable to read the selected media."));
    return;
  }

  auto* target_track = clip.media_type == QStringLiteral("audio") ? defaultTargetAudioTrack() : defaultTargetVideoTrack();
  if (target_track == nullptr) {
    setStatus(QStringLiteral("The sequence tracks are unavailable."));
    return;
  }

  clip.start_seconds = qMax(sequence_playhead_seconds_, trackEndSeconds(*target_track));
  target_track->clips.push_back(clip);
  sort_track_clips(target_track);
  syncAudioTracks();
  refreshSequenceSizeFromContent();
  if (target_track->kind == TrackKind::Video) {
    selected_clip_id_ = clip.clip_id;
    updateCurrentIndexFromSelection();
    preview_source_ = clip.poster_source;
    current_clip_playhead_seconds_ = clip.trim_start_seconds;
    sequence_playhead_seconds_ = clip.start_seconds;
  }
  sequence_playing_ = false;
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Added '%1' to %2.").arg(clip.display_label, target_track->display_label));
}

void TimelineDocumentController::insertClipFromGallery(
    const QString& asset_id,
    const QString& file_path,
    const QString& display_label,
    double duration_seconds,
    double trim_start_seconds,
    double trim_end_seconds,
    const QString& poster_source) {
  pushUndoSnapshot();
  auto clip = buildGalleryClip(
      asset_id,
      file_path,
      display_label,
      duration_seconds,
      trim_start_seconds,
      trim_end_seconds,
      poster_source);
  const auto insert_duration = clipTrimmedDuration(clip);
  if (insert_duration <= 0.0) {
    setStatus(QStringLiteral("Set a valid source in and out before inserting."));
    return;
  }

  auto* target_track = clip.media_type == QStringLiteral("audio") ? defaultTargetAudioTrack() : defaultTargetVideoTrack();
  if (target_track == nullptr) {
    setStatus(QStringLiteral("The sequence tracks are unavailable."));
    return;
  }

  const auto insert_start = qMax(0.0, sequence_playhead_seconds_);
  for (int index = 0; index < target_track->clips.size(); ++index) {
    const auto existing_end = clipEndSeconds(target_track->clips.at(index));
    if (target_track->clips.at(index).start_seconds < insert_start && existing_end > insert_start + kBoundaryEpsilon) {
      splitTrackClipAtSequenceSeconds(target_track, index, insert_start);
      break;
    }
  }
  shiftTrackClipsFrom(target_track, insert_start, insert_duration);
  shiftMarkersFrom(insert_start, insert_duration);
  clip.start_seconds = insert_start;
  target_track->clips.push_back(clip);
  sort_track_clips(target_track);
  syncAudioTracks();
  refreshSequenceSizeFromContent();
  if (target_track->kind == TrackKind::Video) {
    selected_clip_id_ = clip.clip_id;
    updateCurrentIndexFromSelection();
    preview_source_ = clip.poster_source;
    current_clip_playhead_seconds_ = clip.trim_start_seconds;
    sequence_playhead_seconds_ = clip.start_seconds;
  }
  sequence_playing_ = false;
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Inserted '%1' at %2.").arg(clip.display_label, formatDurationText(insert_start)));
}

void TimelineDocumentController::overwriteClipFromGallery(
    const QString& asset_id,
    const QString& file_path,
    const QString& display_label,
    double duration_seconds,
    double trim_start_seconds,
    double trim_end_seconds,
    const QString& poster_source) {
  pushUndoSnapshot();
  auto clip = buildGalleryClip(
      asset_id,
      file_path,
      display_label,
      duration_seconds,
      trim_start_seconds,
      trim_end_seconds,
      poster_source);
  const auto overwrite_duration = clipTrimmedDuration(clip);
  if (overwrite_duration <= 0.0) {
    setStatus(QStringLiteral("Set a valid source in and out before overwriting."));
    return;
  }

  auto* target_track = clip.media_type == QStringLiteral("audio") ? defaultTargetAudioTrack() : defaultTargetVideoTrack();
  if (target_track == nullptr) {
    setStatus(QStringLiteral("The sequence tracks are unavailable."));
    return;
  }

  const auto overwrite_start = qMax(0.0, sequence_playhead_seconds_);
  const auto overwrite_end = overwrite_start + overwrite_duration;
  overwriteTrackRange(target_track, overwrite_start, overwrite_end);
  clip.start_seconds = overwrite_start;
  target_track->clips.push_back(clip);
  sort_track_clips(target_track);
  syncAudioTracks();
  refreshSequenceSizeFromContent();
  if (target_track->kind == TrackKind::Video) {
    selected_clip_id_ = clip.clip_id;
    updateCurrentIndexFromSelection();
    preview_source_ = clip.poster_source;
    current_clip_playhead_seconds_ = clip.trim_start_seconds;
    sequence_playhead_seconds_ = clip.start_seconds;
  }
  sequence_playing_ = false;
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Overwrote %1 at %2.").arg(target_track->display_label, formatDurationText(overwrite_start)));
}

void TimelineDocumentController::addVideoTrack() {
  pushUndoSnapshot();
  appendTrackPair();
  syncAudioTracks();
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Added V%1 / A%1 to the sequence.").arg(videoTrackCount()));
}

void TimelineDocumentController::moveSelectedClipToTrackRow(int track_row_index, double start_seconds) {
  pushUndoSnapshot();
  const auto selected_ref = selectedClipRef();
  auto* source_track = trackAt(selected_ref.track_index);
  auto* selected_clip = clipAt(selected_ref);
  auto* target_track = videoTrackForRowIndex(track_row_index);
  if (source_track == nullptr || selected_clip == nullptr || target_track == nullptr || source_track->kind != TrackKind::Video) {
    return;
  }

  const auto selected_clip_id = selected_clip->clip_id;
  Clip clip_copy = *selected_clip;
  clip_copy.start_seconds = qMax(0.0, start_seconds);

  if (source_track->track_id == target_track->track_id) {
    selected_clip->start_seconds = clip_copy.start_seconds;
    sort_track_clips(source_track);
  } else {
    source_track->clips.removeAt(selected_ref.clip_index);
    target_track->clips.push_back(clip_copy);
    sort_track_clips(source_track);
    sort_track_clips(target_track);
  }

  selected_clip_id_ = selected_clip_id;
  syncAudioTracks();
  updateCurrentIndexFromSelection();
  if (const auto* moved_clip = clipAt(selectedClipRef()); moved_clip != nullptr) {
    preview_source_ = moved_clip->poster_source;
    current_clip_playhead_seconds_ = qBound(
        moved_clip->trim_start_seconds,
        current_clip_playhead_seconds_,
        moved_clip->trim_end_seconds);
    sequence_playhead_seconds_ =
        moved_clip->start_seconds + qMax(0.0, current_clip_playhead_seconds_ - moved_clip->trim_start_seconds);
  }
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Moved the selected clip to %1 at %2.")
                .arg(target_track->display_label, formatDurationText(qMax(0.0, start_seconds))));
}

void TimelineDocumentController::liftSelectedClip() {
  pushUndoSnapshot();
  const auto ref = selectedClipRef();
  auto* track = trackAt(ref.track_index);
  if (track == nullptr || ref.clip_index < 0 || ref.clip_index >= track->clips.size()) {
    return;
  }

  track->clips.removeAt(ref.clip_index);
  syncAudioTracks();
  if (ref.clip_index < track->clips.size()) {
    selected_clip_id_ = track->clips.at(ref.clip_index).clip_id;
  } else if (!track->clips.isEmpty()) {
    selected_clip_id_ = track->clips.last().clip_id;
  } else {
    selected_clip_id_.clear();
  }
  ensureCurrentIndexValid();
  preview_source_ = current_index_ >= 0 && clipAt(current_index_) != nullptr ? clipAt(current_index_)->poster_source : QString{};
  clampPlaybackState();
  sequence_playing_ = false;
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Lifted the selected clip from the sequence."));
}

void TimelineDocumentController::extractSelectedClip() {
  pushUndoSnapshot();
  const auto ref = selectedClipRef();
  auto* track = trackAt(ref.track_index);
  auto* clip = clipAt(ref);
  if (track == nullptr || clip == nullptr || ref.clip_index < 0 || ref.clip_index >= track->clips.size()) {
    return;
  }

  const auto removal_start = clip->start_seconds;
  const auto removal_end = clipEndSeconds(*clip);
  const auto removal_duration = qMax(0.0, removal_end - removal_start);
  const auto removed_label = clip->display_label;

  track->clips.removeAt(ref.clip_index);
  if (removal_duration > kBoundaryEpsilon) {
    shiftAllTrackClipsFrom(removal_end, -removal_duration);
    shiftMarkersFrom(removal_end, -removal_duration);
  }

  const auto refs = orderedVideoClipRefs();
  const auto desired_selection_time = qMax(0.0, removal_start);
  selected_clip_id_.clear();
  for (const auto& candidate_ref : refs) {
    const auto* candidate = clipAt(candidate_ref);
    if (candidate == nullptr) {
      continue;
    }
    if (candidate->start_seconds >= desired_selection_time - kBoundaryEpsilon) {
      selected_clip_id_ = candidate->clip_id;
      break;
    }
  }
  if (selected_clip_id_.isEmpty() && !refs.isEmpty()) {
    if (const auto* candidate = clipAt(refs.last()); candidate != nullptr) {
      selected_clip_id_ = candidate->clip_id;
    }
  }

  updateCurrentIndexFromSelection();
  sequence_playhead_seconds_ = qMin(desired_selection_time, totalDurationSeconds());
  if (const auto* current_clip = clipAt(selectedClipRef()); current_clip != nullptr) {
    preview_source_ = current_clip->poster_source;
    current_clip_playhead_seconds_ = qBound(
        current_clip->trim_start_seconds,
        current_clip->trim_start_seconds + qMax(0.0, sequence_playhead_seconds_ - current_clip->start_seconds),
        current_clip->trim_end_seconds);
  } else {
    preview_source_.clear();
    current_clip_playhead_seconds_ = 0.0;
  }
  sequence_playing_ = false;
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Extracted '%1' and closed the gap.").arg(removed_label));
}

void TimelineDocumentController::removeSelectedClip() {
  liftSelectedClip();
}

void TimelineDocumentController::moveSelectedBackward() {
  pushUndoSnapshot();
  const auto ref = selectedClipRef();
  auto* track = trackAt(ref.track_index);
  auto* clip = clipAt(ref);
  if (track == nullptr || clip == nullptr || track->kind != TrackKind::Video) {
    return;
  }

  sort_track_clips(track);
  auto selected_index = -1;
  for (int index = 0; index < track->clips.size(); ++index) {
    if (track->clips.at(index).clip_id == clip->clip_id) {
      selected_index = index;
      break;
    }
  }
  if (selected_index <= 0) {
    return;
  }

  const auto previous_start = track->clips.at(selected_index - 1).start_seconds;
  track->clips[selected_index - 1].start_seconds = track->clips.at(selected_index).start_seconds;
  track->clips[selected_index].start_seconds = previous_start;
  sort_track_clips(track);
  syncAudioTracks();
  updateCurrentIndexFromSelection();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Moved the clip earlier on %1.").arg(track->display_label));
}

void TimelineDocumentController::moveSelectedForward() {
  pushUndoSnapshot();
  const auto ref = selectedClipRef();
  auto* track = trackAt(ref.track_index);
  auto* clip = clipAt(ref);
  if (track == nullptr || clip == nullptr || track->kind != TrackKind::Video) {
    return;
  }

  sort_track_clips(track);
  auto selected_index = -1;
  for (int index = 0; index < track->clips.size(); ++index) {
    if (track->clips.at(index).clip_id == clip->clip_id) {
      selected_index = index;
      break;
    }
  }
  if (selected_index < 0 || selected_index >= track->clips.size() - 1) {
    return;
  }

  const auto next_start = track->clips.at(selected_index + 1).start_seconds;
  track->clips[selected_index + 1].start_seconds = track->clips.at(selected_index).start_seconds;
  track->clips[selected_index].start_seconds = next_start;
  sort_track_clips(track);
  syncAudioTracks();
  updateCurrentIndexFromSelection();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Moved the clip later on %1.").arg(track->display_label));
}

void TimelineDocumentController::toggleTrackLocked(int track_row_index) {
  auto* track = trackAt(track_row_index);
  if (track == nullptr) {
    return;
  }

  track->locked = !track->locked;
  if (track->kind == TrackKind::Video) {
    if (auto* audio_track = audioTrackForLane(track->lane_index); audio_track != nullptr) {
      audio_track->locked = track->locked;
    }
  } else if (auto* video_track = videoTrackForLane(track->lane_index); video_track != nullptr) {
    video_track->locked = track->locked;
  }
  emit sequenceChanged();
  setDirty(true);
}

void TimelineDocumentController::toggleTrackMuted(int track_row_index) {
  auto* track = trackAt(track_row_index);
  if (track == nullptr) {
    return;
  }

  track->muted = !track->muted;
  emit sequenceChanged();
  emit playbackStateChanged();
  setDirty(true);
}

void TimelineDocumentController::toggleTrackSolo(int track_row_index) {
  auto* track = trackAt(track_row_index);
  if (track == nullptr) {
    return;
  }

  track->solo = !track->solo;
  emit sequenceChanged();
  emit playbackStateChanged();
  setDirty(true);
}

void TimelineDocumentController::toggleTrackVisibility(int track_row_index) {
  auto* track = trackAt(track_row_index);
  if (track == nullptr) {
    return;
  }

  track->visible = !track->visible;
  if (track->kind == TrackKind::Audio) {
    if (auto* video_track = videoTrackForLane(track->lane_index); video_track != nullptr) {
      video_track->visible = track->visible;
    }
  } else if (auto* audio_track = audioTrackForLane(track->lane_index); audio_track != nullptr) {
    audio_track->visible = track->visible;
  }
  emit sequenceChanged();
  emit playbackStateChanged();
  setDirty(true);
}

void TimelineDocumentController::toggleTrackTargeted(int track_row_index) {
  auto* track = trackAt(track_row_index);
  if (track == nullptr) {
    return;
  }

  track->targeted = !track->targeted;
  if (track->kind == TrackKind::Video) {
    if (auto* audio_track = audioTrackForLane(track->lane_index); audio_track != nullptr) {
      audio_track->targeted = track->targeted;
    }
  } else if (auto* video_track = videoTrackForLane(track->lane_index); video_track != nullptr) {
    video_track->targeted = track->targeted;
  }
  emit sequenceChanged();
  setDirty(true);
}

void TimelineDocumentController::setSelectedTrimStartSeconds(double seconds) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(0.0, seconds, qMax(0.0, clip->trim_end_seconds - 0.05));
  if (qFuzzyCompare(clip->trim_start_seconds + 1.0, bounded + 1.0)) {
    return;
  }

  clip->trim_start_seconds = bounded;
  syncAudioTracks();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::setSelectedTrimEndSeconds(double seconds) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(clip->trim_start_seconds + 0.05, seconds, clip->duration_seconds);
  if (qFuzzyCompare(clip->trim_end_seconds + 1.0, bounded + 1.0)) {
    return;
  }

  clip->trim_end_seconds = bounded;
  syncAudioTracks();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::trimSelectedClipStartToSequenceSeconds(double seconds, bool ripple) {
  if (!continuous_edit_active_) {
    pushUndoSnapshot();
  }
  const auto ref = selectedClipRef();
  auto* track = trackAt(ref.track_index);
  auto* clip = clipAt(ref);
  if (track == nullptr || clip == nullptr || track->kind != TrackKind::Video) {
    return;
  }

  constexpr double kMinimumClipLength = 0.05;
  const auto minimum_start = qMax(0.0, clip->start_seconds - clip->trim_start_seconds);
  const auto maximum_start = clip->start_seconds + qMax(0.0, clipTrimmedDuration(*clip) - kMinimumClipLength);
  const auto bounded_start = qBound(minimum_start, seconds, maximum_start);
  const auto delta = bounded_start - clip->start_seconds;
  if (qAbs(delta) <= kBoundaryEpsilon) {
    return;
  }

  const auto previous_end_seconds = clipEndSeconds(*clip);
  if (ripple) {
    clip->trim_start_seconds = qBound(
        0.0,
        clip->trim_start_seconds + delta,
        qMax(0.0, clip->trim_end_seconds - kMinimumClipLength));
    clip->start_seconds = qMax(0.0, clip->start_seconds);
    const auto ripple_delta_seconds = clipEndSeconds(*clip) - previous_end_seconds;
    if (qAbs(ripple_delta_seconds) > kBoundaryEpsilon) {
      shiftAllTrackClipsFrom(previous_end_seconds, ripple_delta_seconds, clip->clip_id);
      shiftMarkersFrom(previous_end_seconds, ripple_delta_seconds);
    }
  } else {
    clip->start_seconds = bounded_start;
    clip->trim_start_seconds = qBound(
        0.0,
        clip->trim_start_seconds + delta,
        qMax(0.0, clip->trim_end_seconds - kMinimumClipLength));
  }
  sort_track_clips(track);
  syncAudioTracks();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::trimSelectedClipEndToSequenceSeconds(double seconds, bool ripple) {
  if (!continuous_edit_active_) {
    pushUndoSnapshot();
  }
  const auto ref = selectedClipRef();
  auto* track = trackAt(ref.track_index);
  auto* clip = clipAt(ref);
  if (clip == nullptr || track == nullptr || track->kind != TrackKind::Video) {
    return;
  }

  constexpr double kMinimumClipLength = 0.05;
  const auto previous_end_seconds = clipEndSeconds(*clip);
  const auto minimum_end = clip->start_seconds + kMinimumClipLength;
  const auto maximum_end = clip->start_seconds + qMax(kMinimumClipLength, clip->duration_seconds - clip->trim_start_seconds);
  const auto bounded_end = qBound(minimum_end, seconds, maximum_end);
  const auto next_trim_end = clip->trim_start_seconds + qMax(0.0, bounded_end - clip->start_seconds);
  if (qAbs(next_trim_end - clip->trim_end_seconds) <= kBoundaryEpsilon) {
    return;
  }

  clip->trim_end_seconds = qBound(
      clip->trim_start_seconds + kMinimumClipLength,
      next_trim_end,
      clip->duration_seconds);
  if (ripple) {
    const auto delta_seconds = clipEndSeconds(*clip) - previous_end_seconds;
    if (qAbs(delta_seconds) > kBoundaryEpsilon) {
      shiftAllTrackClipsFrom(previous_end_seconds, delta_seconds, clip->clip_id);
      shiftMarkersFrom(previous_end_seconds, delta_seconds);
    }
  }
  syncAudioTracks();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
}

bool TimelineDocumentController::rollSelectedClipStartToSequenceSeconds(double seconds) {
  if (!continuous_edit_active_) {
    pushUndoSnapshot();
  }
  const auto ref = selectedClipRef();
  auto* track = trackAt(ref.track_index);
  auto* clip = clipAt(ref);
  if (track == nullptr || clip == nullptr || track->kind != TrackKind::Video) {
    return false;
  }

  sort_track_clips(track);
  auto selected_index = -1;
  for (int index = 0; index < track->clips.size(); ++index) {
    if (track->clips.at(index).clip_id == clip->clip_id) {
      selected_index = index;
      break;
    }
  }
  if (selected_index <= 0) {
    return false;
  }

  constexpr double kMinimumClipLength = 0.05;
  auto& previous_clip = track->clips[selected_index - 1];
  auto& selected_clip = track->clips[selected_index];
  const auto minimum_boundary = previous_clip.start_seconds + kMinimumClipLength;
  const auto maximum_boundary =
      selected_clip.start_seconds + qMax(kMinimumClipLength, clipTrimmedDuration(selected_clip) - kMinimumClipLength);
  const auto bounded_boundary = qBound(minimum_boundary, seconds, maximum_boundary);
  if (qAbs(bounded_boundary - selected_clip.start_seconds) <= kBoundaryEpsilon) {
    return false;
  }

  previous_clip.trim_end_seconds =
      previous_clip.trim_start_seconds + qMax(kMinimumClipLength, bounded_boundary - previous_clip.start_seconds);
  selected_clip.trim_start_seconds =
      qBound(0.0, selected_clip.trim_start_seconds + (bounded_boundary - selected_clip.start_seconds),
             qMax(0.0, selected_clip.trim_end_seconds - kMinimumClipLength));
  selected_clip.start_seconds = bounded_boundary;
  syncAudioTracks();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
  return true;
}

bool TimelineDocumentController::rollSelectedClipEndToSequenceSeconds(double seconds) {
  if (!continuous_edit_active_) {
    pushUndoSnapshot();
  }
  const auto ref = selectedClipRef();
  auto* track = trackAt(ref.track_index);
  auto* clip = clipAt(ref);
  if (track == nullptr || clip == nullptr || track->kind != TrackKind::Video) {
    return false;
  }

  sort_track_clips(track);
  auto selected_index = -1;
  for (int index = 0; index < track->clips.size(); ++index) {
    if (track->clips.at(index).clip_id == clip->clip_id) {
      selected_index = index;
      break;
    }
  }
  if (selected_index < 0 || selected_index >= track->clips.size() - 1) {
    return false;
  }

  constexpr double kMinimumClipLength = 0.05;
  auto& selected_clip = track->clips[selected_index];
  auto& next_clip = track->clips[selected_index + 1];
  const auto minimum_boundary = selected_clip.start_seconds + kMinimumClipLength;
  const auto maximum_boundary = clipEndSeconds(next_clip) - kMinimumClipLength;
  const auto bounded_boundary = qBound(minimum_boundary, seconds, maximum_boundary);
  if (qAbs(bounded_boundary - clipEndSeconds(selected_clip)) <= kBoundaryEpsilon) {
    return false;
  }

  selected_clip.trim_end_seconds =
      qBound(selected_clip.trim_start_seconds + kMinimumClipLength,
             selected_clip.trim_start_seconds + qMax(0.0, bounded_boundary - selected_clip.start_seconds),
             selected_clip.duration_seconds);
  next_clip.trim_start_seconds =
      qBound(0.0, next_clip.trim_start_seconds + (bounded_boundary - next_clip.start_seconds),
             qMax(0.0, next_clip.trim_end_seconds - kMinimumClipLength));
  next_clip.start_seconds = bounded_boundary;
  syncAudioTracks();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
  return true;
}

bool TimelineDocumentController::slipSelectedClipBySeconds(double delta_seconds) {
  if (!continuous_edit_active_) {
    pushUndoSnapshot();
  }
  const auto ref = selectedClipRef();
  auto* clip = clipAt(ref);
  auto* track = trackAt(ref.track_index);
  if (clip == nullptr || track == nullptr || track->kind != TrackKind::Video) {
    return false;
  }

  const auto trimmed_duration = clipTrimmedDuration(*clip);
  const auto next_trim_start = qBound(0.0, clip->trim_start_seconds + delta_seconds, qMax(0.0, clip->duration_seconds - trimmed_duration));
  if (qAbs(next_trim_start - clip->trim_start_seconds) <= kBoundaryEpsilon) {
    return false;
  }

  clip->trim_start_seconds = next_trim_start;
  clip->trim_end_seconds = next_trim_start + trimmed_duration;
  syncAudioTracks();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
  return true;
}

void TimelineDocumentController::setSelectedFrameMode(const QString& frame_mode) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto normalized = frame_mode.trimmed().toLower();
  const auto next_mode =
      normalized == QStringLiteral("fill") || normalized == QStringLiteral("stretch") ? normalized : QStringLiteral("fit");
  if (clip->frame_mode == next_mode) {
    return;
  }

  clip->frame_mode = next_mode;
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::setSelectedFrameScale(double frame_scale) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(1.0, frame_scale, 2.5);
  if (qAbs(clip->frame_scale - bounded) <= kBoundaryEpsilon) {
    return;
  }
  clip->frame_scale = bounded;
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::setSelectedFrameOffsetX(double frame_offset_x) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(-1.0, frame_offset_x, 1.0);
  if (qAbs(clip->frame_offset_x - bounded) <= kBoundaryEpsilon) {
    return;
  }
  clip->frame_offset_x = bounded;
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::setSelectedFrameOffsetY(double frame_offset_y) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(-1.0, frame_offset_y, 1.0);
  if (qAbs(clip->frame_offset_y - bounded) <= kBoundaryEpsilon) {
    return;
  }
  clip->frame_offset_y = bounded;
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::setSelectedClipOpacity(double opacity) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(0.0, opacity, 1.0);
  if (qAbs(clip->opacity - bounded) <= kBoundaryEpsilon) {
    return;
  }
  clip->opacity = bounded;
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::setSelectedClipAudioGain(double gain_db) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(-36.0, gain_db, 12.0);
  if (qAbs(clip->audio_gain_db - bounded) <= kBoundaryEpsilon) {
    return;
  }
  clip->audio_gain_db = bounded;
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::setSelectedClipFadeInSeconds(double seconds) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(0.0, seconds, qMax(0.0, clipTrimmedDuration(*clip) - 0.01));
  if (qAbs(clip->fade_in_seconds - bounded) <= kBoundaryEpsilon) {
    return;
  }
  clip->fade_in_seconds = bounded;
  if (clip->fade_out_seconds > qMax(0.0, clipTrimmedDuration(*clip) - clip->fade_in_seconds)) {
    clip->fade_out_seconds = qMax(0.0, clipTrimmedDuration(*clip) - clip->fade_in_seconds);
  }
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::setSelectedClipFadeOutSeconds(double seconds) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(0.0, seconds, qMax(0.0, clipTrimmedDuration(*clip) - 0.01));
  if (qAbs(clip->fade_out_seconds - bounded) <= kBoundaryEpsilon) {
    return;
  }
  clip->fade_out_seconds = bounded;
  if (clip->fade_in_seconds > qMax(0.0, clipTrimmedDuration(*clip) - clip->fade_out_seconds)) {
    clip->fade_in_seconds = qMax(0.0, clipTrimmedDuration(*clip) - clip->fade_out_seconds);
  }
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::setSelectedClipPlaybackRate(double playback_rate) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto bounded = qBound(0.25, playback_rate, 4.0);
  if (qAbs(clip->playback_rate - bounded) <= kBoundaryEpsilon) {
    return;
  }
  clip->playback_rate = bounded;
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::addSelectedClipEffect(const QString& effect_name) {
  pushUndoSnapshot();
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto normalized = normalized_effect_name(effect_name);
  if (normalized.isEmpty() || clip->effect_stack.contains(normalized)) {
    return;
  }

  clip->effect_stack.push_back(normalized);
  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::removeSelectedClipEffect(const QString& effect_name) {
  pushUndoSnapshot();
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto normalized = normalized_effect_name(effect_name);
  if (normalized.isEmpty() || !clip->effect_stack.removeOne(normalized)) {
    return;
  }

  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::assignSelectedClipTransition(
    const QString& edge,
    const QString& transition_name,
    double duration_seconds) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto normalized_name = normalized_effect_name(transition_name);
  if (normalized_name.isEmpty()) {
    return;
  }

  const auto normalized_edge = normalized_transition_edge(edge);
  const auto bounded_duration = qBound(0.0, duration_seconds, qMax(0.0, clipTrimmedDuration(*clip) - 0.01));
  if (normalized_edge == QStringLiteral("in")) {
    if (clip->transition_in_name == normalized_name &&
        qAbs(clip->transition_in_seconds - bounded_duration) <= kBoundaryEpsilon) {
      return;
    }
    clip->transition_in_name = normalized_name;
    clip->transition_in_seconds = bounded_duration;
  } else {
    if (clip->transition_out_name == normalized_name &&
        qAbs(clip->transition_out_seconds - bounded_duration) <= kBoundaryEpsilon) {
      return;
    }
    clip->transition_out_name = normalized_name;
    clip->transition_out_seconds = bounded_duration;
  }

  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::clearSelectedClipTransition(const QString& edge) {
  auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  const auto normalized_edge = normalized_transition_edge(edge);
  if (normalized_edge == QStringLiteral("in")) {
    if (clip->transition_in_name.isEmpty() && clip->transition_in_seconds <= kBoundaryEpsilon) {
      return;
    }
    clip->transition_in_name.clear();
    clip->transition_in_seconds = 0.0;
  } else {
    if (clip->transition_out_name.isEmpty() && clip->transition_out_seconds <= kBoundaryEpsilon) {
      return;
    }
    clip->transition_out_name.clear();
    clip->transition_out_seconds = 0.0;
  }

  syncAudioTracks();
  emitAllState();
  setDirty(true);
}

double TimelineDocumentController::sourceDurationSecondsForFile(const QString& file_path) const {
  const auto normalized = normalized_path(file_path);
  if (!QFileInfo(normalized).exists()) {
    return 0.0;
  }
  if (isVideoFile(normalized)) {
    return probeVideoDurationSeconds(normalized);
  }
  if (isAudioFile(normalized)) {
    return probeAudioDurationSeconds(normalized);
  }
  if (isImageFile(normalized)) {
    return kDefaultStillDurationSeconds;
  }
  return 0.0;
}

bool TimelineDocumentController::splitSelectedClip(double split_seconds) {
  pushUndoSnapshot();
  const auto ref = selectedClipRef();
  auto* track = trackAt(ref.track_index);
  auto* clip = clipAt(ref);
  if (track == nullptr || clip == nullptr || track->kind != TrackKind::Video) {
    setStatus(QStringLiteral("Select a clip before splitting the timeline."));
    return false;
  }

  const auto minimum_clip_length = 0.05;
  const auto split_point = qBound(
      clip->trim_start_seconds + minimum_clip_length,
      split_seconds,
      clip->trim_end_seconds - minimum_clip_length);

  if (split_point <= clip->trim_start_seconds + minimum_clip_length / 2.0 ||
      split_point >= clip->trim_end_seconds - minimum_clip_length / 2.0) {
    setStatus(QStringLiteral("Move the playhead inside the clip before splitting."));
    return false;
  }

  Clip trailing = *clip;
  trailing.clip_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  trailing.trim_start_seconds = split_point;
  trailing.start_seconds = clip->start_seconds + qMax(0.0, split_point - clip->trim_start_seconds);
  trailing.linked_group_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  clip->trim_end_seconds = split_point;
  track->clips.push_back(trailing);
  sort_track_clips(track);
  syncAudioTracks();
  selected_clip_id_ = trailing.clip_id;
  updateCurrentIndexFromSelection();
  preview_source_ = trailing.poster_source;
  clampPlaybackState();
  sequence_playing_ = false;
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Split '%1' at %2.").arg(trailing.display_label, formatDurationText(split_point)));
  return true;
}

void TimelineDocumentController::loadSourceClipFromGallery(
    const QString& asset_id,
    const QString& file_path,
    const QString& display_label,
    double duration_seconds,
    const QString& poster_source) {
  auto clip = buildGalleryClip(asset_id, file_path, display_label, duration_seconds, 0.0, 0.0, poster_source);
  if (clip.duration_seconds <= 0.0) {
    setStatus(QStringLiteral("Unable to load the selected media."));
    return;
  }

  source_clip_ = clip;
  has_source_clip_ = true;
  source_in_seconds_ = clip.trim_start_seconds;
  source_out_seconds_ = clip.trim_end_seconds;
  emitSourceMonitorState();
  setStatus(QStringLiteral("Loaded '%1' into Source.").arg(clip.display_label));
}

void TimelineDocumentController::clearSourceClip() {
  if (!has_source_clip_) {
    return;
  }

  has_source_clip_ = false;
  source_clip_ = Clip{};
  source_in_seconds_ = 0.0;
  source_out_seconds_ = 0.0;
  emitSourceMonitorState();
}

void TimelineDocumentController::setSourceInSeconds(double seconds) {
  if (!has_source_clip_) {
    return;
  }

  const auto bounded = qBound(0.0, seconds, qMax(0.0, source_out_seconds_ - 0.05));
  if (qAbs(source_in_seconds_ - bounded) <= kBoundaryEpsilon) {
    return;
  }
  source_in_seconds_ = bounded;
  emitSourceMonitorState();
}

void TimelineDocumentController::setSourceOutSeconds(double seconds) {
  if (!has_source_clip_) {
    return;
  }

  const auto bounded = qBound(source_in_seconds_ + 0.05, seconds, source_clip_.duration_seconds);
  if (qAbs(source_out_seconds_ - bounded) <= kBoundaryEpsilon) {
    return;
  }
  source_out_seconds_ = bounded;
  emitSourceMonitorState();
}

void TimelineDocumentController::clearSourceRange() {
  if (!has_source_clip_) {
    return;
  }

  source_in_seconds_ = 0.0;
  source_out_seconds_ = source_clip_.duration_seconds;
  emitSourceMonitorState();
}

void TimelineDocumentController::appendSourceClipToTimeline() {
  if (!has_source_clip_) {
    setStatus(QStringLiteral("Load a source clip before appending."));
    return;
  }

  auto* target_track =
      source_clip_.media_type == QStringLiteral("audio") ? defaultTargetAudioTrack() : defaultTargetVideoTrack();
  if (target_track == nullptr) {
    setStatus(QStringLiteral("The sequence tracks are unavailable."));
    return;
  }

  auto clip = buildGalleryClip(
      source_clip_.asset_id,
      source_clip_.file_path,
      source_clip_.display_label,
      source_clip_.duration_seconds,
      source_in_seconds_,
      source_out_seconds_,
      source_clip_.poster_source);
  if (clip.duration_seconds <= 0.0 || clipTrimmedDuration(clip) <= 0.0) {
    setStatus(QStringLiteral("Set a valid source in and out before appending."));
    return;
  }

  clip.start_seconds = trackEndSeconds(*target_track);
  target_track->clips.push_back(clip);
  sort_track_clips(target_track);
  syncAudioTracks();
  refreshSequenceSizeFromContent();
  if (target_track->kind == TrackKind::Video) {
    selected_clip_id_ = clip.clip_id;
    updateCurrentIndexFromSelection();
    preview_source_ = clip.poster_source;
    current_clip_playhead_seconds_ = clip.trim_start_seconds;
    sequence_playhead_seconds_ = clip.start_seconds;
  }
  sequence_playing_ = false;
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Appended '%1' to %2.").arg(clip.display_label, target_track->display_label));
}

void TimelineDocumentController::insertSourceClipAtPlayhead() {
  if (!has_source_clip_) {
    setStatus(QStringLiteral("Load a source clip before inserting."));
    return;
  }

  insertClipFromGallery(
      source_clip_.asset_id,
      source_clip_.file_path,
      source_clip_.display_label,
      source_clip_.duration_seconds,
      source_in_seconds_,
      source_out_seconds_,
      source_clip_.poster_source);
}

void TimelineDocumentController::overwriteSourceClipAtPlayhead() {
  if (!has_source_clip_) {
    setStatus(QStringLiteral("Load a source clip before overwriting."));
    return;
  }

  overwriteClipFromGallery(
      source_clip_.asset_id,
      source_clip_.file_path,
      source_clip_.display_label,
      source_clip_.duration_seconds,
      source_in_seconds_,
      source_out_seconds_,
      source_clip_.poster_source);
}

QString TimelineDocumentController::addSequenceMarker(const QString& label) {
  Marker marker;
  marker.marker_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  marker.label = label.trimmed().isEmpty()
                     ? QStringLiteral("Marker %1").arg(markers_.size() + 1)
                     : label.trimmed();
  marker.seconds = qBound(0.0, sequence_playhead_seconds_, totalDurationSeconds());
  markers_.push_back(marker);
  std::sort(
      markers_.begin(),
      markers_.end(),
      [](const auto& left, const auto& right) { return left.seconds < right.seconds; });
  emitMarkersState();
  setDirty(true);
  return marker.marker_id;
}

void TimelineDocumentController::moveSequenceMarker(const QString& marker_id, double seconds) {
  for (auto& marker : markers_) {
    if (marker.marker_id != marker_id) {
      continue;
    }
    marker.seconds = qBound(0.0, seconds, totalDurationSeconds());
    std::sort(
        markers_.begin(),
        markers_.end(),
        [](const auto& left, const auto& right) { return left.seconds < right.seconds; });
    emitMarkersState();
    setDirty(true);
    return;
  }
}

void TimelineDocumentController::renameSequenceMarker(const QString& marker_id, const QString& label) {
  for (auto& marker : markers_) {
    if (marker.marker_id != marker_id) {
      continue;
    }
    marker.label = label.trimmed().isEmpty() ? marker.label : label.trimmed();
    emitMarkersState();
    setDirty(true);
    return;
  }
}

void TimelineDocumentController::setSequenceMarkerNote(const QString& marker_id, const QString& note) {
  const auto normalized_note = note.trimmed();
  for (auto& marker : markers_) {
    if (marker.marker_id != marker_id) {
      continue;
    }
    if (marker.note == normalized_note) {
      return;
    }
    marker.note = normalized_note;
    emitMarkersState();
    setDirty(true);
    return;
  }
}

void TimelineDocumentController::setSequenceMarkerStatus(const QString& marker_id, const QString& status) {
  const auto normalized_status = normalized_marker_status(status);
  for (auto& marker : markers_) {
    if (marker.marker_id != marker_id) {
      continue;
    }
    if (marker.status == normalized_status) {
      return;
    }
    marker.status = normalized_status;
    emitMarkersState();
    setDirty(true);
    return;
  }
}

void TimelineDocumentController::setSequenceMarkerCompleted(const QString& marker_id, bool completed) {
  for (auto& marker : markers_) {
    if (marker.marker_id != marker_id) {
      continue;
    }
    if (marker.completed == completed) {
      return;
    }
    marker.completed = completed;
    emitMarkersState();
    setDirty(true);
    return;
  }
}

void TimelineDocumentController::seekSequenceMarker(const QString& marker_id) {
  for (const auto& marker : markers_) {
    if (marker.marker_id != marker_id) {
      continue;
    }
    seekSequenceSeconds(marker.seconds);
    return;
  }
}

bool TimelineDocumentController::seekNextSequenceMarker() {
  for (const auto& marker : markers_) {
    if (marker.seconds > sequence_playhead_seconds_ + kBoundaryEpsilon) {
      seekSequenceSeconds(marker.seconds);
      return true;
    }
  }
  return false;
}

bool TimelineDocumentController::seekPreviousSequenceMarker() {
  for (auto index = markers_.size() - 1; index >= 0; --index) {
    const auto& marker = markers_.at(index);
    if (marker.seconds < sequence_playhead_seconds_ - kBoundaryEpsilon) {
      seekSequenceSeconds(marker.seconds);
      return true;
    }
  }
  return false;
}

bool TimelineDocumentController::seekNextEditPoint() {
  QVector<double> boundaries;
  for (const auto& track : tracks_) {
    if (track.kind != TrackKind::Video) {
      continue;
    }
    for (const auto& clip : track.clips) {
      boundaries.push_back(qMax(0.0, clip.start_seconds));
      boundaries.push_back(qMax(0.0, clipEndSeconds(clip)));
    }
  }
  if (boundaries.isEmpty()) {
    return false;
  }

  std::sort(boundaries.begin(), boundaries.end());
  double previous_value = -1.0;
  for (const auto value : boundaries) {
    if (previous_value >= 0.0 && qAbs(value - previous_value) <= kBoundaryEpsilon) {
      continue;
    }
    previous_value = value;
    if (value > sequence_playhead_seconds_ + kBoundaryEpsilon) {
      seekSequenceSeconds(value);
      return true;
    }
  }
  return false;
}

bool TimelineDocumentController::seekPreviousEditPoint() {
  QVector<double> boundaries;
  for (const auto& track : tracks_) {
    if (track.kind != TrackKind::Video) {
      continue;
    }
    for (const auto& clip : track.clips) {
      boundaries.push_back(qMax(0.0, clip.start_seconds));
      boundaries.push_back(qMax(0.0, clipEndSeconds(clip)));
    }
  }
  if (boundaries.isEmpty()) {
    return false;
  }

  std::sort(boundaries.begin(), boundaries.end());
  double previous_value = -1.0;
  auto candidate = -1.0;
  for (const auto value : boundaries) {
    if (previous_value >= 0.0 && qAbs(value - previous_value) <= kBoundaryEpsilon) {
      continue;
    }
    previous_value = value;
    if (value < sequence_playhead_seconds_ - kBoundaryEpsilon) {
      candidate = value;
    }
  }
  if (candidate < 0.0) {
    return false;
  }
  seekSequenceSeconds(candidate);
  return true;
}

void TimelineDocumentController::removeSequenceMarker(const QString& marker_id) {
  for (int index = 0; index < markers_.size(); ++index) {
    if (markers_.at(index).marker_id != marker_id) {
      continue;
    }
    markers_.removeAt(index);
    emitMarkersState();
    setDirty(true);
    return;
  }
}

void TimelineDocumentController::playSequence() {
  if (clipCount() == 0) {
    return;
  }

  clampPlaybackState();
  if (selectedClipRef().isValid()) {
    sequence_playing_ = true;
    emit playbackStateChanged();
  }
}

void TimelineDocumentController::pauseSequence() {
  if (!sequence_playing_) {
    return;
  }

  sequence_playing_ = false;
  emit playbackStateChanged();
}

void TimelineDocumentController::seekSequenceSeconds(double seconds) {
  sequence_playhead_seconds_ = qBound(0.0, seconds, totalDurationSeconds());
  sequence_playing_ = false;

  const auto active_ref = activeVideoClipRefForTime(sequence_playhead_seconds_);
  const auto* active_clip = clipAt(active_ref);
  if (active_clip == nullptr) {
    selected_clip_id_.clear();
    current_index_ = -1;
    current_clip_playhead_seconds_ = 0.0;
    preview_source_.clear();
    emit selectionChanged();
    emit previewSourceChanged();
    emit playbackStateChanged();
    return;
  }

  selected_clip_id_ = active_clip->clip_id;
  updateCurrentIndexFromSelection();
  current_clip_playhead_seconds_ = clipLocalSecondsForSequenceTime(*active_clip, sequence_playhead_seconds_);
  preview_source_ = active_clip->poster_source;
  emit selectionChanged();
  emit previewSourceChanged();
  emit playbackStateChanged();
}

void TimelineDocumentController::seekCurrentClipSeconds(double seconds) {
  const auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  current_clip_playhead_seconds_ = qBound(clip->trim_start_seconds, seconds, clip->trim_end_seconds);
  sequence_playhead_seconds_ =
      clip->start_seconds + qMax(0.0, current_clip_playhead_seconds_ - clip->trim_start_seconds);
  sequence_playing_ = false;
  emit playbackStateChanged();
}

void TimelineDocumentController::stepSequenceFrames(int direction) {
  if (direction == 0) {
    return;
  }

  seekSequenceSeconds(sequence_playhead_seconds_ + (static_cast<double>(direction) / framesPerSecond()));
}

void TimelineDocumentController::syncPlaybackPosition(double clip_seconds) {
  const auto* clip = clipAt(selectedClipRef());
  if (clip == nullptr) {
    return;
  }

  current_clip_playhead_seconds_ = qBound(clip->trim_start_seconds, clip_seconds, clip->trim_end_seconds);
  sequence_playhead_seconds_ =
      clip->start_seconds + qMax(0.0, current_clip_playhead_seconds_ - clip->trim_start_seconds);
  emit playbackStateChanged();
}

bool TimelineDocumentController::advancePlaybackAfterClipEnd() {
  if (clipCount() == 0) {
    return false;
  }

  const auto* current_clip = clipAt(selectedClipRef());
  if (current_clip == nullptr) {
    sequence_playing_ = false;
    clampPlaybackState();
    emit playbackStateChanged();
    return false;
  }

  const auto current_clip_end = clipEndSeconds(*current_clip);
  const auto next_ref = nextActiveVideoClipRefAfter(current_clip_end + kBoundaryEpsilon);
  const auto* next_clip = clipAt(next_ref);
  if (next_clip == nullptr) {
    sequence_playing_ = false;
    clampPlaybackState();
    emit playbackStateChanged();
    return false;
  }

  const auto target_time =
      next_clip->start_seconds <= current_clip_end + kBoundaryEpsilon ? current_clip_end : next_clip->start_seconds;
  selected_clip_id_ = next_clip->clip_id;
  updateCurrentIndexFromSelection();
  current_clip_playhead_seconds_ = clipLocalSecondsForSequenceTime(*next_clip, target_time);
  sequence_playhead_seconds_ = target_time;
  preview_source_ = next_clip->poster_source;
  emit selectionChanged();
  emit previewSourceChanged();
  emit playbackStateChanged();
  return true;
}

QVariantMap TimelineDocumentController::clipMap(int index) const {
  const auto ref = clipRefAt(index);
  const auto* track = trackAt(ref.track_index);
  const auto* clip = clipAt(ref);
  if (track == nullptr || clip == nullptr) {
    QVariantMap map;
    map.insert(QStringLiteral("clipId"), QString{});
    map.insert(QStringLiteral("index"), index);
    map.insert(QStringLiteral("selected"), false);
    map.insert(QStringLiteral("displayLabel"), QString{});
    map.insert(QStringLiteral("mediaType"), QStringLiteral("video"));
    map.insert(QStringLiteral("posterSource"), QString{});
    map.insert(QStringLiteral("durationText"), QString{});
    map.insert(QStringLiteral("trimmedDurationText"), QString{});
    map.insert(QStringLiteral("trimmedDurationSeconds"), 0.0);
    map.insert(QStringLiteral("timelineOffsetSeconds"), 0.0);
    map.insert(QStringLiteral("startSeconds"), 0.0);
    map.insert(QStringLiteral("trimStartSeconds"), 0.0);
    map.insert(QStringLiteral("trimEndSeconds"), 0.0);
    map.insert(QStringLiteral("durationSeconds"), 0.0);
    map.insert(QStringLiteral("trackId"), QStringLiteral("video-1"));
    map.insert(QStringLiteral("trackLabel"), QStringLiteral("V1"));
    map.insert(QStringLiteral("trackKind"), QStringLiteral("video"));
    map.insert(QStringLiteral("trackRowIndex"), 0);
    map.insert(QStringLiteral("laneIndex"), 0);
    map.insert(QStringLiteral("hasAudio"), false);
    map.insert(QStringLiteral("silentPlaceholder"), false);
    map.insert(QStringLiteral("waveformSource"), QString{});
    map.insert(QStringLiteral("frameMode"), QStringLiteral("fit"));
    map.insert(QStringLiteral("frameScale"), 1.0);
    map.insert(QStringLiteral("frameOffsetX"), 0.0);
    map.insert(QStringLiteral("frameOffsetY"), 0.0);
    map.insert(QStringLiteral("opacity"), 1.0);
    map.insert(QStringLiteral("audioGainDb"), 0.0);
    map.insert(QStringLiteral("fadeInSeconds"), 0.0);
    map.insert(QStringLiteral("fadeOutSeconds"), 0.0);
    map.insert(QStringLiteral("playbackRate"), 1.0);
    map.insert(QStringLiteral("effectStack"), QVariantList{});
    map.insert(QStringLiteral("transitionInName"), QString{});
    map.insert(QStringLiteral("transitionOutName"), QString{});
    map.insert(QStringLiteral("transitionInSeconds"), 0.0);
    map.insert(QStringLiteral("transitionOutSeconds"), 0.0);
    return map;
  }

  return clipMap(*clip, index, *track);
}

QVariantMap TimelineDocumentController::clipMap(const Clip& clip, int flattened_index, const Track& track) const {
  const auto trimmed_duration = clipTrimmedDuration(clip);
  const auto* paired_audio = track.kind == TrackKind::Video ? audioTrackForLane(track.lane_index) : nullptr;
  auto any_audio_solo = false;
  for (const auto& candidate : tracks_) {
    if (candidate.kind == TrackKind::Audio && candidate.solo) {
      any_audio_solo = true;
      break;
    }
  }
  auto track_row_index = -1;
  for (int index = 0; index < tracks_.size(); ++index) {
    const auto& candidate = tracks_.at(index);
    if (candidate.track_id == track.track_id) {
      track_row_index = index;
      break;
    }
  }
  QVariantMap map;
  map.insert(QStringLiteral("clipId"), clip.clip_id);
  map.insert(QStringLiteral("index"), flattened_index);
  map.insert(QStringLiteral("selected"), isClipSelected(clip.clip_id));
  map.insert(QStringLiteral("assetId"), clip.asset_id);
  map.insert(QStringLiteral("mediaType"), clip.media_type);
  map.insert(QStringLiteral("filePath"), clip.file_path);
  map.insert(QStringLiteral("sourceUrl"), QUrl::fromLocalFile(clip.file_path).toString());
  map.insert(QStringLiteral("displayLabel"), clip.display_label);
  map.insert(QStringLiteral("posterSource"), clip.poster_source);
  map.insert(QStringLiteral("durationSeconds"), clip.duration_seconds);
  map.insert(QStringLiteral("durationText"), formatDurationText(clip.duration_seconds));
  map.insert(QStringLiteral("trimStartSeconds"), clip.trim_start_seconds);
  map.insert(QStringLiteral("trimEndSeconds"), clip.trim_end_seconds);
  map.insert(QStringLiteral("trimmedDurationText"), formatDurationText(trimmed_duration));
  map.insert(QStringLiteral("trimmedDurationSeconds"), trimmed_duration);
  map.insert(QStringLiteral("timelineOffsetSeconds"), clip.start_seconds);
  map.insert(QStringLiteral("startSeconds"), clip.start_seconds);
  map.insert(QStringLiteral("endSeconds"), clip.start_seconds + trimmed_duration);
  map.insert(QStringLiteral("trackId"), track.track_id);
  map.insert(QStringLiteral("trackLabel"), track.display_label);
  map.insert(QStringLiteral("trackKind"), track.kind == TrackKind::Audio ? QStringLiteral("audio") : QStringLiteral("video"));
  map.insert(QStringLiteral("trackRowIndex"), track_row_index);
  map.insert(QStringLiteral("laneIndex"), track.lane_index);
  map.insert(QStringLiteral("linkedGroupId"), clip.linked_group_id);
  map.insert(QStringLiteral("hasAudio"), clip.has_audio);
  map.insert(QStringLiteral("silentPlaceholder"), track.kind == TrackKind::Audio && !clip.has_audio);
  map.insert(QStringLiteral("waveformSource"), clip.waveform_source);
  map.insert(QStringLiteral("videoVisible"), track.kind == TrackKind::Video ? track.visible : true);
  map.insert(QStringLiteral("audioMuted"), paired_audio == nullptr ? false : paired_audio->muted);
  map.insert(QStringLiteral("audioSolo"), paired_audio == nullptr ? false : paired_audio->solo);
  map.insert(
      QStringLiteral("audioPlaybackEnabled"),
      paired_audio == nullptr ? true : (!paired_audio->muted && (!any_audio_solo || paired_audio->solo)));
  map.insert(QStringLiteral("frameMode"), clip.frame_mode);
  map.insert(QStringLiteral("frameScale"), clip.frame_scale);
  map.insert(QStringLiteral("frameOffsetX"), clip.frame_offset_x);
  map.insert(QStringLiteral("frameOffsetY"), clip.frame_offset_y);
  map.insert(QStringLiteral("opacity"), clip.opacity);
  map.insert(QStringLiteral("audioGainDb"), clip.audio_gain_db);
  map.insert(QStringLiteral("fadeInSeconds"), clip.fade_in_seconds);
  map.insert(QStringLiteral("fadeOutSeconds"), clip.fade_out_seconds);
  map.insert(QStringLiteral("playbackRate"), clip.playback_rate);
  QVariantList effect_stack;
  effect_stack.reserve(clip.effect_stack.size());
  for (const auto& effect_name : clip.effect_stack) {
    effect_stack.push_back(effect_name);
  }
  map.insert(QStringLiteral("effectStack"), effect_stack);
  map.insert(QStringLiteral("transitionInName"), clip.transition_in_name);
  map.insert(QStringLiteral("transitionOutName"), clip.transition_out_name);
  map.insert(QStringLiteral("transitionInSeconds"), clip.transition_in_seconds);
  map.insert(QStringLiteral("transitionOutSeconds"), clip.transition_out_seconds);
  return map;
}

QVariantMap TimelineDocumentController::trackMap(const Track& track, int track_index) const {
  QVariantList clip_values;
  clip_values.reserve(track.clips.size());
  for (const auto& clip : track.clips) {
    clip_values.push_back(clipMap(clip, flattenedIndexForClipId(clip.clip_id), track));
  }

  QVariantMap map;
  map.insert(QStringLiteral("trackId"), track.track_id);
  map.insert(QStringLiteral("displayLabel"), track.display_label);
  map.insert(QStringLiteral("trackKind"), track.kind == TrackKind::Audio ? QStringLiteral("audio") : QStringLiteral("video"));
  map.insert(QStringLiteral("clipCount"), static_cast<int>(track.clips.size()));
  map.insert(QStringLiteral("trackIndex"), track_index);
  map.insert(QStringLiteral("laneIndex"), track.lane_index);
  map.insert(QStringLiteral("locked"), track.locked);
  map.insert(QStringLiteral("muted"), track.muted);
  map.insert(QStringLiteral("solo"), track.solo);
  map.insert(QStringLiteral("visible"), track.visible);
  map.insert(QStringLiteral("targeted"), track.targeted);
  map.insert(QStringLiteral("clips"), clip_values);
  return map;
}

bool TimelineDocumentController::isVideoFile(const QString& file_path) const {
  return video_extensions().contains(QFileInfo(file_path).suffix().toLower());
}

bool TimelineDocumentController::isAudioFile(const QString& file_path) const {
  return audio_extensions().contains(QFileInfo(file_path).suffix().toLower());
}

bool TimelineDocumentController::isImageFile(const QString& file_path) const {
  return image_extensions().contains(QFileInfo(file_path).suffix().toLower());
}

QString TimelineDocumentController::mediaTypeForFile(const QString& file_path) const {
  if (isVideoFile(file_path)) {
    return QStringLiteral("video");
  }
  if (isAudioFile(file_path)) {
    return QStringLiteral("audio");
  }
  if (isImageFile(file_path)) {
    return QStringLiteral("photo");
  }
  return QString{};
}

QVector<TimelineDocumentController::ClipRef> TimelineDocumentController::orderedVideoClipRefs() const {
  QVector<ClipRef> refs;
  for (int track_index = 0; track_index < tracks_.size(); ++track_index) {
    const auto& track = tracks_.at(track_index);
    if (track.kind != TrackKind::Video || !track.visible) {
      continue;
    }
    for (int clip_index = 0; clip_index < track.clips.size(); ++clip_index) {
      refs.push_back({track_index, clip_index});
    }
  }

  std::sort(
      refs.begin(),
      refs.end(),
      [this](const auto& left, const auto& right) {
        const auto* left_clip = clipAt(left);
        const auto* right_clip = clipAt(right);
        const auto* left_track = trackAt(left.track_index);
        const auto* right_track = trackAt(right.track_index);
        if (left_clip == nullptr || right_clip == nullptr || left_track == nullptr || right_track == nullptr) {
          return left.track_index < right.track_index;
        }
        if (!qFuzzyCompare(left_clip->start_seconds + 1.0, right_clip->start_seconds + 1.0)) {
          return left_clip->start_seconds < right_clip->start_seconds;
        }
        if (left_track->lane_index != right_track->lane_index) {
          return left_track->lane_index < right_track->lane_index;
        }
        return left_clip->display_label < right_clip->display_label;
      });

  return refs;
}

TimelineDocumentController::ClipRef TimelineDocumentController::clipRefAt(int index) const {
  const auto refs = orderedVideoClipRefs();
  return index >= 0 && index < refs.size() ? refs.at(index) : ClipRef{};
}

TimelineDocumentController::ClipRef TimelineDocumentController::selectedClipRef() const {
  return clipRefAt(current_index_);
}

const TimelineDocumentController::Clip* TimelineDocumentController::clipAt(int index) const {
  return clipAt(clipRefAt(index));
}

TimelineDocumentController::Clip* TimelineDocumentController::clipAt(int index) {
  return clipAt(clipRefAt(index));
}

const TimelineDocumentController::Clip* TimelineDocumentController::clipAt(const ClipRef& ref) const {
  const auto* track = trackAt(ref.track_index);
  return track != nullptr && ref.clip_index >= 0 && ref.clip_index < track->clips.size()
             ? &track->clips.at(ref.clip_index)
             : nullptr;
}

TimelineDocumentController::Clip* TimelineDocumentController::clipAt(const ClipRef& ref) {
  auto* track = trackAt(ref.track_index);
  return track != nullptr && ref.clip_index >= 0 && ref.clip_index < track->clips.size()
             ? &track->clips[ref.clip_index]
             : nullptr;
}

const TimelineDocumentController::Track* TimelineDocumentController::trackAt(int track_index) const {
  return track_index >= 0 && track_index < tracks_.size() ? &tracks_.at(track_index) : nullptr;
}

TimelineDocumentController::Track* TimelineDocumentController::trackAt(int track_index) {
  return track_index >= 0 && track_index < tracks_.size() ? &tracks_[track_index] : nullptr;
}

const TimelineDocumentController::Track* TimelineDocumentController::primaryVideoTrack() const {
  return videoTrackForLane(0);
}

TimelineDocumentController::Track* TimelineDocumentController::primaryVideoTrack() {
  return videoTrackForLane(0);
}

const TimelineDocumentController::Track* TimelineDocumentController::videoTrackForLane(int lane_index) const {
  for (const auto& track : tracks_) {
    if (track.kind == TrackKind::Video && track.lane_index == lane_index) {
      return &track;
    }
  }
  return nullptr;
}

TimelineDocumentController::Track* TimelineDocumentController::videoTrackForLane(int lane_index) {
  for (auto& track : tracks_) {
    if (track.kind == TrackKind::Video && track.lane_index == lane_index) {
      return &track;
    }
  }
  return nullptr;
}

const TimelineDocumentController::Track* TimelineDocumentController::audioTrackForLane(int lane_index) const {
  for (const auto& track : tracks_) {
    if (track.kind == TrackKind::Audio && track.lane_index == lane_index) {
      return &track;
    }
  }
  return nullptr;
}

TimelineDocumentController::Track* TimelineDocumentController::audioTrackForLane(int lane_index) {
  for (auto& track : tracks_) {
    if (track.kind == TrackKind::Audio && track.lane_index == lane_index) {
      return &track;
    }
  }
  return nullptr;
}

const TimelineDocumentController::Track* TimelineDocumentController::videoTrackForRowIndex(int track_row_index) const {
  const auto* row_track = trackAt(track_row_index);
  if (row_track == nullptr) {
    return nullptr;
  }
  return row_track->kind == TrackKind::Video ? row_track : videoTrackForLane(row_track->lane_index);
}

TimelineDocumentController::Track* TimelineDocumentController::videoTrackForRowIndex(int track_row_index) {
  auto* row_track = trackAt(track_row_index);
  if (row_track == nullptr) {
    return nullptr;
  }
  return row_track->kind == TrackKind::Video ? row_track : videoTrackForLane(row_track->lane_index);
}

TimelineDocumentController::Track* TimelineDocumentController::defaultTargetVideoTrack() {
  Track* targeted_track = nullptr;
  for (auto& track : tracks_) {
    if (track.kind != TrackKind::Video || !track.targeted) {
      continue;
    }
    if (targeted_track == nullptr || track.lane_index > targeted_track->lane_index) {
      targeted_track = &track;
    }
  }
  if (targeted_track != nullptr) {
    return targeted_track;
  }
  if (const auto* selected_track = trackAt(selectedClipRef().track_index);
      selected_track != nullptr && selected_track->kind == TrackKind::Video) {
    return videoTrackForLane(selected_track->lane_index);
  }
  if (auto* primary_track = primaryVideoTrack(); primary_track != nullptr) {
    return primary_track;
  }
  initializeDefaultTracks();
  return primaryVideoTrack();
}

TimelineDocumentController::Track* TimelineDocumentController::defaultTargetAudioTrack() {
  Track* targeted_track = nullptr;
  for (auto& track : tracks_) {
    if (track.kind != TrackKind::Audio || !track.targeted) {
      continue;
    }
    if (targeted_track == nullptr || track.lane_index > targeted_track->lane_index) {
      targeted_track = &track;
    }
  }
  if (targeted_track != nullptr) {
    return targeted_track;
  }
  if (const auto* selected_track = trackAt(selectedClipRef().track_index);
      selected_track != nullptr) {
    return audioTrackForLane(selected_track->lane_index);
  }
  if (auto* primary_track = audioTrackForLane(0); primary_track != nullptr) {
    return primary_track;
  }
  initializeDefaultTracks();
  return audioTrackForLane(0);
}

const TimelineDocumentController::Track* TimelineDocumentController::pairedAudioTrack(const Track& video_track) const {
  for (const auto& track : tracks_) {
    if (track.kind == TrackKind::Audio && track.lane_index == video_track.lane_index) {
      return &track;
    }
  }
  return nullptr;
}

TimelineDocumentController::Track* TimelineDocumentController::pairedAudioTrack(const Track& video_track) {
  for (auto& track : tracks_) {
    if (track.kind == TrackKind::Audio && track.lane_index == video_track.lane_index) {
      return &track;
    }
  }
  return nullptr;
}

TimelineDocumentController::ClipRef TimelineDocumentController::activeVideoClipRefForTime(double seconds) const {
  const auto bounded = qMax(0.0, seconds);
  auto best_ref = ClipRef{};
  auto best_lane = -1;
  auto best_start = -1.0;
  for (int track_index = 0; track_index < tracks_.size(); ++track_index) {
    const auto& track = tracks_.at(track_index);
    if (track.kind != TrackKind::Video || !track.visible) {
      continue;
    }
    for (int clip_index = 0; clip_index < track.clips.size(); ++clip_index) {
      const auto& clip = track.clips.at(clip_index);
      if (bounded + kBoundaryEpsilon < clip.start_seconds ||
          bounded >= clipEndSeconds(clip) - kBoundaryEpsilon) {
        continue;
      }
      if (track.lane_index > best_lane ||
          (track.lane_index == best_lane && clip.start_seconds > best_start)) {
        best_ref = {track_index, clip_index};
        best_lane = track.lane_index;
        best_start = clip.start_seconds;
      }
    }
  }
  return best_ref;
}

TimelineDocumentController::ClipRef TimelineDocumentController::nextActiveVideoClipRefAfter(double seconds) const {
  const auto immediate = activeVideoClipRefForTime(seconds);
  if (immediate.isValid()) {
    return immediate;
  }

  auto best_ref = ClipRef{};
  auto earliest_start = std::numeric_limits<double>::max();
  auto highest_lane = -1;
  for (int track_index = 0; track_index < tracks_.size(); ++track_index) {
    const auto& track = tracks_.at(track_index);
    if (track.kind != TrackKind::Video || !track.visible) {
      continue;
    }
    for (int clip_index = 0; clip_index < track.clips.size(); ++clip_index) {
      const auto& clip = track.clips.at(clip_index);
      if (clip.start_seconds + kBoundaryEpsilon < seconds) {
        continue;
      }
      if (clip.start_seconds < earliest_start - kBoundaryEpsilon ||
          (qAbs(clip.start_seconds - earliest_start) <= kBoundaryEpsilon && track.lane_index > highest_lane)) {
        best_ref = {track_index, clip_index};
        earliest_start = clip.start_seconds;
        highest_lane = track.lane_index;
      }
    }
  }
  return best_ref;
}

int TimelineDocumentController::flattenedIndexForClipId(const QString& clip_id) const {
  if (clip_id.isEmpty()) {
    return -1;
  }

  const auto refs = orderedVideoClipRefs();
  for (int index = 0; index < refs.size(); ++index) {
    const auto* clip = clipAt(refs.at(index));
    if (clip != nullptr && clip->clip_id == clip_id) {
      return index;
    }
  }
  return -1;
}

int TimelineDocumentController::nextLaneIndex() const {
  auto lane_index = -1;
  for (const auto& track : tracks_) {
    lane_index = qMax(lane_index, track.lane_index);
  }
  return lane_index + 1;
}

double TimelineDocumentController::clipTrimmedDuration(const Clip& clip) const {
  return qMax(0.0, clip.trim_end_seconds - clip.trim_start_seconds);
}

double TimelineDocumentController::clipTrimmedDuration(int index) const {
  const auto* clip = clipAt(index);
  return clip == nullptr ? 0.0 : clipTrimmedDuration(*clip);
}

double TimelineDocumentController::clipEndSeconds(const Clip& clip) const {
  return clip.start_seconds + clipTrimmedDuration(clip);
}

double TimelineDocumentController::clipTimelineOffsetSeconds(int index) const {
  const auto* clip = clipAt(index);
  return clip == nullptr ? 0.0 : clip->start_seconds;
}

double TimelineDocumentController::trackEndSeconds(const Track& track) const {
  auto end = 0.0;
  for (const auto& clip : track.clips) {
    end = qMax(end, clipEndSeconds(clip));
  }
  return end;
}

double TimelineDocumentController::totalDurationSeconds() const {
  auto total = 0.0;
  for (const auto& track : tracks_) {
    if (track.kind != TrackKind::Video) {
      continue;
    }
    total = qMax(total, trackEndSeconds(track));
  }
  return total;
}

double TimelineDocumentController::clipLocalSecondsForSequenceTime(const Clip& clip, double seconds) const {
  const auto local =
      clip.trim_start_seconds + qMax(0.0, seconds - clip.start_seconds);
  return qBound(clip.trim_start_seconds, local, clip.trim_end_seconds);
}

QString TimelineDocumentController::formatDurationText(double seconds) const {
  if (seconds <= 0.0) {
    return QStringLiteral("0:00");
  }

  const auto rounded = qMax(0, static_cast<int>(qRound(seconds)));
  const auto hours = rounded / 3600;
  const auto minutes = (rounded % 3600) / 60;
  const auto remaining_seconds = rounded % 60;
  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(remaining_seconds, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2")
      .arg(minutes)
      .arg(remaining_seconds, 2, 10, QLatin1Char('0'));
}

QVariantMap TimelineDocumentController::markerMap(const Marker& marker) const {
  QVariantMap map;
  map.insert(QStringLiteral("markerId"), marker.marker_id);
  map.insert(QStringLiteral("label"), marker.label);
  map.insert(QStringLiteral("note"), marker.note);
  map.insert(QStringLiteral("status"), marker.status);
  map.insert(QStringLiteral("completed"), marker.completed);
  map.insert(QStringLiteral("color"), marker.color);
  map.insert(QStringLiteral("seconds"), marker.seconds);
  map.insert(QStringLiteral("timecode"), formatDurationText(marker.seconds));
  return map;
}

double TimelineDocumentController::probeVideoDurationSeconds(const QString& file_path) const {
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffprobe.isEmpty()) {
    return 0.0;
  }

  QProcess process;
  process.start(
      ffprobe,
      {
          QStringLiteral("-v"),
          QStringLiteral("error"),
          QStringLiteral("-show_entries"),
          QStringLiteral("format=duration"),
          QStringLiteral("-of"),
          QStringLiteral("default=noprint_wrappers=1:nokey=1"),
          file_path,
      });
  if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return 0.0;
  }

  bool okay = false;
  const auto value = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toDouble(&okay);
  return okay ? qMax(0.0, value) : 0.0;
}

double TimelineDocumentController::probeAudioDurationSeconds(const QString& file_path) const {
  return probeVideoDurationSeconds(file_path);
}

bool TimelineDocumentController::probeVideoHasAudio(const QString& file_path) const {
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffprobe.isEmpty()) {
    return false;
  }

  QProcess process;
  process.start(
      ffprobe,
      {
          QStringLiteral("-v"),
          QStringLiteral("error"),
          QStringLiteral("-select_streams"),
          QStringLiteral("a:0"),
          QStringLiteral("-show_entries"),
          QStringLiteral("stream=index"),
          QStringLiteral("-of"),
          QStringLiteral("csv=p=0"),
          file_path,
      });
  if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return false;
  }

  return !QString::fromUtf8(process.readAllStandardOutput()).trimmed().isEmpty();
}

QString TimelineDocumentController::ensureVideoPoster(const QString& file_path, const QString& cache_key) const {
  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return {};
  }

  const auto cache_seed = cache_key.isEmpty()
                              ? QStringLiteral("timeline-poster-v2:%1").arg(file_path)
                              : QStringLiteral("timeline-poster-v2:%1:%2").arg(cache_key, file_path);
  const auto digest =
      QCryptographicHash::hash(cache_seed.toUtf8(), QCryptographicHash::Sha1).toHex();
  const auto cache_path = QDir(poster_cache_root()).filePath(QStringLiteral("%1.jpg").arg(QString::fromLatin1(digest)));
  const QFileInfo cache_info(cache_path);
  if (cache_info.exists() && cache_info.lastModified() >= source_info.lastModified()) {
    return QUrl::fromLocalFile(cache_path).toString();
  }

  QDir().mkpath(poster_cache_root());

  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-y"),
          QStringLiteral("-ss"),
          QStringLiteral("0.250"),
          QStringLiteral("-i"),
          file_path,
          QStringLiteral("-frames:v"),
          QStringLiteral("1"),
          QStringLiteral("-vf"),
          QStringLiteral("scale=960:960:force_original_aspect_ratio=decrease"),
          cache_path,
      });
  if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }

  return QFileInfo::exists(cache_path) ? QUrl::fromLocalFile(cache_path).toString() : QString{};
}

QString TimelineDocumentController::ensureAudioWaveform(const QString& file_path, const QString& cache_key) const {
  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return {};
  }

  const auto cache_seed = cache_key.isEmpty()
                              ? QStringLiteral("timeline-waveform-v1:%1").arg(file_path)
                              : QStringLiteral("timeline-waveform-v1:%1:%2").arg(cache_key, file_path);
  const auto digest =
      QCryptographicHash::hash(cache_seed.toUtf8(), QCryptographicHash::Sha1).toHex();
  const auto cache_path = QDir(waveform_cache_root()).filePath(QStringLiteral("%1.png").arg(QString::fromLatin1(digest)));
  const QFileInfo cache_info(cache_path);
  if (cache_info.exists() && cache_info.lastModified() >= source_info.lastModified()) {
    return QUrl::fromLocalFile(cache_path).toString();
  }

  QDir().mkpath(waveform_cache_root());

  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-y"),
          QStringLiteral("-i"),
          file_path,
          QStringLiteral("-frames:v"),
          QStringLiteral("1"),
          QStringLiteral("-filter_complex"),
          QStringLiteral("aformat=channel_layouts=mono,showwavespic=s=1200x160:colors=0xE8EDF2"),
          cache_path,
      });
  if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }

  return QFileInfo::exists(cache_path) ? QUrl::fromLocalFile(cache_path).toString() : QString{};
}

QSize TimelineDocumentController::probeVideoSize(const QString& file_path) const {
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffprobe.isEmpty()) {
    return {};
  }

  QProcess process;
  process.start(
      ffprobe,
      {
          QStringLiteral("-v"),
          QStringLiteral("error"),
          QStringLiteral("-select_streams"),
          QStringLiteral("v:0"),
          QStringLiteral("-show_entries"),
          QStringLiteral("stream=width,height"),
          QStringLiteral("-of"),
          QStringLiteral("csv=s=x:p=0"),
          file_path,
      });
  if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }

  const auto parts = QString::fromUtf8(process.readAllStandardOutput()).trimmed().split('x');
  if (parts.size() != 2) {
    return {};
  }

  bool width_ok = false;
  bool height_ok = false;
  const auto width = parts.at(0).toInt(&width_ok);
  const auto height = parts.at(1).toInt(&height_ok);
  return width_ok && height_ok ? QSize(width, height) : QSize{};
}

bool TimelineDocumentController::renderTimelineVideo(const QString& output_path) const {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    const_cast<TimelineDocumentController*>(this)->setStatus(QStringLiteral("Install ffmpeg to save timeline renders."));
    return false;
  }

  const auto ordered_refs = orderedVideoClipRefs();
  if (ordered_refs.isEmpty()) {
    const_cast<TimelineDocumentController*>(this)->setStatus(QStringLiteral("The timeline is empty."));
    return false;
  }

  auto target_size = sequence_size_;
  if (!target_size.isValid() || target_size.isEmpty()) {
    if (const auto* first_clip = clipAt(ordered_refs.first()); first_clip != nullptr) {
      target_size = probeVideoSize(first_clip->file_path);
    }
  }
  if (!target_size.isValid() || target_size.isEmpty()) {
    target_size = QSize(kDefaultSequenceWidth, kDefaultSequenceHeight);
  }

  QVector<double> cut_points = {0.0, totalDurationSeconds()};
  for (const auto& ref : ordered_refs) {
    const auto* clip = clipAt(ref);
    if (clip == nullptr) {
      continue;
    }
    cut_points.push_back(clip->start_seconds);
    cut_points.push_back(clipEndSeconds(*clip));
  }
  std::sort(cut_points.begin(), cut_points.end());
  cut_points.erase(
      std::unique(
          cut_points.begin(),
          cut_points.end(),
          [](double left, double right) { return qAbs(left - right) <= kBoundaryEpsilon; }),
      cut_points.end());

  struct Segment final {
    ClipRef ref;
    double sequence_start = 0.0;
    double duration = 0.0;
  };

  QVector<Segment> segments;
  for (int index = 0; index + 1 < cut_points.size(); ++index) {
    const auto start = cut_points.at(index);
    const auto end = cut_points.at(index + 1);
    const auto duration = end - start;
    if (duration <= kBoundaryEpsilon) {
      continue;
    }
    segments.push_back({activeVideoClipRefForTime(start + kBoundaryEpsilon), start, duration});
  }

  if (segments.isEmpty()) {
    const_cast<TimelineDocumentController*>(this)->setStatus(QStringLiteral("The timeline has no playable segments."));
    return false;
  }

  QStringList arguments;
  QStringList filter_segments;
  QString concat_inputs;
  auto input_index = 0;
  auto output_segment_index = 0;
  auto any_audio_solo = false;
  for (const auto& track : tracks_) {
    if (track.kind == TrackKind::Audio && track.solo) {
      any_audio_solo = true;
      break;
    }
  }
  for (const auto& segment : segments) {
    if (!segment.ref.isValid()) {
      arguments << QStringLiteral("-f") << QStringLiteral("lavfi")
                << QStringLiteral("-t") << QString::number(segment.duration, 'f', 3)
                << QStringLiteral("-i")
                << QStringLiteral("color=c=black:s=%1x%2:r=%3")
                       .arg(target_size.width())
                       .arg(target_size.height())
                       .arg(QString::number(framesPerSecond(), 'f', 0))
                << QStringLiteral("-f") << QStringLiteral("lavfi")
                << QStringLiteral("-t") << QString::number(segment.duration, 'f', 3)
                << QStringLiteral("-i")
                << QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=%1").arg(audio_sample_rate_);
      const auto color_input_index = input_index++;
      const auto audio_input_index = input_index++;
      filter_segments.push_back(
          QStringLiteral("[%1:v]setsar=1,setpts=PTS-STARTPTS[v%2]").arg(color_input_index).arg(output_segment_index));
      filter_segments.push_back(
          QStringLiteral("[%1:a]asetpts=PTS-STARTPTS[a%2]").arg(audio_input_index).arg(output_segment_index));
      concat_inputs += QStringLiteral("[v%1][a%1]").arg(output_segment_index);
      ++output_segment_index;
      continue;
    }

    const auto* clip = clipAt(segment.ref);
    if (clip == nullptr) {
      continue;
    }

    const auto local_start = clip->trim_start_seconds + qMax(0.0, segment.sequence_start - clip->start_seconds);
    if (clip->media_type == QStringLiteral("photo")) {
      arguments << QStringLiteral("-loop") << QStringLiteral("1")
                << QStringLiteral("-framerate") << QString::number(framesPerSecond(), 'f', 0)
                << QStringLiteral("-t") << QString::number(segment.duration, 'f', 3)
                << QStringLiteral("-i") << clip->file_path;
    } else {
      arguments << QStringLiteral("-ss") << QString::number(local_start, 'f', 3)
                << QStringLiteral("-t") << QString::number(segment.duration, 'f', 3)
                << QStringLiteral("-i") << clip->file_path;
    }
    const auto clip_input_index = input_index++;

    filter_segments.push_back(clipVideoFilter(*clip, clip_input_index, output_segment_index, target_size));

    const auto* video_track = trackAt(segment.ref.track_index);
    const auto* audio_track = video_track == nullptr ? nullptr : audioTrackForLane(video_track->lane_index);
    const auto audio_enabled = clip->has_audio && (audio_track == nullptr
                                                       ? true
                                                       : (!audio_track->muted && (!any_audio_solo || audio_track->solo)));
    if (audio_enabled) {
      QStringList audio_filters = {
          QStringLiteral("aresample=%1").arg(audio_sample_rate_),
          QStringLiteral("aformat=sample_rates=%1:channel_layouts=stereo").arg(audio_sample_rate_),
      };
      if (qAbs(clip->audio_gain_db) > kBoundaryEpsilon) {
        audio_filters.push_back(QStringLiteral("volume=%1dB").arg(QString::number(clip->audio_gain_db, 'f', 2)));
      }
      const auto segment_offset = qMax(0.0, local_start - clip->trim_start_seconds);
      const auto trimmed_duration = clipTrimmedDuration(*clip);
      if (clip->fade_in_seconds > kBoundaryEpsilon && segment_offset < clip->fade_in_seconds) {
        const auto remaining_fade = qMin(segment.duration, clip->fade_in_seconds - segment_offset);
        if (remaining_fade > kBoundaryEpsilon) {
          audio_filters.push_back(QStringLiteral("afade=t=in:st=0:d=%1").arg(QString::number(remaining_fade, 'f', 3)));
        }
      }
      if (clip->fade_out_seconds > kBoundaryEpsilon) {
        const auto fade_out_start = qMax(0.0, trimmed_duration - clip->fade_out_seconds);
        const auto segment_end_offset = segment_offset + segment.duration;
        if (segment_end_offset > fade_out_start + kBoundaryEpsilon) {
          const auto local_fade_start = qMax(0.0, fade_out_start - segment_offset);
          const auto available_fade = qMin(segment.duration - local_fade_start, segment_end_offset - qMax(segment_offset, fade_out_start));
          if (available_fade > kBoundaryEpsilon) {
            audio_filters.push_back(
                QStringLiteral("afade=t=out:st=%1:d=%2")
                    .arg(QString::number(local_fade_start, 'f', 3))
                    .arg(QString::number(available_fade, 'f', 3)));
          }
        }
      }
      audio_filters.push_back(QStringLiteral("asetpts=PTS-STARTPTS"));
      filter_segments.push_back(
          QStringLiteral("[%1:a]%2[a%3]")
              .arg(clip_input_index)
              .arg(audio_filters.join(','))
              .arg(output_segment_index));
    } else {
      arguments << QStringLiteral("-f") << QStringLiteral("lavfi")
                << QStringLiteral("-t") << QString::number(segment.duration, 'f', 3)
                << QStringLiteral("-i")
                << QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=%1").arg(audio_sample_rate_);
      const auto silent_audio_input_index = input_index++;
      filter_segments.push_back(
          QStringLiteral("[%1:a]asetpts=PTS-STARTPTS[a%2]")
              .arg(silent_audio_input_index)
              .arg(output_segment_index));
    }

    concat_inputs += QStringLiteral("[v%1][a%1]").arg(output_segment_index);
    ++output_segment_index;
  }

  filter_segments.push_back(
      QStringLiteral("%1concat=n=%2:v=1:a=1[outv][outa]").arg(concat_inputs).arg(output_segment_index));
  arguments << QStringLiteral("-filter_complex") << filter_segments.join(';')
            << QStringLiteral("-map") << QStringLiteral("[outv]")
            << QStringLiteral("-map") << QStringLiteral("[outa]")
            << QStringLiteral("-c:v")
            << (export_codec_ == QStringLiteral("prores") ? QStringLiteral("prores_ks") : QStringLiteral("libx264"))
            << QStringLiteral("-c:a") << QStringLiteral("aac")
            << QStringLiteral("-ar") << QString::number(audio_sample_rate_)
            << QStringLiteral("-b:a") << QStringLiteral("192k")
            << QStringLiteral("-pix_fmt")
            << (export_codec_ == QStringLiteral("prores") ? QStringLiteral("yuv422p10le") : QStringLiteral("yuv420p"))
            << QStringLiteral("-movflags") << QStringLiteral("+faststart")
            << QStringLiteral("-y") << output_path;

  QProcess process;
  process.start(ffmpeg, arguments);
  if (!process.waitForFinished(120000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const auto output = QString::fromUtf8(process.readAllStandardError()).trimmed();
    const_cast<TimelineDocumentController*>(this)->setStatus(
        output.isEmpty() ? QStringLiteral("Timeline render failed.") : output);
    return false;
  }

  return QFileInfo::exists(output_path);
}

QString TimelineDocumentController::clipVideoFilter(
    const Clip& clip,
    int input_index,
    int output_index,
    const QSize& target_size) const {
  const auto target_width = target_size.width();
  const auto target_height = target_size.height();
  const auto scaled_width = qRound(target_width * qMax(1.0, clip.frame_scale));
  const auto scaled_height = qRound(target_height * qMax(1.0, clip.frame_scale));
  const auto offset_x = qRound(clip.frame_offset_x * target_width * 0.25);
  const auto offset_y = qRound(clip.frame_offset_y * target_height * 0.25);

  if (clip.frame_mode == QStringLiteral("stretch")) {
    return QStringLiteral(
               "[%1:v]scale=%2:%3,crop=%4:%5:(iw-%4)/2+%6:(ih-%5)/2+%7,setsar=1,setpts=PTS-STARTPTS[v%8]")
        .arg(input_index)
        .arg(scaled_width)
        .arg(scaled_height)
        .arg(target_width)
        .arg(target_height)
        .arg(offset_x)
        .arg(offset_y)
        .arg(output_index);
  }

  if (clip.frame_mode == QStringLiteral("fill")) {
    return QStringLiteral(
               "[%1:v]scale=%2:%3:force_original_aspect_ratio=increase,crop=%4:%5:(iw-%4)/2+%6:(ih-%5)/2+%7,setsar=1,setpts=PTS-STARTPTS[v%8]")
        .arg(input_index)
        .arg(scaled_width)
        .arg(scaled_height)
        .arg(target_width)
        .arg(target_height)
        .arg(offset_x)
        .arg(offset_y)
        .arg(output_index);
  }

  return QStringLiteral(
             "[%1:v]scale=%2:%3:force_original_aspect_ratio=decrease,pad=%2:%3:(ow-iw)/2:(oh-ih)/2:color=black,crop=%4:%5:(iw-%4)/2+%6:(ih-%5)/2+%7,setsar=1,setpts=PTS-STARTPTS[v%8]")
      .arg(input_index)
      .arg(scaled_width)
      .arg(scaled_height)
      .arg(target_width)
      .arg(target_height)
      .arg(offset_x)
      .arg(offset_y)
      .arg(output_index);
}

TimelineDocumentController::Clip TimelineDocumentController::buildGalleryClip(
    const QString& asset_id,
    const QString& file_path,
    const QString& display_label,
    double duration_seconds,
    double trim_start_seconds,
    double trim_end_seconds,
    const QString& poster_source) const {
  Clip clip;
  const auto normalized = normalized_path(file_path);
  if (!QFileInfo(normalized).exists()) {
    return clip;
  }

  const auto media_type = mediaTypeForFile(normalized);
  if (media_type.isEmpty()) {
    return clip;
  }

  clip.clip_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  clip.asset_id = asset_id.trimmed();
  clip.media_type = media_type;
  clip.file_path = normalized;
  clip.display_label = display_label.trimmed().isEmpty() ? QFileInfo(normalized).completeBaseName() : display_label.trimmed();
  if (!poster_source.trimmed().isEmpty()) {
    clip.poster_source = poster_source.trimmed();
  } else if (media_type == QStringLiteral("video")) {
    clip.poster_source = ensureVideoPoster(normalized);
  } else if (media_type == QStringLiteral("photo")) {
    clip.poster_source = QUrl::fromLocalFile(normalized).toString();
  }
  clip.linked_group_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (duration_seconds > 0.0) {
    clip.duration_seconds = duration_seconds;
  } else if (media_type == QStringLiteral("video")) {
    clip.duration_seconds = probeVideoDurationSeconds(normalized);
  } else if (media_type == QStringLiteral("audio")) {
    clip.duration_seconds = probeAudioDurationSeconds(normalized);
  } else {
    clip.duration_seconds = kDefaultStillDurationSeconds;
  }
  if (clip.duration_seconds <= 0.0) {
    return clip;
  }
  clip.trim_start_seconds = qBound(0.0, trim_start_seconds, qMax(0.0, clip.duration_seconds - 0.05));
  const auto requested_trim_end = trim_end_seconds > 0.0 ? trim_end_seconds : clip.duration_seconds;
  clip.trim_end_seconds = qBound(clip.trim_start_seconds + 0.05, requested_trim_end, clip.duration_seconds);
  clip.start_seconds = 0.0;
  clip.has_audio = media_type == QStringLiteral("video") ? probeVideoHasAudio(normalized) : media_type == QStringLiteral("audio");
  clip.waveform_source = clip.has_audio ? ensureAudioWaveform(normalized) : QString{};
  if (media_type == QStringLiteral("audio") && clip.poster_source.isEmpty()) {
    clip.poster_source = clip.waveform_source;
  }
  clip.frame_mode = media_type == QStringLiteral("photo") ? QStringLiteral("fit") : QStringLiteral("fit");
  clip.frame_scale = 1.0;
  clip.frame_offset_x = 0.0;
  clip.frame_offset_y = 0.0;
  clip.opacity = 1.0;
  clip.audio_gain_db = 0.0;
  clip.fade_in_seconds = 0.0;
  clip.fade_out_seconds = 0.0;
  clip.playback_rate = 1.0;
  clip.effect_stack.clear();
  clip.transition_in_name.clear();
  clip.transition_out_name.clear();
  clip.transition_in_seconds = 0.0;
  clip.transition_out_seconds = 0.0;
  return clip;
}

void TimelineDocumentController::shiftTrackClipsFrom(
    Track* track,
    double sequence_seconds,
    double delta_seconds,
    const QString& excluded_clip_id) {
  if (track == nullptr || qAbs(delta_seconds) <= kBoundaryEpsilon) {
    return;
  }

  for (auto& clip : track->clips) {
    if (!excluded_clip_id.isEmpty() && clip.clip_id == excluded_clip_id) {
      continue;
    }
    if (clip.start_seconds >= sequence_seconds - kBoundaryEpsilon) {
      clip.start_seconds = qMax(0.0, clip.start_seconds + delta_seconds);
    }
  }
  sort_track_clips(track);
}

void TimelineDocumentController::shiftAllTrackClipsFrom(
    double sequence_seconds,
    double delta_seconds,
    const QString& excluded_clip_id) {
  if (qAbs(delta_seconds) <= kBoundaryEpsilon) {
    return;
  }

  for (auto& track : tracks_) {
    shiftTrackClipsFrom(&track, sequence_seconds, delta_seconds, excluded_clip_id);
  }
}

void TimelineDocumentController::shiftMarkersFrom(double sequence_seconds, double delta_seconds) {
  if (qAbs(delta_seconds) <= kBoundaryEpsilon) {
    return;
  }

  for (auto& marker : markers_) {
    if (marker.seconds >= sequence_seconds - kBoundaryEpsilon) {
      marker.seconds = qMax(0.0, marker.seconds + delta_seconds);
    }
  }
  std::sort(
      markers_.begin(),
      markers_.end(),
      [](const auto& left, const auto& right) { return left.seconds < right.seconds; });
}

void TimelineDocumentController::splitTrackClipAtSequenceSeconds(Track* track, int clip_index, double sequence_seconds) {
  if (track == nullptr || clip_index < 0 || clip_index >= track->clips.size()) {
    return;
  }

  auto& clip = track->clips[clip_index];
  const auto split_local_seconds = clipLocalSecondsForSequenceTime(clip, sequence_seconds);
  if (split_local_seconds <= clip.trim_start_seconds + 0.05 ||
      split_local_seconds >= clip.trim_end_seconds - 0.05) {
    return;
  }

  Clip trailing = clip;
  trailing.clip_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  trailing.linked_group_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  trailing.trim_start_seconds = split_local_seconds;
  trailing.start_seconds = sequence_seconds;
  clip.trim_end_seconds = split_local_seconds;
  track->clips.push_back(trailing);
  sort_track_clips(track);
}

void TimelineDocumentController::overwriteTrackRange(Track* track, double range_start_seconds, double range_end_seconds) {
  if (track == nullptr || range_end_seconds <= range_start_seconds + kBoundaryEpsilon) {
    return;
  }

  QVector<Clip> next_clips;
  next_clips.reserve(track->clips.size() + 1);
  for (const auto& existing : track->clips) {
    const auto existing_end = clipEndSeconds(existing);
    if (existing_end <= range_start_seconds + kBoundaryEpsilon || existing.start_seconds >= range_end_seconds - kBoundaryEpsilon) {
      next_clips.push_back(existing);
      continue;
    }

    if (existing.start_seconds < range_start_seconds - kBoundaryEpsilon) {
      Clip leading = existing;
      leading.trim_end_seconds =
          leading.trim_start_seconds + qMax(0.05, range_start_seconds - existing.start_seconds);
      next_clips.push_back(leading);
    }

    if (existing_end > range_end_seconds + kBoundaryEpsilon) {
      Clip trailing = existing;
      trailing.clip_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      trailing.linked_group_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      trailing.trim_start_seconds =
          existing.trim_start_seconds + qMax(0.0, range_end_seconds - existing.start_seconds);
      trailing.start_seconds = range_end_seconds;
      next_clips.push_back(trailing);
    }
  }

  track->clips = next_clips;
  sort_track_clips(track);
}

bool TimelineDocumentController::persistDocument(const QString& document_path) const {
  auto clip_object_for = [](const Clip& clip) {
    QJsonObject clip_object;
    QJsonArray effect_array;
    for (const auto& effect_name : clip.effect_stack) {
      effect_array.push_back(effect_name);
    }
    clip_object.insert(QStringLiteral("clipId"), clip.clip_id);
    clip_object.insert(QStringLiteral("assetId"), clip.asset_id);
    clip_object.insert(QStringLiteral("mediaType"), clip.media_type);
    clip_object.insert(QStringLiteral("filePath"), clip.file_path);
    clip_object.insert(QStringLiteral("displayLabel"), clip.display_label);
    clip_object.insert(QStringLiteral("posterSource"), clip.poster_source);
    clip_object.insert(QStringLiteral("linkedGroupId"), clip.linked_group_id);
    clip_object.insert(QStringLiteral("durationSeconds"), clip.duration_seconds);
    clip_object.insert(QStringLiteral("trimStartSeconds"), clip.trim_start_seconds);
    clip_object.insert(QStringLiteral("trimEndSeconds"), clip.trim_end_seconds);
    clip_object.insert(QStringLiteral("startSeconds"), clip.start_seconds);
    clip_object.insert(QStringLiteral("hasAudio"), clip.has_audio);
    clip_object.insert(QStringLiteral("waveformSource"), clip.waveform_source);
    clip_object.insert(QStringLiteral("frameMode"), clip.frame_mode);
    clip_object.insert(QStringLiteral("frameScale"), clip.frame_scale);
    clip_object.insert(QStringLiteral("frameOffsetX"), clip.frame_offset_x);
    clip_object.insert(QStringLiteral("frameOffsetY"), clip.frame_offset_y);
    clip_object.insert(QStringLiteral("opacity"), clip.opacity);
    clip_object.insert(QStringLiteral("audioGainDb"), clip.audio_gain_db);
    clip_object.insert(QStringLiteral("fadeInSeconds"), clip.fade_in_seconds);
    clip_object.insert(QStringLiteral("fadeOutSeconds"), clip.fade_out_seconds);
    clip_object.insert(QStringLiteral("playbackRate"), clip.playback_rate);
    clip_object.insert(QStringLiteral("effectStack"), effect_array);
    clip_object.insert(QStringLiteral("transitionInName"), clip.transition_in_name);
    clip_object.insert(QStringLiteral("transitionOutName"), clip.transition_out_name);
    clip_object.insert(QStringLiteral("transitionInSeconds"), clip.transition_in_seconds);
    clip_object.insert(QStringLiteral("transitionOutSeconds"), clip.transition_out_seconds);
    return clip_object;
  };

  auto marker_object_for = [](const Marker& marker) {
    QJsonObject marker_object;
    marker_object.insert(QStringLiteral("markerId"), marker.marker_id);
    marker_object.insert(QStringLiteral("label"), marker.label);
    marker_object.insert(QStringLiteral("note"), marker.note);
    marker_object.insert(QStringLiteral("status"), marker.status);
    marker_object.insert(QStringLiteral("completed"), marker.completed);
    marker_object.insert(QStringLiteral("color"), marker.color);
    marker_object.insert(QStringLiteral("seconds"), marker.seconds);
    return marker_object;
  };

  QJsonArray legacy_clip_array;
  if (const auto* video_track = primaryVideoTrack(); video_track != nullptr) {
    for (const auto& clip : video_track->clips) {
      legacy_clip_array.push_back(clip_object_for(clip));
    }
  }

  QJsonArray track_array;
  for (const auto& track : tracks_) {
    QJsonArray clip_array;
    for (const auto& clip : track.clips) {
      clip_array.push_back(clip_object_for(clip));
    }

    QJsonObject track_object;
    track_object.insert(QStringLiteral("trackId"), track.track_id);
    track_object.insert(QStringLiteral("displayLabel"), track.display_label);
    track_object.insert(QStringLiteral("kind"), track.kind == TrackKind::Audio ? QStringLiteral("audio") : QStringLiteral("video"));
    track_object.insert(QStringLiteral("laneIndex"), track.lane_index);
    track_object.insert(QStringLiteral("locked"), track.locked);
    track_object.insert(QStringLiteral("muted"), track.muted);
    track_object.insert(QStringLiteral("solo"), track.solo);
    track_object.insert(QStringLiteral("visible"), track.visible);
    track_object.insert(QStringLiteral("targeted"), track.targeted);
    track_object.insert(QStringLiteral("clips"), clip_array);
    track_array.push_back(track_object);
  }

  QJsonArray marker_array;
  for (const auto& marker : markers_) {
    marker_array.push_back(marker_object_for(marker));
  }

  QJsonObject format_object;
  format_object.insert(QStringLiteral("width"), sequence_size_.width());
  format_object.insert(QStringLiteral("height"), sequence_size_.height());
  format_object.insert(QStringLiteral("frameRate"), framesPerSecond());
  format_object.insert(QStringLiteral("audioSampleRate"), audio_sample_rate_);

  QJsonObject root;
  root.insert(QStringLiteral("timelineId"), timeline_id_);
  root.insert(QStringLiteral("name"), timeline_name_);
  root.insert(QStringLiteral("createdAt"), created_at_);
  root.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  root.insert(QStringLiteral("renderPath"), saved_render_path_);
  root.insert(QStringLiteral("posterPath"), saved_poster_path_);
  root.insert(QStringLiteral("sequenceFormat"), format_object);
  root.insert(QStringLiteral("version"), 3);
  root.insert(QStringLiteral("activeTool"), active_tool_);
  root.insert(QStringLiteral("editMode"), edit_mode_);
  root.insert(QStringLiteral("linkedSelectionEnabled"), linked_selection_enabled_);
  root.insert(QStringLiteral("projectBinding"), project_binding_);
  root.insert(QStringLiteral("colorSpace"), color_space_);
  root.insert(QStringLiteral("dataLevels"), data_levels_);
  root.insert(QStringLiteral("exportContainer"), export_container_);
  root.insert(QStringLiteral("exportCodec"), export_codec_);
  root.insert(QStringLiteral("markers"), marker_array);
  root.insert(QStringLiteral("tracks"), track_array);
  root.insert(QStringLiteral("clips"), legacy_clip_array);

  QSaveFile file(document_path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return file.commit();
}

void TimelineDocumentController::initializeDefaultTracks() {
  tracks_.clear();
  appendTrackPair();
}

void TimelineDocumentController::appendTrackPair() {
  const auto lane_index = nextLaneIndex();

  Track video_track;
  video_track.track_id = QStringLiteral("video-%1").arg(lane_index + 1);
  video_track.display_label = QStringLiteral("V%1").arg(lane_index + 1);
  video_track.kind = TrackKind::Video;
  video_track.lane_index = lane_index;
  video_track.visible = true;
  video_track.targeted = lane_index == 0;
  tracks_.push_back(video_track);

  Track audio_track;
  audio_track.track_id = QStringLiteral("audio-%1").arg(lane_index + 1);
  audio_track.display_label = QStringLiteral("A%1").arg(lane_index + 1);
  audio_track.kind = TrackKind::Audio;
  audio_track.lane_index = lane_index;
  audio_track.visible = true;
  audio_track.targeted = lane_index == 0;
  tracks_.push_back(audio_track);
}

void TimelineDocumentController::syncAudioTracks() {
  QVector<Track> next_tracks;
  next_tracks.reserve(tracks_.size());

  QVector<Track> video_tracks;
  video_tracks.reserve(videoTrackCount());
  QHash<int, Track> audio_track_state;
  QHash<int, QVector<Clip>> standalone_audio_clips;
  for (const auto& track : tracks_) {
    if (track.kind == TrackKind::Audio) {
      audio_track_state.insert(track.lane_index, track);
      QVector<Clip> preserved_clips;
      for (const auto& clip : track.clips) {
        if (clip.media_type == QStringLiteral("audio")) {
          preserved_clips.push_back(clip);
        }
      }
      standalone_audio_clips.insert(track.lane_index, preserved_clips);
    }
  }
  for (const auto& track : tracks_) {
    if (track.kind == TrackKind::Video) {
      video_tracks.push_back(track);
    }
  }
  std::sort(
      video_tracks.begin(),
      video_tracks.end(),
      [](const auto& left, const auto& right) { return left.lane_index < right.lane_index; });

  // Pass 1: all video tracks grouped together
  for (auto& video_track : video_tracks) {
    sort_track_clips(&video_track);
    next_tracks.push_back(video_track);
  }

  // Pass 2: all audio tracks grouped below video tracks
  for (const auto& video_track : video_tracks) {
    Track audio_track;
    audio_track.kind = TrackKind::Audio;
    audio_track.lane_index = video_track.lane_index;
    audio_track.track_id = QStringLiteral("audio-%1").arg(video_track.lane_index + 1);
    audio_track.display_label = QStringLiteral("A%1").arg(video_track.lane_index + 1);
    if (audio_track_state.contains(video_track.lane_index)) {
      const auto existing_audio = audio_track_state.value(video_track.lane_index);
      audio_track.locked = existing_audio.locked;
      audio_track.muted = existing_audio.muted;
      audio_track.solo = existing_audio.solo;
      audio_track.visible = existing_audio.visible;
      audio_track.targeted = existing_audio.targeted;
    } else {
      audio_track.visible = video_track.visible;
      audio_track.targeted = video_track.targeted;
    }
    audio_track.clips.reserve(video_track.clips.size());
    if (standalone_audio_clips.contains(video_track.lane_index)) {
      audio_track.clips = standalone_audio_clips.value(video_track.lane_index);
    }
    for (const auto& clip : video_track.clips) {
      Clip audio_clip = clip;
      audio_clip.display_label = clip.has_audio
                                     ? QStringLiteral("%1 Audio").arg(clip.display_label)
                                     : QStringLiteral("%1 Silent").arg(clip.display_label);
      if (clip.media_type != QStringLiteral("audio")) {
        audio_track.clips.push_back(audio_clip);
      }
    }
    sort_track_clips(&audio_track);
    next_tracks.push_back(audio_track);
  }

  tracks_ = next_tracks;
}

void TimelineDocumentController::refreshSequenceSizeFromContent() {
  const auto ordered_refs = orderedVideoClipRefs();
  if (ordered_refs.isEmpty()) {
    sequence_size_ = QSize(kDefaultSequenceWidth, kDefaultSequenceHeight);
    return;
  }

  const auto* first_clip = clipAt(ordered_refs.first());
  if (first_clip == nullptr) {
    sequence_size_ = QSize(kDefaultSequenceWidth, kDefaultSequenceHeight);
    return;
  }

  const auto probed = probeVideoSize(first_clip->file_path);
  if (probed.isValid() && !probed.isEmpty()) {
    sequence_size_ = probed;
  }
}

void TimelineDocumentController::updateCurrentIndexFromSelection() {
  current_index_ = flattenedIndexForClipId(selected_clip_id_);
}

void TimelineDocumentController::ensureCurrentIndexValid() {
  updateCurrentIndexFromSelection();
  if (current_index_ >= 0) {
    return;
  }

  const auto refs = orderedVideoClipRefs();
  if (refs.isEmpty()) {
    current_index_ = -1;
    selected_clip_id_.clear();
    return;
  }

  const auto* clip = clipAt(refs.first());
  selected_clip_id_ = clip == nullptr ? QString{} : clip->clip_id;
  current_index_ = clip == nullptr ? -1 : 0;
}

void TimelineDocumentController::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }

  status_ = status;
  emit statusChanged();
}

void TimelineDocumentController::setDirty(bool dirty) {
  if (dirty_ == dirty) {
    return;
  }

  dirty_ = dirty;
  emit dirtyChanged();
}

void TimelineDocumentController::clampPlaybackState() {
  if (clipCount() == 0) {
    current_index_ = -1;
    selected_clip_id_.clear();
    sequence_playhead_seconds_ = 0.0;
    current_clip_playhead_seconds_ = 0.0;
    sequence_playing_ = false;
    return;
  }

  sequence_playhead_seconds_ = qBound(0.0, sequence_playhead_seconds_, totalDurationSeconds());
  updateCurrentIndexFromSelection();
  if (current_index_ < 0) {
    const auto active_ref = activeVideoClipRefForTime(sequence_playhead_seconds_);
    if (const auto* active_clip = clipAt(active_ref); active_clip != nullptr) {
      selected_clip_id_ = active_clip->clip_id;
      updateCurrentIndexFromSelection();
    }
  }

  const auto* clip = clipAt(current_index_);
  if (clip == nullptr) {
    current_clip_playhead_seconds_ = 0.0;
    sequence_playing_ = false;
    return;
  }

  current_clip_playhead_seconds_ =
      qBound(clip->trim_start_seconds, current_clip_playhead_seconds_, clip->trim_end_seconds);
  sequence_playhead_seconds_ =
      clip->start_seconds + qMax(0.0, current_clip_playhead_seconds_ - clip->trim_start_seconds);
  sequence_playhead_seconds_ = qBound(0.0, sequence_playhead_seconds_, totalDurationSeconds());
}

void TimelineDocumentController::emitAllState() {
  ensureCurrentIndexValid();
  clampPlaybackState();
  emit timelineStateChanged();
  emit clipsChanged();
  emit sequenceChanged();
  emit selectionChanged();
  emit timelineNameChanged();
  emit previewSourceChanged();
  emit savedPathsChanged();
  emit playbackStateChanged();
  emitEditorState();
  emitSourceMonitorState();
  emitMarkersState();
}

void TimelineDocumentController::emitEditorState() {
  emit editorStateChanged();
}

void TimelineDocumentController::emitSourceMonitorState() {
  emit sourceMonitorChanged();
}

void TimelineDocumentController::emitMarkersState() {
  emit markersChanged();
}

QStringList TimelineDocumentController::selectedClipIds() const {
  return selected_clip_ids_;
}

int TimelineDocumentController::selectedClipCount() const {
  return static_cast<int>(selected_clip_ids_.size());
}

bool TimelineDocumentController::isClipSelected(const QString& clip_id) const {
  if (selected_clip_ids_.isEmpty()) {
    return selected_clip_id_ == clip_id;
  }
  return selected_clip_ids_.contains(clip_id);
}

void TimelineDocumentController::toggleClipSelection(int index) {
  const auto ref = clipRefAt(index);
  const auto* clip = clipAt(ref);
  if (clip == nullptr) {
    return;
  }

  if (selected_clip_ids_.contains(clip->clip_id)) {
    selected_clip_ids_.removeAll(clip->clip_id);
    if (selected_clip_ids_.isEmpty()) {
      selected_clip_id_.clear();
      current_index_ = -1;
    } else {
      selected_clip_id_ = selected_clip_ids_.last();
      current_index_ = flattenedIndexForClipId(selected_clip_id_);
    }
  } else {
    selected_clip_ids_.append(clip->clip_id);
    selected_clip_id_ = clip->clip_id;
    current_index_ = index;
  }

  emit selectionChanged();
  emit clipsChanged();
  emit sequenceChanged();
}

void TimelineDocumentController::selectClipRange(int from_index, int to_index) {
  const int low = std::min(from_index, to_index);
  const int high = std::max(from_index, to_index);
  selected_clip_ids_.clear();

  const auto refs = orderedVideoClipRefs();
  for (int i = low; i <= high && i < refs.size(); ++i) {
    const auto* clip = clipAt(refs.at(i));
    if (clip != nullptr) {
      selected_clip_ids_.append(clip->clip_id);
    }
  }

  if (!selected_clip_ids_.isEmpty()) {
    selected_clip_id_ = selected_clip_ids_.last();
    current_index_ = to_index;
  }

  emit selectionChanged();
  emit clipsChanged();
  emit sequenceChanged();
}

void TimelineDocumentController::selectAllClips() {
  selected_clip_ids_.clear();
  const auto refs = orderedVideoClipRefs();
  for (const auto& ref : refs) {
    const auto* clip = clipAt(ref);
    if (clip != nullptr) {
      selected_clip_ids_.append(clip->clip_id);
    }
  }

  if (!selected_clip_ids_.isEmpty()) {
    selected_clip_id_ = selected_clip_ids_.first();
    current_index_ = 0;
  }

  emit selectionChanged();
  emit clipsChanged();
  emit sequenceChanged();
}

void TimelineDocumentController::deselectAllClips() {
  selected_clip_ids_.clear();
  selected_clip_id_.clear();
  current_index_ = -1;
  emit selectionChanged();
  emit clipsChanged();
  emit sequenceChanged();
}

void TimelineDocumentController::liftSelectedClips() {
  if (selected_clip_ids_.isEmpty()) {
    return;
  }
  pushUndoSnapshot();
  const auto removed_clip_count = selected_clip_ids_.size();

  for (auto& track : tracks_) {
    track.clips.erase(
        std::remove_if(track.clips.begin(), track.clips.end(),
                       [this](const Clip& clip) { return selected_clip_ids_.contains(clip.clip_id); }),
        track.clips.end());
  }

  selected_clip_ids_.clear();
  selected_clip_id_.clear();
  current_index_ = -1;
  syncAudioTracks();
  preview_source_.clear();
  current_clip_playhead_seconds_ = 0.0;
  sequence_playing_ = false;
  setDirty(true);
  emitAllState();
  setStatus(QStringLiteral("Lifted %1 selected clips from the sequence.").arg(removed_clip_count));
}

void TimelineDocumentController::extractSelectedClips() {
  if (selected_clip_ids_.isEmpty()) {
    return;
  }

  struct TimeRange final {
    double start = 0.0;
    double end = 0.0;
  };

  QVector<TimeRange> ranges;
  ranges.reserve(selected_clip_ids_.size());
  for (const auto& track : tracks_) {
    for (const auto& clip : track.clips) {
      if (!selected_clip_ids_.contains(clip.clip_id)) {
        continue;
      }
      ranges.push_back({clip.start_seconds, clipEndSeconds(clip)});
    }
  }
  if (ranges.isEmpty()) {
    return;
  }

  std::sort(
      ranges.begin(),
      ranges.end(),
      [](const auto& left, const auto& right) { return left.start < right.start; });

  QVector<TimeRange> merged_ranges;
  for (const auto& range : ranges) {
    if (merged_ranges.isEmpty() || range.start > merged_ranges.last().end + kBoundaryEpsilon) {
      merged_ranges.push_back(range);
      continue;
    }
    merged_ranges.last().end = qMax(merged_ranges.last().end, range.end);
  }

  for (const auto& track : tracks_) {
    for (const auto& clip : track.clips) {
      if (selected_clip_ids_.contains(clip.clip_id)) {
        continue;
      }
      const auto clip_start = clip.start_seconds;
      const auto clip_end = clipEndSeconds(clip);
      for (const auto& range : merged_ranges) {
        if (clip_end > range.start + kBoundaryEpsilon && clip_start < range.end - kBoundaryEpsilon) {
          setStatus(QStringLiteral("Extract supports multi-selection only when no unselected clips overlap the removed time ranges."));
          return;
        }
      }
    }
  }

  pushUndoSnapshot();

  const auto removed_clip_count = selected_clip_ids_.size();
  const auto first_range_start = merged_ranges.first().start;

  for (auto& track : tracks_) {
    track.clips.erase(
        std::remove_if(track.clips.begin(), track.clips.end(),
                       [this](const Clip& clip) { return selected_clip_ids_.contains(clip.clip_id); }),
        track.clips.end());
  }

  auto cumulative_shift = 0.0;
  for (const auto& range : merged_ranges) {
    const auto adjusted_end = range.end + cumulative_shift;
    const auto duration = qMax(0.0, range.end - range.start);
    if (duration <= kBoundaryEpsilon) {
      continue;
    }
    shiftAllTrackClipsFrom(adjusted_end, -duration);
    shiftMarkersFrom(adjusted_end, -duration);
    cumulative_shift -= duration;
  }

  const auto refs = orderedVideoClipRefs();
  selected_clip_ids_.clear();
  selected_clip_id_.clear();
  for (const auto& ref : refs) {
    const auto* candidate = clipAt(ref);
    if (candidate == nullptr) {
      continue;
    }
    if (candidate->start_seconds >= first_range_start - kBoundaryEpsilon) {
      selected_clip_id_ = candidate->clip_id;
      break;
    }
  }
  if (selected_clip_id_.isEmpty() && !refs.isEmpty()) {
    if (const auto* candidate = clipAt(refs.last()); candidate != nullptr) {
      selected_clip_id_ = candidate->clip_id;
    }
  }

  updateCurrentIndexFromSelection();
  sequence_playhead_seconds_ = qMin(qMax(0.0, first_range_start), totalDurationSeconds());
  if (const auto* current_clip = clipAt(selectedClipRef()); current_clip != nullptr) {
    preview_source_ = current_clip->poster_source;
    current_clip_playhead_seconds_ = qBound(
        current_clip->trim_start_seconds,
        current_clip->trim_start_seconds + qMax(0.0, sequence_playhead_seconds_ - current_clip->start_seconds),
        current_clip->trim_end_seconds);
  } else {
    preview_source_.clear();
    current_clip_playhead_seconds_ = 0.0;
  }
  sequence_playing_ = false;
  setDirty(true);
  emitAllState();
  setStatus(QStringLiteral("Extracted %1 selected clips and closed the gaps.").arg(removed_clip_count));
}

void TimelineDocumentController::removeSelectedClips() {
  liftSelectedClips();
}

bool TimelineDocumentController::canUndo() const {
  return !undo_stack_.isEmpty();
}

bool TimelineDocumentController::canRedo() const {
  return !redo_stack_.isEmpty();
}

void TimelineDocumentController::pushUndoSnapshot() {
  Snapshot snapshot;
  snapshot.tracks = tracks_;
  snapshot.markers = markers_;
  snapshot.selected_clip_id = selected_clip_id_;
  snapshot.selected_clip_ids = selected_clip_ids_;
  snapshot.sequence_playhead_seconds = sequence_playhead_seconds_;

  undo_stack_.append(std::move(snapshot));
  if (undo_stack_.size() > kMaxUndoDepth) {
    undo_stack_.removeFirst();
  }
  redo_stack_.clear();
  emit undoStateChanged();
}

void TimelineDocumentController::beginContinuousEdit() {
  if (!continuous_edit_active_) {
    pushUndoSnapshot();
    continuous_edit_active_ = true;
  }
}

void TimelineDocumentController::endContinuousEdit() {
  continuous_edit_active_ = false;
}

void TimelineDocumentController::undo() {
  if (undo_stack_.isEmpty()) {
    return;
  }

  Snapshot redo_snapshot;
  redo_snapshot.tracks = tracks_;
  redo_snapshot.markers = markers_;
  redo_snapshot.selected_clip_id = selected_clip_id_;
  redo_snapshot.selected_clip_ids = selected_clip_ids_;
  redo_snapshot.sequence_playhead_seconds = sequence_playhead_seconds_;
  redo_stack_.append(std::move(redo_snapshot));

  const auto snapshot = undo_stack_.takeLast();
  tracks_ = snapshot.tracks;
  markers_ = snapshot.markers;
  selected_clip_id_ = snapshot.selected_clip_id;
  selected_clip_ids_ = snapshot.selected_clip_ids;
  sequence_playhead_seconds_ = snapshot.sequence_playhead_seconds;

  updateCurrentIndexFromSelection();
  setDirty(true);
  emit clipsChanged();
  emit sequenceChanged();
  emit selectionChanged();
  emit playbackStateChanged();
  emit markersChanged();
  emit undoStateChanged();
}

void TimelineDocumentController::redo() {
  if (redo_stack_.isEmpty()) {
    return;
  }

  Snapshot undo_snapshot;
  undo_snapshot.tracks = tracks_;
  undo_snapshot.markers = markers_;
  undo_snapshot.selected_clip_id = selected_clip_id_;
  undo_snapshot.selected_clip_ids = selected_clip_ids_;
  undo_snapshot.sequence_playhead_seconds = sequence_playhead_seconds_;
  undo_stack_.append(std::move(undo_snapshot));

  const auto snapshot = redo_stack_.takeLast();
  tracks_ = snapshot.tracks;
  markers_ = snapshot.markers;
  selected_clip_id_ = snapshot.selected_clip_id;
  selected_clip_ids_ = snapshot.selected_clip_ids;
  sequence_playhead_seconds_ = snapshot.sequence_playhead_seconds;

  updateCurrentIndexFromSelection();
  setDirty(true);
  emit clipsChanged();
  emit sequenceChanged();
  emit selectionChanged();
  emit playbackStateChanged();
  emit markersChanged();
  emit undoStateChanged();
}

}  // namespace photo_editor::desktop

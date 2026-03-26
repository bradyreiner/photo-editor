#include "timeline_document_controller.h"

#include "timeline_asset_store.h"

#include <algorithm>
#include <limits>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
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

constexpr double kFramesPerSecond = 30.0;
constexpr int kDefaultSequenceWidth = 1080;
constexpr int kDefaultSequenceHeight = 1350;
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

QString normalized_path(const QString& path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString poster_cache_root() {
  auto cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cache_root.isEmpty()) {
    cache_root = QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-cache"));
  }
  return QDir(cache_root).filePath(QStringLiteral("timeline-posters"));
}

QString waveform_cache_root() {
  auto cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cache_root.isEmpty()) {
    cache_root = QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-cache"));
  }
  return QDir(cache_root).filePath(QStringLiteral("timeline-waveforms"));
}

double clamp_non_negative(double value) {
  return qMax(0.0, value);
}

QString default_timeline_name(const QString& requested_name) {
  const auto trimmed = requested_name.simplified();
  return trimmed.isEmpty() ? QStringLiteral("Untitled Timeline") : trimmed;
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
  createTimeline(QStringLiteral("Untitled Timeline"));
  setDirty(false);
  setStatus(QStringLiteral("Timeline ready. Select a video in Library and add it here."));
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
  return kFramesPerSecond;
}

QString TimelineDocumentController::sequenceFormatLabel() const {
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
  return clip == nullptr ? QString{} : QUrl::fromLocalFile(clip->file_path).toString();
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

void TimelineDocumentController::setTimelineName(const QString& timeline_name) {
  const auto normalized = default_timeline_name(timeline_name);
  if (timeline_name_ == normalized) {
    return;
  }

  timeline_name_ = normalized;
  emit timelineNameChanged();
  setDirty(true);
}

void TimelineDocumentController::createTimeline(const QString& timeline_name) {
  initializeDefaultTracks();
  timeline_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  timeline_name_ = default_timeline_name(timeline_name);
  created_at_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  selected_clip_id_.clear();
  preview_source_.clear();
  saved_document_path_.clear();
  saved_render_path_.clear();
  saved_poster_path_.clear();
  sequence_size_ = QSize(kDefaultSequenceWidth, kDefaultSequenceHeight);
  current_index_ = -1;
  sequence_playhead_seconds_ = 0.0;
  current_clip_playhead_seconds_ = 0.0;
  sequence_playing_ = false;
  emitAllState();
  setDirty(false);
  setStatus(QStringLiteral("New timeline ready. Add videos from the gallery."));
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

        if (clip.duration_seconds <= 0.0 && !clip.file_path.isEmpty()) {
          clip.duration_seconds = probeVideoDurationSeconds(clip.file_path);
        }
        if (clip.trim_end_seconds <= 0.0) {
          clip.trim_end_seconds = clip.duration_seconds;
        }
        if (clip.poster_source.isEmpty() && !clip.file_path.isEmpty()) {
          clip.poster_source = ensureVideoPoster(clip.file_path);
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
      clip.file_path = clip_object.value(QStringLiteral("filePath")).toString().trimmed();
      clip.display_label = clip_object.value(QStringLiteral("displayLabel")).toString().trimmed();
      clip.poster_source = clip_object.value(QStringLiteral("posterSource")).toString().trimmed();
      clip.linked_group_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      clip.duration_seconds = clip_object.value(QStringLiteral("durationSeconds")).toDouble();
      clip.trim_start_seconds = clamp_non_negative(clip_object.value(QStringLiteral("trimStartSeconds")).toDouble());
      clip.trim_end_seconds = clamp_non_negative(
          clip_object.value(QStringLiteral("trimEndSeconds")).toDouble(clip.duration_seconds));
      clip.start_seconds = sequential_start;
      clip.has_audio = !clip.file_path.isEmpty() && probeVideoHasAudio(clip.file_path);
      clip.frame_mode = QStringLiteral("fit");
      clip.frame_scale = 1.0;
      clip.frame_offset_x = 0.0;
      clip.frame_offset_y = 0.0;

      if (clip.poster_source.isEmpty() && !clip.file_path.isEmpty()) {
        clip.poster_source = ensureVideoPoster(clip.file_path);
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

  timeline_id_ = timeline_id;
  timeline_name_ = default_timeline_name(object.value(QStringLiteral("name")).toString());
  created_at_ = object.value(QStringLiteral("createdAt")).toString().trimmed();
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
  if (clipCount() == 0) {
    setStatus(QStringLiteral("Add at least one gallery video before saving a timeline."));
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
  saved_render_path_ = QDir(timelineRendersRootPath()).filePath(QStringLiteral("%1.mp4").arg(timeline_id_));
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
  const auto ref = clipRefAt(index);
  const auto* clip = clipAt(ref);
  if (clip == nullptr) {
    return;
  }

  selected_clip_id_ = clip->clip_id;
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
  auto* target_track = defaultTargetVideoTrack();
  if (target_track == nullptr) {
    setStatus(QStringLiteral("The sequence tracks are unavailable."));
    return;
  }

  auto clip = buildGalleryClip(asset_id, file_path, display_label, duration_seconds, 0.0, 0.0, poster_source);
  if (clip.duration_seconds <= 0.0) {
    setStatus(QStringLiteral("Unable to read the selected video's duration."));
    return;
  }

  clip.start_seconds = qMax(sequence_playhead_seconds_, trackEndSeconds(*target_track));
  target_track->clips.push_back(clip);
  sort_track_clips(target_track);
  syncAudioTracks();
  refreshSequenceSizeFromContent();
  selected_clip_id_ = clip.clip_id;
  updateCurrentIndexFromSelection();
  preview_source_ = clip.poster_source;
  current_clip_playhead_seconds_ = clip.trim_start_seconds;
  sequence_playhead_seconds_ = clip.start_seconds;
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
  auto* target_track = defaultTargetVideoTrack();
  if (target_track == nullptr) {
    setStatus(QStringLiteral("The sequence tracks are unavailable."));
    return;
  }

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

  const auto insert_start = qMax(0.0, sequence_playhead_seconds_);
  for (int index = 0; index < target_track->clips.size(); ++index) {
    const auto existing_end = clipEndSeconds(target_track->clips.at(index));
    if (target_track->clips.at(index).start_seconds < insert_start && existing_end > insert_start + kBoundaryEpsilon) {
      splitTrackClipAtSequenceSeconds(target_track, index, insert_start);
      break;
    }
  }
  shiftTrackClipsFrom(target_track, insert_start, insert_duration);
  clip.start_seconds = insert_start;
  target_track->clips.push_back(clip);
  sort_track_clips(target_track);
  syncAudioTracks();
  refreshSequenceSizeFromContent();
  selected_clip_id_ = clip.clip_id;
  updateCurrentIndexFromSelection();
  preview_source_ = clip.poster_source;
  current_clip_playhead_seconds_ = clip.trim_start_seconds;
  sequence_playhead_seconds_ = clip.start_seconds;
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
  auto* target_track = defaultTargetVideoTrack();
  if (target_track == nullptr) {
    setStatus(QStringLiteral("The sequence tracks are unavailable."));
    return;
  }

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

  const auto overwrite_start = qMax(0.0, sequence_playhead_seconds_);
  const auto overwrite_end = overwrite_start + overwrite_duration;
  overwriteTrackRange(target_track, overwrite_start, overwrite_end);
  clip.start_seconds = overwrite_start;
  target_track->clips.push_back(clip);
  sort_track_clips(target_track);
  syncAudioTracks();
  refreshSequenceSizeFromContent();
  selected_clip_id_ = clip.clip_id;
  updateCurrentIndexFromSelection();
  preview_source_ = clip.poster_source;
  current_clip_playhead_seconds_ = clip.trim_start_seconds;
  sequence_playhead_seconds_ = clip.start_seconds;
  sequence_playing_ = false;
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Overwrote %1 at %2.").arg(target_track->display_label, formatDurationText(overwrite_start)));
}

void TimelineDocumentController::addVideoTrack() {
  appendTrackPair();
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Added V%1 / A%1 to the sequence.").arg(videoTrackCount()));
}

void TimelineDocumentController::moveSelectedClipToTrackRow(int track_row_index, double start_seconds) {
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

void TimelineDocumentController::removeSelectedClip() {
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
  setStatus(QStringLiteral("Removed the selected clip from the sequence."));
}

void TimelineDocumentController::moveSelectedBackward() {
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

  clip->start_seconds = bounded_start;
  clip->trim_start_seconds = qBound(
      0.0,
      clip->trim_start_seconds + delta,
      qMax(0.0, clip->trim_end_seconds - kMinimumClipLength));
  Q_UNUSED(ripple);
  sort_track_clips(track);
  syncAudioTracks();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
}

void TimelineDocumentController::trimSelectedClipEndToSequenceSeconds(double seconds, bool ripple) {
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
      shiftTrackClipsFrom(track, previous_end_seconds, delta_seconds, clip->clip_id);
    }
  }
  syncAudioTracks();
  clampPlaybackState();
  emitAllState();
  setDirty(true);
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

double TimelineDocumentController::sourceDurationSecondsForFile(const QString& file_path) const {
  const auto normalized = normalized_path(file_path);
  return isVideoFile(normalized) && QFileInfo(normalized).exists() ? probeVideoDurationSeconds(normalized) : 0.0;
}

bool TimelineDocumentController::splitSelectedClip(double split_seconds) {
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
    map.insert(QStringLiteral("selected"), index == current_index_);
    map.insert(QStringLiteral("displayLabel"), QString{});
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
    return map;
  }

  return clipMap(*clip, index, *track);
}

QVariantMap TimelineDocumentController::clipMap(const Clip& clip, int flattened_index, const Track& track) const {
  const auto trimmed_duration = clipTrimmedDuration(clip);
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
  map.insert(QStringLiteral("selected"), selected_clip_id_ == clip.clip_id);
  map.insert(QStringLiteral("assetId"), clip.asset_id);
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
  map.insert(QStringLiteral("frameMode"), clip.frame_mode);
  map.insert(QStringLiteral("frameScale"), clip.frame_scale);
  map.insert(QStringLiteral("frameOffsetX"), clip.frame_offset_x);
  map.insert(QStringLiteral("frameOffsetY"), clip.frame_offset_y);
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
  map.insert(QStringLiteral("clips"), clip_values);
  return map;
}

bool TimelineDocumentController::isVideoFile(const QString& file_path) const {
  return video_extensions().contains(QFileInfo(file_path).suffix().toLower());
}

QVector<TimelineDocumentController::ClipRef> TimelineDocumentController::orderedVideoClipRefs() const {
  QVector<ClipRef> refs;
  for (int track_index = 0; track_index < tracks_.size(); ++track_index) {
    const auto& track = tracks_.at(track_index);
    if (track.kind != TrackKind::Video) {
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
    if (track.kind != TrackKind::Video || track.muted) {
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
    if (track.kind != TrackKind::Video) {
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
                << QStringLiteral("-i") << QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000");
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
    arguments << QStringLiteral("-ss") << QString::number(local_start, 'f', 3)
              << QStringLiteral("-t") << QString::number(segment.duration, 'f', 3)
              << QStringLiteral("-i") << clip->file_path;
    const auto clip_input_index = input_index++;

    filter_segments.push_back(clipVideoFilter(*clip, clip_input_index, output_segment_index, target_size));

    if (clip->has_audio) {
      filter_segments.push_back(
          QStringLiteral("[%1:a]aresample=48000,aformat=sample_rates=48000:channel_layouts=stereo,asetpts=PTS-STARTPTS[a%2]")
              .arg(clip_input_index)
              .arg(output_segment_index));
    } else {
      arguments << QStringLiteral("-f") << QStringLiteral("lavfi")
                << QStringLiteral("-t") << QString::number(segment.duration, 'f', 3)
                << QStringLiteral("-i") << QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000");
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
            << QStringLiteral("-c:v") << QStringLiteral("libx264")
            << QStringLiteral("-c:a") << QStringLiteral("aac")
            << QStringLiteral("-b:a") << QStringLiteral("192k")
            << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
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
  if (!isVideoFile(normalized) || !QFileInfo(normalized).exists()) {
    return clip;
  }

  clip.clip_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  clip.asset_id = asset_id.trimmed();
  clip.file_path = normalized;
  clip.display_label = display_label.trimmed().isEmpty() ? QFileInfo(normalized).completeBaseName() : display_label.trimmed();
  clip.poster_source = poster_source.trimmed().isEmpty() ? ensureVideoPoster(normalized) : poster_source.trimmed();
  clip.linked_group_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  clip.duration_seconds = duration_seconds > 0.0 ? duration_seconds : probeVideoDurationSeconds(normalized);
  if (clip.duration_seconds <= 0.0) {
    return clip;
  }
  clip.trim_start_seconds = qBound(0.0, trim_start_seconds, qMax(0.0, clip.duration_seconds - 0.05));
  const auto requested_trim_end = trim_end_seconds > 0.0 ? trim_end_seconds : clip.duration_seconds;
  clip.trim_end_seconds = qBound(clip.trim_start_seconds + 0.05, requested_trim_end, clip.duration_seconds);
  clip.start_seconds = 0.0;
  clip.has_audio = probeVideoHasAudio(normalized);
  clip.waveform_source = clip.has_audio ? ensureAudioWaveform(normalized) : QString{};
  clip.frame_mode = QStringLiteral("fit");
  clip.frame_scale = 1.0;
  clip.frame_offset_x = 0.0;
  clip.frame_offset_y = 0.0;
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
    clip_object.insert(QStringLiteral("clipId"), clip.clip_id);
    clip_object.insert(QStringLiteral("assetId"), clip.asset_id);
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
    return clip_object;
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
    track_object.insert(QStringLiteral("clips"), clip_array);
    track_array.push_back(track_object);
  }

  QJsonObject format_object;
  format_object.insert(QStringLiteral("width"), sequence_size_.width());
  format_object.insert(QStringLiteral("height"), sequence_size_.height());
  format_object.insert(QStringLiteral("frameRate"), framesPerSecond());

  QJsonObject root;
  root.insert(QStringLiteral("timelineId"), timeline_id_);
  root.insert(QStringLiteral("name"), timeline_name_);
  root.insert(QStringLiteral("createdAt"), created_at_);
  root.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  root.insert(QStringLiteral("renderPath"), saved_render_path_);
  root.insert(QStringLiteral("posterPath"), saved_poster_path_);
  root.insert(QStringLiteral("sequenceFormat"), format_object);
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
  tracks_.push_back(video_track);

  Track audio_track;
  audio_track.track_id = QStringLiteral("audio-%1").arg(lane_index + 1);
  audio_track.display_label = QStringLiteral("A%1").arg(lane_index + 1);
  audio_track.kind = TrackKind::Audio;
  audio_track.lane_index = lane_index;
  tracks_.push_back(audio_track);
}

void TimelineDocumentController::syncAudioTracks() {
  QVector<Track> next_tracks;
  next_tracks.reserve(tracks_.size());

  QVector<Track> video_tracks;
  video_tracks.reserve(videoTrackCount());
  for (const auto& track : tracks_) {
    if (track.kind == TrackKind::Video) {
      video_tracks.push_back(track);
    }
  }
  std::sort(
      video_tracks.begin(),
      video_tracks.end(),
      [](const auto& left, const auto& right) { return left.lane_index < right.lane_index; });

  for (auto& video_track : video_tracks) {
    sort_track_clips(&video_track);
    next_tracks.push_back(video_track);

    Track audio_track;
    audio_track.kind = TrackKind::Audio;
    audio_track.lane_index = video_track.lane_index;
    audio_track.track_id = QStringLiteral("audio-%1").arg(video_track.lane_index + 1);
    audio_track.display_label = QStringLiteral("A%1").arg(video_track.lane_index + 1);
    audio_track.clips.reserve(video_track.clips.size());
    for (const auto& clip : video_track.clips) {
      Clip audio_clip = clip;
      audio_clip.display_label = clip.has_audio
                                     ? QStringLiteral("%1 Audio").arg(clip.display_label)
                                     : QStringLiteral("%1 Silent").arg(clip.display_label);
      audio_track.clips.push_back(audio_clip);
    }
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
  emit clipsChanged();
  emit sequenceChanged();
  emit selectionChanged();
  emit timelineNameChanged();
  emit previewSourceChanged();
  emit savedPathsChanged();
  emit playbackStateChanged();
}

}  // namespace photo_editor::desktop

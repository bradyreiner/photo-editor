#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "job_runner.h"
#include "job_store.h"
#include "timeline_asset_store.h"
#include "timeline_document_controller.h"

namespace {

using photo_editor::desktop::loadTimelineAssetRecords;
using photo_editor::desktop::loadPersistedJobRecords;
using photo_editor::desktop::TimelineDocumentController;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool nearly_equal(double left, double right, double tolerance = 0.0001) {
  return std::abs(left - right) <= tolerance;
}

bool wait_for_persisted_job(const QString& job_id, int timeout_ms = 120000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    for (const auto& job : loadPersistedJobRecords()) {
      if (job.job_id != job_id) {
        continue;
      }
      if (job.status == QStringLiteral("Completed") || job.status == QStringLiteral("Failed")) {
        return true;
      }
    }
  }
  return false;
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

bool run_process(const QString& program, const QStringList& arguments, QString* error_output = nullptr) {
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_output != nullptr) {
      *error_output = QString::fromUtf8(process.readAllStandardError()).trimmed();
    }
    return false;
  }
  return true;
}

QString process_stdout(const QString& program, const QStringList& arguments, QString* error_output = nullptr) {
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_output != nullptr) {
      *error_output = QString::fromUtf8(process.readAllStandardError()).trimmed();
    }
    return {};
  }

  return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QString create_test_video(
    const QString& ffmpeg,
    const QString& output_path,
    const QString& color_name,
    bool with_audio) {
  QStringList arguments = {
      QStringLiteral("-y"),
      QStringLiteral("-f"),
      QStringLiteral("lavfi"),
      QStringLiteral("-i"),
      QStringLiteral("color=c=%1:s=320x240:d=1.2:r=30").arg(color_name),
  };

  if (with_audio) {
    arguments << QStringLiteral("-f")
              << QStringLiteral("lavfi")
              << QStringLiteral("-i")
              << QStringLiteral("sine=frequency=660:duration=1.2");
  }

  arguments << QStringLiteral("-shortest")
            << QStringLiteral("-c:v")
            << QStringLiteral("libx264")
            << QStringLiteral("-pix_fmt")
            << QStringLiteral("yuv420p");

  if (with_audio) {
    arguments << QStringLiteral("-c:a")
              << QStringLiteral("aac");
  } else {
    arguments << QStringLiteral("-an");
  }

  arguments << output_path;

  QString error_output;
  if (!run_process(ffmpeg, arguments, &error_output)) {
    std::cerr << "ffmpeg fixture generation failed: " << error_output.toStdString() << '\n';
    return {};
  }

  return QFileInfo::exists(output_path) ? output_path : QString{};
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("temporary workspace should be created");
  }

  qputenv("HOME", workspace.path().toUtf8());
  qputenv("XDG_DATA_HOME", workspace.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", workspace.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", workspace.filePath(QStringLiteral("app-data")).toUtf8());

  QApplication app(argc, argv);
  QStandardPaths::setTestModeEnabled(true);

  const auto first_video = touch_file(workspace.filePath(QStringLiteral("clip-01.mp4")));
  const auto second_video = touch_file(workspace.filePath(QStringLiteral("clip-02.mov")));
  const auto photo_still = touch_file(workspace.filePath(QStringLiteral("still-01.jpg")));
  const auto audio_clip = touch_file(workspace.filePath(QStringLiteral("audio-01.wav")));
  if (first_video.isEmpty() || second_video.isEmpty()) {
    return fail("video placeholders should be created");
  }

  TimelineDocumentController controller;
  if (controller.hasActiveTimeline()) {
    return fail("timeline controller should start without an implicit active timeline");
  }
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Gallery Timeline")},
      {QStringLiteral("width"), 1920},
      {QStringLiteral("height"), 1080},
      {QStringLiteral("frameRate"), 24.0},
      {QStringLiteral("audioSampleRate"), 44100},
      {QStringLiteral("projectBinding"), QStringLiteral("Campaign")},
      {QStringLiteral("colorSpace"), QStringLiteral("Display P3")},
      {QStringLiteral("dataLevels"), QStringLiteral("Full")},
      {QStringLiteral("exportContainer"), QStringLiteral("mov")},
      {QStringLiteral("exportCodec"), QStringLiteral("prores")},
  });
  if (!controller.hasActiveTimeline() ||
      controller.sequenceWidth() != 1920 ||
      controller.sequenceHeight() != 1080 ||
      !nearly_equal(controller.framesPerSecond(), 24.0) ||
      controller.audioSampleRate() != 44100 ||
      controller.projectBinding() != QStringLiteral("Campaign") ||
      controller.colorSpace() != QStringLiteral("Display P3") ||
      controller.dataLevels() != QStringLiteral("Full") ||
      controller.exportContainer() != QStringLiteral("mov") ||
      controller.exportCodec() != QStringLiteral("prores")) {
    return fail("explicit timeline setup should persist professional sequence settings");
  }
  controller.addClipFromGallery(QStringLiteral("video:1"), first_video, QStringLiteral("Clip One"), 12.0, QStringLiteral("poster://one"));
  controller.addClipFromGallery(QStringLiteral("video:2"), second_video, QStringLiteral("Clip Two"), 8.0, QStringLiteral("poster://two"));

  if (controller.clipCount() != 2) {
    return fail("adding gallery videos should append clips");
  }
  if (controller.trackCount() != 2 || controller.videoTrackCount() != 1 || controller.audioTrackCount() != 1) {
    return fail("timeline documents should expose dedicated video and audio tracks");
  }

  controller.selectClip(0);
  controller.setSelectedTrimStartSeconds(2.0);
  controller.setSelectedTrimEndSeconds(9.5);
  const auto first_clip = controller.currentClip();
  if (first_clip.value(QStringLiteral("trimStartSeconds")).toDouble() != 2.0 ||
      first_clip.value(QStringLiteral("trimEndSeconds")).toDouble() != 9.5) {
    return fail("trim updates should persist on the selected clip");
  }

  controller.trimSelectedClipStartToSequenceSeconds(1.0);
  if (!nearly_equal(controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), 1.0) ||
      !nearly_equal(controller.currentClip().value(QStringLiteral("trimStartSeconds")).toDouble(), 3.0)) {
    return fail("timeline start-edge trims should move the clip start while updating trim in");
  }

  controller.trimSelectedClipEndToSequenceSeconds(6.5);
  if (!nearly_equal(controller.currentClip().value(QStringLiteral("trimEndSeconds")).toDouble(), 8.5) ||
      !nearly_equal(controller.currentClip().value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 5.5)) {
    return fail("timeline end-edge trims should update the clip out point from sequence time");
  }

  if (!controller.splitSelectedClip(5.0)) {
    return fail("splitting the selected clip should succeed inside the trim range");
  }

  if (controller.clipCount() != 3) {
    return fail("splitting should insert a second clip segment into the sequence");
  }

  const auto sequence_tracks = controller.sequenceTracks();
  if (sequence_tracks.size() != 2 ||
      sequence_tracks.at(0).toMap().value(QStringLiteral("clipCount")).toInt() != 3 ||
      sequence_tracks.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 3) {
    return fail("splitting should keep the derived audio lane aligned with the video track");
  }

  const auto clips_after_split = controller.clips();
  if (!nearly_equal(clips_after_split.at(0).toMap().value(QStringLiteral("trimEndSeconds")).toDouble(), 5.0) ||
      !nearly_equal(clips_after_split.at(1).toMap().value(QStringLiteral("trimStartSeconds")).toDouble(), 5.0) ||
      !nearly_equal(clips_after_split.at(1).toMap().value(QStringLiteral("trimEndSeconds")).toDouble(), 8.5)) {
    return fail("splitting should preserve both halves of the trimmed clip exactly");
  }

  controller.seekSequenceSeconds(6.0);
  if (controller.currentIndex() != 1 ||
      !nearly_equal(controller.currentClipPlayheadSeconds(), 8.0) ||
      !nearly_equal(controller.sequencePlayheadSeconds(), 6.0)) {
    return fail("native sequence seeking should map sequence time onto the correct split clip");
  }

  controller.playSequence();
  if (!controller.sequencePlaying()) {
    return fail("playSequence should mark the timeline as playing");
  }

  controller.advancePlaybackAfterClipEnd();
  if (controller.currentIndex() != 2 ||
      !nearly_equal(controller.currentClipPlayheadSeconds(), 0.0) ||
      !nearly_equal(controller.sequencePlayheadSeconds(), 12.0)) {
    return fail("advancing after a clip end should move playback to the next active clip start when gaps remain on the timeline");
  }

  controller.stepSequenceFrames(1);
  if (controller.sequencePlaying()) {
    return fail("frame stepping should pause sequence playback");
  }
  if (controller.currentClipPlayheadSeconds() <= 0.0) {
    return fail("frame stepping should advance the native clip playhead");
  }

  controller.selectClip(1);
  controller.moveSelectedForward();
  if (controller.currentIndex() != 2) {
    return fail("moving a clip forward should update the selection index");
  }

  const auto clips = controller.clips();
  if (clips.at(2).toMap().value(QStringLiteral("displayLabel")).toString() != QStringLiteral("Clip One")) {
    return fail("moving a clip forward should reorder the timeline");
  }

  controller.removeSelectedClip();
  if (controller.clipCount() != 2) {
    return fail("removing the selected clip should shrink the sequence");
  }

  if (!controller.dirty()) {
    return fail("timeline edits should mark the document dirty");
  }

  TimelineDocumentController layered_controller;
  layered_controller.createTimeline(QStringLiteral("Layered Timeline"));
  layered_controller.addClipFromGallery(QStringLiteral("video:bg"), first_video, QStringLiteral("Background"), 4.0, QStringLiteral("poster://bg"));
  layered_controller.addClipFromGallery(QStringLiteral("video:fg"), second_video, QStringLiteral("Foreground"), 5.0, QStringLiteral("poster://fg"));
  layered_controller.addVideoTrack();
  layered_controller.selectClip(1);
  layered_controller.moveSelectedClipToTrackRow(1, 1.0);
  if (layered_controller.trackCount() != 4 || layered_controller.videoTrackCount() != 2 || layered_controller.audioTrackCount() != 2) {
    return fail("adding a video track should create a paired video and audio lane");
  }
  const auto layered_tracks = layered_controller.sequenceTracks();
  if (layered_tracks.size() != 4 ||
      layered_tracks.at(0).toMap().value(QStringLiteral("clipCount")).toInt() != 1 ||
      layered_tracks.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 1) {
    return fail("moving a clip to a new video lane should preserve clip counts across paired tracks");
  }
  layered_controller.seekSequenceSeconds(1.5);
  if (layered_controller.currentClip().value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V2") ||
      layered_controller.currentClip().value(QStringLiteral("displayLabel")).toString() != QStringLiteral("Foreground") ||
      !nearly_equal(layered_controller.currentClipPlayheadSeconds(), 0.5)) {
    return fail("sequence seeking should resolve the topmost active clip after moving it to a higher track");
  }
  layered_controller.toggleTrackTargeted(1);
  layered_controller.toggleTrackLocked(1);
  layered_controller.toggleTrackVisibility(1);
  const auto targeted_track_state = layered_controller.sequenceTracks().at(1).toMap();
  if (!targeted_track_state.value(QStringLiteral("targeted")).toBool() ||
      !targeted_track_state.value(QStringLiteral("locked")).toBool() ||
      targeted_track_state.value(QStringLiteral("visible")).toBool()) {
    return fail("track state toggles should persist on the selected lane");
  }
  layered_controller.toggleTrackVisibility(1);
  layered_controller.toggleTrackMuted(3);
  layered_controller.seekSequenceSeconds(1.5);
  if (layered_controller.currentClip().value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V2") ||
      layered_controller.currentClip().value(QStringLiteral("audioPlaybackEnabled")).toBool()) {
    return fail("audio mute should silence playback without hiding the visible video clip");
  }

  TimelineDocumentController source_controller;
  source_controller.createTimeline(QStringLiteral("Source Timeline"));
  source_controller.loadSourceClipFromGallery(
      QStringLiteral("video:src"),
      first_video,
      QStringLiteral("Source Clip"),
      5.0,
      QStringLiteral("poster://src"));
  source_controller.setSourceInSeconds(1.0);
  source_controller.setSourceOutSeconds(3.0);
  source_controller.appendSourceClipToTimeline();
  source_controller.seekSequenceSeconds(0.75);
  const auto beat_marker_id = source_controller.addSequenceMarker(QStringLiteral("Beat"));
  source_controller.setSequenceMarkerNote(beat_marker_id, QStringLiteral("Review this cut"));
  source_controller.setSequenceMarkerStatus(beat_marker_id, QStringLiteral("review"));
  source_controller.setSequenceMarkerCompleted(beat_marker_id, true);
  source_controller.seekSequenceSeconds(1.5);
  source_controller.addSequenceMarker(QStringLiteral("Outro"));
  if (source_controller.clipCount() != 1 ||
      !nearly_equal(source_controller.currentClip().value(QStringLiteral("trimStartSeconds")).toDouble(), 1.0) ||
      !nearly_equal(source_controller.currentClip().value(QStringLiteral("trimEndSeconds")).toDouble(), 3.0) ||
      source_controller.sequenceMarkers().size() != 2) {
    return fail("source monitor edits and sequence markers should persist in controller state");
  }
  const auto source_markers = source_controller.sequenceMarkers();
  if (source_markers.at(0).toMap().value(QStringLiteral("note")).toString() != QStringLiteral("Review this cut") ||
      source_markers.at(0).toMap().value(QStringLiteral("status")).toString() != QStringLiteral("review") ||
      !source_markers.at(0).toMap().value(QStringLiteral("completed")).toBool()) {
    return fail("marker notes, status, and completion state should persist in controller state");
  }
  source_controller.seekSequenceSeconds(0.0);
  if (!source_controller.seekNextSequenceMarker() ||
      !nearly_equal(source_controller.sequencePlayheadSeconds(), 0.75)) {
    return fail("next marker navigation should seek to the next marker after the current playhead");
  }
  source_controller.seekSequenceSeconds(1.6);
  if (!source_controller.seekPreviousSequenceMarker() ||
      !nearly_equal(source_controller.sequencePlayheadSeconds(), 1.5)) {
    return fail("previous marker navigation should seek to the prior marker before the current playhead");
  }
  source_controller.seekSequenceSeconds(0.1);
  if (!source_controller.seekNextEditPoint() ||
      !nearly_equal(source_controller.sequencePlayheadSeconds(), 2.0)) {
    return fail("next edit navigation should seek to the next clip boundary in sequence time");
  }
  source_controller.seekSequenceSeconds(1.9);
  if (!source_controller.seekPreviousEditPoint() ||
      !nearly_equal(source_controller.sequencePlayheadSeconds(), 0.0)) {
    return fail("previous edit navigation should seek to the prior clip boundary in sequence time");
  }

  TimelineDocumentController photo_controller;
  photo_controller.createTimeline(QStringLiteral("Photo Timeline"));
  photo_controller.addClipFromGallery(
      QStringLiteral("photo:1"),
      photo_still,
      QStringLiteral("Still"),
      0.0,
      QStringLiteral("file:///poster.jpg"));
  if (photo_controller.clipCount() != 1 ||
      photo_controller.currentClip().value(QStringLiteral("mediaType")).toString() != QStringLiteral("photo") ||
      !photo_controller.playbackSourceUrl().isEmpty()) {
    return fail("photo clips should be insertable onto video tracks without requiring video playback");
  }

  TimelineDocumentController audio_controller;
  audio_controller.createTimeline(QStringLiteral("Audio Timeline"));
  audio_controller.loadSourceClipFromGallery(
      QStringLiteral("audio:1"),
      audio_clip,
      QStringLiteral("Narration"),
      6.0,
      QString{});
  audio_controller.appendSourceClipToTimeline();
  const auto audio_tracks = audio_controller.sequenceTracks();
  if (audio_tracks.size() != 2 ||
      audio_tracks.at(0).toMap().value(QStringLiteral("clipCount")).toInt() != 0 ||
      audio_tracks.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 1 ||
      audio_tracks.at(1).toMap().value(QStringLiteral("clips")).toList().at(0).toMap().value(QStringLiteral("mediaType")).toString()
          != QStringLiteral("audio")) {
    return fail("standalone audio clips should persist on audio lanes instead of being discarded during sync");
  }

  TimelineDocumentController edit_controller;
  edit_controller.createTimeline(QStringLiteral("Edit Timeline"));
  edit_controller.addClipFromGallery(QStringLiteral("video:a"), first_video, QStringLiteral("Alpha"), 4.0, QStringLiteral("poster://a"));
  edit_controller.addClipFromGallery(QStringLiteral("video:b"), second_video, QStringLiteral("Beta"), 4.0, QStringLiteral("poster://b"));
  edit_controller.seekSequenceSeconds(3.0);
  edit_controller.addSequenceMarker(QStringLiteral("Insert Beat"));
  edit_controller.seekSequenceSeconds(1.5);
  edit_controller.insertClipFromGallery(
      QStringLiteral("video:insert"),
      first_video,
      QStringLiteral("Insert"),
      5.0,
      1.0,
      3.0,
      QStringLiteral("poster://insert"));
  if (edit_controller.clipCount() != 4) {
    return fail("insert edits should split the active clip and push later clips down the timeline");
  }
  const auto inserted_clips = edit_controller.clips();
  if (!nearly_equal(inserted_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 1.5) ||
      !nearly_equal(inserted_clips.at(2).toMap().value(QStringLiteral("startSeconds")).toDouble(), 3.5) ||
      !nearly_equal(inserted_clips.at(3).toMap().value(QStringLiteral("startSeconds")).toDouble(), 6.0)) {
    return fail("insert edits should preserve later clip order while shifting their sequence starts");
  }
  if (!nearly_equal(edit_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 5.0)) {
    return fail("insert edits should ripple later sequence markers forward with the inserted duration");
  }

  edit_controller.selectClip(0);
  edit_controller.seekSequenceSeconds(6.0);
  edit_controller.addSequenceMarker(QStringLiteral("Trim Beat"));
  edit_controller.selectClip(0);
  edit_controller.trimSelectedClipEndToSequenceSeconds(1.0, true);
  const auto ripple_clips = edit_controller.clips();
  if (!nearly_equal(ripple_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 1.0)) {
    return fail("ripple trims should close downstream timing after a trim-out change");
  }
  if (!nearly_equal(edit_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 4.5) ||
      !nearly_equal(edit_controller.sequenceMarkers().at(1).toMap().value(QStringLiteral("seconds")).toDouble(), 5.5)) {
    return fail("ripple trims should move later sequence markers with the rippled timeline");
  }
  edit_controller.selectClip(1);
  const auto slipped_before = edit_controller.currentClip();
  if (!edit_controller.slipSelectedClipBySeconds(0.5)) {
    return fail("slipping a selected clip should succeed when source handles remain available");
  }
  const auto slipped_after = edit_controller.currentClip();
  if (!nearly_equal(slipped_after.value(QStringLiteral("trimStartSeconds")).toDouble(),
                    slipped_before.value(QStringLiteral("trimStartSeconds")).toDouble() + 0.5) ||
      !nearly_equal(slipped_after.value(QStringLiteral("trimEndSeconds")).toDouble(),
                    slipped_before.value(QStringLiteral("trimEndSeconds")).toDouble() + 0.5) ||
      !nearly_equal(slipped_after.value(QStringLiteral("startSeconds")).toDouble(),
                    slipped_before.value(QStringLiteral("startSeconds")).toDouble())) {
    return fail("slipping should change source trim without moving the clip on the timeline");
  }

  TimelineDocumentController extract_controller;
  extract_controller.createTimeline(QStringLiteral("Extract Timeline"));
  extract_controller.addClipFromGallery(QStringLiteral("video:e1"), first_video, QStringLiteral("First"), 4.0, QStringLiteral("poster://e1"));
  extract_controller.addClipFromGallery(QStringLiteral("video:e2"), second_video, QStringLiteral("Second"), 4.0, QStringLiteral("poster://e2"));
  extract_controller.seekSequenceSeconds(5.0);
  const auto extract_marker_id = extract_controller.addSequenceMarker(QStringLiteral("After First"));
  extract_controller.selectClip(0);
  extract_controller.extractSelectedClip();
  if (extract_controller.clipCount() != 1 ||
      !nearly_equal(extract_controller.clips().at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.0) ||
      !nearly_equal(extract_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 1.0) ||
      extract_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("markerId")).toString() != extract_marker_id) {
    return fail("extracting a clip should close the gap and ripple later markers earlier");
  }
  extract_controller.selectClip(0);
  extract_controller.liftSelectedClip();
  if (extract_controller.clipCount() != 0) {
    return fail("lifting a clip should remove it without requiring ripple behavior");
  }

  TimelineDocumentController ripple_head_controller;
  ripple_head_controller.createTimeline(QStringLiteral("Ripple Head Timeline"));
  ripple_head_controller.addClipFromGallery(QStringLiteral("video:r1"), first_video, QStringLiteral("Head"), 4.0, QStringLiteral("poster://r1"));
  ripple_head_controller.addClipFromGallery(QStringLiteral("video:r2"), second_video, QStringLiteral("Tail"), 4.0, QStringLiteral("poster://r2"));
  ripple_head_controller.seekSequenceSeconds(5.0);
  ripple_head_controller.addSequenceMarker(QStringLiteral("After Head Trim"));
  ripple_head_controller.selectClip(0);
  ripple_head_controller.trimSelectedClipStartToSequenceSeconds(1.0, true);
  if (!nearly_equal(ripple_head_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), 0.0) ||
      !nearly_equal(ripple_head_controller.currentClip().value(QStringLiteral("trimStartSeconds")).toDouble(), 1.0) ||
      !nearly_equal(ripple_head_controller.clips().at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 3.0) ||
      !nearly_equal(ripple_head_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 4.0)) {
    return fail("ripple trims on the clip head should shorten the clip and pull later timeline items earlier");
  }

  TimelineDocumentController multi_extract_controller;
  multi_extract_controller.createTimeline(QStringLiteral("Multi Extract Timeline"));
  multi_extract_controller.addClipFromGallery(QStringLiteral("video:m1"), first_video, QStringLiteral("One"), 4.0, QStringLiteral("poster://m1"));
  multi_extract_controller.addClipFromGallery(QStringLiteral("video:m2"), second_video, QStringLiteral("Two"), 4.0, QStringLiteral("poster://m2"));
  multi_extract_controller.addClipFromGallery(QStringLiteral("video:m3"), first_video, QStringLiteral("Three"), 4.0, QStringLiteral("poster://m3"));
  multi_extract_controller.seekSequenceSeconds(10.0);
  multi_extract_controller.addSequenceMarker(QStringLiteral("After Multi"));
  multi_extract_controller.selectClipRange(0, 1);
  multi_extract_controller.extractSelectedClips();
  if (multi_extract_controller.clipCount() != 1 ||
      !nearly_equal(multi_extract_controller.clips().at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.0) ||
      !nearly_equal(multi_extract_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 2.0)) {
    return fail("extracting a multi-selection should close all removed ranges and ripple later markers");
  }

  TimelineDocumentController multi_lift_controller;
  multi_lift_controller.createTimeline(QStringLiteral("Multi Lift Timeline"));
  multi_lift_controller.addClipFromGallery(QStringLiteral("video:l1"), first_video, QStringLiteral("One"), 4.0, QStringLiteral("poster://l1"));
  multi_lift_controller.addClipFromGallery(QStringLiteral("video:l2"), second_video, QStringLiteral("Two"), 4.0, QStringLiteral("poster://l2"));
  multi_lift_controller.addClipFromGallery(QStringLiteral("video:l3"), first_video, QStringLiteral("Three"), 4.0, QStringLiteral("poster://l3"));
  multi_lift_controller.selectClipRange(0, 1);
  multi_lift_controller.liftSelectedClips();
  if (multi_lift_controller.clipCount() != 1 ||
      !nearly_equal(multi_lift_controller.clips().at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 8.0)) {
    return fail("lifting a multi-selection should remove the clips without closing the remaining gap");
  }

  TimelineDocumentController overwrite_controller;
  overwrite_controller.createTimeline(QStringLiteral("Overwrite Timeline"));
  overwrite_controller.addClipFromGallery(QStringLiteral("video:base-a"), first_video, QStringLiteral("Base A"), 4.0, QStringLiteral("poster://oa"));
  overwrite_controller.addClipFromGallery(QStringLiteral("video:base-b"), second_video, QStringLiteral("Base B"), 4.0, QStringLiteral("poster://ob"));
  overwrite_controller.seekSequenceSeconds(3.0);
  overwrite_controller.overwriteClipFromGallery(
      QStringLiteral("video:ovr"),
      first_video,
      QStringLiteral("Overwrite"),
      3.0,
      0.5,
      2.5,
      QStringLiteral("poster://ovr"));
  if (overwrite_controller.clipCount() != 3) {
    return fail("overwrite edits should trim away covered ranges while preserving uncovered material");
  }
  const auto overwritten_clips = overwrite_controller.clips();
  if (!nearly_equal(overwritten_clips.at(0).toMap().value(QStringLiteral("trimEndSeconds")).toDouble(), 3.0) ||
      !nearly_equal(overwritten_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 3.0) ||
      !nearly_equal(overwritten_clips.at(2).toMap().value(QStringLiteral("startSeconds")).toDouble(), 5.0) ||
      !nearly_equal(overwritten_clips.at(2).toMap().value(QStringLiteral("trimStartSeconds")).toDouble(), 1.0)) {
    return fail("overwrite edits should split overlapped clips at the in and out points");
  }

  overwrite_controller.selectClip(1);
  overwrite_controller.setSelectedFrameMode(QStringLiteral("fill"));
  overwrite_controller.setSelectedFrameScale(1.35);
  overwrite_controller.setSelectedFrameOffsetX(0.4);
  overwrite_controller.setSelectedFrameOffsetY(-0.25);
  overwrite_controller.setSelectedClipOpacity(0.62);
  overwrite_controller.setSelectedClipAudioGain(4.5);
  overwrite_controller.setSelectedClipFadeInSeconds(0.35);
  overwrite_controller.setSelectedClipFadeOutSeconds(0.40);
  overwrite_controller.setSelectedClipPlaybackRate(1.25);
  overwrite_controller.addSelectedClipEffect(QStringLiteral("Transform"));
  overwrite_controller.addSelectedClipEffect(QStringLiteral("Opacity"));
  overwrite_controller.assignSelectedClipTransition(QStringLiteral("in"), QStringLiteral("Fade In"), 0.30);
  overwrite_controller.assignSelectedClipTransition(QStringLiteral("out"), QStringLiteral("Cross Dissolve"), 0.45);
  const auto overwrite_marker_id = overwrite_controller.addSequenceMarker(QStringLiteral("Start"));
  overwrite_controller.setSequenceMarkerNote(overwrite_marker_id, QStringLiteral("Lock this cut"));
  overwrite_controller.setSequenceMarkerStatus(overwrite_marker_id, QStringLiteral("approved"));
  const auto transformed_clip = overwrite_controller.currentClip();
  const auto transformed_effects = transformed_clip.value(QStringLiteral("effectStack")).toList();
  if (transformed_clip.value(QStringLiteral("frameMode")).toString() != QStringLiteral("fill") ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameScale")).toDouble(), 1.35) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameOffsetX")).toDouble(), 0.4) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameOffsetY")).toDouble(), -0.25) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("opacity")).toDouble(), 0.62) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("audioGainDb")).toDouble(), 4.5) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("fadeInSeconds")).toDouble(), 0.35) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("fadeOutSeconds")).toDouble(), 0.40) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("playbackRate")).toDouble(), 1.25) ||
      transformed_effects.size() != 2 ||
      transformed_effects.at(0).toString() != QStringLiteral("Transform") ||
      transformed_effects.at(1).toString() != QStringLiteral("Opacity") ||
      transformed_clip.value(QStringLiteral("transitionInName")).toString() != QStringLiteral("Fade In") ||
      transformed_clip.value(QStringLiteral("transitionOutName")).toString() != QStringLiteral("Cross Dissolve") ||
      !nearly_equal(transformed_clip.value(QStringLiteral("transitionInSeconds")).toDouble(), 0.30) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("transitionOutSeconds")).toDouble(), 0.45) ||
      overwrite_controller.sequenceMarkers().size() != 1) {
    return fail("extended clip controls, effects, transitions, and markers should persist on the selected clip");
  }
  if (overwrite_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("status")).toString() != QStringLiteral("approved")) {
    return fail("marker status should update independently of clip controls");
  }
  overwrite_controller.selectClip(1);
  const auto roll_target = overwrite_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble() - 0.5;
  if (!overwrite_controller.rollSelectedClipStartToSequenceSeconds(roll_target)) {
    return fail("rolling the selected clip start should succeed when an adjacent left clip exists");
  }
  if (!nearly_equal(overwrite_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), roll_target)) {
    return fail("rolling a clip boundary should move the boundary to the requested sequence time");
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (!ffmpeg.isEmpty() && !ffprobe.isEmpty()) {
    photo_editor::desktop::JobRunner runner;
    const auto render_video_one = create_test_video(
        ffmpeg,
        workspace.filePath(QStringLiteral("render-01.mp4")),
        QStringLiteral("red"),
        true);
    const auto render_video_two = create_test_video(
        ffmpeg,
        workspace.filePath(QStringLiteral("render-02.mp4")),
        QStringLiteral("blue"),
        false);
    if (render_video_one.isEmpty() || render_video_two.isEmpty()) {
      return fail("fixture videos should be created when ffmpeg is available");
    }

    TimelineDocumentController render_controller;
    render_controller.createTimeline(QStringLiteral("Rendered Timeline"));
    render_controller.addClipFromGallery(QStringLiteral("video:red"), render_video_one, QStringLiteral("Red Clip"), 0.0, QString{});
    render_controller.addClipFromGallery(QStringLiteral("video:blue"), render_video_two, QStringLiteral("Blue Clip"), 0.0, QString{});
    render_controller.selectClip(0);
    render_controller.setSelectedTrimStartSeconds(0.10);
    render_controller.setSelectedTrimEndSeconds(0.70);
    render_controller.setSelectedClipOpacity(0.7);
    render_controller.setSelectedClipAudioGain(3.0);
    render_controller.selectClip(1);
    render_controller.setSelectedTrimStartSeconds(0.20);
    render_controller.setSelectedTrimEndSeconds(0.80);
    render_controller.setSelectedClipPlaybackRate(1.1);
    const auto render_marker_id = render_controller.addSequenceMarker(QStringLiteral("Cut"));
    render_controller.setSequenceMarkerNote(render_marker_id, QStringLiteral("Render-ready"));
    render_controller.setSequenceMarkerStatus(render_marker_id, QStringLiteral("exported"));
    render_controller.setSequenceMarkerCompleted(render_marker_id, true);

    if (!render_controller.saveTimeline()) {
      return fail("saving a rendered timeline should succeed when ffmpeg is available");
    }

    const auto render_job_id =
        QStringLiteral("timeline-render-%1").arg(render_controller.timelineAssetId().section(':', 1));
    if (!wait_for_persisted_job(render_job_id)) {
      return fail("queued timeline renders should complete through the shared job runner");
    }

    if (!QFileInfo::exists(render_controller.savedDocumentPath()) ||
        !QFileInfo::exists(render_controller.savedRenderPath())) {
      return fail("saving a timeline should persist both the document and rendered video");
    }

    QString probe_error;
    const auto saved_render_duration = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"),
            QStringLiteral("error"),
            QStringLiteral("-show_entries"),
            QStringLiteral("format=duration"),
            QStringLiteral("-of"),
            QStringLiteral("default=nw=1:nk=1"),
            render_controller.savedRenderPath(),
        },
        &probe_error);
    if (saved_render_duration.isEmpty()) {
      std::cerr << "ffprobe saved render duration failed: " << probe_error.toStdString() << '\n';
      return fail("saved timeline renders should be probeable as playable media");
    }

    bool duration_ok = false;
    const auto parsed_duration = saved_render_duration.toDouble(&duration_ok);
    if (!duration_ok || parsed_duration <= 0.0) {
      return fail("saved timeline renders should report a nonzero duration");
    }

    const auto saved_render_streams = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"),
            QStringLiteral("error"),
            QStringLiteral("-select_streams"),
            QStringLiteral("v:0"),
            QStringLiteral("-show_entries"),
            QStringLiteral("stream=codec_type"),
            QStringLiteral("-of"),
            QStringLiteral("csv=p=0"),
            render_controller.savedRenderPath(),
        },
        &probe_error);
    if (saved_render_streams.isEmpty()) {
      std::cerr << "ffprobe saved render streams failed: " << probe_error.toStdString() << '\n';
      return fail("saved timeline renders should contain a readable video stream");
    }

    QFile saved_document(render_controller.savedDocumentPath());
    if (!saved_document.open(QIODevice::ReadOnly)) {
      return fail("saved timeline documents should be readable");
    }
    const auto saved_json = QJsonDocument::fromJson(saved_document.readAll()).object();
    if (saved_json.value(QStringLiteral("tracks")).toArray().size() != 2) {
      return fail("saved timelines should persist their track-based sequence layout");
    }
    const auto saved_format = saved_json.value(QStringLiteral("sequenceFormat")).toObject();
    if (!saved_format.contains(QStringLiteral("audioSampleRate")) ||
        !saved_json.contains(QStringLiteral("projectBinding")) ||
        !saved_json.contains(QStringLiteral("exportContainer")) ||
        !saved_json.contains(QStringLiteral("exportCodec"))) {
      return fail("saved timelines should persist the expanded sequence and export settings");
    }

    const auto records = loadTimelineAssetRecords();
    if (records.isEmpty()) {
      return fail("saving a timeline should update the gallery timeline index");
    }

    bool found_saved_timeline = false;
    for (const auto& record : records) {
      if (record.document_path == render_controller.savedDocumentPath() &&
          record.render_path == render_controller.savedRenderPath()) {
        found_saved_timeline = true;
        break;
      }
    }
    if (!found_saved_timeline) {
      return fail("timeline index should contain the newly saved timeline asset");
    }

    const auto persisted_jobs = loadPersistedJobRecords();
    bool found_timeline_render_job = false;
    for (const auto& job : persisted_jobs) {
      if (job.job_kind == QStringLiteral("timeline_render") &&
          job.asset_id == render_controller.timelineAssetId() &&
          job.status == QStringLiteral("Completed") &&
          job.output_path == render_controller.savedRenderPath()) {
        found_timeline_render_job = true;
        break;
      }
    }
    if (!found_timeline_render_job) {
      return fail("saving a timeline should persist a completed shared render job record");
    }

    TimelineDocumentController load_controller;
    if (!load_controller.loadTimeline(render_controller.savedDocumentPath())) {
      return fail("saved timeline documents should load successfully");
    }

    if (load_controller.clipCount() != 2 ||
        load_controller.timelineName() != QStringLiteral("Rendered Timeline")) {
      return fail("loading a saved timeline should restore its clips and metadata");
    }
    if (load_controller.sequenceMarkers().size() != 1) {
      return fail("saved timelines should restore sequence markers");
    }

    const auto loaded_clips = load_controller.clips();
    if (loaded_clips.at(0).toMap().value(QStringLiteral("trimStartSeconds")).toDouble() != 0.10 ||
        loaded_clips.at(0).toMap().value(QStringLiteral("trimEndSeconds")).toDouble() != 0.70 ||
        loaded_clips.at(0).toMap().value(QStringLiteral("opacity")).toDouble() != 0.7 ||
        loaded_clips.at(0).toMap().value(QStringLiteral("audioGainDb")).toDouble() != 3.0 ||
        loaded_clips.at(1).toMap().value(QStringLiteral("trimStartSeconds")).toDouble() != 0.20 ||
        loaded_clips.at(1).toMap().value(QStringLiteral("trimEndSeconds")).toDouble() != 0.80 ||
        loaded_clips.at(1).toMap().value(QStringLiteral("playbackRate")).toDouble() != 1.1 ||
        load_controller.sequenceMarkers().size() != 1) {
      return fail("loading a saved timeline should preserve extended clip and marker state");
    }
    const auto loaded_marker = load_controller.sequenceMarkers().at(0).toMap();
    if (loaded_marker.value(QStringLiteral("note")).toString() != QStringLiteral("Render-ready") ||
        loaded_marker.value(QStringLiteral("status")).toString() != QStringLiteral("exported") ||
        !loaded_marker.value(QStringLiteral("completed")).toBool()) {
      return fail("loading a saved timeline should preserve marker review metadata");
    }

    load_controller.selectClip(0);
    load_controller.addSelectedClipEffect(QStringLiteral("Transform"));
    load_controller.assignSelectedClipTransition(QStringLiteral("out"), QStringLiteral("Fade Out"), 0.25);
    if (load_controller.currentClip().value(QStringLiteral("effectStack")).toList().size() != 1 ||
        load_controller.currentClip().value(QStringLiteral("transitionOutName")).toString() != QStringLiteral("Fade Out")) {
      return fail("loaded timelines should accept new effects and transitions on selected clips");
    }

    if (!QFile::remove(render_controller.savedDocumentPath())) {
      return fail("saved timeline document should be removable for recovery testing");
    }

    TimelineDocumentController recovery_controller;
    if (!recovery_controller.loadTimeline(render_controller.savedDocumentPath())) {
      return fail("timeline recovery should load from the persisted backup when the document file is missing");
    }
    if (recovery_controller.timelineName() != QStringLiteral("Rendered Timeline") ||
        recovery_controller.clipCount() != 2 ||
        recovery_controller.sequenceMarkers().size() != 1) {
      return fail("timeline recovery should restore timeline state from the persisted backup");
    }
  }

  return EXIT_SUCCESS;
}

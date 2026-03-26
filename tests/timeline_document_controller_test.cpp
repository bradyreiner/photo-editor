#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "timeline_asset_store.h"
#include "timeline_document_controller.h"

namespace {

using photo_editor::desktop::loadTimelineAssetRecords;
using photo_editor::desktop::TimelineDocumentController;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool nearly_equal(double left, double right, double tolerance = 0.0001) {
  return std::abs(left - right) <= tolerance;
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
  if (first_video.isEmpty() || second_video.isEmpty()) {
    return fail("video placeholders should be created");
  }

  TimelineDocumentController controller;
  controller.createTimeline(QStringLiteral("Gallery Timeline"));
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
  layered_controller.moveSelectedClipToTrackRow(2, 1.0);
  if (layered_controller.trackCount() != 4 || layered_controller.videoTrackCount() != 2 || layered_controller.audioTrackCount() != 2) {
    return fail("adding a video track should create a paired video and audio lane");
  }
  const auto layered_tracks = layered_controller.sequenceTracks();
  if (layered_tracks.size() != 4 ||
      layered_tracks.at(0).toMap().value(QStringLiteral("clipCount")).toInt() != 1 ||
      layered_tracks.at(2).toMap().value(QStringLiteral("clipCount")).toInt() != 1) {
    return fail("moving a clip to a new video lane should preserve clip counts across paired tracks");
  }
  layered_controller.seekSequenceSeconds(1.5);
  if (layered_controller.currentClip().value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V2") ||
      layered_controller.currentClip().value(QStringLiteral("displayLabel")).toString() != QStringLiteral("Foreground") ||
      !nearly_equal(layered_controller.currentClipPlayheadSeconds(), 0.5)) {
    return fail("sequence seeking should resolve the topmost active clip after moving it to a higher track");
  }

  TimelineDocumentController edit_controller;
  edit_controller.createTimeline(QStringLiteral("Edit Timeline"));
  edit_controller.addClipFromGallery(QStringLiteral("video:a"), first_video, QStringLiteral("Alpha"), 4.0, QStringLiteral("poster://a"));
  edit_controller.addClipFromGallery(QStringLiteral("video:b"), second_video, QStringLiteral("Beta"), 4.0, QStringLiteral("poster://b"));
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

  edit_controller.selectClip(0);
  edit_controller.trimSelectedClipEndToSequenceSeconds(1.0, true);
  const auto ripple_clips = edit_controller.clips();
  if (!nearly_equal(ripple_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 1.0)) {
    return fail("ripple trims should close downstream timing after a trim-out change");
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
  const auto transformed_clip = overwrite_controller.currentClip();
  if (transformed_clip.value(QStringLiteral("frameMode")).toString() != QStringLiteral("fill") ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameScale")).toDouble(), 1.35) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameOffsetX")).toDouble(), 0.4) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameOffsetY")).toDouble(), -0.25)) {
    return fail("frame controls should persist per-clip transform settings");
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (!ffmpeg.isEmpty() && !ffprobe.isEmpty()) {
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
    render_controller.selectClip(1);
    render_controller.setSelectedTrimStartSeconds(0.20);
    render_controller.setSelectedTrimEndSeconds(0.80);

    if (!render_controller.saveTimeline()) {
      return fail("saving a rendered timeline should succeed when ffmpeg is available");
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

    TimelineDocumentController load_controller;
    if (!load_controller.loadTimeline(render_controller.savedDocumentPath())) {
      return fail("saved timeline documents should load successfully");
    }

    if (load_controller.clipCount() != 2 ||
        load_controller.timelineName() != QStringLiteral("Rendered Timeline")) {
      return fail("loading a saved timeline should restore its clips and metadata");
    }

    const auto loaded_clips = load_controller.clips();
    if (loaded_clips.at(0).toMap().value(QStringLiteral("trimStartSeconds")).toDouble() != 0.10 ||
        loaded_clips.at(0).toMap().value(QStringLiteral("trimEndSeconds")).toDouble() != 0.70 ||
        loaded_clips.at(1).toMap().value(QStringLiteral("trimStartSeconds")).toDouble() != 0.20 ||
        loaded_clips.at(1).toMap().value(QStringLiteral("trimEndSeconds")).toDouble() != 0.80) {
      return fail("loading a saved timeline should preserve trimmed clip ranges");
    }
  }

  return EXIT_SUCCESS;
}

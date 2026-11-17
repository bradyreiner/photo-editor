#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>

#include "timeline_document_controller.h"

namespace {
using photo_editor::desktop::TimelineDocumentController;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool nearly_equal(double left, double right, double tolerance = 1.0e-5) {
  return std::abs(left - right) <= tolerance;
}

QString create_photo(const QString& path, QRgb color) {
  QImage image(64, 48, QImage::Format_RGB32);
  image.fill(color);
  return image.save(path) ? path : QString{};
}

QVariantMap clip_by_id(
    const TimelineDocumentController& controller,
    const QString& clip_id) {
  for (const auto& track_value : controller.sequenceTracks()) {
    const auto track = track_value.toMap();
    for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("clipId")).toString() == clip_id) {
        return clip;
      }
    }
  }
  return {};
}

QVariantMap clip_on_track(
    const TimelineDocumentController& controller,
    const QString& track_id,
    const QString& clip_id) {
  for (const auto& track_value : controller.sequenceTracks()) {
    const auto track = track_value.toMap();
    if (track.value(QStringLiteral("trackId")).toString() != track_id) continue;
    for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("clipId")).toString() == clip_id) return clip;
    }
  }
  return {};
}

QVariantMap track_by_id(
    const TimelineDocumentController& controller,
    const QString& track_id) {
  for (const auto& value : controller.sequenceTracks()) {
    const auto track = value.toMap();
    if (track.value(QStringLiteral("trackId")).toString() == track_id) {
      return track;
    }
  }
  return {};
}

bool create_timeline_with_photo(
    TimelineDocumentController* controller,
    const QString& photo_path,
    const QString& label,
    double duration_seconds) {
  if (controller == nullptr) return false;
  controller->createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Direct Tool Fixture")},
      {QStringLiteral("width"), 64},
      {QStringLiteral("height"), 48},
      {QStringLiteral("frameRate"), 24.0},
  });
  controller->addClipFromGallery(
      QStringLiteral("photo:%1").arg(label), photo_path, label,
      duration_seconds, QString{});
  return controller->clipCount() == 1;
}

QString create_video_with_audio(const QString& output_path) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) return {};
  QProcess process;
  process.start(ffmpeg, {
      QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("color=c=red:s=64x48:d=2:r=24"),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("sine=frequency=440:duration=2"),
      QStringLiteral("-shortest"), QStringLiteral("-map"), QStringLiteral("0:v:0"),
      QStringLiteral("-map"), QStringLiteral("1:a:0"), QStringLiteral("-c:v"),
      QStringLiteral("libx264"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
      QStringLiteral("-c:a"), QStringLiteral("aac"), output_path,
  });
  if (!process.waitForStarted(5000) || !process.waitForFinished(30000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    std::cerr << "ffmpeg linked A/V fixture generation failed: "
              << process.readAllStandardError().toStdString() << '\n';
    return {};
  }
  return QFile::exists(output_path) ? output_path : QString{};
}

bool verify_rate_stretch(const QString& workspace, const QString& photo_path) {
  TimelineDocumentController controller;
  if (!create_timeline_with_photo(&controller, photo_path, QStringLiteral("Rate"), 2.0)) {
    std::cerr << "Rate Stretch fixture should contain one real image clip\n";
    return false;
  }
  controller.setActiveTool(QStringLiteral("rate-stretch"));
  if (controller.activeTool() != QStringLiteral("rate-stretch")) {
    std::cerr << "The direct Rate Stretch tool should be accepted as persistent editor state\n";
    return false;
  }
  auto clip = controller.currentClip();
  const auto clip_id = clip.value(QStringLiteral("clipId")).toString();
  const auto initial = clip_by_id(controller, clip_id);
  const auto track_id = controller.rateStretchAvailability(clip_id)
                            .value(QStringLiteral("trackId")).toString();
  const auto availability = controller.rateStretchAvailability(clip_id);
  if (clip_id.isEmpty() || track_id.isEmpty() ||
      !availability.value(QStringLiteral("enabled")).toBool() ||
      !nearly_equal(initial.value(QStringLiteral("playbackRate")).toDouble(), 1.0)) {
    std::cerr << "Rate Stretch should advertise an unlocked constant-speed clip\n";
    return false;
  }

  const auto initial_start = initial.value(QStringLiteral("startSeconds")).toDouble();
  const auto initial_trim_start = initial.value(QStringLiteral("trimStartSeconds")).toDouble();
  const auto initial_trim_end = initial.value(QStringLiteral("trimEndSeconds")).toDouble();
  if (!controller.beginRateStretchEdit(clip_id) ||
      !controller.updateRateStretchEdit(4.0)) {
    std::cerr << "A valid Rate Stretch gesture should update the exact clip\n";
    return false;
  }
  clip = clip_by_id(controller, clip_id);
  if (!nearly_equal(clip.value(QStringLiteral("startSeconds")).toDouble(), initial_start) ||
      !nearly_equal(clip.value(QStringLiteral("trimStartSeconds")).toDouble(), initial_trim_start) ||
      !nearly_equal(clip.value(QStringLiteral("trimEndSeconds")).toDouble(), initial_trim_end) ||
      !nearly_equal(clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 4.0) ||
      !nearly_equal(clip.value(QStringLiteral("playbackRate")).toDouble(), 0.5)) {
    std::cerr << "Rate Stretch must hold clip start/source range and map 2 seconds into 4\n";
    return false;
  }
  if (!controller.endRateStretchEdit() || !controller.canUndo()) {
    std::cerr << "A completed Rate Stretch gesture should create one undo step\n";
    return false;
  }
  controller.undo();
  clip = clip_by_id(controller, clip_id);
  if (!nearly_equal(clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 2.0) ||
      !nearly_equal(clip.value(QStringLiteral("playbackRate")).toDouble(), 1.0)) {
    std::cerr << "One undo should restore the source clip's original duration and rate\n";
    return false;
  }
  controller.redo();
  clip = clip_by_id(controller, clip_id);
  if (!nearly_equal(clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 4.0) ||
      !controller.saveTimeline()) {
    std::cerr << "Redo and timeline save should preserve the rate-stretched state\n";
    return false;
  }

  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(controller.savedDocumentPath())) {
    std::cerr << "The rate-stretched document should reopen\n";
    return false;
  }
  clip = clip_by_id(reopened, clip_id);
  if (!nearly_equal(clip.value(QStringLiteral("startSeconds")).toDouble(), initial_start) ||
      !nearly_equal(clip.value(QStringLiteral("trimStartSeconds")).toDouble(), initial_trim_start) ||
      !nearly_equal(clip.value(QStringLiteral("trimEndSeconds")).toDouble(), initial_trim_end) ||
      !nearly_equal(clip.value(QStringLiteral("playbackRate")).toDouble(), 0.5) ||
      !nearly_equal(clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 4.0)) {
    std::cerr << "Rate, source range, and start must survive document save/reopen\n";
    return false;
  }

  const auto hash_before_cancel = reopened.deterministicDocumentHash();
  const auto undo_before_cancel = reopened.canUndo();
  const auto selection_before_tools = reopened.selectedClipIds();
  const auto playhead_before_tools = reopened.sequencePlayheadSeconds();
  const auto saved_tool = reopened.activeTool();
  for (const auto& tool : {"select", "track-select", "hand", "zoom", "pen", "rate-stretch"}) {
    reopened.setActiveTool(QString::fromLatin1(tool));
    if (reopened.activeTool() != QString::fromLatin1(tool) || reopened.dirty() ||
        reopened.canUndo() != undo_before_cancel ||
        reopened.selectedClipIds() != selection_before_tools ||
        !nearly_equal(reopened.sequencePlayheadSeconds(), playhead_before_tools)) {
      std::cerr << "Choosing an editing or navigation tool must preserve a saved document and its history\n";
      return false;
    }
  }
  reopened.setActiveTool(saved_tool);
  if (reopened.deterministicDocumentHash() != hash_before_cancel) {
    std::cerr << "Restoring the saved tool must preserve all serialized timeline content\n";
    return false;
  }
  if (!reopened.beginRateStretchEdit(clip_id) ||
      !reopened.updateRateStretchEdit(6.0) ||
      !reopened.cancelRateStretchEdit()) {
    std::cerr << "A Rate Stretch gesture should support a deterministic cancel\n";
    return false;
  }
  clip = clip_by_id(reopened, clip_id);
  if (reopened.deterministicDocumentHash() != hash_before_cancel ||
      reopened.canUndo() != undo_before_cancel ||
      !nearly_equal(clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 4.0)) {
    std::cerr << "Cancel should restore the exact recipe without adding an undo operation\n";
    return false;
  }

  if (!reopened.beginRateStretchEdit(clip_id) ||
      !reopened.updateRateStretchEdit(6.0) ||
      !reopened.loadTimeline(reopened.savedDocumentPath()) ||
      reopened.updateRateStretchEdit(7.0)) {
    std::cerr << "A stale gesture must not retarget a same-ID clip after reopening\n";
    return false;
  }
  clip = clip_by_id(reopened, clip_id);
  if (!nearly_equal(clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 4.0)) {
    std::cerr << "Reopening the same clip ID should restore the saved rate without stale writes\n";
    return false;
  }
  Q_UNUSED(workspace);
  return true;
}

bool verify_neighbor_and_lock_guards(
    const QString& first_photo,
    const QString& second_photo) {
  TimelineDocumentController controller;
  controller.createTimeline(QStringLiteral("Rate Stretch Bounds"));
  controller.addClipFromGallery(
      QStringLiteral("photo:first"), first_photo, QStringLiteral("First"), 2.0, {});
  controller.addClipFromGallery(
      QStringLiteral("photo:second"), second_photo, QStringLiteral("Second"), 1.0, {});
  const auto tracks = controller.sequenceTracks();
  const auto video_track = tracks.isEmpty() ? QVariantMap{} : tracks.first().toMap();
  const auto clips = video_track.value(QStringLiteral("clips")).toList();
  if (clips.size() != 2) {
    std::cerr << "The collision fixture should have two adjacent clips on V1\n";
    return false;
  }
  const auto first = clips.first().toMap();
  const auto clip_id = first.value(QStringLiteral("clipId")).toString();
  const auto availability = controller.rateStretchAvailability(clip_id);
  if (!availability.value(QStringLiteral("enabled")).toBool() ||
      !nearly_equal(availability.value(QStringLiteral("maximumDurationSeconds")).toDouble(), 2.0) ||
      !controller.beginRateStretchEdit(clip_id) ||
      controller.updateRateStretchEdit(4.0)) {
    std::cerr << "Rate Stretch should not extend through the next clip on the same track\n";
    return false;
  }
  if (!controller.cancelRateStretchEdit()) {
    std::cerr << "An unchanged collision-clamped gesture should still cancel cleanly\n";
    return false;
  }
  const auto video_track_id = video_track.value(QStringLiteral("trackId")).toString();
  const auto video_row = video_track.value(QStringLiteral("trackIndex")).toInt();
  controller.toggleTrackLocked(video_row);
  const auto locked = controller.rateStretchAvailability(clip_id);
  if (locked.value(QStringLiteral("enabled")).toBool() ||
      !locked.value(QStringLiteral("reason")).toString().contains(QStringLiteral("Unlock")) ||
      controller.beginRateStretchEdit(clip_id)) {
    std::cerr << "Rate Stretch should reject a locked clip track before mutation\n";
    return false;
  }
  Q_UNUSED(video_track_id);
  return true;
}

bool verify_linked_audio_rate_stretch(const QString& video_path) {
  TimelineDocumentController controller;
  controller.createTimeline(QStringLiteral("Linked Rate Stretch"));
  controller.addClipFromGallery(
      QStringLiteral("video:linked-rate"), video_path,
      QStringLiteral("Linked Video"), 2.0, QStringLiteral("test://poster"));
  QElapsedTimer metadata_timer;
  metadata_timer.start();
  while (metadata_timer.elapsed() < 20000) {
    const auto current = controller.currentClip();
    const auto status = current.value(QStringLiteral("mediaAnalysisStatus")).toString();
    if (status == QStringLiteral("ready") || status == QStringLiteral("failed")) {
      break;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
  }
  const auto clip_id = controller.currentClip()
                           .value(QStringLiteral("clipId")).toString();
  const auto video_track_id = controller.rateStretchAvailability(clip_id)
                                  .value(QStringLiteral("trackId")).toString();
  const auto video_track = track_by_id(controller, video_track_id);
  const auto audio_track = track_by_id(controller, QStringLiteral("audio-1"));
  const auto audio_mirror = clip_on_track(controller, QStringLiteral("audio-1"), clip_id);
  const auto initial_availability = controller.rateStretchAvailability(clip_id);
  if (clip_id.isEmpty() || video_track.isEmpty() || audio_track.isEmpty() ||
      audio_mirror.isEmpty() ||
      !controller.currentClip().value(QStringLiteral("hasAudio")).toBool()) {
    std::cerr << "Linked A/V fixture did not resolve: status="
              << controller.currentClip().value(QStringLiteral("mediaAnalysisStatus")).toString().toStdString()
              << " hasAudio="
              << controller.currentClip().value(QStringLiteral("hasAudio")).toBool()
              << " videoTrack=" << !video_track.isEmpty()
              << " audioTrack=" << !audio_track.isEmpty()
              << " audioMirror=" << !audio_mirror.isEmpty()
              << " reason="
              << initial_availability.value(QStringLiteral("reason")).toString().toStdString()
              << '\n';
    return false;
  }
  const auto selected_from_audio = controller.selectClipsFromSequenceTime(
      QStringLiteral("audio-1"), 0.5, 1, false);
  if (selected_from_audio != 1 ||
      !controller.selectedClipIds().contains(clip_id)) {
    std::cerr << "Track Select on the paired audio lane returned " << selected_from_audio
              << " selected=" << controller.selectedClipIds().join(QLatin1Char(',')).toStdString()
              << " expected=" << clip_id.toStdString() << '\n';
    return false;
  }
  const auto availability = controller.rateStretchAvailability(clip_id);
  if (!availability.value(QStringLiteral("enabled")).toBool()) {
    std::cerr << "Linked A/V Rate Stretch is unavailable: "
              << availability.value(QStringLiteral("reason")).toString().toStdString()
              << '\n';
    return false;
  }
  if (!controller.beginRateStretchEdit(clip_id)) {
    std::cerr << "Linked A/V Rate Stretch gesture could not begin.\n";
    return false;
  }
  if (!controller.updateRateStretchEdit(4.0)) {
    std::cerr << "Linked A/V Rate Stretch gesture rejected the duration update.\n";
    (void)controller.cancelRateStretchEdit();
    return false;
  }
  if (!controller.endRateStretchEdit()) {
    std::cerr << "Linked A/V Rate Stretch gesture could not commit.\n";
    return false;
  }
  const auto updated_audio = clip_on_track(controller, QStringLiteral("audio-1"), clip_id);
  if (!nearly_equal(updated_audio.value(QStringLiteral("playbackRate")).toDouble(), 0.5) ||
      !nearly_equal(updated_audio.value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 4.0)) {
    std::cerr << "Rate Stretch should synchronize the linked A/V sequence duration and rate\n";
    return false;
  }

  controller.toggleTrackLocked(audio_track.value(QStringLiteral("trackIndex")).toInt());
  const auto locked_av = controller.rateStretchAvailability(clip_id);
  if (locked_av.value(QStringLiteral("enabled")).toBool() ||
      !locked_av.value(QStringLiteral("reason")).toString().contains(QStringLiteral("linked audio")) ||
      controller.beginRateStretchEdit(clip_id)) {
    std::cerr << "Rate Stretch should respect a locked linked audio row\n";
    return false;
  }
  controller.toggleTrackLocked(audio_track.value(QStringLiteral("trackIndex")).toInt());
  if (!controller.applySelectedClipRetimePreset(QStringLiteral("ramp-up")) ||
      controller.rateStretchAvailability(clip_id)
          .value(QStringLiteral("enabled")).toBool() ||
      !controller.rateStretchAvailability(clip_id)
           .value(QStringLiteral("reason")).toString().contains(QStringLiteral("retime curve"))) {
    std::cerr << "Rate Stretch should preserve existing retime curves and explain why it is unavailable\n";
    return false;
  }
  return true;
}

bool verify_envelope_pen_and_track_select(
    const QString& workspace,
    const QString& photo_path) {
  TimelineDocumentController controller;
  controller.createTimeline(QStringLiteral("Pen and Track Select"));
  controller.addClipFromGallery(
      QStringLiteral("photo:pen-1"), photo_path, QStringLiteral("One"), 1.0, {});
  controller.addClipFromGallery(
      QStringLiteral("photo:pen-2"), photo_path, QStringLiteral("Two"), 1.0, {});
  controller.addClipFromGallery(
      QStringLiteral("photo:pen-3"), photo_path, QStringLiteral("Three"), 1.0, {});
  controller.setActiveTool(QStringLiteral("track-select"));
  if (controller.activeTool() != QStringLiteral("track-select")) {
    std::cerr << "The Track Select tool should be accepted as persistent editor state\n";
    return false;
  }
  const auto tracks = controller.sequenceTracks();
  if (tracks.isEmpty()) {
    std::cerr << "The Pen fixture should have a video track\n";
    return false;
  }
  const auto video_track = tracks.first().toMap();
  const auto video_track_id = video_track.value(QStringLiteral("trackId")).toString();
  const auto clips = video_track.value(QStringLiteral("clips")).toList();
  if (clips.size() != 3) {
    std::cerr << "Track Select should have three clips on one real timeline row\n";
    return false;
  }
  const auto first_clip_id = clips.first().toMap().value(QStringLiteral("clipId")).toString();
  controller.selectClipsByIds({first_clip_id, first_clip_id, QStringLiteral("stale-clip-id")});
  if (controller.selectedClipIds() != QStringList{first_clip_id} || controller.currentIndex() != 0) {
    std::cerr << "Box selection must accept clip zero, deduplicate IDs and ignore stale IDs\n";
    return false;
  }
  if (controller.selectClipsFromSequenceTime(video_track_id, 1.0, 1, false) != 2 ||
      controller.selectedClipIds().size() != 2 ||
      controller.selectClipsFromSequenceTime(video_track_id, 2.0, -1, false) != 3 ||
      controller.selectedClipIds().size() != 3) {
    std::cerr << "Track Select should include the clip under the pointer and the requested direction\n";
    return false;
  }
  const auto before_invalid_track = controller.selectedClipIds();
  if (controller.selectClipsFromSequenceTime(
          QStringLiteral("stale-track-id"), 1.0, 1, false) != 0 ||
      controller.selectedClipIds() != before_invalid_track) {
    std::cerr << "A stale track ID must not alter the current selection\n";
    return false;
  }
  controller.addVideoTrack();
  QVariantMap empty_track;
  for (const auto& value : controller.sequenceTracks()) {
    const auto track = value.toMap();
    if (track.value(QStringLiteral("trackId")).toString().startsWith(QStringLiteral("video-")) &&
        track.value(QStringLiteral("clips")).toList().isEmpty()) {
      empty_track = track;
      break;
    }
  }
  const auto empty_track_id = empty_track.value(QStringLiteral("trackId")).toString();
  if (empty_track_id.isEmpty() ||
      controller.selectClipsFromSequenceTime(empty_track_id, 0.5, 1, false) != 0) {
    std::cerr << "Track Select should accept a real empty lane without selecting another track\n";
    return false;
  }

  auto availability = controller.envelopePenAvailability(
      first_clip_id, video_track_id, false, QStringLiteral("opacity"));
  if (!availability.value(QStringLiteral("enabled")).toBool() ||
      !controller.beginEnvelopePenGesture(
          first_clip_id, video_track_id, false, QStringLiteral("opacity")) ||
      !controller.updateEnvelopePenGesture(0.5, 0.4) ||
      !controller.updateEnvelopePenGesture(0.5, 0.6) ||
      !controller.endEnvelopePenGesture()) {
    std::cerr << "Pen should set clip envelope values by exact clip and track ID\n";
    return false;
  }
  const auto clip_after_pen = clip_by_id(controller, first_clip_id);
  const auto keyframes = clip_after_pen.value(QStringLiteral("keyframes")).toList();
  if (keyframes.size() != 1 ||
      !nearly_equal(keyframes.first().toMap().value(QStringLiteral("value")).toDouble(), 0.6) ||
      !controller.canUndo()) {
    std::cerr << "A Pen gesture should update one point and create one undo step\n";
    return false;
  }
  controller.undo();
  if (!clip_by_id(controller, first_clip_id)
           .value(QStringLiteral("keyframes")).toList().isEmpty()) {
    std::cerr << "One undo should remove the complete clip Pen gesture\n";
    return false;
  }
  controller.redo();
  if (clip_by_id(controller, first_clip_id)
          .value(QStringLiteral("keyframes")).toList().size() != 1) {
    std::cerr << "One redo should restore the complete clip Pen gesture\n";
    return false;
  }

  const auto clip_hash_before_cancel = controller.deterministicDocumentHash();
  const auto undo_before_pen_cancel = controller.canUndo();
  if (!controller.beginEnvelopePenGesture(
          first_clip_id, video_track_id, false, QStringLiteral("opacity")) ||
      !controller.updateEnvelopePenGesture(0.75, 0.8) ||
      !controller.cancelEnvelopePenGesture() ||
      controller.deterministicDocumentHash() != clip_hash_before_cancel ||
      controller.canUndo() != undo_before_pen_cancel ||
      clip_by_id(controller, first_clip_id)
              .value(QStringLiteral("keyframes")).toList().size() != 1) {
    std::cerr << "Cancel should restore the previous clip envelope without adding history\n";
    return false;
  }

  availability = controller.envelopePenAvailability(
      video_track_id, video_track_id, true, QStringLiteral("opacity"));
  if (!availability.value(QStringLiteral("enabled")).toBool() ||
      !controller.beginEnvelopePenGesture(
          video_track_id, video_track_id, true, QStringLiteral("opacity")) ||
      !controller.updateEnvelopePenGesture(0.25, 0.35) ||
      !controller.endEnvelopePenGesture()) {
    std::cerr << "Pen should also write the supported video-track opacity envelope\n";
    return false;
  }
  const auto track_keyframes = track_by_id(controller, video_track_id)
                                   .value(QStringLiteral("automationKeyframes")).toList();
  if (track_keyframes.size() != 1 ||
      track_keyframes.first().toMap().value(QStringLiteral("parameter")).toString() !=
          QStringLiteral("opacity")) {
    std::cerr << "The track Pen gesture should publish its real track automation point\n";
    return false;
  }

  const auto saved = controller.saveTimeline();
  if (!saved) {
    std::cerr << "Clip and track Pen automation should save through the timeline document\n";
    return false;
  }
  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(controller.savedDocumentPath()) ||
      clip_by_id(reopened, first_clip_id).value(QStringLiteral("keyframes")).toList().size() != 1 ||
      track_by_id(reopened, video_track_id)
              .value(QStringLiteral("automationKeyframes")).toList().size() != 1) {
    std::cerr << "Clip and track Pen automation should survive save/reopen\n";
    return false;
  }

  if (!reopened.beginEnvelopePenGesture(
          first_clip_id, video_track_id, false, QStringLiteral("opacity")) ||
      !reopened.updateEnvelopePenGesture(0.75, 0.8) ||
      !reopened.loadTimeline(reopened.savedDocumentPath()) ||
      reopened.updateEnvelopePenGesture(0.9, 0.9) ||
      clip_by_id(reopened, first_clip_id).value(QStringLiteral("keyframes")).toList().size() != 1) {
    std::cerr << "A stale Pen event must not edit a same-ID clip in a reopened session\n";
    return false;
  }
  Q_UNUSED(workspace);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QTemporaryDir workspace;
  if (!workspace.isValid()) return fail("A disposable timeline tool workspace is required.");
  qputenv("XDG_DATA_HOME", workspace.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", workspace.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", workspace.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", workspace.filePath(QStringLiteral("cache")).toUtf8());
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     workspace.filePath(QStringLiteral("settings/user")));
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                     workspace.filePath(QStringLiteral("settings/system")));

  QApplication app(argc, argv);
  QStandardPaths::setTestModeEnabled(true);
  const auto first_photo = create_photo(
      workspace.filePath(QStringLiteral("first.png")), qRgb(220, 65, 50));
  const auto second_photo = create_photo(
      workspace.filePath(QStringLiteral("second.png")), qRgb(45, 90, 210));
  const auto linked_video = create_video_with_audio(
      workspace.filePath(QStringLiteral("linked-source.mp4")));
  if (first_photo.isEmpty() || second_photo.isEmpty() || linked_video.isEmpty()) {
    return fail("The direct edit fixture images should be written to the temporary workspace.");
  }
  if (!verify_rate_stretch(workspace.path(), first_photo)) {
    return EXIT_FAILURE;
  }
  if (!verify_neighbor_and_lock_guards(first_photo, second_photo)) {
    return EXIT_FAILURE;
  }
  if (!verify_linked_audio_rate_stretch(linked_video)) {
    return EXIT_FAILURE;
  }
  if (!verify_envelope_pen_and_track_select(workspace.path(), first_photo)) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

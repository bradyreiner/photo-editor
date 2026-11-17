#include "agent_timeline_commands.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

namespace {

using photo_editor::desktop::AgentAppCommandRegistry;
using photo_editor::desktop::AgentTaskCoordinator;
using photo_editor::desktop::TimelineDocumentController;

bool fail(const char* message) {
  std::cerr << "timeline_clip_retime_action_test: " << message << '\n';
  return false;
}

bool run_process(const QString& program,
                 const QStringList& arguments,
                 QByteArray* standard_output = nullptr,
                 QString* error = nullptr) {
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(program, arguments);
  if (!process.waitForStarted(5000) || !process.waitForFinished(60000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error != nullptr) {
      *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
      if (error->isEmpty()) *error = process.errorString();
    }
    return false;
  }
  if (standard_output != nullptr) *standard_output = process.readAllStandardOutput();
  return true;
}

bool write_color_video(const QString& path,
                       const QString& ffmpeg,
                       QString* error) {
  return run_process(
      ffmpeg,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-y"),
       QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("color=c=red:s=160x90:r=30:d=1"),
       QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("color=c=green:s=160x90:r=30:d=1"),
       QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("color=c=blue:s=160x90:r=30:d=1"),
       QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("color=c=white:s=160x90:r=30:d=1"),
       QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("sine=frequency=440:sample_rate=48000:duration=4"),
       QStringLiteral("-filter_complex"),
       QStringLiteral("[0:v][1:v][2:v][3:v]concat=n=4:v=1:a=0,format=yuv420p[outv]"),
       QStringLiteral("-map"), QStringLiteral("[outv]"),
       QStringLiteral("-map"), QStringLiteral("4:a:0"),
       QStringLiteral("-c:v"), QStringLiteral("libx264"),
       QStringLiteral("-preset"), QStringLiteral("ultrafast"),
       QStringLiteral("-crf"), QStringLiteral("0"),
       QStringLiteral("-c:a"), QStringLiteral("aac"),
       QStringLiteral("-b:a"), QStringLiteral("96k"),
       QStringLiteral("-shortest"), path},
      nullptr, error);
}

bool write_silent_audio(const QString& path,
                        const QString& ffmpeg,
                        QString* error) {
  return run_process(
      ffmpeg,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
       QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("anullsrc=r=48000:cl=stereo:d=4"),
       QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path},
      nullptr, error);
}

QVariantList retime_points() {
  return {
      QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                  {QStringLiteral("sourceSeconds"), 3.5},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 2.0},
                  {QStringLiteral("sourceSeconds"), 2.0},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 4.0},
                  {QStringLiteral("sourceSeconds"), 0.5},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
  };
}

bool read_solid_pixel(const QString& ffmpeg,
                      const QString& video_path,
                      double time_seconds,
                      int* red,
                      int* green,
                      int* blue) {
  QStringList arguments{
      QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
      QStringLiteral("error")};
  if (time_seconds > 0.0) {
    arguments << QStringLiteral("-ss") << QString::number(time_seconds, 'f', 6);
  }
  arguments << QStringLiteral("-i") << video_path
            << QStringLiteral("-frames:v") << QStringLiteral("1")
            << QStringLiteral("-vf")
            << QStringLiteral("crop=2:2:80:45,scale=1:1")
            << QStringLiteral("-f") << QStringLiteral("rawvideo")
            << QStringLiteral("-pix_fmt") << QStringLiteral("rgb24")
            << QStringLiteral("pipe:1");
  QByteArray bytes;
  if (!run_process(ffmpeg, arguments, &bytes) ||
      bytes.size() != 3) {
    return false;
  }
  *red = static_cast<unsigned char>(bytes.at(0));
  *green = static_cast<unsigned char>(bytes.at(1));
  *blue = static_cast<unsigned char>(bytes.at(2));
  return true;
}

QVariantMap action_values(const QString& clip_id,
                          const QVariantList& points = retime_points()) {
  return {{QStringLiteral("clipId"), clip_id},
          {QStringLiteral("expectedSourceInSeconds"), 0.0},
          {QStringLiteral("expectedSourceOutSeconds"), 4.0},
          {QStringLiteral("expectedPlayDurationSeconds"), 4.0},
          {QStringLiteral("points"), points}};
}

QVariantList fast_reverse_points(double duration_seconds) {
  return {
      QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                  {QStringLiteral("sourceSeconds"), 3.5},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), duration_seconds},
                  {QStringLiteral("sourceSeconds"), 0.5},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
  };
}

QVariantMap retime_action_values(const QString& clip_id,
                                 double expected_duration,
                                 double target_duration,
                                 const QVariantList& points,
                                 const QString& ripple_policy = QString{}) {
  QVariantMap values{
      {QStringLiteral("clipId"), clip_id},
      {QStringLiteral("expectedSourceInSeconds"), 0.0},
      {QStringLiteral("expectedSourceOutSeconds"), 4.0},
      {QStringLiteral("expectedPlayDurationSeconds"), expected_duration},
      {QStringLiteral("targetPlayDurationSeconds"), target_duration},
      {QStringLiteral("points"), points}};
  if (!ripple_policy.isEmpty()) {
    values.insert(QStringLiteral("ripplePolicy"), ripple_policy);
  }
  return values;
}

QVariantMap clip_from_track(const TimelineDocumentController& controller,
                            const QString& track_id,
                            const QString& clip_id) {
  for (const auto& track_value : controller.timelineTracks()) {
    const auto track = track_value.toMap();
    if (track.value(QStringLiteral("trackId")).toString() != track_id) continue;
    for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("clipId")).toString() == clip_id) return clip;
    }
  }
  return {};
}

bool run() {
  QTemporaryDir app_data;
  QTemporaryDir workspace;
  QTemporaryDir journal_root;
  if (!app_data.isValid() || !workspace.isValid() || !journal_root.isValid()) {
    return fail("isolated app, render, and task directories should be available");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data.path().toUtf8());

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) return fail("FFmpeg should be available for the native render fixture");
  const auto canonical_workspace = QFileInfo(workspace.path()).canonicalFilePath();
  const auto source_path = QDir(canonical_workspace).filePath(QStringLiteral("retime-source.mp4"));
  const auto audio_path = QDir(canonical_workspace).filePath(QStringLiteral("retime-silent.wav"));
  QString process_error;
  if (!write_color_video(source_path, ffmpeg, &process_error) ||
      !write_silent_audio(audio_path, ffmpeg, &process_error)) {
    std::cerr << "could not prepare the retime media sources: "
              << process_error.toStdString() << '\n';
    return false;
  }

  TimelineDocumentController controller;
  controller.setEditAccessPolicy([] { return true; });
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Native Retime Adapter")},
      {QStringLiteral("width"), 160},
      {QStringLiteral("height"), 90},
      {QStringLiteral("frameRate"), 30.0},
  });

  QString video_track_id;
  for (const auto& row_value : controller.timelineTracks()) {
    const auto row = row_value.toMap();
    if (row.value(QStringLiteral("trackKind")).toString() == QStringLiteral("video") &&
        row.value(QStringLiteral("trackRole")).toString() == QStringLiteral("media")) {
      video_track_id = row.value(QStringLiteral("trackId")).toString();
    }
  }
  if (video_track_id.isEmpty()) return fail("the Timeline should expose a stable video track ID");
  const auto audio_track = controller.createTimelineTrackForAutomation(
      QStringLiteral("audio"), QStringLiteral("Retime Test Audio"));
  const auto audio_track_id = audio_track.value(QStringLiteral("trackId")).toString();
  if (!audio_track.value(QStringLiteral("ok")).toBool() || audio_track_id.isEmpty()) {
    return fail("a stable audio lane should be available for the non-video guard");
  }

  const auto first_clip = controller.createTimelineClipForAutomation(
      video_track_id, QStringLiteral("video:retime-source"), source_path,
      QStringLiteral("Retime Source"), 4.0, 0.0, 4.0, 0.0);
  const auto clip_id = first_clip.value(QStringLiteral("createdClipId")).toString();
  const auto following_clip = controller.createTimelineClipForAutomation(
      video_track_id, QStringLiteral("video:retime-following"), source_path,
      QStringLiteral("Following Source"), 4.0, 0.0, 4.0, 4.0);
  const auto audio_clip = controller.createTimelineClipForAutomation(
      audio_track_id, QStringLiteral("audio:retime-guard"), audio_path,
      QStringLiteral("Audio Guard"), 4.0, 0.0, 4.0, 0.0);
  const auto audio_following_clip = controller.createTimelineClipForAutomation(
      audio_track_id, QStringLiteral("audio:retime-following"), audio_path,
      QStringLiteral("Following Audio Guard"), 4.0, 0.0, 4.0, 4.0);
  if (!first_clip.value(QStringLiteral("ok")).toBool() || clip_id.isEmpty() ||
      !following_clip.value(QStringLiteral("ok")).toBool() ||
      !audio_clip.value(QStringLiteral("ok")).toBool() ||
      !audio_following_clip.value(QStringLiteral("ok")).toBool()) {
    std::cerr << "video fixture creation failed: "
              << first_clip.value(QStringLiteral("error")).toString().toStdString() << "; "
              << following_clip.value(QStringLiteral("error")).toString().toStdString() << "; "
              << audio_clip.value(QStringLiteral("error")).toString().toStdString() << "; "
              << audio_following_clip.value(QStringLiteral("error")).toString().toStdString() << '\n';
    return false;
  }
  if (qAbs(controller.totalDurationSecondsValue() - 8.0) > 1.0e-6) {
    return fail("the two source clips should establish an eight-second sequence");
  }

  // Task preparation deliberately cancels background metadata work. Wait for
  // ffprobe to establish the video's actual AAC stream before admitting the
  // task; otherwise the document correctly preserves only a provisional,
  // silent-looking mirror.
  QElapsedTimer audio_metadata_timer;
  audio_metadata_timer.start();
  QVariantMap first_clip_details;
  while (audio_metadata_timer.elapsed() < 30000) {
    QCoreApplication::processEvents();
    first_clip_details = controller.inspectTimelineClipById(clip_id)
                             .value(QStringLiteral("clip")).toMap();
    if (first_clip_details.value(QStringLiteral("mediaAnalysisStatus")).toString() ==
            QStringLiteral("ready") &&
        first_clip_details.value(QStringLiteral("hasAudio")).toBool()) {
      break;
    }
    QThread::msleep(20);
  }
  if (first_clip_details.value(QStringLiteral("mediaAnalysisStatus")).toString() !=
          QStringLiteral("ready") ||
      !first_clip_details.value(QStringLiteral("hasAudio")).toBool()) {
    return fail("ffprobe should finish and recognize the AAC audio stream before task preparation");
  }

  QVariantMap prepared;
  QElapsedTimer preparation_timer;
  preparation_timer.start();
  while (preparation_timer.elapsed() < 30000) {
    prepared = controller.prepareAgentTaskDocumentForAutomation();
    if (!prepared.value(QStringLiteral("ok")).toBool() ||
        prepared.value(QStringLiteral("ready")).toBool()) {
      break;
    }
    QCoreApplication::processEvents();
    QThread::msleep(20);
  }
  if (!prepared.value(QStringLiteral("ok")).toBool() ||
      !prepared.value(QStringLiteral("ready")).toBool() ||
      !controller.acceptAgentTaskDocumentPreparationForAutomation()
           .value(QStringLiteral("ok")).toBool()) {
    return fail("the stable retime fixture should be admitted to a native task");
  }

  QString paired_audio_track_id;
  int video_track_row = -1;
  const auto prepared_tracks = controller.timelineTracks();
  for (int index = 0; index < prepared_tracks.size(); ++index) {
    const auto track = prepared_tracks.at(index).toMap();
    if (track.value(QStringLiteral("trackId")).toString() == video_track_id) {
      video_track_row = index;
    }
    if (track.value(QStringLiteral("trackKind")).toString() == QStringLiteral("audio") &&
        track.value(QStringLiteral("laneIndex")).toInt() == 0) {
      paired_audio_track_id = track.value(QStringLiteral("trackId")).toString();
    }
  }
  first_clip_details = controller.inspectTimelineClipById(clip_id)
                           .value(QStringLiteral("clip")).toMap();
  const auto paired_clip_before = clip_from_track(controller, paired_audio_track_id, clip_id);
  const auto paired_following_before = clip_from_track(
      controller, paired_audio_track_id,
      following_clip.value(QStringLiteral("createdClipId")).toString());
  if (video_track_row < 0 || paired_audio_track_id.isEmpty() ||
      !first_clip_details.value(QStringLiteral("hasAudio")).toBool() ||
      paired_clip_before.isEmpty() || paired_following_before.isEmpty() ||
      qAbs(paired_clip_before.value(QStringLiteral("trimmedDurationSeconds")).toDouble() - 4.0) > 1.0e-6) {
    std::cerr << "paired-audio diagnostics: pairedTrack="
              << paired_audio_track_id.toStdString()
              << " videoTrackRow=" << video_track_row
              << " hasAudio=" << first_clip_details.value(QStringLiteral("hasAudio")).toBool()
              << " provisional=" << first_clip_details.value(QStringLiteral("audioPresenceProvisional")).toBool()
              << " pairedClipEmpty=" << paired_clip_before.isEmpty()
              << " pairedFollowingEmpty=" << paired_following_before.isEmpty()
              << " pairedDuration="
              << paired_clip_before.value(QStringLiteral("trimmedDurationSeconds")).toDouble()
              << "\n";
    for (const auto& track_value : prepared_tracks) {
      const auto track = track_value.toMap();
      std::cerr << "  track=" << track.value(QStringLiteral("trackId")).toString().toStdString()
                << " kind=" << track.value(QStringLiteral("trackKind")).toString().toStdString()
                << " lane=" << track.value(QStringLiteral("laneIndex")).toInt()
                << " clips=" << track.value(QStringLiteral("clips")).toList().size()
                << "\n";
    }
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("the media clip should have a stable paired audio mirror before retiming");
  }

  const auto before_lock_guard = controller.deterministicDocumentHash();
  controller.toggleTrackLocked(video_track_row);
  const auto locked_before_hash = controller.deterministicDocumentHash();
  const auto locked_result = controller.setTimelineClipRetimeForAutomation(
      clip_id, 0.0, 4.0, 4.0, retime_points(), 0.2,
      QStringLiteral("preserveOtherClipStarts"));
  const auto locked_document_hash = controller.deterministicDocumentHash();
  controller.toggleTrackLocked(video_track_row);
  if (locked_result.value(QStringLiteral("ok")).toBool() ||
      !locked_result.value(QStringLiteral("mutationNotApplied")).toBool() ||
      locked_document_hash != locked_before_hash ||
      controller.deterministicDocumentHash() != before_lock_guard) {
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("retime must reject a locked target and leave its native document unchanged");
  }

  const auto original_hash = controller.deterministicDocumentHash();
  const auto stale_source = controller.setTimelineClipRetimeForAutomation(
      clip_id, 0.25, 4.0, 4.0, retime_points());
  const auto stale_duration = controller.setTimelineClipRetimeForAutomation(
      clip_id, 0.0, 4.0, 3.0, retime_points());
  auto incomplete_points = retime_points();
  incomplete_points.removeLast();
  const auto incomplete_window = controller.setTimelineClipRetimeForAutomation(
      clip_id, 0.0, 4.0, 4.0, incomplete_points);
  auto out_of_range_points = retime_points();
  auto out_of_range_first = out_of_range_points.first().toMap();
  out_of_range_first.insert(QStringLiteral("sourceSeconds"), 4.5);
  out_of_range_points[0] = out_of_range_first;
  const auto out_of_range = controller.setTimelineClipRetimeForAutomation(
      clip_id, 0.0, 4.0, 4.0, out_of_range_points);
  const auto generic_animation = controller.setTimelineClipAnimationForAutomation(
      clip_id,
      {QVariantMap{{QStringLiteral("parameter"), QStringLiteral("sourceTime")},
                   {QStringLiteral("keyframes"), retime_points()}}});
  const auto generic_keyframe = controller.editClipKeyframeForAutomation(
      clip_id, QStringLiteral("set"), QStringLiteral("sourceTime"), 1.0, 2.0);
  const auto wrong_media_type = controller.setTimelineClipRetimeForAutomation(
      audio_clip.value(QStringLiteral("createdClipId")).toString(),
      0.0, 4.0, 4.0, retime_points());
  if (stale_source.value(QStringLiteral("ok")).toBool() ||
      stale_duration.value(QStringLiteral("ok")).toBool() ||
      incomplete_window.value(QStringLiteral("ok")).toBool() ||
      out_of_range.value(QStringLiteral("ok")).toBool() ||
      generic_animation.value(QStringLiteral("ok")).toBool() ||
      generic_keyframe.value(QStringLiteral("ok")).toBool() ||
      wrong_media_type.value(QStringLiteral("ok")).toBool() ||
      controller.deterministicDocumentHash() != original_hash) {
    return fail("stale windows, incomplete/out-of-range curves, and generic sourceTime paths should reject without mutation");
  }

  const auto task_baseline_state = controller.agentTaskDocumentState();
  const auto task_baseline_hash = QString::fromLatin1(
      QCryptographicHash::hash(task_baseline_state, QCryptographicHash::Sha256).toHex());
  const auto canonical_journal_root = QDir(journal_root.path()).canonicalPath();
  AgentTaskCoordinator tasks;
  const auto task_id = QStringLiteral("timeline-retime-adapter");
  if (!tasks.begin(canonical_journal_root, task_id)
           .value(QStringLiteral("ok")).toBool() ||
      !tasks.journal()->recordNativeTimelineScope(
           {controller.agentTaskDocumentId()}, {}, canonical_journal_root)
           .value(QStringLiteral("ok")).toBool()) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("the native retime action should use an owned Timeline task resource");
  }

  AgentAppCommandRegistry registry;
  photo_editor::desktop::register_agent_timeline_commands(
      registry, &controller, tasks, [task_id] { return task_id; },
      [](const QString&) { return QVariantMap{{QStringLiteral("ok"), true}}; },
      {}, false);
  const auto command_id = QStringLiteral("timeline.action.setTimelineClipRetime");
  const auto schema_result = registry.actionInputSchema(command_id);
  const auto input_schema = schema_result.value(QStringLiteral("inputSchema")).toMap();
  const auto properties = input_schema.value(QStringLiteral("properties")).toMap();
  if (!schema_result.value(QStringLiteral("ok")).toBool() ||
      !properties.contains(QStringLiteral("clipId")) ||
      !properties.contains(QStringLiteral("expectedPlayDurationSeconds")) ||
      !properties.contains(QStringLiteral("targetPlayDurationSeconds")) ||
      !properties.contains(QStringLiteral("ripplePolicy")) ||
      !properties.contains(QStringLiteral("points")) ||
      properties.contains(QStringLiteral("filePath"))) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("the registered retime action should be typed, guarded, and contain no source path input");
  }

  const auto applied = registry.invoke(command_id, action_values(clip_id));
  const auto after_apply_hash = controller.deterministicDocumentHash();
  const auto following = controller.inspectTimelineClipById(
      following_clip.value(QStringLiteral("createdClipId")).toString())
                             .value(QStringLiteral("clip")).toMap();
  const auto clip_readback = controller.inspectTimelineClipById(clip_id)
                                 .value(QStringLiteral("clip")).toMap();
  const auto points = clip_readback.value(QStringLiteral("retimePoints")).toList();
  if (!applied.value(QStringLiteral("ok")).toBool() ||
      !applied.value(QStringLiteral("changed")).toBool() ||
      applied.value(QStringLiteral("effectivePlayDurationSeconds")).toDouble() != 4.0 ||
      qAbs(following.value(QStringLiteral("startSeconds")).toDouble() - 4.0) > 1.0e-6 ||
      qAbs(controller.totalDurationSecondsValue() - 8.0) > 1.0e-6 ||
      points.size() != 3 ||
      qAbs(points.first().toMap().value(QStringLiteral("sourceSeconds")).toDouble() - 3.5) > 1.0e-6 ||
      qAbs(points.last().toMap().value(QStringLiteral("sourceSeconds")).toDouble() - 0.5) > 1.0e-6) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("the native action should install the exact curve without changing sequence duration or adjacent clip timing");
  }

  const auto idempotent = registry.invoke(command_id, action_values(clip_id));
  if (!idempotent.value(QStringLiteral("ok")).toBool() ||
      !idempotent.value(QStringLiteral("alreadySatisfied")).toBool() ||
      idempotent.value(QStringLiteral("changed")).toBool() ||
      controller.deterministicDocumentHash() != after_apply_hash) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("repeating the same retime curve should be an unchanged idempotent operation");
  }

  const auto journal_steps = tasks.journal()->report()
                                 .value(QStringLiteral("steps")).toList();
  auto completed_retime_actions = 0;
  for (const auto& step_value : journal_steps) {
    const auto step = step_value.toMap();
    if (step.value(QStringLiteral("command")).toString() == command_id &&
        step.value(QStringLiteral("status")).toString() == QStringLiteral("completed") &&
        step.value(QStringLiteral("outcome")).toMap().value(QStringLiteral("ok")).toBool()) {
      ++completed_retime_actions;
    }
  }
  if (completed_retime_actions != 2) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("both changed and idempotent retime requests should have durable task receipts");
  }

  controller.undo();
  if (controller.deterministicDocumentHash() != original_hash) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("one native Undo should restore the source-time curve preimage");
  }
  controller.redo();
  if (controller.deterministicDocumentHash() != after_apply_hash) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("native Redo should restore the same source-time curve");
  }

  const auto saved_copy = QDir(canonical_workspace).filePath(QStringLiteral("retime-copy.json"));
  const auto save_result = controller.saveTimelineCopyForAutomation(saved_copy);
  TimelineDocumentController reopened;
  reopened.setEditAccessPolicy([] { return true; });
  const auto reopened_clip = save_result.value(QStringLiteral("ok")).toBool() &&
                                     reopened.loadTimeline(saved_copy)
                                 ? reopened.inspectTimelineClipById(clip_id)
                                       .value(QStringLiteral("clip")).toMap()
                                 : QVariantMap{};
  const auto reopened_points = reopened_clip.value(QStringLiteral("retimePoints")).toList();
  if (!save_result.value(QStringLiteral("ok")).toBool() || reopened_points.size() != 3 ||
      qAbs(reopened_points.first().toMap().value(QStringLiteral("sourceSeconds")).toDouble() - 3.5) > 1.0e-6 ||
      qAbs(reopened_points.last().toMap().value(QStringLiteral("sourceSeconds")).toDouble() - 0.5) > 1.0e-6) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("native save and reopen should preserve the authored source-time points");
  }

  const auto first_excerpt = QDir(canonical_workspace).filePath(QStringLiteral("reverse-start.mp4"));
  const auto later_excerpt = QDir(canonical_workspace).filePath(QStringLiteral("reverse-later.mp4"));
  QString render_error;
  if (!controller.renderTimelineReviewExcerpt(first_excerpt, 0, 30, &render_error) ||
      !controller.renderTimelineReviewExcerpt(later_excerpt, 90, 120, &render_error)) {
    std::cerr << "native source-time excerpt failed: "
              << render_error.toStdString() << '\n';
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return false;
  }
  int first_red = 0;
  int first_green = 0;
  int first_blue = 0;
  int later_red = 0;
  int later_green = 0;
  int later_blue = 0;
  if (!read_solid_pixel(ffmpeg, first_excerpt, 0.0,
                        &first_red, &first_green, &first_blue) ||
      !read_solid_pixel(ffmpeg, later_excerpt, 0.0,
                        &later_red, &later_green, &later_blue) ||
      first_red < 170 || first_green < 170 || first_blue < 170 ||
      later_green < later_red + 30 || later_green < later_blue + 30) {
    std::cerr << "source-time pixel probe did not show the expected reverse color order: first="
              << first_red << ',' << first_green << ',' << first_blue
              << " later=" << later_red << ',' << later_green << ',' << later_blue << '\n';
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("native excerpt pixels should follow the authored source-time curve rather than the original forward order");
  }

  const auto unchanged_before_duration_change = controller.deterministicDocumentHash();
  const auto missing_ripple_policy = registry.invoke(
      command_id,
      retime_action_values(clip_id, 4.0, 0.2, fast_reverse_points(0.2)));
  const auto overlapping_preserve = registry.invoke(
      command_id,
      retime_action_values(clip_id, 4.0, 5.0, fast_reverse_points(5.0),
                           QStringLiteral("preserveOtherClipStarts")));
  if (missing_ripple_policy.value(QStringLiteral("ok")).toBool() ||
      overlapping_preserve.value(QStringLiteral("ok")).toBool() ||
      controller.deterministicDocumentHash() != unchanged_before_duration_change) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("duration changes should require an explicit ripple policy and reject same-lane overlap without mutation");
  }

  const auto short_window = registry.invoke(
      command_id,
      retime_action_values(clip_id, 4.0, 0.2, fast_reverse_points(0.2),
                           QStringLiteral("preserveOtherClipStarts")));
  const auto after_short_window_hash = controller.deterministicDocumentHash();
  const auto short_clip = controller.inspectTimelineClipById(clip_id)
                              .value(QStringLiteral("clip")).toMap();
  const auto following_after_short = controller.inspectTimelineClipById(
      following_clip.value(QStringLiteral("createdClipId")).toString())
                                         .value(QStringLiteral("clip")).toMap();
  const auto paired_after_short = clip_from_track(controller, paired_audio_track_id, clip_id);
  const auto paired_following_after_short = clip_from_track(
      controller, paired_audio_track_id,
      following_clip.value(QStringLiteral("createdClipId")).toString());
  const auto separate_audio_following_after_short = controller.inspectTimelineClipById(
      audio_following_clip.value(QStringLiteral("createdClipId")).toString())
                                                        .value(QStringLiteral("clip")).toMap();
  if (!short_window.value(QStringLiteral("ok")).toBool() ||
      qAbs(short_window.value(QStringLiteral("previousPlayDurationSeconds")).toDouble() - 4.0) > 1.0e-6 ||
      qAbs(short_window.value(QStringLiteral("effectivePlayDurationSeconds")).toDouble() - 0.2) > 1.0e-6 ||
      short_window.value(QStringLiteral("ripplePolicy")).toString() != QStringLiteral("preserveOtherClipStarts") ||
      qAbs(short_clip.value(QStringLiteral("trimStartSeconds")).toDouble()) > 1.0e-6 ||
      qAbs(short_clip.value(QStringLiteral("trimEndSeconds")).toDouble() - 4.0) > 1.0e-6 ||
      qAbs(short_clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble() - 0.2) > 1.0e-6 ||
      qAbs(following_after_short.value(QStringLiteral("startSeconds")).toDouble() - 4.0) > 1.0e-6 ||
      qAbs(separate_audio_following_after_short.value(QStringLiteral("startSeconds")).toDouble() - 4.0) > 1.0e-6 ||
      qAbs(paired_after_short.value(QStringLiteral("trimmedDurationSeconds")).toDouble() - 0.2) > 1.0e-6 ||
      qAbs(paired_following_after_short.value(QStringLiteral("startSeconds")).toDouble() - 4.0) > 1.0e-6 ||
      qAbs(controller.totalDurationSecondsValue() - 8.0) > 1.0e-6) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("preserving clip starts should shorten only the retimed picture and its paired audio mirror");
  }

  const auto fast_excerpt = QDir(canonical_workspace).filePath(QStringLiteral("steep-reverse.mp4"));
  if (!controller.renderTimelineReviewExcerpt(fast_excerpt, 0, 30, &render_error)) {
    std::cerr << "steep native source-time excerpt failed: "
              << render_error.toStdString() << '\n';
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return false;
  }
  int fast_white_red = 0;
  int fast_white_green = 0;
  int fast_white_blue = 0;
  int fast_blue_red = 0;
  int fast_blue_green = 0;
  int fast_blue_blue = 0;
  int fast_green_red = 0;
  int fast_green_green = 0;
  int fast_green_blue = 0;
  int fast_late_red = 0;
  int fast_late_green = 0;
  int fast_late_blue = 0;
  if (!read_solid_pixel(ffmpeg, fast_excerpt, 0.0,
                        &fast_white_red, &fast_white_green, &fast_white_blue) ||
      !read_solid_pixel(ffmpeg, fast_excerpt, 0.04,
                        &fast_blue_red, &fast_blue_green, &fast_blue_blue) ||
      !read_solid_pixel(ffmpeg, fast_excerpt, 0.08,
                        &fast_green_red, &fast_green_green, &fast_green_blue) ||
      !read_solid_pixel(ffmpeg, fast_excerpt, 0.15,
                        &fast_late_red, &fast_late_green, &fast_late_blue) ||
      fast_white_red < 170 || fast_white_green < 170 || fast_white_blue < 170 ||
      fast_blue_blue < fast_blue_red + 30 || fast_blue_blue < fast_blue_green + 30 ||
      fast_green_green < fast_green_red + 30 || fast_green_green < fast_green_blue + 30 ||
      fast_late_red < fast_late_green + 30 || fast_late_red < fast_late_blue + 30) {
    std::cerr << "15x source-time pixel probe was not white→blue→green→red: first="
              << fast_white_red << ',' << fast_white_green << ',' << fast_white_blue
              << " second=" << fast_blue_red << ',' << fast_blue_green << ',' << fast_blue_blue
              << " third=" << fast_green_red << ',' << fast_green_green << ',' << fast_green_blue
              << " fourth=" << fast_late_red << ',' << fast_late_green << ',' << fast_late_blue << '\n';
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("steep native reverse playback should render source frames in the authored high-rate order");
  }

  controller.undo();
  if (controller.deterministicDocumentHash() != after_apply_hash ||
      qAbs(clip_from_track(controller, paired_audio_track_id, clip_id)
               .value(QStringLiteral("trimmedDurationSeconds")).toDouble() - 4.0) > 1.0e-6) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("Undo should restore the original retime duration and paired audio mirror");
  }
  controller.redo();
  if (controller.deterministicDocumentHash() != after_short_window_hash ||
      qAbs(clip_from_track(controller, paired_audio_track_id, clip_id)
               .value(QStringLiteral("trimmedDurationSeconds")).toDouble() - 0.2) > 1.0e-6) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("Redo should restore the shortened retime duration and paired audio mirror");
  }

  const auto short_saved_copy = QDir(canonical_workspace).filePath(QStringLiteral("retime-short-copy.json"));
  const auto short_save_result = controller.saveTimelineCopyForAutomation(short_saved_copy);
  TimelineDocumentController reopened_short;
  reopened_short.setEditAccessPolicy([] { return true; });
  const auto reopened_short_clip = short_save_result.value(QStringLiteral("ok")).toBool() &&
                                           reopened_short.loadTimeline(short_saved_copy)
                                       ? reopened_short.inspectTimelineClipById(clip_id)
                                             .value(QStringLiteral("clip")).toMap()
                                       : QVariantMap{};
  const auto reopened_short_points = reopened_short_clip.value(QStringLiteral("retimePoints")).toList();
  if (!short_save_result.value(QStringLiteral("ok")).toBool() ||
      qAbs(reopened_short_clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble() - 0.2) > 1.0e-6 ||
      reopened_short_points.size() != 2 ||
      qAbs(reopened_short_points.last().toMap().value(QStringLiteral("localSeconds")).toDouble() - 0.2) > 1.0e-6 ||
      qAbs(clip_from_track(reopened_short, paired_audio_track_id, clip_id)
               .value(QStringLiteral("trimmedDurationSeconds")).toDouble() - 0.2) > 1.0e-6) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("save and reopen should preserve the shortened local retime window and paired audio duration");
  }

  const auto ripple_action = registry.invoke(
      command_id,
      retime_action_values(clip_id, 0.2, 0.3, fast_reverse_points(0.3),
                           QStringLiteral("rippleAllTracksFromOldEnd")));
  const auto ripple_hash = controller.deterministicDocumentHash();
  const auto video_following_after_ripple = controller.inspectTimelineClipById(
      following_clip.value(QStringLiteral("createdClipId")).toString())
                                               .value(QStringLiteral("clip")).toMap();
  const auto audio_following_after_ripple = controller.inspectTimelineClipById(
      audio_following_clip.value(QStringLiteral("createdClipId")).toString())
                                               .value(QStringLiteral("clip")).toMap();
  const auto paired_following_after_ripple = clip_from_track(
      controller, paired_audio_track_id,
      following_clip.value(QStringLiteral("createdClipId")).toString());
  if (!ripple_action.value(QStringLiteral("ok")).toBool() ||
      ripple_action.value(QStringLiteral("ripplePolicy")).toString() != QStringLiteral("rippleAllTracksFromOldEnd") ||
      qAbs(video_following_after_ripple.value(QStringLiteral("startSeconds")).toDouble() - 4.1) > 1.0e-6 ||
      qAbs(audio_following_after_ripple.value(QStringLiteral("startSeconds")).toDouble() - 4.1) > 1.0e-6 ||
      qAbs(paired_following_after_ripple.value(QStringLiteral("startSeconds")).toDouble() - 4.1) > 1.0e-6 ||
      qAbs(controller.totalDurationSecondsValue() - 8.1) > 1.0e-6) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("explicit all-track ripple should shift the later video clip and both audio representations together");
  }
  controller.undo();
  if (controller.deterministicDocumentHash() != after_short_window_hash ||
      qAbs(controller.inspectTimelineClipById(
               following_clip.value(QStringLiteral("createdClipId")).toString())
               .value(QStringLiteral("clip")).toMap()
               .value(QStringLiteral("startSeconds")).toDouble() - 4.0) > 1.0e-6) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("Undo should restore the complete pre-ripple sequence timing");
  }
  controller.redo();
  if (controller.deterministicDocumentHash() != ripple_hash) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("Redo should restore the explicit all-track ripple result");
  }

  const auto reverted = tasks.revert();
  const auto reverted_hash = QString::fromLatin1(
      QCryptographicHash::hash(controller.agentTaskDocumentState(), QCryptographicHash::Sha256).toHex());
  controller.releaseAgentTaskDocumentForAutomation();
  if (!reverted.value(QStringLiteral("ok")).toBool() ||
      reverted_hash != task_baseline_hash) {
    return fail("task revert should restore the exact native retime action baseline");
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  return run() ? 0 : 1;
}

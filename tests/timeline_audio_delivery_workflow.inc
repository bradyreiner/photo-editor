#include <QPair>

bool create_sample_identity_noise_file(
    const QString& ffmpeg,
    const QString& path,
    double duration_seconds) {
  QString error_output;
  return run_process(
      ffmpeg,
      {
          QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("anoisesrc=color=white:amplitude=0.2:sample_rate=48000:duration=%1:seed=20261030")
              .arg(duration_seconds, 0, 'f', 9),
          QStringLiteral("-ac"), QStringLiteral("2"),
          QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path,
      },
      &error_output);
}

bool create_silent_audio_file(
    const QString& ffmpeg,
    const QString& path,
    double duration_seconds) {
  QString error_output;
  return run_process(
      ffmpeg,
      {
          QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000:duration=%1")
              .arg(duration_seconds, 0, 'f', 9),
          QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path,
      },
      &error_output);
}

bool run_sample_accurate_music_stem_fixture(
    const QString& ffmpeg,
    const QString& workspace_path) {
  constexpr auto kSampleRate = 48000;
  constexpr double kSequenceDuration = 6.0;
  constexpr double kCueStart = 2.35;
  constexpr double kCueEnd = 3.860794;
  const auto music_path = QDir(workspace_path).filePath(
      QStringLiteral("sample-clock-music.wav"));
  const auto cue_path = QDir(workspace_path).filePath(
      QStringLiteral("sample-clock-silent-cue.wav"));
  if (!create_sample_identity_noise_file(ffmpeg, music_path, kSequenceDuration) ||
      !create_silent_audio_file(ffmpeg, cue_path, 2.0)) {
    return fail_timeline_queue_path(
        "the deterministic full-length music and silent cue sources should be created");
  }

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Sample Clock Stem" )},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 240},
      {QStringLiteral("frameRate"), 30.0},
  });
  if (!controller.addIndependentAudioTrack(QStringLiteral("music"))) {
    return fail_timeline_queue_path("the sample-clock music bed should have an independent music track");
  }
  controller.addClipFromGallery(
      QStringLiteral("audio:sample-clock-bed"), music_path,
      QStringLiteral("Sample Clock Bed"), kSequenceDuration, QString{});
  if (!wait_for_media_analysis(&controller)) {
    return fail_timeline_queue_path("the deterministic music bed should finish source analysis before the sample-clock render");
  }

  // Put a silent cue on a second music track. Its fractional end creates the
  // two audio cut points without adding a signal to the bed being verified.
  if (!controller.addIndependentAudioTrack(QStringLiteral("music"))) {
    return fail_timeline_queue_path("the silent cue should have a second independent music track");
  }
  controller.seekSequenceSeconds(kCueStart);
  controller.addClipFromGallery(
      QStringLiteral("audio:sample-clock-silent-cue"), cue_path,
      QStringLiteral("Silent Cue"), kCueEnd - kCueStart, QString{});
  if (!wait_for_media_analysis(&controller) ||
      !controller.setDeliverySetting(QStringLiteral("audioStems"), true) ||
      !nearly_equal(controller.totalDurationSecondsValue(), kSequenceDuration, 1.0e-6)) {
    return fail_timeline_queue_path("the fractional silent cue should leave the six-second music stem renderable");
  }

  auto found_bed = false;
  auto found_cue = false;
  for (const auto& clip_value : controller.clips()) {
    const auto clip = clip_value.toMap();
    const auto file_path = clip.value(QStringLiteral("filePath")).toString();
    if (file_path == music_path) {
      found_bed = qAbs(clip.value(QStringLiteral("startSeconds")).toDouble()) < 1.0e-9 &&
                  qAbs(clip.value(QStringLiteral("durationSeconds")).toDouble() -
                       kSequenceDuration) < 1.0e-6;
    } else if (file_path == cue_path) {
      found_cue = qAbs(clip.value(QStringLiteral("startSeconds")).toDouble() -
                       kCueStart) < 1.0e-9 &&
                  qAbs(clip.value(QStringLiteral("durationSeconds")).toDouble() -
                       (kCueEnd - kCueStart)) < 1.0e-6;
    }
  }
  if (!found_bed || !found_cue) {
    return fail_timeline_queue_path(
        "the native timeline should retain the exact 2.35 s cue start and 3.860794 s cue end");
  }

  const auto stem_path = QDir(workspace_path).filePath(
      QStringLiteral("sample-clock-music-stem.wav"));
  QString render_error;
  const auto render_arguments =
      photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
          controller, stem_path, QStringLiteral("stem"), -1.0, -1.0,
          &render_error);
  if (render_arguments.isEmpty()) {
    std::cerr << "sample-clock stem graph: " << render_error.toStdString() << '\n';
    return fail_timeline_queue_path("the native sample-clock stem graph should be generated");
  }
  QString render_stderr;
  if (!run_process(ffmpeg, render_arguments, &render_stderr)) {
    std::cerr << "sample-clock stem ffmpeg: " << render_stderr.toStdString() << '\n';
    return fail_timeline_queue_path("the audio-only sample-clock concat should render the cue fixture");
  }

  const auto decode_pcm24 = [&ffmpeg](const QString& path) {
    return process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-i"), path,
            QStringLiteral("-map"), QStringLiteral("0:a:0"),
            QStringLiteral("-ar"), QString::number(kSampleRate),
            QStringLiteral("-ac"), QStringLiteral("2"),
            QStringLiteral("-c:a"), QStringLiteral("pcm_s24le"),
            QStringLiteral("-f"), QStringLiteral("s24le"), QStringLiteral("pipe:1"),
        });
  };
  const auto expected_pcm = decode_pcm24(music_path);
  const auto actual_pcm = decode_pcm24(stem_path);
  const auto expected_sample_frames = qRound64(kSequenceDuration * kSampleRate);
  const auto expected_bytes = expected_sample_frames * 2 * 3;
  if (expected_pcm.size() != expected_bytes || actual_pcm.size() != expected_bytes) {
    std::cerr << "sample-clock PCM sizes: expected-source=" << expected_pcm.size()
              << " rendered-stem=" << actual_pcm.size()
              << " expected-bytes=" << expected_bytes << '\n';
    return fail_timeline_queue_path(
        "the music source and rendered stem should contain exactly six seconds of stereo 48 kHz PCM");
  }
  if (actual_pcm != expected_pcm) {
    auto differing_frame = 0;
    constexpr auto kSampleFrameBytes = 2 * 3;
    const auto byte_count = qMin(actual_pcm.size(), expected_pcm.size());
    while (differing_frame * kSampleFrameBytes < byte_count &&
           actual_pcm.mid(differing_frame * kSampleFrameBytes, kSampleFrameBytes) ==
               expected_pcm.mid(differing_frame * kSampleFrameBytes, kSampleFrameBytes)) {
      ++differing_frame;
    }
    const auto cue_start_sample = qRound64(kCueStart * kSampleRate);
    const auto cue_end_sample = qRound64(kCueEnd * kSampleRate);
    std::cerr << "sample-clock first differing frame=" << differing_frame
              << " cue-start-sample=" << cue_start_sample
              << " cue-end-sample=" << cue_end_sample
              << " offset-from-start=" << (differing_frame - cue_start_sample)
              << " offset-from-end=" << (differing_frame - cue_end_sample) << '\n';
    return fail_timeline_queue_path(
        "the music bed PCM should remain sample-identical across the 2.35 s and 3.860794 s silent-cue boundaries");
  }
  return true;
}

double decoded_tone_window_db(
    const QString& ffmpeg,
    const QString& file_path,
    int frequency_hz,
    double start_seconds,
    double duration_seconds) {
  const auto output = process_stderr(
      ffmpeg,
      {
          QStringLiteral("-v"), QStringLiteral("info"),
          QStringLiteral("-ss"), QString::number(start_seconds, 'f', 6),
          QStringLiteral("-t"), QString::number(duration_seconds, 'f', 6),
          QStringLiteral("-i"), file_path,
          QStringLiteral("-af"),
          QStringLiteral("bandpass=f=%1:w=40,volumedetect").arg(frequency_hz),
          QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-"),
      });
  const auto marker = QStringLiteral("mean_volume:");
  const auto start = output.lastIndexOf(marker);
  if (start < 0) {
    return -120.0;
  }
  const auto value_start = start + marker.size();
  const auto value_end = output.indexOf(QStringLiteral(" dB"), value_start);
  bool ok = false;
  const auto value = output.mid(value_start, value_end - value_start)
                         .trimmed().toDouble(&ok);
  return ok ? value : -120.0;
}

double probed_format_duration(const QString& ffprobe, const QString& path) {
  bool ok = false;
  const auto duration = process_stdout(
      ffprobe,
      {
          QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
          QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"), path,
      }).toDouble(&ok);
  return ok ? duration : -1.0;
}

bool create_sine_wave_file(
    const QString& ffmpeg,
    const QString& path,
    int frequency_hz,
    double duration_seconds) {
  QString error_output;
  return run_process(
      ffmpeg,
      {
          QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("sine=frequency=%1:sample_rate=48000:duration=%2")
              .arg(frequency_hz)
              .arg(duration_seconds, 0, 'f', 6),
          QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path,
      },
      &error_output);
}

bool create_video_with_short_audio(
    const QString& ffmpeg,
    const QString& path,
    double video_duration_seconds,
    double audio_duration_seconds) {
  QString error_output;
  return run_process(
      ffmpeg,
      {
          QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("color=c=navy:s=320x240:r=30:d=%1")
              .arg(video_duration_seconds, 0, 'f', 6),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("sine=frequency=440:sample_rate=48000:duration=%1")
              .arg(audio_duration_seconds, 0, 'f', 6),
          QStringLiteral("-map"), QStringLiteral("0:v:0"),
          QStringLiteral("-map"), QStringLiteral("1:a:0"),
          QStringLiteral("-t"), QString::number(video_duration_seconds, 'f', 6),
          QStringLiteral("-c:v"), QStringLiteral("libx264"),
          QStringLiteral("-preset"), QStringLiteral("ultrafast"),
          QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
          QStringLiteral("-c:a"), QStringLiteral("aac"),
          QStringLiteral("-b:a"), QStringLiteral("96k"), path,
      },
      &error_output);
}

bool render_fade_fixture(
    const QString& ffmpeg,
    const QString& workspace_path,
    const QString& output_stem,
    bool split_picture_at_unrelated_boundaries) {
  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), output_stem},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 240},
      {QStringLiteral("frameRate"), 30.0},
  });
  if (!split_picture_at_unrelated_boundaries) {
    const auto picture_path = QDir(workspace_path).filePath(output_stem + QStringLiteral("-picture.mp4"));
    if (create_test_video(
            ffmpeg, picture_path, QStringLiteral("navy"), false, 660, 2.0).isEmpty()) {
      return fail_timeline_queue_path("the uninterrupted fade picture fixture should render");
    }
    controller.addClipFromGallery(
        QStringLiteral("video:fade-picture"), picture_path,
        QStringLiteral("Uninterrupted Picture"), 2.0, QString{});
  } else {
    const auto first_path = QDir(workspace_path).filePath(output_stem + QStringLiteral("-picture-a.mp4"));
    const auto second_path = QDir(workspace_path).filePath(output_stem + QStringLiteral("-picture-b.mp4"));
    const auto third_path = QDir(workspace_path).filePath(output_stem + QStringLiteral("-picture-c.mp4"));
    if (create_test_video(
            ffmpeg, first_path, QStringLiteral("navy"), false, 660, 0.25).isEmpty() ||
        create_test_video(
            ffmpeg, second_path, QStringLiteral("purple"), false, 660, 1.0 / 3.0).isEmpty() ||
        create_test_video(
            ffmpeg, third_path, QStringLiteral("teal"), false, 660, 1.40).isEmpty()) {
      return fail_timeline_queue_path("the unrelated picture cut fixtures should render");
    }
    controller.addClipFromGallery(
        QStringLiteral("video:fade-picture-a"), first_path,
        QStringLiteral("Picture A"), 0.25, QString{});
    controller.addClipFromGallery(
        QStringLiteral("video:fade-picture-b"), second_path,
        QStringLiteral("Picture B"), 1.0 / 3.0, QString{});
    controller.addClipFromGallery(
        QStringLiteral("video:fade-picture-c"), third_path,
        QStringLiteral("Picture C"), 1.40, QString{});
  }

  const auto audio_path = QDir(workspace_path).filePath(output_stem + QStringLiteral("-tone.wav"));
  if (!create_sine_wave_file(ffmpeg, audio_path, 440, 2.0)) {
    return fail_timeline_queue_path("the uninterrupted tone used by the fade comparison should render");
  }
  // The last picture insertion leaves the sequence playhead at that clip's
  // start. Return to zero so the comparison bed begins at sequence zero in
  // both the uninterrupted and picture-cut fixtures.
  controller.seekSequenceSeconds(0.0);
  controller.addMusicTrack();
  controller.addClipFromGallery(
      QStringLiteral("audio:fade-tone"), audio_path,
      QStringLiteral("Fade Tone"), 2.0, QString{});

  auto audio_clip_index = -1;
  const auto clips = controller.clips();
  for (auto index = 0; index < clips.size(); ++index) {
    if (clips.at(index).toMap().value(QStringLiteral("filePath")).toString() == audio_path) {
      audio_clip_index = index;
      break;
    }
  }
  if (audio_clip_index < 0) {
    return fail_timeline_queue_path("the fade tone should remain an editable sequence clip");
  }
  controller.selectClip(audio_clip_index);
  controller.setSelectedClipFadeInSeconds(1.0);
  controller.setSelectedClipFadeOutSeconds(1.0);
  const auto faded_clip = controller.clips().value(audio_clip_index).toMap();
  if (qAbs(faded_clip.value(QStringLiteral("fadeInSeconds")).toDouble() - 1.0) > 0.001 ||
      qAbs(faded_clip.value(QStringLiteral("fadeOutSeconds")).toDouble() - 1.0) > 0.001 ||
      !controller.setDeliverySetting(QStringLiteral("audioStems"), true)) {
    return fail_timeline_queue_path("the clip-local fades and music stem should be configured for the comparison");
  }
  const auto output_path = QDir(workspace_path).filePath(output_stem + QStringLiteral(".mp4"));
  if (!controller.renderCurrentTimeline(output_path)) {
    std::cerr << controller.status().toStdString() << '\n';
    return fail_timeline_queue_path("the fade-segmentation comparison should export through the actual timeline renderer");
  }
  return true;
}

bool run_timeline_audio_delivery_workflow(const QString& workspace_path) {
  const auto retained_output_dir = qEnvironmentVariable(
      "PHOTO_EDITOR_TIMELINE_AUDIO_DELIVERY_OUTPUT_DIR").trimmed();
  if (!retained_output_dir.isEmpty() && !QDir().mkpath(retained_output_dir)) {
    return fail_timeline_queue_path("the retained audio delivery diagnostic directory should be writable");
  }
  const auto output_dir = retained_output_dir.isEmpty() ? workspace_path : retained_output_dir;
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
    return fail_timeline_queue_path("FFmpeg and ffprobe should be available for decoded audio delivery proof");
  }
  if (!run_sample_accurate_music_stem_fixture(ffmpeg, output_dir)) {
    return false;
  }

  constexpr double kSequenceDuration = 6.0;
  const auto embedded_video = QDir(output_dir).filePath(QStringLiteral("short-embedded-audio.mp4"));
  const auto music_bed = QDir(output_dir).filePath(QStringLiteral("six-second-music.wav"));
  if (!create_video_with_short_audio(ffmpeg, embedded_video, kSequenceDuration, 2.0) ||
      !create_sine_wave_file(ffmpeg, music_bed, 880, kSequenceDuration)) {
    return fail_timeline_queue_path("the short embedded audio and full-length music bed fixtures should render");
  }
  if (decoded_tone_window_db(ffmpeg, embedded_video, 440, 0.5, 0.4) < -50.0 ||
      decoded_tone_window_db(ffmpeg, embedded_video, 440, 3.0, 0.4) > -80.0) {
    return fail_timeline_queue_path("the source fixture should contain two seconds of embedded tone followed by silence");
  }

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Full Length Audio Mix")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 240},
      {QStringLiteral("frameRate"), 30.0},
  });

  auto dialogue_track_row = -1;
  const auto initial_tracks = controller.sequenceTracks();
  for (auto row = 0; row < initial_tracks.size(); ++row) {
    const auto track = initial_tracks.at(row).toMap();
    if (track.value(QStringLiteral("trackKind")).toString() == QStringLiteral("audio") &&
        track.value(QStringLiteral("audioRole")).toString() == QStringLiteral("dialogue")) {
      dialogue_track_row = row;
      break;
    }
  }
  if (dialogue_track_row < 0 ||
      !controller.setTrackAudioSetting(
          dialogue_track_row, QStringLiteral("audioBus"), QStringLiteral("dialogue"))) {
    return fail_timeline_queue_path("the default Program audio track should be assigned to the dialogue bus");
  }

  // Link the embedded camera audio to the default Program/dialogue lane before
  // creating the separate music lane. Adding a music lane first would make it
  // the active audio patch and route the video's embedded audio into music.
  controller.addClipFromGallery(
      QStringLiteral("video:short-embedded"), embedded_video,
      QStringLiteral("Six Second Picture"), kSequenceDuration, QString{});

  const auto embedded_audio_ready = [&]() {
    if (controller.mediaAnalysisBusy()) {
      return false;
    }
    for (const auto& value : controller.clips()) {
      const auto clip = value.toMap();
      if (clip.value(QStringLiteral("filePath")).toString() != embedded_video) {
        continue;
      }
      return clip.value(QStringLiteral("mediaAnalysisStatus")).toString() ==
                 QStringLiteral("ready") &&
             clip.value(QStringLiteral("hasAudio")).toBool() &&
             clip.value(QStringLiteral("audioLinked")).toBool() &&
             !clip.value(QStringLiteral("durationProvisional")).toBool();
    }
    return false;
  };
  QElapsedTimer embedded_audio_timer;
  embedded_audio_timer.start();
  while (!embedded_audio_ready() && embedded_audio_timer.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }

  auto embedded_clip = QVariantMap{};
  for (const auto& value : controller.clips()) {
    const auto clip = value.toMap();
    if (clip.value(QStringLiteral("filePath")).toString() == embedded_video) {
      embedded_clip = clip;
      break;
    }
  }
  const auto embedded_audio_track_id =
      embedded_clip.value(QStringLiteral("audioTrackId")).toString();
  auto linked_audio_mirror_ready = false;
  for (const auto& track_value : controller.sequenceTracks()) {
    const auto track = track_value.toMap();
    if (track.value(QStringLiteral("trackId")).toString() != embedded_audio_track_id ||
        track.value(QStringLiteral("trackKind")).toString() != QStringLiteral("audio")) {
      continue;
    }
    for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
      const auto mirror = clip_value.toMap();
      if (mirror.value(QStringLiteral("clipId")).toString() ==
              embedded_clip.value(QStringLiteral("clipId")).toString() &&
          mirror.value(QStringLiteral("hasAudio")).toBool() &&
          mirror.value(QStringLiteral("mediaAnalysisStatus")).toString() ==
              QStringLiteral("ready")) {
        linked_audio_mirror_ready = true;
        break;
      }
    }
    break;
  }
  if (!embedded_audio_ready() || !linked_audio_mirror_ready) {
    std::cerr << "Embedded audio analysis diagnostic: status="
              << embedded_clip.value(QStringLiteral("mediaAnalysisStatus")).toString().toStdString()
              << " hasAudio=" << embedded_clip.value(QStringLiteral("hasAudio")).toBool()
              << " audioLinked=" << embedded_clip.value(QStringLiteral("audioLinked")).toBool()
              << " provisional=" << embedded_clip.value(QStringLiteral("durationProvisional")).toBool()
              << " audioTrackId=" << embedded_audio_track_id.toStdString()
              << " mediaBusy=" << controller.mediaAnalysisBusy()
              << " output=" << output_dir.toStdString() << '\n';
    return fail_timeline_queue_path("the linked camera-audio lane should finish production media analysis before export");
  }
  controller.addMusicTrack();
  controller.addClipFromGallery(
      QStringLiteral("audio:six-second-music"), music_bed,
      QStringLiteral("Six Second Music"), kSequenceDuration, QString{});
  auto dialogue_bus_found = false;
  auto embedded_mirror_routed_to_dialogue = false;
  auto music_bus_found = false;
  for (const auto& value : controller.sequenceTracks()) {
    const auto track = value.toMap();
    const auto track_id = track.value(QStringLiteral("trackId")).toString();
    const auto bus = track.value(QStringLiteral("audioBus")).toString();
    dialogue_bus_found |= track_id == embedded_audio_track_id && bus == QStringLiteral("dialogue");
    if (track_id == embedded_audio_track_id && bus == QStringLiteral("dialogue")) {
      for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
        const auto mirror = clip_value.toMap();
        embedded_mirror_routed_to_dialogue |=
            mirror.value(QStringLiteral("clipId")).toString() ==
                embedded_clip.value(QStringLiteral("clipId")).toString() &&
            mirror.value(QStringLiteral("hasAudio")).toBool() &&
            mirror.value(QStringLiteral("mediaAnalysisStatus")).toString() ==
                QStringLiteral("ready");
      }
    }
    music_bus_found |= bus == QStringLiteral("music") &&
                       track.value(QStringLiteral("clipCount")).toInt() > 0;
  }
  if (embedded_audio_track_id.isEmpty() || !dialogue_bus_found ||
      !embedded_mirror_routed_to_dialogue || !music_bus_found) {
    return fail_timeline_queue_path("the Program source audio and independent music bed should use separate dialogue and music buses");
  }
  if (!controller.setDeliverySetting(QStringLiteral("audioStems"), true) ||
      !nearly_equal(controller.totalDurationSecondsValue(), kSequenceDuration, 0.02)) {
    return fail_timeline_queue_path("the six-second sequence should include embedded picture audio and a full-length music stem");
  }

  const auto master_path = QDir(output_dir).filePath(QStringLiteral("full-length-mix.mp4"));
  if (!controller.renderCurrentTimeline(master_path)) {
    std::cerr << controller.status().toStdString() << '\n';
    return fail_timeline_queue_path("the six-second audio delivery should render through the real export graph");
  }
  const auto stem_root = QDir(output_dir).filePath(QStringLiteral("full-length-mix-stems"));
  const auto dialogue_stem = QDir(stem_root).filePath(QStringLiteral("dialogue.wav"));
  const auto music_stem = QDir(stem_root).filePath(QStringLiteral("music.wav"));
  const auto master_duration = probed_format_duration(ffprobe, master_path);
  const auto dialogue_duration = probed_format_duration(ffprobe, dialogue_stem);
  const auto music_duration = probed_format_duration(ffprobe, music_stem);
  if (!nearly_equal(master_duration, kSequenceDuration, 0.12) ||
      !nearly_equal(dialogue_duration, kSequenceDuration, 0.02) ||
      !nearly_equal(music_duration, kSequenceDuration, 0.02)) {
    std::cerr << "Audio duration diagnostic: master=" << master_duration
              << " dialogue=" << dialogue_duration << " music=" << music_duration
              << " masterBytes=" << QFileInfo(master_path).size()
              << " dialogueBytes=" << QFileInfo(dialogue_stem).size()
              << " musicBytes=" << QFileInfo(music_stem).size()
              << " output=" << output_dir.toStdString() << '\n';
    return fail_timeline_queue_path("the master and both bus stems should retain the complete six-second sequence duration");
  }
  const auto master_music_tail_db =
      decoded_tone_window_db(ffmpeg, master_path, 880, 4.5, 0.8);
  const auto music_stem_tail_db =
      decoded_tone_window_db(ffmpeg, music_stem, 880, 4.5, 0.8);
  const auto dialogue_embedded_tone_db =
      decoded_tone_window_db(ffmpeg, dialogue_stem, 440, 0.5, 0.5);
  const auto dialogue_silent_tail_db =
      decoded_tone_window_db(ffmpeg, dialogue_stem, 440, 4.5, 0.5);
  if (master_music_tail_db < -40.0 || music_stem_tail_db < -40.0 ||
      dialogue_embedded_tone_db < -50.0 || dialogue_silent_tail_db > -80.0) {
    std::cerr << "Decoded audio diagnostic: master880@4.5=" << master_music_tail_db
              << "dB music880@4.5=" << music_stem_tail_db
              << "dB dialogue440@0.5=" << dialogue_embedded_tone_db
              << "dB dialogue440@4.5=" << dialogue_silent_tail_db
              << "dB output=" << output_dir.toStdString() << '\n';
    return fail_timeline_queue_path("decoded master and bus stems should retain the music after embedded audio ends and preserve the embedded source's true silent tail");
  }

  if (!render_fade_fixture(
          ffmpeg, output_dir, QStringLiteral("fade-uninterrupted"), false) ||
      !render_fade_fixture(
          ffmpeg, output_dir, QStringLiteral("fade-with-picture-cuts"), true)) {
    return false;
  }
  const auto baseline_music = QDir(output_dir).filePath(
      QStringLiteral("fade-uninterrupted-stems/music.wav"));
  const auto segmented_music = QDir(output_dir).filePath(
      QStringLiteral("fade-with-picture-cuts-stems/music.wav"));
  for (const auto& window : {QPair<double, double>{0.20, 0.08},
                             QPair<double, double>{0.29, 0.08},
                             QPair<double, double>{0.62, 0.08},
                             QPair<double, double>{1.20, 0.12},
                             QPair<double, double>{1.50, 0.12}}) {
    const auto baseline_db = decoded_tone_window_db(
        ffmpeg, baseline_music, 440, window.first, window.second);
    const auto segmented_db = decoded_tone_window_db(
        ffmpeg, segmented_music, 440, window.first, window.second);
    if (baseline_db < -80.0 || segmented_db < -80.0 ||
        qAbs(baseline_db - segmented_db) > 1.0) {
      std::cerr << "fade envelope dB at " << window.first
                << "s baseline=" << baseline_db
                << " segmented=" << segmented_db << '\n';
      return fail_timeline_queue_path("unrelated picture cuts should preserve the decoded clip-local fade envelope within one dB");
    }
  }
  return true;
}

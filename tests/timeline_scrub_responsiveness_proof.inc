struct TimelineScrubFrameSwapSample final {
  int phase = 0;
  qint64 timestamp_ns = 0;
};

QJsonObject timeline_scrub_frame_swaps(
    const std::vector<TimelineScrubFrameSwapSample>& samples,
    const int phase) {
  std::vector<qint64> timestamps;
  for (const auto& sample : samples)
    if (sample.phase == phase) timestamps.push_back(sample.timestamp_ns);
  std::sort(timestamps.begin(), timestamps.end());
  std::vector<double> gaps_ms;
  for (std::size_t index = 1; index < timestamps.size(); ++index)
    gaps_ms.push_back(static_cast<double>(timestamps[index] - timestamps[index - 1]) / 1.0e6);
  return {{QStringLiteral("frameSwappedSignalCount"), static_cast<qint64>(timestamps.size())},
          {QStringLiteral("interFrameGapMs"), sample_summary_json(gaps_ms)}};
}

QString write_timeline_scrub_receipt(
    const TimelineScrollBenchmarkConfig& config,
    const QJsonObject& receipt) {
  const auto stamp = QDateTime::currentDateTimeUtc().toString(
      QStringLiteral("yyyyMMdd'T'HHmmsszzz"));
  const auto path = QDir(config.cache_dir).filePath(
      QStringLiteral("timeline-scrub-responsiveness-%1-%2.json")
          .arg(stamp).arg(QCoreApplication::applicationPid()));
  QSaveFile file(path);
  const auto bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    return {};
  return path;
}

bool run_timeline_scrub_responsiveness_proof(
    QObject* root,
    QQuickWindow* window,
    QObject* workspace,
    photo_editor::desktop::TimelineDocumentController& document,
    photo_editor::desktop::TimelinePlaybackController& playback,
    const TimelineScrollBenchmarkConfig& config,
    const QString& source_sha256_before,
    QString* error) {
  constexpr int kRequests = 48;
  constexpr int kIntervalMs = 40;
  constexpr int kPlayMs = 2000;
  constexpr int kBudgetMs = 12000;
  if (error != nullptr) error->clear();

  const auto hash_before = document.deterministicDocumentHash();
  const auto source_before = sha256_file(config.project_path);
  const auto copy_before = sha256_file(config.load_path);
  const auto tracks_before = document.timelineTracks().size();
  const auto clips_before = document.clipCount();
  const auto duration = document.totalDurationSecondsValue();
  const auto dirty_before = document.dirty();
  const auto identity_before = hash_before == config.expected_document_hash &&
      source_before == source_sha256_before &&
      source_before.compare(config.expected_file_sha256, Qt::CaseInsensitive) == 0 &&
      copy_before == config.working_copy_sha256 && tracks_before == config.expected_tracks &&
      clips_before == config.expected_clips &&
      std::abs(duration - config.expected_duration_seconds) <= 1.0e-6;

  auto* viewport = find_visible_named_item(workspace, QStringLiteral("timelineViewport"));
  const auto method = [workspace](const char* signature) {
    return workspace != nullptr && workspace->metaObject()->indexOfMethod(signature) >= 0;
  };
  const auto gesture_idle = workspace != nullptr &&
      !workspace->property("timelineSeekGestureActive").toBool() &&
      !workspace->property("timelineSeekCommitPending").toBool();
  QString correctness_error;
  if (!identity_before) {
    correctness_error = QStringLiteral("Timeline scrub proof refused a document/source identity mismatch.");
  } else if (root == nullptr || window == nullptr || workspace == nullptr || viewport == nullptr ||
             !method("beginTimelineSeekGesture()") ||
             !method("requestTimelineSeek(QVariant)") ||
             !method("finishTimelineSeekGesture()") || !gesture_idle) {
    correctness_error = QStringLiteral(
        "Timeline scrub proof requires the visible Timeline viewport, native seek methods, and idle seek state.");
  }

  const auto original_playhead = document.sequencePlayheadSeconds();
  const auto original_playing = document.sequencePlaying();
  const auto original_selection = document.selectedClipIds();
  const auto original_interaction = playback.interactionActive();
  const auto original_x = viewport == nullptr ? 0.0 : viewport->property("contentX").toDouble();
  const auto original_y = viewport == nullptr ? 0.0 : viewport->property("contentY").toDouble();
  const auto save_property = [workspace](const char* name) {
    return workspace == nullptr ? QVariant{} : workspace->property(name);
  };
  const auto zoom = save_property("timelineZoom");
  const auto gap_ids = save_property("selectedGapIds");
  const auto focus_mode = save_property("timelineTrackFocusMode");
  const auto focus_expanded = save_property("timelineTrackFocusExpandedIds");
  const auto focus_heights = save_property("timelineTrackFocusHeightOverrides");
  const auto preview_seconds = save_property("interactionPlayheadPreviewSeconds");
  const auto pending_seconds = save_property("pendingTimelineSeekSeconds");
  const auto timeline_seeking = save_property("timelineSeeking");

  if (!correctness_error.isEmpty()) {
    const auto path = write_timeline_scrub_receipt(config, {
        {QStringLiteral("schema"), QStringLiteral("actual-timeline-scrub-responsiveness:v1")},
        {QStringLiteral("ok"), false}, {QStringLiteral("readOnly"), true},
        {QStringLiteral("saveCalled"), false},
        {QStringLiteral("identityMatchesBefore"), identity_before},
        {QStringLiteral("sourceSha256Before"), source_before},
        {QStringLiteral("workingCopySha256Before"), copy_before},
        {QStringLiteral("documentHashBefore"), hash_before},
        {QStringLiteral("correctnessError"), correctness_error}});
    if (error != nullptr)
      *error = path.isEmpty()
          ? QStringLiteral("Timeline scrub proof could not write its preflight receipt.")
          : correctness_error;
    return false;
  }

  std::vector<double> dispatch_ms, lateness_ms, scrub_cadence_ms, play_cadence_ms;
  std::vector<double> play_lateness_ms, play_arrival_ms;
  std::vector<TimelineScrubFrameSwapSample> frame_swaps;
  std::mutex frame_mutex;
  std::atomic<int> phase{0};
  std::atomic<qint64> scrub_frame_signals{0}, play_frame_signals{0};
  auto scrub_metrics_before = QVariantMap{};
  auto scrub_metrics_after = QVariantMap{};
  auto play_metrics_before = QVariantMap{};
  auto play_metrics_after = QVariantMap{};
  auto input_count = 0;
  auto scrub_complete = false;
  auto play_complete = false;
  auto budget_expired = false;
  auto play_skipped = false;
  auto input_valid = true;
  auto play_started = false;
  auto play_source_has_video = false;
  auto media_ready = false;
  auto play_start = 0.0;
  auto play_end = 0.0;
  auto play_elapsed_ms = 0.0;
  auto play_sample_index = 0;
  auto play_start_dispatch_ms = -1.0;
  auto play_pause_dispatch_ms = -1.0;
  auto frame_source_start = QString{};
  auto frame_source_end = QString{};
  auto active_loop = static_cast<QEventLoop*>(nullptr);
  QElapsedTimer total_timer;
  QObject connections;
  const auto swap_connection = QObject::connect(
      window, &QQuickWindow::frameSwapped, &connections, [&]() {
        const auto sample_phase = phase.load(std::memory_order_relaxed);
        if (sample_phase == 0) return;
        const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch()).count();
        const std::lock_guard<std::mutex> lock(frame_mutex);
        if (phase.load(std::memory_order_relaxed) == sample_phase)
          frame_swaps.push_back({sample_phase, stamp});
      }, Qt::DirectConnection);
  const auto frame_connection = QObject::connect(
      &playback, &photo_editor::desktop::TimelinePlaybackController::processedFrameSourceChanged,
      &connections, [&]() {
        const auto sample_phase = phase.load(std::memory_order_relaxed);
        if (sample_phase == 1) scrub_frame_signals.fetch_add(1, std::memory_order_relaxed);
        if (sample_phase == 2) play_frame_signals.fetch_add(1, std::memory_order_relaxed);
      });
  QTimer watchdog;
  watchdog.setSingleShot(true);
  const auto watchdog_connection = QObject::connect(&watchdog, &QTimer::timeout, &connections, [&]() {
    budget_expired = true;
    if (active_loop != nullptr) active_loop->quit();
  });
  total_timer.start();
  watchdog.start(kBudgetMs);

  scrub_metrics_before = playback.frameProcessingMetrics();
  phase.store(1, std::memory_order_relaxed);
  auto scrub_loop = QEventLoop{};
  active_loop = &scrub_loop;
  input_valid = QMetaObject::invokeMethod(workspace, "beginTimelineSeekGesture");
  QTimer seek_timer;
  seek_timer.setSingleShot(true);
  seek_timer.setTimerType(Qt::PreciseTimer);
  const auto seek_connection = QObject::connect(&seek_timer, &QTimer::timeout, &scrub_loop, [&]() {
    const auto elapsed = static_cast<double>(total_timer.nsecsElapsed()) / 1.0e6;
    if (elapsed >= kBudgetMs) {
      budget_expired = true;
      scrub_loop.quit();
      return;
    }
    const auto index = input_count;
    const auto order_index = index < kRequests / 2 ? index : kRequests - 1 - index;
    const auto end_seconds = std::min(12.0, duration);
    const auto start_seconds = std::min(0.1, end_seconds);
    const auto target = start_seconds + (end_seconds - start_seconds) *
        static_cast<double>(order_index) / static_cast<double>(kRequests / 2 - 1);
    lateness_ms.push_back(elapsed - static_cast<double>(index + 1) * kIntervalMs);
    if (!lateness_ms.empty() && index > 0) {
      const auto previous_due = static_cast<double>(index) * kIntervalMs;
      const auto previous_arrival = previous_due + lateness_ms[lateness_ms.size() - 2];
      scrub_cadence_ms.push_back(elapsed - previous_arrival);
    }
    QElapsedTimer dispatch_timer;
    dispatch_timer.start();
    const auto invoked = QMetaObject::invokeMethod(
        workspace, "requestTimelineSeek", Q_ARG(QVariant, QVariant(target)));
    dispatch_ms.push_back(static_cast<double>(dispatch_timer.nsecsElapsed()) / 1.0e6);
    const auto accepted = invoked &&
        std::abs(workspace->property("pendingTimelineSeekSeconds").toDouble() - target) <= 1.0e-6 &&
        std::abs(workspace->property("interactionPlayheadPreviewSeconds").toDouble() - target) <= 1.0e-6;
    if (!accepted) {
      input_valid = false;
      correctness_error = QStringLiteral("Timeline scrub seek request %1 was not accepted.").arg(index + 1);
      scrub_loop.quit();
      return;
    }
    ++input_count;
    if (input_count == kRequests) {
      scrub_complete = true;
      scrub_loop.quit();
      return;
    }
    const auto next_due = static_cast<double>(input_count + 1) * kIntervalMs;
    seek_timer.start(std::max(0, static_cast<int>(std::ceil(
        next_due - static_cast<double>(total_timer.nsecsElapsed()) / 1.0e6))));
  });
  if (input_valid) {
    seek_timer.start(kIntervalMs);
    scrub_loop.exec();
  } else {
    correctness_error = QStringLiteral("Timeline scrub proof could not begin the seek gesture.");
  }
  seek_timer.stop();
  QObject::disconnect(seek_connection);
  if (!QMetaObject::invokeMethod(workspace, "finishTimelineSeekGesture")) {
    input_valid = false;
    correctness_error = QStringLiteral("Timeline scrub proof could not finish the seek gesture.");
  }
  process_layout_events(2, "timeline-scrub-proof-settle");
  phase.store(0, std::memory_order_relaxed);
  active_loop = nullptr;
  scrub_metrics_after = playback.frameProcessingMetrics();

  auto play_duration_ms = std::max(0, kBudgetMs - static_cast<int>(total_timer.elapsed()));
  if (!budget_expired && play_duration_ms > 0) {
    play_duration_ms = std::min(kPlayMs, play_duration_ms);
    phase.store(2, std::memory_order_relaxed);
    play_start = std::min(0.2, std::max(0.0, duration - play_duration_ms / 1000.0 - 0.05));
    playback.seekSequenceSeconds(play_start);
    play_source_has_video = playback.hasVideoSource();
    media_ready = playback.mediaReady();
    frame_source_start = playback.processedFrameSource();
    play_metrics_before = playback.frameProcessingMetrics();
    QElapsedTimer play_timer;
    play_timer.start();
    QElapsedTimer start_dispatch;
    start_dispatch.start();
    playback.play();
    play_start_dispatch_ms = static_cast<double>(start_dispatch.nsecsElapsed()) / 1.0e6;
    play_started = document.sequencePlaying() || playback.playbackStartPending();
    const auto play_sequence_start = document.sequencePlayheadSeconds();
    auto play_loop = QEventLoop{};
    active_loop = &play_loop;
    QTimer sample_timer;
    sample_timer.setTimerType(Qt::PreciseTimer);
    sample_timer.setInterval(50);
    const auto sample_connection = QObject::connect(&sample_timer, &QTimer::timeout, &play_loop, [&]() {
      const auto elapsed = static_cast<double>(play_timer.nsecsElapsed()) / 1.0e6;
      if (elapsed >= play_duration_ms) {
        play_complete = true;
        play_loop.quit();
      } else {
        ++play_sample_index;
        play_lateness_ms.push_back(elapsed - play_sample_index * 50.0);
        if (!play_arrival_ms.empty())
          play_cadence_ms.push_back(elapsed - play_arrival_ms.back());
        play_arrival_ms.push_back(elapsed);
      }
    });
    sample_timer.start();
    play_loop.exec();
    play_elapsed_ms = static_cast<double>(play_timer.nsecsElapsed()) / 1.0e6;
    sample_timer.stop();
    QObject::disconnect(sample_connection);
    QElapsedTimer pause_dispatch;
    pause_dispatch.start();
    playback.pause();
    play_pause_dispatch_ms = static_cast<double>(pause_dispatch.nsecsElapsed()) / 1.0e6;
    play_start = play_sequence_start;
    play_end = document.sequencePlayheadSeconds();
    play_metrics_after = playback.frameProcessingMetrics();
    frame_source_end = playback.processedFrameSource();
    phase.store(0, std::memory_order_relaxed);
    active_loop = nullptr;
  } else {
    play_skipped = true;
  }

  watchdog.stop();
  QObject::disconnect(watchdog_connection);
  playback.pause();
  playback.seekSequenceSeconds(original_playhead);
  document.selectClipsByIds(original_selection);
  playback.setInteractionActive(original_interaction);
  workspace->setProperty("timelineSeeking", timeline_seeking);
  workspace->setProperty("interactionPlayheadPreviewSeconds", preview_seconds);
  workspace->setProperty("pendingTimelineSeekSeconds", pending_seconds);
  workspace->setProperty("selectedGapIds", gap_ids);
  workspace->setProperty("timelineTrackFocusMode", focus_mode);
  workspace->setProperty("timelineTrackFocusExpandedIds", focus_expanded);
  workspace->setProperty("timelineTrackFocusHeightOverrides", focus_heights);
  workspace->setProperty("timelineZoom", zoom);
  process_layout_events(4, "timeline-scrub-proof-restore");
  viewport->setProperty("contentX", original_x);
  viewport->setProperty("contentY", original_y);
  workspace->setProperty("selectedGapIds", gap_ids);
  workspace->setProperty("timelineTrackFocusMode", focus_mode);
  workspace->setProperty("timelineTrackFocusExpandedIds", focus_expanded);
  workspace->setProperty("timelineTrackFocusHeightOverrides", focus_heights);
  workspace->setProperty("timelineZoom", zoom);
  if (original_playing) playback.play();
  phase.store(0, std::memory_order_relaxed);
  QObject::disconnect(swap_connection);
  QObject::disconnect(frame_connection);

  const auto source_after = sha256_file(config.project_path);
  const auto copy_after = sha256_file(config.load_path);
  const auto hash_after = document.deterministicDocumentHash();
  const auto identity_after = hash_after == config.expected_document_hash &&
      source_after == source_sha256_before &&
      source_after.compare(config.expected_file_sha256, Qt::CaseInsensitive) == 0 &&
      copy_after == config.working_copy_sha256 &&
      document.timelineTracks().size() == config.expected_tracks &&
      document.clipCount() == config.expected_clips &&
      std::abs(document.totalDurationSecondsValue() - config.expected_duration_seconds) <= 1.0e-6 &&
      document.dirty() == dirty_before;
  const auto playback_restored = original_playing
      ? document.sequencePlaying() || playback.playbackStartPending()
      : !document.sequencePlaying() && !playback.playbackStartPending();
  const auto state_restored = document.sequencePlayheadSeconds() == original_playhead &&
      document.selectedClipIds() == original_selection && playback_restored &&
      playback.interactionActive() == original_interaction &&
      std::abs(viewport->property("contentX").toDouble() - original_x) <= 0.5 &&
      std::abs(viewport->property("contentY").toDouble() - original_y) <= 0.5 &&
      std::abs(workspace->property("timelineZoom").toDouble() - zoom.toDouble()) <= 1.0e-6;
  if (!identity_after) {
    correctness_error = QStringLiteral("Timeline scrub proof changed pinned document/source identity.");
  } else if (!state_restored) {
    correctness_error = QStringLiteral("Timeline scrub proof could not restore playback, selection, or view state.");
  }

  auto frame_snapshot = std::vector<TimelineScrubFrameSwapSample>{};
  {
    const std::lock_guard<std::mutex> lock(frame_mutex);
    frame_snapshot = frame_swaps;
  }
  const auto sample_count = [](const QVariantMap& metrics) {
    return metrics.value(QStringLiteral("sampleCount")).toLongLong();
  };
  const auto frame_delta = [&sample_count](const QVariantMap& before, const QVariantMap& after) {
    return after.value(QStringLiteral("sourceKey")) == before.value(QStringLiteral("sourceKey"))
        ? std::max<qint64>(0, sample_count(after) - sample_count(before)) : sample_count(after);
  };
  const auto scrub_frames = frame_delta(scrub_metrics_before, scrub_metrics_after);
  const auto play_frames = frame_delta(play_metrics_before, play_metrics_after);
  const auto scrub = QJsonObject{
      {QStringLiteral("requestedInputCount"), kRequests},
      {QStringLiteral("dispatchedInputCount"), input_count},
      {QStringLiteral("completed"), scrub_complete},
      {QStringLiteral("requestIntervalMs"), kIntervalMs},
      {QStringLiteral("directionOrder"), QJsonArray{QStringLiteral("forward"), QStringLiteral("reverse")}},
      {QStringLiteral("requestedWindowSeconds"),
       std::min(12.0, duration) - std::min(0.1, duration)},
      {QStringLiteral("inputDispatchMs"), sample_summary_json(dispatch_ms)},
      {QStringLiteral("timerArrivalLatenessMs"), sample_summary_json(lateness_ms)},
      {QStringLiteral("eventLoopCadenceMs"), sample_summary_json(scrub_cadence_ms)},
      {QStringLiteral("processedFrameSampleCountDelta"), scrub_frames},
      {QStringLiteral("processedFrameSourceKey"), scrub_metrics_after.value(QStringLiteral("sourceKey")).toString()},
      {QStringLiteral("processedFrameSourceChangedSignalCount"), scrub_frame_signals.load()},
      {QStringLiteral("uiFrameSwaps"), timeline_scrub_frame_swaps(frame_snapshot, 1)}};
  const auto play = QJsonObject{
      {QStringLiteral("requestedDurationMs"), kPlayMs},
      {QStringLiteral("measuredDurationMs"), play_elapsed_ms},
      {QStringLiteral("completed"), play_complete},
      {QStringLiteral("skippedForBudget"), play_skipped},
      {QStringLiteral("started"), play_started},
      {QStringLiteral("startDispatchMs"), play_start_dispatch_ms},
      {QStringLiteral("pauseDispatchMs"), play_pause_dispatch_ms},
      {QStringLiteral("sequenceStartSeconds"), play_start},
      {QStringLiteral("sequenceEndSeconds"), play_end},
      {QStringLiteral("sequenceAdvancedSeconds"), play_end - play_start},
      {QStringLiteral("eventLoopSampleCount"), play_sample_index},
      {QStringLiteral("eventLoopCadenceMs"), sample_summary_json(play_cadence_ms)},
      {QStringLiteral("timerArrivalLatenessMs"), sample_summary_json(play_lateness_ms)},
      {QStringLiteral("videoSourcePresentAtStart"), play_source_has_video},
      {QStringLiteral("processedFrameSampleInterpretation"),
       QStringLiteral("sampleCount includes processed video or scene frames; use videoSourcePresentAtStart and sourceKey to distinguish source context")},
      {QStringLiteral("mediaReadyAtStart"), media_ready},
      {QStringLiteral("processedFrameSourceAtStart"), frame_source_start},
      {QStringLiteral("processedFrameSourceAtEnd"), frame_source_end},
      {QStringLiteral("processedFrameSampleCountDelta"), play_frames},
      {QStringLiteral("processedFrameSourceKey"), play_metrics_after.value(QStringLiteral("sourceKey")).toString()},
      {QStringLiteral("processedFrameSourceChangedSignalCount"), play_frame_signals.load()},
      {QStringLiteral("uiFrameSwaps"), timeline_scrub_frame_swaps(frame_snapshot, 2)}};
  auto receipt = QJsonObject{
      {QStringLiteral("schema"), QStringLiteral("actual-timeline-scrub-responsiveness:v1")},
      {QStringLiteral("readOnly"), true},
      {QStringLiteral("saveCalled"), false},
      {QStringLiteral("measurementScope"),
       QStringLiteral("actual QML seek dispatch, event-loop timing, processed-frame counters, and "
                      "QQuickWindow::frameSwapped signals; no physical presentation claim")},
      {QStringLiteral("qtPlatform"), QGuiApplication::platformName()},
      {QStringLiteral("offscreenDiagnostic"), config.offscreen_diagnostic},
      {QStringLiteral("physicalPresentationQualified"), false},
      {QStringLiteral("probeBudgetMs"), kBudgetMs},
      {QStringLiteral("elapsedMs"), total_timer.elapsed()},
      {QStringLiteral("budgetExpired"), budget_expired},
      {QStringLiteral("budgetNote"),
       QStringLiteral("The watchdog bounds event-loop waits; a single synchronous call cannot be interrupted.")},
      {QStringLiteral("nativeWindowState"), timeline_scroll_native_window_state(window)},
      {QStringLiteral("ok"), correctness_error.isEmpty() && input_valid && identity_after && state_restored},
      {QStringLiteral("inputCorrectness"), input_valid},
      {QStringLiteral("stateRestored"), state_restored},
      {QStringLiteral("identityMatchesBefore"), identity_before},
      {QStringLiteral("identityMatchesAfter"), identity_after},
      {QStringLiteral("sourceSha256Before"), source_before},
      {QStringLiteral("sourceSha256After"), source_after},
      {QStringLiteral("expectedSourceSha256"), config.expected_file_sha256},
      {QStringLiteral("workingCopySha256Before"), copy_before},
      {QStringLiteral("workingCopySha256After"), copy_after},
      {QStringLiteral("documentHashBefore"), hash_before},
      {QStringLiteral("documentHashAfter"), hash_after},
      {QStringLiteral("expectedDocumentHash"), config.expected_document_hash},
      {QStringLiteral("trackCountBefore"), tracks_before},
      {QStringLiteral("trackCountAfter"), document.timelineTracks().size()},
      {QStringLiteral("clipCountBefore"), clips_before},
      {QStringLiteral("clipCountAfter"), document.clipCount()},
      {QStringLiteral("durationSeconds"), duration},
      {QStringLiteral("dirtyBefore"), dirty_before},
      {QStringLiteral("dirtyAfter"), document.dirty()},
      {QStringLiteral("playheadRestoredSeconds"), document.sequencePlayheadSeconds()},
      {QStringLiteral("scrub"), scrub}, {QStringLiteral("playback"), play},
      {QStringLiteral("correctnessError"), correctness_error}};
  const auto receipt_path = write_timeline_scrub_receipt(config, receipt);
  std::cerr << "Timeline scrub responsiveness receipt: " << receipt_path.toStdString() << '\n';
  if (receipt_path.isEmpty()) correctness_error = QStringLiteral("Could not write Timeline scrub proof receipt.");
  if (error != nullptr) *error = correctness_error;
  return correctness_error.isEmpty() && input_valid;
}

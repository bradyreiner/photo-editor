int run_timeline_stress_runtime_proof(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    photo_editor::desktop::TimelinePlaybackController* playback,
    const QString& output_dir) {
  if (!window || !root || !document || !playback || !QDir().mkpath(output_dir))
    return fail("Missing timeline stress runtime");
  const auto media = QDir(output_dir).filePath("moving-4k.mp4");
  if (!QFileInfo::exists(media)) {
    QProcess encode;
    encode.start(QStandardPaths::findExecutable("ffmpeg"), {"-y", "-f", "lavfi", "-i",
        "testsrc2=size=3840x2160:rate=30:duration=16", "-an", "-c:v", "libx264",
        "-preset", "ultrafast", "-crf", "24", "-pix_fmt", "yuv420p", media});
    if (!encode.waitForStarted(5000) || !encode.waitForFinished(60000) || encode.exitCode() != 0)
      return fail("Timeline stress requires a real moving 4K fixture");
  }
  document->createTimelineWithSettings({{"timelineName", "4K playback stress"},
      {"width", 3840}, {"height", 2160}, {"frameRate", 30.0}});
  document->addClipFromGallery("stress-4k", media, "4K moving clip", 16.0);
  root->setProperty("currentWorkspace", root->property("timelineWorkspaceIndex"));
  if (!wait_for_condition([&] { return active_workspace(root) &&
        named_item_visible(root, "timelineViewport") && playback->mediaReady(); }, 30000, "stress-route"))
    return fail("Timeline stress must load the real native video workspace");
  auto* workspace = active_workspace(root);
  if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
    activate_native_test_window(window);
  workspace->setProperty("timelineZoom", 40.0);
  const auto quality = QCoreApplication::arguments().contains("--timeline-stress-auto-quality")
      ? QStringLiteral("auto") : QStringLiteral("full");
  playback->setPreviewQualityMode(quality);
  playback->seekSequenceSeconds(0.0);
  playback->play();
  if (!wait_for_condition([&] { return document->sequencePlayheadSeconds() >= 1.0 &&
        !playback->processedFrameSource().isEmpty(); }, 15000, "stress-warmup")) {
    std::cerr << QJsonDocument(QJsonObject{{"warmupDiagnostics", QJsonObject::fromVariantMap(playback->playbackDiagnostics())},
        {"frameProcessingMetrics", QJsonObject::fromVariantMap(playback->frameProcessingMetrics())},
        {"playhead", document->sequencePlayheadSeconds()}, {"playing", document->sequencePlaying()},
        {"workspaceActive", playback->workspaceActive()}}).toJson().constData();
    return fail("Timeline stress must decode real frames before measurement");
  }
  playback->pause();
  playback->seekSequenceSeconds(1.0);
  process_layout_events(10, "stress-settle");
  playback->resetPlaybackDiagnostics();
  QElapsedTimer elapsed;
  elapsed.start();
  qint64 last_tick = 0;
  qint64 last_timestamp = -1;
  int published_frames = 0;
  std::vector<double> heartbeat_ms;
  bool measuring = true;
  const auto connection = QObject::connect(playback,
      &photo_editor::desktop::TimelinePlaybackController::processedFrameSourceChanged, window, [&] {
    const auto timestamp = playback->processedFrameTimestampUs();
    if (measuring && timestamp != last_timestamp && timestamp >= 0) {
      ++published_frames;
      last_timestamp = timestamp;
    }
  });
  QEventLoop loop;
  QTimer heartbeat;
  heartbeat.setTimerType(Qt::PreciseTimer);
  heartbeat.setInterval(16);
  QObject::connect(&heartbeat, &QTimer::timeout, &loop, [&] {
    const auto now = elapsed.elapsed();
    heartbeat_ms.push_back(static_cast<double>(now - last_tick));
    last_tick = now;
    if (now >= 10000) loop.quit();
  });
  const double start_seconds = document->sequencePlayheadSeconds();
  heartbeat.start();
  playback->play();
  loop.exec();
  measuring = false;
  heartbeat.stop();
  const auto duration_ms = elapsed.elapsed();
  playback->pause();
  QObject::disconnect(connection);
  const auto metrics = playback->frameProcessingMetrics();
  const auto diagnostics = playback->playbackDiagnostics();
  const auto dropped_frames = playback->droppedFrameCount();
  const double progress_seconds = document->sequencePlayheadSeconds() - start_seconds;
  const double fps = published_frames * 1000.0 / static_cast<double>(duration_ms);
  std::sort(heartbeat_ms.begin(), heartbeat_ms.end());
  const double heartbeat_p95 = heartbeat_ms.empty() ? 10000.0
      : heartbeat_ms[std::min(heartbeat_ms.size() - 1,
          static_cast<std::size_t>(std::ceil(heartbeat_ms.size() * 0.95) - 1))];

  // Use the same source-time function used by filmstrip and blade images, and
  // compare it with the authoritative document mapper after trim/rate/reverse.
  QJsonArray mappings;
  bool mapped_correctly = true;
  document->selectClip(0);
  document->trimSelectedClipStartToSequenceSeconds(2.0);
  document->setSelectedClipPlaybackRate(2.0);
  for (const auto& preset : {QStringLiteral("forward"), QStringLiteral("reverse")}) {
    if (preset == "reverse" && !document->applySelectedClipRetimePreset(preset))
      return fail("Reverse retiming must be available in the source-frame proof");
    for (const double local : {0.25, 1.0, 2.0}) {
      const auto clip = document->currentClip();
      document->seekSequenceSeconds(clip.value("startSeconds").toDouble() + local);
      QVariant mapped;
      const bool invoked = QMetaObject::invokeMethod(workspace, "clipSourceTime",
          Q_RETURN_ARG(QVariant, mapped), Q_ARG(QVariant, clip),
          Q_ARG(QVariant, local * workspace->property("timelineZoom").toDouble()));
      const auto expected = document->currentClipPlayheadSeconds();
      const bool match = invoked && std::abs(mapped.toDouble() - expected) < 0.002;
      mapped_correctly = mapped_correctly && match;
      mappings.append(QJsonObject{{"mode", preset}, {"localSeconds", local},
          {"sourceSeconds", mapped.toDouble()}, {"expectedSourceSeconds", expected}, {"matches", match}});
    }
  }
  const bool saved = document->saveTimeline(false);
  const auto saved_path = document->savedDocumentPath();
  const auto saved_hash = document->deterministicDocumentHash();
  const bool reopened = saved && !saved_path.isEmpty() && document->loadTimeline(saved_path) &&
      wait_for_condition([&] { return document->deterministicDocumentHash() == saved_hash; }, 15000, "stress-reopen");
  QJsonArray captures;
  save_capture_if_requested(window, output_dir, "4k-stress-after-retime", &captures);
  const bool passed = fps >= 27.0 && dropped_frames <= 15 && progress_seconds >= 9.0 && heartbeat_p95 <= 50.0 &&
      mapped_correctly && reopened;
  const auto receipt = QJsonDocument(QJsonObject{{"schema", "timeline-native-4k-stress-v1"},
      {"passed", passed}, {"platform", QGuiApplication::platformName()}, {"sourceWidth", 3840},
      {"sourceHeight", 2160}, {"sourceFps", 30}, {"requestedQuality", quality},
      {"durationMs", duration_ms}, {"publishedUniqueTimestampFrames", published_frames},
      {"publishedFramesPerSecond", fps}, {"sequenceProgressSeconds", progress_seconds},
      {"eventLoopHeartbeatP95Ms", heartbeat_p95}, {"droppedFrames", dropped_frames},
      {"frameProcessingMetrics", QJsonObject::fromVariantMap(metrics)},
      {"playbackDiagnostics", QJsonObject::fromVariantMap(diagnostics)},
      {"trimRateReverseSourceTime", mappings}, {"savedAndReopened", reopened}, {"captures", captures}}).toJson();
  QFile result(QDir(output_dir).filePath("result.json"));
  if (!result.open(QIODevice::WriteOnly) || result.write(receipt) != receipt.size())
    return fail("Cannot save native timeline stress receipt");
  std::cout << receipt.constData();
  return passed ? EXIT_SUCCESS : fail("Native 4K stress did not meet every declared gate; see result.json");
}

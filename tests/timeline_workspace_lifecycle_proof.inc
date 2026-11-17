// Identical native route used for baseline and candidate. Resource invariants
// are recorded even on the baseline which predates resource hibernation.
int run_timeline_workspace_lifecycle_proof(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    photo_editor::desktop::TimelinePlaybackController* playback,
    photo_editor::desktop::CatalogAssetModel* catalog,
    const QString& source_path, const QString& output_dir) {
  if (!window || !root || !document || !playback || !catalog || !QDir().mkpath(output_dir))
    return fail("Timeline lifecycle proof needs the complete desktop shell");
  root->setProperty("recentUserInput", true);
#ifdef Q_OS_MACOS
  activate_native_test_application();
#endif
  window->requestActivate();
  if (!wait_for_condition([&] { return window->isActive() && window->isExposed(); },
                          5000, "lifecycle-active-window"))
    return fail("Lifecycle proof requires an active, exposed native window");
  const auto memory = [] {
    QJsonObject result;
#ifdef Q_OS_MACOS
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
      result.insert("residentBytes", static_cast<qint64>(info.resident_size));
      result.insert("physicalFootprintBytes", static_cast<qint64>(info.phys_footprint));
    }
#endif
    return result;
  };
  QJsonArray routes, samples;
  QString error;
  bool foreground_eligible = true;
  QElapsedTimer clock;
  clock.start();
  qint64 last_tick = 0, maximum_gap = 0;
  QTimer sampler;
  sampler.setInterval(25);
  QObject::connect(&sampler, &QTimer::timeout, window, [&] {
    const auto now = clock.elapsed();
    maximum_gap = std::max(maximum_gap, now - last_tick);
    last_tick = now;
    auto row = memory();
    row.insert("elapsedMs", now);
    samples.append(row);
    foreground_eligible = foreground_eligible && window->isActive() && window->isExposed() &&
        QGuiApplication::applicationState() == Qt::ApplicationActive;
  });
  sampler.start();
  int requested_workspace_index = -1;
  quint64 route_generation = 0;
  const auto route_ready = [&] {
    auto* workspace = active_workspace(root);
    return root->property("currentWorkspace").toInt() == requested_workspace_index && workspace &&
           workspace->property("visible").toBool() &&
           !root->property("workspaceTransitionActive").toBool();
  };
  photo_editor::desktop::WorkspacePresentationObserver presentation(
      root, window, window, [&](quint64) { return route_ready(); });
  if (!presentation.installed())
    return fail(presentation.errorMessage());
  const auto route = [&](const QString& page, int iteration) {
    if (!error.isEmpty()) return;
    const auto key = (page.toLower() + "WorkspaceIndex").toUtf8();
    const auto index = root->property(key.constData());
    requested_workspace_index = index.toInt();
    presentation.beginGeneration(++route_generation);
    QElapsedTimer timer;
    timer.start();
    QVariant accepted;
    if (!QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, accepted),
                                  Q_ARG(QVariant, index)) || !accepted.toBool()) {
      error = "Navigation rejected " + page;
      return;
    }
    if (!wait_for_condition(route_ready, 30000, "lifecycle-route-ready")) {
      error = "Workspace did not release input: " + page;
      return;
    }
    if (page == "Library") {
      auto* workspace = active_workspace(root);
      if (workspace == nullptr ||
          workspace->objectName() != QStringLiteral("libraryStudioWorkspace")) {
        error = "Library route did not resolve the libraryStudioWorkspace root";
        return;
      }
    }
    const auto ready_ms = timer.elapsed();
    if (!wait_for_condition([&] { return presentation.hasPresentedGeneration(route_generation); },
                            3000, "lifecycle-presented")) {
      error = "Workspace did not present: " + page;
      return;
    }
    auto row = memory();
    row.insert("page", page);
    row.insert("iteration", iteration);
    row.insert("inputReadyMs", ready_ms);
    row.insert("presentedMs", timer.elapsed());
    row.insert("maxEventLoopGapMs", maximum_gap);
    routes.append(row);
    std::cerr << "LIFECYCLE_ROUTE " << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << '\n';
    maximum_gap = 0;
    last_tick = clock.elapsed();
  };
  const auto video_path = QDir(output_dir).filePath("lifecycle-source.mp4");
  if (!write_timeline_proof_video_file(source_path, video_path))
    return fail("Lifecycle proof needs decoded video media");
  auto photo = make_asset("lifecycle-photo", source_path);
  auto video = make_asset("lifecycle-video", video_path);
  video.media_type = "video";
  video.duration_seconds = 10;
  catalog->replaceAssetsForTesting({photo, video});
  // Warm both destinations before Timeline to make the before/after comparison
  // reflect the cost of having used Timeline, rather than first-use photo UI.
  for (int i = 0; i < 3; ++i) { route("Edit", -3 + i); route("Library", -3 + i); }
  const auto before_timeline = memory();
  document->createTimelineWithSettings({{"timelineName", "Lifecycle regression"},
      {"width", 1920}, {"height", 1080}, {"frameRate", 30.0}});
  document->addClipFromGallery("lifecycle-video", video_path, "Lifecycle video", 10.0,
                              QUrl::fromLocalFile(source_path).toString());
  document->selectClip(0);
  document->seekSequenceSeconds(2.0);
  document->setSelectedClipOpacity(0.85);
  route("Timeline", -1);
  if (!wait_for_condition([&] { return playback->mediaReady(); }, 15000, "lifecycle-decoded-source"))
    error = "Program did not load its real video source";
  auto* timeline = active_workspace(root);
  if (timeline && error.isEmpty()) {
    for (const auto& panel : {"source", "effects", "color", "mixer", "keyframes"})
      QMetaObject::invokeMethod(timeline, "showTimelinePanel", Q_ARG(QVariant, QString::fromLatin1(panel)));
    process_layout_events(8, "lifecycle-open-panels");
  }
  // Capture the session after first-use view initialization, which may publish
  // persisted view preferences. Subsequent navigation may not change it.
  const auto document_hash = document->deterministicDocumentHash();
  const auto selected = document->selectedClipIds();
  const auto playhead = document->sequencePlayheadSeconds();
  const auto undo_before = document->canUndo();
  bool every_session_preserved = true;
  const auto loaded_timeline = memory();
  QPointer<QObject> original_color_panel = find_named_object(timeline, "timelineColorConsole");
  for (int i = 0; i < 20 && error.isEmpty(); ++i) {
    route("Edit", i);
    route("Library", i);
    route("Timeline", i);
    if (document->deterministicDocumentHash() != document_hash ||
        document->selectedClipIds() != selected || document->canUndo() != undo_before ||
        std::abs(document->sequencePlayheadSeconds() - playhead) > 0.001)
      every_session_preserved = false;
  }
  if (original_color_panel)
    original_color_panel->setProperty("activePage", QStringLiteral("curves"));
  route("Library", 20);
  const auto inactive_before = memory();
  // Fixed idle interval gives production hibernation a chance to expire; no GC
  // or forced trim is injected by the benchmark.
  QElapsedTimer idle;
  idle.start();
  wait_for_condition([&] { return idle.elapsed() >= 12000; }, 13000, "lifecycle-idle-release");
  const auto inactive_after = memory();
  const auto inactive_diagnostics = playback->property("playbackResourceDiagnostics").toMap();
  const bool panel_resources_released = timeline &&
      timeline->property("workspaceResourceState").toString() == QStringLiteral("hibernated") &&
      original_color_panel.isNull();
  const bool resources_released = inactive_diagnostics.value("hibernated").toBool() &&
      inactive_diagnostics.value("playerSourceLoaded", true).toBool() == false;
  route("Timeline", 21);
  const bool resumed = error.isEmpty() && wait_for_condition([&] { return playback->mediaReady(); },
                                                           15000, "lifecycle-resume-video");
  const bool state_preserved = every_session_preserved && document->deterministicDocumentHash() == document_hash &&
      document->selectedClipIds() == selected && document->canUndo() == undo_before &&
      std::abs(document->sequencePlayheadSeconds() - playhead) < 0.001;
  bool panel_state_restored = false;
  if (timeline) {
    QMetaObject::invokeMethod(timeline, "showTimelinePanel", Q_ARG(QVariant, QStringLiteral("color")));
    panel_state_restored = wait_for_condition([&] {
      auto* color = find_named_object(timeline, "timelineColorConsole");
      return color && color->property("activePage").toString() == QStringLiteral("curves");
    }, 3000, "lifecycle-panel-session-restored");
  }
  QJsonArray captures;
  save_capture_if_requested(window, output_dir, "lifecycle-resumed-timeline", &captures);
  sampler.stop();
  const auto percentile = [&](const QString& page, bool before) {
    QVector<qint64> values;
    for (const auto& value : routes) {
      const auto row = value.toObject();
      const auto iteration = row.value("iteration").toInt();
      if (row.value("page").toString() == page && (before ? iteration < 0 : iteration >= 0 && iteration < 20))
        values.append(row.value("presentedMs").toInteger());
    }
    std::sort(values.begin(), values.end());
    return values.isEmpty() ? qint64(-1) : values[qMax<qsizetype>(0, (values.size() * 95 + 99) / 100 - 1)];
  };
  const auto edit_p95 = percentile("Edit", false), library_p95 = percentile("Library", false);
  const auto timeline_p95 = percentile("Timeline", false);
  qint64 first_timeline_ms = -1;
  for (const auto& value : routes) {
    const auto row = value.toObject();
    if (row.value("page").toString() == "Timeline" && row.value("iteration").toInt() == -1)
      first_timeline_ms = row.value("presentedMs").toInteger();
  }
  qint64 first_quarter_memory = 0, last_quarter_memory = 0;
  for (const auto& value : routes) {
    const auto row = value.toObject();
    if (row.value("page").toString() != "Library") continue;
    const auto iteration = row.value("iteration").toInt();
    if (iteration >= 0 && iteration < 5)
      first_quarter_memory = qMax(first_quarter_memory, row.value("physicalFootprintBytes").toInteger());
    if (iteration >= 15 && iteration < 20)
      last_quarter_memory = qMax(last_quarter_memory, row.value("physicalFootprintBytes").toInteger());
  }
  const bool memory_measured = before_timeline.contains("physicalFootprintBytes");
  const auto growth = last_quarter_memory - first_quarter_memory;
  const bool budgets_passed = edit_p95 >= 0 && edit_p95 <= 250 && library_p95 >= 0 && library_p95 <= 250 &&
      timeline_p95 >= 0 && timeline_p95 <= 250 && first_timeline_ms >= 0 && first_timeline_ms <= 1500 &&
      memory_measured && growth <= 64LL * 1024 * 1024;
  const bool passed = error.isEmpty() && foreground_eligible && resumed && state_preserved &&
      resources_released && panel_resources_released && panel_state_restored && budgets_passed;
  const QJsonObject result{{"schema", "timeline-workspace-lifecycle:v1"}, {"passed", passed},
      {"error", error}, {"foregroundEligible", foreground_eligible}, {"memoryMeasured", memory_measured},
      {"editBeforeP95Ms", percentile("Edit", true)}, {"libraryBeforeP95Ms", percentile("Library", true)},
      {"editAfterP95Ms", edit_p95}, {"libraryAfterP95Ms", library_p95},
      {"timelineAfterP95Ms", timeline_p95}, {"firstTimelineMs", first_timeline_ms},
      {"firstTimelineBudgetMs", 1500},
      {"warmSwitchBudgetMs", 250}, {"memoryGrowthBytes", growth}, {"memoryGrowthBudgetBytes", 64LL * 1024 * 1024},
      {"beforeTimeline", before_timeline}, {"loadedTimeline", loaded_timeline},
      {"inactiveBefore", inactive_before}, {"inactiveAfter", inactive_after},
      {"inactivePlaybackResources", QJsonObject::fromVariantMap(inactive_diagnostics)},
      {"resourcesReleased", resources_released}, {"resumed", resumed}, {"statePreserved", state_preserved},
      {"panelResourcesReleased", panel_resources_released}, {"panelStateRestored", panel_state_restored},
      {"initialDocumentHash", document_hash}, {"finalDocumentHash", document->deterministicDocumentHash()},
      {"initialPlayhead", playhead}, {"finalPlayhead", document->sequencePlayheadSeconds()},
      {"routes", routes}, {"samples", samples}, {"captures", captures}};
  QFile receipt(QDir(output_dir).filePath("workspace-lifecycle-proof.json"));
  if (!receipt.open(QIODevice::WriteOnly) || receipt.write(QJsonDocument(result).toJson()) < 0)
    return fail("Cannot write lifecycle receipt");
  return passed ? EXIT_SUCCESS : fail("Lifecycle proof recorded failing gates; inspect workspace-lifecycle-proof.json");
}

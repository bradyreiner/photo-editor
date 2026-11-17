int run_timeline_workspace_ux_proof(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    photo_editor::desktop::TimelinePlaybackController* playback,
    photo_editor::desktop::CatalogAssetModel* catalog,
    const QString& source_path, const QString& output_dir) {
  if (!window || !root || !document || !playback || !catalog || !QDir().mkpath(output_dir))
    return fail("Workspace UX proof needs a complete desktop shell");
  const bool diagnose_all_gates = QCoreApplication::arguments().contains(
      QStringLiteral("--timeline-workspace-ux-diagnose-all-gates"));
  root->setProperty("recentUserInput", true);
#ifdef Q_OS_MACOS
  if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
    activate_native_test_application();
    window->requestActivate();
    if (!wait_for_condition([&] { return window->isActive() && window->isExposed(); },
                            5000, "timeline-ux-library-active"))
      return fail("Native workspace timing requires the actual active, exposed Library window");
  }
#endif
  const auto video_path = QDir(output_dir).filePath("interview-camera-a.mp4");
  if (!write_timeline_proof_video_file(source_path, video_path))
    return fail("Workspace UX proof could not encode its real video fixture");
  auto video_asset = make_asset("timeline-ux-video", video_path);
  video_asset.camera_label = "Canon EOS 80D";
  video_asset.media_type = "video";
  video_asset.duration_seconds = 10;
  video_asset.duration_text = "0:10";
  auto photo_asset = make_asset("timeline-ux-photo", source_path);
  photo_asset.camera_label = "Canon EOS 80D";
  catalog->replaceAssetsForTesting({video_asset, photo_asset});
  document->createTimelineWithSettings({{"timelineName", "Interview — workspace proof"},
      {"width", 1080}, {"height", 1350}, {"frameRate", 30.0}});
  document->addClipFromGallery("timeline-ux-video", video_path, "Interview camera A", 10.0,
                                QUrl::fromLocalFile(source_path).toString());
  document->addClipFromGallery("timeline-ux-photo", source_path, "Cutaway", 5.0,
                                QUrl::fromLocalFile(source_path).toString());
  document->selectClip(0);
  document->seekSequenceSeconds(1.0);
  QVariant routed;
  QElapsedTimer timeline_load_timer;
  timeline_load_timer.start();
  if (!QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, routed),
      Q_ARG(QVariant, root->property("timelineWorkspaceIndex"))) || !routed.toBool() ||
      !wait_for_condition([&] {
        auto* active = active_workspace(root);
        auto* editing_action = find_named_object(root, "timelineEditingWorkspaceAction");
        return active && active->objectName() == "timelineWorkspace" &&
               active->property("timelineEditorPageLoaded").toBool() &&
               editing_action && editing_action->property("enabled").toBool() &&
               !root->property("workspaceTransitionActive").toBool() &&
               named_item_visible(root, "timelineHeaderBar") &&
               named_item_visible(root, "timelineProgramMonitor");
      }, 30000, "timeline-ux-route"))
    return fail("Workspace UX proof must route to the actual Timeline UI");
  const auto timeline_shell_ready_ms = timeline_load_timer.elapsed();
  auto* workspace = active_workspace(root);
  const auto invoke = [&](const char* method, const QVariant& value) {
    return QMetaObject::invokeMethod(workspace, method, Q_ARG(QVariant, value));
  };
  const auto action = [&](const QString& name) {
    auto* object = find_named_object(root, name);
    if (!object || !object->property("enabled").toBool()) {
      std::cerr << "Window action unavailable: " << name.toStdString()
                << " exists=" << (object != nullptr) << '\n';
      return false;
    }
    return QMetaObject::invokeMethod(object, "trigger", Q_ARG(QObject*, nullptr));
  };
  QObject* dock_state = find_named_object(workspace, QStringLiteral("timelinePanelDockState"));
  const auto invoke_dock = [&](const char* method, const QVariantList& arguments,
                               QVariant* result = nullptr) {
    if (dock_state == nullptr) return false;
    QVariant local_result;
    auto* output = result == nullptr ? &local_result : result;
    bool invoked = false;
    switch (arguments.size()) {
      case 0:
        invoked = QMetaObject::invokeMethod(dock_state, method, Q_RETURN_ARG(QVariant, *output));
        break;
      case 1:
        invoked = QMetaObject::invokeMethod(dock_state, method, Q_RETURN_ARG(QVariant, *output),
                                            Q_ARG(QVariant, arguments[0]));
        break;
      case 2:
        invoked = QMetaObject::invokeMethod(dock_state, method, Q_RETURN_ARG(QVariant, *output),
                                            Q_ARG(QVariant, arguments[0]),
                                            Q_ARG(QVariant, arguments[1]));
        break;
      default:
        return false;
    }
    if (invoked && output->metaType() == QMetaType::fromType<QJSValue>())
      *output = output->value<QJSValue>().toVariant();
    return invoked;
  };
  const auto panel_visible = [&](const QString& panel_id) {
    QVariant value;
    return invoke_dock("visible", {panel_id}, &value) && value.toBool();
  };
  const auto panel_dock = [&](const QString& panel_id) {
    QVariant value;
    return invoke_dock("dock", {panel_id}, &value) ? value.toString() : QString();
  };
  const auto color_controls_cached = [&] {
    auto* panel = find_named_item(workspace, QStringLiteral("timelineColorConsole"));
    auto* loader = find_named_object(workspace, QStringLiteral("timelineColorConsolePanelLoader"));
    return panel && loader && loader->property("active").toBool() &&
           loader->property("retainLoaded").toBool() &&
           loader->property("status").toInt() == 1 &&
           loader->property("item").value<QObject*>() == panel;
  };
  const auto color_controls_visible = [&] {
    return color_controls_cached() &&
           named_item_visible(workspace, QStringLiteral("timelineColorConsolePrimariesPage")) &&
           named_item_visible(workspace, QStringLiteral("timelineColorConsole_exposure"));
  };
  const auto mixer_surface_ready = [&] {
    auto* mixer = find_named_object(workspace, QStringLiteral("timelineStudioAudioMixer"));
    auto* dock = find_named_item(workspace, QStringLiteral("timelineStudioAudioDock"));
    auto* content = mixer != nullptr
        ? qobject_cast<QQuickItem*>(mixer->property("contentItem").value<QObject*>()) : nullptr;
    auto* master = find_named_item(workspace, QStringLiteral("timelineMasterMixerStrip"));
    return panel_visible(QStringLiteral("mixer")) && mixer != nullptr &&
           mixer->property("visible").toBool() && item_visible(dock) && item_visible(content) &&
           item_visible(master);
  };
  const auto click_visible_item = [&](const QString& object_name) {
    auto* item = find_visible_named_item(workspace, object_name);
    if (!item || !item->property("enabled").toBool()) return false;
    const auto point = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    process_layout_events(4);
    return true;
  };
  const auto present = [&]() {
    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
    // QQuickWindow::update forces a render even when a retained layout leaves
    // the scene graph clean; QWindow::requestUpdate may elide that frame.
    window->update();
    return !frames.isEmpty() || frames.wait(1000);
  };
  if (!workspace || dock_state == nullptr || !action("timelineEditingWorkspaceAction") ||
      !wait_for_condition([&] {
        auto* grid = qobject_cast<QQuickItem*>(
            find_named_object(root, "timelineVirtualizedMediaGrid"));
        return item_visible(grid) && grid->property("count").toInt() >= 2;
      }, 10000, "timeline-ux-media-grid"))
    return fail("Editing workspace must expose its real thumbnail grid through the Window action");
  const auto first_timeline_load_ms = timeline_load_timer.elapsed();
  const bool first_timeline_budget_passed = first_timeline_load_ms <= 1500;
  if (!first_timeline_budget_passed) {
    std::cerr << "WORKSPACE_GATE firstTimelineLoad passed=false elapsed_ms="
              << first_timeline_load_ms << " budget_ms=1500 diagnostic="
              << diagnose_all_gates << '\n';
  }
  if (!first_timeline_budget_passed && !diagnose_all_gates)
    return fail(QStringLiteral("Foreground Timeline and its media grid exceeded the 1500 ms usability budget: %1 ms")
                    .arg(first_timeline_load_ms));
  const auto available = window->screen()->availableGeometry();
  window->resize(std::min(1920, available.width()), std::min(1080, available.height()));
  process_layout_events(8);
  const auto usable_layout = [&](bool color) {
    auto* tracks = find_visible_named_item(workspace, "timelineSurfacePanel");
    auto* preview = find_visible_named_item(workspace, "timelineProgramMonitor");
    if (!tracks || !preview) return false;
    const auto tracks_rect = item_scene_rect(tracks);
    return tracks->height() >= workspace->property("minimumTimelineTrackPanelHeight").toDouble() - 1.0 &&
           tracks_rect.bottom() <= window->height() + 1.0 &&
           (!color || preview->height() >= 180.0);
  };
  if (!wait_for_condition([&] { return usable_layout(false); }, 3000, "timeline-ux-wide-layout"))
    return fail("The real desktop window must contain its complete Timeline track surface");
  QJsonArray captures;
  // Exercise the new owner surfaces with pointer input, including menu bounds.
  if (!click_visible_item(QStringLiteral("timelinePanelsMenuButtonPrimaryAction")) ||
      !wait_for_condition([&] {
        return named_item_visible(workspace, "timelinePanelsMenuButtonPopupItem10");
      }, 3000, "timeline-panels-menu"))
    return fail("Panels must offer all eleven named panels from the workspace header");
  for (int index = 0; index < 11; ++index) {
    auto* row = find_visible_named_item(workspace,
        QStringLiteral("timelinePanelsMenuButtonPopupItem%1").arg(index));
    const auto bounds = item_scene_rect(row);
    if (!row || bounds.left() < 0 || bounds.right() > window->width() + 1 ||
        bounds.top() < 0 || bounds.bottom() > window->height() + 1)
      return fail("Every Panels menu row must fit within the desktop window");
  }
  save_capture_if_requested(window, output_dir, "timeline-panels-menu", &captures);
  if (!click_visible_item(QStringLiteral("timelinePanelsMenuButtonPopupItem1")) ||
      !wait_for_condition([&] {
        return panel_visible(QStringLiteral("source")) &&
            invoke_dock("activePanel", {QStringLiteral("left")}, &routed) && routed.toString() == "source";
      }, 3000, "timeline-source-from-panels") ||
      !panel_visible(QStringLiteral("media")))
    return fail("Choosing Source must focus it while preserving Media in the same dock");
  if (!click_visible_item(QStringLiteral("timelinePanelsMenuButtonPrimaryAction")) ||
      !wait_for_condition([&] {
        return named_item_visible(workspace, "timelinePanelsMenuButtonPopupItem12");
      }, 3000, "timeline-layout-menu"))
    return fail("The Panels menu must expose Reset panel layout without a task-switching button");
  save_capture_if_requested(window, output_dir, "timeline-layout-menu", &captures);
  if (!click_visible_item(QStringLiteral("timelinePanelsMenuButtonPopupItem12")) ||
      !wait_for_condition([&] { return !panel_visible(QStringLiteral("source")); }, 3000,
                          "timeline-reset-from-header"))
    return fail("Reset current layout must restore the focused Editing arrangement through the header");
  QVariant mixer_shown;
  if (!invoke_dock("focusPanel", {QStringLiteral("mixer")}, &mixer_shown) || !mixer_shown.toBool())
    return fail("The Mixer must be a first-class retained Timeline panel before workspace changes");
  for (const auto& name : {"timelineProjectSummaryButton", "timelineLayoutPresetButton",
                           "timelineViewMenuButton", "timelineOutputVariantMenuButton"}) {
    if (named_item_visible(root, QString::fromLatin1(name)))
      return fail("Sequence setup and workspace management must not occupy the edit toolbar");
  }
  const auto source_before = playback->activeSourceUrl();
  const auto clip_before = document->currentClip().value("clipId").toString();
  const auto playhead_before = document->sequencePlayheadSeconds();
  const auto count_before = document->clipCount();
  const auto dirty_before = document->dirty();
  workspace->setProperty("sourcePanelWidth", 382);
  auto* splitter = find_visible_named_item(workspace, "timelineVerticalSplitterHandle");
  if (!splitter || !splitter->isEnabled())
    return fail("The vertical workspace splitter must be usable");
  const auto split_point = splitter->mapToScene(
      QPointF(splitter->width() / 2.0, splitter->height() / 2.0)).toPoint();
  QTest::mouseMove(window, split_point);
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, split_point);
  QTest::mouseMove(window, split_point - QPoint(0, 48), 40);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, split_point - QPoint(0, 48));
  process_layout_events(6);
  const auto resized_monitor_height = workspace->property("monitorPanelHeight").toDouble();
  if (!workspace->property("monitorPanelUserResized").toBool())
    return fail("Dragging the real splitter must record the user's layout override");
  document->selectClip(1);
  document->selectClip(0);
  document->seekSequenceSeconds(playhead_before);
  process_layout_events(6);
  if (std::abs(workspace->property("monitorPanelHeight").toDouble() - resized_monitor_height) > 1.0)
    return fail("Selecting another clip must not move the manually resized workspace split");
  QMetaObject::invokeMethod(workspace, "saveCurrentTimelineWorkspaceLayout");
  save_capture_if_requested(window, output_dir, "timeline-ux-editing-wide", &captures);

  std::vector<double> color_switch_ms;
  std::vector<double> editing_switch_ms;
  double first_color_load_ms = -1.0;
  for (int iteration = 0; iteration < 6; ++iteration) {
    QElapsedTimer timer;
    timer.start();
    if (!action("timelineColorWorkspaceAction") ||
        (iteration == 0 &&
         !wait_for_condition([&] {
           auto* loader = find_named_object(workspace, "timelineColorConsolePanelLoader");
           return loader && loader->property("active").toBool() &&
                  loader->property("status").toInt() != 0;
         }, 10000, "timeline-ux-color-incubating")))
      return fail("Color controls must enter the actual retained Color Console Loader on first use");
    if (iteration == 0) {
      auto* color_loader = find_named_object(workspace, "timelineColorConsolePanelLoader");
      if (!color_loader || !invoke("openInspectorTab", "motion"))
        return fail("The first Color load must permit a rapid focus change to Inspector");
      process_layout_events(3);
      const auto loader_status = color_loader->property("status").toInt();
      if (!color_loader->property("active").toBool() || loader_status == 0 || loader_status == 3)
        return fail("Focusing Inspector must not deactivate or discard the independent Color Console Loader");
      QVariant color_focused;
      if (!invoke_dock("focusPanel", {QStringLiteral("color")}, &color_focused) ||
          !color_focused.toBool())
        return fail("The independent Color panel must remain directly reachable after an Inspector focus change");
      if (!action("resetTimelineWorkspaceLayoutAction"))
        return fail("Reset Color must close manually opened panels and restore its focused arrangement");
    }
    if (!wait_for_condition([&] {
          return workspace->property("timelineLayoutPreset").toString() == "color" &&
                 color_controls_visible();
        }, 10000, "timeline-ux-color-ready") || !present())
      return fail("Color must show its real retained controls in its focused layout");
    if (iteration == 0 &&
        (!click_visible_item(QStringLiteral("timelineColorConsolePage_input")) ||
         !wait_for_condition([&] {
           return named_item_visible(workspace, "timelineColorConsoleInputTransformMenu");
         }, 3000, "timeline-ux-color-input-control") ||
         !click_visible_item(QStringLiteral("timelineColorConsolePage_primaries")) ||
         !wait_for_condition([&] { return color_controls_visible(); }, 3000,
                             "timeline-ux-color-primaries-restored")))
      return fail("The retained Color Console must surface its real input transform and primaries controls");
    const auto color_elapsed = static_cast<double>(timer.elapsed());
    std::cerr << "WORKSPACE_SAMPLE iteration=" << iteration << " color_ms="
              << color_elapsed << " active=" << window->isActive()
              << " exposed=" << window->isExposed() << '\n';
    if (iteration > 0) color_switch_ms.push_back(color_elapsed);
    else first_color_load_ms = color_elapsed;
    if (panel_visible(QStringLiteral("media")) || panel_visible(QStringLiteral("effects")) ||
        panel_visible(QStringLiteral("mixer")) || !panel_visible(QStringLiteral("scopes")) ||
        panel_dock(QStringLiteral("media")) != QStringLiteral("left") ||
        panel_dock(QStringLiteral("color")) != QStringLiteral("right") ||
        panel_dock(QStringLiteral("effects")) != QStringLiteral("auxiliary") ||
        panel_dock(QStringLiteral("mixer")) != QStringLiteral("bottom"))
      return fail("Color must expose Color and Scopes while keeping Media, Effects and Mixer closed");
    if (document->currentClip().value("clipId").toString() != clip_before ||
        std::abs(document->sequencePlayheadSeconds() - playhead_before) > 0.001 ||
        document->clipCount() != count_before || document->dirty() != dirty_before)
      return fail("Workspace changes must preserve selection, playhead, edit contents and dirty state");
    if (iteration == 0) {
      save_capture_if_requested(window, output_dir, "timeline-ux-color-wide", &captures);
      workspace->setProperty("effectsPanelWidth", 374);
    }
    timer.restart();
    if (!action("timelineEditingWorkspaceAction") ||
        !wait_for_condition([&] { return named_item_visible(root, "timelineVirtualizedMediaGrid"); },
                            10000, "timeline-ux-editing-ready") || !present())
      return fail("Returning to Editing must restore the browser");
    if (!color_controls_cached() || panel_visible(QStringLiteral("color")) ||
        !panel_visible(QStringLiteral("effects")) || !panel_visible(QStringLiteral("mixer")))
      return fail("Editing must restore its own Effects and manually opened Mixer while Color stays cached");
    if (iteration > 0) editing_switch_ms.push_back(static_cast<double>(timer.elapsed()));
    std::cerr << "WORKSPACE_SAMPLE iteration=" << iteration << " editing_ms="
              << timer.elapsed() << " active=" << window->isActive()
              << " exposed=" << window->isExposed() << '\n';
    if (std::abs(workspace->property("sourcePanelWidth").toDouble() - 382.0) > 0.01 ||
        !workspace->property("monitorPanelUserResized").toBool() ||
        std::abs(workspace->property("monitorPanelHeight").toDouble() - resized_monitor_height) > 0.01)
      return fail("Editing must retain its independently saved splitter and panel dimensions");
    if (!source_before.isEmpty() && playback->activeSourceUrl() != source_before)
      return fail("Changing task layouts must retain the Program decode source");
  }
  const auto color_summary = summarize_samples(color_switch_ms);
  const auto edit_summary = summarize_samples(editing_switch_ms);
  const bool first_color_budget_passed = first_color_load_ms >= 0.0 && first_color_load_ms <= 1000.0;
  const bool warm_color_budget_passed = color_summary.p95_ms <= 250.0;
  const bool warm_editing_budget_passed = edit_summary.p95_ms <= 250.0;
  const bool all_performance_budgets_passed = first_timeline_budget_passed &&
      first_color_budget_passed && warm_color_budget_passed && warm_editing_budget_passed;
  std::cerr << "WORKSPACE_GATE firstTimelineLoad passed=" << first_timeline_budget_passed
            << " elapsed_ms=" << first_timeline_load_ms << " budget_ms=1500\n"
            << "WORKSPACE_GATE firstColorLoad passed=" << first_color_budget_passed
            << " elapsed_ms=" << first_color_load_ms << " budget_ms=1000\n"
            << "WORKSPACE_GATE warmColorSwitch passed=" << warm_color_budget_passed
            << " p95_ms=" << color_summary.p95_ms << " budget_ms=250\n"
            << "WORKSPACE_GATE warmEditingSwitch passed=" << warm_editing_budget_passed
            << " p95_ms=" << edit_summary.p95_ms << " budget_ms=250\n";
  if (!first_color_budget_passed && !diagnose_all_gates)
    return fail(QStringLiteral("First-use Color controls exceeded the 1000 ms usability budget: %1 ms")
                    .arg(first_color_load_ms));
  if ((!warm_color_budget_passed || !warm_editing_budget_passed) && !diagnose_all_gates)
    return fail(QStringLiteral("Warm workspace switches exceeded the 250 ms usability budget (Color %1, Editing %2)")
                    .arg(color_summary.p95_ms).arg(edit_summary.p95_ms));
  if (!action("timelineColorWorkspaceAction") ||
      std::abs(workspace->property("effectsPanelWidth").toDouble() - 374.0) > 0.01)
    return fail("Color must restore its own panel width");
  if (!action("resetTimelineWorkspaceLayoutAction") ||
      std::abs(workspace->property("effectsPanelWidth").toDouble() - 360.0) > 0.01 ||
      !action("timelineEditingWorkspaceAction") ||
      std::abs(workspace->property("sourcePanelWidth").toDouble() - 382.0) > 0.01)
    return fail("Reset Current Layout must reset only the active workspace");

  if (!action("timelineSequenceSettingsAction") ||
      !wait_for_condition([&] {
        auto* popup = find_named_object(workspace, "timelineSequenceSettingsPopup");
        return popup && popup->property("visible").toBool();
      }, 3000, "timeline-ux-settings"))
    return fail("Sequence Settings must remain accessible from the native Timeline action");
  auto* settings = find_named_object(workspace, "timelineSequenceSettingsPopup");
  QMetaObject::invokeMethod(settings, "close");
  if (!invoke("applyTimelineLayoutPreset", "audio") ||
      !wait_for_condition([&] { return mixer_surface_ready(); }, 10000) ||
      !color_controls_cached() || panel_visible(QStringLiteral("effects")) ||
      panel_visible(QStringLiteral("color")))
    return fail("Audio must show its real Mixer without opening Color or Effects");
  save_capture_if_requested(window, output_dir, "timeline-ux-audio-wide", &captures);
  if (!invoke("applyTimelineLayoutPreset", "finish") ||
      !wait_for_condition([&] {
        auto* title = find_visible_named_item(workspace, QStringLiteral("timelineEditorialPanelTitle"));
        auto* cues = find_visible_named_item(workspace, QStringLiteral("timelineEditorialCaptionCueList"));
        auto* generate = find_visible_named_item(
            workspace, QStringLiteral("timelineEditorialOpenCaptionGenerator"));
        return title && title->property("text").toString() == QStringLiteral("Caption cues") &&
               cues != nullptr && generate != nullptr && generate->property("enabled").toBool() &&
               panel_visible(QStringLiteral("captions")) && color_controls_cached() &&
               !panel_visible(QStringLiteral("effects")) && !panel_visible(QStringLiteral("mixer"));
      }, 10000))
    return fail("Finish must surface real Caption controls without unrelated Effects or Mixer");
  save_capture_if_requested(window, output_dir, "timeline-ux-finishing-wide", &captures);
  if (!invoke("applyTimelineLayoutPreset", "edit"))
    return fail("Editing workspace should be restorable");
  window->resize(std::min(1366, available.width()), std::min(768, available.height()));
  process_layout_events(8);
  if (std::abs(workspace->property("sourcePanelWidth").toDouble() - 382.0) > 0.01)
    return fail("Responsive panel constraints must preserve the user's wider preferred source width");
  save_capture_if_requested(window, output_dir, "timeline-ux-editing-laptop", &captures);
  if (!invoke("applyTimelineLayoutPreset", "color"))
    return fail("Color workspace should adapt to a laptop window");
  if (!wait_for_condition([&] { return usable_layout(true); }, 3000, "timeline-ux-laptop-layout")) {
    save_capture_if_requested(window, output_dir, "timeline-ux-laptop-layout-failure", &captures);
    for (const auto& name : {"timelineProgramMonitor", "timelineProgramMonitorStage",
                             "timelineSurfacePanel", "timelineMainSplit",
                             "timelineDockedProgramScopes"}) {
      auto* item = find_visible_named_item(workspace, QString::fromLatin1(name));
      if (item) {
        const auto rect = item_scene_rect(item);
        std::cerr << name << ": " << rect.x() << ',' << rect.y() << ' '
                  << rect.width() << 'x' << rect.height() << '\n';
      }
    }
    return fail("Laptop Color must retain a usable preview and a fully visible track surface");
  }
  save_capture_if_requested(window, output_dir, "timeline-ux-color-laptop", &captures);
  document->selectClip(1);
  if (!wait_for_condition([&] {
        auto* target = find_visible_named_item(workspace, QStringLiteral("timelineColorConsoleTarget"));
        auto* status = find_visible_named_item(
            workspace, QStringLiteral("timelineColorConsoleSelectionState"));
        auto* exposure = find_visible_named_item(workspace, QStringLiteral("timelineColorConsole_exposure"));
        auto* color = find_named_item(workspace, QStringLiteral("timelineColorConsole"));
        return target && target->property("text").toString() == QStringLiteral("Cutaway") &&
               status && exposure && !exposure->property("enabled").toBool() && color &&
               !color->property("isEditablePicture").toBool();
      }, 5000))
    return fail("The Color Console must identify the selected photo and disable picture-grade controls");
  save_capture_if_requested(window, output_dir, "timeline-ux-color-selection", &captures);
  if (captures.size() != 9)
    return fail("Workspace UX proof must produce all nine fresh native captures");

  // Recreate the actual workspace, including previously uncreated detached
  // windows. This exercises QSettings serialization and the lazy restore path.
  QVariant media_floated;
  QVariant inspector_floated;
  QVariant scopes_floated;
  if (!invoke_dock("setVisible", {QStringLiteral("media"), true}, &media_floated) ||
      !media_floated.toBool() ||
      !invoke_dock("floatPanel", {QStringLiteral("media"), QVariant{}}, &media_floated) ||
      !media_floated.toBool() ||
      !invoke_dock("setVisible", {QStringLiteral("inspector"), true}, &inspector_floated) ||
      !inspector_floated.toBool() ||
      !invoke_dock("floatPanel", {QStringLiteral("inspector"), QVariant{}}, &inspector_floated) ||
      !inspector_floated.toBool() ||
      !invoke_dock("setVisible", {QStringLiteral("scopes"), true}, &scopes_floated) ||
      !scopes_floated.toBool() ||
      !invoke_dock("floatPanel", {QStringLiteral("scopes"), QVariant{}}, &scopes_floated) ||
      !scopes_floated.toBool())
    return fail("The panel state model must float Media, Inspector, and Scopes before recreation");
  if (!QMetaObject::invokeMethod(workspace, "saveCurrentTimelineWorkspaceLayout"))
    return fail("Workspace preferences must be saved before recreation");
  QPointer<QObject> previous_workspace(workspace);
  // Keep background idle prewarming from immediately recreating the view that
  // this lifecycle check deliberately unloads.
  root->setProperty("recentUserInput", true);
  root->setProperty("currentWorkspace", root->property("libraryWorkspaceIndex"));
  root->setProperty("timelineWorkspaceLoaded", false);
  if (!wait_for_condition([&] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return previous_workspace.isNull();
      }, 10000, "timeline-ux-unload"))
    return fail("Workspace recreation must destroy the prior view and its detached windows");
  if (!QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, routed),
          Q_ARG(QVariant, root->property("timelineWorkspaceIndex"))) || !routed.toBool() ||
      !wait_for_condition([&] {
        workspace = active_workspace(root);
        return workspace && workspace->objectName() == "timelineWorkspace" &&
               named_item_visible(workspace, "timelineHeaderBar");
      }, 15000, "timeline-ux-reopen"))
    return fail("The actual Timeline workspace must reopen from its saved preferences");
  dock_state = find_named_object(workspace, QStringLiteral("timelinePanelDockState"));
  if (dock_state == nullptr || panel_dock(QStringLiteral("media")) != QStringLiteral("floating") ||
      panel_dock(QStringLiteral("inspector")) != QStringLiteral("floating") ||
      panel_dock(QStringLiteral("scopes")) != QStringLiteral("floating"))
    return fail("A fresh Timeline view must restore the serialized generic floating dock state");
  for (const auto& name : {"timelineDetachedSourceWindow", "timelineDetachedInspectorWindow",
                           "timelineProgramScopesWindow"}) {
    if (!wait_for_condition([&] {
          auto* detached = find_named_object(workspace, QString::fromLatin1(name));
          return detached && detached->property("visible").toBool();
        }, 10000, "timeline-ux-detached-restore"))
      return fail(QStringLiteral("Saved detached window must create its content on a fresh view: %1")
                      .arg(QString::fromLatin1(name)));
  }
  if (!action("timelineEditingWorkspaceAction") ||
      std::abs(workspace->property("sourcePanelWidth").toDouble() - 382.0) > 0.01 ||
      std::abs(workspace->property("monitorPanelHeight").toDouble() - resized_monitor_height) > 0.01)
    return fail(QStringLiteral("The actual reopened view must restore Editing's own panel and manual splitter preferences (source %1/382, monitor %2/%3)")
                    .arg(workspace->property("sourcePanelWidth").toDouble())
                    .arg(workspace->property("monitorPanelHeight").toDouble()).arg(resized_monitor_height));

  // A landscape sequence needs more horizontal room than the portrait fixture.
  // Exercise the real panel allocation at the narrow supported editing width.
  document->createTimelineWithSettings({{"timelineName", "Landscape layout proof"},
      {"width", 1920}, {"height", 1080}, {"frameRate", 30.0}});
  document->addClipFromGallery("timeline-ux-video", video_path, "Landscape camera", 10.0,
                              QUrl::fromLocalFile(source_path).toString());
  document->selectClip(0);
  window->resize(std::min(1180, available.width()), std::min(768, available.height()));
  if (!action("timelineColorWorkspaceAction") || !action("resetTimelineWorkspaceLayoutAction") ||
      !wait_for_condition([&] { return usable_layout(true); }, 3000,
                          "timeline-ux-landscape-color-layout")) {
    save_capture_if_requested(window, output_dir, "timeline-ux-landscape-layout-failure", &captures);
    for (const auto& name : {"timelineProgramMonitor", "timelineProgramMonitorStage", "timelineProgramAnimationToolbar", "timelineSurfacePanel"}) {
      auto* item = find_visible_named_item(workspace, QString::fromLatin1(name));
      const auto bounds = item_scene_rect(item);
      std::cerr << name << "=" << bounds.x() << ',' << bounds.y() << ' '
                << bounds.width() << 'x' << bounds.height() << '\n';
    }
    return fail("Landscape Color must preserve a 180 px preview and complete tracks at laptop width");
  }
  save_capture_if_requested(window, output_dir, "timeline-ux-color-landscape-laptop", &captures);
  if (captures.size() != 10)
    return fail("Workspace proof must capture both portrait and landscape laptop layouts");
  const QJsonObject result{{"schema", "timeline-workspace-ux-proof:v1"},
      {"captures", captures}, {"warmColorP95Ms", color_summary.p95_ms},
      {"firstColorLoadMs", first_color_load_ms},
      {"firstColorBudgetMs", 1000}, {"firstTimelineLoadMs", first_timeline_load_ms},
      {"timelineShellReadyMs", timeline_shell_ready_ms},
      {"firstTimelineBudgetMs", 1500},
      {"warmEditingP95Ms", edit_summary.p95_ms}, {"warmBudgetMs", 250},
      {"diagnosticAllGates", diagnose_all_gates},
      {"passed", all_performance_budgets_passed},
      {"gatePassed", QJsonObject{
          {"firstTimelineLoad", first_timeline_budget_passed},
          {"firstColorLoad", first_color_budget_passed},
          {"warmColorSwitch", warm_color_budget_passed},
          {"warmEditingSwitch", warm_editing_budget_passed},
          {"allPerformanceBudgets", all_performance_budgets_passed}}},
      {"headerMenusPointerExercised", true}, {"panelMenuBoundsChecked", true},
      {"windowActionsExercised", true}, {"workspaceStateIsolated", true},
      {"colorControlsReadyAfterRapidTabSwitch", true},
      {"taskPanelVisibilityRestored", true},
      {"realColorInputTransformExercised", true},
      {"actualSplitterDragExercised", true}, {"freshViewPreferencesRestored", true},
      {"detachedWindowsRestored", true},
      {"landscapeLaptopPreviewUsable", true},
      {"selectionAndPlayheadPreserved", true}};
  QFile receipt(QDir(output_dir).filePath("workspace-ux-proof.json"));
  if (!receipt.open(QIODevice::WriteOnly) || receipt.write(QJsonDocument(result).toJson()) <= 0)
    return fail("Workspace UX proof could not save its receipt");
  std::cout << QJsonDocument(result).toJson().constData();
  if (diagnose_all_gates && !all_performance_budgets_passed) {
    return fail(QStringLiteral("Diagnostic workspace UX run completed, but original budgets failed: Timeline %1/1500 ms, Color %2/1000 ms, warm Color p95 %3/250 ms, warm Editing p95 %4/250 ms")
                    .arg(first_timeline_load_ms).arg(first_color_load_ms)
                    .arg(color_summary.p95_ms).arg(edit_summary.p95_ms));
  }
  return EXIT_SUCCESS;
}

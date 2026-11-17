// Staged insertion after the actual-app Main.qml initial workspace is ready
// and before the Timeline benchmark branch / normal workflow prewarm wait.
if (arguments.contains(QStringLiteral("--idle-resource-proof"))) {
  const auto scenario = option_value(arguments, QStringLiteral("--idle-resource-proof"));
  const auto output_dir = option_value(arguments, QStringLiteral("--idle-resource-proof-dir"));
  if ((scenario != QStringLiteral("sampler") && scenario != QStringLiteral("prewarm")) ||
      output_dir.isEmpty() || !QDir().mkpath(output_dir)) {
    return fail(QStringLiteral(
        "Idle resource proof requires --idle-resource-proof sampler|prewarm and an output directory."));
  }
  if (scenario == QStringLiteral("prewarm")) {
    root_window->setWindowState(Qt::WindowMinimized);
  } else {
    root_window->showNormal();
    root_window->requestActivate();
  }
  process_layout_events(4);

  // Candidate A is measured only after startup/Edit construction settles. This
  // leaves the one-time prewarm out of the 10-second timer CPU comparison.
  if (scenario == QStringLiteral("sampler")) {
    root->setProperty("recentUserInput", false);
    const auto ready = [root]() {
      QVariant value;
      return QMetaObject::invokeMethod(
                 root, "editWorkspacePrewarmReady", Q_RETURN_ARG(QVariant, value)) &&
             value.toBool();
    };
    if (!wait_for_condition(ready, timeout_ms, "idle-resource-settle-edit"))
      return fail(QStringLiteral("Idle sampler proof did not settle startup Edit prewarm."));
    root_window->setWindowState(Qt::WindowMinimized);
    process_layout_events(4);
  }

  catalog_assets.selectIndex(0);
  if (scenario == QStringLiteral("prewarm")) {
    editor_preview.loadAsset(QStringLiteral("idle-resource-fixture"), source_path,
        {}, {}, {}, {}, {});
  }
  editor_preview.setExposure(0.27);
  using IdleEditorAccess = photo_editor::desktop::EditorPreviewControllerTestAccess;
  if (!wait_for_condition(
          [&]() { return !catalog_assets.loading() && IdleEditorAccess::all_idle(editor_preview); },
          timeout_ms, "idle-resource-settle-fixture")) {
    return fail(QStringLiteral("Idle resource proof could not settle its fixture before measuring."));
  }
  const auto selected_asset_before = catalog_assets.selectedAssetId();
  const auto recipe_before = IdleEditorAccess::recipe_json(editor_preview);

  auto* dispatcher = QAbstractEventDispatcher::instance(app.thread());
  if (dispatcher == nullptr)
    return fail(QStringLiteral("Idle resource proof has no application event dispatcher."));
  quint64 dispatcher_awake_count = 0;
  quint64 dispatcher_about_to_block_count = 0;
  const auto awake_connection = QObject::connect(
      dispatcher, &QAbstractEventDispatcher::awake, &app,
      [&dispatcher_awake_count]() { ++dispatcher_awake_count; });
  const auto block_connection = QObject::connect(
      dispatcher, &QAbstractEventDispatcher::aboutToBlock, &app,
      [&dispatcher_about_to_block_count]() { ++dispatcher_about_to_block_count; });

  const auto sample_property_index =
      root->metaObject()->indexOfProperty("qmlFrameSampleCount");
  if (sample_property_index < 0)
    return fail(QStringLiteral("Idle resource proof cannot observe qmlFrameSampleCount."));
  QSignalSpy sample_change_spy(
      root, root->metaObject()->property(sample_property_index).notifySignal());
  if (!sample_change_spy.isValid())
    return fail(QStringLiteral("Idle resource proof cannot connect to QML frame sample notifications."));

  QJsonObject before_memory;
  QJsonObject after_memory;
#ifdef Q_OS_MACOS
  const auto read_memory = [](QJsonObject* result) {
    mach_task_basic_info_data_t basic{};
    mach_msg_type_number_t basic_count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&basic), &basic_count) == KERN_SUCCESS) {
      result->insert(QStringLiteral("residentBytes"),
                     static_cast<qint64>(basic.resident_size));
    }
    task_vm_info_data_t vm{};
    mach_msg_type_number_t vm_count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&vm), &vm_count) == KERN_SUCCESS) {
      result->insert(QStringLiteral("physicalFootprintBytes"),
                     static_cast<qint64>(vm.phys_footprint));
    }
  };
  read_memory(&before_memory);
#endif

  const auto minimized_visibility = static_cast<int>(root_window->visibility());
  const auto minimized_visible = root_window->isVisible();
  const auto application_state_during_idle =
      static_cast<int>(QGuiApplication::applicationState());

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_IDLE_TRACE")) {
    QJsonArray active_work;
    for (auto* object : root->findChildren<QObject*>()) {
      const auto type = QString::fromLatin1(object->metaObject()->className());
      if ((!type.contains(QStringLiteral("Animation")) && !type.contains(QStringLiteral("Timer"))) ||
          !object->property("running").toBool()) continue;
      const auto* context = qmlContext(object);
      active_work.append(QJsonObject{{QStringLiteral("type"), type},
          {QStringLiteral("name"), object->objectName()},
          {QStringLiteral("source"), context ? context->baseUrl().toString() : QString{}},
          {QStringLiteral("interval"), object->property("interval").toInt()},
          {QStringLiteral("loops"), object->property("loops").toInt()},
          {QStringLiteral("paused"), object->property("paused").toBool()}});
    }
    QFile trace_file(QDir(output_dir).filePath(QStringLiteral("active-work.json")));
    if (trace_file.open(QIODevice::WriteOnly))
      trace_file.write(QJsonDocument(active_work).toJson(QJsonDocument::Indented));
  }
  const auto duration_ms = scenario == QStringLiteral("sampler") ? 10000 : 1500;
  const auto cpu_before = std::clock();
  QElapsedTimer observation;
  observation.start();
  QEventLoop idle_loop;
  QTimer::singleShot(duration_ms, &idle_loop, &QEventLoop::quit);
  idle_loop.exec();
  const auto wall_ms = observation.elapsed();
  const auto cpu_after = std::clock();
  const auto cpu_ms = cpu_after >= cpu_before
      ? 1000.0 * static_cast<double>(cpu_after - cpu_before) /
            static_cast<double>(CLOCKS_PER_SEC)
      : -1.0;
#ifdef Q_OS_MACOS
  read_memory(&after_memory);
#endif
  const auto sample_changes_during_idle = sample_change_spy.count();
  QObject::disconnect(awake_connection);
  QObject::disconnect(block_connection);

  const auto edit_ready = [root]() {
    QVariant ready;
    return QMetaObject::invokeMethod(
               root, "editWorkspacePrewarmReady", Q_RETURN_ARG(QVariant, ready)) &&
           ready.toBool();
  };
  const bool edit_loaded_while_minimized =
      root->property("editWorkspaceLoaded").toBool() || edit_ready();
  bool restored_foreground = false;
  bool foreground_stall_summary = false;
  bool route_ready = false;
  QString recipe_after;
  QString selected_asset_after = selected_asset_before;
  qint64 restore_to_edit_ready_ms = -1;

  root_window->showNormal();
  root_window->show();
  root_window->requestActivate();
  restored_foreground = wait_for_condition(
      [&]() {
        return root_window->isVisible() && root_window->visibility() != QWindow::Minimized &&
            QGuiApplication::applicationState() == Qt::ApplicationActive;
      }, 5000, "idle-resource-foreground-restore");
  if (!restored_foreground)
    return fail(QStringLiteral("Idle resource proof could not restore foreground eligibility."));

  if (scenario == QStringLiteral("prewarm")) {
    QElapsedTimer restore_timer;
    restore_timer.start();
    root->setProperty("recentUserInput", false);
    QVariant route_result;
    const auto edit_index = root->property("editWorkspaceIndex").toInt();
    if (!QMetaObject::invokeMethod(root, "switchToWorkspaceIndex",
                                   Q_RETURN_ARG(QVariant, route_result),
                                   Q_ARG(QVariant, QVariant(edit_index))) ||
        !route_result.toBool()) {
      return fail(QStringLiteral("Idle resource proof could not navigate to Edit after restore."));
    }
    route_ready = wait_for_condition(
        [&]() {
          auto* workspace = active_workspace(root);
          return workspace != nullptr &&
              workspace->objectName() == QStringLiteral("editStudioWorkspace") &&
              !root->property("workspaceTransitionActive").toBool();
        }, timeout_ms, "idle-resource-edit-route");
    if (!route_ready)
      return fail(QStringLiteral("Edit route did not become ready after deferred prewarm."));
    restore_to_edit_ready_ms = restore_timer.elapsed();
    recipe_after = IdleEditorAccess::recipe_json(editor_preview);
    selected_asset_after = catalog_assets.selectedAssetId();
    if (recipe_after != recipe_before || selected_asset_after != selected_asset_before)
      return fail(QStringLiteral("Deferred prewarm changed the selected asset or exact recipe state."));
  } else {
    recipe_after = IdleEditorAccess::recipe_json(editor_preview);
    selected_asset_after = catalog_assets.selectedAssetId();
    if (recipe_after != recipe_before || selected_asset_after != selected_asset_before)
      return fail(QStringLiteral("Sampler suspension changed the selected asset or exact recipe state."));
    const auto log_path = photo_editor::desktop::observability::desktopLogFilePath();
    const auto log_size_before = QFileInfo(log_path).size();
    root->setProperty("qmlFrameCaptureEnabled", true);
    root->setProperty("qmlFrameLastSampleAtMs",
                      qml_performance.nowMs() - 1250);
    foreground_stall_summary = wait_for_condition(
        [&]() {
          photo_editor::desktop::observability::flush();
          QFile log_file(log_path);
          if (!log_file.open(QIODevice::ReadOnly) || !log_file.seek(log_size_before))
            return false;
          const auto lines = log_file.readAll().split('\n');
          for (const auto& line : lines) {
            const auto event = QJsonDocument::fromJson(line).object();
            const auto fields = event.value(QStringLiteral("fields")).toObject();
            if (event.value(QStringLiteral("event")).toString() ==
                    QStringLiteral("qml.frame.summary") &&
                fields.value(QStringLiteral("max_frame_interval_ms")).toDouble() > 1000.0 &&
                fields.value(QStringLiteral("jank_frame_count")).toInt() > 0 &&
                fields.value(QStringLiteral("dropped_frame_count")).toInt() > 0) {
              return true;
            }
          }
          return false;
        }, 3000, "idle-resource-frame-stall-summary");
    if (!foreground_stall_summary)
      return fail(QStringLiteral("Foreground >1 second frame-stall summary was not emitted after restore."));
  }

  QJsonObject receipt{
      {QStringLiteral("contract"), QStringLiteral("windowless-idle-resource-proof:v1")},
      {QStringLiteral("scenario"), scenario},
      {QStringLiteral("qpa"), QGuiApplication::platformName()},
      {QStringLiteral("windowVisibilityDuringIdle"), minimized_visibility},
      {QStringLiteral("windowVisibleDuringIdle"), minimized_visible},
      {QStringLiteral("applicationStateDuringIdle"), application_state_during_idle},
      {QStringLiteral("applicationStateAfterRestore"), static_cast<int>(QGuiApplication::applicationState())},
      {QStringLiteral("observationWallMs"), wall_ms},
      {QStringLiteral("processCpuMs"), cpu_ms},
      {QStringLiteral("dispatcherAwakeCount"), static_cast<qint64>(dispatcher_awake_count)},
      {QStringLiteral("dispatcherAboutToBlockCount"), static_cast<qint64>(dispatcher_about_to_block_count)},
      {QStringLiteral("qmlFrameSamplePropertyNotifications"), sample_changes_during_idle},
      {QStringLiteral("editLoadedWhileMinimized"), edit_loaded_while_minimized},
      {QStringLiteral("restoreToEditRouteReadyMs"), restore_to_edit_ready_ms},
      {QStringLiteral("routeReady"), route_ready},
      {QStringLiteral("foregroundStallSummary"), foreground_stall_summary},
      {QStringLiteral("selectedAssetBefore"), selected_asset_before},
      {QStringLiteral("selectedAssetAfter"), selected_asset_after},
      {QStringLiteral("recipeBefore"), recipe_before},
      {QStringLiteral("recipeAfter"), recipe_after},
      {QStringLiteral("memoryBefore"), before_memory},
      {QStringLiteral("memoryAfter"), after_memory},
  };
  QFile receipt_file(QDir(output_dir).filePath(scenario + QStringLiteral(".json")));
  if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      receipt_file.write(QJsonDocument(receipt).toJson(QJsonDocument::Indented)) <= 0)
    return fail(QStringLiteral("Idle resource proof could not write its receipt."));
  std::cout << QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData() << '\n';
  return EXIT_SUCCESS;
}

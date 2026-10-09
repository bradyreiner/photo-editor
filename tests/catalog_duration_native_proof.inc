    // Included inside the actual-app driver so this uses its real shell,
    // Library, isolated settings, image providers, and native window.
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
      return fail("Duration presentation proof requires a native cocoa window.");
    struct IgnoreExternalInput final : QObject {
      using QObject::QObject;
      int ignored = 0;
      bool eventFilter(QObject*, QEvent* event) override {
        if (event->spontaneous() && (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick ||
            event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease || event->type() == QEvent::Wheel)) {
          ++ignored; return true;
        }
        return false;
      }
    } external_input_filter;
    root_window->installEventFilter(&external_input_filter);
    const auto probe_fixture = option_value(arguments, "--duration-probe-fixture");
    if (!QFileInfo(probe_fixture).isExecutable() || capture_dir.isEmpty())
      return fail("Duration proof needs a compiled probe fixture and capture directory.");
    const QDir duration_root(fixture_dir.filePath("duration-probes"));
    const auto binaries = duration_root.filePath("bin");
    if (!QDir().mkpath(binaries) || !QFile::link(probe_fixture, QDir(binaries).filePath("ffprobe")))
      return fail("Cannot install isolated duration fixture.");
    const auto previous_path = qgetenv("PATH");
    qputenv("PATH", binaries.toUtf8());
    qputenv("PHOTO_EDITOR_DURATION_FIXTURE_ROOT", duration_root.absolutePath().toUtf8());
    const auto restore_duration_environment = qScopeGuard([&]() {
      qputenv("PATH", previous_path);
      qunsetenv("PHOTO_EDITOR_DURATION_FIXTURE_ROOT");
    });
    const auto write_duration_file = [](const QString& path, const QByteArray& bytes) {
      QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    const auto probe_count = [&]() {
      QFile file(duration_root.filePath("counter"));
      return file.open(QIODevice::ReadOnly) ? file.readAll().toInt() : 0;
    };
    QVector<photo_editor::desktop::CatalogAssetItem> duration_items;
    for (int n = 0; n < 1000; ++n) {
      auto item = make_asset(QStringLiteral("duration-%1").arg(n), source_path, QUrl::fromLocalFile(source_path).toString());
      if (n % 10 >= 3) {
        item.media_type = n % 10 >= 7 ? QStringLiteral("audio") : QStringLiteral("video");
        item.file_path = duration_root.filePath(QStringLiteral("media-%1.mp4").arg(n));
        item.online = n % 10 != 3;
        if (item.online && !write_duration_file(item.file_path, "8")) return fail("Cannot create duration media fixture.");
      }
      duration_items.append(item);
    }
    catalog_assets.setInteractiveRenderingActive(true); // Isolate duration work from thumbnail decoding.
    catalog_assets.setMediaTypeFilter("all");
    QElapsedTimer publication_timer; publication_timer.start();
    catalog_assets.replaceAssetsForTesting(duration_items);
    const auto publication_ms = publication_timer.elapsed();
    if (catalog_assets.rowCount() != 1000 || !wait_for_condition([&]() { return probe_count() == 2; }, 3000))
      return fail("Large Library rows were not published before held probes finished.");
    auto* workspace = active_workspace(root);
    auto* grid = workspace ? workspace->findChild<QQuickItem*>("studioLibraryGalleryGrid") : nullptr;
    if (!grid || !grid->isVisible()) return fail("Large Library grid did not become visible.");
    process_layout_events(8);
    QJsonArray captures, wheel_samples;
    save_capture_if_requested(root_window, capture_dir, "duration-held-top", &captures);
    QElapsedTimer heartbeat_clock; heartbeat_clock.start();
    qint64 heartbeat_last = 0, heartbeat_gap = 0;
    int heartbeat_count = 0;
    bool workspace_stayed_library = true;
    QTimer heartbeat; heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&]() {
      const auto now = heartbeat_clock.elapsed();
      heartbeat_gap = std::max(heartbeat_gap, now - heartbeat_last);
      heartbeat_last = now; ++heartbeat_count;
      workspace_stayed_library = workspace_stayed_library &&
          root->property("currentWorkspace") == root->property("libraryWorkspaceIndex");
    });
    heartbeat.start();
    double wheel_max_ms = 0;
    bool all_wheels_presented = true;
    for (int n = 0; n < 12; ++n) {
      const auto point = grid->mapToScene(QPointF(grid->width() * 0.5, grid->height() * 0.5));
      const auto before_y = grid->property("contentY").toDouble();
      QWheelEvent wheel(point, root_window->mapToGlobal(point.toPoint()), QPoint(0, -180), QPoint(0, -120),
          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
      bool presented = false;
      QEventLoop loop;
      QTimer timeout; timeout.setSingleShot(true);
      const auto frame_connection = QObject::connect(root_window, &QQuickWindow::frameSwapped, &loop, [&]() {
        presented = true; loop.quit();
      });
      QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
      QElapsedTimer wheel_timer; wheel_timer.start();
      timeout.start(1000);
      QCoreApplication::sendEvent(root_window, &wheel);
      if (!presented) loop.exec();
      const double elapsed = double(wheel_timer.nsecsElapsed()) / 1e6;
      timeout.stop(); QObject::disconnect(frame_connection);
      const bool moved = grid->property("contentY").toDouble() > before_y + 0.5;
      all_wheels_presented = all_wheels_presented && presented && moved;
      wheel_max_ms = std::max(wheel_max_ms, elapsed);
      wheel_samples.append(QJsonObject{{"ms", elapsed}, {"presented", presented}, {"moved", moved}});
    }
    const auto held_count = probe_count();
    const auto held_heartbeat_gap = heartbeat_gap;
    heartbeat.stop();
    save_capture_if_requested(root_window, capture_dir, "duration-held-scrolled", &captures);
    for (const auto& item : duration_items)
      if (item.media_type != "photo" && item.online &&
          !write_duration_file(duration_root.filePath("release-" + QFileInfo(item.file_path).fileName()), "go"))
        return fail("Cannot release duration fixture.");
    double max_poll_ms = 0;
    const auto all_durations_ready = [&]() {
      QElapsedTimer poll; poll.start();
      int found = 0;
      // Compact gallery roles avoid projecting recipes just for the probe count.
      for (int n = 0; n < catalog_assets.rowCount(); ++n)
        if (catalog_assets.data(catalog_assets.index(n, 0), photo_editor::desktop::CatalogAssetModel::DurationSecondsRole).toDouble() == 8.0)
          ++found;
      max_poll_ms = std::max(max_poll_ms, double(poll.nsecsElapsed()) / 1e6);
      return found == 600;
    };
    // Keep the native event loop running normally. The general driver's
    // wait_for_condition sleeps 25 ms between pumps, which throttles completion
    // delivery and rendering and is unsuitable for this responsiveness gate.
    const auto wait_for_durations = [&](int deadline_ms) {
      if (all_durations_ready()) return true;
      QEventLoop loop;
      QTimer readiness, deadline;
      readiness.setInterval(10); deadline.setSingleShot(true);
      QObject::connect(&readiness, &QTimer::timeout, &loop, [&]() {
        if (all_durations_ready()) loop.quit();
      });
      QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
      readiness.start(); deadline.start(deadline_ms); loop.exec();
      return all_durations_ready();
    };
    heartbeat_gap = 0; heartbeat_last = heartbeat_clock.elapsed(); heartbeat.start();
    QElapsedTimer drain_timer; drain_timer.start();
    const bool cold_ready = wait_for_durations(90000);
    const auto cold_drain_ms = drain_timer.elapsed();
    const auto cold_probe_count = probe_count();
    const auto drain_heartbeat_gap = heartbeat_gap;
    heartbeat_gap = 0; heartbeat_last = heartbeat_clock.elapsed();
    QElapsedTimer warm_timer; warm_timer.start();
    catalog_assets.replaceAssetsForTesting(duration_items);
    const auto warm_publication_ms = warm_timer.elapsed();
    const bool warm_ready = wait_for_durations(15000);
    const auto warm_ready_ms = warm_timer.elapsed();
    const auto warm_heartbeat_gap = heartbeat_gap;
    heartbeat.stop();
    const auto warm_probe_count = probe_count();
    QVariant prewarm_ready;
    const bool prewarm_complete = QMetaObject::invokeMethod(root, "editWorkspacePrewarmReady",
        Q_RETURN_ARG(QVariant, prewarm_ready)) && prewarm_ready.toBool();
    save_capture_if_requested(root_window, capture_dir, "duration-warm-ready", &captures);
    const bool accepted = publication_ms <= 100 && held_count == 2 && all_wheels_presented &&
        wheel_max_ms <= 100 && held_heartbeat_gap <= 100 && heartbeat_count > 0 &&
        cold_ready && cold_probe_count == 600 && warm_ready && warm_probe_count == cold_probe_count &&
        drain_heartbeat_gap <= 100 && warm_heartbeat_gap <= 100 && warm_publication_ms <= 100 && prewarm_complete &&
        workspace_stayed_library;
    const QJsonObject receipt{{"accepted", accepted}, {"platform", QGuiApplication::platformName()},
        {"qmlMode", "current source Main.qml; not packaged compiled QML"},
        {"inputMode", "window-delivered Qt wheel events plus frameSwapped; not OS input-to-photon"},
        {"rows", 1000}, {"onlineMediaPaths", 600}, {"offlineMediaPaths", 100},
        {"publicationMs", publication_ms}, {"heldProbeCount", held_count},
        {"wheelMaxMs", wheel_max_ms}, {"wheelSamples", wheel_samples},
        {"heldHeartbeatMaxGapMs", held_heartbeat_gap}, {"drainHeartbeatMaxGapMs", drain_heartbeat_gap},
        {"warmHeartbeatMaxGapMs", warm_heartbeat_gap}, {"maxPollMs", max_poll_ms},
        {"editPrewarmComplete", prewarm_complete}, {"completionWait", "native QEventLoop with 10ms readiness timer"},
        {"workspaceStayedLibrary", workspace_stayed_library}, {"ignoredExternalInputEvents", external_input_filter.ignored},
        {"coldDrainMs", cold_drain_ms}, {"coldProbeCount", cold_probe_count},
        {"warmPublicationMs", warm_publication_ms}, {"warmReadyMs", warm_ready_ms},
        {"warmProbeCount", warm_probe_count}, {"coldReady", cold_ready}, {"warmReady", warm_ready},
        {"captures", captures}};
    QFile receipt_file(QDir(capture_dir).filePath("duration-receipt.json"));
    if (!receipt_file.open(QIODevice::WriteOnly) || receipt_file.write(QJsonDocument(receipt).toJson()) < 0)
      return fail("Cannot write duration proof receipt.");
    std::cout << QJsonDocument(receipt).toJson().constData();
    return accepted ? EXIT_SUCCESS : fail("Large Library duration runtime gate failed; see receipt.");

    if (QGuiApplication::platformName() != "cocoa" || capture_dir.isEmpty())
      return fail("Membership proof requires a native cocoa window and capture directory.");
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

    const QDir helper_root(qEnvironmentVariable("PHOTO_EDITOR_MEMBERSHIP_FIXTURE_ROOT"));
    const auto read_fixture = [&](const QString& name) {
      QFile file(helper_root.filePath(name));
      return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    const auto helper_count = [&]() { return read_fixture("counter").toInt(); };
    const auto release_helper = [&]() {
      QFile file(helper_root.filePath("release-1"));
      return file.open(QIODevice::WriteOnly) && file.write("go") == 2;
    };
    const auto cleanup = qScopeGuard([&]() { release_helper(); });
    // Isolate the save interaction after retained workspace loading. Cold prewarm
    // has its own failing receipt; it is not included in this warm-workspace gate.
    const auto wait_native = [&](const std::function<bool()>& ready, int milliseconds) {
      if (ready()) return true;
      QEventLoop loop; QTimer poll, deadline;
      poll.setInterval(10); deadline.setSingleShot(true);
      QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (ready()) loop.quit(); });
      QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
      poll.start(); deadline.start(milliseconds); loop.exec(); return ready();
    };
    if (!wait_native([&]() {
          QVariant result;
          return QMetaObject::invokeMethod(root, "editWorkspacePrewarmReady", Q_RETURN_ARG(QVariant, result)) && result.toBool();
        }, 15000)) return fail("Retained Edit workspace did not finish prewarming.");
    QVector<photo_editor::desktop::CatalogAssetItem> items;
    const auto thumbnail = QImage(source_path).scaled(QSize(240, 160), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (thumbnail.isNull()) return fail("Cannot prepare membership thumbnails.");
    for (int n = 0; n < 1000; ++n) {
      const auto path = fixture_dir.filePath(QStringLiteral("membership-thumb-%1.png").arg(n));
      if (!thumbnail.save(path)) return fail("Cannot write membership thumbnail.");
      auto item = make_asset(QStringLiteral("asset-%1").arg(n), source_path, QUrl::fromLocalFile(path).toString());
      item.online = false; // Cached thumbnails remain viewable with originals offline.
      items.append(item);
    }
    catalog_assets.replaceAssetsForTesting(items);
    catalog_assets.selectAll();
    process_layout_events(8);
    auto* workspace = active_workspace(root);
    auto* grid = workspace ? workspace->findChild<QQuickItem*>("studioLibraryGalleryGrid") : nullptr;
    if (!grid || !grid->isVisible()) return fail("Membership Library grid is not visible.");
    const auto visible_thumbnail_readiness = [&]() {
      QPair<int, int> counts{0, 0};
      const auto viewport_bounds = grid->mapRectToScene(QRectF(0, 0, grid->width(), grid->height()));
      std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
        if (item->objectName() == "libraryGalleryTile" && item->isVisible()) {
          const auto tile_bounds = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
          if (tile_bounds.intersects(viewport_bounds)) {
            ++counts.first;
            std::function<QQuickItem*(QQuickItem*)> find_image = [&](QQuickItem* parent) -> QQuickItem* {
              for (auto* child : parent->childItems()) {
                if (child->objectName() == "libraryGalleryImage")
                  return child;
                if (auto* nested = find_image(child))
                  return nested;
              }
              return nullptr;
            };
            if (auto* image = find_image(item); image != nullptr && image->property("status").toInt() == 1)
              ++counts.second;
          }
        }
        for (auto* child : item->childItems())
          visit(child);
      };
      visit(grid);
      return counts;
    };
    const auto visible_ready = [&]() {
      const auto counts = visible_thumbnail_readiness(); return counts.first > 0 && counts.first == counts.second;
    };
    if (!wait_native(visible_ready, 5000)) return fail("Membership thumbnails did not become visible before saving.");
    QJsonArray captures, wheel_samples;
    save_capture_if_requested(root_window, capture_dir, "membership-before", &captures);
    int resets = 0;
    const auto reset_connection = QObject::connect(&catalog_assets, &QAbstractItemModel::modelReset,
        &app, [&]() { ++resets; });
    const auto disconnect_reset = qScopeGuard([&]() { QObject::disconnect(reset_connection); });
    QElapsedTimer heartbeat_clock; heartbeat_clock.start();
    qint64 heartbeat_last = 0, heartbeat_gap = 0;
    bool stayed_library = true;
    QTimer heartbeat; heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&]() {
      const auto now = heartbeat_clock.elapsed(); heartbeat_gap = std::max(heartbeat_gap, now - heartbeat_last);
      heartbeat_last = now;
      stayed_library = stayed_library && root->property("currentWorkspace") == root->property("libraryWorkspaceIndex");
    });
    heartbeat.start();
    QElapsedTimer operation; operation.start();
    catalog_assets.createProjectWithSelectedAssets("Native batch");
    const auto command_ms = operation.elapsed();
    if (!wait_native([&]() { return helper_count() == 1; }, 3000)) return fail("Membership save helper did not start.");
    const auto request = QJsonDocument::fromJson(read_fixture("request-1.json")).object();
    QSet<QString> sent_ids;
    for (const auto& value : request.value("added").toArray()) sent_ids.insert(value.toString());
    bool exact_ids = sent_ids.size() == 1000 && request.value("removed").toArray().isEmpty();
    for (const auto& item : items) exact_ids = exact_ids && sent_ids.contains(item.asset_id);
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

    const bool scrolled_thumbnails_ready = wait_native(visible_ready, 3000);
    const auto thumbnail_counts = visible_thumbnail_readiness();
    const bool held_one = helper_count() == 1;
    const auto held_gap = heartbeat_gap;
    heartbeat.stop();
    save_capture_if_requested(root_window, capture_dir, "membership-held-scrolled", &captures);
    heartbeat_last = heartbeat_clock.elapsed(); heartbeat_gap = 0; heartbeat.start();
    QElapsedTimer completion; completion.start();
    const bool saved = release_helper() && wait_native([&]() { return catalog_assets.status().startsWith("Saved project"); }, 5000);
    const auto completion_ms = completion.elapsed();
    const auto completion_gap = heartbeat_gap; heartbeat.stop();
    const bool accepted = command_ms <= 100 && resets == 1 && exact_ids && held_one && saved &&
        helper_count() == 1 && catalog_assets.assetCountForProject("Native batch") == 1000 &&
        held_gap <= 100 && completion_gap <= 100 && all_wheels_presented && wheel_max_ms <= 100 && stayed_library && scrolled_thumbnails_ready;
    const QJsonObject receipt{{"accepted", accepted}, {"rows", 1000}, {"commandMs", command_ms},
        {"visibleThumbnailCount", thumbnail_counts.first}, {"readyThumbnailCount", thumbnail_counts.second},
        {"resets", resets}, {"helperCount", helper_count()}, {"exactIds", exact_ids},
        {"saved", saved}, {"heldOneHelper", held_one}, {"completionMs", completion_ms},
        {"heldHeartbeatMaxGapMs", held_gap}, {"completionHeartbeatMaxGapMs", completion_gap},
        {"wheelMaxMs", wheel_max_ms}, {"wheelSamples", wheel_samples}, {"workspaceStayedLibrary", stayed_library},
        {"ignoredExternalInputEvents", external_input_filter.ignored}, {"captures", captures},
        {"persistenceMode", qEnvironmentVariableIsSet("PHOTO_EDITOR_MEMBERSHIP_REAL_HELPER")
            ? "gated real catalog_state helper" : "synthetic helper"},
        {"scope", "Warm source-QML native Library, direct controller save command, window-delivered wheel events/frameSwapped, 1000 unique cached synthetic thumbnails with offline originals; database readback belongs to the external orchestrator; no cold-start claim"}};
    QFile output(QDir(capture_dir).filePath("membership-receipt.json"));
    if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(receipt).toJson()) < 0)
      return fail("Cannot write membership receipt.");
    std::cout << QJsonDocument(receipt).toJson().constData();
    return accepted ? EXIT_SUCCESS : fail("Native membership gate failed; see receipt.");

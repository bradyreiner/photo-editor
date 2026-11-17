    const auto redesign_proof_dir =
        QString::fromUtf8(qgetenv("PHOTO_EDITOR_IMPORT_REDESIGN_PROOF_DIR"));
    if (redesign_proof_dir.trimmed().isEmpty() || !QDir().mkpath(redesign_proof_dir)) {
      return fail("Import redesign proof requires a writable PHOTO_EDITOR_IMPORT_REDESIGN_PROOF_DIR");
    }
    auto* redesign_window = qobject_cast<QQuickWindow*>(import_root);
    if (redesign_window == nullptr) {
      return fail("Import redesign proof requires the real Main.qml Qt Quick window");
    }
    redesign_window->show();
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
      activate_native_test_application();
      redesign_window->raise();
      redesign_window->requestActivate();
    }
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER") &&
        !wait_for_condition([&]() {
          return redesign_window->isVisible() && redesign_window->isExposed() &&
              redesign_window->winId() != 0 &&
              QApplication::platformName() == QStringLiteral("cocoa");
        }, 10000)) {
      return fail("Import redesign proof requires an exposed Cocoa window when native pointer proof is enabled");
    }

    QJsonArray redesign_manifest;
    const auto source_root = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR));
    const auto fixture_sources = runtime_root.filePath(QStringLiteral("Client projects - Northern Lights expedition - Original camera files and matching RAW plus JPEG photographs"));
    if (!QDir().mkpath(fixture_sources)) {
      return fail("Import redesign proof should use an isolated temporary photo source");
    }
    const QStringList fixture_relatives{
        QStringLiteral("tests/fixtures/creative-tools/landscape-mountain-source.jpg"),
        QStringLiteral("marketing/media/stock/product-glass-bottle.jpg"),
        QStringLiteral("marketing/media/stock/night-hong-kong-typhoon.jpg"),
    };
    QVariantList fixture_paths;
    for (const auto& relative : fixture_relatives) {
      const auto source = source_root.filePath(relative);
      const auto target = QDir(fixture_sources).filePath(QFileInfo(source).fileName());
      if (!QFileInfo(source).isFile() || !QFile::copy(source, target)) {
        return fail("Import redesign proof should copy repository-owned stock photos into its temporary source");
      }
      fixture_paths.push_back(target);
    }
    const auto fixture_count = static_cast<int>(fixture_paths.size());
    const auto inside_window = [&](QQuickItem* item) {
      return item_visible(item) &&
          contains_with_tolerance(
              QRectF(0, 0, redesign_window->width(), redesign_window->height()), scene_rect(item));
    };
    const auto same_bounds = [](const QRectF& left, const QRectF& right) {
      constexpr qreal tolerance = 0.5;
      return std::abs(left.x() - right.x()) <= tolerance &&
          std::abs(left.y() - right.y()) <= tolerance &&
          std::abs(left.width() - right.width()) <= tolerance &&
          std::abs(left.height() - right.height()) <= tolerance;
    };
    const auto import_layout_ready = [&](QRectF* navigation_bounds, QRectF* page_bounds) {
      auto* navigation = find_named_item(import_root, QStringLiteral("appNavigationBar"));
      auto* import_page = find_named_item(import_root, QStringLiteral("importWorkspace"));
      if (!item_visible(navigation) || !item_visible(import_page) ||
          import_root->property("workspaceTransitionActive").toBool() ||
          redesign_window->width() <= 0.0 || redesign_window->height() <= 0.0) {
        return false;
      }
      const auto navigation_rect = scene_rect(navigation);
      const auto page_rect = scene_rect(import_page);
      if (navigation_rect.height() <= 0.0 || page_rect.height() <= 0.0 ||
          page_rect.top() < navigation_rect.bottom() - 2.0 ||
          page_rect.bottom() > redesign_window->height() + 2.0) {
        return false;
      }
      // Nested layouts can still be resolving a source/path label after the
      // shell itself has stopped moving. Include the actual action and media
      // surfaces in convergence rather than sampling them on one event turn.
      if (!workspace->property("importProjectChooserOpen").toBool()) {
        auto* primary = find_named_item(import_root, QStringLiteral("importPersistentPrimaryButton"));
        if (!inside_window(primary) ||
            !visible_named_items_do_not_overlap(import_root,
                {QStringLiteral("importSourcePanel"), QStringLiteral("importMainPanel"),
                 QStringLiteral("importDestinationPanel")}) ||
            !visible_named_items_do_not_overlap(import_root,
                {QStringLiteral("importHeaderBar"), QStringLiteral("importProgressSummaryCard"),
                 QStringLiteral("importMediaFilterBar"), QStringLiteral("importBrowseTools"),
                 QStringLiteral("importMediaGrid")})) return false;
      }
      if (navigation_bounds != nullptr) *navigation_bounds = navigation_rect;
      if (page_bounds != nullptr) *page_bounds = page_rect;
      return true;
    };
    const auto settle_import_layout = [&](int width, int height) {
      import_root->setProperty("width", width);
      import_root->setProperty("height", height);
      QRectF previous_navigation_bounds;
      QRectF previous_page_bounds;
      int stable_observations = 0;
      const auto stable_layout = [&]() {
        QRectF navigation_bounds;
        QRectF page_bounds;
        if (!import_layout_ready(&navigation_bounds, &page_bounds) ||
            redesign_window->width() != width || redesign_window->height() != height) {
          stable_observations = 0;
          return false;
        }
        if (same_bounds(navigation_bounds, previous_navigation_bounds) &&
            same_bounds(page_bounds, previous_page_bounds)) {
          ++stable_observations;
        } else {
          stable_observations = 1;
          previous_navigation_bounds = navigation_bounds;
          previous_page_bounds = page_bounds;
        }
        return stable_observations >= 3;
      };
      if (wait_for_condition(stable_layout, 5000)) return true;
      std::cerr << "Import layout did not settle at " << width << 'x' << height
                << "; actual " << redesign_window->width() << 'x' << redesign_window->height() << '\n';
      for (const auto* name : {"appNavigationBar", "importWorkspace", "importPersistentActionBar", "importPersistentPrimaryButton", "importSourcePanel", "importMainPanel", "importHeaderBar", "importProgressSummaryCard", "importMediaFilterBar", "importBrowseTools", "importMediaGrid"}) {
        auto* item = find_named_item(import_root, QString::fromLatin1(name));
        const auto rect = item ? scene_rect(item) : QRectF{};
        std::cerr << name << " visible=" << item_visible(item) << " bounds=" << rect.x() << ',' << rect.y() << ',' << rect.width() << ',' << rect.height() << '\n';
      }
      redesign_window->grabWindow().save(QDir(redesign_proof_dir).filePath(QStringLiteral("failed-layout.png")));
      return false;
    };
    const auto bounds_json = [](const QRectF& bounds) {
      return QJsonObject{
          {QStringLiteral("x"), bounds.x()},
          {QStringLiteral("y"), bounds.y()},
          {QStringLiteral("width"), bounds.width()},
          {QStringLiteral("height"), bounds.height()},
      };
    };
    const auto preview_tiles_ready = [&]() {
      for (int index = 0; index < fixture_count; ++index) {
        auto* tile = find_named_item(
            import_root, QStringLiteral("importMediaTile_%1").arg(index));
        if (!item_visible(tile) || !tile->property("previewReady").toBool()) return false;
      }
      return true;
    };
    const auto capture_import_state = [&](
                                           const QString& state,
                                           int width,
                                           int height,
                                           const QStringList& required_objects,
                                           bool require_photo_previews = false) {
      if (!settle_import_layout(width, height)) return false;
      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER") &&
          (!redesign_window->isActive() || !redesign_window->isExposed())) {
        activate_native_test_window(redesign_window);
        if (!wait_for_condition([&] {
              return redesign_window->isActive() && redesign_window->isExposed();
            }, 5000)) {
          std::cerr << "Native Import capture requires an active exposed window after resizing\n";
          return false;
        }
      }
      // GUI layout bounds can settle before the threaded scene graph has
      // rendered the resized shell. Wait for fresh frames before readback.
      QObject frame_scope;
      int rendered_frames = 0;
      QObject::connect(redesign_window, &QQuickWindow::frameSwapped, &frame_scope,
                       [&]() { ++rendered_frames; });
      if (!wait_for_condition([&]() {
            redesign_window->requestUpdate();
            return rendered_frames >= 2;
          }, 5000)) {
        std::cerr << "Import frame capture stalled: " << state.toStdString()
                  << " frames=" << rendered_frames << " exposed=" << redesign_window->isExposed()
                  << " active=" << redesign_window->isActive() << '\n';
        return false;
      }
      if (require_photo_previews && !wait_for_condition(preview_tiles_ready, 15000)) {
        std::cerr << "Import preview images did not decode before capture: " << state.toStdString() << '\n';
        return false;
      }
      QRectF navigation_bounds;
      QRectF page_bounds;
      if (!import_layout_ready(&navigation_bounds, &page_bounds)) return false;
      const QJsonObject metadata{
          {QStringLiteral("shellNavigationBounds"), bounds_json(navigation_bounds)},
          {QStringLiteral("importWorkspaceBounds"), bounds_json(page_bounds)},
      };
      return capture_visual_proof(
                 redesign_window, import_root, redesign_proof_dir, state, width, height,
                 required_objects, &redesign_manifest, metadata) &&
          wait_for_condition([&]() { return import_layout_ready(nullptr, nullptr); }, 1000);
    };

    // Start from the actual ImportView no-source state. The runtime source
    // chooser can have populated a default location during component startup.
    workspace->setProperty("importProjectChooserOpen", false);
    workspace->setProperty("sourceRootPath", QString{});
    workspace->setProperty("sourceRootUrl", QString{});
    workspace->setProperty("sourceLabel", QString{});
    workspace->setProperty("sourceRootIsFileSelection", false);
    workspace->setProperty("stagedPickerFilePaths", QVariantList{});
    workspace->setProperty("currentFolderPath", QString{});
    workspace->setProperty("currentFolderUrl", QString{});
    workspace->setProperty("pendingFolderPath", QString{});
    workspace->setProperty("pendingFolderUrl", QString{});
    QMetaObject::invokeMethod(workspace, "clearSelection");
    process_layout_events(8);
    if (!catalog_recovery_hidden_for_fixture()) {
      return fail("Import no-source proof should keep the fixture catalog startup error out of the rendered page");
    }
    auto* no_source_state = find_named_item(import_root, QStringLiteral("importNoSourceState"));
    auto* choose_files = find_named_item(import_root, QStringLiteral("importNoSourceChooseFilesButton"));
    auto* choose_folder = find_named_item(import_root, QStringLiteral("importNoSourceChooseFolderButton"));
    auto* primary_action = find_named_object(import_root, QStringLiteral("importPersistentPrimaryButton"));
    if (workspace->property("hasSource").toBool() ||
        workspace->property("canImportSelection").toBool() ||
        !item_visible(no_source_state) || !item_visible(choose_files) || !item_visible(choose_folder) ||
        !choose_files->property("enabled").toBool() || !choose_folder->property("enabled").toBool() ||
        !primary_action || primary_action->property("enabled").toBool() ||
        primary_action->property("disabledReason").toString().isEmpty()) {
      return fail("Import no-source state should offer file and folder choices while keeping import disabled");
    }
    if (!capture_import_state(
            QStringLiteral("import-no-source"), 1440, 900,
            {QStringLiteral("importWorkspace"), QStringLiteral("importNoSourceState"),
             QStringLiteral("importNoSourceChooseFilesButton"),
             QStringLiteral("importNoSourceChooseFolderButton")}) ||
        !inside_window(choose_files) || !inside_window(choose_folder) ||
        overlaps_with_tolerance(scene_rect(choose_files), scene_rect(choose_folder))) {
      return fail("Import no-source actions should render within the window without overlapping");
    }
    if (!capture_import_state(
            QStringLiteral("import-no-source-standard"), 1280, 760,
            {QStringLiteral("importWorkspace"), QStringLiteral("importNoSourceState"),
             QStringLiteral("importNoSourceChooseFilesButton"),
             QStringLiteral("importNoSourceChooseFolderButton")}) ||
        !inside_window(choose_files) || !inside_window(choose_folder) ||
        overlaps_with_tolerance(scene_rect(choose_files), scene_rect(choose_folder)) ||
        workspace->property("hasSource").toBool() ||
        workspace->property("canImportSelection").toBool() ||
        primary_action->property("enabled").toBool() ||
        primary_action->property("disabledReason").toString().isEmpty()) {
      return fail("Import no-source actions should remain usable and import disabled at standard size");
    }

    // Exercise the same local URL admission route used by a native drop.
    const auto drop_urls = [&](const QList<QUrl>& urls, bool capture_overlay = false) {
      QMimeData mime;
      mime.setUrls(urls);
      const auto point = scene_rect(qobject_cast<QQuickItem*>(workspace)).center().toPoint();
      QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(redesign_window, &enter);
      QDragMoveEvent move(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(redesign_window, &move);
      process_layout_events(3);
      if (!enter.isAccepted()) return false;
      if (capture_overlay && !capture_import_state(
              QStringLiteral("import-drop-target"), 1280, 760,
              {QStringLiteral("importWorkspace"), QStringLiteral("importDropArea")})) return false;
      QDropEvent drop(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(redesign_window, &drop);
      process_layout_events(3);
      return drop.isAccepted();
    };
    if (!drop_urls({QUrl::fromLocalFile(fixture_sources)}) ||
        workspace->property("sourceRootIsFileSelection").toBool() ||
        workspace->property("selectedFileCount").toInt() != 0 ||
        workspace->property("importProjectChooserOpen").toBool()) {
      return fail("Dropping a folder should browse it without selecting or importing files automatically");
    }
    const auto original_import_scale = app_shell.interfaceScale();
    for (const auto scale : {0.9, 1.0, 2.0}) {
      app_shell.setInterfaceScale(scale);
      if (!capture_import_state(
          QStringLiteral("import-long-folder-%1-percent").arg(qRound(scale * 100)), 1280, 800,
          {QStringLiteral("importWorkspace"), QStringLiteral("importWorkspacePanels"),
           QStringLiteral("importSourceScrollArea"), QStringLiteral("importPersistentActionBar"),
           QStringLiteral("importFolderNavigation"),
           QStringLiteral("importCurrentFolderLabel"), QStringLiteral("importVisibleFileCount"),
           QStringLiteral("importOpenPreviewButton"), QStringLiteral("importMediaGrid")}))
        return fail("Long folder names must remain readable in the actual Import workspace at every scale");
      auto* folder_label = find_named_item(import_root, QStringLiteral("importCurrentFolderLabel"));
      if (!folder_label || folder_label->property("truncated").toBool() ||
          folder_label->property("fontSizeMode").toInt() != 0 ||
          folder_label->property("contentWidth").toDouble() > folder_label->width() + 1 ||
          folder_label->property("contentHeight").toDouble() > folder_label->height() + 1 ||
          !inside_window(folder_label))
        return fail("Import folder names must wrap fully without shrinking, clipping, or leaving the window");
      if (scale == 2.0) {
        auto* workspace_panels = find_named_item(import_root, QStringLiteral("importWorkspacePanels"));
        auto* source_scroll = find_named_item(import_root, QStringLiteral("importSourceScrollArea"));
        auto* source_panel = find_named_item(import_root, QStringLiteral("importSourcePanel"));
        auto* main_panel = find_named_item(import_root, QStringLiteral("importMainPanel"));
        auto* persistent_footer = find_named_item(import_root, QStringLiteral("importPersistentActionBar"));
        auto* destination_name = find_named_item(import_root, QStringLiteral("importDestinationNameLabel"));
        auto* destination_change = find_named_item(import_root, QStringLiteral("importDestinationChangeButton"));
        auto* persistent_import = find_named_item(import_root, QStringLiteral("importPersistentPrimaryButton"));
        auto* media_grid = find_named_item(import_root, QStringLiteral("importMediaGrid"));
        if (!workspace_panels || !source_scroll || !source_panel || !main_panel || !persistent_footer ||
            !destination_name || !destination_change || !persistent_import || !media_grid) {
          return fail("At 200% Import should expose every panel, footer action, and media-grid surface");
        }
        const auto panels_bounds = scene_rect(workspace_panels);
        const auto footer_bounds = scene_rect(persistent_footer);
        const auto source_scroll_bounds = scene_rect(source_scroll);
        const auto folder_label_bounds = scene_rect(folder_label);
        if (!inside_window(workspace_panels) || !inside_window(source_panel) ||
            !inside_window(source_scroll) || !inside_window(main_panel) ||
            !inside_window(persistent_footer) || !inside_window(media_grid) ||
            !item_visible(destination_name) ||
            !item_visible(destination_change) || !item_visible(persistent_import) ||
            media_grid->height() < 120.0 ||
            panels_bounds.bottom() > footer_bounds.top() + 2.0 ||
            overlaps_with_tolerance(panels_bounds, footer_bounds) ||
            !contains_with_tolerance(panels_bounds, scene_rect(source_panel)) ||
            !contains_with_tolerance(panels_bounds, scene_rect(main_panel)) ||
            overlaps_with_tolerance(scene_rect(media_grid), footer_bounds) ||
            overlaps_with_tolerance(folder_label_bounds, footer_bounds) ||
            !contains_with_tolerance(scene_rect(source_panel), source_scroll_bounds) ||
            !contains_with_tolerance(source_scroll_bounds, folder_label_bounds) ||
            !contains_with_tolerance(footer_bounds, scene_rect(destination_name)) ||
            !contains_with_tolerance(footer_bounds, scene_rect(destination_change)) ||
            !contains_with_tolerance(footer_bounds, scene_rect(persistent_import))) {
          return fail("At 200% Import should reserve its destination footer, keep the full folder name in the scrollable source rail, and leave a usable photo grid viewport");
        }
        auto* details_button = find_named_item(import_root, QStringLiteral("importSourceDetailsButton"));
        auto* details_dialog = find_named_object(import_root, QStringLiteral("importSourceDetailsDialog"));
        if (!details_button || !details_dialog || !send_activation_key(details_button) ||
            !wait_for_condition([&]() { return details_dialog->property("opened").toBool(); }, 2000) ||
            !capture_import_state(QStringLiteral("import-source-full-path-200-percent"), 1280, 800,
              {QStringLiteral("importSourceFullPath"), QStringLiteral("importCopyFolderDetailsPathButton")}))
          return fail("Compact Import must expose the complete source path through keyboard-accessible Folder details");
        auto* full_path = find_named_item(import_root, QStringLiteral("importSourceFullPath"));
        if (!full_path || full_path->property("text").toString() != workspace->property("currentFolderPath").toString())
          return fail("Folder details must preserve the exact source path");
        auto saved_details_clipboard = std::make_unique<QMimeData>();
        if (const auto* original = QGuiApplication::clipboard()->mimeData())
          for (const auto& format : original->formats()) saved_details_clipboard->setData(format, original->data(format));
        const auto copied_full_path = send_activation_key(find_named_object(import_root, QStringLiteral("importCopyFolderDetailsPathButton"))) &&
            QGuiApplication::clipboard()->text() == workspace->property("currentFolderPath").toString();
        QGuiApplication::clipboard()->setMimeData(saved_details_clipboard.release());
        if (!copied_full_path)
          return fail("The full source path must remain copyable at maximum scale");
        if (!send_window_key(redesign_window, Qt::Key_Escape) ||
            !wait_for_condition([&]() { return !details_dialog->property("visible").toBool(); }, 1000) ||
            !details_button->hasActiveFocus())
          return fail("Closing Folder details must restore focus to its opener");
      }
    }
    app_shell.setInterfaceScale(original_import_scale);
    QList<QUrl> photo_urls;
    for (const auto& path : fixture_paths) photo_urls.push_back(QUrl::fromLocalFile(path.toString()));
    if (!drop_urls(photo_urls, true) ||
        workspace->property("selectedFileCount").toInt() != fixture_count ||
        !workspace->property("sourceRootIsFileSelection").toBool() ||
        workspace->property("importProjectChooserOpen").toBool()) {
      return fail("Dropping photos should stage the exact selected batch and require review before import");
    }
    if (drop_urls({QUrl(QStringLiteral("https://example.test/photo.jpg"))}) ||
        workspace->property("selectedFileCount").toInt() != fixture_count ||
        !workspace->property("dropMessageIsError").toBool()) {
      return fail("Remote URL drops should be rejected without changing the selected batch");
    }
    QVariant staged_files;
    if (!QMetaObject::invokeMethod(
            workspace,
            "stagePickedFiles",
            Q_RETURN_ARG(QVariant, staged_files),
            Q_ARG(QVariant, QVariant(fixture_paths))) ||
        !staged_files.toBool() ||
        !wait_for_condition([&]() {
          return workspace->property("sourceRootIsFileSelection").toBool() &&
              workspace->property("selectedFileCount").toInt() == fixture_count &&
              workspace->property("filteredFileCount").toInt() == fixture_count &&
              item_visible(find_named_item(import_root, QStringLiteral("importMediaCard_0"))) &&
              item_visible(find_named_item(import_root, QStringLiteral("importMediaCard_1"))) &&
              item_visible(find_named_item(import_root, QStringLiteral("importMediaCard_2")));
        }, 5000)) {
      return fail("Import redesign proof should stage and render the repository photo fixtures through ImportView");
    }
    const auto expected_fixture_source = QDir::cleanPath(fixture_sources);
    if (!wait_for_condition([&]() {
          return QDir::cleanPath(workspace->property("sourceRootPath").toString()) ==
                     expected_fixture_source &&
              QDir::cleanPath(workspace->property("currentFolderPath").toString()) ==
                     expected_fixture_source &&
              workspace->property("pendingFolderPath").toString().isEmpty() &&
              workspace->property("pendingFolderUrl").toString().isEmpty() &&
              workspace->property("currentFolderUrl").toString() ==
                  QUrl::fromLocalFile(expected_fixture_source).toString();
        }, 5000)) {
      return fail("Import staged-photo captures should wait until the actual selected source folder finishes opening");
    }
    if (!wait_for_condition(preview_tiles_ready, 15000)) {
      return fail("Import staged-photo captures should wait until all three real fixture photos decode in the media tiles");
    }
    auto* media_grid = find_named_item(import_root, QStringLiteral("importMediaGrid"));
    auto* media_card_0 = find_named_item(import_root, QStringLiteral("importMediaCard_0"));
    auto* media_card_1 = find_named_item(import_root, QStringLiteral("importMediaCard_1"));
    auto* media_card_2 = find_named_item(import_root, QStringLiteral("importMediaCard_2"));
    primary_action = find_named_object(import_root, QStringLiteral("importPersistentPrimaryButton"));
    if (!item_visible(media_grid) || !item_visible(media_card_0) || !item_visible(media_card_1) ||
        !item_visible(media_card_2) || !workspace->property("canImportSelection").toBool() ||
        !primary_action || !primary_action->property("enabled").toBool() ||
        primary_action->property("text").toString() != QStringLiteral("Import selected photos · 3") ||
        media_card_0->property("delegateFilePath").toString() != fixture_paths[0].toString() ||
        media_card_1->property("delegateFilePath").toString() != fixture_paths[1].toString() ||
        media_card_2->property("delegateFilePath").toString() != fixture_paths[2].toString()) {
      return fail("Import media review should preserve fixture identity and enable the primary action for a real selection");
    }
    if (!capture_import_state(
            QStringLiteral("import-selected-photos"), 1440, 900,
            {QStringLiteral("importWorkspace"), QStringLiteral("importMainPanel"),
             QStringLiteral("importMediaGrid"), QStringLiteral("importMediaCard_0"),
             QStringLiteral("importPersistentPrimaryButton")}, true) ||
        !inside_window(qobject_cast<QQuickItem*>(primary_action)) ||
        !visible_named_items_do_not_overlap(
            import_root,
            {QStringLiteral("importSourcePanel"), QStringLiteral("importMainPanel"),
             QStringLiteral("importDestinationPanel")}) ||
        !visible_named_items_do_not_overlap(
            import_root,
            {QStringLiteral("importHeaderBar"), QStringLiteral("importProgressSummaryCard"),
             QStringLiteral("importMediaFilterBar"), QStringLiteral("importMediaGrid")})) {
      return fail("Import source, media review, and primary action should fit without panel overlap");
    }
    if (!capture_import_state(
            QStringLiteral("import-selected-photos-standard"), 1280, 760,
            {QStringLiteral("importWorkspace"), QStringLiteral("importMainPanel"),
             QStringLiteral("importMediaGrid"), QStringLiteral("importMediaCard_0"),
             QStringLiteral("importPersistentPrimaryButton")}, true) ||
        !inside_window(qobject_cast<QQuickItem*>(primary_action)) ||
        !visible_named_items_do_not_overlap(
            import_root,
            {QStringLiteral("importSourcePanel"), QStringLiteral("importMainPanel"),
             QStringLiteral("importDestinationPanel")}) ||
        !visible_named_items_do_not_overlap(
            import_root,
            {QStringLiteral("importHeaderBar"), QStringLiteral("importProgressSummaryCard"),
             QStringLiteral("importMediaFilterBar"), QStringLiteral("importMediaGrid")})) {
      return fail("Import source, media review, and primary action should fit without panel overlap at standard size");
    }

    workspace->setProperty("filenameQuery", QStringLiteral("night"));
    if (!wait_for_condition([&]() { return workspace->property("filteredFileCount").toInt() == 1; }) ||
        workspace->property("selectedFileCount").toInt() != fixture_count ||
        primary_action->property("text").toString() != QStringLiteral("Import selected photos · 3")) {
      return fail("Filename search should filter visible entries without narrowing the selected import batch");
    }
    workspace->setProperty("filenameQuery", QString{});
    if (!wait_for_condition([&]() { return workspace->property("filteredFileCount").toInt() == fixture_count; }))
      return fail("Clearing filename search should restore the full media list");
    auto* size_slider = find_named_item(import_root, QStringLiteral("importThumbnailSizeSlider"));
    const auto previous_size = workspace->property("thumbnailSize").toDouble();
    if (!send_activation_key(size_slider, Qt::Key_Right) ||
        workspace->property("thumbnailSize").toDouble() <= previous_size ||
        workspace->property("selectedFileCount").toInt() != fixture_count) {
      return fail("Thumbnail density should respond to the keyboard without changing selection");
    }
    workspace->setProperty("thumbnailSize", previous_size);
    process_layout_events(5);
    QMetaObject::invokeMethod(workspace, "clearSelection");
    process_layout_events(5);
    media_card_0 = find_named_item(import_root, QStringLiteral("importMediaCard_0"));
    media_card_2 = find_named_item(import_root, QStringLiteral("importMediaCard_2"));
    if (!send_activation_key(media_card_0, Qt::Key_Return))
      return fail("Import keyboard selection should establish a range anchor");
    media_card_2->forceActiveFocus(Qt::TabFocusReason);
    if (!send_window_key(redesign_window, Qt::Key_Return, Qt::ShiftModifier) ||
        workspace->property("selectedFileCount").toInt() != fixture_count) {
      return fail("Shift selection should include every visible item between the anchor and current photo");
    }
    media_card_1 = find_named_item(import_root, QStringLiteral("importMediaCard_1"));
    if (!send_activation_key(media_card_1) ||
        !wait_for_condition([&]() {
          auto* dialog = find_named_object(import_root, QStringLiteral("importMediaPreviewDialog"));
          auto* image = find_named_object(import_root, QStringLiteral("importLargePreviewImage"));
          return dialog && dialog->property("opened").toBool() && image && image->property("status").toInt() == 1;
        }, 15000) || workspace->property("selectedFileCount").toInt() != fixture_count ||
        workspace->property("previewCurrentPath").toString() != fixture_paths[1].toString() ||
        !capture_import_state(QStringLiteral("import-large-photo-preview"), 1280, 760,
                              {QStringLiteral("importWorkspace"), QStringLiteral("importLargePreviewImage"),
                               QStringLiteral("importPreviewCloseButton")}) ||
        !capture_import_state(QStringLiteral("import-large-photo-preview-compact"), 900, 700,
                              {QStringLiteral("importWorkspace"), QStringLiteral("importLargePreviewImage"),
                               QStringLiteral("importPreviewCloseButton")}) ||
        !inside_window(find_named_item(import_root, QStringLiteral("importPreviewCloseButton"))) ||
        !inside_window(find_named_item(import_root, QStringLiteral("importPreviewNextButton")))) {
      return fail("Space should open a decoded large preview without changing the selected batch");
    }
    if (!send_activation_key(find_named_item(import_root, QStringLiteral("importPreviewNextButton")), Qt::Key_Return) ||
        workspace->property("previewCurrentPath").toString() != fixture_paths[2].toString()) {
      return fail("Preview navigation should follow the visible review order");
    }
    if (!send_activation_key(find_named_item(import_root, QStringLiteral("importPreviewSelectionButton")), Qt::Key_Return) ||
        workspace->property("selectedFileCount").toInt() != 2 ||
        !send_activation_key(find_named_item(import_root, QStringLiteral("importPreviewSelectionButton")), Qt::Key_Return) ||
        workspace->property("selectedFileCount").toInt() != fixture_count ||
        !send_activation_key(find_named_item(import_root, QStringLiteral("importPreviewCloseButton")), Qt::Key_Return)) {
      return fail("Large preview should support deliberate selection changes and return to the grid");
    }
    if (!wait_for_condition([&] {
          auto* preview_dialog = find_named_object(import_root, QStringLiteral("importMediaPreviewDialog"));
          return preview_dialog && !preview_dialog->property("visible").toBool();
        }, 5000))
      return fail("The large preview should finish closing before returning keyboard focus to Import");
    media_card_0 = find_named_item(import_root, QStringLiteral("importMediaCard_0"));
    if (!send_activation_key(media_card_0, Qt::Key_Return) ||
        !wait_for_condition([&]() { return workspace->property("selectedFileCount").toInt() == 2; })) {
      return fail("Import media selection should toggle through the card's native keyboard activation while preserving the other picks");
    }
    const auto selected_after_keyboard = workspace->property("selectedFilePaths").toList();
    if (selected_after_keyboard.size() != 2 ||
        !selected_after_keyboard.contains(QVariant(fixture_paths[1].toString())) ||
        !selected_after_keyboard.contains(QVariant(fixture_paths[2].toString()))) {
      return fail("Import media selection should toggle through the card's native keyboard activation while preserving the other picks");
    }

    workspace->setProperty("workflowProfile", QStringLiteral("studio"));
    workspace->setProperty("importMode", QStringLiteral("managed"));
    const auto selection_action_text = primary_action->property("text").toString();
    const auto selection_action_bounds = scene_rect(qobject_cast<QQuickItem*>(primary_action));
    if (selection_action_text != QStringLiteral("Import selected photos · 2") ||
        !send_activation_key(qobject_cast<QQuickItem*>(primary_action)) ||
        !catalog_assets.importPreflightBusy() || workspace->property("canConfirmImport").toBool()) {
      return fail("Managed preflight should start asynchronously and disable confirmation while scanning");
    }
    if (primary_action->property("enabled").toBool() ||
        primary_action->property("disabledReason").toString().isEmpty()) {
      return fail("The persistent Import action should explain its disabled preflight state");
    }
    QElapsedTimer managed_timer;
    managed_timer.start();
    while (catalog_assets.importPreflightBusy() && managed_timer.elapsed() < 5000)
      process_layout_events(1);
    process_layout_events(8);
    auto* destination_panel = find_named_item(import_root, QStringLiteral("importDestinationPanel"));
    auto* preflight_facts = find_named_item(import_root, QStringLiteral("importPreflightFacts"));
    auto* confirm = find_named_item(import_root, QStringLiteral("importPersistentPrimaryButton"));
    const auto managed_report = workspace->property("importPreflight").toMap();
    const auto confirm_bounds = confirm ? scene_rect(confirm) : QRectF{};
    const bool primary_action_kept_footer_position = confirm &&
        std::abs(confirm_bounds.right() - selection_action_bounds.right()) <= 2.0 &&
        std::abs(confirm_bounds.y() - selection_action_bounds.y()) <= 2.0 &&
        std::abs(confirm_bounds.height() - selection_action_bounds.height()) <= 2.0;
    if (catalog_assets.importPreflightBusy() || !workspace->property("canConfirmImport").toBool() ||
        managed_report.value(QStringLiteral("importMode")).toString() != QStringLiteral("managed") ||
        managed_report.value(QStringLiteral("importableCount")).toInt() != 2 ||
        workspace->property("selectedFileCount").toInt() != 2 ||
        workspace->property("pendingImportPaths").toList().size() != 2 ||
        !item_visible(destination_panel) || !item_visible(preflight_facts) || !item_visible(confirm) ||
        !confirm->property("enabled").toBool() ||
        confirm->property("text").toString() != QStringLiteral("Import selected photos · 2") ||
        !primary_action_kept_footer_position ||
        item_visible(find_named_item(import_root, QStringLiteral("importSourcePanel"))) ||
        item_visible(find_named_item(import_root, QStringLiteral("importMainPanel")))) {
      std::cerr << QJsonDocument(QJsonObject{
          {"busy", catalog_assets.importPreflightBusy()},
          {"canConfirm", workspace->property("canConfirmImport").toBool()},
          {"report", QJsonObject::fromVariantMap(managed_report)},
          {"selectedCount", workspace->property("selectedFileCount").toInt()},
          {"pendingCount", workspace->property("pendingImportPaths").toList().size()},
          {"destinationVisible", item_visible(destination_panel)},
          {"factsVisible", item_visible(preflight_facts)},
          {"confirmVisible", item_visible(confirm)},
          {"confirmEnabled", confirm && confirm->property("enabled").toBool()},
          {"confirmText", confirm ? confirm->property("text").toString() : QString{}},
          {"stableFooter", primary_action_kept_footer_position},
          {"beforeBounds", bounds_json(selection_action_bounds)},
          {"afterBounds", bounds_json(confirm_bounds)},
          {"sourceVisible", item_visible(find_named_item(import_root, QStringLiteral("importSourcePanel")))},
          {"mainVisible", item_visible(find_named_item(import_root, QStringLiteral("importMainPanel")))}}
          ).toJson().constData();
      redesign_window->grabWindow().save(QDir(redesign_proof_dir).filePath(QStringLiteral("managed-preflight-failure.png")));
      return fail("Managed preflight should show the current report, preserve selection, and enable its explicit confirm action");
    }
    if (!capture_import_state(
            QStringLiteral("import-managed-preflight"), 1440, 900,
            {QStringLiteral("importWorkspace"), QStringLiteral("importDestinationPanel"),
             QStringLiteral("importPreflightFacts"), QStringLiteral("importPersistentPrimaryButton")}) ||
        !inside_window(confirm) ||
        overlaps_with_tolerance(scene_rect(preflight_facts), scene_rect(confirm))) {
      return fail("Managed preflight facts and confirm action should fit the window without overlap");
    }
    if (!capture_import_state(
            QStringLiteral("import-managed-preflight-standard"), 1280, 760,
            {QStringLiteral("importWorkspace"), QStringLiteral("importDestinationPanel"),
             QStringLiteral("importPreflightFacts"), QStringLiteral("importPersistentPrimaryButton")}) ||
        !inside_window(confirm) || overlaps_with_tolerance(scene_rect(preflight_facts), scene_rect(confirm))) {
      return fail("Managed preflight facts and confirmation should fit without overlap at standard size");
    }

    const bool review_starts_compact = !item_visible(find_named_item(import_root, QStringLiteral("importReviewBatchStrip")));
    const bool review_toggle_activated = send_activation_key(find_named_item(import_root, QStringLiteral("importReviewSelectionToggle")));
    const bool review_photos_visible = wait_for_condition([&] {
      return item_visible(find_named_item(import_root, QStringLiteral("importReviewBatchStrip")));
    });
    const bool review_paths_activated = send_activation_key(find_named_item(import_root, QStringLiteral("importReviewPathsButton")));
    if (!review_starts_compact || !review_toggle_activated || !review_photos_visible ||
        confirm->property("text").toString() != QStringLiteral("Import selected photos · 2") ||
        !review_paths_activated || !workspace->property("reviewPathsExpanded").toBool()) {
      std::cerr << "review compact=" << review_starts_compact << " toggle=" << review_toggle_activated
                << " photos=" << review_photos_visible << " paths=" << review_paths_activated
                << " expanded=" << workspace->property("reviewPathsExpanded").toBool() << '\n';
      redesign_window->grabWindow().save(QDir(redesign_proof_dir).filePath(QStringLiteral("review-disclosure-failure.png")));
      return fail("Import review should retain the selected batch, name the confirmed action, and expose full paths on request");
    }
    auto saved_clipboard = std::make_unique<QMimeData>();
    if (const auto* original = QGuiApplication::clipboard()->mimeData()) {
      for (const auto& format : original->formats()) saved_clipboard->setData(format, original->data(format));
    }
    const bool copied_exact_source =
        send_activation_key(find_named_item(import_root, QStringLiteral("importCopySourcePathButton"))) &&
        QGuiApplication::clipboard()->text() == fixture_sources;
    QGuiApplication::clipboard()->setMimeData(saved_clipboard.release());
    if (!copied_exact_source) {
      return fail("Review path copy should put the exact source path on the native clipboard");
    }
    workspace->setProperty("reviewPathsExpanded", false);
    workspace->setProperty("importMode", QStringLiteral("referenced"));
    if (!catalog_assets.importPreflightBusy() || workspace->property("canConfirmImport").toBool()) {
      return fail("Switching to referenced mode should invalidate the old report until its new scan finishes");
    }
    QElapsedTimer referenced_timer;
    referenced_timer.start();
    while (catalog_assets.importPreflightBusy() && referenced_timer.elapsed() < 5000)
      process_layout_events(1);
    process_layout_events(8);
    const auto referenced_report = workspace->property("importPreflight").toMap();
    if (catalog_assets.importPreflightBusy() || !workspace->property("canConfirmImport").toBool() ||
        referenced_report.value(QStringLiteral("importMode")).toString() != QStringLiteral("referenced") ||
        referenced_report.value(QStringLiteral("importableCount")).toInt() != 2 ||
        referenced_report.value(QStringLiteral("estimatedDiskBytes")).toLongLong() != 0 ||
        workspace->property("selectedFileCount").toInt() != 2 ||
        !confirm->property("enabled").toBool() ||
        item_visible(find_named_item(import_root, QStringLiteral("importSourcePanel"))) ||
        item_visible(find_named_item(import_root, QStringLiteral("importMainPanel")))) {
      return fail("Referenced preflight should match its leave-in-place policy while preserving the staged selection");
    }
    if (!capture_import_state(
            QStringLiteral("import-referenced-preflight"), 1440, 900,
            {QStringLiteral("importWorkspace"), QStringLiteral("importDestinationPanel"),
             QStringLiteral("importPreflightFacts"), QStringLiteral("importPersistentPrimaryButton")}) ||
        !inside_window(confirm) || overlaps_with_tolerance(scene_rect(preflight_facts), scene_rect(confirm)) ||
        !capture_import_state(
            QStringLiteral("import-referenced-preflight-standard"), 1280, 760,
            {QStringLiteral("importWorkspace"), QStringLiteral("importDestinationPanel"),
             QStringLiteral("importPreflightFacts"), QStringLiteral("importPersistentPrimaryButton")}) ||
        !inside_window(confirm) || overlaps_with_tolerance(scene_rect(preflight_facts), scene_rect(confirm))) {
      return fail("Referenced preflight confirmation should remain visible and within the window at standard size");
    }

    // The compact capture is supplementary because desktop size can change the
    // available Qt Quick area, but if the page can render all primary actions it
    // must keep the confirm action inside the window.
    if (capture_import_state(
            QStringLiteral("import-referenced-preflight-compact"), 900, 700,
            {QStringLiteral("importWorkspace"), QStringLiteral("importDestinationPanel"),
             QStringLiteral("importPersistentPrimaryButton")})) {
      confirm = find_named_item(import_root, QStringLiteral("importPersistentPrimaryButton"));
      if (!inside_window(confirm)) {
        return fail("Compact Import review should keep its primary confirmation within the window");
      }
    }

    if (!settle_import_layout(1280, 760))
      return fail("Import controls should remain in the stable native shell");
    auto* handling_action = find_named_item(import_root, QStringLiteral("importStudioWorkflowButton"));
    if (!inside_window(handling_action) || !send_activation_key(handling_action) ||
        !workspace->property("advancedImportOptionsOpen").toBool() ||
        workspace->property("workflowProfile").toString() != QStringLiteral("studio")) {
      return fail("File handling disclosure should expose custom policies through keyboard activation");
    }
    auto* defaults_action = find_named_item(import_root, QStringLiteral("importPersonalWorkflowButton"));
    if (!send_activation_key(defaults_action) ||
        !wait_for_condition([&]() {
          return !catalog_assets.importPreflightBusy() && workspace->property("canConfirmImport").toBool();
        }) || workspace->property("importMode").toString() != QStringLiteral("managed") ||
        workspace->property("advancedImportOptionsOpen").toBool() ||
        workspace->property("duplicatePolicy").toString() != QStringLiteral("skip") ||
        workspace->property("rawJpegPolicy").toString() != QStringLiteral("both")) {
      return fail("Use defaults should restore safe managed-copy policies and refresh preflight");
    }
    auto* referenced_action = find_named_item(import_root, QStringLiteral("importReferencedModeButton"));
    if (!send_activation_key(referenced_action) ||
        !wait_for_condition([&]() {
          return !catalog_assets.importPreflightBusy() && workspace->property("canConfirmImport").toBool();
        }) || workspace->property("importMode").toString() != QStringLiteral("referenced") ||
        workspace->property("workflowProfile").toString() != QStringLiteral("studio")) {
      return fail("Leave in Place should be directly available from the default setup");
    }
    auto* new_project_action = find_named_item(import_root, QStringLiteral("importNewProjectButton"));
    if (!send_activation_key(new_project_action) ||
        !workspace->property("newProjectEntryOpen").toBool() ||
        !capture_import_state(QStringLiteral("import-project-entry"), 1280, 760,
                              {QStringLiteral("importWorkspace"), QStringLiteral("importNewProjectField"),
                               QStringLiteral("importPersistentPrimaryButton")})) {
      return fail("New project entry should remain accessible on request without starting an import");
    }
    workspace->setProperty("newProjectEntryOpen", false);

#include "import_detail_proof.inc"

    if (!import_warnings->isEmpty()) {
      std::cerr << import_warnings->join(QLatin1Char('\n')).toStdString() << '\n';
      return fail("Import redesign proof should render without ImportView QML warnings");
    }
    QFile redesign_manifest_file(
        QDir(redesign_proof_dir).filePath(QStringLiteral("import-redesign-manifest.json")));
    if (!redesign_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        redesign_manifest_file.write(
            QJsonDocument(redesign_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Import redesign proof should write its capture manifest");
    }
    workspace->setProperty("importProjectChooserOpen", false);
    catalog_assets.cancelImportPreflight();
    process_layout_events(3);
    if (catalog_assets.importPreflightBusy() || !catalog_assets.importPreflightResult().isEmpty()) {
      return fail("Closing the optional Import redesign review should discard only its unconfirmed report");
    }
    std::cout << "Import redesign proof: no source, decoded staged stock photos, keyboard selection, managed and referenced preflight, stable shell geometry; "
              << redesign_manifest.size() << " captures at " << redesign_proof_dir.toStdString() << '\n';

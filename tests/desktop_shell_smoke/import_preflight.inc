  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_IMPORT_PREFLIGHT")) {
    app_shell.completeFirstRunExperience();
    catalog_assets.setInteractiveRenderingActive(true);
    const auto source = runtime_root.filePath(QStringLiteral("preflight-source"));
    if (!QDir().mkpath(source)) return fail("Import fixture directory unavailable");
    const auto photo = QDir(source).filePath(QStringLiteral("photo.jpg"));
    if (!write_overlay_visual_fixture(photo, QStringLiteral("saturated-small")))
      return fail("Import fixture photo unavailable");
    const auto import_warnings = std::make_shared<QStringList>();
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [import_warnings](const QList<QQmlError>& errors) {
      for (const auto& error : errors) {
        const auto message = error.toString();
        if (message.contains(QStringLiteral("ImportView.qml"))) import_warnings->append(message);
      }
    });
    // This fixture intentionally uses a nonexistent database URL. Keep its
    // expected startup failure from reappearing during later resize/capture
    // event processing; the production recovery surface remains untouched.
    bool clearing_fixture_catalog_error = false;
    const auto clear_fixture_catalog_error = [&]() {
      if (clearing_fixture_catalog_error ||
          catalog_assets.status() != QStringLiteral("Catalog unavailable") ||
          catalog_assets.errorMessage().isEmpty()) {
        return;
      }
      clearing_fixture_catalog_error = true;
      catalog_assets.reportCatalogStartupFailure(QString{});
      clearing_fixture_catalog_error = false;
    };
    QObject fixture_catalog_error_scope;
    QObject::connect(
        &catalog_assets, &photo_editor::desktop::CatalogAssetModel::statusChanged,
        &fixture_catalog_error_scope, clear_fixture_catalog_error);
    QObject::connect(
        &catalog_assets, &photo_editor::desktop::CatalogAssetModel::errorMessageChanged,
        &fixture_catalog_error_scope, clear_fixture_catalog_error);
    engine.load(QUrl::fromLocalFile(main_qml_path));
    process_layout_events(8);
    if (engine.rootObjects().isEmpty()) return fail("Import proof could not load Main.qml");
    auto* import_root = engine.rootObjects().constFirst();
    const auto catalog_recovery_hidden_for_fixture = [&]() {
      clear_fixture_catalog_error();
      process_layout_events(2);
      return catalog_assets.errorMessage().isEmpty() &&
          !import_root->property("catalogRecoveryNeeded").toBool() &&
          !item_visible(find_named_item(import_root, QStringLiteral("recoverySummarySurface")));
    };
    if (!QMetaObject::invokeMethod(import_root, "beginImportWorkspace"))
      return fail("Import route unavailable");
    process_layout_events(8);
    auto* workspace = find_named_object(import_root, QStringLiteral("importWorkspace"));
    if (!workspace) return fail("Import workspace unavailable");

    const auto normalized_import_path_matches = [&](const QString& input,
                                                     const QString& expected) {
      QVariant normalized_path;
      return QMetaObject::invokeMethod(
                 workspace,
                 "normalizedImportPath",
                 Q_RETURN_ARG(QVariant, normalized_path),
                 Q_ARG(QVariant, QVariant(input))) &&
          normalized_path.toString() == expected;
    };
    const auto import_path_is_absolute = [&](const QString& input) {
      QVariant is_absolute;
      return QMetaObject::invokeMethod(
                 workspace,
                 "isAbsoluteImportPath",
                 Q_RETURN_ARG(QVariant, is_absolute),
                 Q_ARG(QVariant, QVariant(input))) &&
          is_absolute.toBool();
    };
    const auto unc_import_path = QStringLiteral("\\\\server\\share\\x.jpg");
    if (!normalized_import_path_matches(
            unc_import_path, QStringLiteral("//server/share/x.jpg")) ||
        !import_path_is_absolute(unc_import_path) ||
        !normalized_import_path_matches(
            QStringLiteral("C:\\Users\\test\\x.jpg"),
            QStringLiteral("C:/Users/test/x.jpg")) ||
        !import_path_is_absolute(QStringLiteral("C:\\Users\\test\\x.jpg")) ||
        !normalized_import_path_matches(
            QStringLiteral("/home/test/x.jpg"),
            QStringLiteral("/home/test/x.jpg")) ||
        !import_path_is_absolute(QStringLiteral("/home/test/x.jpg"))) {
      return fail("Import path normalization should preserve UNC, drive-letter, and POSIX roots");
    }

    const auto multi_folder_root = runtime_root.filePath(QStringLiteral("Multi Folder Picker"));
    const auto first_folder = QDir(multi_folder_root).filePath(QStringLiteral("Camera A/Day 1"));
    const auto second_folder = QDir(multi_folder_root).filePath(QStringLiteral("Camera B/Day 2"));
    if (!QDir().mkpath(first_folder) || !QDir().mkpath(second_folder))
      return fail("Multi-folder import picker fixture unavailable");
    const auto first_pick = QDir(first_folder).filePath(QStringLiteral("first.jpg"));
    const auto second_pick = QDir(second_folder).filePath(QStringLiteral("second.jpg"));
    if (!write_overlay_visual_fixture(first_pick, QStringLiteral("saturated-small")) ||
        !write_overlay_visual_fixture(second_pick, QStringLiteral("saturated-small")))
      return fail("Multi-folder import picker files unavailable");
    const QVariantList picked_paths{first_pick, second_pick, first_pick};
    QVariant common_directory;
    QVariant staged_files;
    if (!QMetaObject::invokeMethod(
            workspace,
            "commonDirectoryForFiles",
            Q_RETURN_ARG(QVariant, common_directory),
            Q_ARG(QVariant, QVariant(picked_paths))) ||
        common_directory.toString() != multi_folder_root ||
        !QMetaObject::invokeMethod(
            workspace,
            "stagePickedFiles",
            Q_RETURN_ARG(QVariant, staged_files),
            Q_ARG(QVariant, QVariant(picked_paths))) ||
        !staged_files.toBool() ||
        workspace->property("sourceRootPath").toString() != multi_folder_root ||
        workspace->property("selectedFileCount").toInt() != 2 ||
        workspace->property("selectedFolderCount").toInt() != 2 ||
        !workspace->property("sourceRootIsFileSelection").toBool())
      return fail("Import file picker did not preserve picks across their common folder root");
    QElapsedTimer folder_open_timer;
    folder_open_timer.start();
    while (!workspace->property("pendingFolderPath").toString().isEmpty() &&
           folder_open_timer.elapsed() < 1000)
      process_layout_events(1);
    if (workspace->property("currentFolderPath").toString() != multi_folder_root ||
        workspace->property("canImportFolder").toBool())
      return fail("File-picker selection exposed the common root as an unintended whole-folder import");
    auto* staged_first_preview = find_named_item(import_root, QStringLiteral("importMediaCard_0"));
    auto* staged_second_preview = find_named_item(import_root, QStringLiteral("importMediaCard_1"));
    auto* staged_empty_state = find_named_item(import_root, QStringLiteral("importEmptyState"));
    QVariant source_confirmation_value;
    if (!QMetaObject::invokeMethod(
            workspace, "sourceConfirmationText", Q_RETURN_ARG(QVariant, source_confirmation_value)))
      return fail("File picker selection should report its own source-ready state");
    if (!wait_for_condition([&]() {
          staged_first_preview = find_named_item(import_root, QStringLiteral("importMediaCard_0"));
          staged_second_preview = find_named_item(import_root, QStringLiteral("importMediaCard_1"));
          return workspace->property("filteredFileCount").toInt() == 2 &&
                 item_visible(staged_first_preview) && item_visible(staged_second_preview);
        }, 3000) || workspace->property("mediaQuickFilter").toString() != QStringLiteral("all") ||
        source_confirmation_value.toString() != QStringLiteral("Files Ready") ||
        workspace->property("selectedFileCount").toInt() != 2 ||
        staged_first_preview->property("delegateFilePath").toString() != first_pick ||
        staged_second_preview->property("delegateFilePath").toString() != second_pick ||
        staged_first_preview->property("delegateFolderLabel").toString() != QStringLiteral("Camera A/Day 1") ||
        staged_second_preview->property("delegateFolderLabel").toString() != QStringLiteral("Camera B/Day 2") ||
        !staged_first_preview->property("selected").toBool() ||
        !staged_second_preview->property("selected").toBool() || item_visible(staged_empty_state))
      return fail("File picker should preview both staged files from separate folders instead of showing No media");
    if (workspace->metaObject()->indexOfSignal("reviewImportedRequested()") < 0)
      return fail("Import completion has no Review imported navigation signal");

    workspace->setProperty("pendingImportRootPath", multi_folder_root);
    workspace->setProperty("pendingImportPaths", QVariant::fromValue(picked_paths));
    workspace->setProperty("importMode", QStringLiteral("referenced"));
    if (!QMetaObject::invokeMethod(workspace, "refreshImportPreflight") ||
        !catalog_assets.importPreflightBusy())
      return fail("Multi-folder import did not start a reviewed preflight");
    QElapsedTimer multi_folder_timer;
    multi_folder_timer.start();
    while (catalog_assets.importPreflightBusy() && multi_folder_timer.elapsed() < 5000)
      process_layout_events(1);
    if (catalog_assets.importPreflightBusy() ||
        catalog_assets.importPreflightResult().value(QStringLiteral("importableCount")).toInt() != 2)
      return fail("Multi-folder import preflight did not retain all unique selected files");
    workspace->setProperty("importProjectChooserOpen", false);

    workspace->setProperty("workflowProfile", QStringLiteral("studio"));
    workspace->setProperty("pendingImportRootPath", source);
    workspace->setProperty("pendingImportPaths", QStringList{photo});
    workspace->setProperty("importMode", QStringLiteral("referenced"));
    workspace->setProperty("importProjectChooserOpen", true);
    if (!QMetaObject::invokeMethod(workspace, "refreshImportPreflight") ||
        !catalog_assets.importPreflightBusy() || workspace->property("canConfirmImport").toBool())
      return fail("Import review did not disable confirmation during the asynchronous scan");
    QElapsedTimer timer;
    timer.start();
    while (catalog_assets.importPreflightBusy() && timer.elapsed() < 5000) process_layout_events(1);
    process_layout_events(8);
    const auto result = workspace->property("importPreflight").toMap();
    auto* confirm = find_named_object(import_root, QStringLiteral("importPreflightConfirmButton"));
    if (catalog_assets.importPreflightBusy() || result.value(QStringLiteral("importableCount")).toInt() != 1 ||
        !workspace->property("canConfirmImport").toBool() || !confirm || !confirm->property("enabled").toBool())
      return fail("Import UI did not publish the completed report and enable confirmation");
    workspace->setProperty("rawJpegPolicy", QStringLiteral("raw-only"));
    if (!catalog_assets.importPreflightBusy() || workspace->property("canConfirmImport").toBool())
      return fail("Import policy changes retained a stale confirmation state");
    timer.restart();
    while (catalog_assets.importPreflightBusy() && timer.elapsed() < 5000) process_layout_events(1);
    if (catalog_assets.importPreflightBusy() || catalog_assets.importPreflightScannedCount() != 0)
      return fail("Import policy UI did not reuse the completed scan");
    workspace->setProperty("importProjectChooserOpen", false);
    process_layout_events(2);
    if (catalog_assets.importPreflightBusy() || !catalog_assets.importPreflightResult().isEmpty())
      return fail("Closing import review did not discard its result");

    // Exercise the completed import receipt from the real job queue and rendered ImportView.
    // This is a synthetic persisted UI fixture; it does not claim an import backend run.
    const auto backup_destination = runtime_root.filePath(QStringLiteral("verified-originals-backup"));
    auto completed_import = make_persistent_job(
        QStringLiteral("import-ui-completed-fixture"),
        QStringLiteral("catalog_import"),
        QStringLiteral("Completed"));
    completed_import.display_label = QStringLiteral("Multi-folder selection");
    completed_import.source_path = multi_folder_root;
    completed_import.destination_path = runtime_root.filePath(QStringLiteral("Photo Library"));
    completed_import.progress = 100;
    completed_import.metadata_json.insert(
        QStringLiteral("importResult"),
        QJsonObject{
            {QStringLiteral("importedCount"), 2},
            {QStringLiteral("resumedCount"), 1},
            {QStringLiteral("duplicateCount"), 1},
            {QStringLiteral("unsupportedCount"), 1},
            {QStringLiteral("failureCount"), 0},
            {QStringLiteral("backupEnabled"), true},
            {QStringLiteral("backedUpCount"), 2},
            {QStringLiteral("reusedBackupCount"), 1},
            {QStringLiteral("verificationAlgorithm"), QStringLiteral("SHA-256")},
            {QStringLiteral("backupDestination"), backup_destination},
        });
    if (!photo_editor::desktop::upsertPersistedJobRecord(completed_import, nullptr))
      return fail("Import receipt proof should persist its isolated completed-job UI fixture");
    job_queue.refresh();
    if (!wait_for_condition([&]() {
          const auto latest = job_queue.latestJobForKind(QStringLiteral("catalog_import"));
          return !job_queue.loading() &&
                 latest.value(QStringLiteral("jobId")).toString() == completed_import.job_id &&
                 latest.value(QStringLiteral("status")).toString() == QStringLiteral("Completed");
        }, 5000)) {
      return fail("ImportView should receive the completed import from the real job queue model");
    }
    // The fixture-only signal handler above keeps its deliberately fake
    // database error out of the receipt capture. It does not prove catalog
    // availability or a backend import, and it leaves the real recovery UI intact.
    catalog_assets.reportCatalogStartupFailure(QString{});
    process_layout_events(8);
    if (!catalog_recovery_hidden_for_fixture())
      return fail("Synthetic catalog recovery should be hidden for the isolated completed-import receipt capture");
    auto* receipt_card = find_named_item(import_root, QStringLiteral("importProgressSummaryCard"));
    auto* review_imported = find_named_item(import_root, QStringLiteral("importReviewImportedButton"));
    QVariant completion_summary_value;
    QVariant backup_receipt_value;
    if (!QMetaObject::invokeMethod(
            workspace, "importCompletionSummaryText", Q_RETURN_ARG(QVariant, completion_summary_value)) ||
        !QMetaObject::invokeMethod(
            workspace, "importBackupReceiptText", Q_RETURN_ARG(QVariant, backup_receipt_value))) {
      return fail("Import completion text should be exposed by the active ImportView state");
    }
    const auto completion_summary = completion_summary_value.toString();
    const auto backup_receipt = backup_receipt_value.toString();
    QQuickItem* rendered_completion_summary = nullptr;
    QQuickItem* rendered_backup_receipt = nullptr;
    QList<QObject*> summary_children;
    if (receipt_card) summary_children = receipt_card->findChildren<QObject*>();
    for (auto* child : summary_children) {
      auto* item = qobject_cast<QQuickItem*>(child);
      if (!item_visible(item)) continue;
      if (item->property("text").toString() == completion_summary) rendered_completion_summary = item;
      if (item->property("text").toString() == backup_receipt) rendered_backup_receipt = item;
    }
    const auto expected_completion = QStringLiteral(
        "Imported 2 · resumed 1 · duplicates 1 · unsupported 1 · unreadable/failures 0");
    const auto expected_backup_receipt = QStringLiteral(
        "Verified backup · 3 copies · SHA-256 · %1").arg(backup_destination);
    if (!item_visible(receipt_card) || !item_visible(review_imported) ||
        review_imported->property("text").toString() != QStringLiteral("Review imported") ||
        completion_summary != expected_completion || backup_receipt != expected_backup_receipt ||
        !item_visible(rendered_completion_summary) || !item_visible(rendered_backup_receipt) ||
        !contains_with_tolerance(scene_rect(receipt_card), scene_rect(rendered_completion_summary)) ||
        !contains_with_tolerance(scene_rect(receipt_card), scene_rect(rendered_backup_receipt))) {
      return fail("Completed import counts, verified-backup receipt, and Review imported action should render together");
    }

    const auto receipt_visual_proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
                                              ? qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR")
                                              : runtime_root.filePath(QStringLiteral("import-receipt-proof"));
    if (!QDir().mkpath(receipt_visual_proof_dir))
      return fail("Completed import receipt proof directory should be writable");
    QJsonArray receipt_visual_manifest;
    auto* receipt_window = qobject_cast<QQuickWindow*>(import_root);
    if (!receipt_window) return fail("Completed import receipt proof requires the native main window");
    const auto receipt_manifest_metadata = [&](bool recovery_hidden) {
      return QJsonObject{
          {QStringLiteral("jobId"), completed_import.job_id},
          {QStringLiteral("jobStatus"), completed_import.status},
          {QStringLiteral("syntheticUiFixture"), true},
          {QStringLiteral("backendImportExecuted"), false},
          {QStringLiteral("catalogRecoveryHiddenInUiFixture"), recovery_hidden},
          {QStringLiteral("completionSummary"), completion_summary},
          {QStringLiteral("backupReceipt"), backup_receipt},
      };
    };
    const auto capture_completed_receipt = [&](const QString& state, int width, int height) {
      if (!catalog_recovery_hidden_for_fixture() ||
          !capture_visual_proof(
              receipt_window, import_root, receipt_visual_proof_dir, state, width, height,
              {QStringLiteral("importWorkspace"), QStringLiteral("importProgressSummaryCard"),
               QStringLiteral("importReviewImportedButton")},
              &receipt_visual_manifest, receipt_manifest_metadata(false))) {
        return false;
      }
      const auto recovery_hidden = catalog_recovery_hidden_for_fixture();
      auto entry = receipt_visual_manifest.last().toObject();
      auto metadata = entry.value(QStringLiteral("metadata")).toObject();
      metadata.insert(QStringLiteral("catalogRecoveryHiddenInUiFixture"), recovery_hidden);
      entry.insert(QStringLiteral("metadata"), metadata);
      receipt_visual_manifest.replace(receipt_visual_manifest.size() - 1, entry);
      return recovery_hidden;
    };
    if (!capture_completed_receipt(
            QStringLiteral("import-completed-receipt-1440"), 1440, 900) ||
        !contains_with_tolerance(scene_rect(receipt_card), scene_rect(review_imported)) ||
        overlaps_with_tolerance(scene_rect(rendered_completion_summary), scene_rect(review_imported)) ||
        overlaps_with_tolerance(scene_rect(rendered_backup_receipt), scene_rect(review_imported))) {
      return fail("Completed import receipt and Review imported action should fit without overlap at desktop size");
    }
    if (!capture_completed_receipt(
            QStringLiteral("import-completed-receipt-900"), 900, 700) ||
        !contains_with_tolerance(scene_rect(receipt_card), scene_rect(review_imported)) ||
        overlaps_with_tolerance(scene_rect(rendered_completion_summary), scene_rect(review_imported)) ||
        overlaps_with_tolerance(scene_rect(rendered_backup_receipt), scene_rect(review_imported))) {
      return fail("Completed import receipt and review action should fit desktop and compact captures");
    }

    QFile receipt_manifest_file(
        QDir(receipt_visual_proof_dir).filePath(QStringLiteral("import-completed-receipt-manifest.json")));
    if (!receipt_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        receipt_manifest_file.write(QJsonDocument(receipt_visual_manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Completed import receipt proof should write its visual manifest");
    }
    if (!send_activation_key(review_imported) ||
        !wait_for_condition([&]() {
          return import_root->property("libraryWorkspaceMode").toString() == QStringLiteral("library") &&
                 item_visible(find_named_item(import_root, QStringLiteral("libraryImagesCollectionButton"))) &&
                 !import_root->property("workspaceTransitionActive").toBool();
        }, 10000) ||
        catalog_assets.importSessionFromFilter().isEmpty() ||
        catalog_assets.importSessionToFilter().isEmpty()) {
      return fail("Review imported should return to Images and scope the catalog to the completed import session");
    }
    if (!import_warnings->isEmpty()) {
      std::cerr << import_warnings->join(QLatin1Char('\n')).toStdString() << '\n';
      return fail("Import UI produced QML warnings");
    }
    return EXIT_SUCCESS;
  }

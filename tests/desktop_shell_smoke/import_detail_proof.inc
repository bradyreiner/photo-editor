    workspace->setProperty("importProjectChooserOpen", false);
    const auto pair_jpeg = QDir(fixture_sources).filePath(QStringLiteral("paired-capture.jpg"));
    const auto pair_raw = QDir(fixture_sources).filePath(QStringLiteral("paired-capture.dng"));
    const auto raw_payload = photo_editor::test_fixtures::synthetic_dng();
    QFile raw_file(pair_raw);
    if (!QFile::copy(fixture_paths[1].toString(), pair_jpeg) ||
        !raw_file.open(QIODevice::WriteOnly) ||
        raw_file.write(reinterpret_cast<const char*>(raw_payload.data()), static_cast<qint64>(raw_payload.size())) != static_cast<qint64>(raw_payload.size()))
      return fail("Paired import proof should create a real JPEG and valid synthetic DNG");
    raw_file.close();
    QVariant staged_pair;
    if (!QMetaObject::invokeMethod(workspace, "stagePickedFiles", Q_RETURN_ARG(QVariant, staged_pair),
            Q_ARG(QVariant, QVariant(QVariantList{pair_raw, pair_jpeg}))) || !staged_pair.toBool() ||
        !wait_for_condition([&]() { return workspace->property("filteredFileCount").toInt() == 1; }) ||
        workspace->property("selectedFileCount").toInt() != 2)
      return fail("RAW/JPEG grouping should show one capture while preserving both selected paths");
    auto* pair_card = find_named_item(import_root, QStringLiteral("importMediaCard_0"));
    if (!pair_card || pair_card->property("filePaths").toList().size() != 2 ||
        !send_activation_key(pair_card, Qt::Key_Return) || workspace->property("selectedFileCount").toInt() != 0 ||
        !send_activation_key(pair_card, Qt::Key_Return) || workspace->property("selectedFileCount").toInt() != 2 ||
        !QMetaObject::invokeMethod(workspace, "toggleSelection", Q_ARG(QVariant, QVariant(pair_raw))) ||
        !wait_for_condition([&]() { return pair_card->property("partiallySelected").toBool(); }) ||
        !send_activation_key(pair_card, Qt::Key_Return) || workspace->property("selectedFileCount").toInt() != 2)
      return fail("Grouped capture selection should toggle all members and expose partial selection truthfully");
    if (!capture_import_state(QStringLiteral("import-raw-jpeg-group"), 1280, 760,
            {QStringLiteral("importWorkspace"), QStringLiteral("importMediaCard_0"), QStringLiteral("importGroupPairsButton")}))
      return fail("Grouped RAW/JPEG capture should render in the real contact sheet");

    if (!send_activation_key(find_named_item(import_root, QStringLiteral("importFavoriteSourceButton"))) ||
        !workspace->property("savedFavoriteSources").toList().contains(fixture_sources) ||
        !workspace->property("savedRecentSources").toList().contains(fixture_sources))
      return fail("Source favorites and recents should retain the selected source");
    workspace->setProperty("sourceBrowserMode", QStringLiteral("favorites"));
    if (!wait_for_condition([&]() { return workspace->property("sourceBrowserEntries").toList().size() == 1; }))
      return fail("Favorites source filter should expose the saved source");
    workspace->setProperty("sourceBrowserMode", QStringLiteral("locations"));
    if (!send_activation_key(pair_card) || !wait_for_condition([&]() {
          const auto details = catalog_assets.importFileDetails();
          return details.value("path").toString() == pair_jpeg && !details.value("loading").toBool() &&
                 details.value("width").toInt() == 1627 && details.value("height").toInt() == 2090;
        }, 15000)) return fail("Preview metadata should asynchronously report actual JPEG dimensions");
    if (!send_activation_key(find_named_item(import_root, QStringLiteral("importPreviewActualSizeButton"))) ||
        !wait_for_condition([&]() { return workspace->property("previewZoom").toDouble() == 1.0; }))
      return fail("Actual-size preview should be directly keyboard accessible");
    auto* pan_surface = find_named_item(import_root, QStringLiteral("importPreviewPanSurface"));
    auto* zoom_image = find_named_item(import_root, QStringLiteral("importLargePreviewImage"));
    if (!pan_surface || !zoom_image ||
        !wait_for_condition([&]() {
          return std::abs(zoom_image->width() * redesign_window->devicePixelRatio() - 1627.0) < 1.0 &&
                 pan_surface->property("contentHeight").toDouble() > pan_surface->height();
        })) return fail("100 percent must map one source pixel to one display pixel and permit panning");
    process_layout_events(3);
    pan_surface->setProperty("contentY", 60.0);
    if (std::abs(pan_surface->property("contentY").toDouble() - 60.0) > 1.0)
      return fail("Actual-size inspection should allow moving the visible region");
    if (!capture_import_state(QStringLiteral("import-preview-100-percent"), 1280, 760,
            {QStringLiteral("importPreviewPanSurface"), QStringLiteral("importPreviewMetadata"), QStringLiteral("importPreviewCloseButton")}))
      return fail("Actual-size photo inspection should render with metadata and navigation controls");
    if (!send_activation_key(find_named_item(import_root, QStringLiteral("importPreviewFitButton"))) ||
        workspace->property("previewZoom").toDouble() != 0 ||
        !send_activation_key(find_named_item(import_root, QStringLiteral("importPreviewCloseButton")), Qt::Key_Return))
      return fail("Fit should restore the complete photograph and return to browsing");

    const auto video_fixture = QDir(fixture_sources).filePath(QStringLiteral("metadata-video.mp4"));
    const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty() || QProcess::execute(ffmpeg,
          {"-v", "error", "-f", "lavfi", "-i", "color=c=gray:s=96x64:d=1", "-c:v", "mpeg4",
           "-metadata", "creation_time=2024-01-02T03:04:05Z", "-y", video_fixture}) != 0)
      return fail("Video metadata proof should generate a real bounded media fixture");
    QVariant staged_video;
    if (!QMetaObject::invokeMethod(workspace, "stagePickedFiles", Q_RETURN_ARG(QVariant, staged_video),
            Q_ARG(QVariant, QVariant(QVariantList{video_fixture}))) || !staged_video.toBool() ||
        !QMetaObject::invokeMethod(workspace, "openPreview", Q_ARG(QVariant, QVariant(0))) ||
        !wait_for_condition([&]() {
          const auto details = catalog_assets.importFileDetails();
          return details.value("path").toString() == video_fixture && !details.value("loading").toBool() &&
                 details.value("width").toInt() == 96 && !details.value("durationText").toString().isEmpty() &&
                 details.value("captureTime").toString().startsWith(QStringLiteral("2024-01-02"));
        }, 15000)) return fail("Video metadata should use actual dimensions, duration and creation metadata");
    if (!send_activation_key(find_named_item(import_root, QStringLiteral("importPreviewCloseButton")), Qt::Key_Return))
      return fail("Video metadata preview should close normally");

    QVariant restored_files;
    if (!QMetaObject::invokeMethod(workspace, "stagePickedFiles", Q_RETURN_ARG(QVariant, restored_files),
            Q_ARG(QVariant, QVariant(fixture_paths))) || !restored_files.toBool()) return fail("Restore reviewed photo selection");
    workspace->setProperty("importMode", QStringLiteral("managed"));
    workspace->setProperty("organizationTemplate", QStringLiteral("source"));
    if (!QMetaObject::invokeMethod(workspace, "importSelected") ||
        !wait_for_condition([&]() { return !catalog_assets.importPreflightBusy(); })) return fail("Review restored batch");
    if (!send_activation_key(find_named_item(import_root, QStringLiteral("importDestinationChangeButton"))) ||
        !capture_import_state(QStringLiteral("import-storage-settings"), 1280, 760,
            {QStringLiteral("importStorageDialog"), QStringLiteral("importStorageDoneButton")}))
      return fail("Library and backup storage should be directly available from review");
    const auto existing_library = app_shell.photoLibraryLocation();
    if (!existing_library.startsWith(runtime_root.path() + QLatin1Char('/')))
      return fail("Storage proof must only change the isolated test library");
    const auto next_library = runtime_root.filePath(QStringLiteral("new-import-library"));
    const auto next_backup = runtime_root.filePath(QStringLiteral("new-import-backup"));
    if (!app_shell.setPhotoLibraryLocation(next_library) || !catalog_assets.importPreflightBusy() ||
        workspace->property("canConfirmImport").toBool() ||
        !wait_for_condition([&]() { return !catalog_assets.importPreflightBusy(); }) ||
        catalog_assets.importPreflightResult().value("photoLibraryRoot").toString() != next_library ||
        !app_shell.setOriginalsBackupLocation(next_backup) ||
        !wait_for_condition([&]() { return !catalog_assets.importPreflightBusy(); }) ||
        !catalog_assets.importPreflightResult().value("backupEnabled").toBool())
      return fail("Storage changes should invalidate old confirmation and publish a new destination/backup scan");
    if (!send_activation_key(find_named_item(import_root, QStringLiteral("importStorageDoneButton"))))
      return fail("Storage settings should return to the reviewed import");
    const auto managed_destination = catalog_assets.importPreflightResult().value("destinationPath").toString();
    if (!QDir().mkpath(managed_destination) ||
        !QFile::copy(fixture_paths[1].toString(), QDir(managed_destination).filePath(QFileInfo(fixture_paths[1].toString()).fileName())))
      return fail("Duplicate proof should create a real existing destination file");
    if (!QMetaObject::invokeMethod(workspace, "refreshImportPreflight") ||
        !wait_for_condition([&]() { return !catalog_assets.importPreflightBusy(); }) ||
        catalog_assets.importPreflightResult().value("duplicateCount").toInt() != 1 ||
        catalog_assets.importPreflightResult().value("duplicateMatches").toList().size() != 1 ||
        !send_activation_key(find_named_item(import_root, QStringLiteral("importReviewDuplicatesButton"))) ||
        !wait_for_condition([&]() {
          auto* incoming = find_named_object(import_root, QStringLiteral("importDuplicateIncomingImage"));
          auto* existing = find_named_object(import_root, QStringLiteral("importDuplicateExistingImage"));
          return incoming && existing && incoming->property("status").toInt() == 1 && existing->property("status").toInt() == 1;
        }, 15000) ||
        !capture_import_state(QStringLiteral("import-duplicate-comparison"), 1280, 760,
            {QStringLiteral("importDuplicateReviewDialog"), QStringLiteral("importDuplicateReplaceButtonInDialog")}))
      return fail("Existing file matches should open a visual source/destination comparison");
    if (!send_activation_key(find_named_item(import_root, QStringLiteral("importDuplicateReplaceButtonInDialog"))) ||
        !wait_for_condition([&]() { return !catalog_assets.importPreflightBusy(); }) ||
        workspace->property("duplicatePolicy").toString() != QStringLiteral("replace") ||
        catalog_assets.importPreflightResult().value("importableCount").toInt() != 3)
      return fail("Duplicate comparison should apply explicit Replace policy and refresh readiness");
    workspace->setProperty("importProjectChooserOpen", false);

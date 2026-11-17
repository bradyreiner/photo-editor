  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_EXPORT_PREFLIGHT")) {
    app_shell.completeFirstRunExperience();
    catalog_assets.setInteractiveRenderingActive(true);
    const auto source = runtime_root.filePath(QStringLiteral("export-fixture.png"));
    const auto destination = runtime_root.filePath(QStringLiteral("export-output"));
    if (!write_overlay_visual_fixture(source, QStringLiteral("saturated-small")) || !QDir().mkpath(destination))
      return fail("Export UI fixture unavailable");
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_EXPORT_POLISH_PROOF_DIR")) {
      QImage photo(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/fixtures/creative-tools/landscape-mountain-source.jpg"));
      if (photo.isNull() || !photo.save(source)) return fail("Export review photo unavailable");
    }
    const auto asset = make_asset(QStringLiteral("export-fixture"), source);
    catalog_assets.replaceAssetsForTesting({asset});
    catalog_assets.selectIndex(0);
    if (!catalog_assets.applyThumbnailResultForTesting(asset.asset_id, source,
        catalog_assets.assetById(asset.asset_id).value("adjustmentPatchJson").toString(), QUrl::fromLocalFile(source).toString()))
      return fail("Export photo preview could not be published");
    const auto warnings = std::make_shared<QStringList>();
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [warnings](const QList<QQmlError>& errors) {
      for (const auto& error : errors)
        if (error.toString().contains(QStringLiteral("ExportView.qml"))) warnings->append(error.toString());
    });
    engine.load(QUrl::fromLocalFile(main_qml_path));
    process_layout_events(8);
    if (engine.rootObjects().isEmpty()) return fail("Export UI proof could not load Main.qml");
    auto* export_root = engine.rootObjects().constFirst();
    export_root->setProperty("currentWorkspace", export_root->property("exportWorkspaceIndex"));
    process_layout_events(12);
    auto* workspace = find_named_object(export_root, QStringLiteral("exportStudioWorkspaceContent"));
    if (!workspace) return fail("Export UI proof could not load ExportView");
    workspace->setProperty("deliveryDestinationPath", destination);
    workspace->setProperty("deliveryPresetIndex", 7);
    auto* start = find_named_object(export_root, QStringLiteral("exportCurrentSplitButton"));
    if (!export_jobs.preflightBusy() || !start || !start->property("enabled").toBool())
      return fail("Single export should remain available while submission can perform its own fresh checks");
    QElapsedTimer timer;
    timer.start();
    while (export_jobs.preflightBusy() && timer.elapsed() < 10000) process_layout_events(1);
    process_layout_events(4);
    if (export_jobs.preflightBusy() || !export_jobs.singlePreflight().value(QStringLiteral("ok")).toBool() ||
        !start->property("enabled").toBool())
      return fail("Export report did not publish or enable the ready action");
    const auto polish_dir = qEnvironmentVariable("PHOTO_EDITOR_EXPORT_POLISH_PROOF_DIR");
    if (!polish_dir.isEmpty()) {
      if (!QDir().mkpath(polish_dir)) return fail("Export proof directory unavailable");
      auto* proof_window = qobject_cast<QQuickWindow*>(export_root);
      auto* advanced = find_named_item(export_root, "exportAdvancedSection");
      if (!proof_window || !advanced || advanced->property("expanded").toBool())
        return fail("Export should start with advanced controls disclosed on request");
      proof_window->show();
      auto* preview = find_named_item(export_root, "exportPhotoPreview");
      if (!preview || !wait_for_condition([&] { return preview->property("status").toInt() == 1; }, 10000))
        return fail("Export proof must decode its real photograph before capture");
      QJsonArray captures;
      const QList<QSize> sizes{{1440, 900}, {1280, 760}, {900, 700}};
      for (const auto& size : sizes) {
        if (!capture_visual_proof(proof_window, export_root, polish_dir,
            QStringLiteral("export-ready-%1").arg(size.width()), size.width(), size.height(),
            {QStringLiteral("exportConfigurationPanel"), QStringLiteral("exportPhotoPreview"),
             QStringLiteral("exportDestinationNameLabel"), QStringLiteral("exportCurrentSplitButton")}, &captures))
          return fail("Export should keep preview, destination and action usable at every size");
      }
      advanced->forceActiveFocus();
      QTest::keyClick(proof_window, Qt::Key_Return);
      process_layout_events(8);
      if (!advanced->property("expanded").toBool() ||
          !item_visible(find_named_item(export_root, "exportFilenameTemplateField")))
        return fail("Export advanced controls should open with the keyboard");
      if (!capture_visual_proof(proof_window, export_root, polish_dir,
          "export-advanced", 1280, 800, {QStringLiteral("exportAdvancedSection"),
          QStringLiteral("exportFilenameTemplateField"), QStringLiteral("exportCurrentSplitButton")}, &captures))
        return fail("Export advanced settings should remain readable");
      advanced->setProperty("expanded", false);
      if (!wait_for_condition([&] { return !job_queue.loading(); }, 5000))
        return fail("Export history fixture did not settle");
      auto active_job = make_persistent_job("Northern light — client delivery.jpg", "asset_export", "Running");
      active_job.progress = 42;
      active_job.preset_name = "Web Proof";
      active_job.destination_path = destination;
      photo_editor::desktop::JobQueueModelTestAccess::setHistoryFixture(job_queue, {active_job});
      job_queue.refresh();
      if (!wait_for_condition([&] { return !job_queue.loading() && job_queue.jobCount() == 1; }, 5000))
        return fail("Export active row fixture did not load");
      process_layout_events(8);
      if (!capture_visual_proof(proof_window, export_root, polish_dir,
          "export-running", 1280, 800, {QStringLiteral("exportQueuePanel"),
          QStringLiteral("exportDeliveryProgressStrip"), QStringLiteral("exportCurrentSplitButton")}, &captures))
        return fail("Export progress should stay compact and leave setup accessible");
      active_job.status = "Failed";
      active_job.error_message = "Output folder is unavailable. Choose a folder and retry.";
      photo_editor::desktop::JobQueueModelTestAccess::setHistoryFixture(job_queue, {active_job});
      job_queue.refresh();
      if (!wait_for_condition([&] { return !job_queue.loading(); }, 5000)) return fail("Export failure fixture did not load");
      process_layout_events(8);
      if (!capture_visual_proof(proof_window, export_root, polish_dir,
          "export-failed", 900, 700, {QStringLiteral("exportQueuePanel"),
          QStringLiteral("exportReviewFailedJobsButton")}, &captures))
        return fail("Export failures should keep retry/review controls visible at compact size");
      photo_editor::desktop::JobQueueModelTestAccess::setHistoryFixture(job_queue, {});
      job_queue.refresh();
      if (!wait_for_condition([&] { return !job_queue.loading(); }, 5000)) return fail("Export fixture history did not clear");
      QFile receipt(QDir(polish_dir).filePath("receipt.json"));
      if (!receipt.open(QIODevice::WriteOnly)) return fail("Export proof receipt unavailable");
      receipt.write(QJsonDocument(QJsonObject{{"captures", captures}, {"keyboardDisclosure", true},
          {"preflightAndSubmissionChecksPassed", true}, {"isolatedCatalog", true}}).toJson());
    }
    for (int index = 0; index < 10; ++index)
      workspace->setProperty("deliveryDestinationPath", destination + QStringLiteral("-%1").arg(index));
    if (!export_jobs.preflightBusy() || !start->property("enabled").toBool())
      return fail("Destination typing should refresh review without disabling fresh-check submission");
    timer.restart();
    while (export_jobs.preflightBusy() && timer.elapsed() < 10000) process_layout_events(1);
    if (!export_jobs.singlePreflight().value(QStringLiteral("outputPath")).toString().contains(QStringLiteral("export-output-9")))
      return fail("Export UI published a stale destination");
    workspace->setProperty("deliveryDestinationPath", source); // Existing file is not an output directory.
    timer.restart();
    while (export_jobs.preflightBusy() && timer.elapsed() < 10000) process_layout_events(1);
    process_layout_events(2);
    if (export_jobs.singlePreflight().value(QStringLiteral("ok")).toBool() || start->property("enabled").toBool())
      return fail("Invalid destination did not block export");
    workspace->setProperty("deliveryDestinationPath", destination);
    QVariant alternatives;
    if (find_named_object(export_root, QStringLiteral("exportCurrentSplitButtonMenuAction")) ||
        !QMetaObject::invokeMethod(workspace, "exportActionMenuItems", Q_RETURN_ARG(QVariant, alternatives)) ||
        !alternatives.toList().isEmpty())
      return fail("Single-photo export should have one action without a duplicate command menu");
    catalog_assets.replaceAssetsForTesting({asset, make_asset(QStringLiteral("export-second"), source)});
    catalog_assets.selectAll();
    if (!wait_for_condition([&] { return workspace->property("hasBatchSelection").toBool(); }) ||
        !QMetaObject::invokeMethod(workspace, "exportActionMenuItems", Q_RETURN_ARG(QVariant, alternatives)))
      return fail("Selecting multiple photos should expose batch export");
    const auto alternative_rows = alternatives.toList();
    if (alternative_rows.size() != 1 ||
        alternative_rows.first().toMap().value("key").toString() != QStringLiteral("queue-current"))
      return fail("The batch menu should offer only the single-photo alternative");
    auto* batch = find_named_object(workspace, QStringLiteral("exportBatchSplitButton"));
    if (!batch || !item_visible(qobject_cast<QQuickItem*>(batch)))
      return fail("Batch export must retain a visible primary action");
    catalog_assets.selectIndex(0);
    workspace->setProperty("visible", false);
    if (export_jobs.preflightBusy() || !export_jobs.singlePreflight().isEmpty())
      return fail("Hidden Export view did not cancel review work");
    const QVariantList missing_batch{QVariantMap{{QStringLiteral("assetId"), QStringLiteral("missing")},
        {QStringLiteral("mediaType"), QStringLiteral("photo")},
        {QStringLiteral("filePath"), runtime_root.filePath(QStringLiteral("missing-export.png"))}}};
    if (!export_jobs.queueBatchExportAsync(missing_batch, QStringLiteral("Web Proof"), destination,
                                         {}, 0, 0, 0, 0))
      return fail("Export close-guard fixture did not start");
    auto* window = qobject_cast<QQuickWindow*>(export_root);
    if (!window || window->close()) return fail("Window closed during accepted batch validation");
    timer.restart();
    while (export_jobs.batchSubmissionBusy() && timer.elapsed() < 10000) process_layout_events(1);
    if (export_jobs.batchSubmissionBusy() || export_jobs.jobCount() != 0)
      return fail("Missing export fixture did not finish without admitting jobs");
    if (!warnings->isEmpty()) {
      std::cerr << warnings->join(QLatin1Char('\n')).toStdString() << '\n';
      return fail("Export UI produced QML warnings");
    }
    return EXIT_SUCCESS;
  }

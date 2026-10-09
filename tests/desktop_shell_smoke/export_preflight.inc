  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_EXPORT_PREFLIGHT")) {
    app_shell.completeFirstRunExperience();
    catalog_assets.setInteractiveRenderingActive(true);
    const auto source = runtime_root.filePath(QStringLiteral("export-fixture.png"));
    const auto destination = runtime_root.filePath(QStringLiteral("export-output"));
    if (!write_overlay_visual_fixture(source, QStringLiteral("saturated-small")) || !QDir().mkpath(destination))
      return fail("Export UI fixture unavailable");
    const auto asset = make_asset(QStringLiteral("export-fixture"), source);
    catalog_assets.replaceAssetsForTesting({asset});
    catalog_assets.selectIndex(0);
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
    if (!export_jobs.preflightBusy() || !start || start->property("primaryEnabled").toBool())
      return fail("Export start remained enabled during pending checks");
    QElapsedTimer timer;
    timer.start();
    while (export_jobs.preflightBusy() && timer.elapsed() < 10000) process_layout_events(1);
    process_layout_events(4);
    if (export_jobs.preflightBusy() || !export_jobs.singlePreflight().value(QStringLiteral("ok")).toBool() ||
        !start->property("primaryEnabled").toBool())
      return fail("Export report did not publish or enable the ready action");
    for (int index = 0; index < 10; ++index)
      workspace->setProperty("deliveryDestinationPath", destination + QStringLiteral("-%1").arg(index));
    if (!export_jobs.preflightBusy() || start->property("primaryEnabled").toBool())
      return fail("Destination typing retained stale export readiness");
    timer.restart();
    while (export_jobs.preflightBusy() && timer.elapsed() < 10000) process_layout_events(1);
    if (!export_jobs.singlePreflight().value(QStringLiteral("outputPath")).toString().contains(QStringLiteral("export-output-9")))
      return fail("Export UI published a stale destination");
    workspace->setProperty("deliveryDestinationPath", source); // Existing file is not an output directory.
    timer.restart();
    while (export_jobs.preflightBusy() && timer.elapsed() < 10000) process_layout_events(1);
    process_layout_events(2);
    if (export_jobs.singlePreflight().value(QStringLiteral("ok")).toBool() || start->property("primaryEnabled").toBool())
      return fail("Invalid destination did not block export");
    workspace->setProperty("deliveryDestinationPath", destination);
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

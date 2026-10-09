    const auto source_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(
        QStringLiteral("docs/evidence/creative-tools/optical-diffusion/full-resolution-review-release/contact-sheet-previews/landscape-mountain-source.jpg"));
    if (QImage(QStringLiteral(":/photo-editor/presets/mountain-preview.jpg")).isNull())
      return fail("Preset library should include its photographic thumbnail reference");
    QImage photograph(source_path);
    if (photograph.isNull()) return fail("Preset library proof requires its photographic fixture");
    if (photograph.width() < 1600) photograph = photograph.scaledToWidth(1600, Qt::SmoothTransformation);
    const auto photo_path = runtime_root.filePath(QStringLiteral("preset-library-photo.png"));
    if (!photograph.save(photo_path)) return fail("Preset library proof should copy its photo fixture");
    catalog_assets.replaceAssetsForTesting({make_asset(QStringLiteral("preset-library-photo"), photo_path)});
    catalog_assets.selectIndex(0);
    engine.load(QUrl::fromLocalFile(main_qml_path));
    if (engine.rootObjects().isEmpty()) return fail("Preset library proof should load Main.qml");
    auto* proof_root = engine.rootObjects().constFirst();
    auto* proof_window = qobject_cast<QQuickWindow*>(proof_root);
    if (!proof_window) return fail("Preset library proof requires a real Qt Quick window");
    proof_root->setProperty("currentWorkspace", proof_root->property("editWorkspaceIndex"));
    QObject* edit = nullptr;
    if (!wait_for_condition([&] {
      edit = find_named_object(proof_root, QStringLiteral("editStudioWorkspace"));
      return edit && edit->property("pageContentReady").toBool() &&
          !proof_root->property("workspaceTransitionActive").toBool() && editor_preview.hasAsset() &&
          !editor_preview.displayPreviewSource().isEmpty() && !editor_preview.processing();
    }, 15000)) return fail("Preset library proof should reach the ready editor");
    edit->setProperty("editTool", QStringLiteral("adjust"));
    edit->setProperty("editInspectorTab", QStringLiteral("presets"));
    const auto proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
        ? qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR")
        : runtime_root.filePath(QStringLiteral("preset-library-proof"));
    if (!QDir().mkpath(proof_dir)) return fail("Preset library proof should create its capture directory");
    QJsonArray manifest;
    const auto settle_scene = [&] {
      QElapsedTimer elapsed;
      elapsed.start();
      return wait_for_condition([&] { return elapsed.elapsed() >= 350 && !editor_preview.processing(); }, 10000);
    };
    const auto creator_visible = [&] {
      auto* dialog = find_named_object(edit, QStringLiteral("studioPresetCreateDialog"));
      return dialog && dialog->property("visible").toBool();
    };
    const auto invoke_creator = [&](bool overwrite) {
      QVariant result;
      return QMetaObject::invokeMethod(edit, "submitStudioPresetCreator", Q_RETURN_ARG(QVariant, result),
          Q_ARG(QVariant, overwrite)) && result.toBool();
    };
    const auto library_count = [&] {
      auto* list = find_named_object(edit, QStringLiteral("studioPresetLibraryList"));
      return list ? list->property("count").toInt() : -1;
    };
    edit->setProperty("studioPresetLibraryTab", QStringLiteral("My Presets"));
    if (!wait_for_condition([&] { return library_count() == 0; }))
      return fail("My Presets should start with a genuinely empty library");
    edit->setProperty("studioPresetLibraryTab", QStringLiteral("All"));
    for (const auto& size : {QSize(1280, 800), QSize(900, 700)}) {
      proof_root->setProperty("width", size.width());
      proof_root->setProperty("height", size.height());
      if (!settle_scene()) return fail("Library should settle before capture");
      if (!capture_visual_proof(proof_window, proof_root, proof_dir,
            QStringLiteral("preset-library"), size.width(), size.height(),
            {QStringLiteral("editInspectorPanel"), QStringLiteral("studioPresetSearchField")}, &manifest))
        return fail("Preset library proof should capture the photo and preset library");
      if (!QMetaObject::invokeMethod(edit, "openStudioPresetCreator"))
        return fail("Preset creator should open from the library");
      if (!settle_scene()) return fail("Creator should settle before capture");
      if (!capture_visual_proof(proof_window, proof_root, proof_dir,
            QStringLiteral("preset-creator"), size.width(), size.height(),
            {QStringLiteral("studioPresetNameField"), QStringLiteral("studioSavePresetButton")}, &manifest))
        return fail("Preset creator should fit the editing window");
      auto* cancel = find_named_object(edit, QStringLiteral("studioPresetCreateCancelButton"));
      if (!cancel || !QMetaObject::invokeMethod(cancel, "clicked"))
        return fail("Preset creator should cancel without saving");
      if (!wait_for_condition([&] { return !creator_visible(); }))
        return fail("Creator should finish closing before the next library capture");
    }
    edit->setProperty("studioPresetLibraryTab", QStringLiteral("My Presets"));
    if (!QMetaObject::invokeMethod(edit, "openStudioPresetCreator")) return fail("Creator should reopen");
    const auto custom_name = QStringLiteral("Soft Mountain Light");
    edit->setProperty("studioPresetDraftName", custom_name);
    edit->setProperty("studioPresetDraftPack", QStringLiteral("My Landscape Looks"));
    if (!invoke_creator(false) || !editor_preview.isUserPreset(custom_name))
      return fail("Creator should save a custom preset");
    if (!wait_for_condition([&] { return library_count() == 2; }))
      return fail("My Presets should refresh immediately after saving, including its pack header");
    if (invoke_creator(false) || !edit->property("studioPresetCreateFeedbackError").toBool())
      return fail("Save as new should show a duplicate-name error and preserve the existing preset");
    if (!settle_scene()) return fail("Duplicate feedback should settle before capture");
    if (!capture_visual_proof(proof_window, proof_root, proof_dir,
          QStringLiteral("preset-duplicate-error"), 900, 700,
          {QStringLiteral("studioPresetNameField"), QStringLiteral("studioUpdateSelectedPresetButton")}, &manifest))
      return fail("Duplicate name feedback should fit the creator");
    if (!invoke_creator(true)) return fail("An explicit update should replace a custom preset");
    auto* cancel = find_named_object(edit, QStringLiteral("studioPresetCreateCancelButton"));
    if (!cancel || !QMetaObject::invokeMethod(cancel, "clicked")) return fail("Creator should close after update");
    if (!wait_for_condition([&] { return !creator_visible(); })) return fail("Creator should finish closing after update");
    edit->setProperty("studioPresetLibraryTab", QStringLiteral("Favorites"));
    if (!wait_for_condition([&] { return library_count() == 0; })) return fail("Favorites should start empty");
    editor_preview.setPresetFavorite(custom_name, true);
    if (!wait_for_condition([&] { return library_count() == 2; })) return fail("Favorites should refresh immediately");
    if (!settle_scene()) return fail("Favorite card should settle before capture");
    if (!capture_visual_proof(proof_window, proof_root, proof_dir,
          QStringLiteral("preset-custom-favorite"), 900, 700,
          {QStringLiteral("studioPresetLibraryCard"), QStringLiteral("studioPresetSelectedDetail")}, &manifest))
      return fail("The custom preset should appear in the favorites library");
    editor_preview.setPresetFavorite(custom_name, false);
    if (!wait_for_condition([&] { return library_count() == 0; })) return fail("Unfavoriting should refresh immediately");
    QFile receipt(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!receipt.open(QIODevice::WriteOnly) || receipt.write(QJsonDocument(manifest).toJson()) < 0)
      return fail("Preset library proof should save its capture manifest");
    std::cout << "Preset library photographic screenshots captured\n";
    return EXIT_SUCCESS;

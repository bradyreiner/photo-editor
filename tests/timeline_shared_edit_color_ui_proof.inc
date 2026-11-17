    root->setProperty("width", 1440);
    root->setProperty("height", 900);
    root_window->setVisible(true);
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    const auto video_source = fixture_dir.filePath("shared-edit-video.mp4");
    QProcess encode_color_source;
    encode_color_source.start(ffmpeg, {"-v", "error", "-nostdin", "-loop", "1", "-framerate", "12",
        "-i", source_path, "-vf", "scale=320:240:force_original_aspect_ratio=increase,crop=320:240",
        "-t", "1", "-c:v", "libx264", "-pix_fmt", "yuv420p", video_source});
    if (ffmpeg.isEmpty() || !encode_color_source.waitForFinished(20000) || encode_color_source.exitCode() != 0)
      return fail("Create a real video for the shared Timeline UI.");
    timeline_document.createTimelineWithSettings({{"timelineName", "Shared Edit color UI"},
        {"width", 320}, {"height", 240}, {"frameRate", 12.0}});
    timeline_document.addClipFromGallery("shared-color-ui-source", video_source, "Color source", 1.0, {});
    if (!timeline_document.addAdjustmentClip(1.0)) return fail("Shared color UI requires an adjustment clip.");
    const auto grade_clip_id = timeline_document.currentClip().value("clipId").toString();
    const auto original_photo_asset = editor_preview.assetId();
    QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_ARG(QVariant, root->property("timelineWorkspaceIndex")));
    if (!wait_for_condition([&] {
          auto* workspace = active_workspace(root);
          return workspace && workspace->objectName() == "timelineWorkspace" &&
              workspace->property("panelDockingReady").toBool() &&
              workspace->property("workspacePanelSettingsInitialized").toBool() &&
              !root->property("workspaceTransitionActive").toBool();
        }, 20000, "shared-color-timeline-route")) return fail("Open Timeline for shared Edit controls.");
    auto* workspace = active_workspace(root);
    if (!QMetaObject::invokeMethod(workspace, "showTimelinePanel", Q_ARG(QVariant, QVariant("color"))))
      return fail("Expose the Timeline Color dock.");
    QQuickItem* shared_inspector = nullptr;
    if (!wait_for_condition([&] {
          shared_inspector = workspace->findChild<QQuickItem*>("timelineSharedEditInspector");
          return shared_inspector && shared_inspector->isVisible() && timeline_color_editor->ready();
        }, 25000, "shared-edit-inspector-ready")) {
      std::cerr << timeline_color_editor->error().toStdString() << '\n';
      for (const auto* name : {"timelineColorConsolePanelLoader", "timelineColorConsole", "timelineSharedEditColorLoader"}) {
        if (auto* item = workspace->findChild<QObject*>(name))
          std::cerr << name << " status=" << item->property("status").toInt()
                    << " active=" << item->property("active").toBool()
                    << " visible=" << item->property("visible").toBool() << '\n';
      }
      QJsonArray failure_captures;
      save_capture_if_requested(root_window, capture_dir, "timeline-shared-edit-failure", &failure_captures);
      return fail("The actual Timeline must load the shared Edit inspector.");
    }
    for (const auto* name : {"whiteBalanceInspectorGroup", "lightInspectorGroup", "toneCurveInspectorGroup",
         "levelsInspectorGroup", "colorGradingInspectorGroup", "presenceInspectorGroup",
         "detailInspectorGroup", "lensInspectorGroup", "advancedColorInspectorGroup"}) {
      if (!shared_inspector->findChild<QObject*>(QString::fromLatin1(name))) {
        std::cerr << name << '\n';
        return fail("Timeline must instantiate every shared Edit color section.");
      }
    }
    QJsonArray captures;
    save_capture_if_requested(root_window, capture_dir, "timeline-shared-edit-adjustments", &captures);
    const auto before = timeline_document.colorGradeRecipeJsonForClip(grade_clip_id);
    if (!QMetaObject::invokeMethod(shared_inspector, "setPrimaryAdjustmentValue",
          Q_ARG(QVariant, QVariant("brightness")), Q_ARG(QVariant, QVariant(23.0))) ||
        !wait_for_condition([&] {
          return photo_editor::desktop::parse_edit_recipe_json(
              timeline_document.colorGradeRecipeJsonForClip(grade_clip_id)).brightness == 23.0;
        }, 10000, "shared-ui-brightness-readback") || editor_preview.assetId() != original_photo_asset)
      return fail("The shared Timeline UI must edit its clip without replacing the Library photo.");
    timeline_document.undo();
    if (!wait_for_condition([&] {
          return timeline_document.colorGradeRecipeJsonForClip(grade_clip_id) == before;
        }, 10000, "shared-ui-timeline-undo")) return fail("Timeline Undo must restore the shared UI edit.");
    auto* white_balance_sample = shared_inspector->findChild<QQuickItem*>("whiteBalanceSampleNeutralButton");
    if (!white_balance_sample || !white_balance_sample->isVisible()) return fail("Expose Edit's neutral eyedropper in Timeline.");
    QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier,
        white_balance_sample->mapToScene(white_balance_sample->boundingRect().center()).toPoint());
    auto* program_sampler = workspace->findChild<QQuickItem*>("timelineProgramColorSampler");
    if (!program_sampler || !wait_for_condition([&] { return program_sampler->isVisible(); }, 3000))
      return fail("The shared eyedropper must arm a pointer target over Program.");
    QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier,
        program_sampler->mapToScene(program_sampler->boundingRect().center()).toPoint());
    if (!wait_for_condition([&] {
          return timeline_color_editor->samplingTarget().isEmpty() &&
              timeline_document.colorGradeRecipeJsonForClip(grade_clip_id) != before;
        }, 15000, "shared-program-neutral-sampling")) return fail("Pointer sampling in Program must update the selected clip.");
    timeline_document.undo();
    if (!wait_for_condition([&] { return timeline_document.colorGradeRecipeJsonForClip(grade_clip_id) == before; }, 5000))
      return fail("The Program eyedropper must participate in Timeline Undo.");
    auto* filters_tab = shared_inspector->findChild<QQuickItem*>("filtersInspectorTabButton");
    if (!filters_tab || !filters_tab->isVisible()) return fail("Expose the same Effects tab as Edit.");
    root_window->requestActivate();
    QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier,
        filters_tab->mapToScene(filters_tab->boundingRect().center()).toPoint());
    if (!wait_for_condition([&] {
          auto* add = shared_inspector->findChild<QQuickItem*>("photoAddEffectButton");
          return add && add->isVisible() && add->isEnabled();
        }, 10000, "shared-photo-effects-ready"))
      return fail("The Timeline Effects tab must use the actual PhotoEffectStack browser.");
    save_capture_if_requested(root_window, capture_dir, "timeline-shared-edit-effects", &captures);
    auto* add_effect = shared_inspector->findChild<QQuickItem*>("photoAddEffectButton");
    QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier,
        add_effect->mapToScene(add_effect->boundingRect().center()).toPoint());
    if (!wait_for_condition([&] {
          auto* picker = shared_inspector->findChild<QObject*>("photoEffectPicker");
          return picker && picker->property("opened").toBool();
        }, 10000, "shared-effect-gallery-open")) return fail("Open Edit's photographic effects gallery in Timeline.");
    if (!wait_for_condition([&] {
          auto* example = find_named_object(root, "photoEffectChoice_crosshatch");
          return example && example->property("previewReady").toBool();
        }, 15000, "shared-effect-gallery-photograph")) {
      auto* example = find_named_object(root, "photoEffectChoice_crosshatch");
      std::cerr << "example=" << (example != nullptr)
                << " source=" << (example ? example->property("thumbnailSource").toString().toStdString() : "")
                << " status=" << (example ? example->property("thumbnailStatus").toInt() : -1) << '\n';
      save_capture_if_requested(root_window, capture_dir, "timeline-shared-edit-gallery-failure", &captures);
      return fail("The Timeline gallery must display the same rendered photographic examples as Edit.");
    }
    save_capture_if_requested(root_window, capture_dir, "timeline-shared-edit-effects-gallery", &captures);
    auto* effect_picker = shared_inspector->findChild<QObject*>("photoEffectPicker");
    QMetaObject::invokeMethod(effect_picker, "close");
    if (!wait_for_condition([&] { return !effect_picker->property("visible").toBool(); },
        5000, "shared-effect-gallery-closed")) return fail("Close the shared effect gallery before changing tabs.");
    auto* presets_tab = shared_inspector->findChild<QQuickItem*>("presetsInspectorTabButton");
    QTest::mouseClick(root_window, Qt::LeftButton, Qt::NoModifier,
        presets_tab->mapToScene(presets_tab->boundingRect().center()).toPoint());
    if (!wait_for_condition([&] { return shared_inspector->property("editInspectorTab").toString() == "presets"; },
        10000, "shared-presets-open")) return fail("Open the same Presets UI in Timeline.");
    save_capture_if_requested(root_window, capture_dir, "timeline-shared-edit-presets", &captures);
    std::cout << QJsonDocument(QJsonObject{{"schema", "timeline-shared-edit-ui:v1"}, {"ok", true},
        {"shared_edit_inspector", true}, {"clip_owned_recipe", true}, {"timeline_undo", true},
        {"library_photo_unchanged", true}, {"shared_effect_browser", true}, {"shared_presets", true},
        {"program_pointer_sampling", true},
        {"captures", captures}}).toJson().constData() << std::flush;
    return EXIT_SUCCESS;
